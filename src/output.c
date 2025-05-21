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
    SINK *out_field;

    const size_t ncolsl;
    const size_t fcolsl;
    size_t ncol_ptrl;

    size_t fcols_i;
    size_t ncols_i;
    size_t amount_i; //iterator of ncols[ncols_i].amount

    uint16_t field_lvl;
    uchar field_ended;
} nodes_output_state;

struct fcollector_out {
  SINK f;
  char *v;
  size_t s;
  size_t current;
};

static void
outfield_notempty(SINK *out, nodes_output_state *st)
{
  if (!out || st->out_field != out)
    return;
  flexarr *f = st->outfields;
  if (!f->size)
    return;
  struct outfield **fv = f->v;
  fv[f->size-1]->notempty = 1;
}

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
fcollector_out_end(nodes_output_state *st)
{
  /*
     if output for field is not NULL, it means that this ncollector
     is a child of it and its outputs, if there isn't one then
     fallback to the original output
  */
  SINK *out_default = st->out_field ? st->out_field : st->out_origin;

  flexarr *outs = st->fcol_outs; //outs: struct fcollector_out*
  const size_t ncurrent = st->ncols_i;
  const struct fcollector *fcols = st->fcols;
  const reliq *rq = st->rq;

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
      out_t =  out_default;
      st->out_fcol = NULL;
      outfield_notempty(out_t,st);
    } else {
      out_t = &((struct fcollector_out**)outs->v)[outs->size-2]->f;
      st->out_fcol = out_t;
    }

    sink_close(&fcol_out_last->f);
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
    sink_close(&outsv[i]->f);
    if (outsv[i]->s)
      free(outsv[i]->v);
    free(outsv[i]);
  }
  flexarr_free(outs);
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
    return st->out_field;
  return st->out_origin;
}

static void
field_ended_free(SINK **out_field, uchar *field_ended)
{
  if (*out_field) {
    sink_close(*out_field);
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
    st->out_fcol = &ff->f;
  }
}

static void
ncollector_new(nodes_output_state *st)
{
  if (st->ncols_i >= st->ncolsl || !st->ncols[st->ncols_i].e || !((reliq_expr*)st->ncols[st->ncols_i].e)->exprfl)
    return;
  if (st->out_ncol) {
    sink_size(st->out_ncol,0);
  } else {
    SINK t = sink_open(&st->ncol_ptr,&st->ncol_ptrl);
    st->out_ncol = memdup(&t,sizeof(SINK));
  }
}

static reliq_error *
ncollector_end(nodes_output_state *st)
{
  reliq_error *err = NULL;
  const struct ncollector *ncol = st->ncols+st->ncols_i;
  if (ncol->e && st->out_ncol) {
    sink_close(st->out_ncol);
    free(st->out_ncol);
    st->out_ncol = NULL;

    SINK *out_default = output_default(st);
    outfield_notempty(out_default,st);
    err = format_exec(st->ncol_ptr,st->ncol_ptrl,out_default,NULL,NULL,
      (ncol->e)->exprf,
      (ncol->e)->exprfl,st->rq);
    free(st->ncol_ptr);
    if (err)
      goto END;
  }

  if ((err = fcollector_out_end(st)))
    goto END;

  st->amount_i = 0;
  st->ncols_i++;

  if (st->field_ended)
    field_ended_free(&st->out_field,&st->field_ended);

  END: ;
  return err;
}

static struct outfield *
outfields_inc(enum outfieldCode code, uint16_t lvl, reliq_field const *fieldname, flexarr *outfields)
{
  struct outfield *field,
      **field_pre = flexarr_inc(outfields);
  field = *field_pre = malloc(sizeof(struct outfield));

  *field = (struct outfield){
    .lvl = lvl,
    .code = (uchar)code
  };

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
      field = outfields_inc(code,st->field_lvl,(reliq_field const*)compn->parent,st->outfields);

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
      outfield_notempty(out_default,st);

      const reliq_expr *e = (const reliq_expr*)ncol->e;
      const reliq_chnode *parent = (compn->parent == (uint32_t)-1) ? NULL : compn->parent+st->rq->nodes,
        *node = compn->hnode+st->rq->nodes;
      if ((err = format_exec(NULL,0, out_default,node, parent,
          e->nodef,e->nodefl,st->rq)))
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

  flexarr outfields = flexarr_init(sizeof(struct outfield*),OUTFIELDS_INC);
  flexarr fcol_outs = flexarr_init(sizeof(struct fcollector_out*),FCOLLECTOR_OUT_INC);

  nodes_output_state st = {
    .outfields = &outfields,
    .fcol_outs = &fcol_outs,

    .rq = rq,
    .out_origin = output,

    .fcols = (struct fcollector*)fcollector->v,
    .fcolsl = fcollector->size,
    .ncols = (struct ncollector*)ncollector->v,
    .ncolsl = ncollector->size,
  };

  reliq_error *err = nodes_output_r(compressed_nodes,&st);

  if (!err)
    outfields_print(rq,st.outfields,st.out_origin);

  fcollector_outs_free(st.fcol_outs);
  outfields_free(st.outfields);

  return err;
}
