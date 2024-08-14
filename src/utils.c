/*
    reliq - html searching tool
    Copyright (C) 2020-2024 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#define _GNU_SOURCE
#define __USE_XOPEN
#define __USE_XOPEN_EXTENDED
#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <string.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"

#define RANGES_INC (1<<4)

void
strrev(char *v, size_t size)
{
  if (!v || !size)
    return;
  for (size_t i = 0, j = size-1; i < j; i++, j--) {
    char t = v[i];
    v[i] = v[j];
    v[j] = t;
  }
}

void
uint_to_str(char *dest, size_t *destl, const size_t max_destl, unsigned long num)
{
  *destl = 0;
  if (!max_destl)
    return;
  size_t p = 0;
  while (p < max_destl && num) {
    dest[p++] = (num%10)+'0';
    num /= 10;
  }
  *destl = p;
  if (p) {
    strrev(dest,p);
  } else {
    dest[0] = '0';
    *destl = 1;
  }
}

void
memtrim(void const **dest, size_t *destsize, const void *src, const size_t size)
{
  *destsize = 0;
  if (!src || !size)
    return;

  size_t start=0,end=size;
  while (start < end && isspace(((char*)src)[start]))
    start++;

  while (end-1 > start && isspace(((char*)src)[end-1]))
    end--;

  *dest = (void*)src+start;
  *(size_t*)destsize = end-start;
}

static size_t
memwordtok_r_get_word(const char *ptr, const size_t plen, char const **word, size_t *wordlen)
{
  *word = NULL;
  *wordlen = 0;
  size_t p = 0;
  if (plen == 0)
    return p;
  while_is(isspace,ptr,p,plen);
  if (p >= plen)
    return p;
  *word = ptr+p;
  while_isnt(isspace,ptr,p,plen);
  *wordlen = p-(*word-ptr);
  return p;
}

void
memwordtok_r(const void *ptr, const size_t plen, void const **saveptr, size_t *saveptrlen, void const **word, size_t *wordlen)
{
  *word = NULL;
  *wordlen = 0;

  if (ptr) {
    size_t size = memwordtok_r_get_word((char*)ptr,plen,(char const**)word,wordlen);
    if (*wordlen) {
      *saveptr = ptr+size;
      *saveptrlen = plen-size;
    }
    return;
  }

  if (!*saveptr)
    return;

  size_t size = memwordtok_r_get_word((char*)*saveptr,*saveptrlen,(char const**)word,wordlen);
  if (*wordlen) {
    *saveptr += size;
    *saveptrlen -= size;
  }
  return;
}

void *
memdup(void const *src, size_t size)
{
  return memcpy(malloc(size),src,size);
}

int
memcasecmp(const void *v1, const void *v2, const size_t n)
{
  const char *s1 = v1,
    *s2 = v2;
  for (size_t i = 0; i < n; i++) {
    char u1 = toupper(s1[i]);
    char u2 = toupper(s2[i]);
    char diff = u1-u2;
    if (diff)
      return diff;
  }
  return 0;
}

void const*
memcasemem(void const *haystack, size_t const haystackl, const void *needle, const size_t needlel)
{
  if (haystackl < needlel || !haystackl || !needlel)
    return NULL;
  const char *haystack_s = haystack,
    *needle_s = needle;
  for (size_t i=0,prev; i < haystackl; i++) {
    prev = i;
    for (size_t j=0; i < haystackl && j < needlel && toupper(needle_s[j]) == toupper(haystack_s[i]); i++, j++)
      if (j == needlel-1)
        return haystack+(i-j);
    i = prev;
  }
  return NULL;
}

char
special_character(const char c)
{
  char r;
  switch (c) {
    case '0': r = '\0'; break;
    case 'a': r = '\a'; break;
    case 'b': r = '\b'; break;
    case 't': r = '\t'; break;
    case 'n': r = '\n'; break;
    case 'v': r = '\v'; break;
    case 'f': r = '\f'; break;
    case 'r': r = '\r'; break;
    default: r = c;
  }
  return r;
}

char *
delchar(char *src, const size_t pos, size_t *size)
{
  size_t s = *size-1;
  for (size_t i = pos; i < s; i++)
    src[i] = src[i+1];
  src[s] = 0;
  *size = s;
  return src;
}

unsigned int
get_dec(const char *src, size_t size, size_t *traversed)
{
    size_t pos=0;
    unsigned int r = 0;
    while (pos < size && isdigit(src[pos]))
        r = (r*10)+(src[pos++]-48);
    *traversed = pos;
    return r;
}

unsigned int
number_handle(const char *src, size_t *pos, const size_t size)
{
  size_t s;
  int ret = get_dec(src+*pos,size-*pos,&s);
  if (s == 0)
    return -1;
  *pos += s;
  return ret;
}

reliq_error *
get_quoted(char *src, size_t *i, size_t *size, const char delim, size_t *start, size_t *len)
{
  *len = 0;
  if (src[*i] == '"' || src[*i] == '\'') {
    char tf = src[*i];
    *start = ++(*i);
    while (*i < *size && src[*i] != tf) {
      if (src[*i] == '\\' && src[*i+1] == '\\') {
        (*i)++;
      } else if (src[*i] == '\\' && src[*i+1] == tf)
        delchar(src,*i,size);
      (*i)++;
    }
    if (src[*i] != tf)
      return script_err("string: could not find the end of %c quote",tf);
    *len = ((*i)++)-(*start);
  } else {
    *start = *i;
    while (*i < *size && !isspace(src[*i]) && src[*i] != delim) {
      if (src[*i] == '\\' && src[*i+1] == '\\') {
        (*i)++;
      } else if (src[*i] == '\\' && (isspace(src[*i+1]) || src[*i+1] == delim)) {
        delchar(src,*i,size);
      } else if (src[*i] == '"' || src[*i] == '\'')
        return script_err("string: illegal use of %c inside unquoted string",src[*i]);
      (*i)++;
    }
    *len = *i-*start;
  }
  return NULL;
}

void
conv_special_characters(char *src, size_t *size)
{
  for (size_t i = 0; i < (*size)-1; i++) {
    if (src[i] != '\\')
      continue;
    char c = special_character(src[i+1]);
    if (c != src[i+1]) {
      delchar(src,i,size);
      src[i] = c;
    }
  }
}

uchar
range_match(const uint matched, const reliq_range *range, const size_t last)
{
  if (!range || !range->s)
    return 1;
  struct reliq_range_node const *r;
  uint x,y;
  for (size_t i = 0; i < range->s; i++) {
    r = &range->b[i];
    x = r->v[0];
    y = r->v[1];
    if (!(r->flags&R_RANGE)) {
      if (r->flags&R_RELATIVE(0))
        x = ((uint)last < r->v[0]) ? 0 : last-r->v[0];
      if (matched == x)
        return r->flags&R_INVERT ? 0 : 1;
    } else {
      if (r->flags&R_RELATIVE(0))
        x = ((uint)last < r->v[0]) ? 0 : last-r->v[0];
      if (r->flags&R_RELATIVE(1)) {
        if ((uint)last < r->v[1])
          continue;
        y = last-r->v[1];
      }
      if (matched >= x && matched <= y)
        if (r->v[2] < 2 || (matched+r->v[3])%r->v[2] == 0)
          return r->flags&R_INVERT ? 0 : 1;
    }
  }
  return r->flags&R_INVERT ? 1 : 0;
}

static reliq_error *
range_node_comp(const char *src, const size_t size, struct reliq_range_node *node)
{
  memset(node->v,0,sizeof(struct reliq_range_node));
  size_t pos = 0;

  for (int i = 0; i < 4; i++) {
    while_is(isspace,src,pos,size);
    if (pos < size && src[pos] == '!') {
      if (i != 0)
        return script_err("range: '!' character in the middle of fields");
      node->flags |= R_INVERT; //invert
      pos++;
      while_is(isspace,src,pos,size);
    }
    if (i == 1)
      node->flags |= R_RANGE; //is a range
    if (pos < size && src[pos] == '-') {
      if (i > 1)
        return script_err("range: negative value specified for field that doesn't support it");
      pos++;
      while_is(isspace,src,pos,size);
      node->flags |= R_RELATIVE(i); //starts from the end
      node->flags |= R_NOTEMPTY; //not empty
    }
    if (pos < size && isdigit(src[pos])) {
      node->v[i] = number_handle(src,&pos,size);
      while_is(isspace,src,pos,size);
      node->flags |= R_NOTEMPTY; //not empty
    } else if (i == 1)
      node->flags |= R_RELATIVE(i);

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
range_comp_pre(const char *src, size_t *pos, const size_t size, flexarr *nodes)
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
      memcpy(flexarr_inc(nodes),&node,sizeof(struct reliq_range_node));
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
  if (range->max == (uint)-1)
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

uint
predict_range_node_max(const struct reliq_range_node *node)
{
  //returns 0 on relative, -1 on conflicted values
  uchar flags = node->flags;
  if (flags&R_INVERT)
    return 0;

  if (!(flags&R_RANGE)) {
    if (flags&R_RELATIVE(0))
      return 0;
    return node->v[0]+1;
  }

  if (flags&R_RELATIVE(0) || flags&R_RELATIVE(1))
    return 0;

  if (node->v[0] > node->v[1])
    return -1;

  uint max = node->v[1]+node->v[3];

  if (max < node->v[2])
    return -1;

  if (node->v[2] < 2)
    return node->v[1]+1;

  max -= max%node->v[2];

  if (max < node->v[3])
    return -1;

  return max+1-node->v[3];
}

uint
predict_range_max(const reliq_range *range)
{
  size_t size = range->s;
  struct reliq_range_node *nodes = range->b;
  uint max = 0;

  for (size_t i = 0; i < size; i++) {
    uint x = predict_range_node_max(&nodes[i]);
    if (x == 0)
      return 0;
    if (max == (uint)-1 || x > max)
      max = x;
  }

  return max;
}
