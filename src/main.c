/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2023 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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
#include <limits.h>
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
#define F_FAST 0x2

#define BUFF_INC_VALUE (1<<23)

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGHT(x) (sizeof(x)/(sizeof(*x)))

char *argv0;
hgrep_epattern *patterns = NULL;
size_t patternsl = 0;

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

/*void
hgrep_epattern_print(flexarr *epatterns, size_t tab)
{
  hgrep_epattern *a = (hgrep_patterns*)epatterns->v;
  for (size_t j = 0; j < tab; j++)
    fputc('\t',stderr);
  fprintf(stderr,"%% %lu",epatterns->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < epatterns->size; i++) {
    if (a[i].istable&1) {
      fprintf(stderr,"%%x %d\n",a[i].istable);
      hgrep_epattern_print((flexarr*)a[i].p,tab);
    } else {
      for (size_t j = 0; j < tab; j++)
        fputc('\t',stderr);
      fprintf(stderr,"%%j\n");
    }
  }
} //used for debugging*/

static void
patterns_exec_fast(char *f, size_t s, const uchar inpipe, hgrep_epattern *epatterns, size_t epatternsl)
{
  if (epatternsl == 0)
    return;
  if (epatternsl > 1)
    die("fast mode cannot run in non linear mode");

  FILE *t = outfile;
  char *ptr;
  size_t fsize;

  flexarr *first = (flexarr*)epatterns[0].p;
  for (size_t i = 0; i < first->size; i++) {
    outfile = (i == first->size-1) ? t : open_memstream(&ptr,&fsize);

    if (((hgrep_epattern*)first->v)[i].istable&1)
      die("fast mode cannot run in non linear mode");
    hgrep_fmatch(f,s,outfile,(hgrep_pattern*)((hgrep_epattern*)first->v)[i].p);
    fflush(outfile);

    if (i == 0 && !inpipe) {
      munmap(f,s);
    } else
      free(f);
    
    if (i != first->size-1)
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
    patterns_exec_fast(f,s,inpipe,patterns,patternsl);
    return;
  }

  hgrep hg = hgrep_init(f,s,outfile);
  hgrep_ematch(&hg,patterns,patternsl,NULL,0,NULL,0);
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
      case 'E': hflags |= HGREP_EREGEX; break;
      case 'i': hflags |= HGREP_ICASE; break;
      case 'l': hgrep_epcomp("@p\"%n%A - %c/%l/%s/%p\n\"",NULL,23,hflags,&patterns,&patternsl); break;
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
        hgrep_epcomp(p,NULL,s,hflags,&patterns,&patternsl);
        //hgrep_epattern_print(patterns,0);
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

  if (!patterns && optind < argc) {
    hgrep_epcomp(argv[optind],NULL,strlen(argv[optind]),hflags,&patterns,&patternsl);
    //hgrep_epattern_print(patterns,0);
    optind++;
  }
  if (!patterns)
      return -1;
  int g = optind;
  for (; g < argc; g++)
    file_handle(argv[g]);
  if (g-optind == 0)
    file_handle(NULL);

  fclose(outfile);
  hgrep_epattern_free(patterns,patternsl);

  return 0;
}
