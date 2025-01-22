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

#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "output.h"
#include "npattern.h"
#include "html.h"

#define ATTRIB_INC (1<<13)
#define NODES_INC (1<<13)

const reliq_str8 selfclosing_s[] = { //tags that don't end with </tag>
  {"br",2},{"img",3},{"input",5},{"link",4},{"meta",4},{"hr",2},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const reliq_str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef RELIQ_AUTOCLOSING
const reliq_str8 *autoclosing_s[] = { //tags that don't need to be closed
  (const reliq_str8[]){{"p",1},{"p",1},{"div",3},{"ul",2},{"h1",2},{"h2",2},{"h3",2},{"h4",2},{"h5",2},{"h6",2},{"dl",2},{"dd",2},{"dt",2},{"header",6},{"article",7},{"aside",5},{"footer",6},{"hr",2},{"main",4},{"menu",4},{"nav",3},{"ol",2},{"pre",3},{"section",7},{"table",5},{"form",4},{"blockquote",10},{"details",7},{"address",7},{"fieldset",8},{"figcaption",10},{"caption",7},{"figure",6},{"hgroup",6},{"search",6},{NULL,0}},
  (const reliq_str8[]){{"li",2},{"li",2},{NULL,0}},
  (const reliq_str8[]){{"tr",2},{"tr",2},{NULL,0}},
  (const reliq_str8[]){{"td",2},{"td",2},{"th",2},{NULL,0}},
  (const reliq_str8[]){{"th",2},{"th",2},{"td",2},{NULL,0}},
  (const reliq_str8[]){{"dt",2},{"dt",2},{"dd",2},{NULL,0}},
  (const reliq_str8[]){{"dd",2},{"dd",2},{"dt",2},{NULL,0}},
  (const reliq_str8[]){{"table",5},{"table",5},{NULL,0}},
  (const reliq_str8[]){{"thead",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const reliq_str8[]){{"tbody",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const reliq_str8[]){{"tfoot",5},{"thead",5},{"tbody",5},{NULL,0}},
  (const reliq_str8[]){{"rt",2},{"rt",2},{"rp",2},{NULL,0}},
  (const reliq_str8[]){{"rp",2},{"rp",2},{"rt",2},{NULL,0}},
  (const reliq_str8[]){{"optgroup",8},{"optgroup",8},{"hr",2},{NULL,0}},
  (const reliq_str8[]){{"option",6},{"option",6},{"optgroup",8},{"tr",2},{NULL,0}},
  (const reliq_str8[]){{"colgroup",8},{"colgroup",8},{NULL,0}},
};

/* tags from which no closing tag can escape
   e.g. <div><table></div></table></div> is valid because of it. */
const reliq_str8 inescapable_s[] = {
    {"table",5}
};
#endif

typedef struct {
    struct html_process_expr *expr;
    flexarr *nodes; //reliq_hnode
    flexarr *attribs; //reliq_cstr_pair
    reliq_error *err;
    const char *f;
    const size_t s;
} html_state;

static void
comment_handle(const char *f, size_t *pos, const size_t s)
{
  size_t i = *pos;
  i++;

  size_t diff = 0;

  if (f[i] == '-' && f[i+1] == '-') {
    i += 2;
    diff = 2;
    while (s-i > diff && (f[i] != '-' || f[i+1] != '-' || f[i+2] != '>'))
      i++;
  } else {
    while (i < s && f[i] != '>')
      i++;
  }
  if (s-i > diff) {
      i += diff+1;
  } else {
      i = s-1;
  }
  *pos = i;
}

static inline void
tagname_handle(const char *f, size_t *pos, const size_t s, reliq_cstr *tag)
{
  size_t i = *pos;
  tag->b = f+i;
  if (i < s && isalpha(f[i])) {
    i++;
    while (i < s && !isspace(f[i]) && f[i] != '>' && f[i] != '/')
      i++;
  }
  tag->s = (f+i)-tag->b;
  *pos = i;
}

static inline void
attribname_handle(const char *f, size_t *pos, const size_t s, reliq_cstr *tag)
{
  size_t i = *pos;
  tag->b = f+i;
  while (i < s && f[i] != '=' && f[i] != '>' && f[i] != '/' && !isspace(f[i]))
    i++;
  tag->s = (f+i)-tag->b;
  *pos = i;
}

static void
attrib_value_handle(const char *f, size_t *pos, const size_t s, reliq_cstr *value)
{
  size_t i = *pos;
  i++;
  while_is(isspace,f,i,s);

  if (likely(f[i] == '\'' || f[i] == '"')) {
    char delim = f[i++];
    value->b = f+i;
    char *ending = memchr(f+i,delim,s-i);
    if (unlikely(!ending)) {
      i = s;
      goto END;
    }
    i = ending-f;
    value->s = (f+i)-value->b;
    if (likely(f[i] == delim))
      i++;
  } else {
    value->b = f+i;
    while (i < s && !isspace(f[i]) && f[i] != '>')
      i++;
     value->s = (f+i)-value->b;
  }

  END: ;
  *pos = i;
}

static void
attrib_handle(const char *f, size_t *pos, const size_t s, flexarr *attribs) //attribs: reliq_cstr_pair
{
  size_t i = *pos;
  reliq_cstr_pair *ac = (reliq_cstr_pair*)flexarr_inc(attribs);
  attribname_handle(f,&i,s,&ac->f);
  while_is(isspace,f,i,s);
  ac->s.b = NULL;
  ac->s.s = 0;
  if (unlikely(f[i] == '='))
    attrib_value_handle(f,&i,s,&ac->s);
  *pos = i;
}

#ifdef RELIQ_PHPTAGS
static void
phptag_handle(const char *f, size_t *pos, const size_t s, reliq_hnode *hnode, flexarr *nodes) //nodes: reliq_hnode
{
  size_t i = *pos;
  i++;
  while_is(isspace,f,i,s);
  tagname_handle(f,&i,s,&hnode->tag);
  if (!hnode->tag.s) {
    flexarr_dec(nodes);
    return;
  }

  hnode->insides.b = f+i;
  hnode->insides.s = 0;

  char *ending;
  for (; i < s; i++) {
    if (unlikely(f[i] == '\\')) {
      i += 2;
      continue;
    }
    if (unlikely(f[i] == '?' && f[i+1] == '>')) {
      hnode->insides.s = i-1-(hnode->insides.b-f);
      i++;
      break;
    }
    if (unlikely(f[i] == '"')) {
      i++;
      size_t n,jumpv;
      while (1) {
        ending = memchr(f+i,'"',s-i);
        if (!ending) {
          i = s;
          goto END;
        }
        jumpv = ending-f-i;
        if (!jumpv) {
          i++;
          break;
        }
        i = ending-f;
        n = 1;
        while (jumpv && f[i-n] == '\\')
          n++;
        if ((n-1)&1)
          continue;
        break;
      }
    } else if (unlikely(f[i] == '\'')) {
      i++;
      ending = memchr(f+i,'\'',s-i);
      if (likely(ending)) {
        i = ending-f;
      } else {
        i = s;
        goto END;
      }
    }
  }
  hnode->all.s = (f+i)-hnode->all.b+1;
  END: ;
  *pos = i;
}
#endif

#ifdef RELIQ_AUTOCLOSING
static uchar
isinescapable(reliq_cstr str)
{
  for (size_t g = 0; g < LENGTH(inescapable_s); g++)
    if (strcasecomp(str,inescapable_s[g]))
      return 1;
  return 0;
}
#endif

static uint64_t
html_struct_handle(size_t *pos, const uint16_t lvl, html_state *st);

struct tag_info {
    #ifdef RELIQ_AUTOCLOSING
    uchar autoclosing;
    #endif
    uchar foundend : 1;
    uchar script : 1;
};

static uchar
tag_insides_handle(size_t *pos, const size_t hnindex, uint64_t *ret, struct tag_info *taginfo, html_state *st)
{
  const char *f = st->f;
  const size_t s = st->s;
  flexarr *nodes = st->nodes; //reliq_hnode
  size_t i = *pos;
  reliq_hnode *hnode = ((reliq_hnode*)nodes->v)+hnindex;
  uint16_t lvl = hnode->lvl;
  size_t tagend;

  for (; i < s; i++) {
    if (f[i] != '<')
      continue;


    hnode = ((reliq_hnode*)nodes->v)+hnindex;
    FINAL_TAG_END: ;
    tagend=i;
    i++;
    while_is(isspace,f,i,s);
    if (unlikely(f[i] == '/')) {
      i++;
      while_is(isspace,f,i,s);

      reliq_cstr endname;
      tagname_handle(f,&i,s,&endname);
      if (unlikely(!endname.s)) {
        i++;
        continue;
      }

      if (strcasecomp(hnode->tag,endname)) {
        hnode->insides.s = tagend-hnode->insides.s;

        while (i < s && f[i] != '<' && f[i] != '>')
            i++;
        if (unlikely(i >= s)) {
          i = s;
          *ret = 0;
          hnode->all.s = f+s-hnode->all.b;
          goto ERR;
        }
        if (unlikely(f[i] == '<'))
          i--;

        hnode->all.s = f+i+1-hnode->all.b;
        goto END;
      }

      reliq_hnode *nodesv = (reliq_hnode*)nodes->v;

      if (unlikely(!hnindex || hnode->lvl == 0)) {
        taginfo->foundend = 0;
        continue;
      }

      #ifdef RELIQ_AUTOCLOSING
      if (isinescapable(hnode->tag))
        continue;
      #endif

      for (size_t j = hnindex-1;; j--) {
        if (nodesv[j].all.s || nodesv[j].lvl >= lvl) {
          if (!j)
            break;
          continue;
        }
        reliq_cstr tag = nodesv[j].tag;
        if (strcasecomp(tag,endname)) {
          i = tagend;
          hnode->insides.s = i-(hnode->insides.b-f);
          *ret = (*ret&0xffffffff)+((uint64_t)(lvl-nodesv[j].lvl)<<32);
          goto END;
        }

        #ifdef RELIQ_AUTOCLOSING
        if (isinescapable(tag))
          break;
        #endif

        if (!j || !nodesv[j].lvl)
          break;
      }

      continue;
    }

    if (!taginfo->script) {
      if (f[i] == '!') {
        comment_handle(f,&i,s);
        i--;
        continue;
      }

      #ifdef RELIQ_AUTOCLOSING
      if (taginfo->autoclosing != (uchar)-1) {
        const reliq_str8 *arr = autoclosing_s[taginfo->autoclosing];
        reliq_cstr name;

        while_is(isspace,f,i,s);
        tagname_handle(f,&i,s,&name);

        for (uchar j = 1; arr[j].b; j++) {
          if (strcasecomp(arr[j],name)) {
            i = tagend-1;
            hnode->insides.s = i-hnode->insides.s+1;
            hnode->all.s = (f+i+1)-hnode->all.b;
            goto END;
          }
        }
      }
      #endif
      i = tagend;
      uint64_t rettmp = html_struct_handle(&i,lvl+1,st);
      if (unlikely(st->err)) {
        *ret = 0;
        goto ERR;
      }
      *ret += rettmp&0xffffffff;
      hnode = ((reliq_hnode*)nodes->v)+hnindex;
      uint32_t lvldiff = rettmp>>32;
      if (likely(lvldiff)) {
        if (lvldiff > 1) {
          hnode->insides.s = i-(hnode->insides.b-f);
          *ret |= ((rettmp>>32)-1)<<32;
          goto END;
        } else
          goto FINAL_TAG_END;
      }
      *ret |= rettmp&0xffffffff00000000;
    }
  }

  END: ;
  *pos = i;
  return 0;

  ERR: ;
  *pos = i;
  return 1;
}

static uchar
attribs_handle(const char *f, size_t *pos, const size_t s, reliq_hnode *hnode, flexarr *attribs) //attribs: reliq_cstr_pair
{
  size_t i = *pos;
  uchar ended = 0;

  for (; i < s && f[i] != '>';) {
    if (isspace(f[i])) {
      i++;
      continue;
    }

    if (unlikely(f[i] == '/')) {
      size_t j = i+1;
      for (; j < s && f[j] != '>'; j++) {
        if (isspace(f[j]))
          continue;

        i = j;
        goto CONTINUE;
      }

      i = j;
      hnode->all.s = f+i-hnode->all.b+1;
      ended = 1;
      break;
    }

    if (unlikely(f[i] == '>'))
       break;

    attrib_handle(f,&i,s,attribs);

    CONTINUE: ;
  }

  *pos = i;
  return ended;
}

static reliq_error *
exec_hnode(const reliq_hnode *hn, struct html_process_expr *expr, flexarr *nodes, flexarr *attribs)
{
  if (!expr->expr)
    return NULL;

  reliq *rq = expr->rq;
  rq->nodes = (reliq_hnode*)nodes->v;
  rq->nodesl = nodes->size;
  rq->attribs = (reliq_cstr_pair*)attribs->v;
  rq->attribsl = attribs->size;

  int r = reliq_nexec(expr->rq,hn,NULL,expr->expr);
  if (!r)
    return NULL;

  reliq_error *err = node_output(hn,NULL,expr->nodef,expr->nodefl,expr->output,expr->rq);

  rq->nodes = NULL;
  rq->nodesl = 0;
  rq->attribs = NULL;
  rq->attribsl = 0;

  return err;
}

static uint64_t
html_struct_handle(size_t *pos, const uint16_t lvl, html_state *st)
{
  size_t i = *pos;
  uint64_t ret = 1;

  if (unlikely(lvl >= RELIQ_MAX_NODE_LEVEL)) {
    st->err = reliq_set_error(RELIQ_ERROR_HTML,"html: %lu: reached %u level of recursion in document",i,lvl);
    ret = 0;
    goto ERR;
  }

  const char *f = st->f;
  const size_t s = st->s;
  flexarr *nodes = st->nodes;
  flexarr *attribs = st->attribs;

  const uint32_t attrib_start = attribs->size;
  struct tag_info taginfo = {
    .foundend = 1
  };
  char const *all_b = f+i;

  i++;
  while_is(isspace,f,i,s);

  if (unlikely(f[i] == '/')) {
    ret = 0;
    goto ERR;
  }

  reliq_hnode *hnode = flexarr_incz(nodes);
  hnode->lvl = lvl;
  hnode->all.b = all_b;
  hnode->all.s = 0;
  size_t hnindex = hnode-(reliq_hnode*)nodes->v;

  if (unlikely(f[i] == '!')) {
    comment_handle(f,&i,s);
    flexarr_dec(nodes);

    ret = 0;
    goto ERR;
  }

  #ifdef RELIQ_PHPTAGS
  if (unlikely(f[i] == '?')) {
    phptag_handle(f,&i,s,hnode,nodes);
    goto END;
  }
  #endif

  tagname_handle(f,&i,s,&hnode->tag);
  if (!hnode->tag.s) {
    flexarr_dec(nodes); //!!
    goto END;
  }

  uchar r = attribs_handle(f,&i,s,hnode,attribs);
  if (r)
    goto END;

  #define search_array(x,y) for (uchar _j = 0; _j < (uchar)LENGTH(x); _j++) \
    if (strcasecomp(x[_j],y))

  search_array(selfclosing_s,hnode->tag) {
    hnode->all.s = f+i-hnode->all.b+1;
    goto END;
  }

  search_array(script_s,hnode->tag) {
    taginfo.script = 1;
    goto FOUND_AND_SKIP_OTHERS;
  }

  #ifdef RELIQ_AUTOCLOSING
  taginfo.autoclosing = -1;
  for (uchar j = 0; j < (uchar)LENGTH(autoclosing_s); j++) {
    if (strcasecomp(autoclosing_s[j][0],hnode->tag)) {
      taginfo.autoclosing = j;
      goto FOUND_AND_SKIP_OTHERS;
    }
  }
  #endif

  FOUND_AND_SKIP_OTHERS: ;
  i++;
  hnode->insides.b = f+i;
  hnode->insides.s = i;

  if (tag_insides_handle(&i,hnindex,&ret,&taginfo,st))
    goto ERR;

  hnode = ((reliq_hnode*)nodes->v)+hnindex;

  END: ;
  if (i >= s) {
    hnode->all.s = s-(hnode->all.b-f)-1;
    hnode->insides.s = s-(hnode->insides.b-f)-1;;
  } else if (!hnode->all.s) {
    hnode->all.s = f+i-hnode->all.b;
    hnode->insides.s = f+i-hnode->insides.b;
  }
  if (!taginfo.foundend)
    hnode->insides.s = hnode->all.s;

  hnode->desc_count = ret-1;

  hnode->attribs = attrib_start;

  if (st->expr) {
    st->err = exec_hnode(hnode,st->expr,nodes,attribs);

    nodes->size = hnode-(reliq_hnode*)nodes->v;
    attribs->size = attrib_start;
  }

  ERR: ;
  *pos = i;
  return ret;
}

reliq_error *
html_handle(const char *data, const size_t size, reliq_hnode **nodes, size_t *nodesl, reliq_cstr_pair **attribs, size_t *attribsl, struct html_process_expr *expr)
{
  flexarr *nodes_buffer = flexarr_init(sizeof(reliq_hnode),NODES_INC);
  flexarr *attribs_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);
  html_state st = {
    .expr = expr,
    .f = data,
    .s = size,
    .nodes = nodes_buffer,
    .attribs = attribs_buffer,
  };

  for (size_t i = 0; i < size; i++) {
    while (i < size && data[i] != '<') {
        i++;
    }

    while (i < size && data[i] == '<') {
      html_struct_handle(&i,0,&st);
      if (st.err)
        goto END;
    }
  }

  END: ;

  if (st.err || expr) {
    flexarr_free(nodes_buffer);
    flexarr_free(attribs_buffer);

    if (!expr) {
      *nodes = NULL;
      *nodesl = 0;
      *attribs = NULL;
      *attribsl = 0;
    }
  } else {
    flexarr_conv(nodes_buffer,(void**)nodes,nodesl);
    flexarr_conv(attribs_buffer,(void**)attribs,attribsl);
  }
  return st.err;
}
