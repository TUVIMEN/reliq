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
reliq_expr_free_pre(flexarr *exprs) //exprs: reliq_expr
{
  if (!exprs)
    return;
  reliq_expr *e = (reliq_expr*)exprs->v;
  const size_t size = exprs->size;
  for (size_t i = 0; i < size; i++)
    reliq_efree(&e[i]);
  flexarr_free(exprs);
}

void
reliq_efree(reliq_expr *expr)
{
  #ifdef RELIQ_EDITING
  format_free(expr->nodef,expr->nodefl);
  format_free(expr->exprf,expr->exprfl);
  #else
  if (expr->nodef)
    free(expr->nodef);
  #endif
  if (expr->outfield.name.b)
    free(expr->outfield.name.b);

  if (EXPR_IS_TABLE(expr->flags)) {
    reliq_expr_free_pre(expr->e);
  } else {
    reliq_nfree((reliq_npattern*)expr->e);
    free(expr->e);
  }
}

/*static void //just for debugging
reliq_expr_print(flexarr *expr, size_t tab)
{
  reliq_expr *a = (reliq_expr*)expr->v;
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
  fprintf(stderr,"%% %lu",expr->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < expr->size; i++) {
    for (size_t j = 0; j < tab; j++)
      fputs("  ",stderr);
    if (EXPR_IS_TABLE(a[i].flags)) {
      fprintf(stderr,"table %d node(%lu) expr(%lu) field('%.*s')\n",a[i].flags,a[i].nodefl,a[i].exprfl,(int)a[i].outfield.name.s,a[i].outfield.name.b);
      reliq_expr_print((flexarr*)a[i].e,tab);
    } else {
      fprintf(stderr,"nodes node(%lu) expr(%lu) field('%.*s')\n",a[i].nodefl,a[i].exprfl,(int)a[i].outfield.name.s,a[i].outfield.name.b);
    }
  }
}*/

enum tokenName {
  tInvalidThing = 0,
  tBlockStart,
  tBlockEnd,
  tNextNode,
  tChainLink,
  tNodeFormat,
  #ifdef RELIQ_EDITING
  tExprFormat,
  #endif
  tText,
  tConditionOr,
  tConditionAnd,
  tConditionAndBlank,
  tConditionOrAll,
  tConditionAndAll,
  tConditionAndBlankAll,
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
    #ifdef RELIQ_EDITING
    case tExprFormat: return "tExprFormat";
    #endif
    case tConditionOr: return "tConditionOr";
    case tConditionAnd: return "tConditionAnd";
    case tConditionAndBlank: return "tConditionAndBlank";
    case tConditionOrAll: return "tConditionOrAll";
    case tConditionAndAll: return "tConditionAndAll";
    case tConditionAndBlankAll: return "tConditionAndBlankAll";
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
    fprintf(stderr,"%s - %lu - '%.*s'\n",name,tokens[i].size,(int)tokens[i].size,tokens[i].start);
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

static enum tokenName
tokenize_conditionals_normal(const char *src, const size_t i, const size_t size, size_t *tk_size)
{
  enum tokenName ret = tInvalidThing;

  if (i+1 < size && src[i] == '&' && isspace(src[i+1])) {
    *tk_size = 3;
    ret = tConditionAnd;
  } else if (i+2 < size && isspace(src[i+2])) {
    if (src[i] == '&' && src[i+1] == '&') {
      *tk_size = 4;
      ret = tConditionAndBlank;
    } else if (src[i] == '|' && src[i+1] == '|') {
      *tk_size = 4;
      ret = tConditionOr;
    }
  }

  return ret;
}

static enum tokenName
tokenize_conditionals(const char *src, size_t i, const size_t size, size_t *tk_size)
{
  uchar all = 0;
  if (src[i] == '^') {
      all = 1;
      i++;
  }

  enum tokenName name = tokenize_conditionals_normal(src,i,size,tk_size);
  if (!all || name == tInvalidThing)
      return name;

  (*tk_size) += all;

  switch (name) {
    case tConditionOr:
      return tConditionOrAll;
    case tConditionAnd:
      return tConditionAndAll;
    case tConditionAndBlank:
      return tConditionAndBlankAll;
    default:
      return name;
  }
}

static uchar
isconditional(const enum tokenName name)
{
    switch (name) {
        case tConditionOr:
        case tConditionOrAll:
        case tConditionAnd:
        case tConditionAndAll:
        case tConditionAndBlank:
        case tConditionAndBlankAll:
            return 1;
        default:
            return 0;
    }
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

    if (i && isspace(src[i-1])) {
      size_t tk_size = 0;
      enum tokenName tk_name = tokenize_conditionals(src,i,size,&tk_size);
      if (tk_name != tInvalidThing) {
          i--; //conditionals start and end with space, this accounts for first, already found
          token_found(tk_name,tk_size);
      }
    }

    if ((i == 0 || isspace(src[i-1])) && (src[i] == '|'
      #ifdef RELIQ_EDITING
      || src[i] == '/'
      #endif
      )) {
      size_t s = 1;
      enum tokenName n = tNodeFormat;
      #ifdef RELIQ_EDITING
      if (src[i] == '/')
        n = tExprFormat;
      #endif
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

  if (!cl->e && !cl->outfield.name.b && !cl->nodefl
    #ifdef RELIQ_EDITING
    && !cl->exprfl
    #endif
    )
    goto END;

  if (!noerr && exprs->size) {
    if (((reliq_expr*)exprs->v)[exprs->size-1].childfields)
      goto_script_seterr(ERR,"expression: chains cannot have fields in the middle passed to other expression");
    if (((reliq_expr*)exprs->v)[exprs->size-1].childformats)
      goto_script_seterr(ERR,"expression: chains cannot have string type in the middle passed to other expression");
  }

  if (cl->e == NULL) {
    EXPR_TYPE_SET(cl->flags,EXPR_NPATTERN);
    cl->e = malloc(sizeof(reliq_npattern));
    assert(reliq_ncomp(NULL,0,cl->e) == NULL);
  }

  ERR: ;
  *(reliq_expr*)flexarr_inc(exprs) = *cl;

  END: ;
  memset(cl,0,sizeof(reliq_expr));
  return err;
}

static uint8_t
from_tokenname_conditional(const enum tokenName name)
{
  uint8_t ret = 0;
  switch (name) {
    case tConditionOrAll:
      ret |= EXPR_ALL;
    case tConditionOr:
      ret |= EXPR_OR;
      break;
    case tConditionAndAll:
      ret |= EXPR_ALL;
    case tConditionAnd:
      ret |= EXPR_AND;
      break;
    case tConditionAndBlankAll:
      ret |= EXPR_ALL;
    case tConditionAndBlank:
      ret |= EXPR_AND_BLANK;
      break;
    default:
      return 0;
  }
  return ret;
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
  EXPR_TYPE_SET(current->flags,EXPR_CHAIN);

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
      EXPR_TYPE_SET(expr.flags,EXPR_BLOCK);
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

      if (current->childfields && current->flags&EXPR_CONDITION_EXPR) {
        FIELD_IN_CONDITION: ;
        goto_script_seterr_p(END,"conditional: fields cannot be inside conditional expression");
      }
      continue;
    } else if (name == tBlockEnd) {
      if (!lvl) {
        UNPRECEDENTED_END: ;
        goto_script_seterr_p(END,"block: %lu: unprecedented end of block",i);
      }
      goto END;
    } else if (name == tNodeFormat
      #ifdef RELIQ_EDITING
      || name == tExprFormat
      #endif
      ) {
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
          #ifdef RELIQ_EDITING
          if (name == tExprFormat) {
            format = &expr.exprf;
            formatl = &expr.exprfl;
          }
          #endif
          *err = format_comp(tokens[i+1].start,&g,tokens[i+1].size,format,formatl);
          if (*err)
            goto END;
          i++;
        } else if (tokens[i+1].name == tBlockStart) {
          goto_script_seterr_p(END,"%lu: format '%c' isn't terminated before block",i,tokens[i].start[tokens[i].size-1]);
        } else if (tokens[i+1].name == tChainLink) {
          FORMAT_IN_CHAIN: ;
          const char *ftype = "node";
          #ifdef RELIQ_EDITING
          if (name == tExprFormat)
            ftype = "expression";
          #endif
          goto_script_seterr_p(END,"%lu: illegal use of %s format inside chain",i,ftype);
        }
      }

      if (EXPR_TYPE_IS(expr.flags,EXPR_BLOCK) && name == tNodeFormat)
        EXPR_TYPE_SET(expr.flags,EXPR_SINGULAR);

      if (expr.nodefl
        #ifdef RELIQ_EDITING
        || name == tExprFormat
        #endif
        ) {
        if (expr.childfields) {
          const char *ftype = "node";
          #ifdef RELIQ_EDITING
          if (name == tExprFormat)
            ftype = "expression";
          #endif
          goto_script_seterr_p(END,"illegal assignment of %s format to block with fields",ftype);
        }

        expr.childformats++;
        current->childformats++;
        (*childformats)++;
      }
    } else if (name == tText) {
      char const *start = tokens[i].start;
      size_t len = tokens[i].size;
      if (first_in_node && tokens[i].start[0] == '.') {
        size_t g=0;
        if ((*err = reliq_output_field_comp(start,&g,len,&current->outfield))) //&current->outfield
          goto END;
        if (current->outfield.name.b) {
          /*if the above condition is removed protected fields fill work as normal fields
            i.e. '{ .li }; li' will be impossible but '{ . li } / line [1]' will also be,
            and it will break it's functionality*/
          (*childfields)++;
          current->childfields++;
        }

        if (current->flags&EXPR_CONDITION_EXPR)
          goto FIELD_IN_CONDITION;

        while_is(isspace,start,g,len);
        start += g;
        len -= g;

        if (!len) {
          if (i+1 >= tokensl || tokens[i+1].name == tNextNode || tokens[i+1].name == tBlockEnd)
            goto_script_seterr_p(END,"field: %lu: empty expression",i);
          continue;
        }
      }

      if (i+1 < tokensl && tokens[i+1].name == tBlockStart)
        goto_script_seterr_p(END,"block: %lu: unexpected text before opening of the block",i);
      lasttext_nonempty = 1;
      EXPR_TYPE_SET(expr.flags,EXPR_NPATTERN);

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
        EXPR_TYPE_SET(current->flags,EXPR_CHAIN);
      }
    } else if (isconditional(name)) {
      lasttext_nonempty = 0;
      first_in_node = 1;
      expr_has_nformat=0;
      expr_has_eformat=0;
      if ((*err = add_chainlink(current->e,&expr,0)))
        goto END;

      if (((flexarr*)current->e)->size == 0)
        goto_script_seterr_p(END,"conditional: expected expression before %.*s",(int)tokens[i].size-1,tokens[i].start+1);
      if (i+1 >= tokensl || tokens[i+1].name == tNextNode
        || isconditional(tokens[i+1].name) || tokens[i+1].name == tChainLink)
        goto_script_seterr_p(END,"conditional: expected expression after %.*s",(int)tokens[i].size-1,tokens[i].start+1);

      current->flags |= from_tokenname_conditional(name);

      reliq_expr *lastret = &((reliq_expr*)ret->v)[ret->size-1];
      if (!EXPR_TYPE_IS(lastret->flags,EXPR_BLOCK_CONDITION)) {
        reliq_output_field field = current->outfield;
        if (current->childfields-(field.name.b != NULL))
          goto FIELD_IN_CONDITION;

        reliq_expr t = *current;
        memset(&t.outfield,0,sizeof(reliq_output_field));

        memset(current,0,sizeof(reliq_expr));
        current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
        *(reliq_expr*)flexarr_inc(current->e) = t;

        EXPR_TYPE_SET(current->flags,EXPR_BLOCK_CONDITION);
        current->outfield = field;
      }

      current = (reliq_expr*)flexarr_inc(lastret->e);
      memset(current,0,sizeof(reliq_expr));
      current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
      EXPR_TYPE_SET(current->flags,EXPR_CHAIN);
      current->flags |= EXPR_CONDITION_EXPR;
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
reliq_ecomp(const char *src, const size_t size, reliq_expr *expr)
{
  reliq_error *err = NULL;

  token *tokens;
  size_t tokensl = 0;
  if ((err = tokenize(src,size,&tokens,&tokensl)))
    return err;

  //tokens_print(tokens,tokensl);

  uint16_t childfields=0,childformats=0;
  size_t pos=0;
  flexarr *res = from_token_comp(tokens,&pos,tokensl,0,&childfields,&childformats,&err);

  tokens_free(tokens,tokensl);

  if (res) {
    //reliq_expr_print(res,0);

    if (err) {
      reliq_expr_free_pre(res);
    } else {
      memset(expr,0,sizeof(reliq_expr));
      EXPR_TYPE_SET(expr->flags,EXPR_BLOCK);
      expr->e = res;
    }
  }
  return err;
}
