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
const reliq_str8 autoclosing_s[] = { //tags that don't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},
  {"tfoot",5},{"thead",5},{"rt",2},{"rp",2},
  {"caption",7},{"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

static void
comment_handle(const char *f, size_t *i, const size_t s)
{
  (*i)++;
  if (f[*i] == '-' && f[*i+1] == '-') {
    *i += 2;
    while (s-*i > 2 && memcmp(f+*i,"-->",3) != 0)
      (*i)++;
    *i += 3;
  } else {
    while (*i < s && f[*i] != '>')
      (*i)++;
  }
}

static void
name_handle(const char *f, size_t *i, const size_t s, reliq_cstr *tag)
{
    tag->b = f+*i;
    while (*i < s && (isalnum(f[*i]) || f[*i] == '-' || f[*i] == '_' || f[*i] == ':'))
      (*i)++;
    tag->s = (f+*i)-tag->b;
}

static void
attrib_handle(const char *f, size_t *i, const size_t s, flexarr *attribs)
{
  reliq_cstr_pair *ac = (reliq_cstr_pair*)flexarr_inc(attribs);
  name_handle(f,i,s,&ac->f);
  while_is(isspace,f,*i,s);
  if (f[*i] != '=') {
    ac->s.b = NULL;
    ac->s.s = 0;
    return;
  }
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '>') {
    attribs->size--;
    (*i)++;
    return;
  }
  if (f[*i] == '\'' || f[*i] == '"') {
    char delim = f[(*i)++];
    ac->s.b = f+*i;
    char *ending = memchr(f+*i,delim,s-*i);
    if (!ending) {
      *i = s;
      return;
    }
    *i = ending-f;
    ac->s.s = (f+*i)-ac->s.b;
    if (f[*i] == delim)
      (*i)++;
  } else {
    ac->s.b = f+*i;
    while (*i < s && !isspace(f[*i]) && f[*i] != '>')
      (*i)++;
     ac->s.s = (f+*i)-ac->s.b;
  }
}

#ifdef RELIQ_PHPTAGS
static void
phptag_handle(const char *f, size_t *i, const size_t s, reliq_hnode *hnode)
{
  (*i)++;
  while_is(isspace,f,*i,s);
  name_handle(f,i,s,&hnode->tag);
  hnode->insides.b = f+*i;
  hnode->insides.s = 0;

  char *ending;
  for (; *i < s; (*i)++) {
    if (f[*i] == '\\') {
      *i += 2;
      continue;
    }
    if (f[*i] == '?' && f[*i+1] == '>') {
      hnode->insides.s = (*i)-1-(hnode->insides.b-f);
      (*i)++;
      break;
    }
    if (f[*i] == '"') {
      (*i)++;
      size_t n,jumpv;
      while (1) {
        ending = memchr(f+*i,'"',s-*i);
        if (!ending) {
          *i = s;
          return;
        }
        jumpv = ending-f-*i;
        if (!jumpv) {
          (*i)++;
          break;
        }
        *i = ending-f;
        n = 1;
        while (jumpv && f[*i-n] == '\\')
          n++;
        if ((n-1)&1)
          continue;
        break;
      }
    } else if (f[*i] == '\'') {
      (*i)++;
      ending = memchr(f+*i,'\'',s-*i);
      if (ending) {
        *i = ending-f;
      } else {
        *i = s;
        return;
      }
    }
  }
  hnode->all.s = (f+*i)-hnode->all.b+1;
}
#endif

ulong
html_struct_handle(const char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, reliq *rq, reliq_error **err)
{
  *err = NULL;
  ulong ret = 1;
  reliq_hnode *hnode = flexarr_inc(nodes);
  memset(hnode,0,sizeof(reliq_hnode));
  hnode->lvl = lvl;
  size_t index = nodes->size-1;
  flexarr *a = (flexarr*)rq->attrib_buffer;
  size_t attrib_start = a->size;
  uchar foundend = 1;

  hnode->all.b = f+*i;
  hnode->all.s = 0;
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '!') {
    comment_handle(f,i,s);
    flexarr_dec(nodes);
    return 0;
  }

  #ifdef RELIQ_PHPTAGS
  if (f[*i] == '?') {
    phptag_handle(f,i,s,hnode);
    goto END;
  }
  #endif

  name_handle(f,i,s,&hnode->tag);
  for (; *i < s && f[*i] != '>';) {
    while_is(isspace,f,*i,s);
    if (f[*i] == '/') {
      char *r = memchr(f+*i,'>',s-*i);
      if (r != NULL)
        hnode->all.s = r-hnode->all.b+1;
      goto END;
    }

    if (!isalpha(f[*i])) {
      if (f[*i] == '>')
          break;
      (*i)++;
      continue;
    }

    while_is(isspace,f,*i,s);
    attrib_handle(f,i,s,a);
  }

  #define search_array(x,y) for (uint _j = 0; _j < (uint)LENGTH(x); _j++) \
    if (strcomp(x[_j],y))

  search_array(selfclosing_s,hnode->tag) {
    hnode->all.s = f+*i-hnode->all.b+1;
    goto END;
  }

  uchar script = 0;
  search_array(script_s,hnode->tag) {
    script = 1;
    break;
  }

  #ifdef RELIQ_AUTOCLOSING
  uchar autoclosing = 0;
  search_array(autoclosing_s,hnode->tag) {
    autoclosing = 1;
    break;
  }
  #endif

  (*i)++;
  hnode->insides.b = f+*i;
  hnode->insides.s = *i;
  size_t tagend;
  while (*i < s) {
    if (f[*i] == '<') {
      tagend=*i;
      (*i)++;
      while_is(isspace,f,*i,s);
      if (f[*i] == '/') {
        (*i)++;
        while_is(isspace,f,*i,s);

        if (*i+hnode->tag.s < s && memcmp(hnode->tag.b,f+*i,hnode->tag.s) == 0) {
          hnode->insides.s = tagend-hnode->insides.s;
          *i += hnode->tag.s;
          char *ending = memchr(f+*i,'>',s-*i);
          if (!ending) {
            *i = s;
            flexarr_dec(nodes);
            return 0;
          }
          *i = ending-f;
          hnode->all.s = (f+*i+1)-hnode->all.b;
          goto END;
        }

        if (!index) {
          foundend = 0;
          continue;
        }

        reliq_cstr endname;
        reliq_hnode *nodesv = (reliq_hnode*)nodes->v;
        name_handle(f,i,s,&endname);
        if (!endname.s) {
          (*i)++;
          continue;
        }
        for (size_t j = index-1;; j--) {
          if (nodesv[j].all.s || nodesv[j].lvl >= lvl) {
            if (!j)
              break;
            continue;
          }
          if (strcomp(nodesv[j].tag,endname)) {
            *i = tagend;
            hnode->insides.s = *i-hnode->insides.s;
            ret = (ret&0xffffffff)+((ulong)(lvl-nodesv[j].lvl-1)<<32);
            goto END;
          }
          if (!j || !nodesv[j].lvl)
            break;
        }
      } else if (!script) {
        if (f[*i] == '!') {
          (*i)++;
          comment_handle(f,i,s);
          continue;
        } else {
          #ifdef RELIQ_AUTOCLOSING
          if (autoclosing) {
            reliq_cstr name;

            while_is(isspace,f,*i,s);
            name_handle(f,i,s,&name);

            if (strcomp(hnode->tag,name)) {
              *i = tagend-1;
              hnode->insides.s = *i-hnode->insides.s+1;
              hnode->all.s = (f+*i+1)-hnode->all.b;
              goto END;
            }
          }
          #endif
          *i = tagend;
          ulong rettmp = html_struct_handle(f,i,s,lvl+1,nodes,rq,err);
          if (*err)
            goto END;
          ret += rettmp&0xffffffff;
          hnode = &((reliq_hnode*)nodes->v)[index];
          if (rettmp>>32) {
            (*i)--;
            hnode->insides.s = *i-hnode->insides.s+1;
            hnode->all.s = (f+*i+1)-hnode->all.b;
            ret |= ((rettmp>>32)-1)<<32;
            goto END;
          }
          ret |= rettmp&0xffffffff00000000;
        }
      }
    }
    (*i)++;
  }

  END: ;
  if (*i >= s) {
    hnode->all.s = s-(hnode->all.b-f)-1;
  } else if (!hnode->all.s)
    hnode->all.s = f+*i-hnode->all.b;
  if (!foundend)
    hnode->insides.s = hnode->all.s;

  size_t size = a->size-attrib_start;
  hnode->attribsl = size;
  hnode->child_count = ret-1;
  if (rq->flags&RELIQ_SAVE) {
    hnode->attribs = size ?
        memdup(a->v+(attrib_start*a->elsize),size*a->elsize)
        : NULL;
  } else {
    hnode->attribs = a->v+(attrib_start*a->elsize);
    reliq_node const *expr = rq->expr;
    if (expr && reliq_match(hnode,NULL,expr))
      *err = node_output(hnode,NULL,rq->nodef,rq->nodefl,rq->output,rq);
    flexarr_dec(nodes);
  }
  a->size = attrib_start;
  return ret;
}
