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

#define FORMAT_INC 8

typedef reliq_error *(*reliq_format_function_t)(char*,size_t,SINK*,const void*[4],const uint8_t);

struct reliq_format_function {
  cstr8 name;
  reliq_format_function_t func;
};

const struct reliq_format_function format_functions[] = {
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
  if (hnode && (!formatl || (formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && (!format[0].arg[0] || !((reliq_cstr*)format[0].arg[0])->b)))) {
    chnode_print(output,hnode,rq);
    return NULL;
  }
  if (hnode && formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && format[0].arg[0] && ((reliq_cstr*)format[0].arg[0])->b) {
    chnode_printf(output,((reliq_cstr*)format[0].arg[0])->b,((reliq_cstr*)format[0].arg[0])->s,hnode,parent,rq);
    return NULL;
  }

  SINK *out;
  char *ptr[2];
  ptr[1] = NULL;
  size_t fsize[2];

  for (size_t i = 0; i < formatl; i++) {
    out = (i == formatl-1) ? output : sink_open(&ptr[1],&fsize[1]);
    if (hnode && i == 0 && (format[i].flags&FORMAT_FUNC) == 0) {
      chnode_printf(out,((reliq_cstr*)format[i].arg[0])->b,((reliq_cstr*)format[i].arg[0])->s,hnode,parent,rq);
    } else {
      if (i == 0) {
        if (hnode) {
          SINK *t = sink_open(&ptr[0],&fsize[0]);
          chnode_print(t,hnode,rq);
          sink_close(t);
        } else {
          ptr[0] = input;
          fsize[0] = inputl;
        }
      }
      if (format[i].flags&FORMAT_FUNC) {
        reliq_error *err = format_functions[(format[i].flags&FORMAT_FUNC)-1].func(ptr[0],fsize[0],out,(const void**)format[i].arg,format[i].flags);
        if (err) {
          if (i == 0 && hnode)
            free(ptr[0]);
          return err;
        }
      }
    }

    if (i != formatl-1)
      sink_close(out);
    if (i != 0 || (hnode && (format[i].flags&FORMAT_FUNC) != 0))
      free(ptr[0]);
    ptr[0] = ptr[1];
    fsize[0] = fsize[1];
  }

  return NULL;
}

//!!
/*
  This is a memory optimized implementation of the above. Instead of openinig new SINK for every
  function it reuses it's last SINK.

  However testing it on 64MB sample revealed that it's consistently slower by 200ms, even though
  it allocates 189.49MB less memory using 18502 less allocations.

     time ./reliq -f speed.reliq sp2

  Tests on the same sample concatenated to 444MB showed around 800ms difference.

  Because of that it's not used and is saved for future. Weird are the ways of operating systems.
*/

/*reliq_error *
format_exec(char *input, size_t inputl, SINK *output, const reliq_chnode *hnode, const reliq_chnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq)
{
  if (hnode && (!formatl || (formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && (!format[0].arg[0] || !((reliq_cstr*)format[0].arg[0])->b)))) {
    chnode_print(output,hnode,rq);
    return NULL;
  }
  if (hnode && formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && format[0].arg[0] && ((reliq_cstr*)format[0].arg[0])->b) {
    chnode_printf(output,((reliq_cstr*)format[0].arg[0])->b,((reliq_cstr*)format[0].arg[0])->s,hnode,parent,rq);
    return NULL;
  }

  reliq_error *err = NULL;
  SINK *out;
  char *ptr[2];
  size_t fsize[2];
  SINK *sn[2] = {0};
  if (formatl > 1 || !hnode || (format[0].flags&FORMAT_FUNC) == 0)
    sn[0] = sink_open(&ptr[0],&fsize[0]);
  if (formatl > 1)
    sn[1] = sink_open(&ptr[1],&fsize[1]);

  for (size_t i = 0; i < formatl; i++) {
    out = (i == formatl-1) ? output : sn[1];
    if (hnode && i == 0 && (format[i].flags&FORMAT_FUNC) == 0) {
      chnode_printf(out,((reliq_cstr*)format[i].arg[0])->b,((reliq_cstr*)format[i].arg[0])->s,hnode,parent,rq);
    } else {
      if (i == 0) {
        if (hnode) {
          chnode_print(sn[0],hnode,rq);
          sink_flush(sn[0]);
        } else {
          ptr[0] = input;
          fsize[0] = inputl;
        }
      }
      if (format[i].flags&FORMAT_FUNC) {
        err = format_functions[(format[i].flags&FORMAT_FUNC)-1].func(ptr[0],fsize[0],out,(const void**)format[i].arg,format[i].flags);
        if (err)
          goto ERR;
      }
    }

    if (out != output) {
      sink_flush(out);
      sink_change(sn[1],&ptr[0],&fsize[0],fsize[1]);
      sink_change(sn[0],&ptr[1],&fsize[1],0);

      SINK *t = sn[1];
      sn[1] = sn[0];
      sn[0] = t;
    }
  }

  ERR: ;
  if (sn[0])
    sink_destroy(sn[0]);
  if (sn[1])
    sink_destroy(sn[1]);

  return NULL;
}*/


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

  flexarr *f = flexarr_init(sizeof(reliq_format_func),FORMAT_INC);
  err = format_get_funcs(f,src,&i,size);
  flexarr_conv(f,(void**)format,formatl);

  END: ;
  *pos = i;
  return err;
}
