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
#include <stdarg.h>

#include "flexarr.h"
#include "sink.h"
#include "utils.h"
#include "html.h"
#include "hnode.h"
#include "npattern.h"

#define FROM_COMPRESSED_NODES_INC (1<<10)
#define FROM_COMPRESSED_ATTRIBS_INC (1<<10)

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

  if (rq->nodesl)
    free(rq->nodes);

  if (rq->attribsl)
    free(rq->attribs);

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
  reliq t = {
    .data = data,
    .datal = size
  };

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
  reliq_error *err = html_handle(data,size,NULL,NULL,NULL,NULL,&expr);

  if (output == NULL)
    sink_close(out);

  return err;
}

static void
reliq_hnode_shift(reliq_cattrib *attribs, reliq_chnode *node, const size_t pos, const uint32_t attribsl)
{
  /*char const *ref = node->all.b;
  #define shift_cstr(x) (x).b = (char const*)((x).b-ref)+pos

  shift_cstr(node->all);
  shift_cstr(node->tag);
  shift_cstr(node->insides);*/
  /*reliq_cattrib *a = attribs+node->attribs;
  for (size_t i = 0; i < attribsl; i++) {
    shift_cstr(a[i].key);
    shift_cstr(a[i].value);
  }*/
}

static void
reliq_hnode_shift_finalize(reliq_cattrib *attribs, reliq_chnode *node, char *ref, const uint32_t attribsl)
{
  /*#define shift_cstr_p(x) (x).b = ref+(size_t)(x).b

  shift_cstr_p(node->all);
  shift_cstr_p(node->tag);
  shift_cstr_p(node->insides);*/
  /*reliq_cattrib *a = attribs+node->attribs;
  for (size_t i = 0; i < attribsl; i++) {
    shift_cstr_p(a[i].key);
    shift_cstr_p(a[i].value);
  }*/
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

  reliq_chnode *current,*previous=NULL;
  for (size_t i = 0; i < compressedl; i++) {
    current = ncomp[i].hnode;
    if ((void)current < (void*)10)
      continue;

    uchar passed = 0;

    if (!previous) {
      previous = current;
      continue;
    } else if (current <= previous) {
      if (previous <= current+current->tag_count) {
        previous = current;
        continue;
      }
    }

    ...

    if (current > previous+previous->tag_count)
      previous = current;
  }
  if (previous)
    ...

  This however will not allow for changing levels to be relative to parent.
*/

void
convert_from_compressed_add_descendants(const reliq *rq, const reliq_chnode *root, flexarr *nodes, flexarr *attribs, const size_t pos, const uchar independent)
{
  const size_t desccount = root->tag_count+root->text_count+root->comment_count;
  const uint16_t lvl = root->lvl;
  reliq_chnode *new;
  for (size_t j = 0; j <= desccount; j++) {
    new = (reliq_chnode*)flexarr_inc(nodes);
    const reliq_chnode *hnode = root+j;
    memcpy(new,hnode,sizeof(reliq_chnode));

    uint32_t attribsl = chnode_attribsl(rq,hnode);
    if (attribsl)
      flexarr_append(attribs,rq->attribs+hnode->attribs,attribsl);

    if (independent) {
      size_t tpos = pos+(hnode->all-root->all);
      reliq_hnode_shift(attribs->v,new,tpos,attribsl);
    }
    new->lvl -= lvl;
  }
}

static reliq
convert_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq, const uchar independent)
{
  reliq ret;

  char *ptr;
  size_t pos=0,size;
  SINK *out = NULL;
  if (independent)
    out = sink_open(&ptr,&size);
  flexarr *nodes = flexarr_init(sizeof(reliq_chnode),FROM_COMPRESSED_NODES_INC);
  flexarr *attribs = flexarr_init(sizeof(reliq_cattrib),FROM_COMPRESSED_ATTRIBS_INC);
  reliq_chnode *hnodes = rq->nodes;

  for (size_t i = 0; i < compressedl; i++) {
    uint32_t current = compressed[i].hnode;
    if (OUTFIELDCODE(current))
      continue;
    const reliq_chnode *hn = hnodes+current;

    convert_from_compressed_add_descendants(rq,hn,nodes,attribs,pos,independent);

    if (independent) {
      sink_write(out,rq->data+hn->all,hn->all_len);
      pos += hn->all_len;
    }
  }

  flexarr_conv(nodes,(void**)&ret.nodes,&ret.nodesl);
  flexarr_conv(attribs,(void**)&ret.attribs,&ret.attribsl);

  if (independent) {
    ret.freedata = reliq_std_free;
    sink_close(out);
    ret.data = ptr;
    ret.datal = size;

    const size_t nodessize = ret.nodesl;
    for (size_t i = 0; i < nodessize; i++) {
      reliq_chnode *hn = ret.nodes+i;
      reliq_hnode_shift_finalize(ret.attribs,hn,ptr,chnode_attribsl(&ret,hn));
    }
  } else {
    ret.freedata = NULL;
    ret.data = rq->data;
    ret.datal = rq->datal;
  }
  return ret;
}

reliq
reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq)
{
  return convert_from_compressed(compressed,compressedl,rq,1);
}

reliq
reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq)
{
  return convert_from_compressed(compressed,compressedl,rq,0);
}

int
reliq_std_free(void *addr, size_t UNUSED len)
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

  reliq_error *err = html_handle(data,size,&rq->nodes,&rq->nodesl,&rq->attribs,&rq->attribsl,NULL);

  if (err)
    reliq_free(rq);
  return err;
}
