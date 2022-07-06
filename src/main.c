/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2022 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#define __USE_XOPEN
#define __USE_XOPEN_EXTENDED
#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <regex.h>
#include <ftw.h>
#include <err.h>

#ifdef LINKED
#include <ctype.h>
#else
#include "ctype.h"
#endif

#include "flexarr.h"
#include "hgrep.h"

#define F_RECURSIVE 0x1
#define F_EXTENDED 0x2
#define F_ICASE 0x4
#define F_FAST 0x8

#define BUFF_INC_VALUE (1<<15)

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#define PATTERN_SIZE_INC (1<<5)
#define PASSED_INC (1<<10)

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGHT(x) (sizeof(x)/(sizeof(*x)))

typedef struct {
  hgrep_pattern p;
  uchar posx;
  uchar posy;
} auxiliary_pattern;

char *argv0;
flexarr *patterns = NULL;

uint settings = 0;
uchar hflags = 0;
int nftwflags = FTW_PHYS;
FILE* outfile;

static int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

static void
die(const char *s, ...)
{
  va_list ap;
  va_start(ap,s);
  vfprintf(stderr,s,ap);
  va_end(ap);
  fputc('\n',stderr);
  exit(1);
}

static void
usage()
{
  die("Usage: %s [OPTION]... PATTERNS [FILE]...\n"\
      "Search for PATTERNS in each html FILE.\n"\
      "Example: %s -i 'div +id; a +href=\".*\\.org\"' index.html\n\n"\
      "Options:\n"\
      "  -i\t\t\tignore case distinctions in patterns and data\n"\
      "  -l\t\t\tlist structure of FILE\n"\
      "  -o FILE\t\tchange output to a FILE instead of stdout\n"\
      "  -f FILE\t\tobtain PATTERNS from FILE\n"\
      "  -E\t\t\tuse extended regular expressions\n"\
      "  -H\t\t\tfollow symlinks\n"\
      "  -r\t\t\tread all files under each directory, recursively\n"\
      "  -R\t\t\tlikewise but follow all symlinks\n"\
      "  -F\t\t\tenter fast and low memory consumption mode\n"\
      "  -h\t\t\tshow help\n"\
      "  -v\t\t\tshow version\n\n"\
      "When FILE isn't specified, FILE will become standard input.",argv0,argv0,argv0);
}

static flexarr *
patterns_split(char *src, size_t *pos, size_t s)
{
  if (s == 0)
    return NULL;
  size_t tpos = 0;
  if (pos == NULL)
    pos = &tpos;
  flexarr *ret = flexarr_init(sizeof(auxiliary_pattern),PATTERN_SIZE_INC);
  auxiliary_pattern apattern;
  size_t patternl=0;
  uchar posx=0,posy=0;
  size_t i = *pos;
  for (size_t j; i < s; i++) {
    j = i;
    apattern.posx = posx;
    apattern.posy = posy;

    while (i < s) {
      if (src[i] == '"') {
        i++;
        while (i < s && src[i] != '"') {
          if (src[i] == '\\')
            i++;
          i++;
        }
        if (src[i] == '"')
          i++;
      }
      if (src[i] == ',') {
        posy = 0;
        posx++;
        patternl = i-j;
        i++;
        break;
      }
      if (src[i] == ';' || src[i] == '|') {
        posy++;
        patternl = i-j;
        i++;
        break;
      }
      i++;
      patternl = i-j;
    }
    hgrep_pcomp(src+j,patternl,&apattern.p,0);
    memcpy(flexarr_inc(ret),&apattern,sizeof(apattern));

    while_is(isspace,src,i,s);
    i--;
  }
  *pos = i;
  flexarr_clearb(ret);
  return ret;
}

#define print_or_add(x,y) { \
  if (hgrep_match(&nodes[x],y)) { \
  if (dest) { *(size_t*)flexarr_inc(dest) = x; } \
  else if ((y)->format.b) hgrep_printf(outfile,(y)->format.b,(y)->format.s,&nodes[x],hg->data); \
  else hgrep_print(outfile,&nodes[x]); }}

static void
first_match(FILE *outfile, hgrep *hg, hgrep_pattern *pattern, flexarr *dest)
{
  hgrep_node *nodes = hg->nodes;
  size_t nodesl = hg->nodesl;
  for (size_t i = 0; i < nodesl; i++) {
    print_or_add(i,pattern);
  }
}


static void
pattern_exec(FILE *outfile, hgrep *hg, auxiliary_pattern *pattern, flexarr *source, flexarr *dest)
{
  if (source->size == 0) {
    first_match(outfile,hg,&pattern->p,dest);
    return;
  }
  
  ushort lvl;
  hgrep_node *nodes = hg->nodes;
  size_t current,n;
  for (size_t i = 0; i < source->size; i++) {
    current = ((size_t*)source->v)[i];
    lvl = nodes[current].lvl;
    for (size_t j = 0; j <= nodes[current].child_count; j++) {
      n = current+j;
      nodes[n].lvl -= lvl;
      print_or_add(n,&pattern->p);
      nodes[n].lvl += lvl;
    }
  }
}

static void
row_handle(FILE *outfile, hgrep *hg, flexarr *patterns, size_t *pos, flexarr *buf[2])
{
  auxiliary_pattern *fl = (auxiliary_pattern*)patterns->v;
  for (; *pos < patterns->size; (*pos)++) {
    flexarr *d = (*pos == patterns->size-1 || fl[*pos].posx != fl[*pos+1].posx) ? NULL : buf[1];
    pattern_exec(outfile,hg,&fl[*pos],buf[0],d);
    if (d == NULL || !d->size)
      break;
    buf[0]->size = 0;
    flexarr *tmp = buf[0];
    buf[0] = buf[1];
    buf[1] = tmp;
  }
  buf[0]->size = 0;
  buf[1]->size = 0;
}

static void
columns_handle(hgrep *hg, flexarr *patterns)
{
  flexarr *buf1=flexarr_init(sizeof(size_t),PASSED_INC),
    *buf2=flexarr_init(sizeof(size_t),PASSED_INC);
  for (size_t i = 0; i < patterns->size; i++)
    row_handle(outfile,hg,patterns,&i,(flexarr*[]){buf1,buf2});
  flexarr_free(buf1);
  flexarr_free(buf2);
}

static void
patterns_exec_fast(char *f, size_t s, const uchar inpipe)
{
  for (size_t i = 0; i < patterns->size; i++)
    if (((auxiliary_pattern*)patterns->v)[i].posx)
      exit(1);
  FILE *t = outfile;
  char *ptr;
  size_t fsize;

  for (size_t i = 0; i < patterns->size; i++) {
    outfile = (i == patterns->size-1) ? t : open_memstream(&ptr,&fsize);

    auxiliary_pattern *fl = &((auxiliary_pattern*)patterns->v)[i];
    hgrep_pattern *flv = &fl->p;
    hgrep_init(NULL,f,s,outfile,flv,hflags);
    fflush(outfile);

    if (!inpipe && i == 0) {
      munmap(f,s);
    } else {
      free(f);
    }
    if (i != patterns->size-1)
      fclose(outfile);

    f = ptr;
    s = fsize;
  }
}

static void
patterns_exec(char *f, size_t s, const uchar inpipe)
{
  if (f == NULL || s == 0)
    return;

  if (settings&F_FAST) {
    patterns_exec_fast(f,s,inpipe);
    return;
  }

  hgrep hg;
  hgrep_init(&hg,f,s,NULL,NULL,hflags);
  columns_handle(&hg,patterns);

  hgrep_free(&hg);
}

static void
pipe_to_str(int fd, char **file, size_t *size)
{
  *file = NULL;
  register size_t readbytes = 0;
  *size = 0;

  do {
    *size += readbytes;
    *file = realloc(*file,*size+BUFF_INC_VALUE);
    readbytes = read(fd,*file+*size,BUFF_INC_VALUE);
  } while (readbytes > 0);
  *file = realloc(*file,*size);
}

void
file_handle(const char *f)
{
  int fd;
  struct stat st;
  char *file;

  if (f == NULL) {
    size_t size;
    pipe_to_str(0,&file,&size);
    patterns_exec(file,size,1);
    return;
  }

  if ((fd = open(f,O_RDONLY)) == -1) {
    warn("%s",f);
    return;
  }

  if (fstat(fd,&st) == -1) {
    warn("%s",f);
    close(fd);
    return;
  }

  if ((st.st_mode&S_IFMT) == S_IFDIR) {
    close(fd);
    if (settings&F_RECURSIVE) {
      if (nftw(f,nftw_func,16,nftwflags) == -1)
        warn("%s",f);
    } else
      fprintf(stderr,"%s: -R not specified: omitting directory '%s'\n",argv0,f);
    return;
  }

  file = mmap(NULL,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
  if (file == NULL) {
    warn("%s",f);
    close(fd);
  } else {
    close(fd);
    patterns_exec(file,st.st_size,0);
  }
}

int
nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    file_handle(fpath);

  return 0;
}

void
apatterns_free(flexarr *apatterns)
{
  auxiliary_pattern *a = (auxiliary_pattern*)apatterns->v;
  for (size_t i = 0; i < apatterns->size; i++)
    hgrep_pfree(&a[i].p);
  flexarr_free(apatterns);
}

int
main(int argc, char **argv)
{
  argv0 = argv[0];
  if (argc < 2)
    usage();

  int opt;
  outfile = stdout;

  while ((opt = getopt(argc,argv,"Eilo:f:HrRFvh")) != -1) {
    switch (opt) {
      case 'E': settings |= F_EXTENDED; break;
      case 'i': settings |= F_ICASE; break;
      case 'l': patterns = patterns_split("@p\"%t%I - %s/%p\n\"",NULL,17); break;
      case 'o': {
        outfile = fopen(optarg,"w");
        if (outfile == NULL)
          err(1,"%s",optarg);
        }
        break;
      case 'f': {
        int fd = open(optarg,O_RDONLY);
        if (fd == -1)
          err(1,"%s",optarg);
        size_t s;
        char *p;
        pipe_to_str(fd,&p,&s);
        close(fd);
        patterns = patterns_split(p,NULL,s);
        free(p);
        }
        break;
      case 'H': nftwflags &= ~FTW_PHYS; break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'F': settings |= F_FAST; break;
      case 'v': die(VERSION); break;
      case 'h': usage(); break;
      default: exit(1);
    }
  }

  if (settings&F_EXTENDED)
      hflags |= HGREP_EREGEX;
  if (settings&F_ICASE)
      hflags |= HGREP_ICASE;

  if (!patterns && optind < argc) {
    patterns = patterns_split(argv[optind],NULL,strlen(argv[optind]));
    optind++;
  }
  int g = optind;
  for (; g < argc; g++)
    file_handle(argv[g]);
  if (g-optind == 0)
    file_handle(NULL);

  fclose(outfile);
  apatterns_free(patterns);

  return 0;
}
