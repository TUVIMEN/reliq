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

uint32_t
chnode_attribsl(const reliq *rq, const reliq_chnode *hnode)
{
  size_t index = hnode-rq->nodes;
  if (index >= rq->nodesl-1) {
    assert(rq->attribsl-hnode->attribs <= rq->attribsl);
    return rq->attribsl-hnode->attribs;
  }

  assert(rq->nodes[index+1].attribs-hnode->attribs <= rq->attribsl);
  return rq->nodes[index+1].attribs-hnode->attribs;
}

inline uint8_t
chnode_type(const reliq_chnode *c)
{
  if (c->tag == 0 && c->tagl == 0) {
    if (c->insides == 0 && c->insidesl == 0) {
      return RELIQ_HNODE_TYPE_TEXT;
    } else
      return RELIQ_HNODE_TYPE_COMMENT;
  }
  return RELIQ_HNODE_TYPE_TAG;
}

void
chnode_conv(const reliq *rq, const reliq_chnode *c, reliq_hnode *d)
{
  char const *base = rq->data+c->all;
  d->all = (reliq_cstr){ .b = base, .s = c->all_len };
  base += c->tag;
  d->tag = (reliq_cstr){ .b = base, .s = c->tagl };
  base += c->tagl+c->insides;
  if (c->insides == 0 && c->insidesl == 0) {
    d->insides = (reliq_cstr){NULL,0};
  } else
    d->insides = (reliq_cstr){ .b = base, .s = c->insidesl };

  d->attribs = c->attribs+rq->attribs;
  d->attribsl = chnode_attribsl(rq,c);
  d->lvl = c->lvl;
  d->tag_count = c->tag_count;
  d->text_count = c->text_count;
  d->comment_count = c->comment_count;

  d->type = chnode_type(c);
}

void
cattrib_conv(const reliq *rq, const reliq_cattrib *c, reliq_attrib *d)
{
  char const *base = rq->data+c->key;
  d->key = (reliq_cstr){ .b = base, .s = c->keyl };
  base += c->keyl+c->value;
  d->key = (reliq_cstr){ .b = base, .s = c->valuel };
}
