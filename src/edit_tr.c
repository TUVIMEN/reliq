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

#include "ext.h"

#include <stdlib.h>
#include <string.h>

#include "ctype.h"
#include "utils.h"
#include "sink.h"
#include "format.h"
#include "edit.h"

const struct { char *name; size_t namel; const char *arr; } tr_ctypes[] = {
  {"space",5,IS_SPACE},
  {"alnum",5,IS_ALNUM},
  {"alpha",5,IS_ALPHA},
  {"blank",5,IS_BLANK},
  {"cntrl",5,IS_CNTRL},
  {"digit",5,IS_DIGIT},
  {"graph",5,IS_GRAPH},
  {"lower",5,IS_LOWER},
  {"print",5,IS_PRINT},
  {"punct",5,IS_PUNCT},
  {"upper",5,IS_UPPER},
  {"xdigit",6,IS_XDIGIT}
};

static char const *
tr_match_ctypes(const char *name, const size_t namel) {
  for (size_t i = 0; i < LENGTH(tr_ctypes); i++)
    if (memeq(tr_ctypes[i].name,name,tr_ctypes[i].namel,namel))
      return tr_ctypes[i].arr;
  return NULL;
}

static int
tr_strrange_next(const char *src, const size_t size, size_t *pos, int *rstart, int *rend, char const **array, int *repeat, int *hasended, reliq_error **err)
{
  *err = NULL;
  if (*repeat != -1 && *rstart != -1) {
    if (*pos >= size && *repeat == 0) {
      *hasended = 1;
      return *rstart;
    }
    if (*repeat) {
      (*repeat)--;
      return *rstart;
    } else {
      *repeat = -1;
      size_t t = *rstart;
      *rstart = -1;
      if (*rend == 0)
        return t;
    }
  }
  if (*rstart == -1 && *rend != -1) {
    *hasended = 1;
    return *rend;
  }
  if (*rstart != -1 && *rend != -1) {
    int out = -1;
    if (*rstart == *rend) {
      out = *rstart;
      *rstart = -1;
      *rend = -1;
      if (*pos >= size) {
        *rend = out;
      }
    } else if (*rstart < *rend) {
      (*rstart)++;
      out = *rstart-1;
    } else {
      (*rstart)--;
      out = *rstart+1;
    }
    return out;
  }
  if (*rstart != -1 && *array != NULL) {
    for (; *rstart < 256; (*rstart)++)
      if ((*array)[(uchar)*rstart])
        return (*rstart)++;
    if (*pos >= size) {
      *hasended = 1;
      for (; *rstart > 0; (*rstart)--) {
        if ((*array)[(uchar)*rstart-1]) {
          int r = *rstart-1;
          *rstart = 256;
          return r;
        }
      }
      return -1;
    } else {
      *rstart = -1;
      *array = NULL;
    }
  }

  if (*pos >= size) {
    *hasended = 1;
    return -2; //no output
  }

  char ch,och;
  size_t traversed;

  och = src[*pos];
  if (src[*pos] == '\\' && *pos+1 < size) {
    (*pos)++;
    ch = splchar2(src+*pos,size-*pos,&traversed);
    *pos += traversed-1;
  } else
    ch = src[*pos];
  if (*pos+2 < size && src[(*pos)+1] == '-' && (src[(*pos)+2] != '\\' || *pos+3 < size)) {
    (*pos) += 2;
    char second = src[*pos];
    if (second == '\\') {
      (*pos)++;
      second = splchar2(src+*pos,size-*pos,&traversed);
      (*pos) += traversed-1;
    }
    *rstart = ch;
    *rend = second;
    (*pos)++;
    return tr_strrange_next(src,size,pos,rstart,rend,array,repeat,hasended,err);
  } else {
    if (och != '\\' && *pos+5 < size && ch == '[' && src[(*pos)+1] == ':') {
      size_t j = *pos+2;
      while (j < size && src[j] != ':')
        j++;
      if (src[j] == ':' && j+1 < size && src[j+1] == ']') {
        char const *class = src+*pos+2;
        size_t classl = j-(*pos+2);
        char const * ctype = tr_match_ctypes(class,classl);
        *pos = j+2;
        if (ctype) {
          *array = ctype;
          *rstart = 0;
          return tr_strrange_next(src,size,pos,rstart,rend,array,repeat,hasended,err);
        } else {
          *err = script_err("tr: invalid character class '%.*s'",(int)classl,class);
          return -1;
        }
      }
    } else if (och != '\\' && *pos+3 < size && ch == '[' && (src[(*pos)+1] != '\\' || *pos+4 < size)) {
      size_t prevpos = *pos;
      char cha = src[++(*pos)];
      if (cha == '\\') {
        (*pos)++;
        cha = splchar2(src+*pos,size-*pos,&traversed);
        *pos += traversed-1;
      }
      if (src[(*pos)+1] == '*') {
        *pos += 2;
        int num = number_handle(src,pos,size);
        if (num == -1) {
          *rend = 0;
          num = 1;
        }
        if (src[*pos] == ']') {
          *repeat = num;
          *rstart = cha;
          (*pos)++;
          return tr_strrange_next(src,size,pos,rstart,rend,array,repeat,hasended,err);
        }
      }
      *rend = -1;
      *repeat = -1;
      *pos = prevpos;
    }
    (*pos)++;
    return ch;
  }
  return -2;
}

reliq_error *
tr_strrange(const char *src1, const size_t size1, const char *src2, const size_t size2, uchar arr[256], uchar arr_enabled[256], uchar complement)
{
  size_t pos[2]={0};
  int rstart[2]={-1,-1},rend[2]={-1,-1},repeat[2]={-1,-1},hasended[2]={0};
  char const *array[2] = {NULL};
  reliq_error *err;

  int last_r2 = -1;
  while (!hasended[0]) {
    int r1 = tr_strrange_next(src1,size1,&pos[0],&rstart[0],&rend[0],&array[0],&repeat[0],&hasended[0],&err);
    if (err)
      return err;
    if (r1 <= -1 || hasended[0])
      break;
    int r2 = -1;
    if (src2 && !complement && !hasended[1]) {
      r2 = tr_strrange_next(src2,size2,&pos[1],&rstart[1],&rend[1],&array[1],&repeat[1],&hasended[1],&err);
      if (err)
        return err;
      if (r2 == -1)
        break;
      if (r2 != -2)
        last_r2 = r2;
    }
    if (hasended[1] && last_r2 != -1)
      r2 = last_r2;

    if (!complement && arr_enabled)
      arr_enabled[(uchar)r1] = 1;

    arr[(uchar)r1] = (src2 && !complement) ? (uchar)r2 : 1;
  }
  if (complement) {
    int last = 0;
    if (src2) {
      while (!hasended[1]) {
        int r = tr_strrange_next(src2,size2,&pos[1],&rstart[1],&rend[1],&array[1],&repeat[1],&hasended[1],&err);
        if (err)
          return err;
        if (r == -1 || hasended[1])
          break;
        last = r;
      }
      for (uint16_t i = 0; i < 256; i++) {
        if (arr[(uchar)i]) {
          arr[(uchar)i] = 0;
        } else {
          arr[(uchar)i] = last;
          if (arr_enabled)
            arr_enabled[(uchar)i] = 1;
        }
      }
    } else
      for (uint16_t i = 0; i < 256; i++)
        arr[(uchar)i] = !arr[(uchar)i];
  }
  return NULL;
}

reliq_error *
tr_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  uchar array[256] = {0};
  reliq_cstr *string[2] = {NULL};
  uchar complement=0,squeeze=0;
  const char argv0[] = "tr";
  reliq_error *err;

  if ((err = edit_arg_str(args,argv0,0,&string[0])))
    return err;
  if ((err = edit_arg_str(args,argv0,1,&string[1])))
    return err;

  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,2,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 's') {
        squeeze = 1;
      } else if (flags->b[i] == 'c')
        complement = 1;
    }
  }

  if (!string[0])
    return edit_missing_arg(argv0);

  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;
  const char *str = src->b;
  const size_t strl = src->s;

  if (string[0] && !string[1]) {
    if ((err = tr_strrange(string[0]->b,string[0]->s,NULL,0,array,NULL,complement)))
      return err;
    for (size_t i = 0; i < strl; i++) {
      if (!array[(uchar)str[i]]) {
        buf[bufcurrent++] = str[i];
        if (bufcurrent == bufsize) {
          sink_write(output,buf,bufcurrent);
          bufcurrent = 0;
        }
      }
    }
    if (bufcurrent)
      sink_write(output,buf,bufcurrent);
    return NULL;
  }

  uchar array_enabled[256] = {0};
  if ((err = tr_strrange(string[0]->b,string[0]->s,string[1]->b,string[1]->s,array,array_enabled,complement)))
    return err;

  for (size_t i = 0; i < strl; i++) {
    buf[bufcurrent++] = (array_enabled[(uchar)str[i]]) ? array[(uchar)str[i]] : str[i];
    if (bufcurrent == bufsize) {
      sink_write(output,buf,bufcurrent);
      bufcurrent = 0;
    }
    if (squeeze) {
      char previous = str[i];
      while (i+1 < strl && str[i+1] == previous)
        i++;
    }
  }
  if (bufcurrent)
    sink_write(output,buf,bufcurrent);

  return NULL;
}
