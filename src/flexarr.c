#include <stdlib.h>
#include <string.h>
#include "flexarr.h"

flexarr *
flexarr_init(const size_t nmemb, const size_t inc_r)
{
  flexarr *ret = calloc(sizeof(flexarr),1);
  ret->inc_r = inc_r;
  ret->nmemb = nmemb;
  return ret;
}

void *
flexarr_inc(flexarr *f)
{
  if (f->size == f->asize) {
    void *v = realloc(f->v,(f->asize+=f->inc_r)*f->nmemb);
    if (v == NULL)
      return NULL;
    f->v = v;
  }
  return f->v+(f->size++*f->nmemb);
}

void *
flexarr_dec(flexarr *f)
{
  if (f->size == 0)
    return NULL;
  return f->v+(f->size--*f->nmemb);
}

void *
flexarr_set(flexarr *f, const size_t s)
{
  if (f->size >= s || f->asize >= s)
    return NULL;
  void *v = realloc(f->v,s*f->nmemb);
  if (v == NULL)
    return NULL;
  return f->v = v;
}

void *
flexarr_clearb(flexarr *f)
{
  void *v = realloc(f->v,f->size*f->nmemb);
  if (v == NULL)
    return NULL;
  return f->v = v;
}

void
flexarr_free(flexarr *f)
{
  free(f->v);
  f->v = NULL;
  f->size = 0;
  f->asize = 0;
  free(f);
}
