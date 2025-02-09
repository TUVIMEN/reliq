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

#include "hnode.h"
#include <assert.h>

inline uint32_t
chnode_attribsl(const reliq *rq, const reliq_chnode *hnode)
{
  size_t nextindex = hnode-rq->nodes+1;
  if (nextindex >= rq->nodesl) {
    assert(rq->attribsl-hnode->attribs <= rq->attribsl);
    return rq->attribsl-hnode->attribs;
  }
  const reliq_chnode *next = rq->nodes+nextindex;

  assert(next->attribs-hnode->attribs <= rq->attribsl);
  return next->attribs-hnode->attribs;
}

inline uint8_t
chnode_type(const reliq_chnode *c)
{
  if (c->tag == 0) {
    if (c->endtag == 0) {
      return RELIQ_HNODE_TYPE_TEXT;
    } else
      return RELIQ_HNODE_TYPE_COMMENT;
  }
  return RELIQ_HNODE_TYPE_TAG;
}

static inline uint32_t
chnode_insides_try(const reliq *rq, const reliq_chnode *hnode, const uint8_t type)
{
  if (type == RELIQ_HNODE_TYPE_TEXT)
    return 0;
  if (type == RELIQ_HNODE_TYPE_COMMENT)
    return hnode->tagl;

  if (hnode->tag_count+hnode->text_count+hnode->comment_count == 0) {
    if (rq->data[hnode->all+hnode->tag+hnode->tagl+hnode->endtag] == '<')
      return hnode->endtag;
    return 0;
  }
  const reliq_chnode *next = hnode+1;

  assert(next->all-hnode->all-hnode->tag-hnode->tagl <= rq->datal);
  return next->all-hnode->all-hnode->tag-hnode->tagl;
}

inline uint32_t
chnode_insides(const reliq *rq, const reliq_chnode *hnode, const uint8_t type)
{
  if (type == RELIQ_HNODE_TYPE_COMMENT)
    return hnode->tagl;
  assert(chnode_insides_try(rq,hnode,type) == hnode->insides);
  return hnode->insides;
}

void
chnode_conv(const reliq *rq, const reliq_chnode *c, reliq_hnode *d)
{
  uint8_t type = chnode_type(c);
  d->type = type;

  char const *base = rq->data+c->all;
  d->all = (reliq_cstr){ .b = base, .s = c->all_len };
  if (c->tag) {
    base += c->tag;
    d->tag = (reliq_cstr){ .b = base, .s = c->tagl };
    base += c->tagl;
  } else
    d->tag = (reliq_cstr){ .b = NULL, .s = 0 };

  uint32_t insides = chnode_insides(rq,c,type);
  if (insides == 0 && c->endtag == 0) {
    d->insides = (reliq_cstr){NULL,0};
  } else {
    base += insides;
    d->insides = (reliq_cstr){ .b = base, .s = c->endtag-insides };
  }

  d->attribs = c->attribs+rq->attribs;
  d->attribsl = chnode_attribsl(rq,c);
  d->lvl = c->lvl;
  d->tag_count = c->tag_count;
  d->text_count = c->text_count;
  d->comment_count = c->comment_count;
}

void
cattrib_conv(const reliq *rq, const reliq_cattrib *c, reliq_attrib *d)
{
  char const *base = rq->data+c->key;
  d->key = (reliq_cstr){ .b = base, .s = c->keyl };
  base += c->keyl+c->value;
  d->key = (reliq_cstr){ .b = base, .s = c->valuel };
}
