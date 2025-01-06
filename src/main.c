/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#include "ext.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#if !(defined(__MINGW32__) || defined(__MINGW64__))
#include <sys/mman.h>
#endif
#include <ftw.h>
#include <errno.h>
#include <libgen.h>

#include "reliq.h"

#define F_RECURSIVE 0x1
#define F_FAST 0x2

#define BUFF_INC_VALUE (1<<23)

typedef unsigned char uchar;

char *argv0;
reliq_expr expr = {0};

unsigned int settings = 0; //F_
int nftwflags = FTW_PHYS;
FILE *outfile;
FILE *errfile;

static int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

static void
die(const char *s, ...)
{
  va_list ap;
  va_start(ap,s);
  vfprintf(errfile,s,ap);
  va_end(ap);
  fputc('\n',errfile);
  exit(1);
}

static void
xerrno_print(const char *s, va_list args)
{
  int e = errno;
  fputs(argv0,errfile);
  fputs(": ",errfile);
  vfprintf(errfile,s,args);
  fputs(": ",errfile);
  fputs(strerror(e),errfile);
  fputc('\n',errfile);
}

static void
xwarn(const char *s, ...) {
  va_list ap;
  va_start(ap,s);
  xerrno_print(s,ap);
  va_end(ap);
}

static void
xerr(int eval, const char *s, ...) {
  va_list ap;
  va_start(ap,s);
  xerrno_print(s,ap);
  va_end(ap);
  exit(eval);
}

static void
handle_reliq_error(reliq_error *err) {
  if (err == NULL)
    return;

  fputs(argv0,errfile);
  fputs(": ",errfile);

  fputs(err->msg,errfile);
  fputc('\n',errfile);

  int c = err->code;
  free(err);
  exit(c);
}

static void
usage()
{
  die("Usage: %s [OPTION]... PATTERNS [FILE]...\n"\
      "Search for PATTERNS in each html FILE.\n"\
      "Example: %s 'div id; a href=e>\".org\"' index.html\n\n"\
      "Options:\n"\
      "  -l\t\t\tlist structure of FILE\n"\
      "  -o FILE\t\tchange output to a FILE instead of stdout\n"\
      "  -e FILE\t\tchange output of errors to a FILE instead of stderr\n"\
      "  -f FILE\t\tobtain PATTERNS from FILE\n"\
      "  -H\t\t\tfollow symlinks\n"\
      "  -r\t\t\tread all files under each directory, recursively\n"\
      "  -R\t\t\tlikewise but follow all symlinks\n"\
      "  -F\t\t\tenter fast and low memory consumption mode\n"\
      "  -h\t\t\tshow help\n"\
      "  -v\t\t\tshow version\n\n"\
      "When FILE isn't specified, FILE will become standard input.",argv0,argv0,argv0);
}

static void
expr_exec(char *f, size_t s, const uchar inpipe)
{
  if (f == NULL || s == 0)
    return;

  reliq_error *err;
  int (*freedata)(void*,size_t) = inpipe ? reliq_std_free :
  #if defined(__MINGW32__) || defined(__MINGW64__)
    reliq_std_free;
  #else
    munmap;
  #endif

  if (settings&F_FAST) {
    err = reliq_fexec_file(f,s,outfile,&expr,freedata);
    if (err)
      goto ERR;
    return;
  }

  reliq rq;
  if ((err = reliq_init(f,s,freedata,&rq)))
    goto ERR;
  err = reliq_exec_file(&rq,outfile,&expr);

  reliq_free(&rq);
  ERR: ;

  if (err) {
    reliq_efree(&expr);
    handle_reliq_error(err);
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
file_handle(const char *f)
{
  int fd;
  struct stat st;
  char *file;

  if (f == NULL) {
    size_t size;
    pipe_to_str(0,&file,&size);
    expr_exec(file,size,1);
    return;
  }

  if ((fd = open(f,O_RDONLY)) == -1) {
    xwarn("%s",f);
    return;
  }

  if (fstat(fd,&st) == -1) {
    xwarn("%s",f);
    close(fd);
    return;
  }

  if ((st.st_mode&S_IFMT) == S_IFDIR) {
    close(fd);
    if (settings&F_RECURSIVE) {
      if (nftw(f,nftw_func,16,nftwflags) == -1)
        xwarn("%s",f);
    } else
      fprintf(errfile,"%s: -R not specified: omitting directory '%s'\n",argv0,f);
    return;
  }

  if (st.st_size == 0) {
    close(fd);
    fprintf(errfile,"%s: %s: empty file\n",argv0,f);
    return;
  }

  #if defined(__MINGW32__) || defined(__MINGW64__)
  file = malloc(st.st_size);
  if (read(fd,file,st.st_size) == -1) {
  #else
  file = mmap(NULL,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
  if (file == MAP_FAILED) {
  #endif
    xwarn("%s",f);
    #if defined(__MINGW32__) || defined(__MINGW64__)
    free(file);
    #endif
    close(fd);
  } else {
    close(fd);
    expr_exec(file,st.st_size,0);
  }
}

void
load_expr_from_file(char *filename)
{
  if (!filename)
    return;
  int fd = open(filename,O_RDONLY);
  if (fd == -1)
    xerr(RELIQ_ERROR_SYS,"%s",filename);
  char *file;
  size_t filel;
  pipe_to_str(fd,&file,&filel);
  close(fd);
  reliq_error *err = reliq_ecomp(file,filel,&expr);
  free(file);
  handle_reliq_error(err);
}

int
nftw_func(const char *fpath, const struct stat UNUSED *sb, int typeflag, struct FTW UNUSED *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    file_handle(fpath);

  return 0;
}

int
main(int argc, char **argv)
{
  int opt;

  #if defined(__MINGW32__) || defined(__MINGW64__)
  _setmode(1,O_BINARY);
  _setmode(2,O_BINARY);
  #endif

  outfile = stdout;
  errfile = stderr;

  argv0 = basename(argv[0]);
  if (argc < 2)
    usage();

  while ((opt = getopt(argc,argv,"lo:e:f:HrRFvh")) != -1) {
    switch (opt) {
      case 'l':
        handle_reliq_error(reliq_ecomp("| \"%n%Ua - desc(%c) lvl(%L) size(%s) pos(%I)\\n\"",47,&expr));
        break;
      case 'o':
        outfile = fopen(optarg,"wb");
        if (outfile == NULL)
          xerr(RELIQ_ERROR_SYS,"%s",optarg);
        break;
      case 'e':
        errfile = fopen(optarg,"wb");
        if (errfile == NULL) {
          errfile = stderr;
          xerr(RELIQ_ERROR_SYS,"%s",optarg);
        }
        break;
      case 'f': load_expr_from_file(optarg); break;
      case 'H': nftwflags &= ~FTW_PHYS; break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'F': settings |= F_FAST; break;
      case 'v': die(RELIQ_VERSION); break;
      case 'h': usage(); break;
      default: exit(1);
    }
  }

  if (!expr.e && optind < argc) {
    handle_reliq_error(reliq_ecomp(argv[optind],strlen(argv[optind]),&expr));
    optind++;
  }
  if (!expr.e)
    return -1;
  int g = optind;
  for (; g < argc; g++)
    file_handle(argv[g]);
  if (g-optind == 0)
    file_handle(NULL);

  if (outfile != stdout)
    fclose(outfile);
  reliq_efree(&expr);

  return 0;
}
