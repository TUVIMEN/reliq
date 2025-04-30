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
#include "utils.h"
#include "range.h"

#define RANGES_INC (1<<4)

static inline void
match_relative_xinvert(uint32_t *x, uchar *inf, const uint8_t flags, const uint8_t val, const size_t last)
{
  if (flags&R_RELATIVE(0)) {
    if (x == 0) {
      *inf = 1;
    } else
      *x = ((uint32_t)last < val) ? 0 : last-val;
  } else if (flags&R_NOTSPECIFIED(0))
    *inf = 1;
}

static uchar
match_relative(const uint32_t matched, const reliq_range *range, const size_t last)
{
  struct reliq_range_node const *r;
  for (size_t i = 0; i < range->s; i++) {
    r = &range->b[i];
    uint32_t x = r->v[0], y = r->v[1];
    uchar invert = ((r->flags&R_INVERT) != 0);
    uchar xinf=0,yinf=0;
    if (!(r->flags&R_RANGE)) {
      match_relative_xinvert(&x,&xinf,r->flags,r->v[0],last);
      uchar c = (matched == x && !xinf);
      if (c^invert)
        return 1;
    } else {
      match_relative_xinvert(&x,&xinf,r->flags,r->v[0],last);
      uchar c;
      if (r->flags&R_NOTSPECIFIED(1)) {
        yinf = 1;
      } else if (r->flags&R_RELATIVE(1)) {
        c = ((uint32_t)last < r->v[1]);
        if (c && !invert)
          continue;
        if (y == 0) {
          yinf = 1;
        } else
          y = last-r->v[1];
      }

      c = ((matched >= x || xinf) && (matched <= y || yinf) && (r->v[2] < 2 || (matched+r->v[3])%r->v[2] == 0));
      if (c^invert)
        return 1;
    }
  }
  return 0;
}

static inline void
match_signed_invert(int32_t *x, uchar *inf, const uint8_t n, const uint8_t flags)
{
  if (flags&R_NOTSPECIFIED(n)) {
    *inf = 1;
    return;
  }
  if (!(flags&R_RELATIVE(n)))
    return;
  if (*x == 0) {
    *inf = 1;
  } else
    *x *= -1;
}

static uchar
match_signed(const int32_t matched, const reliq_range *range)
{
  struct reliq_range_node const *r;
  for (size_t i = 0; i < range->s; i++) {
    r = &range->b[i];
    int32_t x = r->v[0], y = r->v[1];
    uchar xinf=0,yinf=0;
    uchar invert = ((r->flags&R_INVERT) != 0);
    if (!(r->flags&R_RANGE)) {
      match_signed_invert(&x,&xinf,0,r->flags);
      uchar c = (matched == x && !xinf);
      if (c^invert)
        return 1;
    } else {
      match_signed_invert(&x,&xinf,0,r->flags);
      uchar c;
      match_signed_invert(&y,&yinf,1,r->flags);
      c = ((matched >= x || xinf) && (matched <= y || yinf) && (r->v[2] < 2 || (matched+r->v[3])%r->v[2] == 0));
      if (c^invert)
        return 1;
    }
  }
  return 0;
}

static uchar
match_unsigned(const uint32_t matched, const reliq_range *range)
{
  struct reliq_range_node const *r;
  for (size_t i = 0; i < range->s; i++) {
    r = &range->b[i];
    uint32_t x = r->v[0], y = r->v[1];
    uchar xinf=0,yinf=0;
    uchar invert = ((r->flags&R_INVERT) != 0);
    if (!(r->flags&R_RANGE)) {
      if (r->flags&(R_RELATIVE(0)|R_NOTSPECIFIED(0)))
        xinf = 1;
      uchar c = (matched == x && !xinf);
      if (c^invert)
        return 1;
    } else {
      if (r->flags&(R_RELATIVE(0)|R_NOTSPECIFIED(0)))
        xinf = 1;
      uchar c;
      if (r->flags&(R_RELATIVE(1)|R_NOTSPECIFIED(1)))
        yinf = 1;
      c = ((matched >= x || xinf) && (matched <= y || yinf) && (r->v[2] < 2 || (matched+r->v[3])%r->v[2] == 0));
      if (c^invert)
        return 1;
    }
  }
  return 0;
}

uchar
range_match(const uint32_t matched, const reliq_range *range, const size_t last)
{
  if (!range || !range->s)
    return 1;

  if (last == RANGE_SIGNED)
    return match_signed(matched,range);
  if (last == RANGE_UNSIGNED)
    return match_unsigned(matched,range);

  return match_relative(matched,range,last);
}

static reliq_error *
range_node_comp(const char *src, const size_t size, struct reliq_range_node *node)
{
  *node = (struct reliq_range_node){0};
  size_t pos = 0;

  for (int i = 0; i < 4; i++) {
    while_is(isspace,src,pos,size);
    if (pos < size && src[pos] == '!') {
      if (i != 0)
        return script_err("range: '!' character in the middle of fields");
      node->flags |= R_INVERT;
      pos++;
      while_is(isspace,src,pos,size);
    }
    if (i == 1)
      node->flags |= R_RANGE;
    if (pos < size && src[pos] == '-') {
      if (i > 1)
        return script_err("range: negative value specified for field that doesn't support it");
      pos++;
      while_is(isspace,src,pos,size);
      node->flags |= R_RELATIVE(i); //starts from the end
      node->flags |= R_NOTEMPTY;
    }
    if (pos < size && isdigit(src[pos])) {
      node->v[i] = number_handle(src,&pos,size);
      while_is(isspace,src,pos,size);
      node->flags |= R_NOTEMPTY;
    } else
      node->flags |= R_NOTSPECIFIED(i);

    if (pos >= size)
      break;
    if (src[pos] != ':')
      return script_err("range: bad syntax, expected ':' separator");
    pos++;
  }
  if (pos != size)
    return script_err("range: too many fields specified");
  return NULL;
}

static reliq_error *
range_comp_pre(const char *src, size_t *pos, const size_t size, flexarr *nodes) //nodes: struct reliq_range_node
{
  if (*pos >= size || src[*pos] != '[')
    return NULL;
  (*pos)++;
  size_t end;
  struct reliq_range_node node;

  while (*pos < size && src[*pos] != ']') {
    while_is(isspace,src,*pos,size);
    end = *pos;
    while (end < size && (isspace(src[end]) || isdigit(src[end]) || src[end] == ':' || src[end] == '-' || src[end] == '!') && src[end] != ',')
      end++;
    if (end >= size)
      goto END_OF_RANGE;
    if (src[end] != ',' && src[end] != ']')
      return script_err("range: char %u(0x%02x): not a number",end,src[end]);

    reliq_error *err = range_node_comp(src+(*pos),end-(*pos),&node);
    if (err)
      return err;

    if (node.flags&(R_RANGE|R_NOTEMPTY))
      *(struct reliq_range_node*)flexarr_inc(nodes) = node;
    *pos = end+((src[end] == ',') ? 1 : 0);
  }
  if (*pos >= size || src[*pos] != ']') {
    END_OF_RANGE: ;
    return script_err("range: char %lu: unprecedented end of range",*pos);
  }
  (*pos)++;

  return NULL;
}

reliq_error *
range_comp(const char *src, size_t *pos, const size_t size, reliq_range *range)
{
  if (!range)
    return NULL;
  reliq_error *err;
  flexarr *r = flexarr_init(sizeof(struct reliq_range_node),RANGES_INC);

  err = range_comp_pre(src,pos,size,r);
  if (err) {
    flexarr_free(r);
    return err;
  }

  flexarr_conv(r,(void**)&range->b,&range->s);
  /*range->max = predict_range_max(range); //new field
  if (range->max == (uint32_t)-1)
    return script_err("range: conditions impossible to fullfill")*/ //future warning
  return NULL;
}

void
range_free(reliq_range *range)
{
  if (!range)
    return;
  if (range->s)
    free(range->b);
}

uint32_t
predict_range_node_max(const struct reliq_range_node *node)
{
  //returns 0 on relative, -1 on conflicted values
  uint8_t flags = node->flags;
  if (flags&R_INVERT)
    return 0; //in most cases its relative

  if (!(flags&R_RANGE)) {
    if (flags&(R_RELATIVE(0)|R_NOTSPECIFIED(0)))
      return 0;
    return node->v[0]+1;
  }

  if (flags&(R_RELATIVE(0)|R_RELATIVE(1)|R_NOTSPECIFIED(0)|R_NOTSPECIFIED(1)))
    return 0;

  if (node->v[0] > node->v[1])
    return -1;

  uint32_t max = node->v[1]+node->v[3];

  if (max < node->v[2])
    return -1;

  if (node->v[2] < 2)
    return node->v[1]+1;

  max -= max%node->v[2];

  if (max < node->v[3])
    return -1;

  return max+1-node->v[3];
}

uint32_t
predict_range_max(const reliq_range *range)
{
  const size_t size = range->s;
  struct reliq_range_node *nodes = range->b;
  uint32_t max = 0;

  for (size_t i = 0; i < size; i++) {
    uint32_t x = predict_range_node_max(&nodes[i]);
    if (x == 0)
      return 0;
    if (max == (uint32_t)-1 || x > max)
      max = x;
  }

  return max;
}
