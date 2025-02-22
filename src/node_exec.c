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

#include "flexarr.h"
#include "reliq.h"
#include "output.h"
#include "npattern_intr.h"
#include "node_exec.h"

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
match_full(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t desccount = current->tag_count+current->text_count+current->comment_count;
  for (size_t i = 0; i <= desccount && *found < lasttofind; i++)
    match_add(rq,current+i,current,nodep,dest,found);
}

static void
match_child(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t desccount = current->tag_count+current->text_count+current->comment_count;
  for (size_t i = 1; i <= desccount && *found < lasttofind; i += current[i].tag_count+current[i].text_count+current[i].comment_count+1)
    match_add(rq,current+i,current,nodep,dest,found);
}

static void
match_descendant(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind) //dest: reliq_compressed
{
  const uint32_t desccount = current->tag_count+current->text_count+current->comment_count;
  for (size_t i = 1; i <= desccount && *found < lasttofind; i++)
    match_add(rq,current+i,current,nodep,dest,found);
}

static void
match_sibling_preceding(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
{
  reliq_chnode *nodes = rq->nodes;
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
match_sibling_subsequent(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
{
  reliq_chnode *nodes = rq->nodes;
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
      i += nodes[i].tag_count+nodes[i].text_count+nodes[i].comment_count;
  }
}

static void
match_sibling(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
{
  match_sibling_preceding(rq,nodep,current,dest,found,lasttofind,depth);
  match_sibling_subsequent(rq,nodep,current,dest,found,lasttofind,depth);
}

static void
match_ancestor(const reliq *rq, reliq_npattern *nodep, const reliq_chnode *current, flexarr *dest, uint32_t *found, const uint32_t lasttofind, const uint16_t depth) //dest: reliq_compressed
{
  reliq_chnode *nodes=rq->nodes;
  const reliq_chnode *first=current;

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
node_exec_first(const reliq *rq, const reliq_chnode *parent, reliq_npattern *nodep, flexarr *dest, const uint32_t lasttofind) //dest: reliq_compressed
{
  const size_t nodesl = rq->nodesl;
  uint32_t found = 0;
  for (size_t i = 0; i < nodesl && found < lasttofind; i++)
    match_add(rq,rq->nodes+i,parent,nodep,dest,&found);

  if (nodep->position.s)
    dest_match_position(&nodep->position,dest,0,dest->size);
}

void
node_exec(const reliq *rq, const reliq_chnode *parent, reliq_npattern *nodep, const flexarr *source, flexarr *dest) //source: reliq_compressed, dest: reliq_compressed
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

  const reliq_chnode *nodes = rq->nodes;
  const size_t size = source->size;
  for (size_t i = 0; i < size; i++) {
    const reliq_compressed *x = &((reliq_compressed*)source->v)[i];
    const reliq_chnode *hn = nodes+x->hnode;
    if (OUTFIELDCODE(x->hnode))
      continue;
    size_t prevdestsize = dest->size;
    const reliq_chnode *hn_parent = nodes+x->parent;

    switch (nodep->flags&N_MATCHED_TYPE) {
      case N_FULL:
        match_full(rq,nodep,hn,dest,&found,lasttofind);
        break;
      case N_SELF:
        match_add(rq,hn,hn_parent,nodep,dest,&found); //!!
        break;
      case N_CHILD:
        match_child(rq,nodep,hn,dest,&found,lasttofind);
        break;
      case N_DESCENDANT:
        match_descendant(rq,nodep,hn,dest,&found,lasttofind);
        break;
      case N_ANCESTOR:
        match_ancestor(rq,nodep,hn,dest,&found,lasttofind,-1);
        break;
      case N_PARENT:
        match_ancestor(rq,nodep,hn,dest,&found,lasttofind,0);
        break;
      case N_RELATIVE_PARENT:
        match_add(rq,hn_parent,hn,nodep,dest,&found);
        break;
      case N_SIBLING:
        match_sibling(rq,nodep,hn,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_PRECEDING:
        match_sibling_preceding(rq,nodep,hn,dest,&found,lasttofind,0);
        break;
      case N_SIBLING_SUBSEQUENT:
        match_sibling_subsequent(rq,nodep,hn,dest,&found,lasttofind,0);
        break;
      case N_FULL_SIBLING:
        match_sibling(rq,nodep,hn,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_PRECEDING:
        match_sibling_preceding(rq,nodep,hn,dest,&found,lasttofind,-1);
        break;
      case N_FULL_SIBLING_SUBSEQUENT:
        match_sibling_subsequent(rq,nodep,hn,dest,&found,lasttofind,-1);
        break;
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
