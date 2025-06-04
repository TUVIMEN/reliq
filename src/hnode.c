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

#include "ext.h"
#include "reliq.h"

uint32_t
reliq_chnode_attribsl(const reliq *rq, const reliq_chnode *hnode)
{
  size_t nextindex = hnode-rq->nodes+1;
  if (nextindex >= rq->nodesl)
    return rq->attribsl-hnode->attribs;
  const reliq_chnode *next = rq->nodes+nextindex;

  return next->attribs-hnode->attribs;
}

uint8_t
reliq_chnode_type(const reliq_chnode *c)
{
  if (c->tag == 0) {
    if (c->endtag == 0) {
      if (c->tagl == 1)
        return RELIQ_HNODE_TYPE_TEXT_EMPTY;
      if (c->tagl == 2)
        return RELIQ_HNODE_TYPE_TEXT_ERR;
      return RELIQ_HNODE_TYPE_TEXT;
    } else
      return RELIQ_HNODE_TYPE_COMMENT;
  }
  return RELIQ_HNODE_TYPE_TAG;
}

uint32_t
reliq_chnode_insides(const reliq *rq, const reliq_chnode *hnode, const uint8_t type)
{
  if (type == RELIQ_HNODE_TYPE_COMMENT)
    return hnode->tagl;
  if (type != RELIQ_HNODE_TYPE_TAG)
    return 0;

  const uint32_t base = hnode->all+hnode->tag+hnode->tagl;
  if (hnode->tag_count+hnode->text_count+hnode->comment_count == 0) {
    if (rq->data[base+hnode->endtag] == '<')
      return hnode->endtag;
    return 0;
  }
  const reliq_chnode *next = hnode+1;

  return next->all-base;
}

void
reliq_chnode_conv(const reliq *rq, const reliq_chnode *c, reliq_hnode *d)
{
  uint8_t type = reliq_chnode_type(c);
  d->type = type;

  char const *base = rq->data+c->all;
  d->all = (reliq_cstr){ .b = base, .s = c->all_len };
  if (c->tag) {
    base += c->tag;
    d->tag = (reliq_cstr){ .b = base, .s = c->tagl };
    base += c->tagl;
  } else
    d->tag = (reliq_cstr){ .b = NULL, .s = 0 };

  const uint32_t insides = reliq_chnode_insides(rq,c,type);
  if (insides == 0 && c->endtag == 0) {
    d->insides = (reliq_cstr){NULL,0};
  } else {
    base += insides;
    d->insides = (reliq_cstr){ .b = base, .s = c->endtag-insides };
  }

  d->attribs = c->attribs+rq->attribs;
  d->attribsl = reliq_chnode_attribsl(rq,c);
  d->lvl = c->lvl;
  d->tag_count = c->tag_count;
  d->text_count = c->text_count;
  d->comment_count = c->comment_count;
}

void
reliq_cattrib_conv(const reliq *rq, const reliq_cattrib *c, reliq_attrib *d)
{
  char const *base = rq->data+c->key;
  d->key = (reliq_cstr){ .b = base, .s = c->keyl };
  base += c->keyl+c->value;
  d->value = (reliq_cstr){ .b = base, .s = c->valuel };
}

const char *
reliq_hnode_endtag(const reliq_hnode *hn, size_t *len)
{
  *len = 0;
  if (!hn->insides.b)
    return NULL;
  *len = hn->all.s-(hn->insides.b-hn->all.b)-hn->insides.s;
  if (!*len)
    return NULL;
  return hn->insides.b+hn->insides.s;
}

const char *
reliq_hnode_endtag_strip(const reliq_hnode *hn, size_t *len)
{
  char const *ret = reliq_hnode_endtag(hn,len);
  if (!ret)
    return ret;
  ret++;
  (*len)--;
  if (*len > 0 && ret[*len-1] == '>')
    (*len)--;
  return ret;
}

const char *
reliq_hnode_starttag(const reliq_hnode *hn, size_t *len)
{
  *len = (hn->insides.b)
    ? (size_t)(hn->insides.b-hn->all.b)
    : hn->all.s;
  return hn->all.b;
}
