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

#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "decode_entities.h"
#include "range.h"
#include "hnode_print.h"
#include "format.h"
#include "edit.h"

#define LINE_EDIT_INC (1<<8)

int
edit_get_arg_delim(const void *args[4], const int num, const uint8_t flag, char *delim)
{
  const void *arg = args[num];
  if (!arg)
    return 0;
  if (!(flag&(FORMAT_ARG0_ISSTR<<num)))
    return -1;

  reliq_str *str = (reliq_str*)arg;
  if (!str->b || !str->s)
    return 0;

  char d = *str->b;
  if (d == '\\' && str->s > 1) {
    d = splchar2(str->b+1,str->s-1,NULL);
    if (d != '\\' && d == str->b[1])
      d = '\\';
  }
  *delim = d;
  return 1;
}

reliq_cstr
edit_cstr_get_line(const char *src, const size_t size, size_t *saveptr, const char delim)
{
  reliq_cstr ret = {NULL,0};
  size_t startline = *saveptr;
  while(*saveptr < size && src[*saveptr] != delim)
    (*saveptr)++;
  if (*saveptr < size && src[*saveptr] == delim)
    (*saveptr)++;
  if (startline != *saveptr) {
    ret.b = src+startline;
    ret.s = *saveptr-startline;
  }
  return ret;
}

//without delim saved
static reliq_cstr
cstr_get_line_d(const char *src, const size_t size, size_t *saveptr, const char delim)
{
  reliq_cstr ret = edit_cstr_get_line(src,size,saveptr,delim);
  if (ret.b && ret.b[ret.s-1] == delim)
    ret.s--;
  return ret;
}

static void
echo_edit_print(reliq_str *str, SINK *output)
{
  const char *b = str->b;
  const size_t size = str->s;
  for (size_t i = 0; i < size; i++) {
    if (b[i] == '\\' && i+1 < size) {
      size_t resultl,traversed;
      char result[8];
      i++;
      splchar3(b+i,size-i,result,&resultl,&traversed);
      if (resultl != 0) {
        sink_write(output,result,resultl);
        i += traversed-1;
        continue;
      } else
        i--;
    }
    sink_put(output,b[i]);
  }
}

reliq_error *
echo_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  reliq_str *str[2] = {NULL};
  const char argv0[] = "echo";

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR) {
      if (((reliq_str*)arg[0])->b && ((reliq_str*)arg[0])->s)
        str[0] = (reliq_str*)arg[0];
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }
  if (arg[1]) {
    if (flag&FORMAT_ARG1_ISSTR) {
      if (((reliq_str*)arg[1])->b && ((reliq_str*)arg[1])->s)
        str[1] = (reliq_str*)arg[1];
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);
  }

  if (!str[0] && !str[1])
    return script_err("%s: missing arguments",argv0);

  if (str[0] && str[0]->s)
    echo_edit_print(str[0],output);
  sink_write(output,src,size);
  if (str[1] && str[1]->s)
    echo_edit_print(str[1],output);

  return NULL;
}

reliq_error *
uniq_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "uniq";

  if (edit_get_arg_delim(arg,0,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);

  reliq_cstr line,previous;
  size_t saveptr = 0;

  previous = cstr_get_line_d(src,size,&saveptr,delim);
  if (!previous.b)
    return NULL;

  while (1) {
    REPEAT: ;
    line = cstr_get_line_d(src,size,&saveptr,delim);
    if (!line.b) {
      sink_write(output,previous.b,previous.s);
      sink_put(output,delim);
      break;
    }

    if (strcomp(line,previous))
      goto REPEAT;
    sink_write(output,previous.b,previous.s);
    sink_put(output,delim);
    previous = line;
  }
  return NULL;
}

static int
sort_cmp(const reliq_cstr *s1, const reliq_cstr *s2)
{
  if (!s1->s)
    return -1;
  if (!s2->s)
    return 1;
  const size_t s = (s1->s > s2->s) ? s2->s : s1->s;
  return memcmp(s1->b,s2->b,s);
}

reliq_error *
sort_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  uchar reverse=0,unique=0; //,natural=0,icase=0;
  const char argv0[] = "sort";

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR && ((reliq_str*)arg[0])->b) {
      reliq_str *str = (reliq_str*)arg[0];
      for (size_t i = 0; i < str->s; i++) {
        if (str->b[i] == 'r') {
          reverse = 1;
        } /* else if (str->b[i] == 'n') {
          natural = 1;
        } else if (str->b[i] == 'i') {
          icase = 1;
        } */ else if (str->b[i] == 'u')
          unique = 1;
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }

  if (edit_get_arg_delim(arg,1,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);

  flexarr *lines = flexarr_init(sizeof(reliq_cstr),LINE_EDIT_INC);
  reliq_cstr line,previous;
  size_t saveptr = 0;

  while (1) {
    line = cstr_get_line_d(src,size,&saveptr,delim);
    if (!line.b)
      break;
    *(reliq_cstr*)flexarr_inc(lines) = line;
  }
  qsort(lines->v,lines->size,sizeof(reliq_cstr),(int(*)(const void*,const void*))sort_cmp);
  reliq_cstr *linesv = (reliq_cstr*)lines->v;

  if (reverse) {
    for (size_t i=0,j=lines->size-1; i < j; i++,j--) {
      line = linesv[i];
      linesv[i] = linesv[j];
      linesv[j] = line;
    }
  }

  if (!lines->size)
    goto END;
  size_t i = 0;
  previous = linesv[i];

  while (1) {
    REPEAT: ;
    if (++i >= lines->size)
      break;
    if (unique && strcomp(previous,linesv[i]))
      goto REPEAT;
    sink_write(output,previous.b,previous.s);
    sink_put(output,delim);
    previous = linesv[i];
  }
  sink_write(output,previous.b,previous.s);
  sink_put(output,delim);

  END: ;
  flexarr_free(lines);
  return NULL;
}

reliq_error *
line_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  reliq_range *range = NULL;
  const char argv0[] = "line";

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR)
      return script_err("%s: arg %d: incorrect type of argument, expected range",argv0,1);
    range = (reliq_range*)arg[0];
  }

  if (edit_get_arg_delim(arg,1,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);

  if (!range)
    return script_err("%s: missing arguments",argv0);

  size_t saveptr=0,linecount=0,currentline=0;
  reliq_cstr line;

  while (1) {
    line = edit_cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    linecount++;
  }

  if (linecount)
      linecount--;

  saveptr = 0;
  while (1) {
    line = edit_cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    if (range_match(currentline,range,linecount))
      sink_write(output,line.b,line.s);
    currentline++;
  }
  return NULL;
}

reliq_error *
cut_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  uchar delim[256]={0};
  uchar complement=0,onlydelimited=0,delimited=0;
  char linedelim = '\n';
  const char argv0[] = "cut";
  reliq_error *err;

  reliq_range *range = NULL;

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR)
      script_err("%s: arg %d: incorrect type of argument, expected range",argv0,1);
    range = (reliq_range*)arg[0];
  }

  if (arg[1]) {
    if (flag&FORMAT_ARG1_ISSTR) {
      if (((reliq_str*)arg[1])->b && ((reliq_str*)arg[1])->s) {
        reliq_str *str = (reliq_str*)arg[1];
        if ((err = tr_strrange(str->b,str->s,NULL,0,delim,NULL,0)))
          return err;
        delimited = 1;
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);
  }
  if (arg[2]) {
    if (flag&FORMAT_ARG2_ISSTR) {
      if (((reliq_str*)arg[2])->b) {
        reliq_str *str = (reliq_str*)arg[2];
        for (size_t i = 0; i < str->s; i++) {
          if (str->b[i] == 's') {
            onlydelimited = 1;
          } else if (str->b[i] == 'c') {
            complement = 1;
          } else if (str->b[i] == 'z')
            linedelim = '\0';
        }
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,3);
  }

  if (edit_get_arg_delim(arg,3,flag,&linedelim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,4);

  if (!range)
    return script_err("%s: missing range argument",argv0);

  reliq_cstr line;
  size_t saveptr = 0;
  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;
  uchar printlinedelim;

  while (1) {
    line = cstr_get_line_d(src,size,&saveptr,linedelim);
    if (!line.b)
      return NULL;

    printlinedelim = 1;
    size_t start=line.b-src,end=(line.b-src)+line.s;
    if (delimited) {
      size_t dstart,dend,dlength,dcount=0,dprevend=start,dprevendlength=0;
      if (onlydelimited)
        printlinedelim = 0;
      while (1) {
        dstart = start;
        dend = dstart;
        while (!delim[(uchar)src[dend]] && dend < end)
          dend++;
        dlength = dend-dstart;
        if (delim[(uchar)src[dend]] && dend < end)
          dend++;
        if (dlength != dend-dstart) {
          printlinedelim = 1;
        } else if (!onlydelimited && start == (size_t)(line.b-src)) {
          sink_write(output,src+dstart,dlength);
          goto PRINT;
        }

        start = dend;
        if (range_match(dcount,range,RANGE_UNSIGNED)^complement) {
          if (dprevendlength)
            sink_write(output,src+dprevend,1);
          if (dlength)
            sink_write(output,src+dstart,dlength);
          dprevendlength = 1;
        }
        dprevend = dstart+dlength;
        if (dprevend >= end)
          break;
        dcount++;
      }
    } else {
      for (size_t i = start; i < end; i++) {
        if (range_match(i-start,range,end-start-1)^complement) {
          buf[bufcurrent++] = src[i];
          if (bufcurrent == bufsize) {
            sink_write(output,buf,bufcurrent);
            bufcurrent = 0;
          }
        }
      }
      if (bufcurrent) {
        sink_write(output,buf,bufcurrent);
        bufcurrent = 0;
      }
    }
    PRINT: ;
    size_t n = 0;
    if (line.b[line.s] == linedelim)
      n = 1;
    while (saveptr < size && src[saveptr] == linedelim) {
      saveptr++;
      n++;
    }
    if (printlinedelim) {
      if (!n || (delimited && onlydelimited)) {
        sink_put(output,linedelim);
      } else for (size_t i = 0; i < n; i++)
        sink_put(output,linedelim);
    }
  }

  return NULL;
}

reliq_error *
trim_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\0';
  const char argv0[] = "trim";
  uchar hasdelim = 0;

  int r = edit_get_arg_delim(arg,0,flag,&delim);
  if (r == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  if (r == 1)
    hasdelim = 1;

  size_t line=0,delimstart,lineend;

  while (line < size) {
    if (hasdelim) {
        delimstart = line;
        while (line < size && src[line] == delim)
          line++;
        if (line-delimstart)
          sink_write(output,src+delimstart,line-delimstart);
        lineend = line;
        while (lineend < size && src[lineend] != delim)
          lineend++;
    } else
      lineend = size;

    if (lineend-line) {
      char const *trimmed;
      size_t trimmedl = 0;
      memtrim(&trimmed,&trimmedl,src+line,lineend-line);
      if (trimmedl)
        sink_write(output,trimmed,trimmedl);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
rev_edit(char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "rev";

  if (edit_get_arg_delim(arg,0,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);

  size_t line=0,delimstart,lineend;

  while (line < size) {
    delimstart = line;
    while (line < size && src[line] == delim)
      line++;
    if (line-delimstart)
      sink_write(output,src+delimstart,line-delimstart);
    lineend = line;
    while (lineend < size && src[lineend] != delim)
      lineend++;

    size_t linel = lineend-line;
    if (linel) {
      strnrev(src+line,linel);
      sink_write(output,src+line,linel);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
tac_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "tac";

  if (edit_get_arg_delim(arg,0,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);

  size_t saveptr=0;
  flexarr *lines = flexarr_init(sizeof(reliq_cstr),LINE_EDIT_INC);
  reliq_cstr line;

  while (1) {
    line = edit_cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    memcpy(flexarr_inc(lines),&line,sizeof(reliq_cstr));
  }

  reliq_cstr *linesv = lines->v;
  const size_t linessize = lines->size;
  for (size_t i = linessize; i; i--)
    sink_write(output,linesv[i-1].b,linesv[i-1].s);

  flexarr_free(lines);
  return NULL;
}

reliq_error *
decode_edit(const char *src, const size_t size, SINK *output, const void UNUSED *arg[4], const uint8_t UNUSED flag)
{
  const char argv0[] = "decode";
  bool exact = false;

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR && ((reliq_str*)arg[0])->b) {
      reliq_str *str = (reliq_str*)arg[0];
      for (size_t i = 0; i < str->s; i++) {
        if (str->b[i] == 'e')
          exact = true;
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }

  reliq_decode_entities_sink(src,size,output,!exact);
  return NULL;
}
