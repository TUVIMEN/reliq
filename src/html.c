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

#include <stdlib.h>
#include <string.h>

#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "npattern.h"
#include "html.h"

#define ATTRIB_INC (1<<13)
#define NODES_INC (1<<13)

const cstr8 selfclosing_s[] = { //tags that don't end with </tag>
  {"br",2},{"img",3},{"input",5},{"link",4},{"meta",4},{"hr",2},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const cstr8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef RELIQ_AUTOCLOSING
const cstr8 *autoclosing_s[] = { //tags that don't need to be closed
  (const cstr8[]){{"p",1},{"p",1},{"div",3},{"ul",2},{"h1",2},{"h2",2},{"h3",2},{"h4",2},{"h5",2},{"h6",2},{"dl",2},{"dd",2},{"dt",2},{"header",6},{"article",7},{"aside",5},{"footer",6},{"hr",2},{"main",4},{"menu",4},{"nav",3},{"ol",2},{"pre",3},{"section",7},{"table",5},{"form",4},{"blockquote",10},{"details",7},{"address",7},{"fieldset",8},{"figcaption",10},{"caption",7},{"figure",6},{"hgroup",6},{"search",6},{NULL,0}},
  (const cstr8[]){{"li",2},{"li",2},{NULL,0}},
  (const cstr8[]){{"tr",2},{"tr",2},{NULL,0}},
  (const cstr8[]){{"td",2},{"td",2},{"th",2},{NULL,0}},
  (const cstr8[]){{"th",2},{"th",2},{"td",2},{NULL,0}},
  (const cstr8[]){{"dt",2},{"dt",2},{"dd",2},{NULL,0}},
  (const cstr8[]){{"dd",2},{"dd",2},{"dt",2},{NULL,0}},
  (const cstr8[]){{"table",5},{"table",5},{NULL,0}},
  (const cstr8[]){{"thead",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const cstr8[]){{"tbody",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const cstr8[]){{"tfoot",5},{"thead",5},{"tbody",5},{NULL,0}},
  (const cstr8[]){{"rt",2},{"rt",2},{"rp",2},{NULL,0}},
  (const cstr8[]){{"rp",2},{"rp",2},{"rt",2},{NULL,0}},
  (const cstr8[]){{"optgroup",8},{"optgroup",8},{"hr",2},{NULL,0}},
  (const cstr8[]){{"option",6},{"option",6},{"optgroup",8},{"tr",2},{NULL,0}},
  (const cstr8[]){{"colgroup",8},{"colgroup",8},{NULL,0}},
};

/* tags from which no closing tag can escape
   e.g. <div><table></div></table></div> is valid because of it. */
const cstr8 inescapable_s[] = {
    {"table",5}
};
#endif

typedef struct {
    flexarr *nodes; //reliq_chnode
    flexarr *attribs; //reliq_cattrib
    reliq_error *err;
    const char *f;
    const size_t s;
    uint32_t tag_count;
    uint32_t text_count;
    uint32_t comment_count;
} html_state;

static void
comment_handle(const char *f, size_t *pos, const size_t s, reliq_chnode *hn)
{
  size_t i = *pos;
  i++;

  size_t diff = 0;
  const size_t base = hn->all;

  if (f[i] == '-' && f[i+1] == '-') {
    i += 2;
    diff = 2;
    hn->tagl = i-base;
    while (s-i > diff && (f[i] != '-' || f[i+1] != '-' || f[i+2] != '>'))
      i++;
  } else {
    hn->tagl = i-base;
    while (i < s && f[i] != '>')
      i++;
  }

  if (s-i > diff) {
    hn->endtag = i-base;
    i += diff+1;
  } else {
    i = s-1;
    hn->endtag = i-base;
  }
  hn->all_len = i-hn->all;
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
attribname_handle(const char *f, size_t *pos, const size_t s, reliq_cattrib *a)
{
  size_t i = *pos;
  a->key = i;
  while (i < s && f[i] != '=' && f[i] != '>' && f[i] != '/' && !isspace(f[i]))
    i++;
  a->keyl = i-a->key;
  *pos = i;
}

static void
attrib_value_handle(const char *f, size_t *pos, const size_t s, reliq_cattrib *a)
{
  size_t i = *pos;
  const size_t base = a->key+a->keyl;
  i++;
  while_is(isspace,f,i,s);

  if (likely(f[i] == '\'' || f[i] == '"')) {
    char delim = f[i++];
    a->value = i-base;
    char *ending = memchr(f+i,delim,s-i);
    if (unlikely(!ending)) {
      i = s;
      a->valuel = i-(base+a->value);
      goto END;
    }
    i = ending-f;
    a->valuel = i-(base+a->value);
    if (likely(f[i] == delim))
      i++;
  } else {
    a->value = i-base;
    while (i < s && !isspace(f[i]) && f[i] != '>')
      i++;
    a->valuel = i-(base+a->value);
  }

  END: ;
  *pos = i;
}

static void
attrib_handle(const char *f, size_t *pos, const size_t s, flexarr *attribs) //attribs: reliq_cattrib
{
  size_t i = *pos;
  reliq_cattrib *ac = (reliq_cattrib*)flexarr_inc(attribs);
  attribname_handle(f,&i,s,ac);
  while_is(isspace,f,i,s);
  ac->value = 0;
  ac->valuel = 0;
  if (unlikely(f[i] == '='))
    attrib_value_handle(f,&i,s,ac);
  *pos = i;
}

#ifdef RELIQ_PHPTAGS
static uchar
phptag_skip_dquote(const char *f, size_t *pos, const size_t s)
{
  size_t n,jumpv;
  size_t i = *pos;
  uchar err = 0;

  while (1) {
    char *ending = memchr(f+i,'"',s-i);
    if (!ending) {
      i = s;
      err = 1;
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

  END: ;
  *pos = i;
  return err;
}

static uint32_t
phptag_handle(const char *f, size_t *pos, const size_t s, reliq_chnode *hnode, flexarr *nodes) //nodes: reliq_chnode
{
  size_t i = *pos;
  i++;
  while_is(isspace,f,i,s);
  reliq_cstr tagname;
  tagname_handle(f,&i,s,&tagname);
  if (!tagname.s) {
    flexarr_dec(nodes);
    return -1;
  }
  size_t base = hnode->tag = tagname.b-f-hnode->all;
  base += hnode->all;
  base += hnode->tagl = tagname.s;

  char *ending;
  for (; i < s; i++) {
    if (unlikely(f[i] == '\\')) {
      i += 2;
      continue;
    }
    if (unlikely(f[i] == '?' && f[i+1] == '>')) {
      hnode->endtag = i-1-base;
      i++;
      break;
    }
    if (unlikely(f[i] == '"')) {
      i++;
      if (phptag_skip_dquote(f,&i,s))
        goto END;
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
  hnode->all_len = i-hnode->all+1;
  END: ;
  *pos = i;
  return 0;
}
#endif

#ifdef RELIQ_AUTOCLOSING
static uchar
isinescapable(reliq_cstr str)
{
  for (size_t g = 0; g < LENGTH(inescapable_s); g++)
    if (strcaseeq(str,inescapable_s[g]))
      return 1;
  return 0;
}
#endif

static uint32_t
html_struct_handle(size_t *pos, const uint16_t lvl, html_state *st);

struct tag_info {
    #ifdef RELIQ_AUTOCLOSING
    uchar autoclosing;
    #endif
    uchar foundend : 1;
    uchar script : 1;
};

static inline uint32_t
last_attrib(const flexarr *attrib) //attrib: reliq_cattrib
{
  /*
    this artifact is created because of deletion of attribsl field from reliq_chnode,
    this requires that each node, even if they inherently cannot have attribs have
    attribs field set to at least to what previous node had set it to so that
    prevnode.attribs-node.attribs will not overflow
  */
  return attrib->size;
}

static uchar
text_is_empty(const char *text, const size_t textl)
{
  for (size_t i = 0; i < textl; i++)
    if (!isspace(text[i]))
      return 0;
  return 1;
}

static void
text_finish(size_t *tnindex, flexarr *nodes, const size_t textstart, const size_t textend, uint32_t *htmlerr, const char *f)
{
  uint32_t err = *htmlerr;
  *htmlerr = 0;
  if (*tnindex == (size_t)-1)
    return;

  reliq_chnode *tn = ((reliq_chnode*)nodes->v)+*tnindex;
  tn->all = textstart;
  const size_t len = tn->all_len = textend-textstart;
  if (err > 1) {
    tn->tagl = 2;
  } else if (text_is_empty(f+textstart,len))
    tn->tagl = 1;
  *tnindex = -1;
}

static void
text_add(html_state *st, const uint16_t lvl, size_t *tnindex)
{
  st->text_count++;
  reliq_chnode *tn = flexarr_incz(st->nodes);
  tn->attribs = last_attrib(st->attribs);
  tn->lvl = lvl;
  *tnindex = tn-((reliq_chnode*)st->nodes->v);
}

#ifdef RELIQ_AUTOCLOSING
static uchar
autocloses(const char *f, size_t pos, const size_t s, const uchar autoclosing)
{
  if (autoclosing == (uchar)-1)
    return 0;

  const cstr8 *arr = autoclosing_s[autoclosing];
  reliq_cstr name;

  while_is(isspace,f,pos,s);
  tagname_handle(f,&pos,s,&name);
  if (!name.s)
    return 0;

  for (uchar j = 1; arr[j].b; j++)
    if (strcaseeq(arr[j],name))
      return 1;

  return 0;
}
#endif

static uchar
ancestor_ending(const char *f, size_t *pos, reliq_cstr endname, const reliq_chnode *nodesv, const size_t hnindex, reliq_chnode *hnode, const uint16_t lvl, const size_t tagend, const size_t base, uint32_t *fallback)
{
  for (size_t j = hnindex-1;; j--) {
    if (nodesv[j].tagl == 0 || nodesv[j].all_len || nodesv[j].lvl >= lvl) {
      if (!j)
        break;

      continue;
    }
    reliq_cstr tag = { .b = f+nodesv[j].all+nodesv[j].tag, .s = nodesv[j].tagl };
    if (strcaseeq(tag,endname)) {
      *pos = tagend;
      hnode->endtag = *pos-base;
      *fallback = lvl-nodesv[j].lvl;
      return 1;
    }

    #ifdef RELIQ_AUTOCLOSING
    if (isinescapable(tag))
      return 0;
    #endif

    if (!j || !nodesv[j].lvl)
      break;
  }
  return 0;
}

static uchar
handle_ending(html_state *st, size_t *pos, reliq_cstr tagname, const size_t hnindex, uint32_t *htmlerr, struct tag_info *info, const uint16_t lvl, const size_t tagend, const size_t base, uint32_t *fallback)
{
  uchar end = 0;
  size_t i = *pos;
  reliq_cstr endname;
  const char *f = st->f;
  const size_t s = st->s;
  reliq_chnode *nodesv = (reliq_chnode*)st->nodes->v;
  reliq_chnode *hnode = nodesv+hnindex;
  tagname_handle(f,&i,s,&endname);

  if (unlikely(!endname.s)) {
    i++;
    goto END;
  }

  if (strcaseeq(tagname,endname)) {
    hnode->endtag = tagend-base;
    while (i < s && f[i] != '>')
      i++;

    if (unlikely(i >= s)) {
      i = s;
      hnode->all_len = s-hnode->all;
      (*htmlerr)++;
      end = 1;
      goto END;
    }

    hnode->all_len = i+1-hnode->all;
    end = 1;
    goto END;
  }

  if (unlikely(hnode->lvl == 0 || !hnindex)) {
    info->foundend = 0;
    goto END;
  }

  if (info->script)
    goto END;

  #ifdef RELIQ_AUTOCLOSING
  if (isinescapable(tagname))
    goto END;
  #endif

  end = ancestor_ending(f,&i,endname,nodesv,hnindex,hnode,lvl,tagend,base,fallback);

  END: ;
  *pos = i;
  return end;
}

static uchar
tag_insides_handle(size_t *pos, const size_t hnindex, uint32_t *fallback, struct tag_info *taginfo, html_state *st)
{
  uchar err = 0;
  const char *f = st->f;
  const size_t s = st->s;
  flexarr *nodes = st->nodes; //reliq_chnode
  size_t i = *pos;
  reliq_chnode *hnode = ((reliq_chnode*)nodes->v)+hnindex;
  uint16_t lvl = hnode->lvl;
  size_t tagend;
  size_t base = hnode->all+hnode->tag;
  const flexarr *attribs = st->attribs;
  reliq_cstr tagname = { .b = f+base, .s = hnode->tagl };
  base += hnode->tagl;
  size_t tnindex = -1; //textnode index
  size_t textstart=0,textend = 0;
  uint32_t htmlerr = 0;

  for (; i < s; i++) {
    textstart = i;

    TEXT_REPEAT: ;
    htmlerr++;
    while (i < s && f[i] != '<')
      i++;
    textend = i;

    if (textstart != i && tnindex == (size_t)-1)
      text_add(st,lvl+1,&tnindex);

    if (i >= s)
      break;

    FINAL_TAG_END: ;
    tagend=i;
    i++;
    while_is(isspace,f,i,s);
    if (unlikely(f[i] == '/')) {
      i++;
      while_is(isspace,f,i,s);

      if (handle_ending(st,&i,tagname,hnindex,&htmlerr,taginfo,lvl,tagend,base,fallback))
        goto END;
      goto TEXT_REPEAT;
    }

    if (taginfo->script)
      goto TEXT_REPEAT;

    if (f[i] == '!') {
      reliq_chnode *hn = flexarr_incz(nodes);
      hn->lvl = lvl+1;
      hn->all = tagend;
      hn->all_len = 0;
      hn->attribs = last_attrib(attribs);
      comment_handle(f,&i,s,hn);
      st->comment_count++;
      i--;
      goto CONTINUE;
    }

    #ifdef RELIQ_AUTOCLOSING
    if (autocloses(f,i,s,taginfo->autoclosing)) {
      hnode = ((reliq_chnode*)nodes->v)+hnindex;
      hnode->endtag = tagend-base;
      hnode->all_len = tagend-hnode->all;
      i = tagend-1;
      goto END;
    }
    #endif

    i = tagend;
    uint32_t nfallback = html_struct_handle(&i,lvl+1,st);
    if (unlikely(st->err)) {
      err = 1;
      htmlerr++;
      goto END;
    }
    hnode = ((reliq_chnode*)nodes->v)+hnindex;
    if (likely(nfallback)) {
      if (nfallback == (uint32_t)-1) {
        goto TEXT_REPEAT;
      }
      if (nfallback > 1) {
        hnode->endtag = i-base;
        *fallback = nfallback-1;
        goto END;
      } else
        goto FINAL_TAG_END;
    }
    *fallback = nfallback;

    CONTINUE: ;
    text_finish(&tnindex,nodes,textstart,textend,&htmlerr,st->f);
  }

  END: ;
  text_finish(&tnindex,nodes,textstart,textend,&htmlerr,st->f);

  if (err)
    *fallback = 0;

  *pos = i;
  return err;
}

static uchar
attribs_handle(const char *f, size_t *pos, const size_t s, reliq_chnode *hnode, flexarr *attribs) //attribs: reliq_cattrib
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
      hnode->all_len = i-hnode->all+1;
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

static uchar
find_tag_info(reliq_cstr tagname, struct tag_info *info)
{
  #define search_array(x,y) for (uchar _j = 0; _j < (uchar)LENGTH(x); _j++) \
    if (strcaseeq(x[_j],y))

  search_array(selfclosing_s,tagname)
    return 1;

  search_array(script_s,tagname) {
    info->script = 1;
    return 0;
  }

  #undef search_array

  #ifdef RELIQ_AUTOCLOSING
  info->autoclosing = -1;
  for (uchar j = 0; j < (uchar)LENGTH(autoclosing_s); j++) {
    if (strcaseeq(autoclosing_s[j][0],tagname)) {
      info->autoclosing = j;
      return 0;
    }
  }
  #endif

  return 0;
}

static uint32_t
html_struct_handle(size_t *pos, const uint16_t lvl, html_state *st)
{
  size_t i = *pos;
  uint32_t fallback = 0;

  if (unlikely(lvl >= RELIQ_MAX_NODE_LEVEL)) {
    st->err = reliq_set_error(RELIQ_ERROR_HTML,"html: %lu: reached %u level of recursion in document",i,lvl);
    goto ERR;
  }

  const char *f = st->f;
  const size_t s = st->s;
  flexarr *nodes = st->nodes;
  flexarr *attribs = st->attribs;

  const uint32_t attrib_start = attribs->size;
  struct tag_info taginfo = { .foundend = 1 };
  size_t start = i;

  const uint32_t tag_count = st->tag_count;
  const uint32_t text_count = st->text_count;
  const uint32_t comment_count = st->comment_count;

  i++;
  while_is(isspace,f,i,s);

  if (unlikely(f[i] == '/')) {
    i++;
    fallback = (uint32_t)-1;
    goto ERR;
  }

  reliq_chnode *hnode = flexarr_incz(nodes);
  hnode->lvl = lvl;
  hnode->all = start;
  hnode->attribs = attrib_start;
  size_t hnindex = hnode-(reliq_chnode*)nodes->v;

  if (unlikely(f[i] == '!')) {
    hnode->attribs = last_attrib(attribs);
    comment_handle(f,&i,s,hnode);
    st->comment_count++;

    goto ERR;
  }

  #ifdef RELIQ_PHPTAGS
  if (unlikely(f[i] == '?')) {
    fallback = phptag_handle(f,&i,s,hnode,nodes);
    if (fallback == (uint32_t)-1)
        goto ERR;

    start += hnode->tag;
    start += hnode->tagl;
    goto END;
  }
  #endif

  reliq_cstr tagname;
  tagname_handle(f,&i,s,&tagname);
  if (!tagname.s) {
    flexarr_dec(nodes);
    fallback = -1;
    goto ERR;
  }
  start += hnode->tag = tagname.b-f-start;
  start += hnode->tagl = tagname.s;

  if (i >= s || attribs_handle(f,&i,s,hnode,attribs))
    goto END;

  if (find_tag_info(tagname,&taginfo)) {
    hnode->all_len = i-hnode->all+1;
    goto END;
  }

  i++;

  if (tag_insides_handle(&i,hnindex,&fallback,&taginfo,st)) {
    flexarr_dec(nodes);
    goto ERR;
  }

  hnode = ((reliq_chnode*)nodes->v)+hnindex;

  END: ;
  if (i >= s) {
    hnode->all_len = s-hnode->all;
    if (hnode->endtag == 0)
      hnode->endtag = s-start;
  } else if (!hnode->all_len) {
    hnode->all_len = i-hnode->all;
    hnode->endtag = i-start; //!! this doesn't change anything
  }
  if (!taginfo.foundend)
    hnode->endtag = hnode->all_len-hnode->tag-hnode->tagl;

  hnode->tag_count = st->tag_count-tag_count;
  hnode->text_count = st->text_count-text_count;
  hnode->comment_count = st->comment_count-comment_count;

  st->tag_count++;

  ERR: ;
  *pos = i;
  return fallback;
}

reliq_error *
html_handle(const char *data, const size_t size, reliq_chnode **nodes, size_t *nodesl, reliq_cattrib **attribs, size_t *attribsl)
{
  flexarr nodes_buffer = flexarr_init(sizeof(reliq_chnode),NODES_INC);
  flexarr attribs_buffer = flexarr_init(sizeof(reliq_cattrib),ATTRIB_INC);
  html_state st = {
    .f = data,
    .s = size,
    .nodes = &nodes_buffer,
    .attribs = &attribs_buffer,
  };
  uint32_t htmlerr = 0;

  for (size_t i = 0; i < size;) {
    size_t textstart=i;
    size_t tnindex = -1; //textnode index
    size_t textend;

    TEXT_REPEAT: ;
    while (i < size && data[i] != '<')
      i++;
    textend = i;
    if (textstart != textend) {
      htmlerr++;
      if (tnindex == (size_t)-1)
        text_add(&st,0,&tnindex);
    }

    while (i < size && data[i] == '<') {
      uint32_t r = html_struct_handle(&i,0,&st);
      if (st.err)
        break;
      if (r == (uint32_t)-1)
        goto TEXT_REPEAT;
    }

    if (tnindex != (size_t)-1)
      text_finish(&tnindex,&nodes_buffer,textstart,textend,&htmlerr,data);
    if (st.err)
      break;

    i++;
  }

  if (st.err) {
    flexarr_free(&nodes_buffer);
    flexarr_free(&attribs_buffer);

    *nodes = NULL;
    *nodesl = 0;
    *attribs = NULL;
    *attribsl = 0;
  } else {
    flexarr_conv(&nodes_buffer,(void**)nodes,nodesl);
    flexarr_conv(&attribs_buffer,(void**)attribs,attribsl);
  }
  return st.err;
}
