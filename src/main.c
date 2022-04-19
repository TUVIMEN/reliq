/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2022 TUVIMEN <suchora.dominik7@gmail.com>

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

#include "flexarr.h"
#include "ctype.h"
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

#define PATTERN_SIZE_INC 8

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGHT(x) (sizeof(x)/(sizeof(*x)))

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
      "  -V\t\t\tshow version\n\n"\
      "When FILE isn't specified, FILE will become standard input.",argv0,argv0,argv0);
}

static char *
delchar(char *src, const size_t pos, size_t *size)
{
  size_t s = *size-1;
  for (size_t i = pos; i < s; i++)
    src[i] = src[i+1];
  src[s] = 0;
  *size = s;
  return src;
}

static flexarr *
split_patterns(char *src, size_t s)
{
  if (s == 0)
    return NULL;
  flexarr *ret = flexarr_init(sizeof(hgrep_pattern),PATTERN_SIZE_INC);
  for (size_t i = 0, j; i < s; i++) {
    j = i;

    while (i < s) {
      if (src[i] == '\\' && src[i+1] == ';')
	    delchar(src,i,&s);
      if (src[i] == ';')
        break;
      i++;
    }

    hgrep_pcomp(src+j,i-j,(hgrep_pattern*)flexarr_inc(ret),0);
    while (i < s && src[i] == ';')
      i++;
    i--;
  }
  flexarr_clearb(ret);
  return ret;
}

uchar
settings_to_hgrep(const uint settings)
{
    uchar ret = 0;
    if (settings&F_EXTENDED)
        ret |= HGREP_EREGEX;
    if (settings&F_ICASE)
        ret |= F_ICASE;
    return ret;
}

static void
analyze(char *f, size_t s, uchar inpipe)
{
  if (f == NULL || s == 0)
    return;

  if (settings&F_FAST) {
    FILE *t = outfile;
    char **ptr = malloc(patterns->size<<3);
    size_t *fsize = malloc(patterns->size<<3);
    
    for (size_t i = 0; i < patterns->size; i++) {
      if (i == patterns->size-1) {
        outfile = t;
      } else {
        outfile = open_memstream(&ptr[i],&fsize[i]);
      }
    
      hgrep_pattern *flv = &((hgrep_pattern*)patterns->v)[i];
      hgrep_init(NULL,f,s,outfile,flv,hflags);
      fflush(outfile);
    
      if (!inpipe && i == 0)
        munmap(f,s);
      else
        free(f);
      if (i != patterns->size-1)
        fclose(outfile);
    
      f = ptr[i];
      s = fsize[i];
    }
    free(ptr);
    free(fsize);
  } else {
    hgrep hg;
    hgrep_pattern *flv = ((hgrep_pattern*)patterns->v);
    hgrep_init(&hg,f,s,NULL,NULL,hflags);
    flexarr *passed1=flexarr_init(sizeof(size_t),1024),
      *passed2=flexarr_init(sizeof(size_t),1024);
    for (size_t j = 0; j < hg.nodesl; j++) {
      if (hgrep_match(&hg.nodes[j],flv)) {
        if (patterns->size == 1) {
          if (flv->format.b)
            hgrep_printf(outfile,flv->format.b,flv->format.s,&hg.nodes[j],f);
          else
           hgrep_print(outfile,&hg.nodes[j]);
        } else {
          *(size_t*)flexarr_inc(passed1) = j;
        }
      }
    }
    size_t *ps,n;
    ushort lvl;
    for (size_t i = 1; i < patterns->size; i++) {
      flv = &((hgrep_pattern*)patterns->v)[i];
      ps = (size_t*)passed1->v;
      for (size_t j = 0; j < passed1->size; j++) {
        lvl = hg.nodes[ps[j]].lvl;
        for (size_t h = 0; h <= hg.nodes[ps[j]].child_count; h++) {
          n = ps[j]+h;
          hg.nodes[n].lvl -= lvl;
          if (hgrep_match(&hg.nodes[n],flv)) {
            if (patterns->size-1 == i) {
              if (flv->format.b)
                hgrep_printf(outfile,flv->format.b,flv->format.s,&hg.nodes[n],f);
              else
               hgrep_print(outfile,&hg.nodes[n]);
            } else {
              *(size_t*)flexarr_inc(passed2) = n;
            }
          }
          hg.nodes[n].lvl += lvl;
        }
      }
      if (!passed2->size)
          break;
      passed1->size = 0;
      flexarr *tmp = passed1;
      passed1 = passed2;
      passed2 = tmp;
    }
    flexarr_free(passed1);
    flexarr_free(passed2);
    hgrep_free(&hg);
  }
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
handle_file(const char *f)
{
  int fd;
  struct stat st;
  char *file;

  if (f == NULL) {
    size_t size;
    pipe_to_str(0,&file,&size);
    analyze(file,size,1);
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
    analyze(file,st.st_size,0);
  }
}

int
nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    handle_file(fpath);

  return 0;
}

int
main(int argc, char **argv)
{
  argv0 = argv[0];
  if (argc < 2)
    usage();

  int opt;
  outfile = stdout;

  while ((opt = getopt(argc,argv,"Eilo:f:HrRFVh")) != -1) {
    switch (opt) {
      case 'E': settings |= F_EXTENDED; break;
      case 'i': settings |= F_ICASE; break;
      case 'l': patterns = split_patterns("@p\"%t%I - %s/%p\n\"",17); break;
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
        patterns = split_patterns(p,s);
        free(p);
        }
        break;
      case 'H': nftwflags &= ~FTW_PHYS; break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'F': settings |= F_FAST; break;
      case 'V': die(VERSION); break;
      case 'h': usage(); break;
      default: exit(1);
    }
  }

  hflags = settings_to_hgrep(settings);

  if (!patterns && optind < argc) {
    patterns = split_patterns(argv[optind],strlen(argv[optind]));
    optind++;
  }
  int g = optind;
  for (; g < argc; g++)
    handle_file(argv[g]);

  if (g-optind == 0)
    handle_file(NULL);
  fclose(outfile);
  for (size_t n = 0; n < patterns->size; n++)
    hgrep_pfree(&((hgrep_pattern*)patterns->v)[n]);
  flexarr_free(patterns);

  return 0;
}
