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

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "output.h"
#include "html.h"

const reliq_str8 selfclosing_s[] = { //tags that don't end with </tag>
  {"br",2},{"hr",2},{"img",3},{"input",5},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"link",4},{"meta",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const reliq_str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef RELIQ_AUTOCLOSING
const reliq_str8 *autoclosing_s[] = { //tags that don't need to be closed
  (const reliq_str8[]){{"p",1},{"p",1},{"div",3},{"ul",2},{"h1",2},{"h2",2},{"h3",2},{"h4",2},{"h5",2},{"h6",2},{"dl",2},{"header",6},{"article",7},{"aside",5},{"footer",6},{"hr",2},{"main",4},{"menu",4},{"nav",3},{"ol",2},{"pre",3},{"section",7},{"table",5},{"form",4},{"blockquote",10},{"details",7},{"address",7},{"fieldset",8},{"figcaption",10},{"figure",6},{"hgroup",6},{"search",6},{NULL,0}},
  (const reliq_str8[]){{"li",2},{"li",2},{NULL,0}},
  (const reliq_str8[]){{"tr",2},{"tr",2},{NULL,0}},
  (const reliq_str8[]){{"td",2},{"td",2},{"th",2},{NULL,0}},
  (const reliq_str8[]){{"th",2},{"th",2},{"td",2},{NULL,0}},
  (const reliq_str8[]){{"dt",2},{"dt",2},{"dd",2},{NULL,0}},
  (const reliq_str8[]){{"dd",2},{"dd",2},{"dt",2},{NULL,0}},
  (const reliq_str8[]){{"thead",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const reliq_str8[]){{"tbody",5},{"tbody",5},{"tfoot",5},{NULL,0}},
  (const reliq_str8[]){{"tfoot",5},{NULL,0}},
  (const reliq_str8[]){{"rt",2},{"rt",2},{"rp",2},{NULL,0}},
  (const reliq_str8[]){{"rp",2},{"rp",2},{"rt",2},{NULL,0}},
  (const reliq_str8[]){{"optgroup",8},{"optgroup",8},{"hr",2},{NULL,0}},
  (const reliq_str8[]){{"option",6},{"option",6},{"optgroup",8},{"tr",2},{NULL,0}},
  (const reliq_str8[]){{"colgroup",8},{"colgroup",8},{NULL,0}},
};
#endif

static void
comment_handle(const char *f, size_t *pos, const size_t s)
{
  size_t i = *pos;
  i++;
  if (f[i] == '-' && f[i+1] == '-') {
    i += 2;
    while (s-i > 2 && memcmp(f+i,"-->",3) != 0)
      i++;
    i += 3;
  } else {
    while (i < s && f[i] != '>')
      i++;
  }
  *pos = i;
}

static void
name_handle(const char *f, size_t *pos, const size_t s, reliq_cstr *tag)
{
  size_t i = *pos;
  tag->b = f+i;
  while (i < s && (isalnum(f[i]) || f[i] == '-' || f[i] == '_' || f[i] == ':'))
    i++;
  tag->s = (f+i)-tag->b;
  *pos = i;
}

static void
attrib_handle(const char *f, size_t *pos, const size_t s, flexarr *attribs)
{
  size_t i = *pos;
  reliq_cstr_pair *ac = (reliq_cstr_pair*)flexarr_inc(attribs);
  name_handle(f,&i,s,&ac->f);
  while_is(isspace,f,i,s);
  if (f[i] != '=') {
    ac->s.b = NULL;
    ac->s.s = 0;
    goto END;
  }
  i++;
  while_is(isspace,f,i,s);
  if (f[i] == '>') {
    attribs->size--;
    i++;
    goto END;
  }
  if (f[i] == '\'' || f[i] == '"') {
    char delim = f[i++];
    ac->s.b = f+i;
    char *ending = memchr(f+i,delim,s-i);
    if (!ending) {
      i = s;
      goto END;
    }
    i = ending-f;
    ac->s.s = (f+i)-ac->s.b;
    if (f[i] == delim)
      i++;
  } else {
    ac->s.b = f+i;
    while (i < s && !isspace(f[i]) && f[i] != '>')
      i++;
     ac->s.s = (f+i)-ac->s.b;
  }

  END: ;
  *pos = i;
}

#ifdef RELIQ_PHPTAGS
static void
phptag_handle(const char *f, size_t *pos, const size_t s, reliq_hnode *hnode)
{
  size_t i = *pos;
  i++;
  while_is(isspace,f,i,s);
  name_handle(f,&i,s,&hnode->tag);
  hnode->insides.b = f+i;
  hnode->insides.s = 0;

  char *ending;
  for (; i < s; i++) {
    if (f[i] == '\\') {
      i += 2;
      continue;
    }
    if (f[i] == '?' && f[i+1] == '>') {
      hnode->insides.s = i-1-(hnode->insides.b-f);
      i++;
      break;
    }
    if (f[i] == '"') {
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
    } else if (f[i] == '\'') {
      i++;
      ending = memchr(f+i,'\'',s-i);
      if (ending) {
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

uint64_t
html_struct_handle(const char *f, size_t *pos, const size_t s, const uint16_t lvl, flexarr *nodes, reliq *rq, reliq_error **err)
{
  *err = NULL;
  uint64_t ret = 1;
  size_t i = *pos;
  if (lvl >= RELIQ_MAX_NODE_LEVEL) {
    *err = reliq_set_error(RELIQ_ERROR_HTML,"html: %lu: reached %u level of recursion in document",i,lvl);
    ret = 0;
    goto ERR;
  }
  reliq_hnode *hnode = flexarr_inc(nodes);
  memset(hnode,0,sizeof(reliq_hnode));
  hnode->lvl = lvl;
  size_t index = nodes->size-1;
  flexarr *a = (flexarr*)rq->attrib_buffer;
  size_t attrib_start = a->size;
  uchar foundend=1;

  hnode->all.b = f+i;
  hnode->all.s = 0;
  i++;
  while_is(isspace,f,i,s);
  if (f[i] == '!') {
    comment_handle(f,&i,s);
    flexarr_dec(nodes);
    ret = 0;
    goto ERR;
  }

  #ifdef RELIQ_PHPTAGS
  if (f[i] == '?') {
    phptag_handle(f,&i,s,hnode);
    goto END;
  }
  #endif

  name_handle(f,&i,s,&hnode->tag);
  for (; i < s && f[i] != '>';) {
    while_is(isspace,f,i,s);
    if (f[i] == '/') {
      char *r = memchr(f+i,'>',s-i);
      if (r != NULL)
        hnode->all.s = r-hnode->all.b+1;
      goto END;
    }

    if (!isalpha(f[i])) {
      if (f[i] == '>')
          break;
      i++;
      continue;
    }

    while_is(isspace,f,i,s);
    attrib_handle(f,&i,s,a);
  }

  #define search_array(x,y) for (uchar _j = 0; _j < (uchar)LENGTH(x); _j++) \
    if (strcasecomp(x[_j],y))

  search_array(selfclosing_s,hnode->tag) {
    hnode->all.s = f+i-hnode->all.b+1;
    goto END;
  }

  uchar script = 0;
  search_array(script_s,hnode->tag) {
    script = 1;
    goto FOUND_AND_SKIP_OTHERS;
  }

  #ifdef RELIQ_AUTOCLOSING
  uchar autoclosing = -1;
  for (uchar j = 0; j < (uchar)LENGTH(autoclosing_s); j++) {
    if (strcasecomp(autoclosing_s[j][0],hnode->tag)) {
      autoclosing = j;
      goto FOUND_AND_SKIP_OTHERS;
    }
  }
  #endif

  FOUND_AND_SKIP_OTHERS: ;
  i++;
  hnode->insides.b = f+i;
  hnode->insides.s = i;
  size_t tagend;
  for (; i < s; i++) {
    if (f[i] != '<')
      continue;

    FINAL_TAG_END: ;
    tagend=i;
    i++;
    while_is(isspace,f,i,s);
    if (f[i] == '/') {
      i++;
      while_is(isspace,f,i,s);

      if (i+hnode->tag.s < s && memcasecmp(hnode->tag.b,f+i,hnode->tag.s) == 0) {
        hnode->insides.s = tagend-hnode->insides.s;
        i += hnode->tag.s;
        char *ending = memchr(f+i,'>',s-i);
        if (!ending) {
          i = s;
          flexarr_dec(nodes);
          ret = 0;
          goto ERR;
        }
        i = ending-f;
        hnode->all.s = (f+i+1)-hnode->all.b;
        goto END;
      }

      if (!index) {
        foundend = 0;
        continue;
      }

      reliq_cstr endname;
      reliq_hnode *nodesv = (reliq_hnode*)nodes->v;
      name_handle(f,&i,s,&endname);
      if (!endname.s) {
        i++;
        continue;
      }
      for (size_t j = index-1;; j--) {
        if (nodesv[j].all.s || nodesv[j].lvl >= lvl) {
          if (!j)
            break;
          continue;
        }
        if (strcasecomp(nodesv[j].tag,endname)) {
          i = tagend;
          hnode->insides.s = i-(hnode->insides.b-f);
          ret = (ret&0xffffffff)+((uint64_t)(lvl-nodesv[j].lvl)<<32);
          goto END;
        }
        if (!j || !nodesv[j].lvl)
          break;
      }
    } else if (!script) {
      if (f[i] == '!') {
        i++;
        comment_handle(f,&i,s);
        continue;
      } else {
        #ifdef RELIQ_AUTOCLOSING
        if (autoclosing != (uchar)-1) {
          const reliq_str8 *arr = autoclosing_s[autoclosing];
          reliq_cstr name;

          while_is(isspace,f,i,s);
          name_handle(f,&i,s,&name);

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
        uint64_t rettmp = html_struct_handle(f,&i,s,lvl+1,nodes,rq,err);
        if (*err) {
          ret = 0;
          goto ERR;
        }
        ret += rettmp&0xffffffff;
        hnode = &((reliq_hnode*)nodes->v)[index];
        uint32_t lvldiff = rettmp>>32;
        if (lvldiff) {
          if (lvldiff > 1) {
            hnode->insides.s = i-(hnode->insides.b-f);
            ret |= ((rettmp>>32)-1)<<32;
            goto END;
          } else
            goto FINAL_TAG_END;
        }
        ret |= rettmp&0xffffffff00000000;
      }
    }
  }

  END: ;
  if (i >= s) {
    hnode->all.s = s-(hnode->all.b-f)-1;
    hnode->insides.s = s-(hnode->insides.b-f)-1;;
  } else if (!hnode->all.s) {
    hnode->all.s = f+i-hnode->all.b;
    hnode->insides.s = f+i-hnode->insides.b;
  }
  if (!foundend)
    hnode->insides.s = hnode->all.s;

  size_t size = a->size-attrib_start;
  hnode->attribsl = size;
  hnode->desc_count = ret-1;
  if (rq->flags&RELIQ_SAVE) {
    hnode->attribs = size ?
        memdup(a->v+(attrib_start*a->elsize),size*a->elsize)
        : NULL;
  } else {
    hnode->attribs = a->v+(attrib_start*a->elsize);
    reliq_npattern const *expr = rq->expr;
    if (expr && reliq_nexec(rq,hnode,NULL,expr))
      *err = node_output(hnode,NULL,rq->nodef,rq->nodefl,rq->output,rq);
    flexarr_dec(nodes);
  }
  a->size = attrib_start;
  ERR: ;
  *pos = i;
  return ret;
}
