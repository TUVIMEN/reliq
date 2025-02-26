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

#include "reliq.h"
#include "range.h"
#include "npattern_intr.h"

typedef void (*hook_func_t)(const reliq *rq, const reliq_chnode *chnode, const reliq_hnode *hnode, const reliq_chnode *parent, char const **src, size_t *srcl);

typedef struct  {
  const reliq *rq;
  const reliq_chnode *chnode;
  const reliq_chnode *parent;
  const reliq_hnode *hnode;
} nmatcher_state;

reliq_error *reliq_exec_r(reliq *rq, const reliq_chnode *parent, SINK *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_expr *expr);

static int
pattrib_match(const reliq *rq, const reliq_hnode *hnode, const struct pattrib *attrib)
{
  uchar found = 0;
  const reliq_cattrib *a = hnode->attribs;
  const uint32_t attribsl = hnode->attribsl;

  for (uint32_t i = 0; i < attribsl; i++) {
    if (!range_match(i,&attrib->position,attribsl-1))
      continue;

    char const* base = rq->data+a[i].key;
    if (!reliq_regexec(&attrib->r[0],base,a[i].keyl))
      continue;

    if (attrib->flags&A_VAL_MATTERS) {
      base += a[i].keyl+a[i].value;
      if (!reliq_regexec(&attrib->r[1],base,a[i].valuel))
        continue;
    }

    found = 1;
    break;
  }
  if (((attrib->flags&A_INVERT)!=A_INVERT)^found)
    return 0;
  return 1;
}

static int
exprs_match(const reliq *rq, const reliq_chnode *chnode, const reliq_hook *hook)
{
  const size_t desccount = chnode->tag_count+chnode->text_count+chnode->comment_count;
  reliq r = {
    .data = rq->data,
    .datal = rq->datal,
    .nodes = (reliq_chnode*)chnode,
    .nodesl = desccount+1,
    .attribs = rq->attribs
  };
  const reliq_chnode *last = chnode+desccount;
  r.attribsl = last->attribs+reliq_chnode_attribsl(rq,last);

  size_t compressedl = 0;
  reliq_error *err = reliq_exec_r(&r,chnode,NULL,NULL,&compressedl,&hook->match.expr);
  if (err)
    free(err);
  if (err || !compressedl)
    return 0;
  return 1;
}

static int
match_hook(const nmatcher_state *st, const reliq_hook *hook)
{
  char const *src = NULL;
  size_t srcl = 0;
  uint16_t flags = hook->hook->flags;
  const uchar invert = hook->invert;
  const reliq *rq = st->rq;
  const reliq_chnode *chnode = st->chnode;
  const reliq_chnode *parent = st->parent;
  const reliq_hnode *hnode = st->hnode;

  const uintptr_t arg = hook->hook->arg;
  if (arg)
    ((hook_func_t)arg)(rq,chnode,hnode,parent,&src,&srcl);

  if (flags&H_RANGE_UNSIGNED) {
    if ((!range_match(srcl,&hook->match.range,RANGE_UNSIGNED))^invert)
      return 0;
  } else if (flags&H_RANGE_SIGNED) {
    if ((!range_match(srcl,&hook->match.range,RANGE_SIGNED))^invert)
      return 0;
  } else if (flags&H_PATTERN) {
    if ((!reliq_regexec(&hook->match.pattern,src,srcl))^invert)
      return 0;
  } else if (flags&H_EXPRS) {
    if (!exprs_match(rq,chnode,hook)^invert)
      return 0;
  }
  return 1;
}

static int nmatcher_match(const nmatcher_state *st, const nmatchers *matchers);

static int
nmatcher_groups_match(const nmatcher_state *st, const nmatchers_groups *groups)
{
  const size_t size = groups->size;
  const nmatchers *list = groups->list;
  for (size_t i = 0; i < size; i++)
    if (nmatcher_match(st,&list[i]))
      return 1;
  return 0;
}

static inline int
nmatcher_match_type(const uint8_t hnode_type, const uint8_t type)
{
  if (type == NM_MULTIPLE)
    return 1;

  if (type == NM_TAG || type == NM_DEFAULT)
    return (hnode_type == RELIQ_HNODE_TYPE_TAG);

  if (type == NM_COMMENT)
    return (hnode_type == RELIQ_HNODE_TYPE_COMMENT);

  uchar istext = (hnode_type == RELIQ_HNODE_TYPE_TEXT);
  uchar istexterr = (hnode_type == RELIQ_HNODE_TYPE_TEXT_ERR);
  uchar istextempty = (hnode_type == RELIQ_HNODE_TYPE_TEXT_EMPTY);
  if (type == NM_TEXT_ALL)
    return (istext || istexterr || istextempty);

  if (type == NM_TEXT_EMPTY)
    return istextempty;

  if (type == NM_TEXT_ERR)
    return istexterr;

  if (type == NM_TEXT_NOERR)
    return istext;

  if (type == NM_TEXT)
    return (istexterr|istext);

  //if (type == NM_TEXT_ALL)
  return (istextempty|istexterr|istext);
}

static int
nmatcher_match(const nmatcher_state *st, const nmatchers *matchers)
{
  const size_t size = matchers->size;
  nmatchers_node *list = matchers->list;

  if (!nmatcher_match_type(st->hnode->type,matchers->type))
    return 0;

  for (size_t i = 0; i < size; i++) {
    switch (list[i].type) {
      case MATCHES_TYPE_HOOK:
        if (!match_hook(st,list[i].data.hook))
          return 0;
        break;
      case MATCHES_TYPE_ATTRIB:
        if (!pattrib_match(st->rq,st->hnode,list[i].data.attrib))
          return 0;
        break;
      case MATCHES_TYPE_GROUPS:
        if (!nmatcher_groups_match(st,list[i].data.groups))
          return 0;
        break;
    }
  }
  return 1;
}

int
reliq_nexec(const reliq *rq, const reliq_chnode *chnode, const reliq_chnode *parent, const reliq_npattern *nodep)
{
  if (nodep->flags&N_EMPTY)
    return 1;

  reliq_hnode hnode;
  reliq_chnode_conv(rq,chnode,&hnode);
  nmatcher_state st = {
    .hnode = &hnode,
    .chnode = chnode,
    .parent = parent,
    .rq = rq
  };
  return nmatcher_match(&st,&nodep->matches);
}
