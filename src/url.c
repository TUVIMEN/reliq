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
#include <string.h>
#include <assert.h>

#include "types.h"
#include "ctype.h"
#include "utils.h"

struct urldir {
  const char *from;
  const char *to;
};
#define DIRS_MAX 256

static void
trim_url(reliq_cstr *url)
{
  size_t i = 0;

  size_t urll = url->s;
  const char *urlb = url->b;

  while (i < urll && urlb[i] <= ' ') {
    i++;
    urlb++;
  }

  url->b = urlb;
  url->s = urll;
}

static inline uchar
char_unsafe(char c)
{
  return (c == '\n' || c == '\t' || c == '\r');
}

void
remove_unsafe(reliq_str *url)
{
  char *urlb = url->b;
  const size_t urll = url->s;

  size_t i=0,j=0;
  for (; i < urll; i++) {
    char c = urlb[i];
    if (char_unsafe(c))
      continue;
    urlb[j++] = c;
  }
  url->s = j;
}

static inline uchar
char_scheme(char c)
{
  return (isalnum(c) || c == '+' || c == '-' || c == '.');
}

static void
get_scheme(reliq_cstr *url, const char *scheme, size_t schemel, reliq_cstr *dest)
{
  const char *urlb = url->b;
  const size_t urll = url->s;

  if (urll && isalpha(urlb[0])) {
    size_t i = 1;
    for (; i < urll; i++) {
      char c = urlb[i];
      if (c == ':') {
        scheme = urlb;
        schemel = i;

        url->b += i+1;
        url->s -= i+1;
        break;
      }
      if (!char_scheme(c))
        break;
    }
  }
  *dest = (reliq_cstr){
    .b = scheme,
    .s = schemel
  };
}

static inline uchar
char_netloc_end(char c)
{
  return (c == '/' || c == '?' || c == '#');
}

static void
get_netloc(reliq_cstr *url, reliq_cstr *dest)
{
  const char *netloc = NULL;
  size_t netlocl = 0;

  const char *urlb = url->b;
  const size_t urll = url->s;

  if (2 >= urll || urlb[0] != '/' || urlb[1] != '/')
    goto END;

  size_t i = 2;
  for (; i < urll; i++)
    if (char_netloc_end(urlb[i]))
      break;

  netloc = urlb+2;
  netlocl = i-2;
  url->b += i;
  url->s -= i;

  END: ;
  *dest = (reliq_cstr){
    .b = netloc,
    .s = netlocl
  };
}

static void
get_by_delim(reliq_cstr *url, reliq_cstr *dest, const char delim)
{
  const char *res = NULL;
  size_t resl = 0;

  const char *urlb = url->b;
  const size_t urll = url->s;

  char *d = memchr(url->b,delim,url->s);
  if (d) {
    resl = urll-((d+1)-urlb);
    res = d+1;
    url->s = d-urlb;
  }

  *dest = (reliq_cstr){
    .b = res,
    .s = resl
  };
}

static void
get_path(reliq_cstr *url, reliq_cstr *dest)
{
  const char *res = NULL;
  size_t resl = 0;

  const char *urlb = url->b;
  const size_t urll = url->s;


  size_t i = 0;
  for (; i < urll; i++) {
    char c = urlb[i];
    if (c == '#' || c == '?')
      break;
  }
  if (i) {
    res = urlb;
    url->b += i;
    resl = i;
    url->s -= i;
  }

  *dest = (reliq_cstr){
    .b = res,
    .s = resl
  };
}

const reliq_cstr scheme_uses_params[] = {
  {"ftp",3}, {"hdl",3}, {"prospero",8},
  {"http",4}, {"imap",4}, {"https",5},
  {"shttp",5}, {"rtsp",4}, {"rtsps",5},
  {"rtspu",5}, {"sip",3}, {"sips",4},
  {"mms",3}, {"sftp",4}, {"tel",3}
};

const reliq_cstr scheme_uses_relative[] = {
  {"ftp",3}, {"http",4}, {"gopher",6},
  {"nntp",4}, {"imap",4}, {"wais",4},
  {"file",4}, {"https",5}, {"shttp",5},
  {"mms",3}, {"prospero",8}, {"rtsp",4},
  {"rtsps",5}, {"rtspu",5}, {"sftp",4},
  {"svn",3}, {"svn+ssh",7}, {"ws",2},
  {"wss",3}
};

const reliq_cstr scheme_uses_netloc[] = {
  {"ftp",3}, {"http",4}, {"gopher",6},
  {"nntp",4}, {"telnet",6}, {"imap",4},
  {"wais",4}, {"file",4}, {"mms",3},
  {"https",5}, {"shttp",5}, {"snews",5},
  {"prospero",8}, {"rtsp",4}, {"rtsps",5},
  {"rtspu",5}, {"rsync",5}, {"svn",3},
  {"svn+ssh",7}, {"sftp",4}, {"nfs",3},
  {"git",3}, {"git+ssh",7}, {"ws",2},
  {"wss",3}, {"itms-services",13}
};

uchar
scheme_in_list(const reliq_cstr *scheme, const reliq_cstr *scheme_list, const size_t len)
{
  const char *b = scheme->b;
  const size_t s = scheme->s;
  for (size_t i = 0; i < len; i++)
    if (scheme_list[i].s == s && memcasecmp(b,scheme_list[i].b,s) == 0)
      return 1;
  return 0;
}
#define scheme_in(x,y) scheme_in_list(x,y,LENGTH(y))

static void
get_params(reliq_cstr *path, reliq_cstr *scheme, reliq_cstr *dest)
{
  const char *params = NULL;
  size_t paramsl = 0;

  if (!path->b || (scheme->s != 0 && !scheme_in(scheme,scheme_uses_params)))
    goto END;

  const char *search = path->b;
  size_t searchl = path->s;

  char *slash = memrchr(search,'/',searchl);
  if (slash) {
    slash++;
    searchl -= slash-search;
    search = slash;
  }

  char *delim = memchr(search,';',searchl);
  if (!delim)
    goto END;

  size_t traversed = delim-path->b;
  params = delim+1;
  if (path->s > traversed+1)
    paramsl = path->s-traversed-1;
  path->s = traversed;

  END: ;
  *dest = (reliq_cstr){
    .b = params,
    .s = paramsl
  };
}

static size_t
url_finalize_size(const reliq_url *url)
{
  size_t r = 0;

  if (url->scheme.s)
    r += url->scheme.s+1; // ":"
  if (url->netloc.s)
    r += url->netloc.s+2; // "//"
  r += url->path.s;
  if (url->params.s)
    r += url->params.s+1; // ";"
  if (url->fragment.s)
    r += url->fragment.s+1; // "#"
  if (url->query.s)
    r += url->query.s+1; // "?"

  return r;
}

static inline void
append_to(char **dest, reliq_cstr *str)
{
  if (str->s == 0)
    return;
  *dest = mempcpy(*dest,str->b,str->s);
}

static inline void
append_finalize(char **dest, reliq_cstr *str)
{
  char *t = *dest;
  append_to(dest,str);
  str->b = t;
}

static inline void
append_c_to(char **dest, char c)
{
  **dest = c;
  *dest += 1;
}

static char *
url_finalize_copy_before(reliq_url *url, char *dest)
{
  if (url->scheme.s) {
    append_finalize(&dest,&url->scheme);
    append_c_to(&dest,':');
  }

  if (url->netloc.s) {
    append_c_to(&dest,'/');
    append_c_to(&dest,'/');
    append_finalize(&dest,&url->netloc);
  }
  return dest;
}

static char *
url_finalize_copy_after(reliq_url *url, char *dest)
{
  if (url->params.s) {
    append_c_to(&dest,';');
    append_finalize(&dest,&url->params);
  }

  if (url->fragment.s) {
    append_c_to(&dest,'#');
    append_finalize(&dest,&url->fragment);
  }
  if (url->query.s) {
    append_c_to(&dest,'?');
    append_finalize(&dest,&url->query);
  }
  return dest;
}

static void
url_finalize(reliq_url *url)
{
  const size_t s = url_finalize_size(url);
  char *u = NULL;
  if (!s)
    goto END;

  char *t = u = malloc(s);

  t = url_finalize_copy_before(url,t);
  append_finalize(&t,&url->path);
  t = url_finalize_copy_after(url,t);

  END: ;
  url->url = (reliq_str){
    .b = u,
    .s = s
  };
}

static size_t
url_finalize_path_size(const struct urldir *dirs, const size_t dirs_size)
{
  size_t r = dirs_size-1; //slashes in between
  for (size_t i = 0; i < dirs_size; i++)
    r += dirs[i].to-dirs[i].from;
  return r;
}

static void
url_finalize_path(reliq_url *url, const struct urldir *dirs, const size_t dirs_size, const uchar first_slash, const uchar last_slash)
{
  url->path.s = url_finalize_path_size(dirs,dirs_size)+first_slash+last_slash;
  const size_t s = url_finalize_size(url);
  char *u = NULL;
  if (!s)
    goto END;

  char *t = u = malloc(s);

  t = url_finalize_copy_before(url,t);

  url->path.b = t;
  if (first_slash)
    append_c_to(&t,'/');

  for (size_t i = 0; i < dirs_size; i++) {
    if (i != 0)
      append_c_to(&t,'/');

    const char *b = dirs[i].from;
    const size_t s = dirs[i].to-dirs[i].from;
    t = mempcpy(t,b,s);
  }

  if (last_slash)
    append_c_to(&t,'/');

  t = url_finalize_copy_after(url,t);

  END: ;
  url->url = (reliq_str){
    .b = u,
    .s = s
  };
}

void
reliq_url_free(reliq_url *url)
{
  free(url->url.b);
}

void
reliq_url_parse(const char *url, const size_t urll, const char *scheme, size_t schemel, reliq_url *dest)
{
  if (!urll || !url) {
    memset(dest,0,sizeof(reliq_url));
    return;
  }

  reliq_str u = {
    .b = memcpy(alloca(urll),url,urll),
    .s = urll
  };

  trim_url((reliq_cstr*)&u);
  remove_unsafe(&u);

  get_scheme((reliq_cstr*)&u,scheme,schemel,&dest->scheme);
  get_netloc((reliq_cstr*)&u,&dest->netloc);
  get_path((reliq_cstr*)&u,&dest->path);
  get_params(&dest->path,&dest->scheme,&dest->params);
  get_by_delim((reliq_cstr*)&u,&dest->fragment,'#');
  get_by_delim((reliq_cstr*)&u,&dest->query,'?');

  url_finalize(dest);
}

static inline void
urldirs_append(struct urldir *dirs, size_t *size, const char *path, const size_t pathl)
{
  size_t i = 0;
  while (i < pathl) {
    uint32_t from = i;
    while (i < pathl && path[i] != '/')
      i++;
    uint32_t to = i;

    if (to-from > 0) {
      assert(*size+1 < DIRS_MAX);
      dirs[(*size)++] = (struct urldir){
        .from = from+path,
        .to = to+path
      };
    }

    while (i < pathl && path[i] == '/')
      i++;
  }
}

static void
url_join_mkpath(struct urldir *dirs, size_t *dirs_size, uchar *first_slash, uchar *last_slash, const reliq_cstr *path, const reliq_cstr *rpath)
{
  size_t dirs_sz = 0;
  *last_slash = 0;
  *first_slash = 1;

  if (path->s && path->b[0] == '/') {
    urldirs_append(dirs,&dirs_sz,path->b,path->s);
  } else {
    if (rpath->s) {
      urldirs_append(dirs,&dirs_sz,rpath->b,rpath->s);
      if (dirs_sz && rpath->b[rpath->s-1] != '/')
        dirs_sz--;
    } else if (!path->s)
      *first_slash = 0;

    urldirs_append(dirs,&dirs_sz,path->b,path->s);
  }

  if (path->s > 1 && path->b[path->s-1] == '/')
    *last_slash = 1;

  for (size_t i = 0; i < dirs_sz;) {
    const char *b = dirs[i].from;
    size_t s = dirs[i].to-dirs[i].from;

    uchar drop = 0;
    if (s == 1 && b[0] == '.') {
      drop = 0;
    } else if (s == 2 && b[0] == '.' && b[1] == '.') {
      drop = 1;
    } else {
      i++;
      continue;
    }

    if (i+1 == dirs_sz) {
      *last_slash = 1;
    } else
      memmove(dirs+i-(i && drop),dirs+i+1,sizeof(struct urldir)*(dirs_sz-i-1));

    dirs_sz -= 1+drop;

    if (i)
     i -= drop;
  }

  *dirs_size = dirs_sz;
}

void
reliq_url_join(const reliq_url *ref, const reliq_url *url, reliq_url *dest)
{
  reliq_url u;
  memcpy(&u,url,sizeof(reliq_url));

  if (!memcasecomp(ref->scheme.b,u.scheme.b,ref->scheme.s,u.scheme.s)
    || (u.scheme.s != 0 && !scheme_in(&u.scheme,scheme_uses_relative)))
      goto END_PRE;

  if (u.scheme.s == 0 || scheme_in(&u.scheme,scheme_uses_netloc)) {
    if (u.netloc.s)
      goto END_PRE;
    memcpy(&u.netloc,&ref->netloc,sizeof(reliq_cstr));
  }

  if (!u.path.s && !u.params.s) {
    memcpy(&u.path,&ref->path,sizeof(reliq_cstr));
    memcpy(&u.params,&ref->params,sizeof(reliq_cstr));
    if (!u.query.s)
      memcpy(&u.query,&ref->query,sizeof(reliq_cstr));

    goto END_PRE;
  }

  struct urldir dirs[DIRS_MAX];
  size_t dirs_size = 0;
  uchar first_slash,last_slash;
  url_join_mkpath(dirs,&dirs_size,&first_slash,&last_slash,&u.path,&ref->path);

  url_finalize_path(&u,dirs,dirs_size,first_slash,last_slash);
  memcpy(dest,&u,sizeof(reliq_url));
  return;

  END_PRE: ;
  url_finalize(&u);
  memcpy(dest,&u,sizeof(reliq_url));
}
