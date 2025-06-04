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
#include "output.h"
#include "format.h"
#include "exprs.h"
#include "node_exec.h"

#define PASSED_INC (1<<8) //!! if increased causes huge allocation
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

typedef struct {
  const reliq *rq;
  SINK *output;
  flexarr *ncollector; //struct ncollector
  flexarr *fcollector; //struct fcollector
  flexarr *out; //reliq_compressed
  uchar isempty : 1;
  uchar noncol : 1; //no ncollector
  uchar something_found : 1;
  uchar something_failed : 1;
} exec_state;

static reliq_error *exec_chain(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st); //source: reliq_compressed, dest: reliq_compressed

static inline void
add_compressed_blank(flexarr *dest, const enum outfieldCode val1, const void *val2) //dest: reliq_compressed
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = (uint32_t)val1+OUTFIELDCODE_OFFSET;
  x->parent = (uintptr_t)val2;
}

reliq_error *
expr_check_chain(const reliq_expr *expr)
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
  }

  return NULL;
  ERR: ;
  return script_err("expression is not a chain");
}

/*static reliq_error *
ncollector_check(flexarr *ncollector, size_t correctsize) //ncollector: struct ncollector
{
  size_t ncollector_sum = 0;
  struct ncollector *pcol = (struct ncollector*)ncollector->v;
  for (size_t j = 0; j < ncollector->size; j++)
    ncollector_sum += pcol[j].s;
  if (ncollector_sum != correctsize)
    return script_err("ncollector does not match count of found tags, %lu != %lu",ncollector_sum,correctsize);
  return NULL;
}*/

static inline void
ncollector_add(flexarr *ncollector, const size_t newsize, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar flags, uchar useformat, uchar isempty, uchar noncollector) //ncollector: struct ncollector, dest: reliq_compressed, source: reliq_compressed
{
  if ((!newsize && !isempty) || noncollector || (useformat && !lastnode))
    return;

  if (EXPR_IS_TABLE(flags) && !isempty) {
    if (startn == lastn)
      return;
    //truncate previously added, now useless ncollector
    const size_t size = ncollector->size;
    if (lastn < size) {
      struct ncollector *nv = ncollector->v;
      memmove(nv+startn,nv+lastn,size-lastn);
    }
    ncollector->size -= lastn-startn;
  } else {
    ncollector->size = startn;
    *(struct ncollector*)flexarr_inc(ncollector) = (struct ncollector){lastnode,newsize};
  }
}

static void
ncollector_add_copy(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar flags, uchar useformat, uchar isempty, uchar noncollector) //ncollector: struct ncollector, dest: reliq_compressed, source: reliq_compressed
{
  if (!source->size && !isempty)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  ncollector_add(ncollector,dest->size-prevsize,startn,lastn,lastnode,flags,useformat,isempty,noncollector);
  source->size = 0;
}

static void
fcollector_add(const size_t lastn, const uchar isnodef, const reliq_expr *expr, flexarr *ncollector, flexarr *fcollector) //ncollector: struct ncollector, fcollector: struct fcollector
{
  struct fcollector *fcols = (struct fcollector*)fcollector->v;
  for (size_t i = fcollector->size; i > 0; i--) {
    if (fcols[i-1].start < lastn)
      break;
    fcols[i-1].lvl++;
  }
  *(struct fcollector*)flexarr_inc(fcollector) = (struct fcollector){expr,lastn,ncollector->size-1,0,isnodef};
}

static reliq_error *
exec_block_conditional(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed
{
  flexarr *expr_e = (flexarr*)expr->e;
  const reliq_expr *exprs = expr_e->v;
  const size_t exprsl = expr_e->size;
  reliq_error *err = NULL;

  if (expr->outfield.isset) {
    if (expr->outfield.name.b) {
      add_compressed_blank(dest,expr->childfields ? ofBlock : ofNoFieldsBlock,&expr->outfield);
    } else
      add_compressed_blank(dest,ofUnnamed,&expr->outfield);
  }

  size_t startn = st->ncollector->size;
  size_t lastn = st->ncollector->size;
  size_t prevfcolsize = st->fcollector->size;
  size_t firstsize = dest->size;

  reliq_expr const *lastnode = NULL;

  for (size_t i = 0; i < exprsl; i++) {
    reliq_expr const *current = &exprs[i];
    assert(EXPR_TYPE_IS(current->flags,EXPR_CHAIN));

    st->something_found = 0;
    st->something_failed = 0;
    if ((err = exec_chain(current,source,dest,st)))
      goto END;

    uchar success = st->something_found;
    if (current->flags&EXPR_ALL)
        success = (success && !st->something_failed);

    if (!success)
      st->fcollector->size = prevfcolsize;

    // if first part is replaced by (current->flags&EXPR_AND_BLOCK && success) then "{ p, nothing } ^&& li" will print failed output of first expression,
    // i have not decided which behaviour is better
    if (current->flags&EXPR_AND_BLANK || (current->flags&EXPR_OR && !success)) {
      dest->size = firstsize;
      st->ncollector->size = lastn;
      st->fcollector->size = prevfcolsize;
    }

    if (current->e)
      lastnode = current;

    if (current->flags&(EXPR_AND|EXPR_AND_BLANK) && !success)
      break;

    if (current->flags&EXPR_OR && success)
      break;
  }

  uchar isempty = st->isempty;
  if (!isempty && dest->size == firstsize && expr->outfield.isset && expr->outfield.name.b)
    isempty = 1;

  ncollector_add(st->ncollector,dest->size-firstsize,startn,lastn,lastnode,expr->flags,1,isempty,st->noncol);

  END: ;
  if ((expr->outfield.isset))
    add_compressed_blank(dest,ofBlockEnd,NULL);

  return err;
}

static reliq_error *
exec_block(const reliq_expr *expr, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed
{
  flexarr *expr_e = (flexarr*)expr->e;
  if (!expr_e)
    return NULL;
  const reliq_expr *exprs = expr_e->v;
  const size_t exprsl = expr_e->size;
  reliq_error *err = NULL;

  flexarr *destfinal = dest;
  flexarr f_dest;
  if (!dest) {
    f_dest = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
    destfinal = &f_dest;
  }

  size_t startn = st->ncollector->size;
  size_t lastn;

  for (size_t i = 0; i < exprsl; i++) {
    reliq_expr const *current = &exprs[i];
    lastn = st->ncollector->size;
    size_t prevsize = destfinal->size;

    if (EXPR_TYPE_IS(current->flags,EXPR_CHAIN)) {
      if ((err = exec_chain(current,source,destfinal,st)))
        goto END;
    } else {
      assert(EXPR_TYPE_IS(current->flags,EXPR_BLOCK_CONDITION));

      if ((err = exec_block_conditional(current,source,destfinal,st)))
        goto END;
    }

    ncollector_add(st->ncollector,destfinal->size-prevsize,startn,lastn,NULL,current->flags,1,st->isempty,st->noncol);
  }

  if (!dest) {
    if (st->output) {
      if ((err = nodes_output(st->rq,st->output,destfinal,st->ncollector,st->fcollector)))
        goto END;
      flexarr_free(destfinal);
    } else {
      *st->out = *destfinal;
    }
  }

  END: ;
  if (err)
    flexarr_free(destfinal);
  return err;
}

static reliq_error *
exec_singular(const reliq_expr *expr, const reliq_field *named, const flexarr *source, flexarr *dest, exec_state *st)
{
  reliq_error *err = NULL;
  if (!source->size)
    return err;

  //function accepts flexarr type, but it doesn't grow
  flexarr in = flexarr_init(sizeof(reliq_compressed),1);
  in.size = 1;
  reliq_compressed inv;
  in.v = &inv;

  const reliq_compressed *sourcev = (reliq_compressed*)source->v;

  const size_t size = source->size;
  for (size_t i = 0; i < size; i++) {
    inv = sourcev[i];
    if (OUTFIELDCODE(inv.hnode))
      continue;
    size_t lastn = st->ncollector->size;
    if (named && expr->childfields)
      add_compressed_blank(dest,ofBlock,NULL);
    if ((err = exec_block(expr,&in,dest,st)))
      break;
    if (named && expr->childfields)
      add_compressed_blank(dest,ofBlockEnd,NULL);
    if (!st->noncol && st->ncollector->size-lastn && expr->nodefl)
      fcollector_add(lastn,1,expr,st->ncollector,st->fcollector);
  }

  return err;
}

static reliq_error *
exec_table(const reliq_expr *expr, const reliq_field *named, const flexarr *source, flexarr *dest, exec_state *st) //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: struct ncollector
{
  reliq_error *err = NULL;

  if (EXPR_TYPE_IS(expr->flags,EXPR_SINGULAR)) {
    if (named) {
      add_compressed_blank(dest,expr->childfields ? ofArray : ofNoFieldsBlock,named);
    } else if (expr->childfields)
      return script_err("output field: array with child fields is not assigned to any name");

    err = exec_singular(expr,named,source,dest,st);
  } else {
    if (named)
      add_compressed_blank(dest,expr->childfields ? ofBlock : ofNoFieldsBlock,named);

    err = exec_block(expr,source,dest,st);
  }

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
  flexarr srctemp;
  uchar src_alloc = (exprsl > 1 || !source);
  if (src_alloc)
    srctemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  flexarr desttemp = flexarr_init(sizeof(reliq_compressed),PASSED_INC);

  size_t startn = st->ncollector->size;
  size_t lastn = startn;
  reliq_expr const *lastnode = NULL;
  uchar fieldprotected=0,copied_state=0;
  reliq_field const *fieldnamed = NULL;
  if (expr->outfield.isset) {
    if (expr->outfield.name.b) {
      fieldnamed = &expr->outfield;
    } else
      fieldprotected = 1;
  }

  const flexarr *src = source ? source : &srctemp;
  uchar something_failed = 0,
        something_found = 0;

  for (size_t i = 0; i < exprsl; i++) {
    something_failed = 0;
    something_found = 0;
    reliq_expr const *current = &exprs[i];

    if (EXPR_IS_TABLE(current->flags)) {
      lastn = st->ncollector->size;
      size_t prevsize = desttemp.size;
      uchar prev_noncol = st->noncol;

      if (i != exprsl-1)
        st->noncol |= 1;

      if ((err = exec_table(current,fieldnamed,src,&desttemp,st)))
        goto END;

      if (desttemp.size-prevsize <= 2
        && (desttemp.size == 0 || OUTFIELDCODE(((reliq_compressed*)desttemp.v)[0].hnode))) {
        something_failed = 1;

        if (!st->noncol && fieldnamed) {
          ncollector_add_copy(st->ncollector,dest,&desttemp,startn,lastn,NULL,current->flags,0,1,0);
          break;
        }
      } else
        something_found = 1;
      st->noncol = prev_noncol;

      if (!st->noncol && (EXPR_TYPE_IS(current->flags,EXPR_BLOCK) ||
        EXPR_TYPE_IS(current->flags,EXPR_SINGULAR) ||
        EXPR_TYPE_IS(current->flags,EXPR_BLOCK_CONDITION)) && current->exprfl)
        fcollector_add(lastn,0,&exprs[i],st->ncollector,st->fcollector);
    } else if (current->e) {
      lastnode = current;
      reliq_npattern *nodep = (reliq_npattern*)current->e;
      if (fieldnamed)
        add_compressed_blank(&desttemp,ofNamed,fieldnamed);

      if (!st->isempty) {
        size_t prevsize = desttemp.size;
        node_exec(st->rq,nodep,src,&desttemp);
        if (desttemp.size-prevsize == 0) {
          something_failed = 1;
        } else
          something_found = 1;
      }

      if (fieldnamed)
        add_compressed_blank(&desttemp,ofBlockEnd,NULL);

      if (!st->noncol && fieldprotected && desttemp.size == 0) {
        add_compressed_blank(&desttemp,ofUnnamed,NULL);
        ncollector_add_copy(st->ncollector,dest,&desttemp,startn,lastn,NULL,current->flags,0,0,st->noncol);
        break;
      }
    }

    if (i == exprsl-1) {
      ncollector_add_copy(st->ncollector,dest,&desttemp,startn,lastn,lastnode,current->flags,1,st->isempty,st->noncol);
      continue;
    }
    if (!desttemp.size) {
      if (!copied_state) {
        copied_state = 1;
        st = memcpy(alloca(sizeof(exec_state)),st,sizeof(exec_state));
      }
      st->isempty = 1;
      if (!expr->childfields)
        break;
    }

    srctemp.size = 0;
    flexarr tmp = srctemp;
    srctemp = desttemp;
    desttemp = tmp;
    src = &srctemp;
  }

  END: ;
  st->something_failed |= something_failed;
  st->something_found |= something_found;

  if (src_alloc)
    flexarr_free(&srctemp);
  flexarr_free(&desttemp);
  return err;
}

reliq_error *
reliq_exec_r(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, SINK *output, reliq_compressed **outnodes, size_t *outnodesl)
{
  if (!expr)
    return NULL;

  reliq_error *err;
  flexarr compressed;
  flexarr ncollector = flexarr_init(sizeof(struct ncollector),NCOLLECTOR_INC);
  flexarr fcollector = flexarr_init(sizeof(struct fcollector),FCOLLECTOR_INC);

  exec_state state = {
    .rq = rq,
    .output = output,
    .ncollector = &ncollector,
    .fcollector = &fcollector,
    .out = &compressed,
  };

  flexarr *src = NULL;
  flexarr f_src;
  if (inputl) {
    f_src = flexarr_init(sizeof(reliq_compressed),1);
    f_src.v = (void*)input;
    f_src.size = inputl;
    src = &f_src;
  }

  err = exec_block(expr,src,NULL,&state);

  if (!err && outnodesl && compressed.size) {
    *outnodesl = compressed.size;
    if (outnodes) {
      flexarr_conv(&compressed,(void**)outnodes,outnodesl);
    } else
      flexarr_free(&compressed);
  }

  flexarr_free(&ncollector);
  flexarr_free(&fcollector);
  return err;
}

#ifdef SCHEME_DEBUG

static void
scheme_print_tabs(const uint16_t lvl)
{
  for (size_t i = 0; i < lvl; i++)
    fputs("  ",stderr);
}

void pretty_print_str(const char *src, const size_t size);

static void
scheme_print_type_arg(const struct reliq_field_type_arg *arg)
{
  if (arg->type == RELIQ_FIELD_TYPE_ARG_STR) {
    pretty_print_str(arg->v.s.b,arg->v.s.s);
  } else {
    fputs("\033[31m",stderr);
    if (arg->type == RELIQ_FIELD_TYPE_ARG_UNSIGNED) {
      fprintf(stderr,"%lu",arg->v.u);
    } else if (arg->type == RELIQ_FIELD_TYPE_ARG_SIGNED) {
      fprintf(stderr,"%ld",arg->v.i);
    } else if (arg->type == RELIQ_FIELD_TYPE_ARG_FLOATING) {
      fprintf(stderr,"%g",arg->v.d);
    } else
      assert(0);
    fputs("\033[0m",stderr);
  }
}

static void
scheme_print_type_args(const struct reliq_field_type_arg *args, const size_t argsl)
{
  if (!argsl)
    return;

  fputc('(',stderr);
  for (size_t i = 0; i < argsl; i++) {
    if (i != 0)
      fputc(',',stderr);

    scheme_print_type_arg(args+i);
  }
  fputc(')',stderr);
}

static void
scheme_print_types(const reliq_field_type *types, const size_t typesl, uchar isarray)
{
  for (size_t i = 0; i < typesl; i++) {
    const struct reliq_field_type *type = types+i;

    if (type->name.s == 1 && type->name.b[0] == 'a') {
      fputc('[',stderr);
      scheme_print_types(type->subtypes,type->subtypesl,1);
      fputc(']',stderr);
    } else {
      if (i == 0 && !isarray)
        fputc('.',stderr);
      fprintf(stderr,"\033[32m%.*s\033[0m",(int)type->name.s,type->name.b);
    }
    scheme_print_type_args(type->args,type->argsl);

    if (i != typesl-1)
      fputc('|',stderr);
  }
}

static size_t
scheme_print_r(const reliq_scheme_t *scheme, const size_t index, uint16_t minlvl)
{
  const size_t fieldsl = scheme->fieldsl;
  const struct reliq_scheme_field *fields = scheme->fields;

  for (size_t i = index; i < fieldsl; i++) {
    const struct reliq_scheme_field *f = fields+i;
    if (f->lvl < minlvl)
      return i;
    const reliq_field *field = f->field;
    const uint8_t type = f->type;

    scheme_print_tabs(f->lvl);
    fprintf(stderr,"\033[34m%.*s\033[0m",(int)field->name.s,field->name.b);

    if (type == RELIQ_SCHEME_FIELD_TYPE_NORMAL)
      scheme_print_types(field->types,field->typesl,0);

    if (field->annotation.s)
      fprintf(stderr," \033[32m%.*s\033[0m",(int)field->annotation.s,field->annotation.b);

    if (type != RELIQ_SCHEME_FIELD_TYPE_NORMAL) {
      if (type == RELIQ_SCHEME_FIELD_TYPE_ARRAY) {
        fputs(" \033[31;1m[\033[0m\n",stderr);
      } else
        fputs(" \033[32;1m{\033[0m\n",stderr);

      i = scheme_print_r(scheme,i+1,f->lvl+1)-1;

      scheme_print_tabs(f->lvl);
      if (type == RELIQ_SCHEME_FIELD_TYPE_ARRAY) {
        fputs("\033[31;1m]\033[0m",stderr);
      } else
        fputs("\033[32;1m}\033[0m",stderr);
    }

    fputc('\n',stderr);
  }
  return fieldsl;
}

static void
scheme_print(const reliq_scheme_t *scheme)
{
  RELIQ_DEBUG_SECTION_HEADER("SCHEME");

  if (scheme->leaking)
    fprintf(stderr,"\033[31;1m------ LEAKING ------\033[0m\n");
  if (scheme->repeating)
    fprintf(stderr,"\033[31;1m------ REPEATING ------\033[0m\n");

  scheme_print_r(scheme,0,0);
  fputc('\n',stderr);
}

#endif //SCHEME_DEBUG

reliq_error *
reliq_exec(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, reliq_compressed **nodes, size_t *nodesl)
{
  return reliq_exec_r(rq,input,inputl,expr,NULL,nodes,nodesl);
}

reliq_error *
reliq_exec_file(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, FILE *output)
{
  if (!expr)
    return NULL;

  #ifdef SCHEME_DEBUG

  reliq_scheme_t sch = reliq_scheme(expr);
  scheme_print(&sch);
  reliq_scheme_free(&sch);

  #endif //SCHEME_DEBUG

  SINK out = sink_from_file(output);
  reliq_error *err = reliq_exec_r(rq,input,inputl,expr,&out,NULL,NULL);
  sink_close(&out);
  return err;
}

reliq_error *
reliq_exec_str(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, char **str, size_t *strl)
{
  *str = NULL;
  *strl = 0;
  if (!expr)
    return NULL;
  SINK output = sink_open(str,strl);
  reliq_error *err = reliq_exec_r(rq,input,inputl,expr,&output,NULL,NULL);
  sink_close(&output);
  return err;
}
