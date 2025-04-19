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
#include <getopt.h>

#include "reliq.h"

#define F_RECURSIVE 0x1

#define BUFF_INC_VALUE (1<<23)

typedef unsigned char uchar;

char *argv0;
reliq_expr *expr = NULL;

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

static uchar
should_colorize(FILE *o)
{
  #ifdef __unix__

  int fd = fileno(o);
  if (fd == -1)
    return 0;

  const char *term = getenv("TERM");
  if (strcmp(term,"dump") == 0)
    return 0;

  struct stat st;
  fstat(fd,&st);
  if (!S_ISCHR(st.st_mode))
    return 0;

  struct stat t_st;
  stat("/dev/null",&t_st);
  if (st.st_ino == t_st.st_ino && st.st_dev == t_st.st_dev)
    return 0;

  return isatty(fd);

  #else

  return 0;

  #endif
}

static void
usage(FILE *o)
{
  volatile uchar cancolor = should_colorize(o);
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wunused-value"
  #pragma GCC diagnostic ignored "-Wnonnull"

  #define color(x,y) for (size_t _i = 0; _i == 0; _i++, cancolor && fputs("\033[0m",o)) { \
    cancolor && ( fputs("\033[",o), fputs(y,o), fputc('m',o) ); \
    fputs(x,o); \
  }
  #define COLOR_OPTION "35;1"
  #define COLOR_ARG "36"
  #define COLOR_SCRIPT "32"
  #define COLOR_INPUT "33"
  #define COLOR_SECTION "34;1"

  color("Usage",COLOR_SECTION)
  fprintf(o,": %s [",argv0);
  color("OPTION",COLOR_OPTION);
  fputs("]... ",o);
  color("PATTERNS",COLOR_SCRIPT);
  fputs(" [",o);
  color("FILE",COLOR_INPUT);
  fputs("]...\n",o);

  fputs("Search for ",o);
  color("PATTERNS",COLOR_SCRIPT);
  fputs(" in each html ",o);
  color("FILE",COLOR_INPUT);
  fputs(".\n",o);

  color("Example",COLOR_SECTION)
  fputs(": ",o);
  fputs(argv0,o);
  fputs(" \'",o);
  color("div id; a href=e>\".org\"",COLOR_SCRIPT);
  fputs("' ",o);
  color("index.html",COLOR_INPUT);
  fputs("\n\n",o);

  color("Options",COLOR_SECTION);
  fputs(":\n",o);

  #define color_option(w,x,y) do { \
    fputs("  ",o); \
    if (w) { \
      fputc('-',o); \
      color(w,COLOR_OPTION); \
      if (x) \
        fputs(", ",o); \
    } \
    if (x) { \
      fputs("--",o); \
      color(x,COLOR_OPTION); \
    } \
    if (y != NULL) { \
      fputc(' ',o); \
      color(y,COLOR_ARG); \
    } \
  } while (0)

  color_option("l","list-structure",NULL);
  fputs("\t\tlist structure of ",o);
  color("FILE",COLOR_INPUT);
  fputc('\n',o);

  color_option("i","output","FILE");
  fputs("\t\tchange output to a ",o);
  color("FILE",COLOR_ARG);
  fputs(" instead of ",o);
  color("stdout",COLOR_ARG);
  fputc('\n',o);

  color_option("e","error-file","FILE");
  fputs("\t\tchange output of errors to a ",o);
  color("FILE",COLOR_ARG);
  fputs(" instead of ",o);
  color("stderr",COLOR_ARG);
  fputc('\n',o);

  color_option("f","file","FILE");
  fputs("\t\tobtain ",o);
  color("PATTERNS",COLOR_SCRIPT);
  fputs(" from ",o);
  color("FILE",COLOR_ARG);
  fputc('\n',o);

  color_option("r","recursive",NULL);
  fputs("\t\tread all files under each directory, recursively\n",o);

  color_option("R","dereference-recursive",NULL);
  fputs("\tlikewise but follow all symlinks\n",o);

  color_option("h","help",NULL);
  fputs("\t\t\tshow help\n",o);

  color_option("v","version",NULL);
  fputs("\t\t\tshow version\n\n",o);

  fputc('\n',o);

  fputs("When input files aren't specified, standard input will be read.\n",o);

  #pragma GCC diagnostic pop

  #undef color
  #undef color_option
  #undef COLOR_OPTION
  #undef COLOR_ARG
  #undef COLOR_SCRIPT
  #undef COLOR_INPUT
  #undef COLOR_SECTION

  exit(1);
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

  reliq rq;
  if ((err = reliq_init(f,s,&rq)))
    goto ERR;
  rq.freedata = freedata;
  err = reliq_exec_file(&rq,outfile,expr);

  reliq_free(&rq);
  ERR: ;

  if (err) {
    reliq_efree(expr);
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

static void
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

static void
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
  if (expr)
    reliq_efree(expr);
  reliq_error *err = reliq_ecomp(file,filel,&expr);
  free(file);
  handle_reliq_error(err);
}

static int
nftw_func(const char *fpath, const struct stat UNUSED *sb, int typeflag, struct FTW UNUSED *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    file_handle(fpath);

  return 0;
}

static FILE *
open_file(const char *path, const char *mode)
{
  FILE *f = fopen(optarg,mode);
  if (f == NULL)
    xerr(RELIQ_ERROR_SYS,"%s",path);
  return f;
}

int
main(int argc, char **argv)
{
  #if defined(__MINGW32__) || defined(__MINGW64__)
  _setmode(1,O_BINARY);
  _setmode(2,O_BINARY);
  #endif

  outfile = stdout;
  errfile = stderr;

  argv0 = strdup(basename(argv[0]));
  if (argc < 2)
    usage(errfile);

  struct option long_options[] = {
    {"output",required_argument,NULL,'o'},
    {"help",no_argument,NULL,'h'},
    {"version",no_argument,NULL,'v'},
    {"recursive",no_argument,NULL,'r'},
    {"dereference-recursive",no_argument,NULL,'R'},
    {"list-structure",no_argument,NULL,'l'},
    {"error-file",required_argument,NULL,'e'},
    {"file",required_argument,NULL,'f'},
    {NULL,0,NULL,0}
  };

  while (1) {
    int index;
    int opt = getopt_long(argc,argv,"lo:e:f:HrRvh",long_options,&index);
    if (opt == -1)
      break;
    switch (opt) {
      case 'l':
        handle_reliq_error(reliq_ecomp("| \"%n%Ua - desc(%c) lvl(%L) size(%s) pos(%I)\\n\"",47,&expr));
        break;
      case 'o':
        outfile = open_file(optarg,"wb");
        break;
      case 'e':
        errfile = open_file(optarg,"wb");
        break;
      case 'f': load_expr_from_file(optarg); break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'v': die(RELIQ_VERSION); break;
      case 'h': usage(errfile); break;
      case 0:
        break;
      default: exit(1);
    }
  }

  if (!expr && optind < argc) {
    handle_reliq_error(reliq_ecomp(argv[optind],strlen(argv[optind]),&expr));
    optind++;
    if (!expr)
      return -1;
  }
  int g = optind;
  for (; g < argc; g++)
    file_handle(argv[g]);
  if (g-optind == 0)
    file_handle(NULL);

  if (outfile != stdout)
    fclose(outfile);

  if (expr)
    reliq_efree(expr);

  free(argv0);

  return 0;
}
