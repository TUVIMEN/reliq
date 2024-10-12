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

#include "ctype.h"
#include "utils.h"
#include "npattern.h"
#include "format.h"

#define PASSED_INC (1<<8) //!! if increased causes huge allocation see val_mem1 file
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

reliq_error *reliq_fmatch(const char *data, const size_t size, SINK *output, const reliq_npattern *nodep,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl);

static reliq_error *reliq_exec_pre(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *exprs, size_t exprsl, const flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar isempty, uchar noncol, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    ); //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
reliq_error *reliq_exec_r(reliq *rq, const reliq_hnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_exprs *exprs);

static inline void
add_compressed_blank(flexarr *dest, const enum outfieldCode val1, const void *val2) //dest: reliq_compressed
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = (reliq_hnode *const)val1;
  x->parent = (void *const)val2;
}

reliq_error *
exprs_check_chain(const reliq_exprs *exprs, const uchar noaccesshooks)
{
  if (!exprs->s)
    return NULL;
  if (exprs->s > 1)
    goto ERR;

  flexarr *chain = exprs->b[0].e; //reliq_expr
  reliq_expr *chainv = (reliq_expr*)chain->v;

  const size_t size = chain->size;
  for (size_t i = 0; i < size; i++) {
    if (chainv[i].flags&EXPR_TABLE)
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
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar flags, uchar useformat, uchar isempty, uchar non) //ncollector: reliq_cstr, dest: reliq_compressed, source: reliq_compressed
{
  if (!source->size && !isempty)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (non || (useformat && !lastnode))
    goto END;
  if (flags&EXPR_TABLE && !isempty) {
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

static reliq_error *
reliq_exec_table(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *expr, const reliq_output_field *named, const flexarr *source, flexarr *dest, flexarr **out, uchar isempty, uchar noncol, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    ) //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  reliq_error *err = NULL;
  flexarr *exprs = (flexarr*)expr->e; //reliq_expr
  reliq_expr *exprsv = (reliq_expr*)exprs->v;
  const size_t exprsl = exprs->size;

  if (expr->flags&EXPR_SINGULAR) {
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
      size_t lastn = ncollector->size;
      #endif
      if (named && expr->childfields)
        add_compressed_blank(dest,ofBlock,NULL);
      if ((err = reliq_exec_pre(rq,parent,output,exprsv,exprsl,in,dest,out,0,isempty,noncol,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        )))
        break;
      if (named && expr->childfields)
        add_compressed_blank(dest,ofBlockEnd,NULL);
      #ifdef RELIQ_EDITING
      if (!noncol && ncollector->size-lastn && expr->nodefl)
        fcollector_add(lastn,ofNamed,expr,ncollector,fcollector);
      #endif
    }

    flexarr_free(in);
    goto END;
  }

  if (named)
    add_compressed_blank(dest,expr->childfields ? ofBlock : ofNoFieldsBlock,named);

  err = reliq_exec_pre(rq,parent,output,exprsv,exprsl,source,dest,out,expr->childfields,noncol,isempty,ncollector
    #ifdef RELIQ_EDITING
    ,fcollector
    #endif
    );

  END: ;
  if (named)
    add_compressed_blank(dest,ofBlockEnd,NULL);
  return err;
}

static reliq_error *
reliq_exec_pre(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *exprs, size_t exprsl, const flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar noncol, uchar isempty, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    )   //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  flexarr *buf[3];
  reliq_error *err;

  buf[0] = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  buf[1] = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  buf[2] = dest ? dest : flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  flexarr *buf_unchanged[2];
  buf_unchanged[0] = buf[0];
  buf_unchanged[1] = buf[1];

  size_t startn = ncollector->size;
  size_t lastn = startn;

  reliq_expr const *lastnode = NULL;

  uchar outprotected = 0;
  reliq_output_field const *outnamed = NULL;
  uchar samesource = 1;

  for (size_t i = 0; i < exprsl; i++) {
    if (exprs[i].outfield.isset) {
      if (exprs[i].outfield.name.b) {
        outnamed = &exprs[i].outfield;
      } else
        outprotected = 1;
    }

    const flexarr *src = buf[0];
    if (samesource && source)
      src = source;


    if (exprs[i].flags&EXPR_TABLE) {
      lastn = ncollector->size;
      size_t prevsize = buf[1]->size;
      uchar noncol_r = noncol;

      if (i != exprsl-1 && !(exprs[i].flags&EXPR_TABLE && !(exprs[i].flags&EXPR_BLOCK)))
        noncol_r |= 1;

      if ((err = reliq_exec_table(rq,parent,output,&exprs[i],outnamed,src,buf[1],out,noncol_r,isempty,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        )))
        goto ERR;

      if (!noncol_r && outnamed && buf[1]->size-prevsize <= 2) {
        //isempty = 1;
        ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,exprs[i].flags,0,1,noncol);
        break;
      }
    } else if (exprs[i].e) {
      lastnode = &exprs[i];
      reliq_npattern *nodep = (reliq_npattern*)exprs[i].e;
      if (outnamed)
        add_compressed_blank(buf[1],ofNamed,outnamed);

      if (!isempty)
        node_exec(rq,parent,nodep,src,buf[1]);

      if (outnamed)
        add_compressed_blank(buf[1],ofBlockEnd,NULL);

      if (!noncol && outprotected && buf[1]->size == 0) {
        add_compressed_blank(buf[1],ofUnnamed,NULL);
        ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,exprs[i].flags,0,0,noncol);
        break;
      }
    }

    #ifdef RELIQ_EDITING
    if (!noncol && exprs[i].flags&EXPR_BLOCK && exprs[i].exprfl)
      fcollector_add(lastn,0,&exprs[i],ncollector,fcollector);
    #endif

    if ((exprs[i].flags&EXPR_TABLE &&
      !(exprs[i].flags&EXPR_BLOCK)) || i == exprsl-1) {
      ncollector_add(ncollector,buf[2],buf[1],startn,lastn,lastnode,exprs[i].flags&EXPR_TABLE,1,isempty,noncol);
      continue;
    }
    if (!buf[1]->size) {
      isempty = 1;
      if (!childfields)
        break;
    }

    samesource = 0;
    buf[0]->size = 0;
    flexarr *tmp = buf[0];
    buf[0] = buf[1];
    buf[1] = tmp;
  }

  if (!dest) {
    if (output) {
      if ((err = nodes_output(rq,output,buf[2],ncollector
          #ifdef RELIQ_EDITING
          ,fcollector
          #endif
          )))
        goto ERR;
      flexarr_free(buf[2]);
    } else
      *out = buf[2];
  }

  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);
  return NULL;

  ERR: ;
  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);
  flexarr_free(buf[2]);
  return err;
}

reliq_error *
reliq_exec_r(reliq *rq, const reliq_hnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  flexarr *compressed=NULL;
  reliq_error *err;

  flexarr *ncollector = flexarr_init(sizeof(reliq_cstr),NCOLLECTOR_INC);
  #ifdef RELIQ_EDITING
  flexarr *fcollector = flexarr_init(sizeof(struct fcollector_expr),FCOLLECTOR_INC);
  #endif

  err = reliq_exec_pre(rq,parent,output,exprs->b,exprs->s,NULL,NULL,&compressed,0,0,0,ncollector
      #ifdef RELIQ_EDITING
      ,fcollector
      #endif
      );

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
reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_exprs *exprs)
{
  return reliq_exec_r(rq,NULL,NULL,nodes,nodesl,exprs);
}

reliq_error *
reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  SINK *out = sink_from_file(output);
  reliq_error *err = reliq_exec_r(rq,NULL,out,NULL,NULL,exprs);
  sink_close(out);
  return err;
}

reliq_error *
reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  SINK *output = sink_open(str,strl);
  reliq_error *err = reliq_exec_r(rq,NULL,output,NULL,NULL,exprs);
  sink_close(output);
  return err;
}

static reliq_error *
reliq_fexec_sink(char *data, size_t size, SINK *output, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len))
{
  reliq_error *err = NULL;
  uchar was_unallocated = 0;
  SINK *destination = output;
  if (!exprs || exprs->s == 0)
    goto END;
  if ((err = exprs_check_chain(exprs,1)))
    goto END;

  char *ptr=data,*nptr;
  size_t fsize;

  flexarr *chain = (flexarr*)exprs->b[0].e; //reliq_expr
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
reliq_fexec_file(char *data, size_t size, FILE *output, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len))
{
  return reliq_fexec_sink(data,size,sink_from_file(output),exprs,freedata);
}

reliq_error *
reliq_fexec_str(char *data, const size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freedata)(void *data, size_t len))
{
  if (!exprs)
    return NULL;
  SINK *output = sink_open(str,strl);
  return reliq_fexec_sink(data,size,output,exprs,freedata);
}
