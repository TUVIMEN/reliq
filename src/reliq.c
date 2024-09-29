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
#include <stdarg.h>

#include "flexarr.h"
#include "sink.h"
#include "utils.h"
#include "html.h"
#include "npattern.h"

#define FROM_COMPRESSED_NODES_INC (1<<10)

reliq_error *
reliq_set_error(const int code, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  reliq_error *err = malloc(sizeof(reliq_error));
  vsnprintf(err->msg,RELIQ_ERROR_MESSAGE_LENGTH,fmt,ap);
  err->code = code;
  va_end(ap);
  return err;
}

void
reliq_free_hnodes(reliq_hnode *nodes, const size_t nodesl)
{
  for (size_t i = 0; i < nodesl; i++)
    free(nodes[i].attribs);
  if (nodesl)
    free(nodes);
}

int
reliq_free(reliq *rq)
{
  if (rq == NULL)
    return -1;

  reliq_free_hnodes(rq->nodes,rq->nodesl);

  if (rq->freedata)
      return (*rq->freedata)((void*)rq->data,rq->datal);
  return 0;
}

reliq_error *
reliq_fmatch(const char *data, const size_t size, SINK *output, const reliq_npattern *nodep,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl)
{
  reliq t;
  t.data = data;
  t.datal = size;
  t.nodes = NULL;
  t.nodesl = 0;

  SINK *out = output;
  if (output == NULL)
    out = sink_from_file(stdout);

  struct html_process_expr expr = {
    .rq = &t,
    .output = out,
    .expr = nodep,
    .nodef = nodef,
    .nodefl = nodefl
  };
  reliq_error *err = html_handle(data,size,NULL,NULL,&expr);

  if (output == NULL)
    sink_close(out);

  return err;
}

static void
reliq_hnode_shift(reliq_hnode *node, const size_t pos)
{
  char const *ref = node->all.b;
  #define shift_cstr(x) x.b = (char const*)(x.b-ref)+pos

  shift_cstr(node->all);
  shift_cstr(node->tag);
  shift_cstr(node->insides);
  const size_t size = node->attribsl;
  for (size_t i = 0; i < size; i++) {
    shift_cstr(node->attribs[i].f);
    shift_cstr(node->attribs[i].s);
  }
}

static void
reliq_hnode_shift_finalize(reliq_hnode *node, char *ref)
{
  #define shift_cstr_p(x) x.b = ref+(size_t)x.b

  shift_cstr_p(node->all);
  shift_cstr_p(node->tag);
  shift_cstr_p(node->insides);
  const size_t size = node->attribsl;
  for (size_t i = 0; i < size; i++) {
    shift_cstr_p(node->attribs[i].f);
    shift_cstr_p(node->attribs[i].s);
  }
}

/*
  reliq_from_compressed functions can be optimized by eliminating repeating or descendant tags.

  static int
  compressed_cmp(const reliq_compressed *c1, const reliq_compressed *c2)
  {
    return c1->hnode < c2->hnode;
  }

  reliq_compressed *ncomp = memdup(compressed,compressedl*sizeof(reliq_compressed));
  qsort(ncomp,compressedl,sizeof(reliq_compressed),(int(*)(const void*,const void*)compressed_cmp));

  reliq_hnode *current,*previous=NULL;
  for (size_t i = 0; i < compressedl; i++) {
    current = ncomp[i].hnode;
    if ((void)current < (void*)10)
      continue;

    uchar passed = 0;

    if (!previous) {
      previous = current;
      continue;
    } else if (current <= previous) {
      if (previous <= current+current->desc_count) {
        previous = current;
        continue;
      }
    }

    ...

    if (current > previous+previous->desc_count)
      previous = current;
  }
  if (previous)
    ...

  This however will not allow for changing levels to be relative to parent.
*/

reliq
reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl)
{
  reliq t;

  char *ptr;
  size_t pos=0,size;
  uint16_t lvl;
  SINK *out = sink_open(&ptr,&size);
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),FROM_COMPRESSED_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    current = compressed[i].hnode;
    if ((void*)current < (void*)10)
      continue;

    lvl = current->lvl;

    const size_t desc_count = current->desc_count;
    for (size_t j = 0; j <= desc_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,current+j,sizeof(reliq_hnode));

      new->attribs = NULL;
      if ((current+j)->attribsl)
        new->attribs = memdup((current+j)->attribs,sizeof(reliq_cstr_pair)*(current+j)->attribsl);

      size_t tpos = pos+(new->all.b-current->all.b);

      reliq_hnode_shift(new,tpos);
      new->lvl -= lvl;
    }

    sink_write(out,current->all.b,current->all.s);
    pos += current->all.s;
  }

  sink_close(out);

  const size_t nodessize = nodes->size;
  for (size_t i = 0; i < nodessize; i++)
    reliq_hnode_shift_finalize(&((reliq_hnode*)nodes->v)[i],ptr);

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  t.freedata = reliq_std_free;
  t.data = ptr;
  t.datal = size;
  return t;
}

reliq
reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq)
{
  reliq t;
  t.freedata = NULL;
  t.data = rq->data;
  t.datal = rq->datal;

  uint16_t lvl;
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),FROM_COMPRESSED_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    current = compressed[i].hnode;
    if ((void*)current < (void*)10)
      continue;

    lvl = current->lvl;

    const size_t desc_count = current->desc_count;
    for (size_t j = 0; j <= desc_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,current+j,sizeof(reliq_hnode));

      new->attribs = NULL;
      if ((current+j)->attribsl)
        new->attribs = memdup((current+j)->attribs,sizeof(reliq_cstr_pair)*(current+j)->attribsl);

      new->lvl -= lvl;
    }
  }

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  return t;
}

int
reliq_std_free(void *addr, size_t len)
{
    free(addr);
    return 0;
}

reliq_error *
reliq_init(const char *data, const size_t size, int (*freedata)(void *addr, size_t len), reliq *rq)
{
  rq->data = data;
  rq->datal = size;
  rq->freedata = freedata;

  reliq_error *err = html_handle(data,size,&rq->nodes,&rq->nodesl,NULL);

  if (err)
    reliq_free(rq);
  return err;
}
