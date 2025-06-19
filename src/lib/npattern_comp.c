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

#include "../ext.h"

#include <stdlib.h>
#include <string.h>

#include "ctype.h"
#include "reliq.h"
#include "utils.h"
#include "node_exec.h"
#include "npattern_intr.h"

#define NODE_MATCHES_INC 8

#define XN(x) h_##x
#define X(x) static void XN(x)(const UNUSED reliq *rq, const UNUSED reliq_chnode *chnode, const UNUSED reliq_hnode *hnode, const UNUSED reliq_chnode *parent, char UNUSED const **src, size_t UNUSED *srcl)

X(node_attributes) {
  *srcl = hnode->attribsl;
}

X(node_insides) {
  *src = hnode->insides.b;
  *srcl = hnode->insides.s;
}

X(node_all) {
  *src = hnode->all.b;
  *srcl = hnode->all.s;
}

X(node_start) {
  *src = reliq_hnode_starttag(hnode,srcl);
}

X(node_name) {
  *src = hnode->tag.b;
  *srcl = hnode->tag.s;
}

X(node_end_strip) {
  *src = reliq_hnode_endtag_strip(hnode,srcl);
}

X(node_end) {
  *src = reliq_hnode_endtag(hnode,srcl);
}

X(global_index) {
  *srcl = chnode->all;
}

X(global_level_relative) {
  if (parent) {
    *srcl = chnode->lvl-parent->lvl;
  } else
    *srcl = chnode->lvl;
}

X(global_level) {
  *srcl = chnode->lvl;
}

X(global_tag_count) {
  *srcl = hnode->tag_count;
}

X(global_comments_count) {
  *srcl = hnode->comment_count;
}

X(global_text_count) {
  *srcl = hnode->text_count;
}

X(global_all_count) {
  *srcl = hnode->tag_count+hnode->comment_count+hnode->text_count;
}

X(global_position_relative) {
  if (parent) {
    *srcl = chnode-parent;
  } else
    *srcl = chnode-rq->nodes;
}

X(global_position) {
  *srcl = chnode-rq->nodes;
}

X(comment_all) {
  *src = hnode->all.b;
  *srcl = hnode->all.s;
}

X(comment_insides) {
  *src = hnode->insides.b;
  *srcl = hnode->insides.s;
}

X(text_all) {
  *src = hnode->all.b;
  *srcl = hnode->all.s;
}

#undef X

const struct hook_t hooks_list[] = {
  //global matching
  {{"l",1},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_level_relative),0},
  {{"L",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_level),0},
  {{"c",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_tag_count),0},
  {{"Cc",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_comments_count),0},
  {{"Ct",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_text_count),0},
  {{"Ca",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_all_count),0},
  {{"p",1},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_position_relative),0},
  {{"P",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_position),0},
  {{"I",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_index),0},

  {{"levelrelative",13},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_level_relative),0},
  {{"level",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_level),0},
  {{"count",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_tag_count),0},
  {{"countcomments",13},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_comments_count),0},
  {{"counttext",9},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_text_count),0},
  {{"countall",8},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_all_count),0},
  {{"positionrelative",16},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_position_relative),0},
  {{"position",8},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_position),0},
  {{"index",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_index),0},

  //node matching
  {{"A",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_all),(uintptr_t)"uWcnas"},
  {{"i",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_insides),(uintptr_t)"tWncas"},
  {{"S",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_start),(uintptr_t)"uWcnas"},
  {{"n",1},H_MATCH_NODE|H_PATTERN|H_MATCH_NODE_MAIN,(uintptr_t)XN(node_name),(uintptr_t)"uWinfs"},
  {{"a",1},H_MATCH_NODE|H_RANGE_UNSIGNED,(uintptr_t)XN(node_attributes),0},
  {{"E",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end),(uintptr_t)"uWcnas"},
  {{"e",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end_strip),(uintptr_t)"tWcnfs"},

  {{"all",3},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_all),(uintptr_t)"uWcnas"},
  {{"insides",7},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_insides),(uintptr_t)"tWncas"},
  {{"start",5},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_start),(uintptr_t)"uWcnas"},
  {{"name",4},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_name),(uintptr_t)"uWinfs"},
  {{"attributes",10},H_MATCH_NODE|H_RANGE_UNSIGNED,(uintptr_t)XN(node_attributes),0},
  {{"end",3},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end),(uintptr_t)"uWcnas"},
  {{"endstrip",8},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end_strip),(uintptr_t)"tWcnfs"},
  {{"has",3},H_MATCH_NODE|H_EXPRS,(uintptr_t)NULL,0},

  //comment matching
  {{"A",1},H_MATCH_COMMENT|H_PATTERN|H_MATCH_COMMENT_MAIN,(uintptr_t)XN(comment_all),(uintptr_t)"tWncas"},
  {{"i",1},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_insides),(uintptr_t)"tWncas"},

  {{"all",3},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_all),(uintptr_t)"tWncas"},
  {{"insides",7},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_insides),(uintptr_t)"tWncas"},

  //text matching
  {{"A",1},H_MATCH_TEXT|H_PATTERN|H_MATCH_TEXT_MAIN,(uintptr_t)XN(text_all),(uintptr_t)"tWncas"},

  {{"all",3},H_MATCH_TEXT|H_PATTERN,(uintptr_t)XN(text_all),(uintptr_t)"tWncas"},

  //access
  {{"",0},H_ACCESS|H_NOARG,AXIS_SELF,0},
  {{"desc",4},H_ACCESS|H_NOARG,AXIS_DESCENDANTS,0},
  {{"rparent",7},H_ACCESS|H_NOARG,AXIS_RELATIVE_PARENT,0},
  {{"sibl",4},H_ACCESS|H_NOARG,AXIS_SIBLINGS_PRECEDING|AXIS_SIBLINGS_SUBSEQUENT,0},
  {{"spre",4},H_ACCESS|H_NOARG,AXIS_SIBLINGS_PRECEDING,0},
  {{"ssub",4},H_ACCESS|H_NOARG,AXIS_SIBLINGS_SUBSEQUENT,0},
  {{"fsibl",5},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_PRECEDING|AXIS_FULL_SIBLINGS_SUBSEQUENT,0},
  {{"fspre",5},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_PRECEDING,0},
  {{"fssub",5},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_SUBSEQUENT,0},

  {{"everything",10},H_ACCESS|H_NOARG,AXIS_EVERYTHING,0},
  {{"full",4},H_ACCESS|H_NOARG,AXIS_SELF|AXIS_DESCENDANTS,0},
  {{"self",4},H_ACCESS|H_NOARG,AXIS_SELF,0},
  {{"child",5},H_ACCESS|H_NOARG,AXIS_CHILDREN,0},
  {{"descendant",10},H_ACCESS|H_NOARG,AXIS_DESCENDANTS,0},
  {{"ancestor",8},H_ACCESS|H_NOARG,AXIS_ANCESTORS,0},
  {{"parent",6},H_ACCESS|H_NOARG,AXIS_PARENT,0},
  {{"relative_parent",15},H_ACCESS|H_NOARG,AXIS_RELATIVE_PARENT,0},
  {{"sibling",7},H_ACCESS|H_NOARG,AXIS_SIBLINGS_PRECEDING|AXIS_SIBLINGS_SUBSEQUENT,0},
  {{"sibling_preceding",17},H_ACCESS|H_NOARG,AXIS_SIBLINGS_PRECEDING,0},
  {{"sibling_subsequent",18},H_ACCESS|H_NOARG,AXIS_SIBLINGS_SUBSEQUENT,0},
  {{"full_sibling",12},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_PRECEDING|AXIS_FULL_SIBLINGS_SUBSEQUENT,0},
  {{"full_sibling_preceding",22},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_PRECEDING,0},
  {{"full_sibling_subsequent",23},H_ACCESS|H_NOARG,AXIS_FULL_SIBLINGS_SUBSEQUENT,0},
  {{"preceding",9},H_ACCESS|H_NOARG,AXIS_PRECEDING,0},
  {{"before",6},H_ACCESS|H_NOARG,AXIS_BEFORE,0},
  {{"after",5},H_ACCESS|H_NOARG,AXIS_AFTER,0},
  {{"subsequent",10},H_ACCESS|H_NOARG,AXIS_SUBSEQUENT,0},

  //type
  {{"tag",3},H_TYPE|H_NOARG,NM_TAG,0},
  {{"comment",7},H_TYPE|H_NOARG,NM_COMMENT,0},
  {{"text",4},H_TYPE|H_NOARG,NM_TEXT,0},
  {{"textempty",9},H_TYPE|H_NOARG,NM_TEXT_EMPTY,0},
  {{"textnoerr",9},H_TYPE|H_NOARG,NM_TEXT_NOERR,0},
  {{"texterr",7},H_TYPE|H_NOARG,NM_TEXT_ERR,0},
  {{"textall",7},H_TYPE|H_NOARG,NM_TEXT_ALL,0},
};

#undef XN

struct nmatchers_state {
  nmatchers *matches;
  reliq_range *position;
  reliq_error *err;
  const char *src;
  size_t size;
  uint16_t nodeflags;
  uint16_t axisflags;
  uint16_t lvl;
  uint8_t prevtype;
  uchar hastag;
  uchar typehooks_count : 1;
};

static void
pattrib_free(struct pattrib *attrib) {
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
  const uint16_t flags = hook->hook->flags;
  if (flags&(H_RANGE_SIGNED|H_RANGE_UNSIGNED)) {
    range_free(&hook->match.range);
  } if (flags&H_EXPRS) {
    reliq_efree_intr(&hook->match.expr);
  } else if (flags&H_PATTERN)
    reliq_regfree(&hook->match.pattern);
}

static void free_nmatchers(nmatchers *matches);

static void
free_nmatchers_group(nmatchers_groups *groups)
{
  const size_t size = groups->size;
  nmatchers *list = groups->list;
  for (size_t i = 0; i < size; i++)
    free_nmatchers(&list[i]);
  free(list);
}

static void
free_nmatchers(nmatchers *matches)
{
  const size_t size = matches->size;
  nmatchers_node *list = matches->list;
  for (size_t i = 0; i < size; i++) {
    nmatchers_node *node = &list[i];
    switch (node->type) {
      case MATCHES_TYPE_HOOK:
        reliq_free_hook(node->data.hook);
        free(node->data.hook);
        break;
      case MATCHES_TYPE_ATTRIB:
        pattrib_free(node->data.attrib);
        free(node->data.attrib);
        break;
      case MATCHES_TYPE_GROUPS:
        free_nmatchers_group(node->data.groups);
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

  free_nmatchers(&nodep->matches);
}

static const char *
match_hook_unexpected_argument(const uint16_t flags)
{
  if (flags&H_PATTERN)
    return "hook \"%.*s\" expected pattern argument";
  if (flags&H_EXPRS)
    return "hook \"%.*s\" expected node argument";
  if (flags&(H_RANGE_SIGNED|H_RANGE_UNSIGNED))
    return "hook \"%.*s\" expected list argument";
  if (flags&H_NOARG)
    return "hook \"%.*s\" unexpected argument";
  return NULL;
}

static inline uchar
hook_handle_isname(char c)
{
  if (c == '_' || c == '-')
    return 1;
  return isalpha(c);
}

static inline uint16_t
nmatchers_type_hmask(const uint8_t type)
{
  switch (type) {
    case NM_DEFAULT:
    case NM_TAG:
      return H_MATCH_NODE;
    case NM_COMMENT:
      return H_MATCH_COMMENT;
    case NM_MULTIPLE:
      return 0;
    default:
      return H_MATCH_TEXT;
  }
}

static uchar
find_hook(const char *name, const size_t namel, const uint8_t type, size_t *pos)
{
  const uint16_t hmask = (H_ACCESS|H_TYPE|H_GLOBAL|nmatchers_type_hmask(type));
  size_t i = 0;
  for (; i < LENGTH(hooks_list); i++) {
    uint16_t flags = hooks_list[i].flags;
    if (!(flags&hmask))
      continue;
    if (memeq(hooks_list[i].name.b,name,hooks_list[i].name.s,namel)) {
      *pos = i;
      return 1;
    }
  }

  return 0;
}

static reliq_error *
match_hook_handle_expr(const char *src, const size_t size, size_t *pos, reliq_hook *hook)
{
  reliq_error *err = NULL;
  size_t i = *pos;
  if (src[i] != '"' && src[i] != '\'')
    goto_script_seterr(ERR,match_hook_unexpected_argument(hook->hook->flags),hook->hook->name.b);
  char *str;
  size_t strl;
  if ((err = get_quoted(src,&i,size,' ',&str,&strl)) || !strl)
    goto ERR;
  err = reliq_ecomp_intr(str,strl,&hook->match.expr);
  free(str);
  if (err)
    goto ERR;
  if ((err = expr_check_chain(&hook->match.expr))) {
    reliq_efree_intr(&hook->match.expr);
    goto ERR;
  }

  ERR: ;
  *pos = i;
  return err;
}

static reliq_error *
match_hook_handle_pattern(const char *src, const size_t size, size_t *pos, reliq_hook *hook)
{
  reliq_error *err = NULL;
  size_t i = *pos;

  if ((err = reliq_regcomp(&hook->match.pattern,src,&i,size,' ',(const char*)hook->hook->arg2,NULL)))
    goto ERR;
  if (!hook->match.pattern.range.s && hook->match.pattern.flags&RELIQ_PATTERN_ALL) { //ignore if it matches everything
    reliq_regfree(&hook->match.pattern);
    goto ERR;
  }

  ERR: ;
  *pos = i;
  return err;
}

static inline const char *
get_hook_name(const char *src, size_t *pos, const size_t size, size_t *namel)
{
  size_t p = *pos;
  const char *name = src+p;

  while_is(hook_handle_isname,src,p,size);

  if (p >= size || !namel || src[p] != '@')
    return NULL;

  *namel = p-(name-src);
  p++;
  *pos = p;
  return name;
}

static const char *
matched_type_str(const uint8_t type)
{
  if (type == NM_TAG || type == NM_DEFAULT)
    return "nodes";
  if (type == NM_COMMENT)
    return "comments";

  if (type == NM_MULTIPLE)
    return "global";
  return "text";
}

static reliq_error *
hook_handle(const char *src, size_t *pos, const size_t size, reliq_hook *out_hook, const uint8_t type)
{
  reliq_error *err = NULL;
  size_t p=*pos;
  size_t namel;
  const char *name = get_hook_name(src,&p,size,&namel);
  if (!name)
    goto ERR;

  size_t i = 0;
  if (!find_hook(name,namel,type,&i))
    goto_script_seterr(ERR,"hook \"%.*s\" does not exists for %s",(int)namel,name,matched_type_str(type));

  const struct hook_t *mhook = hooks_list+i;
  out_hook->hook = mhook;
  const uint16_t hflags = mhook->flags;

  #define HOOK_EXPECT(x) if (!(hflags&(x))) \
      goto_script_seterr(ERR,match_hook_unexpected_argument(hflags),(int)namel,name)

  char firstchar = 0;
  if (p >= size) {
    if (!(hflags&H_NOARG))
      goto_script_seterr(ERR,"hook \"%.*s\" expected argument",namel,name);
  } else
    firstchar = src[p];

  if (!firstchar || isspace(firstchar)) {
    HOOK_EXPECT(H_NOARG);
  } else if (src[p] == '[') {
    HOOK_EXPECT(H_RANGE_UNSIGNED|H_RANGE_SIGNED);
    if ((err = range_comp(src,&p,size,&out_hook->match.range)))
      goto ERR;
  } else if (hflags&H_EXPRS) {
    HOOK_EXPECT(H_EXPRS);
    if ((err = match_hook_handle_expr(src,size,&p,out_hook)))
      goto ERR;
  } else {
    HOOK_EXPECT(H_PATTERN);
    if ((err = match_hook_handle_pattern(src,size,&p,out_hook)))
      goto ERR;
  }

  #undef HOOK_EXPECT

  ERR:
  *pos = p;
  return err;
}

static inline void
nmatchers_node_add(flexarr *arr, uchar type, void *data, const size_t size) //arr: nmatchers
{
  nmatchers_node *new = flexarr_inc(arr);
  new->type = type;
  new->data.hook = memdup(data,size);
}

static void
free_node_matches_flexarr(flexarr *groups_matches) //group_matches: nmatchers
{
  nmatchers *matchesv = (nmatchers*)groups_matches->v;
  const size_t size = groups_matches->size;
  for (size_t i = 0; i < size; i++)
    free_nmatchers(&matchesv[i]);
  flexarr_free(groups_matches);
}

static size_t
strclass_tagname(const char *str, const size_t strl)
{
  if (strl < 1)
    return -1;

  if (!isalpha(str[0]))
    return 0;

  for (size_t i = 1; i < strl; i++)
    if (str[i] == '>' || str[i] == '/' || isspace(str[i]))
      return i;
  return -1;
}

static size_t
strclass_attrib(const char *str, const size_t strl)
{
  for (size_t i = 0; i < strl; i++)
    if (str[i] == '=' || str[i] == '>' || str[i] == '/' || isspace(str[i]))
      return i;
  return -1;
}

static inline uchar
nmatchers_type_text(const uint8_t type)
{
  return (type >= NM_TEXT && type <= NM_TEXT_ALL);
}

static inline uchar
nmatchers_type_conflict(const uint8_t type1, const uint8_t type2)
{
  if (type1 == type2)
    return 0;
  if (type1 == NM_DEFAULT || type2 == NM_DEFAULT)
    return 0;
  if (type1 == NM_TEXT && (type2 == NM_TEXT_NOERR || type2 == NM_TEXT_ERR))
    return 0;
  if (type1 == NM_TEXT_ALL &&
    nmatchers_type_text(type2))
    return 0;
  return 1;
}

static void handle_nmatchers(size_t *pos, struct nmatchers_state *st);

static inline uchar //i've run out of names
nmatchers_type_text_pure(const uint8_t type)
{
  return (type == NM_TEXT || type == NM_TEXT_NOERR || type == NM_TEXT_ERR);
}

static void
nmatchers_type_merge(const uint8_t type, uint8_t *dest)
{
  const uint8_t t = *dest;
  if (t == type)
    return;
  if (t == NM_DEFAULT) {
    *dest = type;
    return;
  }
  if (nmatchers_type_text(type) && nmatchers_type_text(t)) {
    if (nmatchers_type_text_pure(type) && nmatchers_type_text_pure(t)) {
      *dest = NM_TEXT;
    } else
      *dest = NM_TEXT_ALL;
    return;
  }

  *dest = NM_MULTIPLE;
}

static nmatchers *
handle_nmatchers_group_add(size_t *pos, struct nmatchers_state *st, flexarr *groups_matches, uchar *wastag)
{
  uchar prevhastag = st->hastag;
  nmatchers *matches = st->matches;
  nmatchers *ret = flexarr_incz(groups_matches);
  uint8_t prevtype = st->prevtype;

  st->lvl++;
  st->prevtype = matches->type;
  st->matches = ret;
  handle_nmatchers(pos,st);
  uchar sethastag = st->hastag;
  st->hastag = prevhastag;
  st->matches = matches;
  st->prevtype = prevtype;
  st->lvl--;
  if (st->err)
    goto ERR;
  if (!prevhastag && wastag && !sethastag) {
    st->err = script_err("node: %lu: if one group specifies tag then the rest has too",*pos);
    goto ERR;
  }
  *wastag = sethastag;

  return ret;

  ERR: ;
  return NULL;
}

static uchar
handle_nmatchers_group(size_t *pos, flexarr *result, struct nmatchers_state *st) //result: nmatchers_node
{
  const char *src = st->src;
  const size_t size = st->size;
  size_t i = *pos;
  flexarr *groups_matches = NULL;
  if (++i >= size) {
    END_OF_RANGE: ;
    st->err = script_err("node: %lu: unprecedented end of group",i-1);
    goto ERR;
  }

  flexarr f_groups_matches = flexarr_init(sizeof(nmatchers),NODE_MATCHES_INC);
  groups_matches = &f_groups_matches;
  uchar wastag = 0;

  uint8_t type_acc = NM_DEFAULT;

  while (1) {
    nmatchers *nmatch = handle_nmatchers_group_add(&i,st,groups_matches,&wastag);
    if (nmatch == NULL)
      goto ERR;

    nmatchers_type_merge(nmatch->type,&type_acc);

    if (i >= size || src[i] != '(') {
      size_t lastindex = i-1;
      if (i >= size)
        lastindex = size-1;
      if (i >= size+1 || src[lastindex] != ')')
        goto END_OF_RANGE;
      if (i >= size) //this is a horrible hack which checks if all groups are closed
        i++;
      break;
    }

    /*if (nmatch->size < 1) {
      free_node_matches_flexarr(groups_matches);
      st->err = script_err("node: empty group will always pass");
      goto ERR;
    }*/ //future warning

    i++;
  }

  if (!st->hastag)
    st->hastag = wastag;

  /*if (groups_matches->size < 2) {
    free_node_matches_flexarr(groups_matches);
    st->err = script_err("node: groups must have at least 2 groups to affect anything");
    goto ERR;
  }*/ //future warning

  nmatchers_type_merge(type_acc,&st->matches->type);

  nmatchers_groups groups;
  flexarr_conv(groups_matches,(void**)&groups.list,&groups.size);
  nmatchers_node_add(result,MATCHES_TYPE_GROUPS,&groups,sizeof(nmatchers_groups));

  *pos = i;
  return 0;

  ERR: ;
  if (groups_matches)
    free_node_matches_flexarr(groups_matches);
  *pos = i;
  return 1;
}

static reliq_error *
match_hook_add_access_type(const size_t pos, const reliq_hook *hook, const uchar invert, flexarr *result, struct nmatchers_state *st) //result: nmatchers_node
{
  const uchar isaccess = ((hook->hook->flags&H_ACCESS) > 0);
  if (invert)
    return script_err("%s hook \"%s\" cannot be inverted",
      isaccess ? "access" : "type",hook->hook->name.b);

  if (isaccess) {
    if (st->lvl != 0)
      return script_err("node: %lu: groups cannot have access hooks",pos);
    st->axisflags |= hook->hook->arg1;
  } else {
    if (st->typehooks_count)
      return script_err("hook \"%s\": type hooks can be specified only once",hook->hook->name.b);
    if (result->size != 0)
      return script_err("hook \"%s\": type hooks have to be specified before everything else",hook->hook->name.b);
    if (nmatchers_type_conflict(st->matches->type,hook->hook->arg1))
      return script_err("hook \"%s\" is in conflict with higher type hook",hook->hook->name.b);

    st->matches->type = hook->hook->arg1;
    st->typehooks_count = 1;
  }

  return NULL;
}

static uchar
hook_add(size_t *pos, const uchar invert, flexarr *result, struct nmatchers_state *st) //result: nmatchers_node
{
  const char *src = st->src;
  const size_t size = st->size;
  reliq_hook hook = {0};
  size_t prev = *pos;
  if ((st->err = hook_handle(src,pos,size,&hook,st->matches->type)))
    return 0;

  if (!hook.hook)
    goto ERR;

  if (*pos == prev)
    goto ERR;

  const uint16_t hflags = hook.hook->flags;

  if (hflags&(H_TYPE|H_ACCESS)) {
    if ((st->err = match_hook_add_access_type(*pos,&hook,invert,result,st)))
      goto ERR;
    goto END;
  }

  hook.invert = invert;

  nmatchers_node_add(result,MATCHES_TYPE_HOOK,&hook,sizeof(reliq_hook));

  if (hflags&(H_MATCH_NODE|H_MATCH_COMMENT|H_MATCH_TEXT))
    return 2;

  END: ;
  return 1;

  ERR: ;
  return 0;
}

static const hook_t *
find_main_hook(const uint16_t main_hook_mask)
{
  size_t i = 0;
  for (; i < LENGTH(hooks_list); i++)
    if (hooks_list[i].flags&main_hook_mask)
      return hooks_list+i;

  return NULL; //should never happen
}

static reliq_error *
comp_node_add_tag(const char *src, size_t *pos, const size_t size, const uchar invert, flexarr *result) //result: nmatchers_node
{
  reliq_pattern tag;
  reliq_error *err;
  const hook_t *hook = find_main_hook(H_MATCH_NODE_MAIN);
  if ((err = reliq_regcomp(&tag,src,pos,size,' ',(const char*)hook->arg2,strclass_tagname)))
    return err;
  reliq_hook h = {
      .match.pattern = tag,
      .invert = invert,
      .hook = hook
  };
  nmatchers_node_add(result,MATCHES_TYPE_HOOK,&h,sizeof(reliq_hook));
  return NULL;
}

static reliq_error *
comp_node(const char *src, size_t *pos, const size_t size, uchar invert, uchar *hastag, flexarr *result) //result: nmatchers_node
{
  reliq_error *err = NULL;
  size_t i = *pos;
  char shortcut = 0;
  struct pattrib attrib = {0};
  uchar tofree = 1;

  if (invert)
    attrib.flags |= A_INVERT;

  if (!*hastag)
    goto GET_TAG_NAME;

  if (src[i] == '.' || src[i] == '#') {
    shortcut = src[i++];
  } else if (i+1 < size && src[i] == '\\' && (src[i+1] == '.' || src[i+1] == '#'))
    i++;

  while_is(isspace,src,i,size);
  if (i >= size)
    goto END;

  if (src[i] == '[') {
    if ((err = range_comp(src,&i,size,&attrib.position)))
      goto END;
  } else if (*hastag && i+1 < size && src[i] == '\\' && src[i+1] == '[')
    i++;

  if (!*hastag) {
    GET_TAG_NAME: ;
    *hastag = 1;
    err = comp_node_add_tag(src,&i,size,invert,result);
    goto END;
  }

  if (i >= size)
    goto END;

  if (shortcut == '.' || shortcut == '#') {
    char *t_name = (shortcut == '.') ? "class" : "id";
    size_t t_pos=0,t_size=(shortcut == '.' ? 5 : 2);
    if ((err = reliq_regcomp(&attrib.r[0],t_name,&t_pos,t_size,' ',"uWnsfi",strclass_attrib)))
      goto END;

    if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',"uwncsf",NULL)))
      goto END;
    attrib.flags |= A_VAL_MATTERS;
  } else {
    if ((err = reliq_regcomp(&attrib.r[0],src,&i,size,'=',"uWnsfi",strclass_attrib)))
      goto END;

    while_is(isspace,src,i,size);
    if (i >= size)
      goto ADD_ATTRIB;
    if (src[i] == '=') {
      i++;
      while_is(isspace,src,i,size);
      if (i >= size)
        goto END;

      if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',"tWncfs",NULL)))
        goto END;
      attrib.flags |= A_VAL_MATTERS;
    } else {
      attrib.flags &= ~A_VAL_MATTERS;
      goto ADD_ATTRIB;
    }
  }

  if (i < size && src[i] != '+' && src[i] != '-')
    i++;

  ADD_ATTRIB: ;
  tofree = 0;
  nmatchers_node_add(result,MATCHES_TYPE_ATTRIB,&attrib,sizeof(struct pattrib));

  END: ;
  if (tofree)
    pattrib_free(&attrib);
  *pos = i;
  return err;
}

static reliq_error *
comp_single_text(const char *src, size_t *pos, const size_t size, uchar invert, uchar *hastag, const uint16_t main_hook_mask, flexarr *result) //result: nmatchers_node
{
  reliq_error *err = NULL;

  reliq_pattern tag;
  const hook_t *hook = find_main_hook(main_hook_mask);
  if ((err = reliq_regcomp(&tag,src,pos,size,' ',(const char*)hook->arg2,NULL)))
    goto END;

  *hastag = 1;

  reliq_hook h = {
    .match.pattern = tag,
    .invert = invert,
    .hook = hook
  };

  nmatchers_node_add(result,MATCHES_TYPE_HOOK,&h,sizeof(reliq_hook));

  END: ;
  return err;
}


static reliq_error *
comp_comment(const char *src, size_t *pos, const size_t size, uchar invert, uchar *hastag, flexarr *result) //result: nmatchers_node
{
  return comp_single_text(src,pos,size,invert,hastag,H_MATCH_COMMENT_MAIN,result);
}

static reliq_error *
comp_text(const char *src, size_t *pos, const size_t size, uchar invert, uchar *hastag, flexarr *result) //result: nmatchers_node
{
  return comp_single_text(src,pos,size,invert,hastag,H_MATCH_TEXT_MAIN,result);
}

static uchar
handle_nmatchers_position(size_t *pos, struct nmatchers_state *st)
{
  const char *src = st->src;
  const size_t size = st->size;
  size_t i = *pos;
  const char *r = memchr(src+i,']',size-i);
  if (r == NULL)
    goto ERR;

  r++;
  if ((size_t)(r-src) < size && !isspace(*r))
    goto ERR;

  if (st->lvl) {
    st->err = script_err("node: %lu: groups cannot have position",r-src);
    goto ERR;
  } else if (st->position->s) {
    st->err = script_err("node: %lu: position already declared",r-src);
    goto ERR;
  }

  if ((st->err = range_comp(src,&i,size,st->position)))
    goto ERR;

  if (!st->hastag)
    st->nodeflags |= N_POSITION_ABSOLUTE;

  *pos = r-src;
  return 1;

  ERR: ;
  *pos = i;
  return 0;
}

static reliq_error *
err_multiple(const size_t pos)
{
  return script_err("node %lu: multiple types cannot be mixed",pos);
}

static uchar
hook_check(size_t *pos, const uchar invert, flexarr *result, struct nmatchers_state *st) //result: nmatchers_node
{
  uchar r = hook_add(pos,invert,result,st);
  if (!r)
    return 0;

  uint8_t *type = &st->matches->type;
  if (r == 2) {
    if (*type == NM_DEFAULT)
      *type = NM_TAG;
    if (*type == NM_MULTIPLE) {
      st->err = err_multiple(*pos);
      return 0;
    }
  }

  return 1;
}

static reliq_error *
type_comp(const char *src, size_t *pos, const size_t size, const uchar invert, uchar *hastag, flexarr *result, uint8_t *type) //result: nmatchers_node
{
  switch (*type) {
    case NM_DEFAULT:
      *type = NM_TAG;
      return NULL;
    case NM_TAG:
      return comp_node(src,pos,size,invert,hastag,result);
    case NM_COMMENT:
      return comp_comment(src,pos,size,invert,hastag,result);
    default:
      return comp_text(src,pos,size,invert,hastag,result);
  }
}

static void
handle_nmatchers(size_t *pos, struct nmatchers_state *st)
{
  const char *src = st->src;
  const size_t size = st->size;
  if (st->lvl >= RELIQ_MAX_GROUP_LEVEL) {
    st->err = script_err("node: %lu: reached %lu level of recursion",*pos,st->lvl);
    return;
  }
  flexarr result = flexarr_init(sizeof(nmatchers_node),NODE_MATCHES_INC);
  nmatchers *matches = st->matches;
  *matches = (nmatchers){ .type = st->prevtype };
  uint8_t *type = &matches->type;
  uchar *hastag = &st->hastag;
  st->typehooks_count = 0;

  size_t i = *pos;
  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')') {
      if (st->lvl == 0)
        st->err = script_err("node: %lu: unexpected '%c'",i,src[i]);
      i++;
      break;
    }

    if (src[i] == '(') {
      if (handle_nmatchers_group(&i,&result,st))
        break;
      continue;
    }

    if (src[i] == '[') {
      if (handle_nmatchers_position(&i,st))
        continue;
      if (st->err)
        break;
    }

    uchar invert = 0;
    if (src[i] == '+') {
      i++;
    } else if (src[i] == '-') {
      invert = 1;
      i++;
    } else if (i+1 < size && src[i] == '\\' && (src[i+1] == '+' || src[i+1] == '-'))
      i++;

    if ((isalpha(src[i]) || src[i] == '@') &&
      hook_check(&i,invert,&result,st))
      continue;

    if (i >= size || st->err)
      break;

    if (*type == NM_MULTIPLE) {
      st->err = err_multiple(i);
      break;
    }

    if ((st->err = type_comp(src,&i,size,invert,hastag,&result,type)))
      break;
  }

  flexarr_conv(&result,(void**)&matches->list,&matches->size);
  *pos = i;
}

reliq_error *
reliq_ncomp(const char *script, const size_t size, reliq_npattern *nodep)
{
  if (!nodep)
    return NULL;

  *nodep = (reliq_npattern){0};
  if (!size) {
    nodep->flags |= N_EMPTY;
    return NULL;
  }

  struct nmatchers_state st = {
    .src = script,
    .size = size,
    .matches = &nodep->matches,
    .position = &nodep->position,
    .prevtype = NM_DEFAULT
  };

  size_t pos=0;
  handle_nmatchers(&pos,&st);
  nodep->flags = st.nodeflags;
  if (!st.err && nodep->matches.size == 0 && nodep->matches.type == NM_DEFAULT)
    nodep->flags |= N_EMPTY;

  if (st.err) {
    reliq_nfree(nodep);
  } else {
    nodep->position_max = predict_range_max(&nodep->position);
    if (st.axisflags == 0)
      st.axisflags = AXIS_SELF|AXIS_DESCENDANTS;
    axis_comp_functions(st.axisflags,(void*)&nodep->axis_funcs);
  }

  return st.err;
}
