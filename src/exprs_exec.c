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

#include "ext.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <assert.h>

#include "ctype.h"
#include "utils.h"
#include "npattern.h"
#include "format.h"

#define PASSED_INC (1<<8) //!! if increased causes huge allocation see val_mem1 file
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

typedef struct {
  const reliq *rq;
  const reliq_hnode *parent;
  SINK *output;
  flexarr *ncollector; //reliq_cstr
  #ifdef RELIQ_EDITING
  flexarr *fcollector; //struct fcollector_expr
  #endif
  flexarr **out; //reliq_compressed
  uchar isempty;
  uchar noncol;
  uchar found;
} exec_state;

reliq_error *reliq_fmatch(const char *data, const size_t size, SINK *output, const reliq_npattern *nodep,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl);

static reliq_error *exec_chain(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st); //source: reliq_compressed, dest: reliq_compressed
reliq_error *reliq_exec_r(reliq *rq, const reliq_hnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_expr *expr);

static inline void
add_compressed_blank(flexarr *dest, const enum outfieldCode val1, const void *val2) //dest: reliq_compressed
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = (reliq_hnode *const)val1;
  x->parent = (void *const)val2;
}

reliq_error *
expr_check_chain(const reliq_expr *expr, const uchar noaccesshooks)
{
  if (!EXPR_TYPE_IS(expr->flags,EXPR_BLOCK))
    return NULL;
  flexarr *v = expr->e;
  if (!v->size)
    return NULL;
  if (v->size > 1)
    goto ERR;

  reliq_expr *e = &((reliq_expr*)v->v)[0];
  if (!EXPR_TYPE_IS(e->flags,EXPR_CHAIN))
    goto ERR;

  flexarr *chain = e->e; //reliq_expr
  reliq_expr *chainv = (reliq_expr*)chain->v;

  const size_t size = chain->size;
  for (size_t i = 0; i < size; i++) {
    if (EXPR_IS_TABLE(chainv[i].flags))
      goto ERR;
    if (noaccesshooks && (((reliq_npattern*)chainv[i].e)->flags&N_MATCHED_TYPE) > 1)
      return script_err("illegal use of access hooks in fast mode",((reliq_npattern*)chainv[i].e)->flags&N_MATCHED_TYPE);
  }

  return NULL;
  ERR: ;
  return script_err("expression is not a chain");
}

/*static reliq_error *
ncollector_check(flexarr *ncollector, size_t correctsize) //ncollector: reliq_cstr
{
  size_t ncollector_sum = 0;
  reliq_cstr *pcol = (reliq_cstr*)ncollector->v;
  for (size_t j = 0; j < ncollector->size; j++)
    ncollector_sum += pcol[j].s;
  if (ncollector_sum != correctsize)
    return script_err("ncollector does not match count of found tags, %lu != %lu",ncollector_sum,correctsize);
  return NULL;
}*/

static void
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar flags, uchar useformat, uchar isempty, uchar noncollector) //ncollector: reliq_cstr, dest: reliq_compressed, source: reliq_compressed
{
  if (!source->size && !isempty)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (noncollector || (useformat && !lastnode))
    goto END;
  if (EXPR_IS_TABLE(flags) && !isempty) {
    if (startn != lastn) { //truncate previously added, now useless ncollector
      const size_t size = ncollector->size;
      for (size_t i = lastn; i < size; i++)
        ((reliq_cstr*)ncollector->v)[startn+i] = ((reliq_cstr*)ncollector->v)[i];
      ncollector->size -= lastn-startn;
    }
  } else {
    ncollector->size = startn;
    *(reliq_cstr*)flexarr_inc(ncollector) = (reliq_cstr){(char const*)lastnode,dest->size-prevsize};
  }
  END: ;
  source->size = 0;
}

#ifdef RELIQ_EDITING
static void
fcollector_add(const size_t lastn, const uchar isnodef, const reliq_expr *expr, flexarr *ncollector, flexarr *fcollector) //ncollector: reliq_cstr, fcollector: struct fcollector_expr
{
  struct fcollector_expr *fcols = (struct fcollector_expr*)fcollector->v;
  for (size_t i = fcollector->size; i > 0; i--) {
    if (fcols[i-1].start < lastn)
      break;
    fcols[i-1].lvl++;
  }
  *(struct fcollector_expr*)flexarr_inc(fcollector) = (struct fcollector_expr){expr,lastn,ncollector->size-1,0,isnodef};
}
#endif

/*static reliq_error *
exec_chainlink(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *expr, const flexarr *source, flexarr *dest, flexarr **out, uchar noncol, uchar isempty, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    )   //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  if (current->e) {
    lastnode = current;
    reliq_npattern *nodep = (reliq_npattern*)current->e;
    if (outnamed)
      add_compressed_blank(buf[1],ofNamed,outnamed);

    if (!isempty)
      node_exec(rq,parent,nodep,src,buf[1]);

    if (outnamed)
      add_compressed_blank(buf[1],ofBlockEnd,NULL);

    if (!noncol && outprotected && buf[1]->size == 0) {
      add_compressed_blank(buf[1],ofUnnamed,NULL);
      ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,current->flags,0,0,noncol);
      break;
    }
  }
}*/

static reliq_error *
exec_block_conditional(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed
{
  flexarr *expr_e = (flexarr*)expr->e;
  const reliq_expr *exprs = expr_e->v;
  const size_t exprsl = expr_e->size;
  reliq_error *err = NULL;

  if (expr->outfield.name.b)
    add_compressed_blank(dest,expr->childfields ? ofBlock : ofNoFieldsBlock,&expr->outfield);

  flexarr *desttemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);

  size_t startn = st->ncollector->size;
  size_t lastn = st->ncollector->size;
  #ifdef RELIQ_EDITING
  size_t prevfcolsize = st->fcollector->size;
  #endif

  reliq_expr const *lastnode = NULL;

  for (size_t i = 0; i < exprsl; i++) {
    reliq_expr const *current = &exprs[i];
    assert(EXPR_TYPE_IS(current->flags,EXPR_CHAIN));

    if ((err = exec_chain(current,source,desttemp,st)))
      goto END;

    #ifdef RELIQ_EDITING
    if (!st->found)
      st->fcollector->size = prevfcolsize;
    #endif

    if (current->flags&(EXPR_AND|EXPR_AND_BLANK) && !st->found)
      break;

    if ((current->flags&EXPR_AND_BLANK && st->found) || (current->flags&EXPR_OR && !st->found)) {
      desttemp->size = 0;
      st->ncollector->size = lastn;
      #ifdef RELIQ_EDITING
      st->fcollector->size = prevfcolsize;
      #endif
      continue;
    }

    if (current->flags&EXPR_OR && st->found)
      break;
  }

  ncollector_add(st->ncollector,dest,desttemp,startn,lastn,lastnode,expr->flags,1,st->isempty,st->noncol);

  END: ;
  if (expr->outfield.name.b)
    add_compressed_blank(dest,ofBlockEnd,NULL);

  flexarr_free(desttemp);
  return err;
}

static reliq_error *
exec_block(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed
{
  flexarr *expr_e = (flexarr*)expr->e;
  const reliq_expr *exprs = expr_e->v;
  const size_t exprsl = expr_e->size;
  reliq_error *err = NULL;

  flexarr *desttemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  flexarr *destfinal = dest ? dest : flexarr_init(sizeof(reliq_compressed),PASSED_INC);

  size_t startn = st->ncollector->size;
  size_t lastn;

  reliq_expr const *lastnode = NULL;

  for (size_t i = 0; i < exprsl; i++) {
    reliq_expr const *current = &exprs[i];
    lastn = st->ncollector->size;

    if (EXPR_TYPE_IS(current->flags,EXPR_CHAIN)) {
      if ((err = exec_chain(current,source,desttemp,st)))
        goto END;
    } else {
      assert(EXPR_TYPE_IS(current->flags,EXPR_BLOCK_CONDITION));

      if ((err = exec_block_conditional(current,source,desttemp,st)))
        goto END;
    }

    ncollector_add(st->ncollector,destfinal,desttemp,startn,lastn,lastnode,current->flags,1,st->isempty,st->noncol);
  }

  if (!dest) {
    if (st->output) {
      if ((err = nodes_output(st->rq,st->output,destfinal,st->ncollector
          #ifdef RELIQ_EDITING
          ,st->fcollector
          #endif
          )))
        goto END;
      flexarr_free(destfinal);
    } else
      *st->out = destfinal;
  }

  END: ;
  flexarr_free(desttemp);
  if (err)
    flexarr_free(destfinal);
  return err;
}

static reliq_error *
exec_table(const reliq_expr *expr, const reliq_output_field *named, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  reliq_error *err = NULL;

  if (EXPR_TYPE_IS(expr->flags,EXPR_SINGULAR)) {
    if (named)
      add_compressed_blank(dest,expr->childfields ? ofArray : ofNoFieldsBlock,named);
    if (!source->size)
      goto END;

    flexarr *in = flexarr_init(sizeof(reliq_compressed),1);
    reliq_compressed *inv = (reliq_compressed*)flexarr_inc(in);
    const reliq_compressed *sourcev = (reliq_compressed*)source->v;

    const size_t size = source->size;
    for (size_t i = 0; i < size; i++) {
      *inv = sourcev[i];
      if ((void*)inv->hnode < (void*)10)
        continue;
      #ifdef RELIQ_EDITING
      size_t lastn = st->ncollector->size;
      #endif
      if (named && expr->childfields)
        add_compressed_blank(dest,ofBlock,NULL);
      if ((err = exec_block(expr,in,dest,st)))
        break;
      if (named && expr->childfields)
        add_compressed_blank(dest,ofBlockEnd,NULL);
      #ifdef RELIQ_EDITING
      if (!st->noncol && st->ncollector->size-lastn && expr->nodefl)
        fcollector_add(lastn,ofNamed,expr,st->ncollector,st->fcollector);
      #endif
    }

    flexarr_free(in);
    goto END;
  }

  if (named)
    add_compressed_blank(dest,expr->childfields ? ofBlock : ofNoFieldsBlock,named);

  err = exec_block(expr,source,dest,st);

  END: ;
  if (named)
    add_compressed_blank(dest,ofBlockEnd,NULL);
  return err;
}

static reliq_error *
exec_chain(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed
{
  flexarr *expr_e = (flexarr*)expr->e;
  const reliq_expr *exprs = expr_e->v;
  const size_t exprsl = expr_e->size;
  if (exprsl == 0)
    return NULL;
  reliq_error *err = NULL;
  flexarr *srctemp = NULL;
  uchar src_alloc = (exprsl > 1 || !source);
  if (src_alloc)
    srctemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  flexarr *desttemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);

  size_t startn = st->ncollector->size;
  size_t lastn = startn;
  reliq_expr const *lastnode = NULL;
  uchar fieldprotected=0,copied_state=0;
  reliq_output_field const *fieldnamed = NULL;
  if (expr->outfield.isset) {
    if (expr->outfield.name.b) {
      fieldnamed = &expr->outfield;
    } else
      fieldprotected = 1;
  }

  const flexarr *src = source ? source : srctemp;
  st->found = 1;

  for (size_t i = 0; i < exprsl; i++) {
    reliq_expr const *current = &exprs[i];

    if (EXPR_IS_TABLE(current->flags)) {
      lastn = st->ncollector->size;
      size_t prevsize = desttemp->size;
      uchar prev_noncol = st->noncol;

      if (i != exprsl-1)
        st->noncol |= 1;

      if ((err = exec_table(current,fieldnamed,src,desttemp,st)))
        goto END;

      if (desttemp->size-prevsize <= 2
        && (desttemp->size == 0 || (void*)((reliq_compressed*)desttemp->v)[0].hnode < (void*)10)) {
        st->found = 0;

        if (!st->noncol && fieldnamed) {
          ncollector_add(st->ncollector,dest,desttemp,startn,lastn,NULL,current->flags,0,1,0);
          break;
        }
      }
      st->noncol = prev_noncol;

      #ifdef RELIQ_EDITING
      if (!st->noncol && (EXPR_TYPE_IS(current->flags,EXPR_BLOCK) ||
        EXPR_TYPE_IS(current->flags,EXPR_SINGULAR) ||
        EXPR_TYPE_IS(current->flags,EXPR_BLOCK_CONDITION)) && current->exprfl)
        fcollector_add(lastn,0,&exprs[i],st->ncollector,st->fcollector);
      #endif
    } else if (current->e) {
      lastnode = current;
      reliq_npattern *nodep = (reliq_npattern*)current->e;
      if (fieldnamed)
        add_compressed_blank(desttemp,ofNamed,fieldnamed);

      if (!st->isempty) {
        size_t prevsize = desttemp->size;
        node_exec(st->rq,st->parent,nodep,src,desttemp);
        if (desttemp->size-prevsize == 0)
          st->found = 0;
      }

      if (fieldnamed)
        add_compressed_blank(desttemp,ofBlockEnd,NULL);

      if (!st->noncol && fieldprotected && desttemp->size == 0) {
        add_compressed_blank(desttemp,ofUnnamed,NULL);
        ncollector_add(st->ncollector,dest,desttemp,startn,lastn,NULL,current->flags,0,0,st->noncol);
        break;
      }
    }

    if (i == exprsl-1) {
      ncollector_add(st->ncollector,dest,desttemp,startn,lastn,lastnode,current->flags,1,st->isempty,st->noncol);
      continue;
    }
    if (!desttemp->size) {
      if (!copied_state) {
        copied_state = 1;
        st = memcpy(alloca(sizeof(exec_state)),st,sizeof(exec_state));
      }
      st->isempty = 1;
      if (!expr->childfields)
        break;
    }

    srctemp->size = 0;
    flexarr *tmp = srctemp;
    srctemp = desttemp;
    desttemp = tmp;
    src = srctemp;
  }

  END: ;
  if (src_alloc)
    flexarr_free(srctemp);
  flexarr_free(desttemp);
  return err;
}

reliq_error *
reliq_exec_r(reliq *rq, const reliq_hnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_expr *expr)
{
  if (!expr)
    return NULL;
  flexarr *compressed=NULL;
  reliq_error *err;

  flexarr *ncollector = flexarr_init(sizeof(reliq_cstr),NCOLLECTOR_INC);
  #ifdef RELIQ_EDITING
  flexarr *fcollector = flexarr_init(sizeof(struct fcollector_expr),FCOLLECTOR_INC);
  #endif

  exec_state state = {
    .rq = rq,
    .parent = parent,
    .output = output,
    .ncollector = ncollector,
    #ifdef RELIQ_EDITING
    .fcollector = fcollector,
    #endif
    .out = &compressed,
  };

  err = exec_block(expr,NULL,NULL,&state);

  if (compressed && !err && !output) {
    *outnodesl = compressed->size;
    if (outnodes)
      flexarr_conv(compressed,(void**)outnodes,outnodesl);
    else
      flexarr_free(compressed);
  }

  flexarr_free(ncollector);
  #ifdef RELIQ_EDITING
  flexarr_free(fcollector);
  #endif
  return err;
}

reliq_error *
reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_expr *expr)
{
  return reliq_exec_r(rq,NULL,NULL,nodes,nodesl,expr);
}

reliq_error *
reliq_exec_file(reliq *rq, FILE *output, const reliq_expr *expr)
{
  if (!expr)
    return NULL;
  SINK *out = sink_from_file(output);
  reliq_error *err = reliq_exec_r(rq,NULL,out,NULL,NULL,expr);
  sink_close(out);
  return err;
}

reliq_error *
reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_expr *expr)
{
  if (!expr)
    return NULL;
  SINK *output = sink_open(str,strl);
  reliq_error *err = reliq_exec_r(rq,NULL,output,NULL,NULL,expr);
  sink_close(output);
  return err;
}

static reliq_error *
reliq_fexec_sink(char *data, size_t size, SINK *output, const reliq_expr *expr, int (*freedata)(void *addr, size_t len))
{
  reliq_error *err = NULL;
  uchar was_unallocated = 0;
  SINK *destination = output;
  if (!expr || !EXPR_TYPE_IS(expr->flags,EXPR_BLOCK))
    goto END;
  if ((err = expr_check_chain(expr,1)))
    goto END;

  char *ptr=data,*nptr;
  size_t fsize;

  flexarr *chain = ((reliq_expr*)((flexarr*)expr->e)->v)[0].e;
  reliq_expr *chainv = (reliq_expr*)chain->v;
  SINK *out;

  const size_t chainsize = chain->size;
  for (size_t i = 0; i < chainsize; i++) {
    out = (i == chain->size-1) ? destination : sink_open(&nptr,&fsize);

    err = reliq_fmatch(ptr,size,out,(reliq_npattern*)chainv[i].e,
      chainv[i].nodef,chainv[i].nodefl);

    sink_close(out);

    if (i == 0) {
      if (freedata)
        (*freedata)(data,size);
      was_unallocated = 1;
    } else
      free(ptr);

    if (err)
      return err;

    ptr = nptr;
    size = fsize;
  }

  END: ;
  if (!was_unallocated) {
    sink_close(destination);
    if (freedata)
      (*freedata)(data,size);
  }
  return err;
}

reliq_error *
reliq_fexec_file(char *data, size_t size, FILE *output, const reliq_expr *expr, int (*freedata)(void *addr, size_t len))
{
  return reliq_fexec_sink(data,size,sink_from_file(output),expr,freedata);
}

reliq_error *
reliq_fexec_str(char *data, const size_t size, char **str, size_t *strl, const reliq_expr *expr, int (*freedata)(void *data, size_t len))
{
  if (!expr)
    return NULL;
  SINK *output = sink_open(str,strl);
  return reliq_fexec_sink(data,size,output,expr,freedata);
}
