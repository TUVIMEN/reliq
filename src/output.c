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

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "edit.h"
#include "output.h"

#define FCOLLECTOR_OUT_INC (1<<4)
#define OUTFIELDS_INC (1<<4)

static void outfields_value_print(FILE *out, const reliq_output_field *field, const char *value, const size_t valuel);

reliq_error *
node_output(const reliq_hnode *hnode, const reliq_hnode *parent,
        #ifdef RELIQ_EDITING
        const reliq_format_func *format
        #else
        const char *format
        #endif
        , const size_t formatl, FILE *output, const reliq *rq) {
  #ifdef RELIQ_EDITING
  return format_exec(NULL,0,output,hnode,parent,format,formatl,rq);
  #else
  if (format) {
    reliq_printf(output,format,formatl,hnode,parent,rq);
  } else
    reliq_print(output,hnode);
  return NULL;
  #endif
}

struct fcollector_out {
  FILE *f;
  char *v;
  size_t s;
  size_t current;
};

struct outfield {
  FILE *f;
  char *v;
  size_t s;
  reliq_output_field const *o;
  ushort lvl;
  uchar code;
};

#ifdef RELIQ_EDITING
void
fcollector_rearrange_pre(struct fcollector_expr *fcols, size_t start, size_t end, ushort lvl)
{
  size_t i=start;
  while (start < end) {
    while (i < end && fcols[i].lvl != lvl)
      i++;

    if (i < end && i != start) {
      struct fcollector_expr t = fcols[i];
      for (size_t j = i-1;; j--) {
        fcols[j+1] = fcols[j];
        if (j == start)
          break;
      }
      fcols[start] = t;

      if (i-start > 1)
        fcollector_rearrange_pre(fcols,start+1,i+1,lvl+1);
    }

    start = ++i;
  }
}

void
fcollector_rearrange(flexarr *fcollector)
{
  fcollector_rearrange_pre((struct fcollector_expr*)fcollector->v,0,fcollector->size,0);
}

static reliq_error *
fcollector_out_end(flexarr *outs, const size_t ncurrent, struct fcollector_expr *fcols, const reliq *rq, FILE *rout, FILE **fout)
{
  reliq_error *err = NULL;
  START: ;
  if (!outs->size)
    return err;

  struct fcollector_out *fcol_out_last = ((struct fcollector_out**)outs->v)[outs->size-1];
  struct fcollector_expr *ecurrent = &fcols[fcol_out_last->current];

  if (ecurrent->end != ncurrent)
    return err;

  reliq_expr const *rqe = ecurrent->e;

  reliq_format_func *format = rqe->exprf;
  size_t formatl = rqe->exprfl;
  if (ecurrent->isnodef) {
    format = rqe->nodef;
    formatl = rqe->nodefl;
  }
  FILE *tmp_out = (ecurrent->lvl == 0) ? rout : ((struct fcollector_out**)outs->v)[outs->size-2]->f;
  *fout = tmp_out;

  fclose(fcol_out_last->f);
  err = format_exec(fcol_out_last->v,fcol_out_last->s,tmp_out,NULL,NULL,format,formatl,rq);
  free(fcol_out_last->v);

  free(fcol_out_last);
  flexarr_dec(outs);

  if (err)
    return err;

  goto START;
}
#endif

#define OUTFIELDS_NUM_FLOAT 1
#define OUTFIELDS_NUM_INT 2
#define OUTFIELDS_NUM_UNSIGNED 4

static void
outfields_num_print(FILE *out, const char *value, const size_t valuel, uchar flags)
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
      fputc('-',out);
    fwrite(start,1,end,out);
  } else if (!pointcount)
    fputc('0',out);

  start += end;
  end = 0;

  if (haspoint) {
    pointcount++;
    haspoint = 0;
    start++;
    isminus = 0;
    fputc('.',out);
    goto GET_NUMBER;
  }
}

static void
outfields_bool_print(FILE *out, const char *value, const size_t valuel)
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
  fputs(ret ? "true" : "false",out);
}

static void
outfields_unicode_print(FILE *out, ushort character)
{
  char val[] = "\\u0000";
  const size_t vall = 6;

  for (size_t i = vall-1; i >= 2; i--) {
    char c = character&0xf;
    character >>= 4;
    val[i] = (c < 10) ? c+'0' : c+'a'-10;
  }
  fwrite(val,1,vall,out);
}

static void
outfields_str_print(FILE *out, const char *value, const size_t valuel)
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

  fputc('"',out);

  for (size_t i = 0; i < valuel; i++) {
    uchar s = sub[(uchar)value[i]];
    if (s) {
      if (end)
        fwrite(start,1,end,out);
      if (s < 128) {
        fputc('\\',out);
        fputc(s,out);
      } else
        outfields_unicode_print(out,s-128);

      start = value+i+1;
      end = 0;
      continue;
    }
    end++;
  }
  if (end)
    fwrite(start,1,end,out);
  fputc('"',out);
}

static void
outfields_array_print(FILE *out, const reliq_output_field *field, const char *value, const size_t valuel)
{

  fputc('[',out);

  char const *start=value,*end,*last=value+valuel;
  reliq_output_field f;
  f.type = field->arr_type;

  while (start < last) {
    end = memchr(start,field->arr_delim,last-start);
    if (!end)
      end = last;

    if (start != value)
      fputc(',',out);
    outfields_value_print(out,&f,start,end-start);
    start = end+1;
  }

  fputc(']',out);
}

static void
outfields_value_print(FILE *out, const reliq_output_field *field, const char *value, const size_t valuel)
{
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
      fputs("null",out);
      break;
  }
}

static void
outfields_print_pre(struct outfield **fields, size_t *pos, const size_t size, const ushort lvl, uchar isarray, FILE *out)
{
  fputc(isarray ? '[' : '{',out);
  for (; *pos < size; (*pos)++) {
    struct outfield *field = fields[*pos];
    if (field->lvl < lvl)
        break;

    if (field->o && field->o->name.s) {
      fputc('"',out);
      fwrite(field->o->name.b,1,field->o->name.s,out);
      fputc('"',out);
      fputc(':',out);
    }

    if (field->code == 1 || field->code == 4) {
      if (field->f)
        fclose(field->f);
      field->f = NULL;

      outfields_value_print(out,field->o,field->v,field->s);
      if (field->v)
        free(field->v);
      field->s = 0;
    }
    if (field->code == 2 || field->code == 3) {
      (*pos)++;
      outfields_print_pre(fields,pos,size,lvl+1,(field->code == 3) ? 1 : 0,out);
      (*pos)--;
    }

    if (*pos+1 < size && fields[*pos+1]->lvl >= lvl)
      fputc(',',out);
  }
  fputc(isarray ? ']' : '}',out);
}

static void
outfields_print(flexarr *fields, FILE *out)
{
  if (!fields->size)
    return;
  size_t pos = 0;
  outfields_print_pre((struct outfield**)fields->v,&pos,fields->size,0,0,out);
}

static void
outfields_free(flexarr *outfields)
{
  struct outfield **outfieldsv = (struct outfield**)outfields->v;
  for (size_t i = 0; i < outfields->size; i++) {
    if (outfieldsv[i]->f)
      fclose(outfieldsv[i]->f);
    if (outfieldsv[i]->s)
      free(outfieldsv[i]->v);
    free(outfieldsv[i]);
  }
  flexarr_free(outfields);
}

// hnode codes
// 0 unnamed field
// 1 named field
// 2 start field block
// 3 start field block array
// 4 named field empty block
// 5 end field block

reliq_error *
nodes_output(const reliq *rq, flexarr *compressed_nodes, flexarr *ncollector
        #ifdef RELIQ_EDITING
        , flexarr *fcollector
        #endif
        )
{
  #ifdef RELIQ_EDITING
  struct fcollector_expr *fcols = (struct fcollector_expr*)fcollector->v;
  if (fcollector->size)
      fcollector_rearrange(fcollector);
  #endif
  if (!compressed_nodes->size || !ncollector->size)
    return NULL;
  reliq_error *err = NULL;
  reliq_cstr *ncol = (reliq_cstr*)ncollector->v;

  FILE *out = rq->output;
  FILE *fout = out;
  size_t j=0, //iterator of compressed_nodes
      ncurrent=0, //iterator of ncollector
      g=0; //iterator of u in ncollector
  #ifdef RELIQ_EDITING
  flexarr *outs = flexarr_init(sizeof(struct fcollector_out*),FCOLLECTOR_OUT_INC);
  size_t fcurrent=0;
  size_t fsize;
  char *ptr;
  #endif

  flexarr *outfields = flexarr_init(sizeof(struct outfield*),OUTFIELDS_INC);
  ushort fieldlvl = 0;
  FILE **oout = NULL; //outfields output
  enum outfieldCode prevcode = ofUnnamed;
  size_t prev_j = j;
  ushort field_ended = 0;

  for (;; j++) {
    #ifdef RELIQ_EDITING
    if (compressed_nodes->size && g == 0) {
      while (fcurrent < fcollector->size && fcols[fcurrent].start == ncurrent) {
        struct fcollector_out *ff,**ff_pre;
        ff_pre = flexarr_inc(outs);
        *ff_pre = malloc(sizeof(struct fcollector_out));
        ff = *ff_pre;
        ff->f = open_memstream(&ff->v,&ff->s);
        ff->current = fcurrent++;
        fout = ff->f;
      }

      if (j >= compressed_nodes->size)
        break;

      if (ncurrent < ncollector->size && ncol[ncurrent].b && ((reliq_expr*)ncol[ncurrent].b)->exprfl) {
        if (out != rq->output && out) {
          fclose(out);
          free(ptr);
        }
        out = open_memstream(&ptr,&fsize);
      }
    }
    #endif
    if (j >= compressed_nodes->size)
      break;

    FILE *rout = (out == rq->output) ? fout : out;
    if (rout == rq->output && oout)
      rout = *oout;

    reliq_compressed *x = &((reliq_compressed*)compressed_nodes->v)[j];
    if ((void*)x->hnode < (void*)10) {
      enum outfieldCode code = (enum outfieldCode)x->hnode;
      struct outfield *field,**field_pre;

      /*the if ends in hard to manage ways so these values are assigned before it ends
        and copied variables are used*/
      const enum outfieldCode prevcode_r = prevcode;
      prevcode = code;
      const size_t prev_j_r = prev_j;
      prev_j = j;

      switch (code) {
        case ofUnnamed:
          fputc('\n',rout);
          break;
        case ofBlock:
        case ofArray:
        case ofNoFieldsBlock:
        case ofNamed:
          field_pre = flexarr_inc(outfields);
          *field_pre = malloc(sizeof(struct outfield));
          field = *field_pre;
          field->s = 0;
          field->f = NULL;
          if (code == ofNamed || code == ofNoFieldsBlock) {
            field->f = open_memstream(&field->v,&field->s);
            oout = &field->f;
          }
          field->lvl = fieldlvl;
          field->code = (uchar)code;
          field->o = (reliq_output_field const*)x->parent;
          struct outfield **outfieldsv = (struct outfield**)outfields->v;
          if (outfields->size > 2 && field->o == outfieldsv[outfields->size-2]->o)
            field->o = NULL;
          fieldlvl++;
          break;
        case ofBlockEnd:
          if (fieldlvl)
            fieldlvl--;
          field_ended = 1;

          if ((prevcode_r == ofNoFieldsBlock || prevcode_r == ofArray || prevcode_r == ofBlock) && j-prev_j_r == ofNamed)
            goto NCOLLECTOR_END; //j-prev_j_r is 1 meaning that block was immedietly ended, and so it has to end

          if (g == 0) //the first node in ncol[ncurrent] was ending block, so the previous one did not free oout
            goto FIELD_ENDED_FREE_OOUT;
          break;
      }

      if (code != ofUnnamed && code != ofNamed && (prevcode_r != ofNamed || code != ofBlockEnd))
         continue;
    } else if (ncurrent < ncollector->size && ncol[ncurrent].b) {
      err = node_output(x->hnode,x->parent,((reliq_expr*)ncol[ncurrent].b)->nodef,
        ((reliq_expr*)ncol[ncurrent].b)->nodefl,rout,rq);
      if (err)
        goto END;
    }

    g++;
    if (ncurrent < ncollector->size && ncol[ncurrent].s == g) {
      NCOLLECTOR_END: ;
      #ifdef RELIQ_EDITING
      if (ncol[ncurrent].b && out != rq->output) {
        fclose(out);
        out = NULL;
        err = format_exec(ptr,fsize,(oout && fout == rq->output) ? *oout : fout,NULL,NULL,
          ((reliq_expr*)ncol[ncurrent].b)->exprf,
          ((reliq_expr*)ncol[ncurrent].b)->exprfl,rq);
        free(ptr);
        if (err)
          goto END;
        out = rq->output;
      }

      if ((err = fcollector_out_end(outs,ncurrent,fcols,rq,oout ? *oout : rq->output,&fout)))
        goto END;
      if (oout && fout == *oout)
        fout = rq->output;
      #endif

      g = 0;
      ncurrent++;

      if (field_ended) {
        FIELD_ENDED_FREE_OOUT: ;
        if (oout) {
          fclose(*oout);
          *oout = NULL;
          oout = NULL;
        }
        field_ended = 0;
      }
    }
  }

  outfields_print(outfields,rq->output);

  END: ;
  #ifdef RELIQ_EDITING
  struct fcollector_out **outsv = (struct fcollector_out**)outs->v;
  for (size_t i = 0; i < outs->size; i++) {
    fclose(outsv[i]->f);
    if (outsv[i]->s)
      free(outsv[i]->v);
    free(outsv[i]);
  }
  flexarr_free(outs);
  #endif

  outfields_free(outfields);

  return err;
}
