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
#include <regex.h>
#include <stdarg.h>

typedef unsigned char uchar;

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "edit.h"
#include "output.h"
#include "html.h"
#include "htmlescapecodes.h"

#define PASSED_INC (1<<8) //!! if increased causes huge allocation see val_mem1 file
#define PATTERN_SIZE_INC (1<<8)
#define PATTRIB_INC 8
#define NODE_MATCHES_INC 8
#define NODE_MATCHES_GROUPS_INC 4
#define HOOK_INC 8
#define FORMAT_INC 8
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

//reliq_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//reliq_npattern flags
#define N_MATCHED_TYPE 0xf
#define N_FULL 1
#define N_SELF 2
#define N_CHILD 3
#define N_DESCENDANT 4
#define N_ANCESTOR 5
#define N_PARENT 6
#define N_RELATIVE_PARENT 7
#define N_SIBLING 8
#define N_SIBLING_PRECEDING 9
#define N_SIBLING_SUBSEQUENT 10
#define N_FULL_SIBLING 11
#define N_FULL_SIBLING_PRECEDING 12
#define N_FULL_SIBLING_SUBSEQUENT 13

#define N_EMPTY 0x10 //ignore matching
#define N_POSITION_ABSOLUTE 0x20

//reliq_match_hook flags
#define H_KINDS 0x1f

#define H_ATTRIBUTES 1
#define H_LEVEL 2
#define H_LEVEL_RELATIVE 3
#define H_CHILD_COUNT 4
#define H_MATCH_INSIDES 5
#define H_CHILD_MATCH 6
#define H_MATCH_END 7
#define H_INDEX 8
#define H_POSITION 9
#define H_POSITION_RELATIVE 10
#define H_FULL 11
#define H_SELF 12
#define H_CHILD 13
#define H_DESCENDANT 14
#define H_ANCESTOR 15
#define H_PARENT 16
#define H_RELATIVE_PARENT 17
#define H_SIBLING 18
#define H_SIBLING_PRECEDING 19
#define H_SIBLING_SUBSEQUENT 20
#define H_FULL_SIBLING 21
#define H_FULL_SIBLING_PRECEDING 22
#define H_FULL_SIBLING_SUBSEQUENT 23

#define H_RANGE 0x20
#define H_PATTERN 0x40
#define H_EXPRS 0x80
#define H_NOARG 0x100

#define H_INVERT 0x200
#define H_FLAG 0x400
#define H_EMPTY 0x800

//reliq_expr istable flags
#define EXPR_TABLE 0x1
#define EXPR_NEWBLOCK 0x2
#define EXPR_NEWCHAIN 0x4
#define EXPR_SINGULAR 0x8

#define ATTRIB_INC (1<<3)
#define RELIQ_NODES_INC (1<<13)

#define MATCHES_TYPE_TAG 1
#define MATCHES_TYPE_HOOK 2
#define MATCHES_TYPE_ATTRIB 3
#define MATCHES_TYPE_GROUPS 4

//reliq_pattern flags
#define RELIQ_PATTERN_TRIM 0x1
#define RELIQ_PATTERN_CASE_INSENSITIVE 0x2
#define RELIQ_PATTERN_INVERT 0x4

#define RELIQ_PATTERN_MATCH ( \
        RELIQ_PATTERN_MATCH_FULL | \
        RELIQ_PATTERN_MATCH_ALL | \
        RELIQ_PATTERN_MATCH_BEGINNING | \
        RELIQ_PATTERN_MATCH_ENDING \
        )

#define RELIQ_PATTERN_MATCH_FULL 0x8
#define RELIQ_PATTERN_MATCH_ALL 0x10
#define RELIQ_PATTERN_MATCH_BEGINNING 0x18
#define RELIQ_PATTERN_MATCH_ENDING 0x20

#define RELIQ_PATTERN_PASS ( \
        RELIQ_PATTERN_PASS_WHOLE | \
        RELIQ_PATTERN_PASS_WORD \
        )

#define RELIQ_PATTERN_PASS_WHOLE 0x40
#define RELIQ_PATTERN_PASS_WORD 0x80

#define RELIQ_PATTERN_TYPE ( \
        RELIQ_PATTERN_TYPE_STR | \
        RELIQ_PATTERN_TYPE_BRE | \
        RELIQ_PATTERN_TYPE_ERE \
        )

#define RELIQ_PATTERN_TYPE_STR 0x100
#define RELIQ_PATTERN_TYPE_BRE 0x200
#define RELIQ_PATTERN_TYPE_ERE 0x300

#define RELIQ_PATTERN_EMPTY 0x400
#define RELIQ_PATTERN_ALL 0x800

static reliq_error *reliq_exec_pre(const reliq *rq, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar isempty, uchar noncol, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    );
reliq_error *reliq_exec_r(reliq *rq, FILE *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_exprs *exprs);

struct reliq_match_hook {
  reliq_str8 name;
  uint16_t flags; //H_
};

const struct reliq_match_hook match_hooks[] = {
    {{"m",1},H_PATTERN|H_MATCH_INSIDES},
    {{"a",1},H_RANGE|H_ATTRIBUTES},
    {{"l",1},H_RANGE|H_LEVEL_RELATIVE},
    {{"L",1},H_RANGE|H_LEVEL},
    {{"c",1},H_RANGE|H_CHILD_COUNT},
    {{"C",1},H_EXPRS|H_CHILD_MATCH},
    {{"p",1},H_RANGE|H_POSITION_RELATIVE},
    {{"P",1},H_RANGE|H_POSITION},
    {{"e",1},H_PATTERN|H_MATCH_END},
    {{"I",1},H_RANGE|H_INDEX},

    {{"match",5},H_PATTERN|H_MATCH_INSIDES},
    {{"attributes",10},H_RANGE|H_ATTRIBUTES},
    {{"levelrelative",13},H_RANGE|H_LEVEL_RELATIVE},
    {{"level",5},H_RANGE|H_LEVEL},
    {{"count",5},H_RANGE|H_CHILD_COUNT},
    {{"childmatch",10},H_EXPRS|H_CHILD_MATCH},
    {{"positionrelative",16},H_RANGE|H_POSITION_RELATIVE},
    {{"position",8},H_RANGE|H_POSITION},
    {{"endmatch",8},H_PATTERN|H_MATCH_END},
    {{"index",5},H_RANGE|H_INDEX},

    {{"desc",4},H_DESCENDANT|H_NOARG|H_FLAG},
    {{"rparent",7},H_RELATIVE_PARENT|H_NOARG|H_FLAG},
    {{"sibl",4},H_SIBLING|H_NOARG|H_FLAG},
    {{"spre",4},H_SIBLING_PRECEDING|H_NOARG|H_FLAG},
    {{"ssub",4},H_SIBLING_SUBSEQUENT|H_NOARG|H_FLAG},
    {{"fsibl",5},H_FULL_SIBLING|H_NOARG|H_FLAG},
    {{"fspre",5},H_FULL_SIBLING_PRECEDING|H_NOARG|H_FLAG},
    {{"fssub",5},H_FULL_SIBLING_SUBSEQUENT|H_NOARG|H_FLAG},

    {{"full",4},H_FULL|H_NOARG|H_FLAG},
    {{"self",4},H_SELF|H_NOARG|H_FLAG},
    {{"child",5},H_CHILD|H_NOARG|H_FLAG},
    {{"descendant",10},H_DESCENDANT|H_NOARG|H_FLAG},
    {{"ancestor",8},H_ANCESTOR|H_NOARG|H_FLAG},
    {{"parent",6},H_PARENT|H_NOARG|H_FLAG},
    {{"relative_parent",15},H_RELATIVE_PARENT|H_NOARG|H_FLAG},
    {{"sibling",7},H_SIBLING|H_NOARG|H_FLAG},
    {{"sibling_preceding",17},H_SIBLING_PRECEDING|H_NOARG|H_FLAG},
    {{"sibling_subsequent",18},H_SIBLING_SUBSEQUENT|H_NOARG|H_FLAG},
    {{"full_sibling",12},H_FULL_SIBLING|H_NOARG|H_FLAG},
    {{"full_sibling_preceding",22},H_FULL_SIBLING_PRECEDING|H_NOARG|H_FLAG},
    {{"full_sibling_subsequent",23},H_FULL_SIBLING_SUBSEQUENT|H_NOARG|H_FLAG},
};

reliq_error *
reliq_set_error(const int code, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  reliq_error *err = malloc(sizeof(reliq_error));
  vsnprintf(err->msg,RELIQ_ERROR_MESSAGE_LENGTH,fmt,ap);
  err->code = code;
  va_end(ap);
  return err;
}

static void
add_compressed(flexarr *dest, reliq_hnode *const hnode, reliq_hnode *const parent)
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = hnode;
  x->parent = parent;
}

static void
add_compressed_blank(flexarr *dest, const enum outfieldCode val1, const void *val2)
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = (reliq_hnode *const)val1;
  x->parent = (void *const)val2;
}

static void
reliq_regcomp_set_flags(uint16_t *flags, const char *src, const size_t len)
{
  for (size_t i = 0; i < len; i++) {
    switch (src[i]) {
      case 't':
          *flags |= RELIQ_PATTERN_TRIM;
          break;
      case 'u':
          *flags &= ~RELIQ_PATTERN_TRIM;
          break;

      case 'i':
          *flags |= RELIQ_PATTERN_CASE_INSENSITIVE;
          break;
      case 'c':
          *flags &= ~RELIQ_PATTERN_CASE_INSENSITIVE;
          break;

      case 'v':
          *flags |= RELIQ_PATTERN_INVERT;
          break;
      case 'n':
          *flags &= ~RELIQ_PATTERN_INVERT;
          break;

      case 'a':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_ALL;
          break;
      case 'f':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_FULL;
          break;
      case 'b':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_BEGINNING;
          break;
      case 'e':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_ENDING;
          break;

      case 'W':
          *flags &= ~RELIQ_PATTERN_PASS;
          *flags |= RELIQ_PATTERN_PASS_WHOLE;
          break;
      case 'w':
          *flags &= ~RELIQ_PATTERN_PASS;
          *flags |= RELIQ_PATTERN_PASS_WORD;
          break;

      case 's':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_STR;
          break;
      case 'B':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_BRE;
          break;
      case 'E':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_ERE;
          break;
      /*case 'P':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_PCRE;
          break;*/
    }
  }
}

static void
reliq_regcomp_get_flags(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char *flags)
{
  size_t p = *pos;
  pattern->flags = RELIQ_PATTERN_TRIM|RELIQ_PATTERN_PASS_WHOLE|RELIQ_PATTERN_MATCH_FULL|RELIQ_PATTERN_TYPE_STR;
  pattern->range.s= 0;

  if (flags)
    reliq_regcomp_set_flags(&pattern->flags,flags,strlen(flags));

  if (src[p] == '\'' || src[p] == '"' || src[p] == '*')
    return;

  while (p < size && isalpha(src[p]))
    p++;
  if (p >= size || src[p] != '>')
    return;

  reliq_regcomp_set_flags(&pattern->flags,src+*pos,p-*pos);

  *pos = p+1;
  return;
}

static uchar
strclass_attrib(char c)
{
  if (isalnum(c))
    return 1;
  if (c == '_' || c == '-' || c == ':')
    return 1;
  return 0;
}

static reliq_error *
reliq_regcomp_add_pattern(reliq_pattern *pattern, const char *src, const size_t size, uchar (*checkstrclass)(char))
{
  uint16_t match = pattern->flags&RELIQ_PATTERN_MATCH,
    type = pattern->flags&RELIQ_PATTERN_TYPE;

  if (!size) {
    pattern->flags |= RELIQ_PATTERN_EMPTY;
    return NULL;
  }

  if (type == RELIQ_PATTERN_TYPE_STR) {
    if (checkstrclass)
      for (size_t i = 0; i < size; i++)
        if (!checkstrclass(src[i]))
          return script_err("pattern %lu: '%c' is a character impossible to find in searched field",i,src[i]);
    pattern->match.str.b = memdup(src,size);
    pattern->match.str.s = size;
  } else {
    int regexflags = REG_NOSUB;
    if (pattern->flags&RELIQ_PATTERN_CASE_INSENSITIVE)
      regexflags |= REG_ICASE;
    if (type == RELIQ_PATTERN_TYPE_ERE)
      regexflags |= REG_EXTENDED;

    size_t addedspace = 0;
    uchar fullmatch = (match == RELIQ_PATTERN_MATCH_FULL) ? 1 : 0;

    if (fullmatch)
      addedspace = 2;
    else if (match == RELIQ_PATTERN_MATCH_BEGINNING || match == RELIQ_PATTERN_MATCH_ENDING)
      addedspace = 1;

    char *tmp = malloc(size+addedspace+1);
    size_t p = 0;

    if (fullmatch || match == RELIQ_PATTERN_MATCH_BEGINNING)
      tmp[p++] = '^';

    memcpy(tmp+p,src,size);
    p += size;

    if (fullmatch || match == RELIQ_PATTERN_MATCH_ENDING)
      tmp[p++] = '$';
    tmp[p] = 0;

    int r = regcomp(&pattern->match.reg,tmp,regexflags);
    free(tmp);
    if (r != 0)
      return script_err("pattern: regcomp: could not compile pattern");
  }
  return NULL;
}

static void
reliq_regfree(reliq_pattern *pattern)
{
  if (!pattern)
    return;

  range_free(&pattern->range);

  if (pattern->flags&RELIQ_PATTERN_EMPTY || pattern->flags&RELIQ_PATTERN_ALL)
    return;

  if ((pattern->flags&RELIQ_PATTERN_TYPE) == RELIQ_PATTERN_TYPE_STR) {
    if (pattern->match.str.b)
      free(pattern->match.str.b);
  } else
    regfree(&pattern->match.reg);
}

static reliq_error *
reliq_regcomp(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char delim, const char *flags, uchar (*checkstrclass)(char))
{
  reliq_error *err=NULL;
  size_t i = *pos;

  memset(pattern,0,sizeof(reliq_pattern));
  reliq_regcomp_get_flags(pattern,src,&i,size,flags);

  if (i && i < size && src[i-1] == '>' && src[i] == '[') {
    if ((err = range_comp(src,&i,size,&pattern->range)))
      goto END;
    if (i >= size || src[i] == delim || isspace(src[i])) {
      pattern->flags |= RELIQ_PATTERN_ALL;
      goto END;
    }
  }

  if ((i+1 == size || (i+1 < size &&
    (isspace(src[i+1]) || src[i+1] == delim)))
    && src[i] == '*') {
    i++;
    pattern->flags |= RELIQ_PATTERN_ALL;
    goto END;
  }

  char *str;
  size_t strl;
  if ((err = get_quoted(src,&i,size,delim,&str,&strl)))
    goto END;

  err = reliq_regcomp_add_pattern(pattern,str,strl,checkstrclass);
  free(str);
  END: ;
  *pos = i;
  if (err)
    reliq_regfree(pattern);
  return err;
}

static int
reliq_regexec_match_str(const reliq_pattern *pattern, reliq_cstr *str)
{
  reliq_pattern const *p = pattern;
  uint16_t match = p->flags&RELIQ_PATTERN_MATCH,
    icase = p->flags&RELIQ_PATTERN_CASE_INSENSITIVE;

  if (!p->match.str.s)
    return 1;
  if (!str->s)
    return 0;

  switch (match) {
    case RELIQ_PATTERN_MATCH_ALL:
      if (icase) {
        if (memcasemem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
          return 1;
      } else if (memmem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_FULL:
      if (str->s != p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,str->s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,str->s) == 0)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_BEGINNING:
      if (str->s < p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,p->match.str.s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,p->match.str.s) == 0)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_ENDING:
      if (str->s < p->match.str.s)
        return 0;
      const char *start = str->b+(str->s-p->match.str.s);
      size_t len = p->match.str.s;
      if (icase) {
        if (memcasecmp(start,p->match.str.b,len) == 0)
          return 1;
      } else if (memcmp(start,p->match.str.b,len) == 0)
        return 1;
      break;
  }
  return 0;
}

static int
reliq_regexec_match_pattern(const reliq_pattern *pattern, reliq_cstr *str)
{
  uint16_t type = pattern->flags&RELIQ_PATTERN_TYPE;
  if (type == RELIQ_PATTERN_TYPE_STR) {
    return reliq_regexec_match_str(pattern,str);
  } else {
    if (!str->s)
      return 0;

    regmatch_t pmatch;
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)str->s;

    if (regexec(&pattern->match.reg,str->b,1,&pmatch,REG_STARTEND) == 0)
      return 1;
  }
  return 0;
}

static int
reliq_regexec_match_word(const reliq_pattern *pattern, reliq_cstr *str)
{
  const char *ptr = str->b;
  size_t plen = str->s;
  char const *saveptr,*word;
  size_t saveptrlen,wordlen;

  while (1) {
    memwordtok_r(ptr,plen,(void const**)&saveptr,&saveptrlen,(void const**)&word,&wordlen);
    if (!word)
      return 0;

    reliq_cstr t;
    t.b = word;
    t.s = wordlen;
    if (reliq_regexec_match_pattern(pattern,&t))
      return 1;

    ptr = NULL;
  }
  return 0;
}

static int
reliq_regexec(const reliq_pattern *pattern, const char *src, const size_t size)
{
  uint16_t pass = pattern->flags&RELIQ_PATTERN_PASS;
  uchar invert = (pattern->flags&RELIQ_PATTERN_INVERT) ? 1 : 0;
  if ((!range_match(size,&pattern->range,-1)))
    return invert;

  if (pattern->flags&RELIQ_PATTERN_ALL)
    return !invert;

  if (pattern->flags&RELIQ_PATTERN_EMPTY)
    return (size == 0) ? !invert : invert;

  if (!src)
    return invert;

  reliq_cstr str = {src,size};

  if (pass == RELIQ_PATTERN_PASS_WORD)
    return reliq_regexec_match_word(pattern,&str)^invert;

  if (pattern->flags&RELIQ_PATTERN_TRIM)
    memtrim((void const**)&str.b,&str.s,src,size);

  return reliq_regexec_match_pattern(pattern,&str)^invert;
}

static void
pattrib_free(struct reliq_pattrib *attrib) {
  if (!attrib)
    return;
  reliq_regfree(&attrib->r[0]);
  if (attrib->flags&A_VAL_MATTERS)
    reliq_regfree(&attrib->r[1]);
  range_free(&attrib->position);
}

static void
reliq_free_hook(reliq_hook *hook)
{
  if (hook->flags&H_RANGE) {
    range_free(&hook->match.range);
  } if (hook->flags&H_EXPRS) {
    reliq_efree(&hook->match.exprs);
  } else if (hook->flags&H_PATTERN)
    reliq_regfree(&hook->match.pattern);
}

static void reliq_free_matches(reliq_node_matches *matches);

static void
reliq_free_matches_group(reliq_node_matches_groups *groups)
{
  const size_t size = groups->size;
  reliq_node_matches *list = groups->list;
  for (size_t i = 0; i < size; i++)
    reliq_free_matches(&list[i]);
  free(list);
}

static void
reliq_free_matches(reliq_node_matches *matches)
{
  const size_t size = matches->size;
  reliq_node_matches_node *list = matches->list;
  for (size_t i = 0; i < size; i++) {
    reliq_node_matches_node *node = &list[i];
    switch (node->type) {
      case MATCHES_TYPE_TAG:
        reliq_regfree(node->data.tag);
        free(node->data.tag);
        break;
      case MATCHES_TYPE_HOOK:
        reliq_free_hook(node->data.hook);
        free(node->data.hook);
        break;
      case MATCHES_TYPE_ATTRIB:
        pattrib_free(node->data.attrib);
        free(node->data.attrib);
        break;
      case MATCHES_TYPE_GROUPS:
        reliq_free_matches_group(node->data.groups);
        free(node->data.groups);
        break;
    }
  }
  free(list);
}

void
reliq_nfree(reliq_npattern *nodep)
{
  if (nodep == NULL)
    return;

  range_free(&nodep->position);

  if (nodep->flags&N_EMPTY)
    return;

  reliq_free_matches(&nodep->matches);
}

int
reliq_free(reliq *rq)
{
  if (rq == NULL)
    return -1;
  const size_t size = rq->nodesl;
  for (size_t i = 0; i < size; i++)
    free(rq->nodes[i].attribs);
  if (rq->nodesl)
    free(rq->nodes);
  if (rq->freedata)
      return (*rq->freedata)((void*)rq->data,rq->datal);
  return 0;
}

static int
pattrib_match(const reliq_hnode *hnode, const struct reliq_pattrib *attrib)
{
  const reliq_cstr_pair *a = hnode->attribs;
  uchar found = 0;
  const size_t size = hnode->attribsl;
  for (uint16_t i = 0; i < size; i++) {
    if (!range_match(i,&attrib->position,hnode->attribsl-1))
      continue;

    if (!reliq_regexec(&attrib->r[0],a[i].f.b,a[i].f.s))
      continue;

    if (attrib->flags&A_VAL_MATTERS && !reliq_regexec(&attrib->r[1],a[i].s.b,a[i].s.s))
      continue;

    found = 1;
    break;
  }
  if (((attrib->flags&A_INVERT)!=A_INVERT)^found)
    return 0;
  return 1;
}

static int
reliq_match_hook(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_hook *hook)
{
  char const *src = NULL;
  size_t srcl = 0;
  uint16_t flags = hook->flags;
  const uchar invert = (flags&H_INVERT) ? 1 : 0;

  switch (flags&H_KINDS) {
    case H_ATTRIBUTES:
      srcl = hnode->attribsl;
      break;
    case H_LEVEL_RELATIVE:
      if (parent) {
        if (hnode->lvl < parent->lvl) {
          srcl = parent->lvl-hnode->lvl;
        } else
          srcl = hnode->lvl-parent->lvl;
      } else
        srcl = hnode->lvl;
      break;
    case H_LEVEL:
      srcl = hnode->lvl;
      break;
    case H_CHILD_COUNT:
      srcl = hnode->desc_count;
      break;
    case H_MATCH_INSIDES:
      src = hnode->insides.b;
      srcl = hnode->insides.s;
      break;
    case H_POSITION_RELATIVE:
      if (parent) {
        if (hnode < parent) {
          srcl = parent-hnode;
        } else
          srcl = hnode-parent;
      } else
        srcl = hnode-rq->nodes;
      break;
    case H_POSITION:
      srcl = hnode-rq->nodes;
      break;
    case H_MATCH_END:
      src = hnode->insides.b+hnode->insides.s;
      srcl = hnode->all.s-(hnode->insides.b-hnode->all.b)-hnode->insides.s;
      if (srcl >= 2) {
        src++;
        srcl -= 2;
      }
      if (!hnode->insides.b) {
        src = NULL;
        srcl = 0;
      }
      break;
    case H_INDEX:
      srcl = hnode->all.b-rq->data;
      break;
  }

  if (flags&H_RANGE) {
    if ((!range_match(srcl,&hook->match.range,-1))^invert)
      return 0;
  } else if (flags&H_PATTERN) {
    if ((!reliq_regexec(&hook->match.pattern,src,srcl))^invert)
      return 0;
  } else if ((flags&H_KINDS) == H_CHILD_MATCH && flags&H_EXPRS) {
    reliq r;
    memset(&r,0,sizeof(reliq));
    r.nodes = (reliq_hnode*)hnode;
    r.nodesl = hnode->desc_count+1;
    r.parent = hnode;

    size_t compressedl = 0;
    reliq_error *err = reliq_exec_r(&r,NULL,NULL,&compressedl,&hook->match.exprs);
    if (err)
      free(err);
    if ((err || !compressedl)^invert)
      return 0;
  }
  return 1;
}

static int reliq_node_matched_match(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_node_matches *matches);

static int
reliq_node_matched_groups_match(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_node_matches_groups *groups)
{
  const size_t size = groups->size;
  reliq_node_matches *list = groups->list;
  for (size_t i = 0; i < size; i++)
    if (reliq_node_matched_match(rq,hnode,parent,&list[i]))
      return 1;
  return 0;
}

static int
reliq_node_matched_match(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_node_matches *matches)
{
  const size_t size = matches->size;
  reliq_node_matches_node *list = matches->list;
  for (size_t i = 0; i < size; i++) {
    switch (list[i].type) {
      case MATCHES_TYPE_TAG:
        if (!reliq_regexec(list[i].data.tag,hnode->tag.b,hnode->tag.s))
          return 0;
        break;
      case MATCHES_TYPE_HOOK:
        if (!reliq_match_hook(rq,hnode,parent,list[i].data.hook))
          return 0;
        break;
      case MATCHES_TYPE_ATTRIB:
        if (!pattrib_match(hnode,list[i].data.attrib))
          return 0;
        break;
      case MATCHES_TYPE_GROUPS:
        if (!reliq_node_matched_groups_match(rq,hnode,parent,list[i].data.groups))
          return 0;
        break;
    }
  }
  return 1;
}

int
reliq_nexec(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_npattern *nodep)
{
  if (nodep->flags&N_EMPTY)
    return 1;
  return reliq_node_matched_match(rq,hnode,parent,&nodep->matches);
}

static void
reliq_match_add(const reliq *rq, reliq_hnode const *hnode, reliq_hnode const *parent, reliq_npattern const *nodep, flexarr *dest, uint32_t *found)
{
  if (!reliq_nexec(rq,hnode,parent,nodep))
    return;
  add_compressed(dest,(reliq_hnode *const)hnode,(reliq_hnode *const)parent);
  (*found)++;
}

#define PC_UNTRIM 0x1
#define PC_DECODE 0x2

static void
print_chars(char const *src, size_t size, const uint8_t flags, FILE *outfile)
{
  if (!(flags&PC_UNTRIM))
    memtrim((void const**)&src,&size,src,size);
  if (!size)
    return;
  if (flags&PC_DECODE) {
    htmlescapecodes_file(src,size,outfile);
  } else
    fwrite(src,1,size,outfile);
}

static void
print_attribs(const reliq_hnode *hnode, const uint8_t flags, FILE *outfile)
{
  reliq_cstr_pair *a = hnode->attribs;
  const size_t size = hnode->attribsl;
  for (uint16_t j = 0; j < size; j++) {
    fputc(' ',outfile);
    fwrite(a[j].f.b,1,a[j].f.s,outfile);
    fputs("=\"",outfile);
    print_chars(a[j].s.b,a[j].s.s,flags,outfile);
    fputc('"',outfile);
  }
}

static void
print_attrib_value(const reliq_cstr_pair *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uint8_t flags, FILE *outfile)
{
  if (num != -1) {
    if ((size_t)num < attribsl)
      print_chars(attribs[num].s.b,attribs[num].s.s,flags,outfile);
  } else if (textl != 0) {
    for (size_t i = 0; i < attribsl; i++)
      if (memcomp(attribs[i].f.b,text,textl,attribs[i].f.s))
        print_chars(attribs[i].s.b,attribs[i].s.s,flags,outfile);
  } else for (size_t i = 0; i < attribsl; i++) {
    print_chars(attribs[i].s.b,attribs[i].s.s,flags,outfile);
    fputc('"',outfile);
  }
}

static void
print_text(const reliq_hnode *nodes, const reliq_hnode *hnode, uint8_t flags, FILE *outfile, uchar recursive)
{
  char const *start = hnode->insides.b;
  size_t end;
  flags |= PC_UNTRIM;

  const size_t size = hnode->desc_count;
  for (size_t i = 1; i <= size; i++) {
    const reliq_hnode *n = hnode+i;

    end = n->all.b-start;
    if (end)
      print_chars(start,end,flags,outfile);

    if (recursive)
      print_text(nodes,n,flags,outfile,recursive);

    i += n->desc_count;
    start = n->all.b+n->all.s;
  }

  end = hnode->insides.s-(start-hnode->insides.b);
  if (end)
    print_chars(start,end,flags,outfile);
}

void
reliq_printf(FILE *outfile, const char *format, const size_t formatl, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq *rq)
{
  size_t i = 0;
  char const *text=NULL;
  size_t textl=0;
  int num = -1;
  while (i < formatl) {
    if (format[i] == '\\') {
      size_t resultl,traversed;
      char result[8];
      i++;
      splchar3(format+i,formatl-i,result,&resultl,&traversed);
      if (resultl != 0) {
        fwrite(result,resultl,1,outfile);
        i += traversed;
        continue;
      } else
        i--;
    }
    if (format[i] == '%') {
      if (++i >= formatl)
        break;
      if (isdigit(format[i])) {
        num = number_handle(format,&i,formatl);
      } else if (format[i] == '(') {
        text = ++i+format;
        char *t = memchr(format+i,')',formatl-i);
        if (!t)
          return;
        textl = t-(format+i);
        i = t-format+1;
      }

      uint8_t printflags=0, //PC_
        endinsides=0;
      char const *src;
      size_t srcl;

      REPEAT: ;
      if (i >= formatl)
          return;
      switch (format[i++]) {
        case '%': fputc('%',outfile); break;
        case 'U': printflags |= PC_UNTRIM; goto REPEAT; break;
        case 'D': printflags |= PC_DECODE; goto REPEAT; break;
        case 'i': print_chars(hnode->insides.b,hnode->insides.s,printflags,outfile); break;
        case 't': print_text(rq->nodes,hnode,printflags,outfile,0); break;
        case 'T': print_text(rq->nodes,hnode,printflags,outfile,1); break;
        case 'l': {
          uint16_t lvl = hnode->lvl;
          if (parent) {
            if (lvl < parent->lvl) {
              lvl = parent->lvl-lvl; //happens when passed from ancestor
            } else
              lvl -= parent->lvl;
          }
          print_uint(lvl,outfile);
          }
          break;
        case 'L': print_uint(hnode->lvl,outfile); break;
        case 'a':
          print_attribs(hnode,printflags,outfile); break;
        case 'v':
          print_attrib_value(hnode->attribs,hnode->attribsl,text,textl,num,printflags,outfile);
          break;
        case 's': print_uint(hnode->all.s,outfile); break;
        case 'c': print_uint(hnode->desc_count,outfile); break;
        case 'C': print_chars(hnode->all.b,hnode->all.s,printflags|PC_UNTRIM,outfile); break;
        case 'S':
          src = hnode->all.b;
          if (hnode->insides.b) {
            srcl = hnode->insides.b-hnode->all.b;
          } else
            srcl = hnode->all.s;
          print_chars(src,srcl,printflags|PC_UNTRIM,outfile);
          break;
        case 'e':
          endinsides = 1;
        case 'E':
          if (!hnode->insides.b)
            break;
          srcl = hnode->all.s-(hnode->insides.b-hnode->all.b)-hnode->insides.s;
          src = hnode->insides.b+hnode->insides.s;
          if (!srcl)
            break;
          if (endinsides) {
            if (srcl < 2)
              break;
            src++;
            srcl -= 2;
          }

          print_chars(src,srcl,printflags|(endinsides ? 0 : PC_UNTRIM),outfile);
          break;
        case 'I': print_uint(hnode->all.b-rq->data,outfile); break;
        case 'p': {
          uint32_t pos = hnode-rq->nodes;
          if (parent) {
            if (hnode < parent) {
              pos = parent-hnode;
            } else
              pos = hnode-parent;
          }
          print_uint(pos,outfile);
          }
          break;
        case 'P': print_uint(hnode-rq->nodes,outfile); break;
        case 'n': fwrite(hnode->tag.b,1,hnode->tag.s,outfile); break;
      }
      continue;
    }
    fputc(format[i++],outfile);
  }
}

void
reliq_print(FILE *outfile, const reliq_hnode *hnode)
{
  fwrite(hnode->all.b,1,hnode->all.s,outfile);
  fputc('\n',outfile);
}

static reliq_error *
format_comp(const char *src, size_t *pos, const size_t size,
#ifdef RELIQ_EDITING
  reliq_format_func **format,
#else
  char **format,
#endif
  size_t *formatl)
{
  reliq_error *err = NULL;
  size_t i = *pos;
  if (i >= size || !src)
    goto END;
  #ifndef RELIQ_EDITING
  while_is(isspace,src,i,size);

  if (src[i] != '"' && src[i] != '\'')
    goto END;

  if ((err = get_quoted(src,&i,size,' ',format,formatl)))
    goto END;
  #else
  flexarr *f = flexarr_init(sizeof(reliq_format_func),FORMAT_INC);
  err = format_get_funcs(f,src,&i,size);
  flexarr_conv(f,(void**)format,formatl);
  #endif
  END: ;
  *pos = i;
  return err;
}

static reliq_error *
exprs_check_chain(const reliq_exprs *exprs, const uchar noaccesshooks)
{
  if (!exprs->s)
    return NULL;
  if (exprs->s > 1)
    goto ERR;

  flexarr *chain = exprs->b[0].e;
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

static const char *
match_hook_unexpected_argument(const uint16_t flags)
{
    if (flags&H_PATTERN)
      return "hook \"%.*s\" expected pattern argument";
    if (flags&H_EXPRS)
      return "hook \"%.*s\" expected node argument";
    if (flags&H_RANGE)
      return "hook \"%.*s\" expected list argument";
    if (flags&H_NOARG)
      return "hook \"%.*s\" unexpected argument";
    return NULL;
}

static uchar
hook_handle_isname(char c)
{
  if (c == '_' || c == '-')
    return 1;
  return isalpha(c);
}

static reliq_error *
match_hook_handle(const char *src, size_t *pos, const size_t size, reliq_hook *out_hook, const uchar invert, uint8_t *nodeflags)
{
  reliq_error *err = NULL;
  size_t p=*pos;
  const size_t prevpos = p;
  const char *func_name = src+p;
  out_hook->flags = 0;

  while_is(hook_handle_isname,src,p,size);

  size_t func_len = p-prevpos;

  if (p >= size || !func_len || src[p] != '@') {
    p = prevpos;
    goto ERR;
  }
  p++;

  char found = 0;
  size_t i = 0;
  for (; i < LENGTH(match_hooks); i++) {
    if (memcomp(match_hooks[i].name.b,func_name,match_hooks[i].name.s,func_len)) {
      found = 1;
      break;
    }
  }
  if (!found)
    goto_script_seterr(ERR,"hook \"%.*s\" does not exists",(int)func_len,func_name);

  reliq_hook hook = (reliq_hook){0};
  hook.flags = match_hooks[i].flags;

  #define HOOK_EXPECT(x) if (!(hook.flags&(x))) \
      goto_script_seterr(ERR,match_hook_unexpected_argument(hook.flags),(int)func_len,func_name)

  char firstchar = 0;
  if (p >= size) {
    if (!(hook.flags&H_NOARG))
      goto_script_seterr(ERR,"hook \"%.*s\" expected argument",func_len,func_name);
  } else
    firstchar = src[p];

  if (!firstchar || isspace(firstchar)) {
    HOOK_EXPECT(H_NOARG);

    hook.flags |= H_EMPTY;
    if (!nodeflags)
      goto END;

    *nodeflags = *nodeflags&(~N_MATCHED_TYPE);
    switch (hook.flags&H_KINDS) {
      case H_FULL: *nodeflags |= N_FULL; break;
      case H_SELF: *nodeflags |= N_SELF; break;
      case H_CHILD: *nodeflags |= N_CHILD; break;
      case H_DESCENDANT: *nodeflags |= N_DESCENDANT; break;
      case H_ANCESTOR: *nodeflags |= N_ANCESTOR; break;
      case H_PARENT: *nodeflags |= N_PARENT; break;
      case H_RELATIVE_PARENT: *nodeflags |= N_RELATIVE_PARENT; break;
      case H_SIBLING: *nodeflags |= N_SIBLING; break;
      case H_SIBLING_PRECEDING: *nodeflags |= N_SIBLING_PRECEDING; break;
      case H_SIBLING_SUBSEQUENT: *nodeflags |= N_SIBLING_SUBSEQUENT; break;
      case H_FULL_SIBLING: *nodeflags |= N_FULL_SIBLING; break;
      case H_FULL_SIBLING_PRECEDING: *nodeflags |= N_FULL_SIBLING_PRECEDING; break;
      case H_FULL_SIBLING_SUBSEQUENT: *nodeflags |= N_FULL_SIBLING_SUBSEQUENT; break;
    }
  } else if (src[p] == '[') {
    HOOK_EXPECT(H_RANGE);
    if ((err = range_comp(src,&p,size,&hook.match.range)))
      goto ERR;
  } else if (hook.flags&H_EXPRS) {
     if (src[p] != '"' && src[p] != '\'')
       goto_script_seterr(ERR,match_hook_unexpected_argument(hook.flags),(int)func_len,func_name);
     char *str;
     size_t strl;
     if ((err = get_quoted(src,&p,size,' ',&str,&strl)) || !strl)
       goto ERR;
     err = reliq_ecomp(str,strl,&hook.match.exprs);
     free(str);
     if (err)
       goto ERR;
     if ((err = exprs_check_chain(&hook.match.exprs,0))) {
       reliq_efree(&hook.match.exprs);
       goto ERR;
     }
  } else {
    HOOK_EXPECT(H_PATTERN);
    char *rflags = "uWcas";
    if ((hook.flags&H_KINDS) == H_MATCH_END)
      rflags = "uWcnfs";
    if ((err = reliq_regcomp(&hook.match.pattern,src,&p,size,' ',rflags,NULL)))
      goto ERR;
    if (!hook.match.pattern.range.s && hook.match.pattern.flags&RELIQ_PATTERN_ALL) { //ignore if it matches everything
      reliq_regfree(&hook.match.pattern);
      goto ERR;
    }
  }

  END:
  if (invert)
    hook.flags |= H_INVERT;
  memcpy(out_hook,&hook,sizeof(hook));
  ERR:
  *pos = p;
  return err;
}

static void
reliq_node_matches_node_add(flexarr *arr, uchar type, void *data, const size_t size)
{
  reliq_node_matches_node *new = flexarr_inc(arr);
  new->type = type;
  new->data.hook = memdup(data,size);
}

static void
free_node_matches_flexarr(flexarr *groups_matches)
{
  reliq_node_matches *matchesv = (reliq_node_matches*)groups_matches->v;
  const size_t size = groups_matches->size;
  for (size_t i = 0; i < size; i++)
    reliq_free_matches(&matchesv[i]);
  flexarr_free(groups_matches);
}

static reliq_error *
get_node_matches(const char *src, size_t *pos, const size_t size, const uint16_t lvl, reliq_node_matches *matches, uchar *hastag, reliq_range *position, uint8_t *nodeflags)
{
  if (lvl >= RELIQ_MAX_GROUP_LEVEL)
    return script_err("node: %lu: reached %lu level of recursion",*pos,lvl);
  reliq_error *err = NULL;
  struct reliq_pattrib attrib;
  reliq_hook hook;
  flexarr *result = flexarr_init(sizeof(reliq_node_matches),NODE_MATCHES_INC);
  matches->size = 0;
  matches->list = NULL;

  uchar tofree=0,fullmode=0;
  if (position)
    fullmode = 1;

  size_t i = *pos;
  char shortcut;
  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    shortcut = 0;

    if (src[i] == ')') {
      if (fullmode)
        err = script_err("node: %lu: unexpected '%c'",i,src[i]);
      i++;
      break;
    }

    if (src[i] == '(') {
      if (++i >= size) {
        END_OF_RANGE: ;
        err = script_err("node: %lu: unprecedented end of group",i-1);
        break;
      }

      flexarr *groups_matches = flexarr_init(sizeof(reliq_node_matches),NODE_MATCHES_INC);
      uchar wastag = 0;

      while (1) {
        uchar tag = *hastag;
        reliq_node_matches *match = flexarr_inc(groups_matches);

        if ((err = get_node_matches(src,&i,size,lvl+1,match,&tag,NULL, NULL))) {
          flexarr_dec(groups_matches);
          goto END;
        }
        if (!*hastag && wastag && !tag) {
          err = script_err("node: %lu: if one group specifies tag then the rest has too",i);
          goto END;
        }
        wastag = tag;

        if (i >= size || src[i] != '(') {
          size_t lastindex = i-1;
          if (i >= size)
            lastindex = size-1;
          if (i >= size+1 || src[lastindex] != ')') {
            free_node_matches_flexarr(groups_matches);
            goto END_OF_RANGE;
          }
          if (i >= size)
            i++;
          break;
        }

        /*if (match->size < 1) {
          free_node_matches_flexarr(groups_matches);
          err = script_err("node: empty group will always pass");
          break;
        }*/ //future warning

        i++;
      }

      if (!*hastag)
        *hastag = wastag;

      /*if (groups_matches->size < 2) {
        free_node_matches_flexarr(groups_matches);
        err = script_err("node: groups must have at least 2 groups to affect anything");
        break;
      }*/ //future warning

      reliq_node_matches_groups groups;
      flexarr_conv(groups_matches,(void**)&groups.list,&groups.size);
      reliq_node_matches_node_add(result,MATCHES_TYPE_GROUPS,&groups,sizeof(reliq_hook));
      continue;
    }

    if (!*hastag)
      goto GET_TAG;

    memset(&attrib,0,sizeof(struct reliq_pattrib));
    tofree = 1;

    if (src[i] == '+') {
      attrib.flags &= ~A_INVERT;
      i++;
    } else if (src[i] == '-') {
      attrib.flags |= A_INVERT;
      i++;
    } else if (i+1 < size && src[i] == '\\' && (src[i+1] == '+' || src[i+1] == '-'))
      i++;

    if (isalpha(src[i])) {
      size_t prev = i;
      if ((err = match_hook_handle(src,&i,size,&hook,attrib.flags&A_INVERT,nodeflags)))
        break;
      if (i != prev) {
        if (!fullmode && hook.flags&H_FLAG) {
          err = script_err("node: %lu: groups cannot have access hooks",i);
          break;
        }

        if (hook.flags&H_EMPTY) {
          tofree = 0;
          continue;
        }
        reliq_node_matches_node_add(result,MATCHES_TYPE_HOOK,&hook,sizeof(reliq_hook));
        tofree = 0;
        continue;
      }
    }

    if (i >= size)
      break;

    if (src[i] == '.' || src[i] == '#') {
      shortcut = src[i++];
    } else if (i+1 < size && src[i] == '\\' && (src[i+1] == '.' || src[i+1] == '#'))
      i++;

    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    GET_TAG: ;
    if (src[i] == '[') {
      if ((err = range_comp(src,&i,size,&attrib.position)))
        break;
      if (i >= size || isspace(src[i])) {
        if (!fullmode) {
          err = script_err("node: %lu: groups cannot have position",i);
          break;
        }
        if (position->s) {
          err = script_err("node: %lu: position already declared",i);
          break;
        }
        memcpy(position,&attrib.position,sizeof(reliq_range));
        tofree = 0;
        if (!*hastag)
          *nodeflags |= N_POSITION_ABSOLUTE;
        continue;
      } else if (!*hastag)
          goto GET_TAG_NAME;
    } else if (*hastag && i+1 < size && src[i] == '\\' && src[i+1] == '[')
      i++;

    if (!*hastag) {
      GET_TAG_NAME: ;
      reliq_pattern tag;
      if ((err = reliq_regcomp(&tag,src,&i,size,' ',NULL,strclass_attrib)))
        break;
      *hastag = 1;
      reliq_node_matches_node_add(result,MATCHES_TYPE_TAG,&tag,sizeof(reliq_pattern));
      continue;
    }

    if (i >= size)
      break;

    if (shortcut == '.' || shortcut == '#') {
      char *t_name = (shortcut == '.') ? "class" : "id";
      size_t t_pos=0,t_size=(shortcut == '.' ? 5 : 2);
      if ((err = reliq_regcomp(&attrib.r[0],t_name,&t_pos,t_size,' ',"uWsfi",strclass_attrib)))
        break;

      if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',"uwsf",NULL)))
        break;
      attrib.flags |= A_VAL_MATTERS;
    } else {
      if ((err = reliq_regcomp(&attrib.r[0],src,&i,size,'=',NULL,strclass_attrib))) //!
        break;

      while_is(isspace,src,i,size);
      if (i >= size)
        goto ADD_SKIP;
      if (src[i] == '=') {
        i++;
        while_is(isspace,src,i,size);
        if (i >= size)
          break;

        if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',NULL,NULL)))
          break;
        attrib.flags |= A_VAL_MATTERS;
      } else {
        attrib.flags &= ~A_VAL_MATTERS;
        goto ADD_SKIP;
      }
    }

    if (i < size && src[i] != '+' && src[i] != '-')
      i++;
    ADD_SKIP: ;

    reliq_node_matches_node_add(result,MATCHES_TYPE_ATTRIB,&attrib,sizeof(struct reliq_pattrib));
    tofree = 0;
  }
  END: ;
  if (tofree)
    pattrib_free(&attrib);

  flexarr_conv(result,(void**)&matches->list,&matches->size);
  *pos = i;
  return err;
}

reliq_error *
reliq_ncomp(const char *script, const size_t size, reliq_npattern *nodep)
{
  if (!nodep)
    return NULL;
  reliq_error *err = NULL;
  size_t pos=0;

  memset(nodep,0,sizeof(reliq_npattern));
  nodep->flags |= N_FULL;
  if (pos >= size) {
    nodep->flags |= N_EMPTY;
    if (pos)
      goto END_FREE;
    return NULL;
  }

  uchar hastag=0;

  err = get_node_matches(script,&pos,size,0,&nodep->matches,&hastag,&nodep->position,&nodep->flags);
  if (!err && nodep->matches.size == 0)
    nodep->flags |= N_EMPTY;

  if (err) {
    END_FREE: ;
    reliq_nfree(nodep);
  } else
    nodep->position_max = predict_range_max(&nodep->position);

  return err;
}

/*void //just for debugging
reliq_expr_print(flexarr *exprs, size_t tab)
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
      fprintf(stderr,"table %d node(%u) expr(%u)\n",a[i].flags,a[i].nodefl,a[i].exprfl);
      reliq_expr_print((flexarr*)a[i].e,tab);
    } else {
      fprintf(stderr,"nodes node(%u) expr(%u)\n",a[i].nodefl,a[i].exprfl);
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
reliq_exprs_free_pre(flexarr *exprs)
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

static reliq_error *
reliq_output_field_get(const char *src, size_t *pos, const size_t s, reliq_output_field *outfield)
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

static flexarr *
reliq_ecomp_pre(const char *csrc, size_t *pos, size_t s, const uint16_t lvl, uint16_t *childfields, reliq_error **err)
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

      if (exprl) {
        if (j == first_pos) {
          size_t g = j;
          while_is(isspace,src,g,s);
          if (g < s && src[g] == '.') {
            if ((*err = reliq_output_field_get(src,&g,s,&expr.outfield)))
              goto NODE_COMP_END;
            if (expr.outfield.name.b) {
              if (childfields)
                (*childfields)++;
              acurrent->childfields++;
            }
            exprl -= g-j;
            j = g;
          }
        }

        if (next == typeGroupStart) {
          uchar empty = 1;
          if (get_format)
            empty = 0;
          if (empty) {
            const char *base = src+j;
            for (size_t g = 0; g < exprl; g++) {
              if (!isspace(base[g])) {
                  empty = 0;
                  break;
              }
            }
          }
          if (!empty) {
            UNEXPECTED_BEFORE_BLOCK: ;
            goto_script_seterr_p(EXIT,"block: %lu: unexpected text before opening of the block",i);
          }
        }

        if (expr.e) { //!
          *err = reliq_ncomp(src+j,exprl,(reliq_npattern*)expr.e);
          if (*err)
            goto EXIT;
        }
      } else if (expr.e) {
        memset(expr.e,0,sizeof(reliq_npattern));
        ((reliq_npattern*)expr.e)->flags |= N_EMPTY;
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

reliq_error *
reliq_ecomp(const char *src, const size_t size, reliq_exprs *exprs)
{
  reliq_error *err = NULL;
  flexarr *ret = reliq_ecomp_pre(src,NULL,size,0,NULL,&err);
  if (ret) {
    //reliq_expr_print(ret,0);
    flexarr_conv(ret,(void**)&exprs->b,&exprs->s);
  } else
    exprs->s = 0;
  return err;
}

static void
dest_match_position(const reliq_range *range, flexarr *dest, size_t start, size_t end) {
  reliq_compressed *x = (reliq_compressed*)dest->v;

  while (start < end && (void*)x[start].hnode < (void*)10)
    start++;
  while (end != start && (void*)x[end-1].hnode < (void*)10)
    end--;

  size_t found = start;
  for (size_t i = start; i < end; i++) {
    if (!range_match(i-start,range,end-start-1))
      continue;
    if (found != i)
      x[found] = x[i];
    found++;
  }
  dest->size = found;
}

static void
nodes_match_full(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind)
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 0; i <= childcount && *found < lasttofind; i++) {
    reliq_match_add(rq,current+i,current,nodep,dest,found);
  }
}

static void
nodes_match_child(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind)
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 1; i <= childcount && *found < lasttofind; i += current[i].desc_count+1)
    reliq_match_add(rq,current+i,current,nodep,dest,found);
}

static void
nodes_match_descendant(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind)
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 1; i <= childcount && *found < lasttofind; i++)
    reliq_match_add(rq,current+i,current,nodep,dest,found);
}

static void
nodes_match_sibling_preceding(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth)
{
  reliq_hnode *nodes = rq->nodes;
  if (nodes == current)
    return;
  const uint16_t lvl = current->lvl;
  uint16_t lvldiff = lvl+depth;
  if (depth == (uint16_t)-1)
    lvldiff = -1;

  for (size_t i=(current-nodes)-1; nodes[i].lvl >= lvl && *found < lasttofind; i--) {
    if (nodes[i].lvl >= lvl && nodes[i].lvl <= lvldiff)
      reliq_match_add(rq,nodes+i,current,nodep,dest,found);
    if (!i)
      break;
  }
}

static void
nodes_match_sibling_subsequent(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth)
{
  reliq_hnode *nodes = rq->nodes;
  const size_t nodesl = rq->nodesl;
  if (current+1 >= nodes+nodesl)
    return;
  const uint16_t lvl = current->lvl;
  uint16_t lvldiff = lvl+depth;
  if (depth == (uint16_t)-1)
    lvldiff = -1;

  for (size_t first=current-nodes,i=first; i < nodesl && (nodes[i].lvl >= lvl && nodes[i].lvl <= lvldiff) && *found < lasttofind; i++) {
    if (i != first)
      reliq_match_add(rq,nodes+i,current,nodep,dest,found);

    if (nodes[i].lvl == lvldiff)
      i += nodes[i].desc_count;
  }
}

static void
nodes_match_sibling(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth)
{
  nodes_match_sibling_preceding(rq,nodep,current,dest,found,lasttofind,depth);
  nodes_match_sibling_subsequent(rq,nodep,current,dest,found,lasttofind,depth);
}

static void
nodes_match_ancestor(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth)
{
  reliq_hnode *nodes=rq->nodes;
  const reliq_hnode *first=current;

  for (uint16_t i = 0; i <= depth && current != nodes && *found < lasttofind; i++) {
    const uint16_t lvl = current->lvl;
    for (size_t j=(current-nodes)-1; nodes[j].lvl >= lvl-1; j--) {
      if (nodes[j].lvl == lvl-1) {
        current = &nodes[j];
        break;
      }

      if (!j)
        break;
    }

    reliq_match_add(rq,current,first,nodep,dest,found);

    if (current->lvl == 0)
      break;
  }
}

static void
node_exec_first(const reliq *rq, reliq_npattern *nodep, flexarr *dest, const uint32_t lasttofind)
{
  const size_t nodesl = rq->nodesl;
  uint32_t found = 0;
  for (size_t i = 0; i < nodesl && found < lasttofind; i++)
    reliq_match_add(rq,rq->nodes+i,rq->parent,nodep,dest,&found);

  if (nodep->position.s)
    dest_match_position(&nodep->position,dest,0,dest->size);
}

static void
node_exec(const reliq *rq, reliq_npattern *nodep, flexarr *source, flexarr *dest)
{
  uint32_t found=0,lasttofind=nodep->position_max;
  if (lasttofind == (uint32_t)-1)
    return;
  if (lasttofind == 0)
    lasttofind = -1;

  if (source->size == 0) {
    node_exec_first(rq,nodep,dest,lasttofind);
    return;
  }

  const size_t size = source->size;
  for (size_t i = 0; i < size; i++) {
    reliq_compressed *x = &((reliq_compressed*)source->v)[i];
    reliq_hnode *current = x->hnode;
    if ((void*)current < (void*)10)
      continue;
    size_t prevdestsize = dest->size;

    switch (nodep->flags&N_MATCHED_TYPE) {
      case N_FULL:
        nodes_match_full(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_SELF:
        reliq_match_add(rq,current,x->parent,nodep,dest,&found); //!!
        break;
      case N_CHILD:
        nodes_match_child(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_DESCENDANT:
        nodes_match_descendant(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_ANCESTOR:
        nodes_match_ancestor(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_PARENT:
        nodes_match_ancestor(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_RELATIVE_PARENT:
        reliq_match_add(rq,x->parent,current,nodep,dest,&found);
        break;
      case N_SIBLING:
        nodes_match_sibling(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_PRECEDING:
        nodes_match_sibling_preceding(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_SUBSEQUENT:
        nodes_match_sibling_subsequent(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_FULL_SIBLING:
        nodes_match_sibling(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_PRECEDING:
        nodes_match_sibling_preceding(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_SUBSEQUENT:
        nodes_match_sibling_subsequent(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      /*case N_TEXT: break;*/
      /*case N_NODE: break;*/
      /*case N_ELEMENT: break;*/
    }

    if (nodep->position.s) {
      if (!(nodep->flags&N_POSITION_ABSOLUTE)) {
        dest_match_position(&nodep->position,dest,prevdestsize,dest->size);
        found = 0;
      } else if (found >= lasttofind)
        break;
    }
  }
  if (nodep->flags&N_POSITION_ABSOLUTE && nodep->position.s)
    dest_match_position(&nodep->position,dest,0,dest->size);
}

/*static reliq_error *
ncollector_check(flexarr *ncollector, size_t correctsize)
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
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar istable, uchar useformat, uchar isempty, uchar non)
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
fcollector_add(const size_t lastn, const uchar isnodef, const reliq_expr *expr, flexarr *ncollector, flexarr *fcollector)
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
reliq_exec_table(const reliq *rq, const reliq_expr *expr, const reliq_output_field *named, flexarr *source, flexarr *dest, flexarr **out, uchar isempty, uchar noncol, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    )
{
  reliq_error *err = NULL;
  flexarr *exprs = (flexarr*)expr->e;
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
      if ((err = reliq_exec_pre(rq,exprsv,exprsl,in,dest,out,0,isempty,noncol,ncollector
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

  err = reliq_exec_pre(rq,exprsv,exprsl,source,dest,out,expr->childfields,noncol,isempty,ncollector
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
reliq_exec_pre(const reliq *rq, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, const uint16_t childfields, uchar noncol, uchar isempty, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    )
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

      if ((err = reliq_exec_table(rq,&exprs[i],outnamed,buf[0],buf[1],out,noncol_r,isempty,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        )))
        goto ERR;

      if (!noncol_r && outnamed && buf[1]->size-prevsize <= 2) {
        isempty = 1;
        ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,exprs[i].flags,0,1,noncol);
        break;
      }
    } else if (exprs[i].e) {
      lastnode = &exprs[i];
      reliq_npattern *nodep = (reliq_npattern*)exprs[i].e;
      if (outnamed)
        add_compressed_blank(buf[1],ofNamed,outnamed);

      if (!isempty)
        node_exec(rq,nodep,buf[0],buf[1]);

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
    if (rq->output) {
      if ((err = nodes_output(rq,buf[2],ncollector
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
reliq_exec_r(reliq *rq, FILE *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  flexarr *compressed=NULL;
  rq->output = output;
  reliq_error *err;

  flexarr *ncollector = flexarr_init(sizeof(reliq_cstr),NCOLLECTOR_INC);
  #ifdef RELIQ_EDITING
  flexarr *fcollector = flexarr_init(sizeof(struct fcollector_expr),FCOLLECTOR_INC);
  #endif

  err = reliq_exec_pre(rq,exprs->b,exprs->s,NULL,NULL,&compressed,0,0,0,ncollector
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
  return reliq_exec_r(rq,NULL,nodes,nodesl,exprs);
}

reliq_error *
reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  reliq_error *err = reliq_exec_r(rq,output,NULL,NULL,exprs);
  fflush(output);
  return err;
}

reliq_error *
reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs)
{
  if (!exprs)
    return NULL;
  FILE *output = open_memstream(str,strl);
  reliq_error *err = reliq_exec_file(rq,output,exprs);
  fclose(output);
  return err;
}

static reliq_error *
reliq_analyze(const char *data, const size_t size, flexarr *nodes, reliq *rq)
{
  reliq_error *err;
  for (size_t i = 0; i < size; i++) {
    while (i < size && data[i] != '<')
        i++;
    while (i < size && data[i] == '<') {
      html_struct_handle(data,&i,size,0,nodes,rq,&err);
      if (err)
        return err;
    }
  }
  return NULL;
}

static reliq_error *
reliq_fmatch(const char *data, const size_t size, FILE *output, const reliq_npattern *nodep,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl)
{
  reliq t;
  t.data = data;
  t.datal = size;
  t.expr = nodep;
  t.nodef = nodef;
  t.nodefl = nodefl;
  t.flags = 0;
  if (output == NULL)
    output = stdout;
  t.output = output;
  t.nodes = NULL;
  t.nodesl = 0;
  t.parent = NULL;

  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);

  reliq_error *err = reliq_analyze(data,size,nodes,&t);

  flexarr_free(nodes);
  flexarr_free((flexarr*)t.attrib_buffer);
  return err;
}

reliq_error *
reliq_fexec_file(char *data, size_t size, FILE *output, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len))
{
  reliq_error *err = NULL;
  uchar was_unallocated = 0;
  if (!exprs || exprs->s == 0)
    goto END;
  if ((err = exprs_check_chain(exprs,1)))
    goto END;

  FILE *destination = output;
  char *ptr=data,*nptr = NULL;
  size_t fsize;

  flexarr *chain = (flexarr*)exprs->b[0].e;
  reliq_expr *chainv = (reliq_expr*)chain->v;

  const size_t chainsize = chain->size;
  for (size_t i = 0; i < chainsize; i++) {
    output = (i == chain->size-1) ? destination : open_memstream(&nptr,&fsize);

    err = reliq_fmatch(ptr,size,output,(reliq_npattern*)chainv[i].e,
      chainv[i].nodef,chainv[i].nodefl);

    fflush(output);

    if (i == 0) {
      if (freedata)
        (*freedata)(data,size);
      was_unallocated = 1;
    } else
      free(ptr);

    if (i != chain->size-1)
      fclose(output);

    if (err)
      return err;

    ptr = nptr;
    size = fsize;
  }

  END: ;
  if (!was_unallocated && freedata)
    (*freedata)(data,size);
  return err;
}

reliq_error *
reliq_fexec_str(char *data, const size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freedata)(void *data, size_t len))
{
  if (!exprs)
    return NULL;
  FILE *output = open_memstream(str,strl);
  reliq_error *err = reliq_fexec_file(data,size,output,exprs,freedata);
  fclose(output);
  return err;
}

static void
reliq_hnode_shift(reliq_hnode *node, const size_t pos)
{
  char const *ref = node->all.b;
  #define shift_cstr(x) x.b = (char const*)(x.b-ref)+pos

  shift_cstr(node->all);
  shift_cstr(node->tag);
  shift_cstr(node->insides);
  const size_t size = node->attribsl;
  for (size_t i = 0; i < size; i++) {
    shift_cstr(node->attribs[i].f);
    shift_cstr(node->attribs[i].s);
  }
}

static void
reliq_hnode_shift_finalize(reliq_hnode *node, char *ref)
{
  #define shift_cstr_p(x) x.b = ref+(size_t)x.b

  shift_cstr_p(node->all);
  shift_cstr_p(node->tag);
  shift_cstr_p(node->insides);
  const size_t size = node->attribsl;
  for (size_t i = 0; i < size; i++) {
    shift_cstr_p(node->attribs[i].f);
    shift_cstr_p(node->attribs[i].s);
  }
}

reliq
reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl)
{
  reliq t;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;
  t.parent = NULL;

  char *ptr;
  size_t pos=0,size;
  uint16_t lvl;
  FILE *out = open_memstream(&ptr,&size);
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    current = compressed[i].hnode;
    if ((void*)current < (void*)10)
      continue;

    lvl = current->lvl;

    const size_t desc_count = current->desc_count;
    for (size_t j = 0; j <= desc_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,current+j,sizeof(reliq_hnode));

      new->attribs = NULL;
      if ((current+j)->attribsl)
        new->attribs = memdup((current+j)->attribs,sizeof(reliq_cstr_pair)*(current+j)->attribsl);

      size_t tpos = pos+(new->all.b-current->all.b);

      reliq_hnode_shift(new,tpos);
      new->lvl -= lvl;
    }

    fwrite(current->all.b,1,current->all.s,out);
    pos += current->all.s;
  }

  fclose(out);

  const size_t nodessize = nodes->size;
  for (size_t i = 0; i < nodessize; i++)
    reliq_hnode_shift_finalize(&((reliq_hnode*)nodes->v)[i],ptr);

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  t.freedata = reliq_std_free;
  t.data = ptr;
  t.datal = size;
  return t;
}

reliq
reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq)
{
  reliq t;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;
  t.freedata = NULL;
  t.data = rq->data;
  t.datal = rq->datal;
  t.parent = NULL;

  uint16_t lvl;
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    current = compressed[i].hnode;
    if ((void*)current < (void*)10)
      continue;

    lvl = current->lvl;

    const size_t desc_count = current->desc_count;
    for (size_t j = 0; j <= desc_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,current+j,sizeof(reliq_hnode));

      new->attribs = NULL;
      if ((current+j)->attribsl)
        new->attribs = memdup((current+j)->attribs,sizeof(reliq_cstr_pair)*(current+j)->attribsl);

      new->lvl -= lvl;
    }
  }

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  return t;
}

int
reliq_std_free(void *addr, size_t len)
{
    free(addr);
    return 0;
}

reliq_error *
reliq_init(const char *data, const size_t size, int (*freedata)(void *addr, size_t len), reliq *rq)
{
  rq->data = data;
  rq->datal = size;
  rq->freedata = freedata;
  rq->expr = NULL;
  rq->flags = RELIQ_SAVE;
  rq->output = NULL;
  rq->nodef = NULL;
  rq->nodefl = 0;
  rq->parent = NULL;

  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  rq->attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);

  reliq_error *err = reliq_analyze(data,size,nodes,rq);

  flexarr_conv(nodes,(void**)&rq->nodes,&rq->nodesl);
  flexarr_free((flexarr*)rq->attrib_buffer);
  if (err)
    reliq_free(rq);
  return err;
}
