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

#include <stdlib.h>
#include <string.h>

#include "builtin.h"
#include "flexarr.h"

flexarr * ATTR_MALLOC
flexarr_init(const size_t elsize, const size_t inc_r)
{
  flexarr *ret = calloc(1,sizeof(flexarr));
  ret->inc_r = inc_r;
  ret->elsize = elsize;
  return ret;
}

static inline void * ATTR_MALLOC
flexarr_realloc(void *ptr, size_t size)
{
  if (unlikely(!size)) {
    if (ptr)
      free(ptr);
    return NULL;
  }
  return realloc(ptr,size);
}

void *
flexarr_inc(flexarr *f)
{
  if (unlikely(f->size >= f->asize)) {
    f->v = flexarr_realloc(f->v,(f->asize+=f->inc_r)*f->elsize);
    if (unlikely(!f->v))
      return NULL;
  }
  return ((char*)f->v)+(f->size++*f->elsize);
}

void *
flexarr_append(flexarr *f, const void *v, const size_t count)
{
  if (unlikely(!count))
    return f->v;

  size_t free_space = f->asize-f->size;
  if (unlikely(free_space < count)) {
    size_t needed=(count-free_space),n=needed/f->inc_r;
    if (likely(needed%f->inc_r))
      n++;
    f->asize += n*f->inc_r;
    f->v = flexarr_realloc(f->v,f->asize*f->elsize);
    if (unlikely(f->v == NULL))
      return NULL;
  }
  void *ret = memcpy(((char*)f->v)+f->size,v,count);
  f->size += count;
  return ret;
}

void *
flexarr_dec(flexarr *f)
{
  if (unlikely(f->size == 0))
    return NULL;
  return ((char*)f->v)+(f->size--*f->elsize);
}

void *
flexarr_set(flexarr *f, const size_t s) //set number of allocated elements to s
{
  if (f->size >= s || f->asize >= s)
    return NULL;
  f->v = flexarr_realloc(f->v,s*f->elsize);
  f->asize = s;
  return f->v;
}

void *
flexarr_alloc(flexarr *f, const size_t s) //allocate additional s amount of elements
{
  if (unlikely(s == 0) || f->asize-f->size >= s)
    return f->v;
  f->v = flexarr_realloc(f->v,(f->size+s)*f->elsize);
  f->asize = f->size+s;
  return f->v;
}

void *
flexarr_add(flexarr *dst, const flexarr *src) //append contents of src to dst
{
  if (dst->size+src->size > dst->asize &&
    unlikely(flexarr_alloc(dst,src->size) == NULL))
    return NULL;
  memcpy(((char*)dst->v)+(dst->size*dst->elsize),src->v,src->size*dst->elsize);
  dst->size += src->size;
  return dst->v;
}

void *
flexarr_clearb(flexarr *f) //clear buffer
{
  if (unlikely(f->size == f->asize || !f->v))
    return NULL;
  f->asize = f->size;
  return f->v = flexarr_realloc(f->v,f->size*f->elsize);
}

void
flexarr_conv(flexarr *f, void **v, size_t *s) //convert from flexarr to normal array
{
  flexarr_clearb(f);
  *s = f->size;
  *v = f->v;
  free(f);
}

void
flexarr_free(flexarr *f)
{
  if (likely(f->asize))
    free(f->v);
  f->v = NULL;
  f->size = 0;
  f->asize = 0;
  free(f);
}
