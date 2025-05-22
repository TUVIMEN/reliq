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

#if defined(__MINGW32__) || defined(__MINGW64__)
char *strptime(const char *restrict s, const char *restrict f, struct tm *restrict tm);
#endif

#define OUTFIELD_ARGS_INC 8
#define OUTFIELD_TYPE_INC 8

static void outfields_value_print(const reliq *rq, SINK *out, const reliq_field_type *types, const size_t typesl, const char *value, const size_t valuel, const uchar notempty);

static void
outfield_type_name_get(const char *src, size_t *pos, const size_t s, char const **type, size_t *typel)
{
  size_t i = *pos;

  *type = src+i;
  while (i < s && (isalnum(src[i]) || src[i] == '_' || src[i] == '-'))
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
err_unexpected_char(char c, size_t pos)
{
  return script_err("output field: type argument list: unexpected character 0x%02x at %lu",c,pos);
}

static reliq_error *
outfield_type_get_arg(const char *src, size_t *pos, const size_t size, SINK *buf, struct reliq_field_type_arg *arg)
{
  reliq_error *err = NULL;
  size_t i = *pos;

  if (src[i] == '"' || src[i] == '\'') {
    size_t qstart = i+1;
    if ((err = skip_quotes(src,&i,size)))
      goto END;
    size_t qend = i-1;

    sink_size(buf,0);
    splchars_conv_sink(src+qstart,qend-qstart,buf);
    sink_flush(buf);

    *arg = (struct reliq_field_type_arg){
      .type = RELIQ_FIELD_TYPE_ARG_STR,
      .v.s = (reliq_str){
        .b = memdup(*buf->v.sf.ptr,*buf->v.sf.ptrl),
        .s = *buf->v.sf.ptrl
      }
    };
  } else if (isdigit(src[i]) || src[i] == '-') {
    char r = universal_number(src,&i,size,&arg->v);
    if (r == 0)
      goto_script_seterr(END,"output field: type argument list: incorrect number");

    if (r == 'u') {
      arg->type = RELIQ_FIELD_TYPE_ARG_UNSIGNED;
    } else if (r == 's') {
      arg->type = RELIQ_FIELD_TYPE_ARG_SIGNED;
    } else if (r == 'd') {
      arg->type = RELIQ_FIELD_TYPE_ARG_FLOATING;
    } else
      assert(0);
  } else
    err = err_unexpected_char(src[i],i);

  END: ;
  *pos = i;
  return err;
}

static void
reliq_field_type_args_free(struct reliq_field_type_arg *args, const size_t argsl)
{
  for (size_t i = 0; i < argsl; i++) {
    if (args[i].type == RELIQ_FIELD_TYPE_ARG_STR)
      free(args[i].v.s.b);
  }
}

static reliq_error *
outfield_type_get_args(const char *src, size_t *pos, const size_t size, struct reliq_field_type_arg **args, size_t *argsl)
{
  if (*pos >= size)
    return NULL;

  reliq_error *err = NULL;
  size_t i = *pos;
  *args = NULL;
  *argsl = 0;
  flexarr a = flexarr_init(sizeof(struct reliq_field_type_arg),OUTFIELD_ARGS_INC);
  char *arg;
  size_t argl;
  SINK buf = sink_open(&arg,&argl);

  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')')
      goto_script_seterr(END,"output field: type: expected an argument in '(' bracket");

    struct reliq_field_type_arg s_arg;
    if ((err = outfield_type_get_arg(src,&i,size,&buf,&s_arg)))
      goto END;

    *(struct reliq_field_type_arg*)flexarr_inc(&a) = s_arg;

    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')') {
      i++;
      goto END;
    }

    if (src[i] != ',') {
      err = err_unexpected_char(src[i],i);
      goto END;
    }
    i++;
  }

  if (i >= size)
    goto_script_seterr(END,"output field: type argument list: unprecedented end of list at %lu",i);

  END: ;
  if (err) {
    reliq_field_type_args_free(a.v,a.size);
    flexarr_free(&a);
  } else
    flexarr_conv(&a,(void**)args,argsl);

  sink_destroy(&buf);

  *pos = i;
  return err;
}

static void
print_null(SINK *out)
{
  sink_write(out,"null",4);
}

#define OUTFIELDS_NUM_FLOAT 1
#define OUTFIELDS_NUM_SIGNED 2
#define OUTFIELDS_NUM_UNSIGNED 4

static uchar
outfields_num_get(const char *value, const size_t valuel, const uint8_t flags, uchar *hasminus, reliq_cstr *digits, reliq_cstr *points)
{
  if (!valuel)
    return 1;

  char const *start = value;
  size_t end = 0;
  *hasminus = 0;
  uchar haspoint=0;

  #define NUM_PARSE_FIRST_ZERO \
    if ((size_t)(start-value) < valuel && *start == '0') { \
      start++; \
      while ((size_t)(start-value) < valuel && *start == '0') \
        start++; \
      if ((size_t)(start-value) >= valuel || !isdigit(*start)) \
        start--; \
      goto GET_NUMBER; \
    }

  if (flags&(OUTFIELDS_NUM_FLOAT|OUTFIELDS_NUM_SIGNED)) {
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
      *hasminus = 1;
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
      && !digits->b && (start[end] == ',' || start[end] == '.') && isdigit(start[end+1]))
      haspoint = 1;

    reliq_cstr t = { .b = start, .s = end };
    if (digits->b) {
      *points = t;
    } else
      *digits = t;
  }

  start += end;
  end = 0;

  if (haspoint) {
    haspoint = 0;
    start++;
    goto GET_NUMBER;
  }

  return 0;
}

static char
outfields_num_matches(const uint8_t flags, const struct reliq_field_type_arg *args, const size_t argsl, uchar hasminus, reliq_cstr *digits, reliq_cstr *points)
{
  if (!argsl)
    return 0;

  size_t pos = 0;
  uint64_t u = number_handle(digits->b,&pos,digits->s);

  #define set_signed(x) \
    if (args[x].type == RELIQ_FIELD_TYPE_ARG_UNSIGNED) { \
      c = args[x].v.u; \
    } else \
      c = args[x].v.i

  #define set_floating(x) \
    if (args[x].type == RELIQ_FIELD_TYPE_ARG_FLOATING) { \
      c = args[x].v.d; \
    } else set_signed(x)

  if (flags == OUTFIELDS_NUM_UNSIGNED) {
    if (u < args[0].v.u)
      return 1;
    if (argsl > 1 && u > args[1].v.u)
      return 1;
  } else if (flags == OUTFIELDS_NUM_SIGNED) {
    int64_t s = u;
    if (hasminus)
      s *= -1;

    int64_t c;
    set_signed(0);
    if (s < c)
      return 1;

    if (argsl > 1) {
      set_signed(1);
      if (s > c)
        return 1;
    }
  } else {
    double d = u;
    pos = 0;
    d += get_point_of_double(points->b,&pos,points->s);
    if (hasminus)
      d *= -1;

    double c;
    set_floating(0);
    if (d < c)
      return 1;

    if (argsl > 1) {
    set_floating(1);
      if (d > c)
        return 1;
    }
  }

  return 0;
}

static uchar
outfields_num_print(SINK *out, const reliq_field_type *type, const char *value, const size_t valuel, const uint8_t flags)
{
  uchar hasminus = 0;
  reliq_cstr digits={0},points={0};
  if (outfields_num_get(value,valuel,flags,&hasminus,&digits,&points))
    return 1;

  if (outfields_num_matches(flags,type->args,type->argsl,hasminus,&digits,&points))
    return 1;

  if (hasminus)
    sink_put(out,'-');
  if (digits.b)
    sink_write(out,digits.b,digits.s);
  if (points.b) {
    sink_put(out,'.');
    sink_write(out,points.b,points.s);
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
outfields_array_print(const reliq *rq, SINK *out, const reliq_field_type *type, const char *value, const size_t valuel)
{
  if (!valuel)
    return 1;

  sink_put(out,'[');

  char const *start=value,*end,*last=value+valuel;
  char delim = '\n';
  if (type->argsl)
    delim = *type->args[0].v.s.b;

  while (start < last) {
    end = memchr(start,delim,last-start);
    if (!end)
      end = last;

    if (start != value)
      sink_put(out,',');

    outfields_value_print(rq,out,type->subtypes,type->subtypesl,start,end-start,1);
    start = end+1;
  }

  sink_put(out,']');

  return 0;
}

static size_t
outfields_date_maxsize(const struct reliq_field_type_arg *args, const size_t argsl)
{
  size_t max_sz = 0;
  for (size_t i = 0; i < argsl; i++)
    if (max_sz < args[i].v.s.s)
      max_sz = args[i].v.s.s;
  return max_sz;
}

static uchar
outfields_date_match(const struct reliq_field_type_arg *args, const size_t argsl, char *matched, struct tm *date)
{
  size_t max_sz = outfields_date_maxsize(args,argsl);
  uchar found = 0;
  if (!max_sz)
    return found;
  char *format = alloca(max_sz+1);

  *date = (struct tm){0};
  for (size_t i = 0; i < argsl; i++) {
    memcpy(format,args[i].v.s.b,args[i].v.s.s);
    format[args[i].v.s.s] = '\0';

    char *r = strptime(matched,format,date);
    if (r && *r == '\0') {
      found = 1;
      break;
    }
  }
  return found;
}

static uchar
outfields_date_print(SINK *out, const reliq_field_type *type, const char *value, const size_t valuel)
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
outfields_url_print(const reliq *rq, SINK *out, const reliq_field_type *type, const char *value, const size_t valuel)
{
  const reliq_url *ref = &rq->url;
  reliq_url ref_buf;
  uchar uses_arg = type->argsl > 0;

  if (uses_arg) {
    reliq_str *s = &type->args[0].v.s;
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

#define X(x) static uchar predef_repr_##x (UNUSED const reliq *rq, SINK *out, UNUSED const reliq_field_type *type, UNUSED const char *value, UNUSED const size_t valuel)
X(s)
{
  if (type->argsl > 0 && valuel < type->args[0].v.u)
    return 1;
  if (type->argsl > 1 && valuel > type->args[1].v.u)
    return 1;
  return outfields_str_print(out,value,valuel);
}
X(n) { return outfields_num_print(out,type,value,valuel,OUTFIELDS_NUM_FLOAT); }
X(i) { return outfields_num_print(out,type,value,valuel,OUTFIELDS_NUM_SIGNED); }
X(u) { return outfields_num_print(out,type,value,valuel,OUTFIELDS_NUM_UNSIGNED); }
X(b) { return outfields_bool_print(out,value,valuel); }
X(d) { return outfields_date_print(out,type,value,valuel); }
X(U) { return outfields_url_print(rq,out,type,value,valuel); }
X(a) { return outfields_array_print(rq,out,type,value,valuel); }
X(N) {
  print_null(out);
  return 0;
}
X(e) {  return outfields_str_print(out,value,valuel); }
#undef X

#define X(x) static void predef_default_val_##x (SINK *out, UNUSED const char *value, UNUSED const size_t valuel)
X(s) { sink_write(out,"\"\"",2); }
X(n) { sink_put(out,'0'); }
X(i) { sink_put(out,'0'); }
X(u) { sink_put(out,'0'); }
X(b) { sink_write(out,"false",5); }
X(d) { outfields_str_print(out,value,valuel); }
X(U) { sink_write(out,"\"\"",2); }
X(a) { sink_write(out,"[]",2); }
X(N) { print_null(out); }
X(e) { outfields_str_print(out,value,valuel); }
#undef X

static reliq_error *
err_too_many_arguments(const size_t amount, const char *type, const size_t typel, const size_t argsl)
{
  if (amount == 0)
    return script_err("output field: type %.*s doesn't take any arguments yet %lu were specified",(int)typel,type,argsl);
  if (amount == 1)
    return script_err("output field: type %.*s takes at most 1 argument yet %lu were specified",(int)typel,type,argsl);
  return script_err("output field: type %.*s takes at most %lu arguments yet %lu were specified",(int)typel,type,amount,argsl);
}

static reliq_error *
valid_no_args(const reliq_field_type *type)
{
  if (type->argsl > 0)
    return err_too_many_arguments(0,type->name.b,type->name.s,type->argsl);
  return NULL;
}

static reliq_error *
valid_2_unsigned(const reliq_field_type *type)
{
  if (type->argsl > 2)
    return err_too_many_arguments(2,type->name.b,type->name.s,type->argsl);

  for (size_t i = 0; i < 2; i++) {
    if (i >= type->argsl)
      break;
    if (type->args[i].type != RELIQ_FIELD_TYPE_ARG_UNSIGNED)
      return script_err("output field: type %.*s accepts only an unsigned integer argument",(int)type->name.s,type->name.b);
  }

  return NULL;
}

static reliq_error *
valid_2_signed(const reliq_field_type *type)
{
  if (type->argsl > 2)
    return err_too_many_arguments(2,type->name.b,type->name.s,type->argsl);

  for (size_t i = 0; i < 2; i++) {
    if (i >= type->argsl)
      break;
    if (type->args[i].type != RELIQ_FIELD_TYPE_ARG_UNSIGNED
        && type->args[i].type != RELIQ_FIELD_TYPE_ARG_SIGNED)
      return script_err("output field: type %.*s accepts only an integer argument",(int)type->name.s,type->name.b);
  }

  return NULL;
}

static reliq_error *
valid_2_float(const reliq_field_type *type)
{
  if (type->argsl > 2)
    return err_too_many_arguments(2,type->name.b,type->name.s,type->argsl);

  for (size_t i = 0; i < 2; i++) {
    if (i >= type->argsl)
      break;
    if (type->args[i].type != RELIQ_FIELD_TYPE_ARG_UNSIGNED
        && type->args[i].type != RELIQ_FIELD_TYPE_ARG_SIGNED
        && type->args[i].type != RELIQ_FIELD_TYPE_ARG_FLOATING)
      return script_err("output field: type %.*s accepts only a float argument",(int)type->name.s,type->name.b);
  }

  return NULL;
}

static reliq_error *
valid_one_str_optional(const reliq_field_type *type)
{
  if (type->argsl > 1)
    return err_too_many_arguments(1,type->name.b,type->name.s,type->argsl);
  if (type->argsl == 1 && type->args[0].type != RELIQ_FIELD_TYPE_ARG_STR)
    return script_err("output field: type %.*s accepts only a string argument",(int)type->name.s,type->name.b);
  return NULL;
}

static reliq_error *
valid_any_str(const reliq_field_type *type)
{
  const size_t argsl = type->argsl;
  const struct reliq_field_type_arg *args = type->args;
  for (size_t i = 0; i < argsl; i++)
    if (args[i].type != RELIQ_FIELD_TYPE_ARG_STR)
      return script_err("output field: type %.*s accepts only string arguments",(int)type->name.s,type->name.b);
  return NULL;
}

static reliq_error *
valid_array(const reliq_field_type *type)
{
  reliq_error *err = valid_one_str_optional(type);
  if (err)
    return err;

  if (type->args[0].v.s.s > 1)
    return script_err("output field: type %.*s: expected a single character argument",(int)type->name.s,type->name.b);
  return NULL;
}

struct predefined {
  reliq_cstr name;
  reliq_error *(*valid)(const reliq_field_type*);
  uchar (*repr)(const reliq*, SINK*, const reliq_field_type*, const char*, const size_t);
  void (*default_val)(SINK*, const char*, const size_t);
} const predefined_types[] = {
#define X(x,y) { \
  .name = { .b = #x, .s = sizeof(#x)-1 }, \
  .valid = y, \
  .repr = predef_repr_##x, \
  .default_val = predef_default_val_##x \
}
X(s,valid_2_unsigned),
X(n,valid_2_float),
X(i,valid_2_signed),
X(u,valid_2_unsigned),
X(b,valid_no_args),
X(d,valid_any_str),
X(U,valid_one_str_optional),
X(a,valid_array),
X(N,valid_no_args),
X(e,NULL),
#undef X
};

const struct predefined *
find_predefined(const char *name, const size_t namel)
{
  for (size_t i = 0 ; i < LENGTH(predefined_types); i++)
    if (memeq(name,predefined_types[i].name.b,namel,predefined_types[i].name.s))
      return predefined_types+i;
  return NULL;
}

static void reliq_field_types_free(reliq_field_type *types, const size_t typesl);

static void
reliq_field_type_free(reliq_field_type *type)
{
  if (type->name.b)
    free(type->name.b);

  if (type->args) {
    reliq_field_type_args_free(type->args,type->argsl);
    free(type->args);
  }

  reliq_field_types_free(type->subtypes,type->subtypesl);
}

static void
reliq_field_types_free(reliq_field_type *types, const size_t typesl)
{
  if (!types)
    return;

  for (size_t i = 0; i < typesl; i++)
    reliq_field_type_free(types+i);
  free(types);
}

static reliq_error *outfield_type_get(const char *src, size_t *pos, const size_t size, reliq_field_type **types, size_t *typesl, const uchar isarray);

static reliq_error *
outfield_type_get_r(const char *src, size_t *pos, const size_t size, reliq_field_type *type, const uchar isarray)
{
  reliq_error *err = NULL;
  size_t i = *pos;
  const char *name;
  size_t namel;
  outfield_type_name_get(src,&i,size,&name,&namel);
  if (namel == 0)
    goto_script_seterr(END,"output field: unspecified type name at %lu",i);

  const struct predefined *match = find_predefined(name,namel);
  const uchar typearray = (match && match->name.s == 1 && match->name.b[0] == 'a');

  if (isarray && typearray)
    goto_script_seterr(END,"output field: array: array type in array is not allowed");

  type->name.b = memdup(name,namel);
  type->name.s = namel;

  if (i < size && src[i] == '(') {
    i++;
    if ((err = outfield_type_get_args(src,&i,size,&type->args,&type->argsl)))
      goto END;
    if (match && match->valid && (err = match->valid(type)))
      goto END;
  }

  if (typearray)
    if ((err = outfield_type_get(src,&i,size,&type->subtypes,&type->subtypesl,1)))
      goto END;

  END: ;
  if (err)
    reliq_field_type_free(type);
  *pos = i;
  return err;
}

static reliq_error *
outfield_type_get(const char *src, size_t *pos, const size_t size, reliq_field_type **types, size_t *typesl, const uchar isarray)
{
  size_t i = *pos;

  if (i < size && src[i] == '.') {
    i++;
  } else {
    *typesl = 1;
    *types = calloc(1,sizeof(reliq_field_type));
    (*types)->name = (reliq_str){
      .b = memdup("s",1),
      .s = 1
    };
    return NULL;
  }

  reliq_error *err = NULL;
  flexarr tps = flexarr_init(sizeof(reliq_field_type),OUTFIELD_TYPE_INC);

  while (i < size) {
    reliq_field_type t = {0};
    if ((err = outfield_type_get_r(src,&i,size,&t,isarray)))
      goto END;

    *(reliq_field_type*)flexarr_inc(&tps) = t;
    if (i >= size || src[i] != '|')
      break;
    i++;
  }

  END:
  if (err) {
    flexarr_free(&tps);
  } else {
    flexarr_conv(&tps,(void**)types,typesl);
  }
  *pos = i;
  return err;
}

reliq_error *
reliq_field_comp(const char *src, size_t *pos, const size_t size, reliq_field *outfield)
{
  if (*pos >= size || src[*pos] != '.')
    return NULL;

  reliq_error *err = NULL;
  const char *name;
  size_t i=*pos,namel=0;

  *outfield = (reliq_field){0};
  i++;

  outfield_name_get(src,&i,size,&name,&namel);

  if ((err = outfield_type_get(src,&i,size,&outfield->types,&outfield->typesl,0)))
    goto ERR;

  outfield->isset = 1;

  if (i < size && (src[i] == '\'' || src[i] == '"')) {
    size_t qstart = i+1;
    if ((err = skip_quotes(src,&i,size)))
      goto ERR;
    size_t qend = i-1;

    size_t qlen = qend-qstart;
    outfield->annotation = (reliq_str){
      .b = memdup(src+qstart,qlen),
      .s = qlen
    };
  }

  if (i < size && !isspace(src[i])) {
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

static void
outfields_value_print(const reliq *rq, SINK *out, const reliq_field_type *types, const size_t typesl, const char *value, const size_t valuel, const uchar notempty)
{
  if (!typesl) {
    DEFAULT_PRINT: ;
    outfields_str_print(out,value,valuel);
    return;
  }

  size_t i = 0;
  if (!notempty)
    i = typesl-1;

  for (; i < typesl; i++) {
    const reliq_field_type *type = types+i;

    const struct predefined *match = find_predefined(type->name.b,type->name.s);
    if (!match)
      goto DEFAULT_PRINT;

    if (!notempty) {
      match->default_val(out,value,valuel);
      return;
    }

    uchar def = match->repr(rq,out,type,value,valuel);
    if (!def)
      return;
    if (i == typesl-1)
      match->default_val(out,value,valuel);
  }
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
        outfields_value_print(rq,out,field->o->types,field->o->typesl,field->v,field->s,field->notempty);
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

void
reliq_field_free(reliq_field *outfield)
{
  if (outfield->name.b)
    free(outfield->name.b);
  if (outfield->annotation.b)
    free(outfield->annotation.b);

  reliq_field_types_free(outfield->types,outfield->typesl);
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
