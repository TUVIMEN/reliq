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

void (*file_exec)(char*,size_t s,int (*)(void*,size_t));

static int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
void usage(const char *argv0, FILE *o);

char *url_ref = NULL;
size_t url_refl = 0;

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
str_decode(char *f, size_t s, int (*freedata)(void*,size_t)) {
  reliq_decode_entities_file(f,s,outfile,true);
  freedata(f,s);
}

static void
str_decode_exact(char *f, size_t s, int (*freedata)(void*,size_t)) {
  reliq_decode_entities_file(f,s,outfile,false);
  freedata(f,s);
}

static void
str_encode(char *f, size_t s, int (*freedata)(void*,size_t)) {
  reliq_encode_entities_file(f,s,outfile,false);
  freedata(f,s);
}

static void
str_encode_full(char *f, size_t s, int (*freedata)(void*,size_t)) {
  reliq_encode_entities_file(f,s,outfile,true);
  freedata(f,s);
}

static void
expr_exec(char *f, size_t s, int (*freedata)(void*,size_t))
{
  if (f == NULL || s == 0)
    return;

  reliq_error *err;

  reliq rq;
  if ((err = reliq_init(f,s,&rq)))
    goto ERR;
  if (url_ref)
    reliq_set_url(&rq,url_ref,url_refl);

  err = reliq_exec_file(&rq,outfile,expr);

  reliq_free(&rq);
  freedata(f,s);
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
    file_exec(file,size,reliq_std_free);
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
    file_exec(file,st.st_size,
      #if defined(__MINGW32__) || defined(__MINGW64__)
        reliq_std_free
      #else
        munmap
      #endif
    );
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
    usage(argv0,errfile);

  enum {
    htmlProcess,
    entityDecode,
    entityDecodeExact,
    entityEncode,
    entityEncodeFull
  } mode = htmlProcess;

  struct option long_options[] = {
    {"output",required_argument,NULL,'o'},
    {"help",no_argument,NULL,'h'},
    {"version",no_argument,NULL,'v'},
    {"recursive",no_argument,NULL,'r'},
    {"dereference-recursive",no_argument,NULL,'R'},
    {"list-structure",no_argument,NULL,'l'},
    {"error-file",required_argument,NULL,'E'},
    {"expression",required_argument,NULL,'e'},
    {"file",required_argument,NULL,'f'},
    {"url",required_argument,NULL,'u'},
    {"encode",no_argument,NULL,0},
    {"encode-full",no_argument,NULL,0},
    {"decode",no_argument,NULL,0},
    {"decode-exact",no_argument,NULL,0},
    {NULL,0,NULL,0}
  };

  while (1) {
    int index;
    char const *name;
    int opt = getopt_long(argc,argv,"lo:e:E:f:u:HrRvh",long_options,&index);
    if (opt == -1)
      break;

    switch (opt) {
      case 'l':
        mode = htmlProcess;
        handle_reliq_error(reliq_ecomp("| \"%n%Ua - desc(%c) lvl(%L) size(%s) pos(%I)\\n\"",47,&expr));
        break;
      case 'o':
        outfile = open_file(optarg,"wb");
        break;
      case 'e':
        mode = htmlProcess;
        handle_reliq_error(reliq_ecomp(optarg,strlen(optarg),&expr));
        break;
      case 'E':
        errfile = open_file(optarg,"wb");
        break;
      case 'f':
        mode = htmlProcess;
        load_expr_from_file(optarg);
        break;
      case 'u':
        mode = htmlProcess;
        url_ref = optarg;
        url_refl = strlen(optarg);
        break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'v': die(RELIQ_VERSION); break;
      case 'h': usage(argv0,errfile); break;
      case 0:
        name = long_options[index].name;
        if (strcmp(name,"encode") == 0) {
          mode = entityEncode;
        } else if (strcmp(name,"encode-full") == 0) {
          mode = entityEncodeFull;
        } else if (strcmp(name,"decode") == 0) {
          mode = entityDecode;
        } if (strcmp(name,"decode-exact") == 0) {
          mode = entityDecodeExact;
        }
        break;
      default: exit(1);
    }
  }

  if (mode == htmlProcess) {
    if (!expr && optind < argc) {
      handle_reliq_error(reliq_ecomp(argv[optind],strlen(argv[optind]),&expr));
      optind++;
      if (!expr)
        return -1;
    }
    file_exec = expr_exec;
  } else if (mode == entityDecode) {
    file_exec = str_decode;
  } else if (mode == entityDecodeExact) {
    file_exec = str_decode_exact;
  } else if (mode == entityEncode) {
    file_exec = str_encode;
  } else if (mode == entityEncodeFull) {
    file_exec = str_encode_full;
  }

  int g = optind;
  for (; g < argc; g++)
    file_handle(argv[g]);
  if (g-optind == 0)
    file_handle(NULL);

  if (outfile != stdout)
    fclose(outfile);
  if (errfile != stderr)
    fclose(outfile);
  if (expr)
    reliq_efree(expr);

  free(argv0);

  return 0;
}
