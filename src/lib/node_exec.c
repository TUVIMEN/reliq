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

#include <assert.h>

#include "reliq.h"
#include "output.h"
#include "npattern_intr.h"
#include "utils.h"
#include "node_exec.h"

const struct axis_incompability {
  uint16_t type;
  uint16_t incompatible;
} axis_incompabilities[] = {
  {AXIS_DESCENDANTS,AXIS_CHILDREN},
  {AXIS_ANCESTORS,AXIS_PARENT},
  {AXIS_PRECEDING,AXIS_SIBLINGS_PRECEDING|AXIS_FULL_SIBLINGS_PRECEDING},
  {AXIS_BEFORE,AXIS_FULL_SIBLINGS_PRECEDING|AXIS_SIBLINGS_PRECEDING|AXIS_ANCESTORS|AXIS_PARENT},
  {AXIS_SUBSEQUENT,AXIS_SIBLINGS_SUBSEQUENT|AXIS_FULL_SIBLINGS_SUBSEQUENT},
  {AXIS_AFTER,AXIS_SIBLINGS_SUBSEQUENT|AXIS_FULL_SIBLINGS_SUBSEQUENT|AXIS_DESCENDANTS|AXIS_CHILDREN},
  {AXIS_EVERYTHING,-1}
};

const struct axis_replacement {
  uint16_t type;
  uint16_t substituted;
} axis_replacements[] = {
  {AXIS_BEFORE,AXIS_PRECEDING|AXIS_ANCESTORS},
  {AXIS_AFTER,AXIS_SUBSEQUENT|AXIS_DESCENDANTS},
  {AXIS_EVERYTHING,AXIS_SELF|AXIS_BEFORE|AXIS_AFTER},
};

static inline void
add_compressed(flexarr *dest, const uint32_t hnode, const uint32_t parent) //dest: reliq_compressed
{
  reliq_compressed *x = flexarr_inc(dest);
  x->hnode = hnode;
  x->parent = parent;
}

static inline void
match_add(const reliq *rq, reliq_chnode const *hnode, reliq_chnode const *parent, reliq_npattern const *nodep, flexarr *dest, uint32_t *found) //dest: reliq_compressed
{
  if (!reliq_nexec(rq,hnode,parent,nodep))
    return;
  add_compressed(dest,hnode-rq->nodes,
    parent ? parent-rq->nodes : (uint32_t)-1);
  (*found)++;
}

#define XN(x) match_##x
#define X(x) static void XN(x)(const UNUSED reliq *rq, const UNUSED reliq_npattern *nodep, const UNUSED reliq_chnode *current, const UNUSED reliq_chnode *parent, flexarr *dest, uint32_t UNUSED *found, const uint32_t UNUSED lasttofind)

X(descendants) {
  const uint32_t desccount = current->tag_count+current->text_count+current->comment_count;
  for (size_t i = 1; i <= desccount; i++) {
    match_add(rq,current+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

X(only_self) {
  match_add(rq,current,parent,nodep,dest,found);
}

X(self) {
  match_add(rq,current,current,nodep,dest,found);
}

X(children) {
  const uint32_t desccount = current->tag_count+current->text_count+current->comment_count;
  for (size_t i = 1; i <= desccount; i += current[i].tag_count+current[i].text_count+current[i].comment_count+1) {
    match_add(rq,current+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

X(relative_parent) {
  if (parent)
    match_add(rq,parent,current,nodep,dest,found);
}

static inline const reliq_chnode *
find_parent(const reliq_chnode *nodes, const reliq_chnode *current)
{
  uint16_t lvl = current->lvl-1;
  for (size_t j=(current-nodes)-1; nodes[j].lvl >= lvl; j--) {
    if (nodes[j].lvl == lvl)
      return &nodes[j];

    if (!j)
      break;
  }

  return NULL;
}

X(ancestors) {
  const reliq_chnode *nodes=rq->nodes;
  const reliq_chnode *first=current;

  while (current != nodes) {
    current = find_parent(nodes,current);
    if (!current)
      break;

    match_add(rq,current,first,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

X(parent) {
  const reliq_chnode *p = find_parent(rq->nodes,current);
  if (!p)
    return;

  match_add(rq,p,current,nodep,dest,found);
}

static inline void
siblings_preceding(const reliq *rq, const reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uchar full)
{
  const reliq_chnode *nodes = rq->nodes;
  if (nodes == current)
    return;
  const uint16_t lvl = current->lvl;

  for (size_t i=(current-nodes)-1; nodes[i].lvl >= lvl; i--) {
    if (full || nodes[i].lvl == lvl) {
      match_add(rq,nodes+i,current,nodep,dest,found);
      if (*found >= lasttofind)
        return;
    }
    if (!i)
      break;
  }
}

X(siblings_preceding) {
  siblings_preceding(rq,nodep,current,dest,found,lasttofind,0);
}

X(full_siblings_preceding) {
  siblings_preceding(rq,nodep,current,dest,found,lasttofind,1);
}

X(siblings_subsequent) {
  reliq_chnode *nodes = rq->nodes;
  const size_t nodesl = rq->nodesl;
  const uint16_t lvl = current->lvl;
  const size_t desc = current->tag_count+current->comment_count+current->text_count;

  for (size_t i=(current-nodes)+desc+1; i < nodesl && nodes[i].lvl == lvl;) {
    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;

    i += nodes[i].tag_count+nodes[i].text_count+nodes[i].comment_count+1;
  }
}

X(full_siblings_subsequent) {
  reliq_chnode *nodes = rq->nodes;
  const size_t nodesl = rq->nodesl;
  const uint16_t lvl = current->lvl;
  const size_t desc = current->tag_count+current->comment_count+current->text_count;

  for (size_t i=(current-nodes)+desc+1; i < nodesl && nodes[i].lvl >= lvl; i++) {
    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

X(everything) {
  const size_t nodesl = rq->nodesl;
  const reliq_chnode *nodes = rq->nodes;
  for (size_t i = 0; i < nodesl && *found < lasttofind; i++)
    match_add(rq,nodes+i,current,nodep,dest,found);
}

X(preceding) {
  const reliq_chnode *nodes = rq->nodes;
  if (current == nodes)
    return;

  uint16_t lvl = current->lvl;
  lvl = (lvl == 0) ? (uint16_t)-1 : lvl-1;

  for (size_t i = current-nodes-1; ; i--) {
    if (nodes[i].lvl == lvl) {
      if (i == 0)
        break;
      lvl--;
      continue;
    }

    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind || i == 0)
      return;
  }
}

X(before) {
  const reliq_chnode *nodes = rq->nodes;
  if (current == nodes)
    return;

  for (size_t i = current-nodes-1; ; i--) {
    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind || i == 0)
      return;
  }
}

X(subsequent)
{
  const reliq_chnode *nodes = rq->nodes;
  const size_t desc = current->tag_count+current->comment_count+current->text_count;
  const size_t nodesl = rq->nodesl;

  for (size_t i = (current-nodes)+desc+1; i < nodesl; i++) {
    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

X(after)
{
  const reliq_chnode *nodes = rq->nodes;
  const size_t nodesl = rq->nodesl;

  for (size_t i = (current-nodes)+1; i < nodesl; i++) {
    match_add(rq,nodes+i,current,nodep,dest,found);
    if (*found >= lasttofind)
      return;
  }
}

const struct axis_translation {
    uint16_t type;
    axis_func_t func;
} axis_translations_in_order[] = {
  {AXIS_RELATIVE_PARENT,XN(relative_parent)},
  {AXIS_EVERYTHING,XN(everything)},

  {AXIS_BEFORE,XN(before)},
  {AXIS_PRECEDING,XN(preceding)},

  {AXIS_ANCESTORS,XN(ancestors)},
  {AXIS_PARENT,XN(parent)},

  {AXIS_SIBLINGS_PRECEDING,XN(siblings_preceding)},
  {AXIS_FULL_SIBLINGS_PRECEDING,XN(full_siblings_preceding)},

  {AXIS_SELF,XN(self)},

  {AXIS_CHILDREN,XN(children)},
  {AXIS_DESCENDANTS,XN(descendants)},

  {AXIS_SIBLINGS_SUBSEQUENT,XN(siblings_subsequent)},
  {AXIS_FULL_SIBLINGS_SUBSEQUENT,XN(full_siblings_subsequent)},
  {AXIS_SUBSEQUENT,XN(subsequent)},
  {AXIS_AFTER,XN(after)},
};

#undef X

static uint16_t
axis_remove_incompatible(uint16_t type)
{
  for (size_t i = 0; i < LENGTH(axis_incompabilities); i++) {
    uint16_t itype = axis_incompabilities[i].type;
    if ((itype&type) == itype)
      type = (type&(~axis_incompabilities[i].incompatible))|itype;
  }
  return type;
}

static uint16_t
axis_replace(uint16_t type)
{
  type = axis_remove_incompatible(type);
  for (size_t i = 0; i < LENGTH(axis_replacements); i++) {
    uint16_t subs = axis_replacements[i].substituted;
    if ((type&subs) != subs)
      continue;

    type = (type&(~subs))|axis_replacements[i].type;
    type = axis_remove_incompatible(type);
  }
  return type;
}

void
axis_comp_functions(uint16_t type, axis_func_t *out)
{
  type = axis_replace(type);
  if (type == AXIS_SELF) {
    out[0] = XN(only_self);
    out[1] = NULL;
    return;
  }

  size_t len = 0;
  for (size_t i = 0; i < LENGTH(axis_translations_in_order); i++) {
    uint16_t itype = axis_translations_in_order[i].type;
    if ((type&itype) != itype)
      continue;

    assert(len != AXIS_FUNCS_MAX);
    out[len++] = axis_translations_in_order[i].func;
  }
  if (len != AXIS_FUNCS_MAX)
    out[len] = NULL;
}

#undef XN

static void
axis_run(const reliq *rq, const reliq_npattern *nodep, const reliq_chnode *current, const reliq_chnode *parent, flexarr *dest, uint32_t *found, const uint32_t lasttofind)
{
  for (size_t i = 0; i < AXIS_FUNCS_MAX && nodep->axis_funcs[i] != NULL && *found < lasttofind; i++)
    ((axis_func_t)nodep->axis_funcs[i]) (rq,nodep,current,parent,dest,found,lasttofind);
}

static void
dest_match_position(const reliq_range *range, flexarr *dest, size_t start, size_t end) { //dest: reliq_compressed
  reliq_compressed *x = (reliq_compressed*)dest->v;

  while (start < end && OUTFIELDCODE(x[start].hnode))
    start++;
  while (end != start && OUTFIELDCODE(x[end-1].hnode))
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
node_exec_first(const reliq *rq, const reliq_npattern *nodep, flexarr *dest, const uint32_t lasttofind) //dest: reliq_compressed
{
  const size_t nodesl = rq->nodesl;
  uint32_t found = 0;
  for (size_t i = 0; i < nodesl && found < lasttofind; i++)
    match_add(rq,rq->nodes+i,NULL,nodep,dest,&found);

  if (nodep->position.s)
    dest_match_position(&nodep->position,dest,0,dest->size);
}

void
node_exec(const reliq *rq, const reliq_npattern *nodep, const flexarr *source, flexarr *dest) //source: reliq_compressed, dest: reliq_compressed
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

  const reliq_chnode *nodes = rq->nodes;
  const size_t size = source->size;
  for (size_t i = 0; i < size; i++) {
    const reliq_compressed *x = &((reliq_compressed*)source->v)[i];
    const reliq_chnode *hn = nodes+x->hnode;
    if (OUTFIELDCODE(x->hnode))
      continue;
    size_t prevdestsize = dest->size;
    const reliq_chnode *hn_parent = (x->parent == (uint32_t)-1) ? NULL : nodes+x->parent;

    axis_run(rq,nodep,hn,hn_parent,dest,&found,lasttofind);

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
