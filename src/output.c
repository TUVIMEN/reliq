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
#include "output.h"
#include "format.h"
#include "exprs.h"

/*#define NCOLLECTOR_DEBUG*/
/*#define FCOLLECTOR_DEBUG*/
/*#define PRINT_CODE_DEBUG*/

#define FCOLLECTOR_OUT_INC (1<<4)
#define OUTFIELDS_INC (1<<4)

typedef struct {
    const reliq *rq;
    flexarr *outfields; //struct outfield*
    flexarr *fcol_outs; //struct fcollector_out*
    const struct fcollector *fcols;
    const struct ncollector *ncols;
    char *ncol_ptr;

    SINK *out_origin;
    SINK *out_ncol;
    SINK *out_fcol;
    SINK **out_field;

    const size_t ncolsl;
    const size_t fcolsl;
    size_t ncol_ptrl;

    size_t fcols_i;
    size_t ncols_i;
    size_t amount_i; //iterator of ncols[ncols_i].amount

    uint16_t field_lvl;
    uchar field_ended;
} nodes_output_state;

static void outfields_value_print(SINK *out, const reliq_output_field *field, const char *value, const size_t valuel);

static inline reliq_error *
node_output(const reliq_chnode *hnode, const reliq_chnode *parent, const reliq_format_func *format, const size_t formatl, SINK *output, const reliq *rq)
{
  return format_exec(NULL,0,output,hnode,parent,format,formatl,rq);
}

static reliq_error *
reliq_output_type_get(const char *src, size_t *pos, const size_t s, uchar arraypossible, char const **type, size_t *typel)
{
  size_t i = *pos;
  reliq_error *err = NULL;

  *type = src+i;
  while (i < s && isalnum(src[i]))
    (i)++;
  *typel = i-(*type-src);
  if (i < s && !isspace(src[i]) && !(arraypossible && (**type == 'a' && (src[i] == '(' || src[i] == '.'))))
    err = script_err("output field: unexpected character in type 0x%02x",src[i]);

  *pos = i;
  return err;
}

static reliq_error *
reliq_output_type_array_get_delim(const char *src, size_t *pos, const size_t s, char *delim)
{
  reliq_error *err = NULL;
  size_t i = *pos;

  if (i >= s || src[i] != '(')
    goto END;

  i++;
  char const *b_start = src+i;
  char *b_end = memchr(b_start,')',s-i);
  if (!b_end)
    goto_script_seterr(END,"output field: array: could not find the end of '(' bracket");

  while (b_start != b_end && isspace(*b_start))
    b_start++;
  if (b_start == b_end || *b_start != '"')
    goto_script_seterr(END,"output field: array: expected argument in '(' bracket");

  b_start++;
  char *q_end = memchr(b_start,'"',s-i);
  if (!q_end)
    goto_script_seterr(END,"output field: array: could not find the end of '\"' quote");

  *delim = *b_start;
  if (*b_start == '\\' && b_start+1 != b_end) {
    b_start++;
    size_t traversed;
    *delim = splchar2(b_start,b_end-b_start,&traversed);
    if (*delim != '\\' && *delim == *b_start) {
      *delim = '\\';
      b_start--;
    } else
      b_start += traversed-1;
  }
  b_start++;
  if (b_start != q_end)
    goto_script_seterr(END,"output field: array: expected a single character argument");

  q_end++;
  while (q_end != b_end && isspace(*q_end))
    q_end++;
  if (q_end != b_end)
    goto_script_seterr(END,"output field: array: expected only one argument");

  i = b_end-src+1;

  END: ;
  *pos = i;
  return err;
}

static reliq_error *
reliq_output_type_array_get(const char *src, size_t *pos, const size_t s, reliq_output_field *outfield)
{
  reliq_error *err = NULL;
  size_t i = *pos;

  if (i >= s || (err = reliq_output_type_array_get_delim(src,&i,s,&outfield->arr_delim)))
    goto END;

  if (i < s && !isspace(src[i]) && src[i] != '.')
    goto_script_seterr(END,"output field: array: unexpected character 0x%02x",src[i]);

  if (i < s && src[i] == '.') {
    i++;
    char const *arr_type;
    size_t arr_typel = 0;
    if ((err = reliq_output_type_get(src,&i,s,0,&arr_type,&arr_typel)))
      goto END;
    if (arr_typel) {
      if (*arr_type == 'a')
        goto_script_seterr(END,"output field: array: array type in array is not allowed");
      outfield->arr_type = *arr_type;
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
  const char *name,*type;
  size_t i=*pos,namel=0,typel=0;

  outfield->arr_type = 's';
  outfield->arr_delim = '\n';

  i++;
  name = src+i;
  while (i < s && (isalnum(src[i]) || src[i] == '-' || src[i] == '_'))
    i++;
  namel = i-(name-src);
  if (i < s && !isspace(src[i])) {
    if (src[i] != '.')
      goto_script_seterr(ERR,"output field: unexpected character in name 0x%02x",src[i]);
    i++;

    if ((err = reliq_output_type_get(src,&i,s,1,&type,&typel)))
      goto ERR;

    if (typel && *type == 'a')
      if ((err = reliq_output_type_array_get(src,&i,s,outfield)))
        goto ERR;
  }

  outfield->isset = 1;

  if (!namel)
    goto ERR;

  outfield->type = typel ? *type : 's';
  outfield->name.s = namel;
  outfield->name.b = memdup(name,namel);

  ERR:
  *pos = i;
  return err;
}

struct fcollector_out {
  SINK *f;
  char *v;
  size_t s;
  size_t current;
};

struct outfield {
  SINK *f;
  char *v;
  size_t s;
  reliq_output_field const *o;
  uint16_t lvl;
  uchar code;
};

static void
fcollector_rearrange_pre(struct fcollector *fcols, size_t start, size_t end, const uint16_t lvl)
{
  size_t i=start;
  while (start < end) {
    while (i < end && fcols[i].lvl != lvl)
      i++;

    if (i < end && i != start) {
      struct fcollector t = fcols[i];
      memmove(fcols+start+1,fcols+start,(i-start)*sizeof(struct fcollector));
      fcols[start] = t;

      if (i-start > 1)
        fcollector_rearrange_pre(fcols,start+1,i+1,lvl+1);
    }

    start = ++i;
  }
}

void
fcollector_rearrange(flexarr *fcollector) //fcollector: struct fcollector
{
  if (!fcollector->size)
    return;
  fcollector_rearrange_pre((struct fcollector*)fcollector->v,0,fcollector->size,0);
}

static reliq_error *
fcollector_out_end(flexarr *outs, const size_t ncurrent, const struct fcollector *fcols, const reliq *rq, SINK *out, SINK **out_fcol) //outs: struct fcollector_out*
{
  reliq_error *err = NULL;
  while (1) {
    if (!outs->size)
      return err;

    struct fcollector_out *fcol_out_last = ((struct fcollector_out**)outs->v)[outs->size-1];
    const struct fcollector *ecurrent = fcols+fcol_out_last->current;

    if (ecurrent->end != ncurrent)
      return err;

    reliq_expr const *rqe = ecurrent->e;

    reliq_format_func *format = rqe->exprf;
    size_t formatl = rqe->exprfl;
    if (ecurrent->isnodef) {
      format = rqe->nodef;
      formatl = rqe->nodefl;
    }
    SINK *out_t;
    if (ecurrent->lvl == 0) {
      out_t =  out;
      *out_fcol = NULL;
    } else {
      out_t = ((struct fcollector_out**)outs->v)[outs->size-2]->f;
      *out_fcol = out_t;
    }

    sink_close(fcol_out_last->f);
    err = format_exec(fcol_out_last->v,fcol_out_last->s,out_t,NULL,NULL,format,formatl,rq);
    free(fcol_out_last->v);

    free(fcol_out_last);
    flexarr_dec(outs);

    if (err)
      return err;
  }
}

static void
fcollector_outs_free(flexarr *outs) //outs: struct fcollector_out*
{
  struct fcollector_out **outsv = (struct fcollector_out**)outs->v;
  size_t size = outs->size;
  for (size_t i = 0; i < size; i++) {
    sink_close(outsv[i]->f);
    if (outsv[i]->s)
      free(outsv[i]->v);
    free(outsv[i]);
  }
  flexarr_free(outs);
}

#define OUTFIELDS_NUM_FLOAT 1
#define OUTFIELDS_NUM_INT 2
#define OUTFIELDS_NUM_UNSIGNED 4

static void
outfields_num_print(SINK *out, const char *value, const size_t valuel, const uint8_t flags)
{
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
}

static void
outfields_bool_print(SINK *out, const char *value, const size_t valuel)
{
  int ret = 0;

  if (!valuel || (*value == '-' && valuel > 1 && isdigit(value[1])))
    goto END;

  if (*value == 'y' || *value == 'Y' || *value == 't' || *value == 'T') {
    ret = 1;
    goto END;
  }

  char const *start = value;
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

static void
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
}

static void
outfields_array_print(SINK *out, const reliq_output_field *field, const char *value, const size_t valuel)
{

  sink_put(out,'[');

  char const *start=value,*end,*last=value+valuel;
  reliq_output_field f = {
    .type = field->arr_type
  };

  while (start < last) {
    end = memchr(start,field->arr_delim,last-start);
    if (!end)
      end = last;

    if (start != value)
      sink_put(out,',');
    outfields_value_print(out,&f,start,end-start);
    start = end+1;
  }

  sink_put(out,']');
}

static void
outfields_value_print(SINK *out, const reliq_output_field *field, const char *value, const size_t valuel)
{
  if (!field)
    return;
  switch (field->type) {
    case 's':
      outfields_str_print(out,value,valuel);
      break;
    case 'n':
      outfields_num_print(out,value,valuel,OUTFIELDS_NUM_FLOAT);
      break;
    case 'i':
      outfields_num_print(out,value,valuel,OUTFIELDS_NUM_INT);
      break;
    case 'u':
      outfields_num_print(out,value,valuel,OUTFIELDS_NUM_UNSIGNED);
      break;
    case 'b':
      outfields_bool_print(out,value,valuel);
      break;
    case 'a':
      outfields_array_print(out,field,value,valuel);
      break;
    default:
      sink_write(out,"null",4);
      break;
  }
}

static void
outfields_print_pre(struct outfield **fields, size_t *pos, const size_t size, const uint16_t lvl, uchar isarray, SINK *out)
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
      if (field->f)
        sink_close(field->f);
      field->f = NULL;

      outfields_value_print(out,field->o,field->v,field->s);
      if (field->v)
        free(field->v);
      field->s = 0;
    } else if (field->code == ofBlock || field->code == ofArray) {
      i++;
      outfields_print_pre(fields,&i,size,lvl+1,(field->code == ofArray) ? 1 : 0,out);
      i--;
    }

    if (i+1 < size && fields[i+1]->lvl >= lvl)
      sink_put(out,',');
  }
  sink_put(out,isarray ? ']' : '}');

  *pos = i;
}

static void
outfields_print(flexarr *fields, SINK *out) //fields: struct outfield*
{
  if (!fields->size)
    return;
  size_t pos = 0;
  outfields_print_pre((struct outfield**)fields->v,&pos,fields->size,0,0,out);
}

static void
outfields_free(flexarr *outfields) //struct outfield*
{
  struct outfield **outfieldsv = (struct outfield**)outfields->v;
  const size_t size = outfields->size;
  for (size_t i = 0; i < size; i++) {
    if (outfieldsv[i]->f)
      sink_close(outfieldsv[i]->f);
    if (outfieldsv[i]->s)
      free(outfieldsv[i]->v);
    free(outfieldsv[i]);
  }
  flexarr_free(outfields);
}

#ifdef PRINT_CODE_DEBUG
static void
print_code_debug(const size_t nodeindex, uint16_t fieldlvl, const enum outfieldCode code, const enum outfieldCode prevcode)
{
  if (fieldlvl && code == ofBlockEnd)
    fieldlvl--;
  if (nodeindex != 0 && (code != ofBlockEnd || prevcode != ofNamed || prevcode != ofNoFieldsBlock))
    for (size_t k = 0; k < fieldlvl; k++)
      fputs("  ",stderr);

  switch (code) {
    case ofNULL: fprintf(stderr,"|NULL %lu| {}\n",nodeindex); break;
    case ofUnnamed: fprintf(stderr,"|unnamed %lu| {}\n",nodeindex); break;
    case ofBlock: fprintf(stderr,"|block %lu| {\n",nodeindex); break;
    case ofArray: fprintf(stderr,"|array %lu| {\n",nodeindex); break;
    case ofNoFieldsBlock: fprintf(stderr,"|noFieldsBlock %lu| {",nodeindex); break;
    case ofNamed: fprintf(stderr,"|named %lu| {",nodeindex); break;
    case ofBlockEnd: fprintf(stderr,"} |blockEnd %lu|\n",nodeindex); break;
  }
}
#endif //PRINT_CODE_DEBUG

static SINK *
output_default(nodes_output_state *st)
{
  if (st->out_ncol)
    return st->out_ncol;
  if (st->out_fcol)
    return st->out_fcol;
  if (st->out_field)
    return *st->out_field;
  return st->out_origin;
}

static void
field_ended_free(SINK ***out_field, uchar *field_ended)
{
  if (*out_field) {
    sink_close(**out_field);
    **out_field = NULL;
    *out_field = NULL;
  }
  *field_ended = 0;
}

static void
fcollector_start(nodes_output_state *st)
{
  while (st->fcols_i < st->fcolsl && st->fcols[st->fcols_i].start == st->ncols_i) {
    struct fcollector_out *ff,**ff_r;
    ff_r = flexarr_inc(st->fcol_outs);
    *ff_r = malloc(sizeof(struct fcollector_out));

    ff = *ff_r;
    ff->f = sink_open(&ff->v,&ff->s);
    ff->current = st->fcols_i++;
    st->out_fcol = ff->f;
  }
}

static void
ncollector_new(nodes_output_state *st)
{
  if (st->ncols_i >= st->ncolsl || !st->ncols[st->ncols_i].e || !((reliq_expr*)st->ncols[st->ncols_i].e)->exprfl)
    return;
  if (st->out_ncol) {
    sink_close(st->out_ncol);
    free(st->ncol_ptr);
  }
  st->out_ncol = sink_open(&st->ncol_ptr,&st->ncol_ptrl);
}

static reliq_error *
ncollector_end(nodes_output_state *st)
{
  reliq_error *err = NULL;
  const struct ncollector *ncol = st->ncols+st->ncols_i;
  if (ncol->e && st->out_ncol) {
    sink_close(st->out_ncol);
    st->out_ncol = NULL;

    SINK *out_default = output_default(st);
    err = format_exec(st->ncol_ptr,st->ncol_ptrl,out_default,NULL,NULL,
      (ncol->e)->exprf,
      (ncol->e)->exprfl,st->rq);
    free(st->ncol_ptr);
    if (err)
      goto END;
  }

  /*
     if output for field is not NULL, it means that this ncollector
     is a child of it and its outputs, if there isn't one then
     fallback to the original output
  */
  SINK *out_default = st->out_field ? *st->out_field : st->out_origin;
  if ((err = fcollector_out_end(st->fcol_outs,st->ncols_i,st->fcols,st->rq,out_default,&st->out_fcol)))
    goto END;

  st->amount_i = 0;
  st->ncols_i++;

  if (st->field_ended)
    field_ended_free(&st->out_field,&st->field_ended);

  END: ;
  return err;
}

static struct outfield *
outfields_inc(enum outfieldCode code, uint16_t lvl, reliq_output_field const *fieldname, flexarr *outfields)
{
  struct outfield *field,
      **field_pre = flexarr_inc(outfields);
  *field_pre = malloc(sizeof(struct outfield));
  field = *field_pre;

  field->s = 0;
  field->f = NULL;
  field->lvl = lvl;
  field->code = (uchar)code;

  struct outfield **outfieldsv = (struct outfield**)outfields->v;
  if (outfields->size > 2 && fieldname == outfieldsv[outfields->size-2]->o) {
    field->o = NULL;
  } else
    field->o = fieldname;

  return field;
}

static uchar
nodes_output_code_handle(enum outfieldCode code, enum outfieldCode prevcode, size_t nodes_i_diff, SINK *out, const reliq_compressed *compn, nodes_output_state *st)
{
  struct outfield *field;

  switch (code) {
    case ofUnnamed:
      sink_put(out,'\n');
      break;
    case ofBlock:
    case ofArray:
    case ofNoFieldsBlock:
    case ofNamed:
      field = outfields_inc(code,st->field_lvl,(reliq_output_field const*)compn->parent,st->outfields);

      if (code == ofNamed || code == ofNoFieldsBlock) {
        field->f = sink_open(&field->v,&field->s);
        st->out_field = &field->f;
      }

      st->field_lvl++;
      st->field_ended = 0;
      break;
    case ofBlockEnd:
      if (st->field_lvl)
        st->field_lvl--;
      st->field_ended = 1;

      if ((prevcode == ofNoFieldsBlock || prevcode == ofArray || prevcode == ofBlock)
        && nodes_i_diff == 1) {
        //st->nodes_i-prev_nodes_i is 1 meaning that block was immedietly ended, and so it has to end
        ncollector_end(st);
      }

      if (st->amount_i == 0) {
        //the first node in st->ncols[st->ncols_i] was ending block, so the previous one did not free st->out_field
        field_ended_free(&st->out_field,&st->field_ended);
        return 1;
      }
      break;
    default:
      break;
  }

  if (code != ofUnnamed && code != ofNamed && (prevcode != ofNamed || code != ofBlockEnd))
    return 1;
  return 0;
}

static reliq_error *
nodes_output_r(const flexarr *comp_nodes, nodes_output_state *st) //comp_nodes: reliq_compressed
{
  size_t nodes_i = 0;
  const size_t nodesl = comp_nodes->size;
  const reliq_compressed *nodes = comp_nodes->v;

  reliq_error *err = NULL;
  enum outfieldCode prevcode = ofUnnamed;
  size_t prev_nodes_i = 0;

  #ifdef PRINT_CODE_DEBUG
  RELIQ_DEBUG_SECTION_HEADER("CODES");
  #endif

  for (;; nodes_i++) {
    if (nodesl && st->amount_i == 0) {
      fcollector_start(st);
      ncollector_new(st);
    }
    if (nodes_i >= nodesl)
      break;

    const struct ncollector *ncol =
      (st->ncols_i < st->ncolsl) ? st->ncols+st->ncols_i : NULL;

    SINK *out_default = output_default(st);
    const reliq_compressed *compn = nodes+nodes_i;
    enum outfieldCode code = OUTFIELDCODE(compn->hnode);
    if (code) {
      #ifdef PRINT_CODE_DEBUG
      print_code_debug(nodes_i,st->field_lvl,code,prevcode);
      #endif
      enum outfieldCode prevcode_r = prevcode;
      size_t prev_nodes_i_r = prev_nodes_i;
      prevcode = code;
      prev_nodes_i = nodes_i;

      size_t diff = (nodes_i < nodesl)
          ? nodes_i-prev_nodes_i_r : 0;
      if (nodes_output_code_handle(code,prevcode_r,diff,out_default,compn,st))
        continue;
    } else if (ncol && ncol->e) {
      const reliq_expr *e = (const reliq_expr*)ncol->e;
      if ((err = node_output(compn->hnode+st->rq->nodes,
        (compn->parent == (uint32_t)-1) ? NULL : compn->parent+st->rq->nodes,
        e->nodef,
        e->nodefl,out_default,st->rq)))
        break;
    }

    st->amount_i++;
    if (ncol && ncol->amount == st->amount_i
      && (err = ncollector_end(st)))
      break;
  }

  #ifdef PRINT_CODE_DEBUG
  fputc('\n',stderr);
  #endif

  return err;
}

#if defined(NCOLLECTOR_DEBUG) || defined(FCOLLECTOR_DEBUG)

static void
reliq_expr_print(const reliq_expr *e)
{
  if (!e) {
    fprintf(stderr,"\033[31mNULL\033[0m");
    return;
  }
  fprintf(stderr,"\033[35m%p\033[0m childfields(\033[31m%u\33[0m) childformats(\033[34m%u\033[0m)",(void*)e,e->childfields,e->childformats);
}

#endif

#ifdef NCOLLECTOR_DEBUG

static void
ncollector_print(const flexarr *ncollector) //ncollector: struct ncollector
{
  const size_t ncolsl = ncollector->size;
  const struct ncollector *ncols = ncollector->v;
  size_t start = 0;

  RELIQ_DEBUG_SECTION_HEADER("NCOLLECTOR");
  for (size_t i = 0; i < ncolsl; i++) {
    fprintf(stderr,"\033[34m%03lu\033[0m ",i);

    reliq_expr_print(ncols[i].e);
    fprintf(stderr," amount(\033[33m%02lu\033[0m) start(\033[32m%04lu\033[0m)\n",ncols[i].amount,start);
    start += ncols[i].amount;
  }
  fputc('\n',stderr);
}

#endif //NCOLLECTOR_DEBUG

#ifdef FCOLLECTOR_DEBUG

static void
fcollector_print(const flexarr *fcollector) //fcollector: struct fcollector
{
  const size_t fcolsl = fcollector->size;
  const struct fcollector *fcols = fcollector->v;

  RELIQ_DEBUG_SECTION_HEADER("FCOLLECTOR");
  for (size_t i = 0; i < fcolsl; i++) {
    for (size_t j = 0; j < fcols[i].lvl; j++)
      fputs("  ",stderr);
    fprintf(stderr,"\033[3%cm%03lu\033[0m%*s",((fcols[i].lvl)%7)+'1',i,12-fcols[i].lvl*2," ");
    reliq_expr_print(fcols[i].e);
    fprintf(stderr," %03lu-%03lu lvl(%u) nodef(%u)\n",fcols[i].start,fcols[i].end,fcols[i].lvl,fcols[i].isnodef);
  }
  fputc('\n',stderr);
}

#endif //FCOLLECTOR_DEBUG

reliq_error *
nodes_output(const reliq *rq, SINK *output, const flexarr *compressed_nodes, const flexarr *ncollector, flexarr *fcollector) //compressed_nodes: reliq_compressed, ncollector: struct ncollector, fcollector: struct fcollector
{
  if (!compressed_nodes->size || !ncollector->size)
    return NULL;

  fcollector_rearrange(fcollector);

  #ifdef NCOLLECTOR_DEBUG
  ncollector_print(ncollector);
  #endif

  #ifdef FCOLLECTOR_DEBUG
  fcollector_print(fcollector);
  #endif

  nodes_output_state st = {
    .outfields = flexarr_init(sizeof(struct outfield*),OUTFIELDS_INC),
    .fcol_outs = flexarr_init(sizeof(struct fcollector_out*),FCOLLECTOR_OUT_INC),

    .rq = rq,
    .out_origin = output,

    .fcols = (struct fcollector*)fcollector->v,
    .fcolsl = fcollector->size,
    .ncols = (struct ncollector*)ncollector->v,
    .ncolsl = ncollector->size,
  };

  reliq_error *err = nodes_output_r(compressed_nodes,&st);

  if (!err)
    outfields_print(st.outfields,st.out_origin);

  fcollector_outs_free(st.fcol_outs);
  outfields_free(st.outfields);

  return err;
}
