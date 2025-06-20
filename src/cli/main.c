/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <hexderm@gmail.com>

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

#include "../ext.h"

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
#include <assert.h>

#include "../lib/reliq.h"
#include "pretty.h"

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

struct pretty_settings psettings = {0};

enum {
  htmlProcess,
  htmlPrettify,
  urlJoin,
  entityDecode,
  entityDecodeExact,
  entityEncode,
  entityEncodeFull
} run_mode = htmlProcess;

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
join_urls(const char *url, char **argv, size_t pos, const size_t argc)
{
  if (pos >= argc)
    return;

  reliq_url ref;
  reliq_url_parse(url,strlen(url),NULL,0,false,&ref);
  reliq_url c = (reliq_url){0};

  for (; pos < argc; pos++) {
    reliq_url_parse(argv[pos],strlen(argv[pos]),ref.scheme.b,ref.scheme.s,false,&c);
    reliq_url_join(&ref,&c,&c);

    fwrite(c.url.b,1,c.url.s,outfile);
    fputc('\n',outfile);
  }

  reliq_url_free(&c);
  reliq_url_free(&ref);
}

static void
html_prettify(char *f, size_t s, int (*freedata)(void*,size_t))
{
  reliq_error *err;

  reliq rq;
  if ((err = reliq_init(f,s,&rq))) {
    reliq_efree(expr);
    handle_reliq_error(err);
  }



  reliq_free(&rq);
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

  err = reliq_exec_file(&rq,NULL,0,expr,outfile);

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

static void
file_exec_set(int argc, const char **argv)
{
  if (run_mode == htmlProcess) {
    if (!expr && optind < argc) {
      handle_reliq_error(reliq_ecomp(argv[optind],strlen(argv[optind]),&expr));
      optind++;
      assert(expr);
    }
    file_exec = expr_exec;
  } else if (run_mode == htmlPrettify) {
    file_exec = html_prettify;
  } else if (run_mode == entityDecode) {
    file_exec = str_decode;
  } else if (run_mode == entityDecodeExact) {
    file_exec = str_decode_exact;
  } else if (run_mode == entityEncode) {
    file_exec = str_encode;
  } else if (run_mode == entityEncodeFull) {
    file_exec = str_encode_full;
  }
}

static int
valid_int(const char *arg, const char *option)
{
  long int ret;
  char *end;
  ret = strtol(arg,&end,10);

  if (!*end)
    die("--%s: expected integer, got \"%s\"", option, arg);

  return ret;
}

static uchar
longopts_handle_mode(const char *name)
{
  #define X(x,y) if (strcmp(name,x) == 0) { \
      run_mode = y; \
    }

  X("html",htmlProcess)
  else X("urljoin",urlJoin)
  else X("encode",entityEncode)
  else X("encode-full",entityEncodeFull)
  else X("decode",entityDecode)
  else X("decode-exact",entityDecodeExact)
  else
    return 0;
  return 1;

  #undef X
}

static uchar
longopts_handle_html_process(const char *name)
{
  #define X(x,y,z) if (strcmp(name,x) == 0) { \
      psettings.y = z; \
    }

  X("indent",indent,valid_int(optarg,"indent"))
  else X("cycle-indent",cycle_indent,valid_int(optarg,"cycle-indent"))
  else X("indent-script",indent_script,1)
  else X("no-indent-script",indent_script,0)
  else X("indent-style",indent_style,1)
  else X("no-indent-style",indent_style,0)
  else X("wrap-text",wrap_text,1)
  else X("no-wrap-text",wrap_text,0)
  else X("wrap-comments",wrap_comments,1)
  else X("no-wrap-comments",wrap_comments,0)
  else X("color",color,1)
  else X("force-color",color,2)
  else X("no-color",color,0)
  else X("trim-tags",trim_tags,1)
  else X("no-trim-tags",trim_tags,0)
  else X("trim-attribs",trim_attribs,1)
  else X("no-trim-attribs",trim_attribs,0)
  else X("trim-comments",trim_comments,1)
  else X("no-trim-comments",trim_comments,0)
  else X("normal-case",normal_case,1)
  else X("no-normal-case",normal_case,0)
  else X("fix",fix,1)
  else X("no-fix",fix,0)
  else X("merge-attribs",merge_attribs,1)
  else X("no-merge-attribs",merge_attribs,0)
  else X("remove-comments",remove_comments,1)
  else X("no-remove-comments",remove_comments,0)
  else X("show-hidden",show_hidden,1)
  else X("no-show-hidden",show_hidden,0)
  else X("overlap-ending",overlap_ending,1)
  else X("no-overlap-ending",overlap_ending,0)
  else
    return 0;

  run_mode = htmlProcess;
  return 1;

  #undef X
}

static void
longopts_handle(const char *name)
{
  if (longopts_handle_mode(name))
    return;

  if (longopts_handle_html_process(name))
    return;
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

    {"html",no_argument,NULL,0},
    {"pretty",no_argument,NULL,'p'},
    {"urljoin",no_argument,NULL,0},
    {"encode",no_argument,NULL,0},
    {"encode-full",no_argument,NULL,0},
    {"decode",no_argument,NULL,0},
    {"decode-exact",no_argument,NULL,0},

    {"maxline",required_argument,NULL,'L'},
    {"indent",required_argument,NULL,0},
    {"cycle-indent",required_argument,NULL,0},

    {"indent-script",no_argument,NULL,0},
    {"no-indent-script",no_argument,NULL,0},
    {"indent-style",no_argument,NULL,0},
    {"no-indent-style",no_argument,NULL,0},
    {"wrap-text",no_argument,NULL,0},
    {"no-wrap-text",no_argument,NULL,0},
    {"wrap-comments",no_argument,NULL,0},
    {"no-wrap-comments",no_argument,NULL,0},
    {"color",no_argument,NULL,0},
    {"force-color",no_argument,NULL,0},
    {"no-color",no_argument,NULL,0},
    {"trim-tags",no_argument,NULL,0},
    {"no-trim-tags",no_argument,NULL,0},
    {"trim-attribs",no_argument,NULL,0},
    {"no-trim-attribs",no_argument,NULL,0},
    {"trim-comments",no_argument,NULL,0},
    {"no-trim-comments",no_argument,NULL,0},
    {"normal-case",no_argument,NULL,0},
    {"no-normal-case",no_argument,NULL,0},
    {"fix",no_argument,NULL,0},
    {"no-fix",no_argument,NULL,0},
    {"merge-attribs",no_argument,NULL,0},
    {"no-merge-attribs",no_argument,NULL,0},
    {"remove-comments",no_argument,NULL,0},
    {"no-remove-comments",no_argument,NULL,0},
    {"show-hidden",no_argument,NULL,0},
    {"no-show-hidden",no_argument,NULL,0},
    {"overlap-ending",no_argument,NULL,0},
    {"no-overlap-ending",no_argument,NULL,0},
    {NULL,0,NULL,0}
  };

  while (1) {
    int index;
    int opt = getopt_long(argc,argv,"lo:e:E:f:u:HrRvhpL:",long_options,&index);
    if (opt == -1)
      break;

    switch (opt) {
      case 'l':
        run_mode = htmlProcess;
        handle_reliq_error(reliq_ecomp("| \"%n%Ua - desc(%c) lvl(%L) size(%s) pos(%I)\\n\"",47,&expr));
        break;
      case 'o':
        outfile = open_file(optarg,"wb");
        break;
      case 'e':
        run_mode = htmlProcess;
        handle_reliq_error(reliq_ecomp(optarg,strlen(optarg),&expr));
        break;
      case 'E':
        errfile = open_file(optarg,"wb");
        break;
      case 'f':
        run_mode = htmlProcess;
        load_expr_from_file(optarg);
        break;
      case 'u':
        run_mode = htmlProcess;
        url_ref = optarg;
        url_refl = strlen(optarg);
        break;
      case 'L':
        run_mode = htmlProcess;
        psettings.maxline = valid_int(optarg,"maxline");
        break;
      case 'r': settings |= F_RECURSIVE; break;
      case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
      case 'v': die(RELIQ_VERSION); break;
      case 'h': usage(argv0,errfile); break;
      case 'p':
        run_mode = htmlPrettify;
        pretty_settings_init(&psettings);
        break;
      case 0:
        longopts_handle(long_options[index].name);
        break;
      default: exit(1);
    }
  }

  file_exec_set(argc,(const char**)argv);

  int g = optind;
  if (run_mode == urlJoin) {
    if (optind < argc)
      join_urls(argv[g],argv,g+1,argc);
  } else {
    for (; g < argc; g++)
      file_handle(argv[g]);
    if (g-optind == 0)
      file_handle(NULL);
  }

  if (outfile != stdout)
    fclose(outfile);
  if (errfile != stderr)
    fclose(outfile);
  if (expr)
    reliq_efree(expr);

  free(argv0);

  return 0;
}
