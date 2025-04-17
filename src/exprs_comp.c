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
#include "exprs.h"
#include "reliq.h"

/*#define EXPR_DEBUG*/
/*#define TOKEN_DEBUG*/

#define PATTERN_SIZE_INC (1<<5)

void reliq_efree_intr(reliq_expr *expr);

static void
reliq_expr_free_pre(flexarr *exprs) //exprs: reliq_expr
{
  if (!exprs)
    return;
  reliq_expr *e = (reliq_expr*)exprs->v;
  const size_t size = exprs->size;
  for (size_t i = 0; i < size; i++)
    reliq_efree_intr(&e[i]);
  flexarr_free(exprs);
}

void
reliq_efree_intr(reliq_expr *expr)
{
  format_free(expr->nodef,expr->nodefl);
  format_free(expr->exprf,expr->exprfl);
  if (expr->outfield.name.b)
    free(expr->outfield.name.b);

  if (EXPR_IS_TABLE(expr->flags)) {
    reliq_expr_free_pre(expr->e);
  } else {
    reliq_nfree((reliq_npattern*)expr->e);
    free(expr->e);
  }
}

void
reliq_efree(reliq_expr *expr)
{
  reliq_efree_intr(expr);
  free(expr);
}

#ifdef EXPR_DEBUG

static void
reliq_expr_print_tab(size_t tab)
{
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
}

static void reliq_expr_print_array(const flexarr *expr, size_t tab);

static void
reliq_expr_print_field(const reliq_expr *e)
{
  if (e->outfield.name.s)
      fprintf(stderr,"\033[33;1m.%.*s\033[0m ",(int)e->outfield.name.s,e->outfield.name.b);
}

static void
reliq_expr_print_format(const reliq_expr *e)
{
  if (e->nodefl)
      fprintf(stderr,"\033[32m|\033[0m\033[;1m%lu\033[0m ",e->nodefl);
  if (e->exprfl)
      fprintf(stderr,"\033[32m/\033[0m\033[;1m%lu\033[0m ",e->exprfl);
}

static void
reliq_expr_print_nontable(const reliq_expr *e, size_t tab)
{
  reliq_expr_print_tab(tab);
  assert((e->flags&EXPR_CONDITION) == 0);
  fputs("node ",stderr);
  reliq_expr_print_field(e);

  reliq_expr_print_format(e);
}

static void
reliq_expr_print_table(const reliq_expr *e, size_t tab)
{
  reliq_expr_print_tab(tab);

  const char *name = "Unidentified";
  switch (e->flags&EXPR_TYPE) {
    case EXPR_BLOCK: name = "block"; break;
    case EXPR_BLOCK_CONDITION: name = "condition"; break;
    case EXPR_CHAIN: name = "chain"; break;
    case EXPR_SINGULAR: name = "singular"; break;
  }
  fputs(name,stderr);
  fputc(' ',stderr);

  reliq_expr_print_field(e);

  char *cond;
  switch ((e->flags&EXPR_CONDITION)&~(EXPR_ALL)) {
    case EXPR_CONDITION_EXPR: cond = "cond_expr"; break;
    case EXPR_AND: cond = "&"; break;
    case EXPR_AND_BLANK: cond = "&&"; break;
    case EXPR_OR: cond = "||"; break;
    default:
      cond = NULL;
  }
  if (cond) {
    fputs("\033[35m",stderr);
    if (e->flags&EXPR_ALL)
        fputc('^',stderr);
    fputs(cond,stderr);
    fputc(' ',stderr);
    fputs("\033[0m",stderr);
  }

  reliq_expr_print_format(e);

  fputs("\033[36m{\033[0m",stderr);
  fputc('\n',stderr);

  reliq_expr_print_array((const flexarr*)e->e,tab);

  reliq_expr_print_tab(tab);
  fputs("\033[36m}\033[0m",stderr);
}

static void
reliq_expr_print_array(const flexarr *expr, size_t tab)
{
  const reliq_expr *e = (const reliq_expr*)expr->v;
  tab++;
  for (size_t i = 0; i < expr->size; i++) {
    if (EXPR_IS_TABLE(e[i].flags)) {
      reliq_expr_print_table(e+i,tab);
    } else {
      reliq_expr_print_nontable(e+i,tab);
    }
    if (i < expr->size-1)
      fputc(',',stderr);
    fputc('\n',stderr);
  }
}

static void //just for debugging
reliq_expr_print(const flexarr *expr, size_t tab)
{
  reliq_expr_print_tab(tab);
  fprintf(stderr,"\033[34;2m//\033[0m\033[32;6mEXPR\033[0m\n");
  fprintf(stderr,"\033[31mroot\033[0m {\n");

  reliq_expr_print_array(expr,tab);

  fprintf(stderr,"}\n\n");
}

#endif //EXPR_DEBUG

enum tokenName {
  tInvalidThing = 0,
  tBlockStart,
  tBlockEnd,
  tNextNode,
  tChainLink,
  tNodeFormat,
  tExprFormat,
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

#ifdef TOKEN_DEBUG

static const char *
token_name(enum tokenName name)
{
  switch (name) {
    case tInvalidThing: return "tInvalidThing";
    case tText: return "tText";
    case tBlockStart: return "tBlockStart";
    case tBlockEnd: return "tBlockEnd";
    case tNextNode: return "tNextNode";
    case tChainLink: return "tChainLink";
    case tNodeFormat: return "tNodeFormat";
    case tExprFormat: return "tExprFormat";
    case tConditionOr: return "tConditionOr";
    case tConditionAnd: return "tConditionAnd";
    case tConditionAndBlank: return "tConditionAndBlank";
    case tConditionOrAll: return "tConditionOrAll";
    case tConditionAndAll: return "tConditionAndAll";
    case tConditionAndBlankAll: return "tConditionAndBlankAll";
  }
  return NULL;
}

static char
tosplchar(const char c)
{
  char r;
  switch (c) {
    case '\0': r = '0'; break;
    case '\a': r = 'a'; break;
    case '\b': r = 'b'; break;
    case '\t': r = 't'; break;
    case '\n': r = 'n'; break;
    case '\v': r = 'v'; break;
    case '\f': r = 'f'; break;
    case '\r': r = 'r'; break;
    default: r = c;
  }
  return r;
}

static void
tokens_print(const token *tokens, const size_t tokensl)
{
  uint16_t lvl = 0;
  fprintf(stderr,"\033[34;2m//\033[0m\033[32;6mTOKENS\033[0m\n");
  fprintf(stderr,"\033[34;1m%-21s\033[0m | \033[32;1m%-4s\033[0m | \033[33;1mcontent\033[0m\n","name","size");
  fprintf(stderr,"--------------------- | ---- | -------\n");
  for (size_t i = 0; i < tokensl; i++) {
    const char *name = token_name(tokens[i].name);
    const token *tk = tokens+i;
    if (tk->name == tBlockEnd && lvl)
      lvl--;
    for (size_t k = 0; k < lvl; k++)
      fwrite("  ",1,2,stderr);
    fprintf(stderr,"\033[34m%-21s\033[0m | \033[32;1m%-4lu\033[0m | '\033[33m",name,tk->size);

    for (size_t j = 0; j < tk->size; j++) {
        char c = tosplchar(tk->start[j]);
        if (c != tk->start[j]) {
            fputs("\033[0m\033[35m",stderr);
            fputc('\\',stderr);
            fputc(c,stderr);
            fputs("\033[0m\033[33m",stderr);
        } else
            fputc(c,stderr);
    }
    fputs("\033[0m'\n",stderr);

    if (tk->name == tBlockStart)
      lvl++;
  }
  fputc('\n',stderr);
}

#endif //TOKEN_DEBUG

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

static inline void
skip_comment_c_oneline(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos+2;
  for (; i < s; i++) {
    if (src[i] == '\n') {
      i++;
      break;
    }
  }
  *pos = i;
}

static inline void
skip_comment_c_multiline(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos+2;
  for (; i < s; i++) {
    if (i+1 < s && src[i] == '*' && src[i+1] == '/') {
      i += 2;
      break;
    }
  }
  *pos = i;
}

static inline void
skip_comment_haskell_oneline(const char *src, size_t *pos, const size_t s)
{
  skip_comment_c_oneline(src,pos,s);
}

static inline void
skip_comment_haskell_multiline(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos+2;
  for (; i < s; i++) {
    if (i+1 < s && src[i] == '-' && src[i+1] == '}') {
      i += 2;
      break;
    }
  }
  *pos = i;
}

static uchar
skip_comment(const char *src, size_t *pos, const size_t s)
{
  size_t i = *pos;
  if (i+1 >= s)
    return 0;
  if (src[i] == '/') {
    if (src[i+1] == '/') {
      skip_comment_c_oneline(src,pos,s);
      return 1;
    } else if (src[i+1] == '*') {
      skip_comment_c_multiline(src,pos,s);
      return 1;
    }
  } else if (src[i] == '{' && src[i+1] == '-') {
    skip_comment_haskell_multiline(src,pos,s);
    return 1;
  } else if (src[i] == '-' && src[i+1] == '-') {
    skip_comment_haskell_oneline(src,pos,s);
    return 1;
  }

  return 0;
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
    if ((i == 0 || isspace(src[i-1])) && skip_comment(src,&i,size)) {
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
      || src[i] == '/'
      )) {
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
  #undef token_found
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
    EXPR_TYPE_SET(cl->flags,EXPR_NPATTERN);
    cl->e = malloc(sizeof(reliq_npattern));
    assert(reliq_ncomp(NULL,0,cl->e) == NULL);
  }

  ERR: ;
  *(reliq_expr*)flexarr_inc(exprs) = *cl;

  END: ;
  *cl = (reliq_expr){0};
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

static reliq_error *
err_field_in_condition(void)
{
  return script_err("conditional: fields cannot be inside conditional expression");
}

static reliq_error *
err_unprecedented_end(size_t pos)
{
  return script_err("block: %lu: unprecedented end of block",pos);
}

typedef struct {
  flexarr *ret; //reliq_expr*
  reliq_expr *current;
  reliq_expr *expr;
  const token *tokens;
  const size_t size;

  uint16_t lvl;
  uint16_t childfields;
  uint16_t childformats;
  uchar foundend : 1;
  uchar first_in_node : 1;
  uchar expr_has_nformat : 1;
  uchar expr_has_eformat : 1;
  uchar lasttext_nonempty : 1;
} tcomp_state;

static reliq_error *from_token_comp(size_t *pos, tcomp_state *st);

static inline void
tcomp_state_cleanvars(tcomp_state *st)
{
  st->first_in_node=1;
  st->expr_has_nformat=0;
  st->expr_has_eformat=0;
  st->lasttext_nonempty=0;
}

static reliq_error *
tcomp_blockstart(size_t *pos, tcomp_state *st)
{
  size_t i = *pos;
  reliq_error *err = NULL;
  reliq_expr *current = st->current;
  reliq_expr *expr = st->expr;
  const token *tokens = st->tokens;
  const size_t size = st->size;

  if (i) {
     if (tokens[i-1].name == tBlockEnd)
      goto_script_seterr(END,"block: %lu: unterminated block before opening of the block",i);
    if (tokens[i-1].name == tText && st->lasttext_nonempty)
      goto_script_seterr(END,"block: %lu: unexpected text before opening of the block",i);
  }
  i++;
  EXPR_TYPE_SET(expr->flags,EXPR_BLOCK);

  tcomp_state nst = *st;
  nst.lvl++;
  err = from_token_comp(&i,&nst);
  expr->e = nst.ret;
  expr->childfields = nst.childfields;
  expr->childformats = nst.childformats;
  if (err)
    goto END;
  if (i >= size || tokens[i].name != tBlockEnd) {
    err = err_unprecedented_end(i);
    goto END;
  }

  if (i+1 < size && tokens[i+1].name == tText)
    goto_script_seterr(END,"block: %lu: unexpected text after ending of the block",i);

  st->childfields += nst.childfields;
  current->childfields += nst.childfields;
  st->childformats += nst.childformats;
  current->childformats += nst.childformats;

  if (current->childfields && current->flags&EXPR_CONDITION_EXPR)
    err = err_field_in_condition();

  END: ;
  *pos = i;
  return err;
}

static inline const char *
tcomp_format_err_name(uchar isnode)
{
  return isnode ? "node" : "expression";
}

static inline reliq_error *
tcomp_format(size_t *pos, const uchar isnode, tcomp_state *st)
{
  size_t i = *pos;
  const size_t size = st->size;
  const token *tokens = st->tokens;
  reliq_error *err = NULL;
  reliq_expr *expr = st->expr;
  reliq_expr *current = st->current;

  if (isnode) {
    if (st->expr_has_nformat)
      goto CANNOT_TWICE;
    st->expr_has_nformat = 1;
  } else {
    if (st->expr_has_eformat) {
      CANNOT_TWICE:
      goto_script_seterr(END,"%lu: format '%c' cannot be specified twice",i,tokens[i].start[tokens[i].size-1]);
    }
    st->expr_has_eformat = 1;
  }

  if (i+1 < size) {
    if (tokens[i+1].name == tText) {
      if (i+2 < size && tokens[i+2].name == tChainLink)
        goto FORMAT_IN_CHAIN;

      st->lasttext_nonempty = 1;
      size_t g=0;
      void *format = &expr->nodef;
      size_t *formatl = &expr->nodefl;
      if (!isnode) {
        format = &expr->exprf;
        formatl = &expr->exprfl;
      }
      err = format_comp(tokens[i+1].start,&g,tokens[i+1].size,format,formatl);
      if (err)
        goto END;
      i++;
    } else if (tokens[i+1].name == tBlockStart) {
      goto_script_seterr(END,"%lu: format '%c' isn't terminated before block",i,tokens[i].start[tokens[i].size-1]);
    } else if (tokens[i+1].name == tChainLink) {
      FORMAT_IN_CHAIN: ;
      goto_script_seterr(END,"%lu: illegal use of %s format inside chain",i,tcomp_format_err_name(isnode));
    }
  }

  if (EXPR_TYPE_IS(expr->flags,EXPR_BLOCK) && isnode)
    EXPR_TYPE_SET(expr->flags,EXPR_SINGULAR);

  if (expr->nodefl
    || !isnode
    ) {
    if (expr->childfields)
      goto_script_seterr(END,"illegal assignment of %s format to block with fields",tcomp_format_err_name(isnode));

    expr->childformats++;
    current->childformats++;
    st->childformats++;
  }

  END: ;
  *pos = i;
  return err;
}

static reliq_error *
tcomp_nodeformat(size_t *pos, tcomp_state *st)
{
  return tcomp_format(pos,1,st);
}

static reliq_error *
tcomp_exprformat(size_t *pos, tcomp_state *st)
{
  return tcomp_format(pos,0,st);
}

static reliq_error *
tcomp_text(size_t *pos, tcomp_state *st)
{
  size_t i = *pos;
  const size_t size = st->size;
  const token *tokens = st->tokens;
  reliq_expr *current = st->current;
  reliq_expr *expr = st->expr;
  char const *start = tokens[i].start;
  reliq_error *err = NULL;

  size_t len = tokens[i].size;
  if (st->first_in_node && tokens[i].start[0] == '.') {
    size_t g=0;
    if ((err = reliq_output_field_comp(start,&g,len,&current->outfield))) //&current->outfield
      return err;
    if (current->outfield.name.b) {
      /*if the above condition is removed protected fields fill work as normal fields
        i.e. '{ .li }; li' will be impossible but '{ . li } / line [1]' will also be,
        and it will break it's functionality*/
      st->childfields++;
      current->childfields++;
    }

    if (current->flags&EXPR_CONDITION_EXPR)
      return err_field_in_condition();

    while_is(isspace,start,g,len);
    start += g;
    len -= g;

    if (!len) {
      if (i+1 >= size || tokens[i+1].name == tNextNode || tokens[i+1].name == tBlockEnd)
        return script_err("field: %lu: empty expression",i);
      return NULL;
    }
  }

  if (i+1 < size && tokens[i+1].name == tBlockStart)
    return script_err("block: %lu: unexpected text before opening of the block",i);
  st->lasttext_nonempty = 1;
  EXPR_TYPE_SET(expr->flags,EXPR_NPATTERN);

  expr->e = malloc(sizeof(reliq_npattern));
  if ((err = reliq_ncomp(start,len,(reliq_npattern*)expr->e))) {
    free(expr->e);
    expr->e = NULL;
  }
  return err;
}

static reliq_error *
tcomp_blockend(size_t *pos, tcomp_state *st)
{
  if (!st->lvl)
    return err_unprecedented_end(*pos);
  st->foundend = 1;
  return NULL;
}

static reliq_error *
tcomp_chainlink(tcomp_state *st)
{
  st->lasttext_nonempty = 0;
  st->first_in_node = 0;
  return add_chainlink(st->current->e,st->expr,0);
}

static reliq_error *
tcomp_nextnode(tcomp_state *st)
{
  reliq_error *err = NULL;
  reliq_expr *current = st->current;

  tcomp_state_cleanvars(st);
  if ((err = add_chainlink(current->e,st->expr,0)))
    return err;

  if (((flexarr*)current->e)->size) {
    current = st->current = (reliq_expr*)flexarr_incz(st->ret);
    current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
    EXPR_TYPE_SET(st->current->flags,EXPR_CHAIN);
  }
  return NULL;
}

static reliq_error *
tcomp_conditional(size_t *pos, const enum tokenName name, tcomp_state *st)
{
  size_t i = *pos;
  reliq_error *err = NULL;
  const token *tokens = st->tokens;
  const size_t size = st->size;
  reliq_expr *current = st->current;
  reliq_expr *expr = st->expr;
  flexarr *ret = st->ret;

  tcomp_state_cleanvars(st);
  if ((err = add_chainlink(current->e,expr,0)))
    return err;

  if (((flexarr*)current->e)->size == 0)
    return script_err("conditional: expected expression before %.*s",(int)tokens[i].size-1,tokens[i].start+1);
  if (i+1 >= size || tokens[i+1].name == tNextNode
    || isconditional(tokens[i+1].name) || tokens[i+1].name == tChainLink)
    return script_err ("conditional: expected expression after %.*s",(int)tokens[i].size-1,tokens[i].start+1);

  current->flags |= from_tokenname_conditional(name);

  reliq_expr *lastret = &((reliq_expr*)ret->v)[ret->size-1];
  if (!EXPR_TYPE_IS(lastret->flags,EXPR_BLOCK_CONDITION)) {
    reliq_output_field field = current->outfield;
    if (current->childfields-(field.name.b != NULL))
      return err_field_in_condition();

    reliq_expr t = *current;
    t.outfield = (reliq_output_field){0};

    *current = (reliq_expr){0};
    current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
    *(reliq_expr*)flexarr_inc(current->e) = t;

    EXPR_TYPE_SET(current->flags,EXPR_BLOCK_CONDITION);
    current->outfield = field;
  }

  current = st->current = (reliq_expr*)flexarr_incz(lastret->e);
  current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  EXPR_TYPE_SET(current->flags,EXPR_CHAIN);
  current->flags |= EXPR_CONDITION_EXPR;
  return NULL;
}

static reliq_error *
tcomp_tokenName(size_t *pos, const enum tokenName name, tcomp_state *st)
{
  if (name == tBlockStart) {
    return tcomp_blockstart(pos,st);
  } else if (name == tBlockEnd) {
    return tcomp_blockend(pos,st);
  } else if (name == tNodeFormat) {
    return tcomp_nodeformat(pos,st);
  } else if (name == tExprFormat) {
    return tcomp_exprformat(pos,st);
  } else if (name == tText) {
    return tcomp_text(pos,st);
  } else if (name == tChainLink) {
    return tcomp_chainlink(st);
  } else if (name == tNextNode) {
    return tcomp_nextnode(st);
  } else if (isconditional(name)) {
    return tcomp_conditional(pos,name,st);
  }
  return NULL;
}

static reliq_error *
from_token_comp(size_t *pos, tcomp_state *st)
{
  size_t i = *pos;
  size_t size = st->size;
  const uint16_t lvl = st->lvl;
  const token *tokens = st->tokens;
  reliq_error *err = NULL;
  st->ret = NULL;

  if (lvl >= RELIQ_MAX_BLOCK_LEVEL)
    return script_err("block: %lu: reached %lu level of recursion",i,lvl);
  if (i >= size)
    return NULL;

  reliq_expr expr = (reliq_expr){0};
  st->ret = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  st->current = flexarr_incz(st->ret);
  st->current->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  EXPR_TYPE_SET(st->current->flags,EXPR_CHAIN);

  st->expr = &expr;
  st->childfields = 0;
  st->childformats = 0;
  tcomp_state_cleanvars(st);

  for (; i < size; i++)
    if ((err = tcomp_tokenName(&i,tokens[i].name,st)) || st->foundend)
      break;

  *pos = i;
  if (err) {
    add_chainlink(st->current->e,&expr,1);
  } else
    err = add_chainlink(st->current->e,&expr,0);

  if (!err)
    flexarr_clearb(st->ret);
  st->expr = NULL;
  return err;
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
reliq_ecomp_intr(const char *src, const size_t size, reliq_expr *expr)
{
  reliq_error *err = NULL;

  token *tokens;
  size_t tokensl = 0;
  if ((err = tokenize(src,size,&tokens,&tokensl)))
    return err;

  #ifdef TOKEN_DEBUG
  tokens_print(tokens,tokensl);
  #endif

  size_t pos=0;
  tcomp_state st = {
    .tokens = tokens,
    .size = tokensl
  };
  err = from_token_comp(&pos,&st);

  tokens_free(tokens,tokensl);

  if (st.ret) {
    #ifdef EXPR_DEBUG
    reliq_expr_print(st.ret,0);
    #endif

    if (err) {
      reliq_expr_free_pre(st.ret);
    } else {
      *expr = (reliq_expr){0};
      EXPR_TYPE_SET(expr->flags,EXPR_BLOCK);
      expr->e = st.ret;
    }
  } else
      *expr = (reliq_expr){0};

  return err;
}

reliq_error *
reliq_ecomp(const char *src, const size_t size, reliq_expr **expr)
{
  reliq_expr e;
  reliq_error *err = reliq_ecomp_intr(src,size,&e);
  if (err)
    return err;
  *expr = memdup(&e,sizeof(reliq_expr));
  return NULL;
}
