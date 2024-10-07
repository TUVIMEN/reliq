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
#define PATTERN_SIZE_INC (1<<8)
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

//reliq_expr istable flags
#define EXPR_TABLE 0x1
#define EXPR_NEWBLOCK 0x2
#define EXPR_NEWCHAIN 0x4
#define EXPR_SINGULAR 0x8

reliq_error *reliq_fmatch(const char *data, const size_t size, SINK *output, const reliq_npattern *nodep,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl);

static reliq_error *reliq_exec_pre(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar isempty, uchar noncol, flexarr *ncollector
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

/*void //just for debugging
reliq_exprs_print(flexarr *exprs, size_t tab)
{
  reliq_expr *a = (reliq_expr*)exprs->v;
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
  fprintf(stderr,"%% %lu",exprs->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < exprs->size; i++) {
    for (size_t j = 0; j < tab; j++)
      fputs("  ",stderr);
    if (a[i].flags&EXPR_TABLE) {
      fprintf(stderr,"table %d node(%lu) expr(%lu)\n",a[i].flags,a[i].nodefl,a[i].exprfl);
      reliq_exprs_print((flexarr*)a[i].e,tab);
    } else {
      fprintf(stderr,"nodes node(%lu) expr(%lu)\n",a[i].nodefl,a[i].exprfl);
    }
  }
}*/

static void
reliq_expr_free(reliq_expr *expr)
{
  #ifdef RELIQ_EDITING
  format_free(expr->nodef,expr->nodefl);
  format_free(expr->exprf,expr->exprfl);
  #else
  if (expr->nodef)
    free(expr->nodef);
  #endif
  reliq_nfree((reliq_npattern*)expr->e);
  free(expr->e);
  if (expr->outfield.name.b)
    free(expr->outfield.name.b);
}

static void
reliq_exprs_free_pre(flexarr *exprs) //exprs: reliq_expr
{
  if (!exprs)
    return;
  reliq_expr *e = (reliq_expr*)exprs->v;
  const size_t size = exprs->size;
  for (size_t i = 0; i < size; i++) {
    if (e[i].flags&EXPR_TABLE) {
      if (e[i].outfield.name.b)
        free(e[i].outfield.name.b);
      #ifdef RELIQ_EDITING
      format_free(e[i].nodef,e[i].nodefl);
      format_free(e[i].exprf,e[i].exprfl);
      #else
      if (e[i].nodef)
        free(e[i].nodef);
      #endif

      reliq_exprs_free_pre((flexarr*)e[i].e);
    } else
      reliq_expr_free(&e[i]);
  }
  flexarr_free(exprs);
}

void
reliq_efree(reliq_exprs *exprs)
{
  const size_t size = exprs->s;
  for (size_t i = 0; i < size; i++) {
    if (exprs->b[i].flags&EXPR_TABLE) {
      if (exprs->b[i].outfield.name.b)
        free(exprs->b[i].outfield.name.b);
      reliq_exprs_free_pre((flexarr*)exprs->b[i].e);
    } else
      reliq_expr_free(&exprs->b[i]);
  }
  free(exprs->b);
}

static reliq_error *
skip_quotes(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos;
  char quote = src[i++];
  reliq_error *err = NULL;

  while (i < s && src[i] != quote) {
    if (src[i] == '\\' && (src[i+1] == '\\' || src[i+1] == quote))
      i++;
    i++;
  }
  if (i < s && src[i] == quote) {
    i++;
  } else
    err = script_err("string: could not find the end of %c quote",quote);

  *pos = i;
  return err;
}

static reliq_error *
skip_sbrackets(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos;
  reliq_error *err = NULL;

  i++;
  while (i < s && src[i] != ']')
    i++;
  if (i < s && src[i] == ']') {
    i++;
  } else
    err = script_err("range: char %lu: unprecedented end of range",i);

  *pos = i;
  return err;
}

static uchar
skip_comment(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos;
  if (i+1 >= s || src[i] != '/' || (src[i+1] != '/' && src[i+1] != '*'))
    return 0;

  char tf = src[i+1];
  i += 2;

  if (tf == '/') {
    for (; i < s; i++) {
      if (src[i] == '\n') {
        i++;
        break;
      }
    }
  } else {
    for (; i < s; i++) {
      if (i+1 < s && src[i] == '*' && src[i+1] == '/') {
        i += 2;
        break;
      }
    }
  }

  *pos = i;
  return 1;
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
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar istable, uchar useformat, uchar isempty, uchar non) //ncollector: reliq_cstr, dest: reliq_compressed, source: reliq_compressed
{
  if (!source->size && !isempty)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (non || (useformat && !lastnode))
    goto END;
  if (istable&EXPR_TABLE && !isempty) {
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
reliq_exec_table(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *expr, const reliq_output_field *named, flexarr *source, flexarr *dest, flexarr **out, uchar isempty, uchar noncol, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    ) //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  reliq_error *err = NULL;
  flexarr *exprs = (flexarr*)expr->e; //reliq_expr
  reliq_expr *exprsv = (reliq_expr*)exprs->v;
  size_t exprsl = exprs->size;

  if (expr->flags&EXPR_SINGULAR) {
    if (named)
      add_compressed_blank(dest,expr->childfields ? ofArray : ofNoFieldsBlock,named);
    if (!source->size)
      goto END;

    flexarr *in = flexarr_init(sizeof(reliq_compressed),1);
    reliq_compressed *inv = (reliq_compressed*)flexarr_inc(in);
    reliq_compressed *sourcev = (reliq_compressed*)source->v;

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

static flexarr *
reliq_ecomp_pre(const char *csrc, size_t *pos, size_t s, const uint16_t lvl, uint16_t *childfields, reliq_error **err) //reliq_expr
{
  if (s == 0)
    return NULL;

  if (lvl >= RELIQ_MAX_BLOCK_LEVEL) {
    *err = script_err("block: %lu: reached %lu level of recursion",*pos,lvl);
    return NULL;
  }

  size_t tpos = 0;
  if (pos == NULL)
    pos = &tpos; //works since it's passed up the stack

  flexarr *ret = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  reliq_expr *acurrent = (reliq_expr*)flexarr_inc(ret);
  memset(acurrent,0,sizeof(reliq_expr));
  acurrent->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  acurrent->flags = EXPR_TABLE|EXPR_NEWCHAIN;

  reliq_expr expr = (reliq_expr){0};
  char *src = memdup(csrc,s);
  size_t exprl;
  size_t i=*pos,first_pos=*pos,
    i_diff=0; //i_diff accounts for deleted charactes (by comments or escape characters) from copy of csrc,
    //since calling functions operate on unchanged csrc returned *pos has to be i+i_diff
  uchar found_block_end = 0;

  enum {
    typeChainlink,
    typeNextExpr,
    typeGroupStart,
    typeGroupEnd,
    typeFormat
  } next = typeChainlink;

  reliq_str nodef,exprf; //formats

  for (size_t j; i < s; i++) {
    j = i;

    if (next == typeNextExpr) {
      first_pos = j;
      next = typeChainlink;
    }

    uchar hasexpr=0,hasended=0;
    reliq_expr *new = NULL;
    exprl = 0;
    uchar get_format = 0;

    REPEAT: ;

    expr.nodefl = 0;
    nodef.b = NULL;
    nodef.s = 0;
    exprf.b = NULL;
    exprf.s = 0;
    while (i < s) {
      if (src[i] == '\\' && i+1 < s) {
        char c = src[i+1];
        if (c == '\\') {
          i += 2;
          continue;
        }
        uchar toescape = 0;
        if (c == ',' || c == ';' || c == '"' || c == '\'' || c == '{' || c == '}') {
          toescape = 1;
        }
        if (toescape) {
          delchar(src,i++,&s);
          exprl = (i-j)-nodef.s-((nodef.b) ? 1 : 0);
          i_diff++;
          continue;
        }
      }

      if ((i == j || (i && isspace(src[i-1]))) &&
        (src[i] == '|' || src[i] == '/')) {
        size_t prev_i = i;
        if (skip_comment(src,&i,s)) {
          i_diff += i-prev_i;
          delstr(src,prev_i,&s,i-prev_i);
          i = prev_i;
          continue;
        }

        if ((src[i] == '|' && nodef.b) ||
          (src[i] == '/' && exprf.b) ||
          (i+1 < s && (src[i+1] == '/' || src[i+1] == '|')))
          goto_script_seterr_p(EXIT,"%lu: format '%c' cannot be specified twice",i,src[i]);

        if (i == j)
          hasexpr = 1;
        if (src[i] == '|') {
          nodef.b = src+(++i);
        } else {
          if (nodef.b)
            nodef.s = i-(nodef.b-src);
          exprf.b = src+(++i);
        }
        get_format = 1;
        continue;
      }

      if (src[i] == '"' || src[i] == '\'') {
        if ((*err = skip_quotes(src,&i,s)))
          goto EXIT;
        if (i < s)
          continue;
      }
      if (i < s && src[i] == '[') {
        if ((*err = skip_sbrackets(src,&i,s)))
          goto EXIT;
        if (i < s)
          continue;
      }

      if (i < s && (src[i] == ',' || src[i] == ';' || src[i] == '{' || src[i] == '}')) {
        if (get_format && src[i] == ';')
          goto_script_seterr_p(EXIT,"%lu: illegal use of node format inside chain",i);

        if (exprf.b) {
          exprf.s = i-(exprf.b-src);
        } else if (nodef.b)
          nodef.s = i-(nodef.b-src);

        if (src[i] == '{') {
          next = typeGroupStart;
          if (get_format)
            goto UNEXPECTED_BEFORE_BLOCK;
        } else if (src[i] == '}') {
          next = typeGroupEnd;
          found_block_end = 1;
          get_format = 0;
        } else {
          next = (src[i] == ',') ? typeNextExpr : typeChainlink;
          exprl = i-j;
          exprl -= nodef.s+((nodef.b) ? 1 : 0);
          exprl -= exprf.s+((exprf.b) ? 1 : 0);
          get_format = 0;
        }
        i++;
        break;
      }
      i++;
      if (!nodef.b
        #ifdef RELIQ_EDITING
              && !exprf.b
        #endif
              )
        exprl = i-j;
    }

    if (j+exprl > s)
      exprl = s-j;
    if (i > s)
      i = s;
    expr.nodef = NULL;
    expr.nodefl = 0;
    if (!nodef.s)
      nodef.s = i-(nodef.b-src);

    #ifdef RELIQ_EDITING
    expr.exprf = NULL;
    expr.exprfl = 0;
    if (!exprf.s)
      exprf.s = i-(exprf.b-src);
    #endif

    if (nodef.b) {
      size_t g=0,t=nodef.s;
      *err = format_comp(nodef.b,&g,t,&expr.nodef,&expr.nodefl);
      s -= nodef.s-t;
      if (*err)
        goto EXIT;
      if (hasended) {
        new->flags |= EXPR_SINGULAR;
        new->nodef = expr.nodef;
        new->nodefl = expr.nodefl;
        if (new->childfields && expr.nodefl)
          goto_script_seterr_p(EXIT,"illegal assignment of node format to block with fields");
      }
    }
    #ifdef RELIQ_EDITING
    if (exprf.b) {
      size_t g=0,t=exprf.s;
      *err = format_comp(exprf.b,&g,t,&expr.exprf,&expr.exprfl);
      s -= exprf.s-t;
      if (*err)
        goto EXIT;
      if (hasended) {
        new->exprf = expr.exprf;
        new->exprfl = expr.exprfl;
        if (new->childfields)
          goto_script_seterr_p(EXIT,"illegal assignment of expression format to block with fields");
      }
    }
    #endif

    if (hasended) {
      if (next == typeGroupEnd)
        goto END_BRACKET;
      if (next == typeNextExpr)
        goto NEXT_NODE;
      continue;
    }

    if ((next != typeGroupEnd || src[j] != '}') && (next == typeGroupStart
        || next == typeGroupEnd || exprl || hasexpr)) {
      expr.e = NULL;
      expr.flags = 0;
      expr.outfield.name.b = NULL;
      expr.childfields = 0;
      expr.outfield.isset = 0;

      if (next != typeGroupStart)
        expr.e = malloc(sizeof(reliq_npattern));

      size_t g = j;
      while_is(isspace,src,g,s);
      exprl -= g-j;

      if (exprl) {
        if (j == first_pos && g < s && src[g] == '.') {
          size_t g_prev = g;
          if ((*err = reliq_output_field_comp(src,&g,s,&expr.outfield)))
            goto NODE_COMP_END;
          exprl -= g-g_prev;
          if (expr.outfield.name.b) {
            if (childfields)
              (*childfields)++;
            acurrent->childfields++;
          }
          g_prev = g;
          while_is(isspace,src,g,s);
          exprl -= g-g_prev;
        }

        if (next == typeGroupStart && (exprl || get_format)) {
          UNEXPECTED_BEFORE_BLOCK: ;
          goto_script_seterr_p(EXIT,"block: %lu: unexpected text before opening of the block",i);
        }

        if (expr.e) {
          if (!exprl) {
            free(expr.e);
            continue;
          }
          *err = reliq_ncomp(src+g,exprl,(reliq_npattern*)expr.e);
          if (*err)
            goto EXIT;
        }
      } else if (expr.e) {
        if (nodef.b || exprf.b) {
          memset(expr.e,0,sizeof(reliq_npattern));
          ((reliq_npattern*)expr.e)->flags |= N_EMPTY;
        } else {
          free(expr.e);
          continue;
        }
      }
      NODE_COMP_END:
      new = (reliq_expr*)flexarr_inc(acurrent->e);
      *new = expr;

      if (*err) {
        if (expr.e) {
          free(expr.e);
          new->e = NULL;
        }
        goto EXIT;
      }
    }

    if (next == typeGroupStart) {
      new->flags |= EXPR_TABLE|EXPR_NEWBLOCK;
      next = typeChainlink;
      *pos = i;
      new->e = reliq_ecomp_pre(src,pos,s,lvl+1,&new->childfields,err);
      if (*err)
        goto EXIT;
      if (childfields)
        *childfields += new->childfields;
      acurrent->childfields += new->childfields;

      i = *pos;
      while_is(isspace,src,i,s);
      if (i < s) {
        if (src[i] == ',') {
          i++;
          next = typeNextExpr;
          goto NEXT_NODE;
        } else if (src[i] == '}') {
          i++;
          found_block_end = 1;
          goto END_BRACKET;
        } else if (src[i] == ';')
          continue;
        else if (src[i] == '{') {
          goto UNEXPECTED_BEFORE_BLOCK;
        } else if (src[i] == '|' || src[i] == '/') {
           hasended = 1;
           get_format = 1;
           goto REPEAT;
        } else
          goto_script_seterr_p(EXIT,"block: %lu: unexpected text after ending of the block",i);
      }
    }

    if (next == typeNextExpr) {
      NEXT_NODE: ;
      acurrent = (reliq_expr*)flexarr_inc(ret);
      memset(acurrent,0,sizeof(reliq_expr));
      acurrent->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
      acurrent->flags = EXPR_TABLE|EXPR_NEWCHAIN;
    }
    if (next == typeGroupEnd)
      goto END_BRACKET;

    while_is(isspace,src,i,s);
    i--;
  }

  END_BRACKET: ;
  *pos = i+i_diff;
  flexarr_clearb(ret);
  EXIT:
  free(src);
  if (!*err && ((lvl > 0 && !found_block_end) || (lvl == 0 && found_block_end)))
    *err = script_err("block: %lu: unprecedented end of block",i);
  if (*err) {
    reliq_exprs_free_pre(ret);
    return NULL;
  }
  return ret;
}

static reliq_error *block_check(flexarr *exprs);

static uchar
block_is_str(flexarr *exprs)
{
  reliq_expr *a = (reliq_expr*)exprs->v;
  const size_t s = exprs->size;
  for (size_t i = 0; i < s; i++) {
    if (a[i].nodefl || a[i].exprfl)
      return 1;
    if (a[i].flags&EXPR_TABLE && block_is_str((flexarr*)a[i].e))
      return 1;
  }
  return 0;
}

static reliq_error *
chain_check(flexarr* exprs)
{
  reliq_error *err = NULL;
  reliq_expr *a = (reliq_expr*)exprs->v;
  const size_t s = exprs->size;
  for (size_t i = 0; i < s; i++) {
    if (i < s-1) {
      if (a[i].childfields)
        return script_err("expression: chains cannot have fields in the middle, %lu %lu, passed to other expression",i,s);
      if (a[i].flags&EXPR_TABLE && block_is_str((flexarr*)a[i].e))
        return script_err("expression: chains cannot have string type in the middle, %lu %lu, passed to other exprssion",i,s);
    }
    if (a[i].flags&EXPR_NEWBLOCK || a[i].flags&EXPR_SINGULAR) {
      err = block_check((flexarr*)a[i].e);
      if (err)
        break;
    }
  }
  return err;
}

static reliq_error *
block_check(flexarr *exprs)
{
  reliq_error *err = NULL;
  reliq_expr *a = (reliq_expr*)exprs->v;
  for (size_t i = 0; i < exprs->size; i++) {
    if (!(a[i].flags&EXPR_TABLE))
      continue;

    if (a[i].flags&EXPR_NEWCHAIN) {
        err = chain_check((flexarr*)a[i].e);
        if (err)
          break;
    }
  }
  return err;
}

static reliq_error *
exprs_check_integrity(flexarr *exprs)
{
  return block_check(exprs);
}

reliq_error *
reliq_ecomp(const char *src, const size_t size, reliq_exprs *exprs)
{
  reliq_error *err = NULL;
  flexarr *ret = reliq_ecomp_pre(src,NULL,size,0,NULL,&err);
  if (ret) {
    if (!err) {
        //reliq_exprs_print(ret,0);
        err = exprs_check_integrity(ret);
    }
    flexarr_conv(ret,(void**)&exprs->b,&exprs->s);
  } else
    exprs->s = 0;
  return err;
}

static reliq_error *
reliq_exec_pre(const reliq *rq, const reliq_hnode *parent, SINK *output, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar noncol, uchar isempty, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector //struct fcollector_expr
    #endif
    )   //source: reliq_compressed, dest: reliq_compressed, out: reliq_compressed, ncollector: reliq_cstr
{
  flexarr *buf[3];
  reliq_error *err;

  buf[0] = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  if (source && source->size) {
    buf[0]->asize = source->size;
    buf[0]->size = source->size;
    buf[0]->v = memdup(source->v,source->size*sizeof(reliq_compressed));
  }

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

  for (size_t i = 0; i < exprsl; i++) {
    if (exprs[i].outfield.isset) {
      if (exprs[i].outfield.name.b) {
        outnamed = &exprs[i].outfield;
      } else
        outprotected = 1;
    }

    if (exprs[i].flags&EXPR_TABLE) {
      lastn = ncollector->size;
      size_t prevsize = buf[1]->size;
      uchar noncol_r = noncol;

      if (i != exprsl-1 && !(exprs[i].flags&EXPR_TABLE && !(exprs[i].flags&EXPR_NEWBLOCK)))
        noncol_r |= 1;

      if ((err = reliq_exec_table(rq,parent,output,&exprs[i],outnamed,buf[0],buf[1],out,noncol_r,isempty,ncollector
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
        node_exec(rq,parent,nodep,buf[0],buf[1]);

      if (outnamed)
        add_compressed_blank(buf[1],ofBlockEnd,NULL);

      if (!noncol && outprotected && buf[1]->size == 0) {
        add_compressed_blank(buf[1],ofUnnamed,NULL);
        ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,exprs[i].flags,0,0,noncol);
        break;
      }
    }

    #ifdef RELIQ_EDITING
    if (!noncol && exprs[i].flags&EXPR_NEWBLOCK && exprs[i].exprfl)
      fcollector_add(lastn,0,&exprs[i],ncollector,fcollector);
    #endif

    if ((exprs[i].flags&EXPR_TABLE &&
      !(exprs[i].flags&EXPR_NEWBLOCK)) || i == exprsl-1) {
      ncollector_add(ncollector,buf[2],buf[1],startn,lastn,lastnode,exprs[i].flags&EXPR_TABLE,1,isempty,noncol);
      continue;
    }
    if (!buf[1]->size) {
      isempty = 1;
      if (!childfields)
        break;
    }

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
