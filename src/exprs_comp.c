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

#define PATTERN_SIZE_INC (1<<5)

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


/*static void //just for debugging
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

enum tokenName {
  tBlockStart = 1,
  tBlockEnd,
  tNextNode,
  tChainLink,
  tNodeFormat,
  tExprFormat,
  tText
};

typedef struct {
  char const *start;
  size_t size;
  enum tokenName name;
} token;

/*static const char *
token_name(enum tokenName name)
{
  switch (name) {
    case tText: return "tText";
    case tBlockStart: return "tBlockStart";
    case tBlockEnd: return "tBlockEnd";
    case tNextNode: return "tNextNode";
    case tChainLink: return "tChainLink";
    case tNodeFormat: return "tNodeFormat";
    case tExprFormat: return "tExprFormat";
  }
  return NULL;
}

static void
tokens_print(const token *tokens, const size_t tokensl)
{
  uint16_t lvl = 0;
  for (size_t i = 0; i < tokensl; i++) {
    const char *name = token_name(tokens[i].name);
    if (tokens[i].name == tBlockEnd && lvl)
      lvl--;
    for (size_t k = 0; k < lvl; k++)
      fwrite("  ",1,2,stderr);
    fprintf(stderr,"%s - %lu - '%.*s'\n",name,tokens[i].size,tokens[i].size,tokens[i].start);
    if (tokens[i].name == tBlockStart)
      lvl++;
  }
}*/

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

static void
token_text(flexarr *tokens, const char *src, const size_t size) //tokens: token
{
  flexarr *str = flexarr_init(sizeof(char),1<<7);
  token t;
  t.name = tText;

  for (size_t i = 0; i < size;) {
    if (unlikely(!isalnum(src[i]))) {
      if (i+1 < size && src[i] == '\\' && (src[i+1] == ',' || src[i+1] == ';' ||
        src[i+1] == '"' || src[i+1] == '\'' || src[i+1] == '{' || src[i+1] == '}')) {
        i++;
      } else if (src[i] == '"' || src[i] == '\'') {
        size_t i_prev = i;
        assert(!skip_quotes(src,&i,size));
        for (size_t j = i_prev; j < i; j++)
          *(char*)flexarr_inc(str) = src[j];
        continue;
      } else if (i && isspace(src[i-1]) && skip_comment(src,&i,size))
        continue;
    }

    *(char*)flexarr_inc(str) = src[i++];
  }

  flexarr_conv(str,(void**)&t.start,&t.size);
  if (t.size)
    *(token*)flexarr_inc(tokens) = t;
}

static reliq_error *
tokenize(const char *src, const size_t size, token **tokens, size_t *tokensl) //tokens: token
{
  #define token_found(x,y) { tokenstart=src+i; name=(x); tokensize=(y); goto FOUND; }
  reliq_error *err = NULL;
  flexarr *ret = flexarr_init(sizeof(token),1<<5);
  char const *textstart = NULL;
  enum tokenName name;
  size_t tokensize;
  char const *tokenstart;

  for (size_t i = 0; i < size; i++) {
    if (likely(isalnum(src[i]))) {
      if (unlikely(!textstart))
        textstart = src+i;
      continue;
    }

    if (src[i] == '\\') {
      if (!textstart)
        textstart = src+i;
      i++;
      continue;
    }
    if (src[i] == '"' || src[i] == '\'') {
      if (!textstart)
        textstart = src+i;
      if ((err = skip_quotes(src,&i,size)))
        goto END;
      if (i)
        i--;
      continue;
    }
    if (src[i] == '[') {
      if (!textstart)
        textstart = src+i;
      if ((err = skip_sbrackets(src,&i,size)))
        goto END;
      i--;
      continue;
    }
    if ((i == 0 || isspace(src[i-1])) && src[i] == '/' && skip_comment(src,&i,size)) {
      i--;
      continue;
    }

    if (src[i] == '{')
      token_found(tBlockStart,1);
    if (src[i] == '}')
      token_found(tBlockEnd,1);
    if (src[i] == ',')
      token_found(tNextNode,1);
    if (src[i] == ';')
      token_found(tChainLink,1);

    if ((i == 0 || isspace(src[i-1])) && (src[i] == '/' || src[i] == '|')) {
      size_t s = 1;
      enum tokenName n = tNodeFormat;
      if (src[i] == '/')
        n = tExprFormat;
      if (i != 0) {
        s = 2;
        i--;
      }
      token_found(n,s);
    }
    if (!textstart && !isspace(src[i])) {
      textstart = src+i;
      continue;
    }

    if (0) {
      FOUND: ;
      if (textstart) {
        token_text(ret,textstart,src+i-textstart);
        textstart = NULL;
      }
      *(token*)flexarr_inc(ret) = (token){tokenstart,tokensize,name};
      i += tokensize-1;
    }
  }

  if (textstart)
    token_text(ret,textstart,src+size-textstart);

  END: ;
  if (err) {
    flexarr_free(ret);
    *tokens = NULL;
    *tokensl = 0;
  } else {
    flexarr_conv(ret,(void**)tokens,tokensl);
  }
  return err;
}

static reliq_error *
add_chainlink(flexarr *exprs, reliq_expr *cl, const uchar noerr) //exprs: reliq_expr
{
  reliq_error *err = NULL;

  if (!cl->e && !cl->outfield.name.b && !cl->nodefl && !cl->exprfl)
    goto END;

  if (!noerr && exprs->size) {
    if (((reliq_expr*)exprs->v)[exprs->size-1].childfields)
      goto_script_seterr(ERR,"expression: chains cannot have fields in the middle passed to other expression");
    if (((reliq_expr*)exprs->v)[exprs->size-1].childformats)
      goto_script_seterr(ERR,"expression: chains cannot have string type in the middle passed to other expression");
  }

  if (cl->e == NULL) {
    cl->flags |= EXPR_NPATTERN;
    cl->e = malloc(sizeof(reliq_npattern));
    assert(reliq_ncomp(NULL,0,cl->e) == NULL);
  }

  ERR: ;
  *(reliq_expr*)flexarr_inc(exprs) = *cl;

  END: ;
  memset(cl,0,sizeof(reliq_expr));
  return err;
}

static flexarr *
from_token_comp(const token *tokens, size_t *pos, const size_t tokensl, const uint16_t lvl, uint16_t *childfields, uint16_t *childformats, reliq_error **err) //reliq_expr, expr: reliq_expr
{
  size_t i = *pos;
  if (lvl >= RELIQ_MAX_BLOCK_LEVEL) {
    *err = script_err("block: %lu: reached %lu level of recursion",i,lvl);
    return NULL;
  }
  if (i >= tokensl)
    return NULL;

  flexarr *ret = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  reliq_expr *current = flexarr_inc(ret);
  memset(current,0,sizeof(reliq_expr));

  current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  current->flags = EXPR_CHAIN;

  reliq_expr expr = (reliq_expr){0};
  uchar first_in_node=1,expr_has_nformat=0,expr_has_eformat=0,lasttext_nonempty=0;

  for (; i < tokensl; i++) {
    const enum tokenName name = tokens[i].name;
    if (name == tBlockStart) {
      if (i) {
         if (tokens[i-1].name == tBlockEnd)
          goto_script_seterr_p(END,"block: %lu: unterminated block before opening of the block",i);
        if (tokens[i-1].name == tText && lasttext_nonempty)
          goto_script_seterr_p(END,"block: %lu: unexpected text before opening of the block",i);
      }
      i++;
      expr.flags |= EXPR_BLOCK;
      expr.e = from_token_comp(tokens,&i,tokensl,lvl+1,&expr.childfields,&expr.childformats,err);
      if (*err)
        goto END;
      if (i >= tokensl || tokens[i].name != tBlockEnd)
        goto UNPRECEDENTED_END;

      if (i+1 < tokensl && tokens[i+1].name == tText)
        goto_script_seterr_p(END,"block: %lu: unexpected text after ending of the block",i);

      *childfields += expr.childfields;
      current->childfields += expr.childfields;
      *childformats += expr.childformats;
      current->childformats += expr.childformats;
      continue;
    } else if (name == tBlockEnd) {
      if (!lvl) {
        UNPRECEDENTED_END: ;
        goto_script_seterr_p(END,"block: %lu: unprecedented end of block",i);
      }
      goto END;
    } else if (name == tNodeFormat || name == tExprFormat) {
      if (name == tNodeFormat) {
        if (expr_has_nformat)
          goto CANNOT_TWICE;
        expr_has_nformat = 1;
      } else {
        if (expr_has_eformat) {
          CANNOT_TWICE:
          goto_script_seterr_p(END,"%lu: format '%c' cannot be specified twice",i,tokens[i].start[tokens[i].size-1]);
        }
        expr_has_eformat = 1;
      }

      if (i+1 < tokensl) {
        if (tokens[i+1].name == tText) {
          if (i+2 < tokensl && tokens[i+2].name == tChainLink)
            goto FORMAT_IN_CHAIN;

         lasttext_nonempty = 1;
          size_t g=0;
          void *format = &expr.nodef;
          size_t *formatl = &expr.nodefl;
          if (name == tExprFormat) {
            format = &expr.exprf;
            formatl = &expr.exprfl;
          }
          *err = format_comp(tokens[i+1].start,&g,tokens[i+1].size,format,formatl);
          if (*err)
            goto END;
          i++;
        } else if (tokens[i+1].name == tBlockStart) {
          goto_script_seterr_p(END,"%lu: format '%c' isn't terminated before block",i,tokens[i].start[tokens[i].size-1]);
        } else if (tokens[i+1].name == tChainLink) {
          FORMAT_IN_CHAIN: ;
          goto_script_seterr_p(END,"%lu: illegal use of %s format inside chain",i,(name == tNodeFormat) ? "node" : "expression");
        }
      }

      if (expr.flags&EXPR_BLOCK && name == tNodeFormat)
        expr.flags |= EXPR_SINGULAR;

      if (name == tExprFormat || expr.nodefl) {
        if (expr.childfields)
          goto_script_seterr_p(END,"illegal assignment of %s format to block with fields",(name == tNodeFormat) ? "node" : "expression");

        expr.childformats++;
        current->childformats++;
        (*childformats)++;
      }
    } else if (name == tText) {
      char const *start = tokens[i].start;
      size_t len = tokens[i].size;
      if (first_in_node && tokens[i].start[0] == '.') {
        size_t g=0;
        if ((*err = reliq_output_field_comp(start,&g,len,&expr.outfield)))
          goto END;
        if (expr.outfield.name.b) {
          (*childfields)++;
          current->childfields++;
        }
        while_is(isspace,start,g,len);
        start += g;
        len -= g;

        if (!len)
          continue;
      }

      if (i+1 < tokensl && tokens[i+1].name == tBlockStart)
        goto_script_seterr_p(END,"block: %lu: unexpected text before opening of the block",i);
      lasttext_nonempty = 1;
      expr.flags |= EXPR_NPATTERN;

      expr.e = malloc(sizeof(reliq_npattern));
      if ((*err = reliq_ncomp(start,len,(reliq_npattern*)expr.e))) {
        free(expr.e);
        expr.e = NULL;
        goto END;
      }
    } else if (name == tChainLink) {
      lasttext_nonempty = 0;
      first_in_node = 0;
      if ((*err = add_chainlink(current->e,&expr,0)))
        goto END;
    } else if (name == tNextNode) {
      lasttext_nonempty = 0;
      first_in_node = 1;
      expr_has_nformat=0;
      expr_has_eformat=0;
      if ((*err = add_chainlink(current->e,&expr,0)))
        goto END;

      if (((flexarr*)current->e)->size) {
        current = (reliq_expr*)flexarr_inc(ret);
        memset(current,0,sizeof(reliq_expr));
        current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
        current->flags = EXPR_CHAIN;
      }
    }
  }

  END:
  *pos = i;
  if (*err) {
    add_chainlink(current->e,&expr,1);
  } else
    *err = add_chainlink(current->e,&expr,0);

  if (!*err)
    flexarr_clearb(ret);
  return ret;
}

static void
tokens_free(token *tokens, const size_t tokensl)
{
  for (size_t i = 0; i < tokensl; i++)
    if (tokens[i].name == tText)
      free((void*)tokens[i].start);
  free(tokens);
}

reliq_error *
reliq_ecomp(const char *src, const size_t size, reliq_exprs *exprs)
{
  reliq_error *err = NULL;
  exprs->s = 0;

  token *tokens;
  size_t tokensl = 0;
  if ((err = tokenize(src,size,&tokens,&tokensl)))
    return err;

  //tokens_print(tokens,tokensl);

  uint16_t childfields=0,childformats=0;
  size_t pos=0;
  flexarr *ret = from_token_comp(tokens,&pos,tokensl,0,&childfields,&childformats,&err);

  tokens_free(tokens,tokensl);

  if (ret) {
    //reliq_exprs_print(ret,0);

    if (err) {
      reliq_exprs_free_pre(ret);
    } else
      flexarr_conv(ret,(void**)&exprs->b,&exprs->s);
  }
  return err;
}
