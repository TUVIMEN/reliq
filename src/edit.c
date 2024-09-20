/*
    reliq - html searching tool
    Copyright (C) 2020-2024 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#define _GNU_SOURCE
#define __USE_XOPEN
#define __USE_XOPEN_EXTENDED
#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <limits.h>

typedef unsigned char uchar;

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "htmlescapecodes.h"
#include "edit.h"

#define LINE_EDIT_INC (1<<8)

#define SED_MAX_PATTERN_SPACE (1<<20)

const struct reliq_format_function format_functions[] = {
    {{"sed",3},(reliq_format_function_t)sed_edit},
    {{"trim",4},(reliq_format_function_t)trim_edit},
    {{"tr",2},(reliq_format_function_t)tr_edit},
    {{"line",4},(reliq_format_function_t)line_edit},
    {{"cut",3},(reliq_format_function_t)cut_edit},
    {{"decode",6},(reliq_format_function_t)decode_edit},
    {{"sort",4},(reliq_format_function_t)sort_edit},
    {{"uniq",4},(reliq_format_function_t)uniq_edit},
    {{"echo",4},(reliq_format_function_t)echo_edit},
    {{"wc",2},(reliq_format_function_t)wc_edit},
    {{"rev",3},(reliq_format_function_t)rev_edit},
    {{"tac",3},(reliq_format_function_t)tac_edit},
};

reliq_error *
format_exec(char *input, size_t inputl, FILE *output, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq)
{
  if (hnode && (!formatl || (formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && (!format[0].arg[0] || !((reliq_cstr*)format[0].arg[0])->b)))) {
    reliq_print(output,hnode);
    return NULL;
  }
  if (hnode && formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && format[0].arg[0] && ((reliq_cstr*)format[0].arg[0])->b) {
    reliq_printf(output,((reliq_cstr*)format[0].arg[0])->b,((reliq_cstr*)format[0].arg[0])->s,hnode,parent,rq);
    return NULL;
  }

  FILE *out;
  char *ptr[2];
  ptr[1] = NULL;
  size_t fsize[2];

  for (size_t i = 0; i < formatl; i++) {
    out = (i == formatl-1) ? output : open_memstream(&ptr[1],&fsize[1]);
    if (hnode && i == 0 && (format[i].flags&FORMAT_FUNC) == 0) {
      reliq_printf(out,((reliq_cstr*)format[i].arg[0])->b,((reliq_cstr*)format[i].arg[0])->s,hnode,parent,rq);
    } else {
      if (i == 0) {
        if (hnode) {
          FILE *t = open_memstream(&ptr[0],&fsize[0]);
          reliq_print(t,hnode);
          fclose(t);
        } else {
          ptr[0] = input;
          fsize[0] = inputl;
        }
      }
      if (format[i].flags&FORMAT_FUNC) {
        reliq_error *err = format_functions[(format[i].flags&FORMAT_FUNC)-1].func(ptr[0],fsize[0],out,(const void**)format[i].arg,format[i].flags);
        if (err)
          return err;
      }
    }

    if (i != formatl-1)
      fclose(out);
    if (i != 0 || (hnode && (format[i].flags&FORMAT_FUNC) != 0))
      free(ptr[0]);
    ptr[0] = ptr[1];
    fsize[0] = fsize[1];
  }

  return NULL;
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
      if (isalnum(src[i]) || src[i] == '/' || src[i] == '|')
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

reliq_error *
format_get_funcs(flexarr *format, const char *src, size_t *pos, const size_t size)
{
  reliq_format_func *f;
  char const *fname;
  size_t fnamel=0,i=*pos;
  reliq_error *err = NULL;

  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == '|' || src[i] == '/')
      goto END;

    if (isalnum(src[i])) {
      fname = src+i;
      while_is(isalnum,src,i,size);
      fnamel = i-(fname-src);
      if (i < size && !isspace(src[i]))
        goto_script_seterr(END,"format function has to be separated by space from its arguments");
    } else
      fname = NULL;

    f = (reliq_format_func*)flexarr_inc(format);
    memset(f,0,sizeof(reliq_format_func));

    while_is(isspace,src,i,size);
    size_t argcount = 0;
    if ((err = format_get_func_args(f,src,&i,size,&argcount)))
      goto END;

    if (fname) {
      char found = 0;
      size_t i = 0;
      for (; i < LENGTH(format_functions); i++) {
        if (memcomp(format_functions[i].name.b,fname,format_functions[i].name.s,fnamel)) {
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

static int
get_arg_delim(const void *args[4], const int num, const uint8_t flag, char *delim)
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
    if (memcomp(tr_ctypes[i].name,name,tr_ctypes[i].namel,namel))
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

static reliq_error *
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

#define SED_A_EMPTY 0x0
#define SED_A_REVERSE 0x1
#define SED_A_NUM1 0x2
#define SED_A_CHECKFIRST 0x4
#define SED_A_REG1 0x8
#define SED_A_NUM2 0x10
#define SED_A_STEP 0x20
#define SED_A_ADD 0x40
#define SED_A_MULTIPLE 0x80
#define SED_A_END 0x100
#define SED_A_REG2 0x200
#define SED_A_FOUND1 0x400
#define SED_A_FOUND2 0x800

struct sed_address {
  unsigned int num[2];
  regex_t reg[2];
  unsigned int fline;
  uint16_t flags; //SED_A_
};

static void
sed_address_comp_number(const char *src, size_t *pos, const size_t size, uint32_t *result)
{
  size_t s;
  *result = get_dec(src+*pos,size-*pos,&s);
  *pos += s;
}

static reliq_error *
sed_address_comp_regex(const char *src, size_t *pos, const size_t size, regex_t *preg, const int eflags)
{
  char regex_delim = '/';
  if (*pos+1 < size && src[*pos] == '\\')
    regex_delim = src[++(*pos)];
  (*pos)++;
  size_t regex_end = *pos;
  while (regex_end < size && src[regex_end] != regex_delim && src[regex_end] != '\n') {
    if (src[regex_end] == '\\')
      regex_end++;
    regex_end++;
  }
  if (regex_end >= size || src[regex_end] != regex_delim)
    return script_err("sed: char %lu: unterminated address regex",*pos);
  if (regex_end == *pos)
    return script_err("sed: char %lu: no previous regular expression",*pos);
  char tmp[REGEX_PATTERN_SIZE];
  if (regex_end-*pos >= REGEX_PATTERN_SIZE-1)
    return script_err("sed: char %lu: regex is too long",regex_end);

  size_t len = regex_end-*pos;
  memcpy(tmp,src+*pos,len);
  splchars_conv(tmp,&len);
  tmp[len] = 0;

  *pos = regex_end+1;
  if (regcomp(preg,tmp,eflags))
    return script_err("sed: char %lu: couldn't compile regex",regex_end);
  return NULL;
}

static reliq_error *
sed_address_comp_reverse(const char *src, size_t *pos, const size_t size, struct sed_address *address)
{
  while_is(isspace,src,*pos,size);
  if (*pos < size && src[*pos] == '!') {
    address->flags |= SED_A_REVERSE;
    (*pos)++;
  }
  if (address->flags&SED_A_NUM1 && address->num[0] == 0) {
    if (!(address->flags&SED_A_REG2))
      return script_err("sed: char %lu: invalid use of line address 0",*pos);
    address->flags |= SED_A_CHECKFIRST;
  }
  return NULL;
}

static reliq_error *
sed_address_comp_pre(const char *src, size_t *pos, const size_t size, struct sed_address *address, int eflags)
{
  reliq_error *err = NULL;
  address->flags = 0;
  while_is(isspace,src,*pos,size);
  if (*pos >= size)
    return NULL;
  if (isdigit(src[*pos])) {
    sed_address_comp_number(src,pos,size,&address->num[0]);
    address->flags |= SED_A_NUM1;
  } else if (src[*pos] == '\\' || src[*pos] == '/') {
    if ((err = sed_address_comp_regex(src,pos,size,&address->reg[0],eflags)))
      return err;
    address->flags |= SED_A_REG1;
  } else if (src[*pos] == '$') {
    address->flags |= SED_A_END;
    (*pos)++;
  }

  while_is(isspace,src,*pos,size);

  if (*pos >= size || src[*pos] == '!')
    return NULL;
  if (src[*pos] == '~') {
    if (address->flags&SED_A_REG1)
      return NULL;
    (*pos)++;
    while_is(isspace,src,*pos,size);
    sed_address_comp_number(src,pos,size,&address->num[1]);
    address->flags |= SED_A_NUM2|SED_A_STEP;
    return NULL;
  }

  uchar onlynumber = 0;

  if (src[*pos] != ',')
    return NULL;
  (*pos)++;
  if (*pos >= size)
    return NULL;
  if (src[*pos] == '+' || src[*pos] == '~') {
    onlynumber = 1;
    address->flags |= ((src[*pos] == '+') ? SED_A_ADD : SED_A_MULTIPLE);
    (*pos)++;
    while_is(isspace,src,*pos,size);
  } else  if (src[*pos] == '\\' || src[*pos] == '/') {
    err = sed_address_comp_regex(src,pos,size,&address->reg[1],eflags);
    address->flags |= SED_A_REG2;
    return err;
  }

  if (*pos >= size)
    return NULL;
  if (isdigit(src[*pos])) {
    sed_address_comp_number(src,pos,size,&address->num[1]);
    address->flags |= SED_A_NUM2;
  } else if (onlynumber)
    return NULL;

  if (*pos < size && src[*pos] == '$') {
    address->flags |= SED_A_END;
    (*pos)++;
  }

  return NULL;
}

static void
sed_address_free(struct sed_address *a)
{
  if (a->flags&SED_A_REG1)
    regfree(&a->reg[0]);
  if (a->flags&SED_A_REG2)
    regfree(&a->reg[1]);
}

static reliq_error *
sed_address_comp(const char *src, size_t *pos, const size_t size, struct sed_address *address, int eflags)
{
  reliq_error *err = sed_address_comp_pre(src,pos,size,address,eflags|REG_NOSUB);
  if (err) {
    sed_address_free(address);
    return err;
  }
  return sed_address_comp_reverse(src,pos,size,address);
}

static int
sed_address_exec(const char *src, const size_t size, const uint32_t line, const uchar islast, struct sed_address *address)
{
  if (address->flags == SED_A_EMPTY)
    return 1;
  regmatch_t pmatch;
  uchar rev=0,range=0,first=0;
  uint16_t flags = address->flags;

  if (flags&SED_A_REVERSE)
    rev = 1;
  if (flags&(SED_A_REG2|SED_A_NUM2) || (flags&(SED_A_NUM1|SED_A_REG1) && flags&SED_A_END))
    range = 1;

  if (flags&SED_A_STEP)
    return ((line-address->num[0])%address->num[1] == 0)^rev;

  if (!range && flags&SED_A_END)
    return (islast > 0)^rev;

  if (flags&SED_A_NUM1) {
    first = (range ? (line >= address->num[0]) : (address->num[0] == line));
  } else if (flags&SED_A_REG1) {
    if (range && flags&SED_A_FOUND1) {
      first = 1;
    } else {
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)size;
      first = (regexec(&address->reg[0],src,1,&pmatch,REG_STARTEND) == 0);
      if (first) {
        address->flags |= SED_A_FOUND1;
        address->flags &= ~SED_A_FOUND2;
        flags = address->flags;
        address->fline = line;
      }
    }
  }

  if (!range || (!first && !rev))
    return first^rev;

  if (flags&SED_A_ADD) {
    if (flags&SED_A_NUM1)
      return (line <= address->num[0]+address->num[1])^rev;

    if (!(flags&SED_A_FOUND1))
      return rev;
    uchar r = (line <= address->fline+address->num[1]);
    if (!r)
      address->flags &= ~SED_A_FOUND1;
    return r^rev;
  }
  if (flags&SED_A_MULTIPLE) {
    size_t prevline = address->num[0];
    if (flags&SED_A_FOUND1)
      prevline = address->fline;
    if (line == prevline)
      return !rev;
    if (flags&SED_A_FOUND2)
      return rev;
    if ((line%address->num[1]) == 0) {
      address->flags |= SED_A_FOUND2;
      address->flags &= ~SED_A_FOUND1;
    }
    return !rev;
  }
  if (flags&SED_A_END)
    return first^rev;
  if (flags&SED_A_NUM2)
    return (first&(line <= address->num[1]))^rev;
  if (flags&flags&SED_A_REG2) {
    if (flags&SED_A_REG1 && !(flags&SED_A_FOUND1))
      return rev;
    if (line == 1 && !(flags&SED_A_CHECKFIRST))
      return first^rev;
    if (flags&SED_A_FOUND2) {
      return rev;
    } else {
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)size;
      if (!regexec(&address->reg[1],src,1,&pmatch,REG_STARTEND)) {
        address->flags |= SED_A_FOUND2;
        address->flags &= ~SED_A_FOUND1;
      }
      return first^rev;
    }
  }

  return 0^rev;
}

#define SED_EXPRESSION_S_NUMBER 0x00ffffff
#define SED_EXPRESSION_S_GLOBAL 0x01000000
#define SED_EXPRESSION_S_ICASE 0x02000000
#define SED_EXPRESSION_S_PRINT 0x04000000

struct sed_expression {
  uint16_t lvl;
  struct sed_address address;
  char name;
  reliq_cstr arg;
  void *arg1;
  void *arg2; //SED_EXPRESSION_S_
};

static void
sed_expression_free(struct sed_expression *e)
{
  sed_address_free(&e->address);
  if (e->name == 'y') {
    if (e->arg1)
      free(e->arg1);
    if (e->arg2)
      free(e->arg2);
  } else if (e->name == 's' && e->arg1) {
    regfree(e->arg1);
    free(e->arg1);
  }
}

#define SC_ONLY_NEWLINE 0x1 //can be ended only by a newline eg. i, a, c commands
#define SC_ARG 0x2
#define SC_ARG_OPTIONAL 0x4
#define SC_NOADDRESS 0x8
#define SC_ESCAPE_NEWLINE 0x10

struct sed_command {
  char name;
  uint16_t flags; //SC_
} sed_commands[] = {
  {'{',0},
  {'}',SC_NOADDRESS},
  {'#',SC_ONLY_NEWLINE|SC_NOADDRESS|SC_ARG},
  {':',SC_ARG|SC_NOADDRESS},
  {'=',0},
  {'a',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'i',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'q',0},
  {'c',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'z',0},
  {'d',0},
  {'D',0},
  {'h',0},
  {'H',0},
  {'g',0},
  {'G',0},
  {'n',0},
  {'N',0},
  {'p',0},
  {'P',0},
  {'s',SC_ARG},
  {'b',SC_ARG|SC_ARG_OPTIONAL},
  {'t',SC_ARG|SC_ARG_OPTIONAL},
  {'T',SC_ARG|SC_ARG_OPTIONAL},
  {'x',0},
  {'y',SC_ARG},
};


struct sed_command *
sed_get_command(char name)
{
  for (size_t i = 0; i < LENGTH(sed_commands); i++)
    if (name == sed_commands[i].name)
      return &sed_commands[i];
  return NULL;
}

static void
sed_script_free(flexarr *script)
{
  const size_t size = script->size;
  for (size_t i = 0; i < size; i++)
    sed_expression_free(&((struct sed_expression*)script->v)[i]);
  flexarr_free(script);
}

static reliq_error *
sed_UNTERMINATED(const size_t pos, const char name, const char foundchar)
{
  return script_err(name == ':' ? "sed: char %lu: \"%c\" lacks a label" :
    "sed: char %lu: unterminated `%c' command",pos,foundchar);
}

static reliq_error *
sed_DIFFERENT_LENGHTS(const size_t pos, const char name)
{
  return script_err("sed: char %lu: strings for `%c' command are different lenghts",pos,name);
}

static reliq_error *
sed_EXTRACHARS(const size_t pos)
{
  return script_err("sed: char %lu: extra characters after command",pos);
}

static void
sed_comp_onlynewline(const char *src, size_t *pos, const size_t size, const uint16_t flags)
{
  size_t p = *pos;
  if (flags&SC_ESCAPE_NEWLINE) {
    while (p < size && src[p] != '\n') {
      if (src[p] == '\\')
        p++;
      p++;
    }
  } else while (p < size && src[p] != '\n')
    p++;
  *pos = p;
}

static reliq_error *
sed_comp_sy_arg(const char *src, size_t *pos, const size_t size, const char argdelim, const char name, const char foundchar, reliq_cstr *result)
{
  reliq_error *err = NULL;
  size_t p = (*pos)+1;
  const char *res = src+p;
  size_t resl=0;

  while (p < size && src[p] != argdelim && src[p] != '\n') {
    if (src[p] == '\\')
      p++;
    p++;
  }
  resl = p-(res-src);
  if (p >= size || src[p] != argdelim) {
    err = sed_UNTERMINATED(p,name,foundchar);
    goto END;
  }

  END: ;
  *pos = p;
  result->b = res;
  result->s = resl;

  return err;
}

static reliq_error *
sed_comp_y(const size_t pos, const char name, struct sed_expression *sedexpr, reliq_cstr *second, reliq_cstr *third)
{
  if (third->s)
    return sed_EXTRACHARS(pos);

  sedexpr->arg1 = malloc(256*sizeof(char));
  sedexpr->arg2 = malloc(256*sizeof(uchar));
  reliq_cstr first = sedexpr->arg;
  size_t i=0,j=0;

  const size_t size = second->s;
  for (; i < first.s && j < size; i++, j++) {
    char c1 = first.b[i];
    size_t traversed;
    if (c1 == '\\') {
      i++;
      c1 = splchar2(first.b+i,first.s-i,&traversed);
      i += traversed-1;
    }
    char c2 = second->b[j];
    if (c2 == '\\') {
      j++;
      c2 = splchar2(second->b+j,second->s-j,&traversed);
      j += traversed-1;
    }

    ((uchar*)sedexpr->arg2)[(uchar)c1] = 1;
    ((char*)sedexpr->arg1)[(uchar)c1] = c2;
  }

  if (i != first.s || j != second->s)
    return sed_DIFFERENT_LENGHTS(pos,name);

  return NULL;
}

static reliq_error *
sed_comp_s_flags(const char *src, const size_t size, const size_t pos, int *eflags, uint64_t *args)
{
  uint64_t arg2 = 0;
  for (size_t i = 0; i < size; i++) {
    if (src[i] == 'i') {
      if (arg2&SED_EXPRESSION_S_ICASE)
        goto S_ARG_REPEAT;
      arg2 |= SED_EXPRESSION_S_ICASE;
      *eflags |= REG_ICASE;
    } else if (src[i] == 'g') {
      if (arg2&SED_EXPRESSION_S_GLOBAL)
        goto S_ARG_REPEAT;
      arg2 |= SED_EXPRESSION_S_GLOBAL;
    } else if (src[i] == 'p') {
      if (arg2&SED_EXPRESSION_S_PRINT) {
        S_ARG_REPEAT: ;
        return script_err("sed: char %lu: multiple `%c' options to `s' command",pos,src[i]);
      }
      arg2 |= SED_EXPRESSION_S_PRINT;
    } else if (isdigit(src[i])) {
      if (arg2&SED_EXPRESSION_S_NUMBER)
        return script_err("sed: char %lu: multiple number options to `s' command",pos);
      uint32_t c = number_handle(src,&i,size);
      if (!c)
        return script_err("sed: char %lu: number option to `s' may not be zero",pos);
      arg2 |= c&SED_EXPRESSION_S_NUMBER;
    } else if (!isspace(src[i]))
      return script_err("sed: char %lu: unknown option to `s'",pos);
  }
  *args = arg2;
  return NULL;
}

static reliq_error *
sed_comp_s(const char *src, const size_t pos, int eflags, struct sed_expression *sedexpr, reliq_cstr *second, reliq_cstr *third)
{
  reliq_error *err = NULL;
  char tmp[REGEX_PATTERN_SIZE];

  if (sedexpr->arg.s >= REGEX_PATTERN_SIZE-1)
    return script_err("sed: `s' pattern is too big");

  if ((err = sed_comp_s_flags(third->b,third->s,pos,&eflags,(uint64_t*)(void*)&sedexpr->arg2)))
    return err;

  size_t len = sedexpr->arg.s;
  memcpy(tmp,sedexpr->arg.b,sedexpr->arg.s);
  splchars_conv(tmp,&len);
  tmp[len] = 0;

  sedexpr->arg1 = malloc(sizeof(regex_t));
  if (regcomp(sedexpr->arg1,tmp,eflags)) {
    free(sedexpr->arg1);
    sedexpr->arg1 = NULL;
    return script_err("sed: char %lu: couldn't compile regex",sedexpr->arg.b-src);
  }

  sedexpr->arg = *second;
  return err;
}

static reliq_error *
sed_comp_sy(const char *src, size_t *pos, const size_t size, const char name, int eflags, struct sed_expression *sedexpr)
{
  reliq_error *err = NULL;
  size_t p = *pos;
  char argdelim = src[p];

  if ((err = sed_comp_sy_arg(src,&p,size,argdelim,name,sedexpr->name,&sedexpr->arg)))
    goto END;

  if (!sedexpr->arg.s) {
    if (name == 'y') {
      err = sed_DIFFERENT_LENGHTS(p,name);
    } else
      err = script_err("sed: char %lu: no previous regular expression",p);
    goto END;
  }

  reliq_cstr second,third;

  if ((err = sed_comp_sy_arg(src,&p,size,argdelim,name,sedexpr->name,&second)))
    goto END;

  third.b = src+(++p);
  while (p < size && src[p] != '\n' && src[p] != '#' && src[p] != ';' && src[p] != '}')
    p++;
  third.s = p-(third.b-src);

  if (name == 'y') {
    err = sed_comp_y(p,name,sedexpr,&second,&third);
  } else
    err = sed_comp_s(src,p,eflags,sedexpr,&second,&third);

  END:
  *pos = p;
  return err;
}

static reliq_error *
sed_comp_check_labels(flexarr *script)
{
  struct sed_expression *scriptv = (struct sed_expression*)script->v;
  const size_t size = script->size;
  for (size_t i = 0; i < size; i++) {
    if ((scriptv[i].name == 'b' || scriptv[i].name == 't' || scriptv[i].name == 'T') && scriptv[i].arg.s) {
      uchar found = 0;
      for (size_t j = 0; j < size; j++) {
        if (scriptv[j].name == ':' &&
          strcomp(scriptv[i].arg,scriptv[j].arg)) {
          found = 1;
          break;
        }
      }
      if (!found)
        return script_err("sed: can't find label for jump to `%.*s'",scriptv[i].arg.s,scriptv[i].arg.b);
    }
  }
  return NULL;
}

static reliq_error *
sed_script_comp_pre(const char *src, const size_t size, int eflags, flexarr **script)
{
  reliq_error *err;
  size_t pos = 0;
  *script = flexarr_init(sizeof(struct sed_expression),2<<4);
  struct sed_expression *sedexpr = (struct sed_expression*)flexarr_inc(*script);
  sedexpr->address.flags = 0;
  sedexpr->arg1 = NULL;
  sedexpr->arg2 = NULL;
  struct sed_command *command;
  uint16_t lvl = 0;
  while (pos < size) {
    sedexpr->name = '\0';
    while (pos < size && (isspace(src[pos]) || src[pos] == ';'))
      pos++;

    size_t addrdiff = pos;
    if ((err = sed_address_comp(src,&pos,size,&sedexpr->address,eflags)))
      return err;
    while_is(isspace,src,pos,size);
    if (pos >= size) {
      if (pos-addrdiff)
        return script_err("sed: char %lu: missing command",pos);
      sed_expression_free(sedexpr);
      break;
    }
    command = sed_get_command(src[pos]);
    if (!command)
      return script_err("sed: char %lu: unknown command: `%c'",pos,src[pos]);
    if (command->flags&SC_NOADDRESS && sedexpr->address.flags)
      return script_err("sed: char %lu: %c doesn't want any addresses",pos,src[pos]);
    sedexpr->name = src[pos];
    sedexpr->lvl = lvl;
    if (sedexpr->name == '{' || sedexpr->name == '}') {
      if (sedexpr->name == '}') {
        CLOSING_BRACKET: ;
        if (!lvl)
          return script_err("sed: char %lu: unexpected `}'",pos);
        lvl--;
      } else
        lvl++;
      sedexpr = (struct sed_expression*)flexarr_inc(*script);
      sedexpr->address.flags = 0;
      pos++;
      continue;
    }
    pos++;
    while (pos < size && src[pos] != '\n' && isspace(src[pos]))
      pos++;

    char const *argstart = src+pos;
    if (command->flags&SC_ONLY_NEWLINE) {
      sed_comp_onlynewline(src,&pos,size,command->flags);
    } else if (command->name == 's' || command->name == 'y') {
      if ((err = sed_comp_sy(src,&pos,size,command->name,eflags,sedexpr)))
        return err;
    } else if (command->name == ':') {
      while (pos < size && src[pos] != '\n' && src[pos] != '#' && src[pos] != ';' && src[pos] != '}' && !isspace(src[pos]))
        pos++;
    } else
      while (pos < size && src[pos] != '\n' && src[pos] != '#' && src[pos] != ';' && src[pos] != '}')
        pos++;
    if (command->name != 's' && command->name != 'y') {
      size_t argend = pos;
      if (!(command->flags&SC_ONLY_NEWLINE))
        while (argend > (size_t)(argstart-src) && isspace(src[argend-1]))
          argend--;
      sedexpr->arg.b = argstart;
      sedexpr->arg.s = argend-(argstart-src);
      if (command->name != '#') {
        if (!sedexpr->arg.s && command->flags&SC_ARG && !(command->flags&SC_ARG_OPTIONAL))
          return sed_UNTERMINATED(pos,command->name,sedexpr->name);
        if (sedexpr->arg.s && !(command->flags&SC_ARG))
          return sed_EXTRACHARS(pos);
      }
    }
    sedexpr = (struct sed_expression*)flexarr_inc(*script);
    sedexpr->address.flags = 0;
    sedexpr->arg1 = NULL;
    sedexpr->arg2 = NULL;
    if (pos >= size)
      break;
    if (src[pos] == '}') {
      sedexpr->name = src[pos];
      sedexpr->lvl = lvl;
      goto CLOSING_BRACKET;
    }
  }
  /*if (sedexpr->arg1) //random solution for it giving segfault
    sed_expression_free(sedexpr);*/
  if (lvl)
    return script_err("sed: char %lu: unmatched `{'",pos);
  flexarr_dec(*script);

  return sed_comp_check_labels(*script);
}

static reliq_error *
sed_script_comp(const char *src, const size_t size, int eflags, flexarr **script)
{
  reliq_error *err = NULL;
  if ((err = sed_script_comp_pre(src,size,eflags,script))) {
    sed_script_free(*script);
    *script = NULL;
  }
  return err;
}

static reliq_error *
sed_pre_edit(const char *src, const size_t size, FILE *output, char *buffers[3], flexarr *script, const char linedelim, uchar silent)
{
  char *patternsp = buffers[0],
    *buffersp = buffers[1],
    *holdsp = buffers[2];
  size_t patternspl=0,bufferspl=0,holdspl=0;

  size_t line=0,lineend;
  uchar islastline,appendnextline=0,successfulsub=0;

  uchar patternsp_delim=0,
    buffersp_delim=0,
    holdsp_delim=0;

  uint32_t linenumber = 0;
  struct sed_expression *scriptv = (struct sed_expression*)script->v;
  size_t cycle = 0;

  while (1) {
    patternsp_delim = 0;
    if (line < size) {
      linenumber++;
    } else if (cycle == 0)
      break;

    lineend = line;

    islastline = 0;
    size_t start=line,end=lineend,offset=0;
    if (lineend < size) {
      while (lineend < size && src[lineend] != linedelim)
        lineend++;
      if (src[lineend] == linedelim)
        patternsp_delim = 1;

      end = lineend;

      if (appendnextline)
        offset = patternspl;
      if ((end-start)+offset >= SED_MAX_PATTERN_SPACE) {
        BIGLINE: ;
        return script_err("sed: line too big to process");
      }
      patternspl = (end-start)+offset;
      if (end-start)
        memcpy(patternsp+offset,src+start,end-start);
    }

    if (lineend+1 >= size)
      islastline = 1;

    appendnextline = 0;
    const size_t scriptsize = script->size;
    for (; cycle < scriptsize; cycle++) {
      if (!sed_address_exec(patternsp,patternspl,linenumber,islastline,&scriptv[cycle].address)) {
        if (scriptv[cycle].name == '{') {
          uint16_t lvl = scriptv[++cycle].lvl;
          while (cycle+1 < script->size && lvl <= scriptv[cycle+1].lvl)
            cycle++;
          if (cycle < script->size)
            cycle--;
        }
        continue;
      }

      offset = 0;

      switch (scriptv[cycle].name) {
        case 'H':
          offset = holdspl+1;
          if (offset+patternspl > SED_MAX_PATTERN_SPACE)
            goto BIGLINE;
          holdsp[holdspl] = linedelim;
        case 'h':
          memcpy(holdsp+offset,patternsp,patternspl);
          holdspl = patternspl+offset;
          holdsp_delim = patternsp_delim;
          break;
        case 'G':
          offset = patternspl+1;
          if (offset+holdspl > SED_MAX_PATTERN_SPACE)
            goto BIGLINE;
          patternsp[patternspl] = linedelim;
        case 'g':
          memcpy(patternsp+offset,holdsp,holdspl);
          patternspl = holdspl+offset;
          patternsp_delim = holdsp_delim;
          break;
        case 'd':
          patternspl = 0;
          cycle = 0;
          goto NEXT;
          break;
        case 'D': {
            size_t i = 0;
            while (i < patternspl && patternsp[i] != linedelim)
              i++;
            if (i >= patternspl || patternsp[i] != linedelim) {
              patternspl = 0;
              cycle = 0;
              goto NEXT;
            }
            i++;
            patternspl -= i;
            memcpy(buffersp,patternsp+i,patternspl);
            memcpy(patternsp,buffersp,patternspl);
          }
          break;
        case 'P':
          offset = 0;
          while (offset < patternspl && patternsp[offset] != linedelim)
            offset++;
        case 'p':
          if (scriptv[cycle].name == 'p') {
            COMMAND_PRINT: ;
            offset = patternspl;
          }
          if (offset)
            fwrite(patternsp,1,offset,output);
          if (!silent || patternsp_delim)
            fputc(linedelim,output);
          break;
        case 'N':
          appendnextline = 1;
          cycle++;
          goto NEXT_PRINT;
          break;
        case 'n':
          cycle++;
          goto NEXT_PRINT;
          break;
        case 'z':
          patternspl = 0;
          break;
        case 'x':
          memcpy(buffersp,patternsp,patternspl);
          memcpy(patternsp,holdsp,holdspl);
          memcpy(holdsp,buffersp,patternspl);
          bufferspl = patternspl;
          patternspl = holdspl;
          holdspl = bufferspl;

          buffersp_delim = patternsp_delim;
          patternsp_delim = holdsp_delim;
          holdsp_delim = buffersp_delim;
          break;
        case 'q':
          goto END;
          break;
        case '=':
          print_uint(linenumber,output);
          fputc(linedelim,output);
          break;
        case 't':
          if (!successfulsub)
            break;
        case 'T':
          if (scriptv[cycle].name == 'T' && successfulsub)
            break;
        case 'b':
          if (!scriptv[cycle].arg.s) {
            cycle = 0;
            goto NEXT;
          }
          for (size_t i = 0; i < scriptsize; i++)
            if (scriptv[i].name == ':' && strcomp(scriptv[cycle].arg,scriptv[i].arg))
              cycle = i;
          break;
        case 'y': {
          for (size_t i = 0; i < patternspl; i++)
            buffersp[i] = (((uchar*)scriptv[cycle].arg2)[(uchar)patternsp[i]]) ?
                ((char*)scriptv[cycle].arg1)[(uchar)patternsp[i]] : patternsp[i];
          memcpy(patternsp,buffersp,patternspl);
          }
          break;
        case 's': {
          successfulsub = 0;
          uchar global = ((((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_GLOBAL) ? 1 : 0),
            print = ((((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_PRINT) ? 1 : 0);
          uint32_t matchnum = ((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_NUMBER,
            matchfound = 0;
          size_t after = 0;
          do {
          regmatch_t pmatch[10];
          pmatch[0].rm_so = 0;
          pmatch[0].rm_eo = (int)patternspl-after;
          if (regexec((regex_t*)scriptv[cycle].arg1,patternsp+after,10,pmatch,REG_STARTEND) != 0)
            break;
          successfulsub = 1;
          pmatch[0].rm_so += (int)after;
          pmatch[0].rm_eo += (int)after;
          matchfound++;
          if (matchnum && ((!global || matchfound < matchnum) && matchfound != matchnum)) {
            after = pmatch[0].rm_so+(pmatch[0].rm_eo-pmatch[0].rm_so);
            continue;
          }

          bufferspl = pmatch[0].rm_so;
          if (bufferspl)
            memcpy(buffersp,patternsp,bufferspl);
          if (scriptv[cycle].arg.s) {
            reliq_cstr arg = scriptv[cycle].arg;
            for (size_t i = 0; i < arg.s; i++) {
              char c = arg.b[i];
              if (c == '&' || (i+1 < arg.s && c == '\\')) {
                char unchanged_c = '0';
                if (c == '\\') {
                  unchanged_c = arg.b[++i];
                  size_t traversed,resultl;
                  char result[8];
                  splchar3(arg.b+i,arg.s-i,result,&resultl,&traversed);
                  i += traversed-1;
                  if (resultl == 0) {
                    c = unchanged_c;
                  } else if (resultl > 1) {
                    memcpy(buffersp+bufferspl,result,resultl);
                    bufferspl += resultl;
                    continue;
                  } else
                    c = result[0];
                }

                if (isdigit(unchanged_c)) {
                  c = unchanged_c-'0';
                  if (pmatch[(uchar)c].rm_so == -1 || pmatch[(uchar)c].rm_eo == -1)
                    continue;
                  if (bufferspl+(pmatch[(uchar)c].rm_eo-pmatch[(uchar)c].rm_so) >= SED_MAX_PATTERN_SPACE)
                    goto BIGLINE;
                  int loop_start=pmatch[(uchar)c].rm_so,loop_end=pmatch[(uchar)c].rm_eo;
                  if (c) { //shift by after if not \0
                    loop_start += after;
                    loop_end += after;
                  }
                  for (int j = loop_start; j < loop_end; j++)
                    buffersp[bufferspl++] = patternsp[j];
                  continue;
                }
              }
              buffersp[bufferspl++] = c;
            }
          }
          after = bufferspl;
          if (patternspl-pmatch[0].rm_eo) {
            if (bufferspl+(patternspl-pmatch[0].rm_eo) >= SED_MAX_PATTERN_SPACE)
              goto BIGLINE;
            memcpy(buffersp+bufferspl,patternsp+pmatch[0].rm_eo,patternspl-pmatch[0].rm_eo);
            bufferspl += patternspl-pmatch[0].rm_eo;
          }
          patternspl = bufferspl;
          if (bufferspl) {
            memcpy(patternsp,buffersp,bufferspl);
            bufferspl = 0;
          }
          } while((global || (matchnum && matchfound != matchnum)) && after < patternspl);
          if (successfulsub && print)
            goto COMMAND_PRINT;
          }
          break;
        case 'i':
        case 'c':
        case 'a':
          break;
      }
    }
    if (cycle >= script->size)
      cycle = 0;

    NEXT_PRINT: ;
    if (appendnextline) {
      if (patternsp_delim && patternspl < SED_MAX_PATTERN_SPACE)
        patternsp[patternspl++] = linedelim;
    } else {
      if (!silent) {
        if (patternspl)
          fwrite(patternsp,1,patternspl,output);
        if (patternsp_delim)
          fputc(linedelim,output);
      }
      patternspl = 0;
    }
    if (lineend >= size)
      break;

    NEXT: ;
    if (patternsp_delim)
      lineend++;
    line = lineend;
  }

  END: ;
  if (!silent && patternspl) {
    fwrite(patternsp,1,patternspl,output);
    if (patternsp_delim)
      fputc(linedelim,output);
  }

  return NULL;
}

reliq_error *
sed_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  reliq_error *err;
  const char argv0[] = "sed";
  uchar extendedregex=0,silent=0;
  flexarr *script = NULL;

  char linedelim = '\n';

  if (arg[1]) {
    if (flag&FORMAT_ARG1_ISSTR) {
      if (((reliq_str*)arg[1])->b && ((reliq_str*)arg[1])->s) {
        reliq_str *str = (reliq_str*)arg[1];
        for (size_t i = 0; i < str->s; i++) {
          if (str->b[i] == 'E') {
            extendedregex = 1;
          } else if (str->b[i] == 'z') {
            linedelim = '\0';
          } else if (str->b[i] == 'n')
            silent = 1;
        }
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);
  }

  if (get_arg_delim(arg,2,flag,&linedelim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,3);

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR) {
      if (((reliq_str*)arg[0])->b && ((reliq_str*)arg[0])->s) {
        reliq_str *str = (reliq_str*)arg[0];
        if ((err = sed_script_comp(str->b,str->s,extendedregex ? REG_EXTENDED : 0,&script)))
          return err;
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }
  if (script == NULL)
    return script_err("%s: missing script argument",argv0);

  char *buffers[3];
  for (size_t i = 0; i < 3; i++)
    buffers[i] = malloc(SED_MAX_PATTERN_SPACE);

  err = sed_pre_edit(src,size,output,buffers,script,linedelim,silent);

  for (size_t i = 0; i < 3; i++)
    free(buffers[i]);
  sed_script_free(script);
  return err;
}

static reliq_cstr
cstr_get_line(const char *src, const size_t size, size_t *saveptr, const char delim)
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
  reliq_cstr ret = cstr_get_line(src,size,saveptr,delim);
  if (ret.b && ret.b[ret.s-1] == delim)
    ret.s--;
  return ret;
}

static void
echo_edit_print(reliq_str *str, FILE *output)
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
        fwrite(result,resultl,1,output);
        i += traversed-1;
        continue;
      } else
        i--;
    }
    fputc(b[i],output);
  }
}

reliq_error *
echo_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
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
  fwrite(src,1,size,output);
  if (str[1] && str[1]->s)
    echo_edit_print(str[1],output);

  return NULL;
}

reliq_error *
uniq_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "uniq";

  if (get_arg_delim(arg,0,flag,&delim) == -1)
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
      fwrite(previous.b,1,previous.s,output);
      fputc(delim,output);
      break;
    }

    if (strcomp(line,previous))
      goto REPEAT;
    fwrite(previous.b,1,previous.s,output);
    fputc(delim,output);
    previous = line;
  }
  return NULL;
}

static int
sort_cmp(const reliq_cstr *s1, const reliq_cstr *s2)
{
  size_t s = s1->s;
  if (s < s2->s)
    s = s2->s;
  return memcmp(s1->b,s2->b,s);
}

reliq_error *
sort_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
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

  if (get_arg_delim(arg,1,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);

  flexarr *lines = flexarr_init(sizeof(reliq_cstr),(1<<10));
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
    fwrite(previous.b,1,previous.s,output);
    fputc(delim,output);
    previous = linesv[i];
  }
  fwrite(previous.b,1,previous.s,output);
  fputc(delim,output);

  END: ;
  flexarr_free(lines);
  return NULL;
}

reliq_error *
line_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  reliq_range *range = NULL;
  const char argv0[] = "line";

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR)
      return script_err("%s: arg %d: incorrect type of argument, expected range",argv0,1);
    range = (reliq_range*)arg[0];
  }

  if (get_arg_delim(arg,1,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);

  if (!range)
    return script_err("%s: missing arguments",argv0);

  size_t saveptr=0,linecount=0,currentline=0;
  reliq_cstr line;

  while (1) {
    line = cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    linecount++;
  }

  saveptr = 0;
  while (1) {
    line = cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    currentline++;
    if (range_match(currentline,range,linecount))
      fwrite(line.b,1,line.s,output);
  }
  return NULL;
}

reliq_error *
cut_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
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

  if (get_arg_delim(arg,3,flag,&linedelim) == -1)
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
          fwrite(src+dstart,1,dlength,output);
          goto PRINT;
        }

        start = dend;
        if (range_match(dcount+1,range,-1)^complement) {
          if (dprevendlength)
            fwrite(src+dprevend,1,1,output);
          if (dlength)
            fwrite(src+dstart,1,dlength,output);
          dprevendlength = 1;
        }
        dprevend = dstart+dlength;
        if (dprevend >= end)
          break;
        dcount++;
      }
    } else {
      for (size_t i = start; i < end; i++) {
        if (range_match(i+1-start,range,end-start)^complement) {
          buf[bufcurrent++] = src[i];
          if (bufcurrent == bufsize) {
            fwrite(buf,1,bufcurrent,output);
            bufcurrent = 0;
          }
        }
      }
      if (bufcurrent) {
        fwrite(buf,1,bufcurrent,output);
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
        fputc(linedelim,output);
      } else for (size_t i = 0; i < n; i++)
        fputc(linedelim,output);
    }
  }

  return NULL;
}

reliq_error *
tr_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  uchar array[256] = {0};
  reliq_str *string[2] = {NULL};
  uchar complement=0,squeeze=0;
  const char argv0[] = "tr";
  reliq_error *err;

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR) {
      if (((reliq_str*)arg[0])->b && ((reliq_str*)arg[0])->s)
        string[0] = (reliq_str*)arg[0];
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }
  if (arg[1]) {
    if (flag&FORMAT_ARG1_ISSTR) {
      if (((reliq_str*)arg[1])->b && ((reliq_str*)arg[1])->s)
        string[1] = (reliq_str*)arg[1];
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);
  }
  if (arg[2]) {
    if (flag&FORMAT_ARG2_ISSTR) {
      if (((reliq_str*)arg[2])->b) {
        reliq_str *str = (reliq_str*)arg[2];
        for (size_t i = 0; i < str->s; i++) {
            if (str->b[i] == 's') {
              squeeze = 1;
            } else if (str->b[i] == 'c')
              complement = 1;
        }
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,3);
  }

  if (!string[0])
    return script_err("%s: missing arguments",argv0);

  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;

  if (string[0] && !string[1]) {
    if ((err = tr_strrange(string[0]->b,string[0]->s,NULL,0,array,NULL,complement)))
      return err;
    for (size_t i = 0; i < size; i++) {
      if (!array[(uchar)src[i]]) {
        buf[bufcurrent++] = src[i];
        if (bufcurrent == bufsize) {
          fwrite(buf,1,bufcurrent,output);
          bufcurrent = 0;
        }
      }
    }
    if (bufcurrent) {
      fwrite(buf,1,bufcurrent,output);
      bufcurrent = 0;
    }
    return NULL;
  }

  uchar array_enabled[256] = {0};
  if ((err = tr_strrange(string[0]->b,string[0]->s,string[1]->b,string[1]->s,array,array_enabled,complement)))
    return err;

  for (size_t i = 0; i < size; i++) {
    buf[bufcurrent++] = (array_enabled[(uchar)src[i]]) ? array[(uchar)src[i]] : src[i];
    if (bufcurrent == bufsize) {
      fwrite(buf,1,bufcurrent,output);
      bufcurrent = 0;
    }
    if (squeeze) {
      char previous = src[i];
      while (i+1 < size && src[i+1] == previous)
        i++;
    }
  }
  if (bufcurrent) {
    fwrite(buf,1,bufcurrent,output);
    bufcurrent = 0;
  }

  return NULL;
}

reliq_error *
trim_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\0';
  const char argv0[] = "trim";
  uchar hasdelim = 0;

  int r = get_arg_delim(arg,0,flag,&delim);
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
          fwrite(src+delimstart,1,line-delimstart,output);
        lineend = line;
        while (lineend < size && src[lineend] != delim)
          lineend++;
    } else
      lineend = size;

    if (lineend-line) {
      char *trimmed;
      size_t trimmedl = 0;
      memtrim((void const**)&trimmed,&trimmedl,src+line,lineend-line);
      if (trimmedl)
        fwrite(trimmed,1,trimmedl,output);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
rev_edit(char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "rev";

  if (get_arg_delim(arg,0,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);

  size_t line=0,delimstart,lineend;

  while (line < size) {
    delimstart = line;
    while (line < size && src[line] == delim)
      line++;
    if (line-delimstart)
      fwrite(src+delimstart,1,line-delimstart,output);
    lineend = line;
    while (lineend < size && src[lineend] != delim)
      lineend++;

    size_t linel = lineend-line;
    if (linel) {
      strrev(src+line,linel);
      fwrite(src+line,1,linel,output);
    }

    line = lineend;
  }

  return NULL;
}

reliq_error *
tac_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "tac";

  if (get_arg_delim(arg,0,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);

  size_t saveptr=0;
  flexarr *lines = flexarr_init(sizeof(reliq_cstr),LINE_EDIT_INC);
  reliq_cstr line;

  while (1) {
    line = cstr_get_line(src,size,&saveptr,delim);
    if (!line.b)
      break;
    memcpy(flexarr_inc(lines),&line,sizeof(reliq_cstr));
  }

  reliq_cstr *linesv = lines->v;
  const size_t linessize = lines->size;
  for (size_t i = linessize; i; i--)
    fwrite(linesv[i-1].b,1,linesv[i-1].s,output);

  flexarr_free(lines);
  return NULL;
}

reliq_error *
wc_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  char delim = '\n';
  const char argv0[] = "wc";
  uchar v[4];
  v[0] = 2; //lines
  v[1] = 2; //words
  v[2] = 2; //characters
  v[3] = 0; //maxline
  size_t r[4] = {0};
  r[2] = size;

  if (arg[0]) {
    if (flag&FORMAT_ARG0_ISSTR) {
      if (((reliq_str*)arg[0])->b) {
        reliq_str *str = (reliq_str*)arg[0];
        for (size_t i = 0; i < str->s; i++) {
          if (str->b[i] == 'c') {
            v[2] = 1;
          } else if (str->b[i] == 'l') {
            v[0] = 1;
          } else if (str->b[i] == 'L') {
            v[3] = 1;
          } else if (str->b[i] == 'w')
            v[1] = 1;
        }
      }
    } else
      return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,1);
  }

  if (get_arg_delim(arg,1,flag,&delim) == -1)
    return script_err("%s: arg %d: incorrect type of argument, expected string",argv0,2);

  if (v[0] == 1 || v[1] == 1 || v[2] == 1 || v[3] == 1)
    for (uchar i = 0; i < 4; i++)
      if (v[i] == 2)
        v[i] = 0;

  if (v[0] || v[1] || v[3]) {
    size_t saveptr=0;
    reliq_cstr line;

    while (1) {
      line = cstr_get_line(src,size,&saveptr,delim);
      if (!line.b)
        break;
      r[0]++;

      if (v[1] || v[3]) {
        for (size_t i = 0; i < line.s; i++) {
          if (line.b[i] != delim && !isspace(line.b[i])) {
            i++;
            while (line.b[i] != delim && !isspace(line.b[i]) && i < line.s)
              i++;
            r[1]++;
          }
          if (line.b[i] == delim) {
            if (r[3] < i)
              r[3] = i;
            break;
          }
        }
      }
    }
  }

  uchar amountset = 0;
  for (uchar i = 0; i < 4; i++)
    if (v[i])
      amountset++;

  char numbuf[22];
  size_t numl;

  if (amountset == 1) {
    for (uchar i = 0; i < 4; i++) {
      if (v[i]) {
        uint_to_str(numbuf,&numl,22,r[i]);
        fwrite(numbuf,1,numl,output);
        break;
      }
    }
  } else for (uchar i = 0; i < 4; i++) {
    if (v[i]) {
      uint_to_str(numbuf,&numl,22,r[i]);
      fputc('\t',output);
      fwrite(numbuf,1,numl,output);
    }
  }

  fputc('\n',output);

  return NULL;
}

reliq_error *
decode_edit(const char *src, const size_t size, FILE *output, const void *arg[4], const uint8_t flag)
{
  htmlescapecodes_file(src,size,output);
  return NULL;
}
