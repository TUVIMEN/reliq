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

#define ATTRIB_INC (1<<4)
#define RELIQ_NODES_INC (1<<13)

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

int
reliq_free(reliq *rq)
{
  if (rq == NULL)
    return -1;
  const size_t size = rq->nodesl;
  for (size_t i = 0; i < size; i++)
    free(rq->nodes[i].attribs);
  if (rq->nodesl)
    free(rq->nodes);
  if (rq->freedata)
      return (*rq->freedata)((void*)rq->data,rq->datal);
  return 0;
}

static reliq_error *
reliq_analyze(const char *data, const size_t size, flexarr *nodes, reliq *rq) //nodes: reliq_hnode
{
  reliq_error *err;
  for (size_t i = 0; i < size; i++) {
    while (i < size && data[i] != '<')
        i++;
    while (i < size && data[i] == '<') {
      html_struct_handle(data,&i,size,0,nodes,rq,&err);
      if (err)
        return err;
    }
  }
  return NULL;
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
  t.expr = nodep;
  t.nodef = nodef;
  t.nodefl = nodefl;
  t.flags = 0;
  if (output == NULL)
    output = sink_from_file(stdout);
  t.output = output;
  t.nodes = NULL;
  t.nodesl = 0;
  t.parent = NULL;

  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);

  reliq_error *err = reliq_analyze(data,size,nodes,&t);

  flexarr_free(nodes);
  flexarr_free((flexarr*)t.attrib_buffer);
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

reliq
reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl)
{
  reliq t;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;
  t.parent = NULL;

  char *ptr;
  size_t pos=0,size;
  uint16_t lvl;
  SINK *out = sink_open(&ptr,&size);
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
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
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;
  t.freedata = NULL;
  t.data = rq->data;
  t.datal = rq->datal;
  t.parent = NULL;

  uint16_t lvl;
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
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
  rq->expr = NULL;
  rq->flags = RELIQ_SAVE;
  rq->output = NULL;
  rq->nodef = NULL;
  rq->nodefl = 0;
  rq->parent = NULL;

  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  rq->attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);

  reliq_error *err = reliq_analyze(data,size,nodes,rq);

  flexarr_conv(nodes,(void**)&rq->nodes,&rq->nodesl);
  flexarr_free((flexarr*)rq->attrib_buffer);
  if (err)
    reliq_free(rq);
  return err;
}
