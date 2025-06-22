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
#include <string.h>

#include "utils.h"
#include "range.h"
#include "format.h"
#include "edit.h"
#include "entities.h"
#include "ctype.h"

#define LINE_EDIT_INC (1<<8)

reliq_error *
edit_missing_arg(const char *argv0)
{
  return script_err("%s: missing arguments",argv0);
}

reliq_error *
edit_arg_str(const edit_args *args, const char *argv0, const uchar num, reliq_cstr **dest)
{
  const void *arg = args->arg[num];
  *dest = NULL;
  if (!arg)
    return NULL;

  const uint8_t flags = args->flags;
  if (flags&(FORMAT_ARG0_ISSTR<<num)) {
    reliq_cstr *str = (reliq_cstr*)arg;
    if (str->b) {
      *dest = str;
    }
    return NULL;
  } else
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,num+1);
}

reliq_error *
edit_arg_delim(const edit_args *args, const char *argv0, const uchar num, char *delim, uchar *found)
{
  reliq_error *err = NULL;

  reliq_cstr *str;
  if ((err = edit_arg_str(args,argv0,num,&str)))
    return err;

  if (!str || str->s == 0) {
    if (found)
      *found = 0;
    return err;
  }

  char d = *str->b;
  if (d == '\\' && str->s > 1) {
    d = splchar2(str->b+1,str->s-1,NULL);
    if (d != '\\' && d == str->b[1])
      d = '\\';
  }
  *delim = d;
  if (found)
    *found = 1;
  return err;
}

reliq_error *
edit_arg_range(const edit_args *args, const char *argv0, const uchar num, reliq_range const **dest)
{
  const void *arg = args->arg[num];
  *dest = NULL;
  if (!arg)
    return NULL;
  const uint8_t flags = args->flags;
  if (flags&(FORMAT_ARG0_ISSTR<<num))
    return script_err("%s: arg %d: incorrect type of argument, expected range",argv0,num+1);

  *dest = (const reliq_range*)arg;
  return NULL;
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
echo_edit_print(reliq_cstr *str, SINK *output)
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
echo_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  reliq_cstr *str[2] = {NULL};
  const char argv0[] = "echo";

  reliq_error *err;
  if ((err = edit_arg_str(args,argv0,0,&str[0])))
    return err;
  if ((err = edit_arg_str(args,argv0,1,&str[1])))
    return err;

  if (!str[0] && !str[1])
    return edit_missing_arg(argv0);

  if (str[0] && str[0]->s)
    echo_edit_print(str[0],output);
  sink_write(output,src->b,src->s);
  if (str[1] && str[1]->s)
    echo_edit_print(str[1],output);

  return NULL;
}

reliq_error *
uniq_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  const char argv0[] = "uniq";

  reliq_error *err;
  if ((err = edit_arg_delim(args,argv0,0,&delim,NULL)))
    return err;

  const char *str = src->b;
  const size_t strl = src->s;
  reliq_cstr line,previous;
  size_t saveptr = 0;

  previous = cstr_get_line_d(str,strl,&saveptr,delim);
  if (!previous.b)
    return NULL;

  while (1) {
    REPEAT: ;
    line = cstr_get_line_d(str,strl,&saveptr,delim);
    if (!line.b) {
      sink_write(output,previous.b,previous.s);
      sink_put(output,delim);
      break;
    }

    if (streq(line,previous))
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
  const size_t s = MIN(s1->s,s2->s);
  return memcmp(s1->b,s2->b,s);
}

reliq_error *
sort_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  uchar reverse=0,unique=0; //,natural=0,icase=0;
  const char argv0[] = "sort";

  reliq_error *err;
  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,0,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 'r') {
        reverse = 1;
      } /* else if (flags->b[i] == 'n') {
        natural = 1;
      } else if (flags->b[i] == 'i') {
        icase = 1;
      } */ else if (flags->b[i] == 'u')
        unique = 1;
    }
  }

  if ((err = edit_arg_delim(args,argv0,1,&delim,NULL)))
    return err;

  flexarr lines = flexarr_init(sizeof(reliq_cstr),LINE_EDIT_INC);
  reliq_cstr line,previous;
  size_t saveptr = 0;
  const char *str = src->b;
  const size_t strl = src->s;

  while (1) {
    line = cstr_get_line_d(str,strl,&saveptr,delim);
    if (!line.b)
      break;
    *(reliq_cstr*)flexarr_inc(&lines) = line;
  }
  qsort(lines.v,lines.size,sizeof(reliq_cstr),(int(*)(const void*,const void*))sort_cmp);
  reliq_cstr *linesv = (reliq_cstr*)lines.v;

  if (reverse) {
    for (size_t i=0,j=lines.size-1; i < j; i++,j--) {
      line = linesv[i];
      linesv[i] = linesv[j];
      linesv[j] = line;
    }
  }

  if (!lines.size)
    goto END;
  size_t i = 0;
  previous = linesv[i];

  while (1) {
    REPEAT: ;
    if (++i >= lines.size)
      break;
    if (unique && streq(previous,linesv[i]))
      goto REPEAT;
    sink_write(output,previous.b,previous.s);
    sink_put(output,delim);
    previous = linesv[i];
  }
  sink_write(output,previous.b,previous.s);
  sink_put(output,delim);

  END: ;
  flexarr_free(&lines);
  return NULL;
}

reliq_error *
line_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  const char argv0[] = "line";

  const reliq_range *range;
  reliq_error *err;
  if ((err = edit_arg_range(args,argv0,0,&range)))
    return err;

  if ((err = edit_arg_delim(args,argv0,1,&delim,NULL)))
    return err;

  if (!range)
    return edit_missing_arg(argv0);

  size_t saveptr=0,linecount=0,currentline=0;
  reliq_cstr line;
  const char *str = src->b;
  const size_t strl = src->s;

  while (1) {
    line = edit_cstr_get_line(str,strl,&saveptr,delim);
    if (!line.b)
      break;
    linecount++;
  }

  if (linecount)
      linecount--;

  saveptr = 0;
  while (1) {
    line = edit_cstr_get_line(str,strl,&saveptr,delim);
    if (!line.b)
      break;
    if (range_match(currentline,range,linecount))
      sink_write(output,line.b,line.s);
    currentline++;
  }
  return NULL;
}

reliq_error *
cut_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  uchar delim[256]={0};
  uchar complement=0,onlydelimited=0,delimited=0;
  char linedelim = '\n';
  const char argv0[] = "cut";
  reliq_error *err;

  const reliq_range *range;
  if ((err = edit_arg_range(args,argv0,0,&range)))
    return err;

  if (!range)
    return edit_missing_arg(argv0);

  reliq_cstr *f_delim;
  if ((err = edit_arg_str(args,argv0,1,&f_delim)))
    return err;

  if (f_delim && f_delim->b && f_delim->s) {
    if ((err = tr_strrange(f_delim->b,f_delim->s,NULL,0,delim,NULL,0)))
      return err;
    delimited = 1;
  }

  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,2,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 's') {
        onlydelimited = 1;
      } else if (flags->b[i] == 'c') {
        complement = 1;
      } else if (flags->b[i] == 'z')
        linedelim = '\0';
    }
  }

  if ((err = edit_arg_delim(args,argv0,3,&linedelim,NULL)))
    return err;

  reliq_cstr line;
  size_t saveptr = 0;
  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;
  uchar printlinedelim;

  const char *str = src->b;
  const size_t strl = src->s;

  while (1) {
    line = cstr_get_line_d(str,strl,&saveptr,linedelim);
    if (!line.b)
      return NULL;

    printlinedelim = 1;
    size_t start=line.b-str,end=(line.b-str)+line.s;
    if (delimited) {
      size_t dstart,dend,dlength,dcount=0,dprevend=start,dprevendlength=0;
      if (onlydelimited)
        printlinedelim = 0;
      while (1) {
        dstart = start;
        dend = dstart;
        while (!delim[(uchar)str[dend]] && dend < end)
          dend++;
        dlength = dend-dstart;
        if (delim[(uchar)str[dend]] && dend < end)
          dend++;
        if (dlength != dend-dstart) {
          printlinedelim = 1;
        } else if (!onlydelimited && start == (size_t)(line.b-str)) {
          sink_write(output,str+dstart,dlength);
          goto PRINT;
        }

        start = dend;
        if (range_match(dcount,range,RANGE_UNSIGNED)^complement) {
          if (dprevendlength)
            sink_write(output,str+dprevend,1);
          if (dlength)
            sink_write(output,str+dstart,dlength);
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
          buf[bufcurrent++] = str[i];
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
    while (saveptr < strl && str[saveptr] == linedelim) {
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
trim_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\0';
  const char argv0[] = "trim";
  uchar hasdelim = 0;

  reliq_error *err;
  if ((err = edit_arg_delim(args,argv0,0,&delim,&hasdelim)))
    return err;

  size_t line=0,delimstart,lineend;
  const char *str = src->b;
  const size_t strl = src->s;

  while (line < strl) {
    if (hasdelim) {
        delimstart = line;
        while (line < strl && str[line] == delim)
          line++;
        if (line-delimstart)
          sink_write(output,str+delimstart,line-delimstart);
        lineend = line;
        while (lineend < strl && str[lineend] != delim)
          lineend++;
    } else
      lineend = strl;

    if (lineend-line) {
      char const *trimmed;
      size_t trimmedl = 0;
      memtrim(&trimmed,&trimmedl,str+line,lineend-line);
      if (trimmedl)
        sink_write(output,trimmed,trimmedl);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
rev_edit(const reliq_str *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  const char argv0[] = "rev";

  reliq_error *err;
  if ((err = edit_arg_delim(args,argv0,0,&delim,NULL)))
    return err;

  size_t line=0,delimstart,lineend;
  char *str = src->b;
  size_t strl = src->s;

  while (line < strl) {
    delimstart = line;
    while (line < strl && str[line] == delim)
      line++;
    if (line-delimstart)
      sink_write(output,str+delimstart,line-delimstart);
    lineend = line;
    while (lineend < strl && str[lineend] != delim)
      lineend++;

    size_t linel = lineend-line;
    if (linel) {
      strnrev(str+line,linel);
      sink_write(output,str+line,linel);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
tac_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  const char argv0[] = "tac";

  reliq_error *err;
  if ((err = edit_arg_delim(args,argv0,0,&delim,NULL)))
    return err;

  size_t saveptr=0;
  flexarr lines = flexarr_init(sizeof(reliq_cstr),LINE_EDIT_INC);
  reliq_cstr line;
  const char *str = src->b;
  const size_t strl = src->s;

  while (1) {
    line = edit_cstr_get_line(str,strl,&saveptr,delim);
    if (!line.b)
      break;
    *(reliq_cstr*)flexarr_inc(&lines) = line;
  }

  reliq_cstr *linesv = lines.v;
  const size_t linessize = lines.size;
  for (size_t i = linessize; i; i--)
    sink_write(output,linesv[i-1].b,linesv[i-1].s);

  flexarr_free(&lines);
  return NULL;
}

reliq_error *
decode_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  const char argv0[] = "decode";
  bool exact = false;

  reliq_error *err;
  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,0,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 'e')
        exact = true;
    }
  }

  reliq_decode_entities_sink(src->b,src->s,output,!exact);
  return NULL;
}

reliq_error *
encode_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  const char argv0[] = "encode";
  bool full = false;

  reliq_error *err;
  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,0,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 'f')
        full = true;
    }
  }

  reliq_encode_entities_sink(src->b,src->s,output,full);
  return NULL;
}

reliq_error *
lower_edit(const reliq_cstr *src, SINK *output, const UNUSED edit_args *args)
{
  const char *str = src->b;
  const size_t strl = src->s;

  for (size_t i = 0; i < strl; i++)
    sink_put(output,tolower_inline(str[i]));

  return NULL;
}

reliq_error *
upper_edit(const reliq_cstr *src, SINK *output, const UNUSED edit_args *args)
{
  const char *str = src->b;
  const size_t strl = src->s;

  for (size_t i = 0; i < strl; i++)
    sink_put(output,toupper_inline(str[i]));

  return NULL;
}
