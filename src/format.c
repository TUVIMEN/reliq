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
#include "edit.h"
#include "range.h"
#include "hnode_print.h"
#include "ctype.h"
#include "utils.h"
#include "format.h"

#include <assert.h>

#define FORMAT_INC 8

typedef reliq_error *(*reliq_format_function_t)(reliq_str*,SINK*,const reliq_format_func*);

struct {
  cstr8 name;
  reliq_format_function_t func;
} const format_functions[] = {
  {{"sed",3},(reliq_format_function_t)sed_edit},
  {{"trim",4},(reliq_format_function_t)trim_edit},
  {{"tr",2},(reliq_format_function_t)tr_edit},
  {{"line",4},(reliq_format_function_t)line_edit},
  {{"cut",3},(reliq_format_function_t)cut_edit},
  {{"decode",6},(reliq_format_function_t)decode_edit},
  {{"encode",6},(reliq_format_function_t)encode_edit},
  {{"sort",4},(reliq_format_function_t)sort_edit},
  {{"uniq",4},(reliq_format_function_t)uniq_edit},
  {{"echo",4},(reliq_format_function_t)echo_edit},
  {{"wc",2},(reliq_format_function_t)wc_edit},
  {{"rev",3},(reliq_format_function_t)rev_edit},
  {{"tac",3},(reliq_format_function_t)tac_edit},
};

reliq_error *
format_exec(char *input, size_t inputl, SINK *output, const reliq_chnode *hnode, const reliq_chnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq)
{
  reliq_error *err = NULL;
  SINK *out;
  char *ptr[2];
  ptr[1] = NULL;
  size_t fsize[2];
  SINK *sn[2] = {0};
  size_t i = 0;

  if (hnode) {
    out = output;
    if (formatl && (format[0].flags&FORMAT_FUNC) == 0) {
      if (formatl > 1)
        out = sn[0] = sink_open(&ptr[0],&fsize[0]);

      chnode_printf(out,((reliq_cstr*)format[0].arg[0])->b,((reliq_cstr*)format[0].arg[0])->s,hnode,parent,rq);
      i++;
    } else {
      if (formatl)
        out = sn[0] = sink_open(&ptr[0],&fsize[0]);

      chnode_print(out,hnode,rq);
    }

    if (out != output) {
      sink_flush(sn[0]);
      input = ptr[0];
      inputl = fsize[0];
    }
  }
  if (formatl-i > 1) {
    if (!sn[0])
      sn[0] = sink_open(&ptr[0],&fsize[0]);
    sn[1] = sink_open(&ptr[1],&fsize[1]);
  }

  for (; i < formatl; i++) {
    assert(format[i].flags&FORMAT_FUNC);
    out = (i == formatl-1) ? output : sn[1];

    reliq_str str = (reliq_str){
      .b = input,
      .s = inputl
    };
    if ((err = format_functions[(format[i].flags&FORMAT_FUNC)-1].func((reliq_str*)&str,out,format+i)))
      break;

    if (out != output) {
      sink_flush(sn[1]);
      sink_change(sn[1],&ptr[0],&fsize[0],fsize[1]);
      sink_change(sn[0],&ptr[1],&fsize[1],0);

      SINK *t = sn[1];
      sn[1] = sn[0];
      sn[0] = t;

      input = ptr[0];
      inputl = fsize[0];
    }
  }

  if (sn[0])
    sink_destroy(sn[0]);
  if (sn[1])
    sink_destroy(sn[1]);

  return err;
}

static reliq_error *
format_get_func_args(reliq_format_func *f, const char *src, size_t *pos, const size_t size, size_t *argcount)
{
  reliq_error *err = NULL;
  size_t arg=0,i=*pos;
  for (; i < size; arg++) {
    if (arg >= 4)
      goto_script_seterr(END,"too many arguments passed to a function");

    if (src[i] == '"' || src[i] == '\'') {
      char *result;
      size_t resultl;
      if ((err = get_quoted(src,&i,size,' ',&result,&resultl)))
        goto END;

      if (resultl) {
        reliq_str *str = f->arg[arg] = malloc(sizeof(reliq_str));
        str->b = result;
        str->s = resultl;
        f->flags |= (FORMAT_ARG0_ISSTR<<arg);
      }
    } else if (src[i] == '[') {
      reliq_range range;
      if ((err = range_comp(src,&i,size,&range)))
        goto END;
      f->arg[arg] = memdup(&range,sizeof(range));
    }

    if (i >= size)
      break;

    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] != '[' && src[i] != '"' && src[i] != '\'') {
      if (isalnum(src[i]))
        break;
      *argcount = arg+1;
      goto_script_seterr(END,"bad argument at %lu(0x%02x)",i,src[i]);
    }
  }
  *argcount = arg+1;
  END: ;
  *pos = i;
  return err;
}

static reliq_error *
format_get_funcs(flexarr *format, const char *src, size_t *pos, const size_t size) //format: reliq_format_func
{
  reliq_format_func *f;
  char const *fname;
  size_t fnamel=0,i=*pos;
  reliq_error *err = NULL;

  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (isalnum(src[i])) {
      fname = src+i;
      while_is(isalnum,src,i,size);
      fnamel = i-(fname-src);
      if (i < size && !isspace(src[i]))
        goto_script_seterr(END,"format function has to be separated by space from its arguments");
    } else
      fname = NULL;

    f = (reliq_format_func*)flexarr_incz(format);

    while_is(isspace,src,i,size);
    size_t argcount = 0;
    if ((err = format_get_func_args(f,src,&i,size,&argcount)))
      goto END;

    if (fname) {
      char found = 0;
      size_t i = 0;
      for (; i < LENGTH(format_functions); i++) {
        if (memeq(format_functions[i].name.b,fname,format_functions[i].name.s,fnamel)) {
          found = 1;
          break;
        }
      }
      if (!found)
        goto_script_seterr(END,"format function does not exist: \"%.*s\"",fnamel,fname);
      f->flags |= i+1;
    } else if (argcount > 1)
      goto_script_seterr(END,"printf defined two times in format");
  }
  END: ;
  *pos = i;
  return err;
}

void
format_free(reliq_format_func *format, const size_t formatl)
{
  if (!format)
    return;
  for (size_t i = 0; i < formatl; i++) {
    for (size_t j = 0; j < 4; j++) {
      if (!format[i].arg[j])
        continue;

      if (format[i].flags&(FORMAT_ARG0_ISSTR<<j)) {
        if (((reliq_str*)format[i].arg[j])->b)
          free(((reliq_str*)format[i].arg[j])->b);
      } else
        range_free((reliq_range*)format[i].arg[j]);
      free(format[i].arg[j]);
    }
  }
  free(format);
}

reliq_error *
format_comp(const char *src, size_t *pos, const size_t size, reliq_format_func **format, size_t *formatl)
{
  reliq_error *err = NULL;
  *format = NULL;
  *formatl = 0;
  size_t i = *pos;
  if (i >= size || !src)
    goto END;

  flexarr f = flexarr_init(sizeof(reliq_format_func),FORMAT_INC);
  err = format_get_funcs(&f,src,&i,size);
  flexarr_conv(&f,(void**)format,formatl);

  END: ;
  *pos = i;
  return err;
}
