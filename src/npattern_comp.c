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

#include "ctype.h"
#include "reliq.h"
#include "utils.h"
#include "npattern_intr.h"
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
  {{"l",1},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_level_relative)},
  {{"L",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_level)},
  {{"c",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_tag_count)},
  {{"Cc",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_comments_count)},
  {{"Ct",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_text_count)},
  {{"Ca",2},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_all_count)},
  {{"p",1},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_position_relative)},
  {{"P",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_position)},
  {{"I",1},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_index)},

  {{"levelrelative",13},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_level_relative)},
  {{"level",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_level)},
  {{"count",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_tag_count)},
  {{"countcomments",13},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_comments_count)},
  {{"counttext",9},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_text_count)},
  {{"countall",8},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_all_count)},
  {{"positionrelative",16},H_GLOBAL|H_RANGE_SIGNED,(uintptr_t)XN(global_position_relative)},
  {{"position",8},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_position)},
  {{"index",5},H_GLOBAL|H_RANGE_UNSIGNED,(uintptr_t)XN(global_index)},

  //node matching
  {{"A",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_all)},
  {{"i",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_insides)},
  {{"S",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_start)},
  {{"n",1},H_MATCH_NODE|H_PATTERN|H_MATCH_NODE_MAIN,(uintptr_t)XN(node_name)},
  {{"a",1},H_MATCH_NODE|H_RANGE_UNSIGNED,(uintptr_t)XN(node_attributes)},
  {{"e",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end_strip)},
  {{"E",1},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end)},

  {{"all",3},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_all)},
  {{"insides",7},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_insides)},
  {{"start",5},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_start)},
  {{"name",4},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_name)},
  {{"attributes",10},H_MATCH_NODE|H_RANGE_UNSIGNED,(uintptr_t)XN(node_attributes)},
  {{"has",3},H_MATCH_NODE|H_EXPRS,(uintptr_t)NULL},
  {{"end",3},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end)},
  {{"endstrip",8},H_MATCH_NODE|H_PATTERN,(uintptr_t)XN(node_end_strip)},

  //comment matching
  {{"A",1},H_MATCH_COMMENT|H_PATTERN|H_MATCH_COMMENT_MAIN,(uintptr_t)XN(comment_all)},
  {{"i",1},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_insides)},

  {{"all",3},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_all)},
  {{"insides",7},H_MATCH_COMMENT|H_PATTERN,(uintptr_t)XN(comment_insides)},

  //text matching
  {{"A",1},H_MATCH_TEXT|H_PATTERN|H_MATCH_TEXT_MAIN,(uintptr_t)XN(text_all)},

  {{"all",3},H_MATCH_TEXT|H_PATTERN,(uintptr_t)XN(text_all)},

  //access
  {{"",0},H_ACCESS|H_NOARG,N_SELF},
  {{"desc",4},H_ACCESS|H_NOARG,N_DESCENDANT},
  {{"rparent",7},H_ACCESS|H_NOARG,N_RELATIVE_PARENT},
  {{"sibl",4},H_ACCESS|H_NOARG,N_SIBLING},
  {{"spre",4},H_ACCESS|H_NOARG,N_SIBLING_PRECEDING},
  {{"ssub",4},H_ACCESS|H_NOARG,N_SIBLING_SUBSEQUENT},
  {{"fsibl",5},H_ACCESS|H_NOARG,N_FULL_SIBLING},
  {{"fspre",5},H_ACCESS|H_NOARG,N_FULL_SIBLING_PRECEDING},
  {{"fssub",5},H_ACCESS|H_NOARG,N_FULL_SIBLING_SUBSEQUENT},

  {{"full",4},H_ACCESS|H_NOARG,N_FULL},
  {{"self",4},H_ACCESS|H_NOARG,N_SELF},
  {{"child",5},H_ACCESS|H_NOARG,N_CHILD},
  {{"descendant",10},H_ACCESS|H_NOARG,N_DESCENDANT},
  {{"ancestor",8},H_ACCESS|H_NOARG,N_ANCESTOR},
  {{"parent",6},H_ACCESS|H_NOARG,N_PARENT},
  {{"relative_parent",15},H_ACCESS|H_NOARG,N_RELATIVE_PARENT},
  {{"sibling",7},H_ACCESS|H_NOARG,N_SIBLING},
  {{"sibling_preceding",17},H_ACCESS|H_NOARG,N_SIBLING_PRECEDING},
  {{"sibling_subsequent",18},H_ACCESS|H_NOARG,N_SIBLING_SUBSEQUENT},
  {{"full_sibling",12},H_ACCESS|H_NOARG,N_FULL_SIBLING},
  {{"full_sibling_preceding",22},H_ACCESS|H_NOARG,N_FULL_SIBLING_PRECEDING},
  {{"full_sibling_subsequent",23},H_ACCESS|H_NOARG,N_FULL_SIBLING_SUBSEQUENT},

  //type
  {{"node",4},H_TYPE|H_NOARG,NM_NODE},
  {{"comment",7},H_TYPE|H_NOARG,NM_COMMENT},
  {{"text",4},H_TYPE|H_NOARG,NM_TEXT},
  {{"textempty",9},H_TYPE|H_NOARG,NM_TEXT_EMPTY},
  {{"textnoerr",9},H_TYPE|H_NOARG,NM_TEXT_NOERR},
  {{"texterr",7},H_TYPE|H_NOARG,NM_TEXT_ERR},
  {{"textall",7},H_TYPE|H_NOARG,NM_TEXT_ALL},
};

#undef XN

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

static void free_matches(nmatchers *matches);

static void
free_matches_group(nmatchers_groups *groups)
{
  const size_t size = groups->size;
  nmatchers *list = groups->list;
  for (size_t i = 0; i < size; i++)
    free_matches(&list[i]);
  free(list);
}

static void
free_matches(nmatchers *matches)
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
        free_matches_group(node->data.groups);
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

  free_matches(&nodep->matches);
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
    case NM_NODE:
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
    if (memcomp(hooks_list[i].name.b,name,hooks_list[i].name.s,namel)) {
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
  char *rflags = "uWcas";
  if (hook->hook->arg == (uintptr_t)h_node_end ||
    hook->hook->arg == (uintptr_t)h_node_end_strip)
    rflags = "tWcnfs";
  if ((err = reliq_regcomp(&hook->match.pattern,src,&i,size,' ',rflags,NULL)))
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
  if (type == NM_NODE || type == NM_DEFAULT)
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
  reliq_hook hook = { .hook = mhook };
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
    if ((err = range_comp(src,&p,size,&hook.match.range)))
      goto ERR;
  } else if (hflags&H_EXPRS) {
    HOOK_EXPECT(H_EXPRS);
    if ((err = match_hook_handle_expr(src,size,&p,&hook)))
      goto ERR;
  } else {
    HOOK_EXPECT(H_PATTERN);
    if ((err = match_hook_handle_pattern(src,size,&p,&hook)))
      goto ERR;
  }

  #undef HOOK_EXPECT

  memcpy(out_hook,&hook,sizeof(hook));
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
    free_matches(&matchesv[i]);
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

static reliq_error *handle_nmatchers(const char *src, size_t *pos, const size_t size, const uint16_t lvl, nmatchers *matches, uchar *hastag, reliq_range *position, uint16_t *nodeflags, const uint8_t prevtype);

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

static uchar
handle_nmatchers_group(const char *src, size_t *pos, const size_t size, const uint16_t lvl, uchar *hastag, nmatchers *matches, flexarr *result, reliq_error **err) //result: nmatchers_node
{
  size_t i = *pos;
  if (++i >= size) {
    END_OF_RANGE: ;
    *err = script_err("node: %lu: unprecedented end of group",i-1);
    goto ERR;
  }

  flexarr *groups_matches = flexarr_init(sizeof(nmatchers),NODE_MATCHES_INC);
  uchar wastag = 0;

  uint8_t type_acc = NM_DEFAULT;

  while (1) {
    uchar tag = *hastag;
    nmatchers *match = flexarr_inc(groups_matches);

    if ((*err = handle_nmatchers(src,&i,size,lvl+1,match,&tag,NULL,NULL,matches->type))) {
      flexarr_dec(groups_matches);
      goto ERR;
    }
    if (!*hastag && wastag && !tag) {
      *err = script_err("node: %lu: if one group specifies tag then the rest has too",i);
      goto ERR;
    }
    wastag = tag;

    nmatchers_type_merge(match->type,&type_acc);

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
      *err = script_err("node: empty group will always pass");
      goto ERR;
    }*/ //future warning

    i++;
  }

  if (!*hastag)
    *hastag = wastag;

  /*if (groups_matches->size < 2) {
    free_node_matches_flexarr(groups_matches);
    *err = script_err("node: groups must have at least 2 groups to affect anything");
    goto ERR;
  }*/ //future warning

  nmatchers_type_merge(type_acc,&matches->type);

  nmatchers_groups groups;
  flexarr_conv(groups_matches,(void**)&groups.list,&groups.size);
  nmatchers_node_add(result,MATCHES_TYPE_GROUPS,&groups,sizeof(nmatchers_groups));

  *pos = i;
  return 0;

  ERR: ;
  *pos = i;
  return 1;
}

static reliq_error *
match_hook_add_access_type(const size_t pos, const reliq_hook *hook, const uchar invert, const uchar fullmode, uint16_t *nodeflags, uchar *typehooks_count, uint8_t *type, flexarr *result) //result: nmatchers_node
{
  const uchar isaccess = ((hook->hook->flags&H_ACCESS) > 0);
  if (invert)
    return script_err("%s hook \"%s\" cannot be inverted",
      isaccess ? "access" : "type",hook->hook->name.b);

  if (isaccess) {
    if (!fullmode)
      return script_err("node: %lu: groups cannot have access hooks",pos);
    *nodeflags = (*nodeflags&(~N_MATCHED_TYPE)) | hook->hook->arg;
  } else {
    if (*typehooks_count)
      return script_err("hook \"%s\": type hooks can be specified only once",hook->hook->name.b);
    if (result->size != 0)
      return script_err("hook \"%s\": type hooks have to be specified before everything else",hook->hook->name.b);
    if (nmatchers_type_conflict(*type,hook->hook->arg))
      return script_err("hook \"%s\" is in conflict with higher type hook",hook->hook->name.b);

    *type = hook->hook->arg;
    (*typehooks_count)++;
  }

  return NULL;
}

static uchar
hook_add(const char *src, size_t *pos, const size_t size, const uchar invert, uint8_t *type, uchar fullmode, uint16_t *nodeflags, uchar *typehooks_count, flexarr *result, reliq_error **err) //result: nmatchers_node
{
  reliq_hook hook;
  size_t prev = *pos;
  if ((*err = hook_handle(src,pos,size,&hook,*type)))
    goto ERR;

  if (!hook.hook)
    goto ERR;

  if (*pos == prev)
    goto ERR;

  const uint16_t hflags = hook.hook->flags;

  if (hflags&(H_TYPE|H_ACCESS)) {
    if ((*err = match_hook_add_access_type(*pos,&hook,invert,fullmode,nodeflags,typehooks_count,type,result)))
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
  if ((err = reliq_regcomp(&tag,src,pos,size,' ',NULL,strclass_tagname)))
    return err;
  reliq_hook h = {
      .match.pattern = tag,
      .invert = invert,
      .hook = find_main_hook(H_MATCH_NODE_MAIN)
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
    if ((err = reliq_regcomp(&attrib.r[0],t_name,&t_pos,t_size,' ',"uWsfi",strclass_attrib)))
      goto END;

    if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',"uwsf",NULL)))
      goto END;
    attrib.flags |= A_VAL_MATTERS;
  } else {
    if ((err = reliq_regcomp(&attrib.r[0],src,&i,size,'=',NULL,strclass_attrib)))
      goto END;

    while_is(isspace,src,i,size);
    if (i >= size)
      goto ADD_ATTRIB;
    if (src[i] == '=') {
      i++;
      while_is(isspace,src,i,size);
      if (i >= size)
        goto END;

      if ((err = reliq_regcomp(&attrib.r[1],src,&i,size,' ',NULL,NULL)))
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
  if ((err = reliq_regcomp(&tag,src,pos,size,' ',"at",NULL)))
    goto END;

  *hastag = 1;

  reliq_hook h = {
    .match.pattern = tag,
    .hook = find_main_hook(main_hook_mask),
    .invert = invert
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
handle_nmatchers_position(const char *src, size_t *pos, const size_t size, reliq_range *position, uchar *fullmode, uchar *hastag, uint16_t *nodeflags, reliq_error **err)
{
  size_t i = *pos;
  const char *r = memchr(src+i,']',size-i);
  if (r == NULL)
    goto ERR;

  r++;
  if ((size_t)(r-src) < size && !isspace(*r))
    goto ERR;

  if (*fullmode) {
    if (position->s) {
      *err = script_err("node: %lu: position already declared",r-src);
      goto ERR;
    }
  } else {
    *err = script_err("node: %lu: groups cannot have position",r-src);
    goto ERR;
  }

  if ((*err = range_comp(src,&i,size,position)))
    goto ERR;

  if (!*hastag)
    *nodeflags |= N_POSITION_ABSOLUTE;

  *pos = r-src;
  return 1;

  ERR: ;
  *pos = i;
  return 0;
}

static reliq_error *
handle_nmatchers(const char *src, size_t *pos, const size_t size, const uint16_t lvl, nmatchers *matches, uchar *hastag, reliq_range *position, uint16_t *nodeflags, const uint8_t prevtype)
{
  if (lvl >= RELIQ_MAX_GROUP_LEVEL)
    return script_err("node: %lu: reached %lu level of recursion",*pos,lvl);
  reliq_error *err = NULL;
  flexarr *result = flexarr_init(sizeof(nmatchers_node),NODE_MATCHES_INC);
  matches->size = 0;
  matches->list = NULL;
  matches->type = prevtype;

  uchar fullmode=0,typehooks_count=0;
  if (position)
    fullmode = 1;

  size_t i = *pos;
  while (i < size) {
    while_is(isspace,src,i,size);
    if (i >= size)
      break;

    if (src[i] == ')') {
      if (fullmode)
        err = script_err("node: %lu: unexpected '%c'",i,src[i]);
      i++;
      break;
    }

    if (src[i] == '(') {
      if (handle_nmatchers_group(src,&i,size,lvl,hastag,matches,result,&err))
        break;
      continue;
    }

    if (src[i] == '[') {
      if (handle_nmatchers_position(src,&i,size,position,&fullmode,hastag,nodeflags,&err))
        continue;
      if (err)
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

    if (isalpha(src[i]) || src[i] == '@') {
      uchar r = hook_add(src,&i,size,invert,&matches->type,fullmode,nodeflags,&typehooks_count,result,&err);
      if (r) {
        if (r == 2) {
          if (matches->type == NM_DEFAULT)
            matches->type = NM_NODE;
          if (matches->type == NM_MULTIPLE)
            goto ERR_MULTIPLE;
        }
        continue;
      }
      if (err)
        break;
    }

    if (i >= size)
      break;

    if (matches->type == NM_MULTIPLE) {
      ERR_MULTIPLE: ;
      err = script_err("node %lu: multiple types cannot be mixed",i);
      break;
    }

    switch(matches->type) {
      case NM_DEFAULT:
        matches->type = NM_NODE;
      case NM_NODE:
        err = comp_node(src,&i,size,invert,hastag,result);
        break;
      case NM_COMMENT:
        err = comp_comment(src,&i,size,invert,hastag,result);
        break;
      default:
        err = comp_text(src,&i,size,invert,hastag,result);
        break;
    }
    if (err)
      break;
  }

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
  nodep->matches.type = NM_DEFAULT;
  if (pos >= size) {
    nodep->flags |= N_EMPTY;
    if (pos)
      goto END_FREE;
    return NULL;
  }

  uchar hastag=0;

  err = handle_nmatchers(script,&pos,size,0,&nodep->matches,&hastag,&nodep->position,&nodep->flags,NM_DEFAULT);
  if (!err && nodep->matches.size == 0 && nodep->matches.type == NM_DEFAULT)
    nodep->flags |= N_EMPTY;

  if (err) {
    END_FREE: ;
    reliq_nfree(nodep);
  } else
    nodep->position_max = predict_range_max(&nodep->position);

  return err;
}
