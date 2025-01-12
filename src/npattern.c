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
#include <limits.h>
#include <stdint.h>

#include "ctype.h"
#include "utils.h"
#include "range.h"
#include "pattern.h"
#include "exprs.h"
#include "npattern.h"

#define PATTRIB_INC 8
#define NODE_MATCHES_INC 8
#define NODE_MATCHES_GROUPS_INC 4
#define HOOK_INC 8

#define MATCHES_TYPE_TAG 1
#define MATCHES_TYPE_HOOK 2
#define MATCHES_TYPE_ATTRIB 3
#define MATCHES_TYPE_GROUPS 4

//reliq_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//match_hook flags
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

reliq_error *reliq_exec_r(reliq *rq, const reliq_hnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_expr *expr);

struct match_hook {
  reliq_str8 name;
  uint16_t flags; //H_
};

const struct match_hook match_hooks[] = {
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

static inline void
add_compressed(flexarr *dest, reliq_hnode *const hnode, reliq_hnode *const parent) //dest: reliq_compressed
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = hnode;
  x->parent = parent;
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
    reliq_efree(&hook->match.expr);
  } else if (hook->flags&H_PATTERN)
    reliq_regfree(&hook->match.pattern);
}

static void free_matches(reliq_node_matches *matches);

static void
free_matches_group(reliq_node_matches_groups *groups)
{
  const size_t size = groups->size;
  reliq_node_matches *list = groups->list;
  for (size_t i = 0; i < size; i++)
    free_matches(&list[i]);
  free(list);
}

static void
free_matches(reliq_node_matches *matches)
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
match_hook(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_hook *hook)
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

    size_t compressedl = 0;
    reliq_error *err = reliq_exec_r(&r,hnode,NULL,NULL,&compressedl,&hook->match.expr);
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
        if (!match_hook(rq,hnode,parent,list[i].data.hook))
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

static inline uchar
hook_handle_isname(char c)
{
  if (c == '_' || c == '-')
    return 1;
  return isalpha(c);
}

static void
match_add(const reliq *rq, reliq_hnode const *hnode, reliq_hnode const *parent, reliq_npattern const *nodep, flexarr *dest, uint32_t *found) //dest: reliq_compressed
{
  if (!reliq_nexec(rq,hnode,parent,nodep))
    return;
  add_compressed(dest,(reliq_hnode *const)hnode,(reliq_hnode *const)parent);
  (*found)++;
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
     err = reliq_ecomp(str,strl,&hook.match.expr);
     free(str);
     if (err)
       goto ERR;
     if ((err = expr_check_chain(&hook.match.expr,0))) {
       reliq_efree(&hook.match.expr);
       goto ERR;
     }
  } else {
    HOOK_EXPECT(H_PATTERN);
    char *rflags = "uWcas";
    if ((hook.flags&H_KINDS) == H_MATCH_END)
      rflags = "tWcnfs";
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
reliq_node_matches_node_add(flexarr *arr, uchar type, void *data, const size_t size) //arr: reliq_node_matches
{
  reliq_node_matches_node *new = flexarr_inc(arr);
  new->type = type;
  new->data.hook = memdup(data,size);
}

static void
free_node_matches_flexarr(flexarr *groups_matches) //group_matches: reliq_node_matches
{
  reliq_node_matches *matchesv = (reliq_node_matches*)groups_matches->v;
  const size_t size = groups_matches->size;
  for (size_t i = 0; i < size; i++)
    free_matches(&matchesv[i]);
  flexarr_free(groups_matches);
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

static void
dest_match_position(const reliq_range *range, flexarr *dest, size_t start, size_t end) { //dest: reliq_compressed
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
match_full(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 0; i <= childcount && *found < lasttofind; i++) {
    match_add(rq,current+i,current,nodep,dest,found);
  }
}

static void
match_child(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 1; i <= childcount && *found < lasttofind; i += current[i].desc_count+1)
    match_add(rq,current+i,current,nodep,dest,found);
}

static void
match_descendant(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t childcount = current->desc_count;
  for (size_t i = 1; i <= childcount && *found < lasttofind; i++)
    match_add(rq,current+i,current,nodep,dest,found);
}

static void
match_sibling_preceding(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
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
      match_add(rq,nodes+i,current,nodep,dest,found);
    if (!i)
      break;
  }
}

static void
match_sibling_subsequent(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
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
      match_add(rq,nodes+i,current,nodep,dest,found);

    if (nodes[i].lvl == lvldiff)
      i += nodes[i].desc_count;
  }
}

static void
match_sibling(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
{
  match_sibling_preceding(rq,nodep,current,dest,found,lasttofind,depth);
  match_sibling_subsequent(rq,nodep,current,dest,found,lasttofind,depth);
}

static void
match_ancestor(const reliq *rq, reliq_npattern *nodep, const reliq_hnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
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

    match_add(rq,current,first,nodep,dest,found);

    if (current->lvl == 0)
      break;
  }
}

static void
node_exec_first(const reliq *rq, const reliq_hnode *parent, reliq_npattern *nodep, flexarr *dest, const uint32_t lasttofind) //dest: reliq_compressed
{
  const size_t nodesl = rq->nodesl;
  uint32_t found = 0;
  for (size_t i = 0; i < nodesl && found < lasttofind; i++)
    match_add(rq,rq->nodes+i,parent,nodep,dest,&found);

  if (nodep->position.s)
    dest_match_position(&nodep->position,dest,0,dest->size);
}

void
node_exec(const reliq *rq, const reliq_hnode *parent, reliq_npattern *nodep, const flexarr *source, flexarr *dest) //source: reliq_compressed, dest: reliq_compressed
{
  uint32_t found=0,lasttofind=nodep->position_max;
  if (lasttofind == (uint32_t)-1)
    return;
  if (lasttofind == 0)
    lasttofind = -1;

  if (source->size == 0) {
    node_exec_first(rq,parent,nodep,dest,lasttofind);
    return;
  }

  const size_t size = source->size;
  for (size_t i = 0; i < size; i++) {
    const reliq_compressed *x = &((reliq_compressed*)source->v)[i];
    const reliq_hnode *current = x->hnode;
    if ((void*)current < (void*)10)
      continue;
    size_t prevdestsize = dest->size;

    switch (nodep->flags&N_MATCHED_TYPE) {
      case N_FULL:
        match_full(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_SELF:
        match_add(rq,current,x->parent,nodep,dest,&found); //!!
        break;
      case N_CHILD:
        match_child(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_DESCENDANT:
        match_descendant(rq,nodep,current,dest,&found,lasttofind);
        break;
      case N_ANCESTOR:
        match_ancestor(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_PARENT:
        match_ancestor(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_RELATIVE_PARENT:
        match_add(rq,x->parent,current,nodep,dest,&found);
        break;
      case N_SIBLING:
        match_sibling(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_PRECEDING:
        match_sibling_preceding(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_SUBSEQUENT:
        match_sibling_subsequent(rq,nodep,current,dest,&found,lasttofind,0);
        break;
      case N_FULL_SIBLING:
        match_sibling(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_PRECEDING:
        match_sibling_preceding(rq,nodep,current,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_SUBSEQUENT:
        match_sibling_subsequent(rq,nodep,current,dest,&found,lasttofind,-1);
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
