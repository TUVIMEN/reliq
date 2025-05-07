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

#include "utils.h"
#include "fields.h"
#include "output.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define OUTFIELD_ARGS_INC 8

static void outfields_value_print(const reliq *rq, SINK *out, const reliq_output_field_type *type, const char *value, const size_t valuel, const uchar notempty);

static void
outfield_type_name_get(const char *src, size_t *pos, const size_t s, char const **type, size_t *typel)
{
  size_t i = *pos;

  *type = src+i;
  while (i < s && isalnum(src[i]))
    (i)++;
  *typel = i-(*type-src);

  *pos = i;
}

static void
outfield_name_get(const char *src, size_t *pos, const size_t s, char const **name, size_t *namel)
{
  size_t i = *pos;

  *name = src+i;
  while (i < s && (isalnum(src[i]) || src[i] == '-' || src[i] == '_'))
    i++;
  *namel = i-(*name-src);

  *pos = i;
}

static reliq_error *
outfield_type_get_args(const char *src, size_t *pos, const size_t size, reliq_str **args, size_t *argsl)
{
  if (*pos >= size)
    return NULL;

  reliq_error *err = NULL;
  size_t i = *pos;
  *args = NULL;
  *argsl = 0;
  flexarr a = flexarr_init(sizeof(reliq_str),OUTFIELD_ARGS_INC);
  char *arg;
  size_t argl;
  SINK buf = sink_open(&arg,&argl);

  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')')
      goto_script_seterr(END,"output field: type: expected an argument in '(' bracket");

    if (src[i] != '"' && src[i] != '\'') {
      UNEXPECTED_CHAR: ;
      goto_script_seterr(END,"output field: type argument list: unexpected character 0x%02x at %lu",src[i],i);
    }

    size_t qstart = i+1;
    if ((err = skip_quotes(src,&i,size)))
      goto END;
    size_t qend = i-1;

    sink_zero(&buf);
    splchars_conv_sink(src+qstart,qend-qstart,&buf);
    sink_flush(&buf);
    *(reliq_str*)flexarr_inc(&a) = (reliq_str){
      .b = memdup(arg,argl),
      .s = argl
    };

    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')') {
      i++;
      goto END;
    }

    if (src[i] != ',')
      goto UNEXPECTED_CHAR;
    i++;
  }

  if (i >= size)
    goto_script_seterr(END,"output field: type argument list: unprecedented end of list at %lu",i);

  END: ;
  if (err) {
    const size_t a_size = a.size;
    reliq_str *av = a.v;
    for (size_t i = 0; i < a_size; i++)
      free(av[i].b);
    flexarr_free(&a);
  } else
    flexarr_conv(&a,(void**)args,argsl);

  sink_destroy(&buf);

  *pos = i;
  return err;
}

static reliq_error *
outfield_validate_args(const reliq_output_field_type *type)
{
  char c = type->type;
  switch (c) {
    case 'a':
    case 'U':
      if (type->argsl > 1)
        return script_err("output field: type %c takes at most 1 argument yet %lu were specified",c,type->argsl);
      if (c == 'a' && type->args[0].s > 1)
        return script_err("output field: type %c: expected a single character argument",c);
      break;
    case 'd':
      break;

    default:
      return script_err("output field: type %c doesn't take any arguments yet %lu were specified",c,type->argsl);
  }
  return NULL;
}

static reliq_error *
outfield_type_get(const char *src, size_t *pos, const size_t size, reliq_output_field_type *type, const uchar isarray)
{
  reliq_error *err = NULL;
  size_t i = *pos;

  const char *name;
  size_t namel;
  outfield_type_name_get(src,&i,size,&name,&namel);
  if (namel == 0)
    goto_script_seterr(END,"output field: unspecified type name at %lu",i);

  if (isarray && *name == 'a')
    goto_script_seterr(END,"output field: array: array type in array is not allowed");

  type->type = *name;

  if (i < size && src[i] == '(') {
    i++;
    if ((err = outfield_type_get_args(src,&i,size,&type->args,&type->argsl)))
      goto END;
    if ((err = outfield_validate_args(type)))
      goto END;
  }

  if (*name == 'a') {
    type->subtype = calloc(1,sizeof(reliq_output_field_type));
    type->subtype->type = 's';

    if (i < size && src[i] == '.') {
      i++;
      if ((err = outfield_type_get(src,&i,size,type->subtype,1)))
        goto END;
    }
  }

  END:
  *pos = i;
  return err;
}

reliq_error *
reliq_output_field_comp(const char *src, size_t *pos, const size_t s, reliq_output_field *outfield)
{
  if (*pos >= s || src[*pos] != '.')
    return NULL;

  reliq_error *err = NULL;
  const char *name;
  size_t i=*pos,namel=0;

  *outfield = (reliq_output_field){
    .type = (reliq_output_field_type){
        .type = 's'
    }
  };

  i++;

  outfield_name_get(src,&i,s,&name,&namel);

  if (i < s && src[i] == '.') {
    i++;
    if ((err = outfield_type_get(src,&i,s,&outfield->type,0)))
      goto ERR;
  }

  outfield->isset = 1;

  if (i < s && (src[i] == '\'' || src[i] == '"')) {
    size_t qstart = i+1;
    if ((err = skip_quotes(src,&i,s)))
      goto ERR;
    size_t qend = i-1;

    size_t qlen = qend-qstart;
    outfield->annotation = (reliq_str){
      .b = memdup(src+qstart,qlen),
      .s = qlen
    };
  }

  if (i < s && !isspace(src[i])) {
    if (isprint(src[i])) {
      goto_script_seterr(ERR,"output field: unexpected character '%c' at %lu",src[i],i);
    } else
      goto_script_seterr(ERR,"output field: unexpected character 0x%02x at %lu",src[i],i);
  }

  if (!namel)
    goto ERR;

  outfield->name.s = namel;
  outfield->name.b = memdup(name,namel);

  ERR:
  *pos = i;
  return err;
}

#define OUTFIELDS_NUM_FLOAT 1
#define OUTFIELDS_NUM_INT 2
#define OUTFIELDS_NUM_UNSIGNED 4

static uchar
outfields_num_print(SINK *out, const char *value, const size_t valuel, const uint8_t flags)
{
  if (!valuel)
    return 1;

  char const *start = value;
  size_t end = 0;
  uchar isminus=0,haspoint=0,pointcount=0;

  #define NUM_PARSE_FIRST_ZERO \
    if ((size_t)(start-value) < valuel && *start == '0') { \
      start++; \
      while ((size_t)(start-value) < valuel && *start == '0') \
        start++; \
      if ((size_t)(start-value) >= valuel || !isdigit(*start)) \
        start--; \
      goto GET_NUMBER; \
    }

  if (flags&(OUTFIELDS_NUM_FLOAT|OUTFIELDS_NUM_INT)) {
    NOT_A_NUMBER: ;
    while ((size_t)(start-value) < valuel && ((*start < '1' || *start > '9') && *start != '-')) {
      NUM_PARSE_FIRST_ZERO;
      start++;
    }
    if ((size_t)(start-value) < valuel && *start == '-') {
      start++;
      while ((size_t)(start-value) < valuel && *start == '0')
        start++;
      if ((size_t)(start-value) < valuel && (*start < '1' || *start > '9'))
        goto NOT_A_NUMBER;
      isminus = 1;
    }
  } else
    while ((size_t)(start-value) < valuel && (*start < '1' || *start > '9')) {
      NUM_PARSE_FIRST_ZERO;
      start++;
    }

  GET_NUMBER: ;
  if ((size_t)(start-value) >= valuel || !isdigit(*start))
    return 1;

  while ((size_t)(start-value)+end < valuel && isdigit(start[end]))
    end++;

  if (end) {
    if (flags&OUTFIELDS_NUM_FLOAT && (size_t)(start-value)+end+1 < valuel
      && !pointcount && (start[end] == ',' || start[end] == '.') && isdigit(start[end+1]))
      haspoint = 1;
    if (isminus && end && (haspoint || *start != '0'))
      sink_put(out,'-');
    sink_write(out,start,end);
  } else if (!pointcount)
    sink_put(out,'0');

  start += end;
  end = 0;

  if (haspoint) {
    pointcount++;
    haspoint = 0;
    start++;
    isminus = 0;
    sink_put(out,'.');
    goto GET_NUMBER;
  }

  return 0;
}

static uchar
outfields_bool_print(SINK *out, const char *value, const size_t valuel)
{
  char const *start = value;

  while ((size_t)(start-value) < valuel && isspace(*start))
    start++;

  if ((size_t)(start-value) >= valuel)
    return 1;

  int ret = 0;

  if (*start == 'y' || *start == 'Y' || *start == 't' || *start == 'T') {
    ret = 1;
    goto END;
  }

  if ((size_t)(start-value+1) < valuel && *start == '-' && isdigit(start[1]))
    goto END;

  while ((size_t)(start-value) < valuel && *start == '0')
    start++;
  if ((size_t)(start-value) >= valuel || (start != value && !isdigit(*start)))
    goto END;

  if (isdigit(*start))
    ret = 1;

  END: ;
  if (ret) {
    sink_write(out,"true",4);
  } else
    sink_write(out,"false",5);

  return 0;
}

static void
outfields_unicode_print(SINK *out, uint16_t character)
{
  char val[] = "\\u0000";
  const size_t vall = 6;

  for (size_t i = vall-1; i >= 2; i--) {
    char c = character&0xf;
    character >>= 4;
    val[i] = (c < 10) ? c+'0' : c+'a'-10;
  }
  sink_write(out,val,vall);
}

static uchar
outfields_str_print(SINK *out, const char *value, const size_t valuel)
{
  const uchar sub[256] = {
    128,129,130,131,132,133,134,135,'b','t','n',139,'f','r',142,143,144,145,146,147,148,149,150,
    151,152,153,154,155,156,157,158,159,0,0,34,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,92,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
  };

  char const *start = value;
  size_t end=0;

  sink_put(out,'"');

  for (size_t i = 0; i < valuel; i++) {
    uchar s = sub[(uchar)value[i]];
    if (s) {
      if (end)
        sink_write(out,start,end);
      if (s < 128) {
        sink_put(out,'\\');
        sink_put(out,s);
      } else
        outfields_unicode_print(out,s-128);

      start = value+i+1;
      end = 0;
      continue;
    }
    end++;
  }
  if (end)
    sink_write(out,start,end);
  sink_put(out,'"');

  return 0;
}

static uchar
outfields_array_print(const reliq *rq, SINK *out, const reliq_output_field_type *type, const char *value, const size_t valuel)
{
  if (!valuel)
    return 1;

  sink_put(out,'[');

  char const *start=value,*end,*last=value+valuel;
  reliq_output_field_type sub = {
    .type = type->subtype->type
  };
  char delim = '\n';
  if (type->argsl)
    delim = *type->args[0].b;

  while (start < last) {
    end = memchr(start,delim,last-start);
    if (!end)
      end = last;

    if (start != value)
      sink_put(out,',');

    outfields_value_print(rq,out,&sub,start,end-start,1);
    start = end+1;
  }

  sink_put(out,']');

  return 0;
}

static size_t
outfields_date_maxsize(const reliq_str *args, const size_t argsl)
{
  size_t max_sz = 0;
  for (size_t i = 0; i < argsl; i++)
    if (max_sz < args[i].s)
      max_sz = args[i].s;
  return max_sz;
}

static uchar
outfields_date_match(const reliq_str *args, const size_t argsl, char *matched, struct tm *date)
{
  size_t max_sz = outfields_date_maxsize(args,argsl);
  uchar found = 0;
  if (!max_sz)
    return found;
  char *format = alloca(max_sz+1);

  *date = (struct tm){0};
  for (size_t i = 0; i < argsl; i++) {
    memcpy(format,args[i].b,args[i].s);
    format[args[i].s] = '\0';

    char *r = strptime(matched,format,date);
    if (r && *r == '\0') {
      found = 1;
      break;
    }
  }
  return found;
}

static uchar
outfields_date_print(SINK *out, const reliq_output_field_type *type, const char *value, const size_t valuel)
{
  if (!type->argsl)
    return 1;

  const size_t max_iso_format_size = 24;
  size_t buf_size = MAX(max_iso_format_size+1,valuel+1);
  char *buf = memcpy(alloca(buf_size),value,valuel);
  buf[valuel] = '\0';

  struct tm date;
  if (!outfields_date_match(type->args,type->argsl,buf,&date))
    return 1;

  assert(strftime(buf,max_iso_format_size+1,"%FT%T%z",&date) == 24);
  outfields_str_print(out,buf,max_iso_format_size);
  return 0;
}

static uchar
outfields_url_print(const reliq *rq, SINK *out, const reliq_output_field_type *type, const char *value, const size_t valuel)
{
  const reliq_url *ref = &rq->url;
  reliq_url ref_buf;
  uchar uses_arg = type->argsl > 0;

  if (uses_arg) {
    reliq_str *s = &type->args[0];
    reliq_url_parse(s->b,s->s,NULL,0,false,&ref_buf);
    ref = &ref_buf;
  }

  reliq_url url;
  reliq_url_parse(value,valuel,ref->scheme.b,ref->scheme.s,false,&url);
  reliq_url_join(ref,&url,&url);

  outfields_str_print(out,url.url.b,url.url.s);

  reliq_url_free(&url);
  if (uses_arg)
    reliq_url_free(&ref_buf);

  return 0;
}

static void
outfields_value_print_default(char type, SINK *out, const char *value, const size_t valuel)
{
  switch (type) {
    case 's':
      sink_write(out,"\"\"",2);
      break;
    case 'n':
      sink_put(out,'0');
      break;
    case 'i':
      sink_put(out,'0');
      break;
    case 'u':
      sink_put(out,'0');
      break;
    case 'b':
      sink_write(out,"false",5);
      break;
    case 'd':
      outfields_str_print(out,value,valuel);
      break;
    case 'U':
      sink_write(out,"\"\"",2);
      break;
    case 'a':
      sink_write(out,"[]",2);
      break;
    default:
      sink_write(out,"null",4);
      break;
  }
}

static void
outfields_value_print(const reliq *rq, SINK *out, const reliq_output_field_type *type, const char *value, const size_t valuel, const uchar notempty)
{
  uchar def = 0;

  if (!notempty) {
    outfields_value_print_default(type->type,out,value,valuel);
    return;
  }

  switch (type->type) {
    case 's':
      def = outfields_str_print(out,value,valuel);
      break;
    case 'n':
      def = outfields_num_print(out,value,valuel,OUTFIELDS_NUM_FLOAT);
      break;
    case 'i':
      def = outfields_num_print(out,value,valuel,OUTFIELDS_NUM_INT);
      break;
    case 'u':
      def = outfields_num_print(out,value,valuel,OUTFIELDS_NUM_UNSIGNED);
      break;
    case 'b':
      def = outfields_bool_print(out,value,valuel);
      break;
    case 'd':
      def = outfields_date_print(out,type,value,valuel);
      break;
    case 'U':
      def = outfields_url_print(rq,out,type,value,valuel);
      break;
    case 'a':
      def = outfields_array_print(rq,out,type,value,valuel);
      break;
    default:
      sink_write(out,"null",4);
      break;
  }

  if (def)
    outfields_value_print_default(type->type,out,value,valuel);
}

static void
outfields_print_pre(const reliq *rq, struct outfield **fields, size_t *pos, const size_t size, const uint16_t lvl, uchar isarray, SINK *out)
{
  size_t i = *pos;

  sink_put(out,isarray ? '[' : '{');
  for (; i < size; i++) {
    struct outfield *field = fields[i];
    if (field->lvl < lvl)
        break;

    if (field->o && field->o->name.s) {
      sink_put(out,'"');
      sink_write(out,field->o->name.b,field->o->name.s);
      sink_put(out,'"');
      sink_put(out,':');
    }

    if (field->code == ofNamed || field->code == ofNoFieldsBlock) {
      if (field->f.type)
        sink_close(&field->f);

      if (field->o)
        outfields_value_print(rq,out,&field->o->type,field->v,field->s,field->notempty);
      if (field->v)
        free(field->v);
      field->s = 0;
    } else if (field->code == ofBlock || field->code == ofArray) {
      i++;
      outfields_print_pre(rq,fields,&i,size,lvl+1,(field->code == ofArray) ? 1 : 0,out);
      i--;
    }

    if (i+1 < size && fields[i+1]->lvl >= lvl)
      sink_put(out,',');
  }
  sink_put(out,isarray ? ']' : '}');

  *pos = i;
}

void
outfields_print(const reliq *rq, flexarr *fields, SINK *out) //fields: struct outfield*
{
  if (!fields->size)
    return;
  size_t pos = 0;
  outfields_print_pre(rq,(struct outfield**)fields->v,&pos,fields->size,0,0,out);
}

static void
reliq_output_field_type_free(reliq_output_field_type *type)
{
  if (type->args) {
    const size_t argsl = type->argsl;
    reliq_str *args = type->args;
    for (size_t i = 0; i < argsl; i++)
      free(args[i].b);
    free(args);
  }

  if (type->subtype) {
    reliq_output_field_type_free(type->subtype);
    free(type->subtype);
  }
}

void
reliq_output_field_free(reliq_output_field *outfield)
{
  if (outfield->name.b)
    free(outfield->name.b);
  if (outfield->annotation.b)
    free(outfield->annotation.b);

  reliq_output_field_type_free(&outfield->type);
}

void
outfields_free(flexarr *outfields) //struct outfield*
{
  struct outfield **outfieldsv = (struct outfield**)outfields->v;
  const size_t size = outfields->size;
  for (size_t i = 0; i < size; i++) {
    if (outfieldsv[i]->f.type)
      sink_close(&outfieldsv[i]->f);
    if (outfieldsv[i]->s)
      free(outfieldsv[i]->v);
    free(outfieldsv[i]);
  }
  flexarr_free(outfields);
}
