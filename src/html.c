/*
    hgrep - html searching tool
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
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <stdarg.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "hgrep.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "edit.h"
#include "html.h"

const hgrep_str8 selfclosing_s[] = { //tags that don't end with </tag>
  {"br",2},{"hr",2},{"img",3},{"input",5},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"link",4},{"meta",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const hgrep_str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef HGREP_AUTOCLOSING
const hgrep_str8 autoclosing_s[] = { //tags that don't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},
  {"tfoot",5},{"thead",5},{"rt",2},{"rp",2},
  {"caption",7},{"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

hgrep_error *
node_output(hgrep_node *hgn,
        #ifdef HGREP_EDITING
        const hgrep_format_func *format
        #else
        const char *format
        #endif
        , const size_t formatl, FILE *output, const char *reference) {
  #ifdef HGREP_EDITING
  return format_exec(NULL,0,output,hgn,format,formatl,reference);
  #else
  if (format) {
    hgrep_printf(output,format,formatl,hgn,reference);
  } else
    hgrep_print(output,hgn);
  return NULL;
  #endif
}

struct fcol_out {
  FILE *f;
  char *v;
  size_t s;
  size_t current;
};

#ifdef HGREP_EDITING
void
fcollector_rearrange_pre(struct fcol_pattern *fcols, size_t start, size_t end, ushort lvl)
{
  size_t i=start;
  while (start < end) {
    while (i < end && fcols[i].lvl != lvl)
      i++;

    if (i < end && i != start) {
      struct fcol_pattern t = fcols[i];
      for (size_t j = i-1;; j--) {
        fcols[j+1] = fcols[j];
        if (j == start)
          break;
      }
      fcols[start] = t;

      if (i-start > 1)
        fcollector_rearrange_pre(fcols,start+1,i+1,lvl+1);
    }

    start = ++i;
  }
}

void
fcollector_rearrange(flexarr *fcollector)
{
  fcollector_rearrange_pre((struct fcol_pattern*)fcollector->v,0,fcollector->size,0);
}
#endif

hgrep_error *
nodes_output(hgrep *hg, flexarr *compressed_nodes, flexarr *pcollector
        #ifdef HGREP_EDITING
        , flexarr *fcollector
        #endif
        )
{
  #ifdef HGREP_EDITING
  //fprintf(stderr,"fcollector - size(%lu) compressed_nodes->size(%lu)\n",fcollector->size,compressed_nodes->size);
  struct fcol_pattern *fcols = (struct fcol_pattern*)fcollector->v;
  //for (size_t j = 0; j < fcollector->size; j++)
    //fprintf(stderr,"fcollector start(%lu) end(%lu) diff(%lu) lvl(%u)\n",fcols[j].start,fcols[j].end,(fcols[j].end+1)-fcols[j].start,fcols[j].lvl);
  if (fcollector->size) {
      fcollector_rearrange(fcollector);
      /*fprintf(stderr,"fcollector rearrangement\n");
      for (size_t j = 0; j < fcollector->size; j++)
        fprintf(stderr,"fcollector start(%lu) end(%lu) diff(%lu) lvl(%u)\n",fcols[j].start,fcols[j].end,(fcols[j].end+1)-fcols[j].start,fcols[j].lvl);*/
  }
  #endif
  if (!pcollector->size)
    return NULL;
  hgrep_error *err = NULL;
  hgrep_cstr *pcol = (hgrep_cstr*)pcollector->v;

  FILE *out = hg->output;
  FILE *fout = out;
  size_t j=0,pcurrent=0,g=0;
  #ifdef HGREP_EDITING
  flexarr *outs = flexarr_init(sizeof(struct fcol_out),16);
  size_t fcurrent=0;
  size_t fsize;
  char *ptr;
  #endif
  for (;; j++) {
    #ifdef HGREP_EDITING
    if (compressed_nodes->size && g == 0) {
      if (out != hg->output) {
        fclose(out);
        err = format_exec(ptr,fsize,fout,NULL,
          ((hgrep_epattern*)pcol[pcurrent-1].b)->exprf,
          ((hgrep_epattern*)pcol[pcurrent-1].b)->exprfl,hg->data);
        free(ptr);
        if (err)
          return err;
        out = hg->output;
      }

      //fprintf(stderr,"loop fcurrent(%lu) pg(%lu)\n",fcurrent,pcurrent);

      REP: ;
      if (outs->size) {
        struct fcol_out *fcol_out_last = &((struct fcol_out*)outs->v)[outs->size-1];
        while (fcols[fcol_out_last->current].end == pcurrent) {
          //fprintf(stderr,"fcollector end fcurrent(%lu) pcurrent(%lu)\n",fcurrent,pcurrent);

          FILE *tmp_out = (fcols[fcol_out_last->current].lvl == 0) ? hg->output : ((struct fcol_out*)outs->v)[outs->size-2].f;
          fout = tmp_out;

          fclose(fcol_out_last->f);
          //fprintf(stderr,"%.*s\n",fcol_out_last->s,fcol_out_last->v);
          //fprintf(stderr,"fcollector end\n\n",fcurrent,pcurrent);
          err = format_exec(fcol_out_last->v,fcol_out_last->s,tmp_out,NULL,
            fcols[fcol_out_last->current].p->exprf,
            fcols[fcol_out_last->current].p->exprfl,hg->data);
          free(fcol_out_last->v);
          if (err)
            return err;

          flexarr_dec(outs);
          goto REP;
        }
      }

      while (fcurrent < fcollector->size && fcols[fcurrent].start == pcurrent) { // && fcols[fcurrent].lvl != 0
        //fprintf(stderr,"fcollector start fcurrent(%lu) pcurrent(%lu)\n",fcurrent,pcurrent);
        struct fcol_out *ff;
        ff = (struct fcol_out*)flexarr_inc(outs);
        ff->f = open_memstream(&ff->v,&ff->s);
        ff->current = fcurrent;
        fout = ff->f;
        fcurrent++;
      }

      if (j >= compressed_nodes->size)
        break;
      if (((hgrep_epattern*)pcol[pcurrent].b)->exprfl) {
        out = open_memstream(&ptr,&fsize);
        //fprintf(stderr,"fexpr pcurrent(%u) fout(%p)\n",pcurrent,fout);
      }
    }
    #endif
    if (j >= compressed_nodes->size)
      break;
    hgrep_compressed *x = &((hgrep_compressed*)compressed_nodes->v)[j];
    //fprintf(stderr,"nodes_output istalbe(%u) pcurrent(%u) pcollector->size(%u) j(%lu) name(%.*s)\n",((hgrep_epattern*)pcol[pcurrent].b)->istable,pcurrent,pcollector->size,j,hg->nodes[x->id].tag.s,hg->nodes[x->id].tag.b);
    hg->nodes[x->id].lvl -= x->lvl;
    err = node_output(&hg->nodes[x->id],((hgrep_epattern*)pcol[pcurrent].b)->nodef,
      ((hgrep_epattern*)pcol[pcurrent].b)->nodefl,(out == hg->output) ? fout : out,hg->data);
    hg->nodes[x->id].lvl += x->lvl;
    if (err)
      return err;

    g++;
    if (pcol[pcurrent].s == g) {
      g = 0;
      pcurrent++;
    }
  }
  /*fprintf(stderr,"FCOL outs %lu\n",outs->size);
  for (size_t i = 0; i < outs->size; i++) {
    struct fcol_out *ff = &((struct fcol_out*)outs->v)[i];
    fprintf(stderr,"FCOL outs - %lu %lu\n",ff->current,ff->s);
    //fwrite(ff->v,1,ff->s,hg->output);
  }*/
  return NULL;
}

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
name_handle(const char *f, size_t *i, const size_t s, hgrep_cstr *tag)
{
    tag->b = f+*i;
    while (*i < s && (isalnum(f[*i]) || f[*i] == '-' || f[*i] == '_' || f[*i] == ':'))
      (*i)++;
    tag->s = (f+*i)-tag->b;
}

static void
attrib_handle(const char *f, size_t *i, const size_t s, flexarr *attribs)
{
  hgrep_cstr_pair *ac = (hgrep_cstr_pair*)flexarr_inc(attribs);
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

#ifdef HGREP_PHPTAGS
static void
phptag_handle(const char *f, size_t *i, const size_t s, hgrep_node *hgn)
{
  (*i)++;
  while_is(isspace,f,*i,s);
  name_handle(f,i,s,&hgn->tag);
  hgn->insides.b = f+*i;
  hgn->insides.s = 0;

  char *ending;
  for (; *i < s; (*i)++) {
    if (f[*i] == '\\') {
      *i += 2;
      continue;
    }
    if (f[*i] == '?' && f[*i+1] == '>') {
      hgn->insides.s = (*i)-1-(hgn->insides.b-f);
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
  hgn->all.s = (f+*i)-hgn->all.b+1;
}
#endif

ulong
html_struct_handle(const char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, hgrep *hg, hgrep_error **err)
{
  *err = NULL;
  ulong ret = 1;
  hgrep_node *hgn = flexarr_inc(nodes);
  memset(hgn,0,sizeof(hgrep_node));
  hgn->lvl = lvl;
  size_t index = nodes->size-1;
  flexarr *a = (flexarr*)hg->attrib_buffer;
  size_t attrib_start = a->size;
  uchar foundend = 1;

  hgn->all.b = f+*i;
  hgn->all.s = 0;
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '!') {
    comment_handle(f,i,s);
    flexarr_dec(nodes);
    return 0;
  }

  #ifdef HGREP_PHPTAGS
  if (f[*i] == '?') {
    phptag_handle(f,i,s,hgn);
    goto END;
  }
  #endif

  name_handle(f,i,s,&hgn->tag);
  for (; *i < s && f[*i] != '>';) {
    while_is(isspace,f,*i,s);
    if (f[*i] == '/') {
      char *r = memchr(f+*i,'>',s-*i);
      if (r != NULL)
        hgn->all.s = r-hgn->all.b+1;
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

  search_array(selfclosing_s,hgn->tag) {
    hgn->all.s = f+*i-hgn->all.b+1;
    goto END;
  }

  uchar script = 0;
  search_array(script_s,hgn->tag) {
    script = 1;
    break;
  }

  #ifdef HGREP_AUTOCLOSING
  uchar autoclosing = 0;
  search_array(autoclosing_s,hgn->tag) {
    autoclosing = 1;
    break;
  }
  #endif

  (*i)++;
  hgn->insides.b = f+*i;
  hgn->insides.s = *i;
  size_t tagend;
  while (*i < s) {
    if (f[*i] == '<') {
      tagend=*i;
      (*i)++;
      while_is(isspace,f,*i,s);
      if (f[*i] == '/') {
        (*i)++;
        while_is(isspace,f,*i,s);

        if (*i+hgn->tag.s < s && memcmp(hgn->tag.b,f+*i,hgn->tag.s) == 0) {
          hgn->insides.s = tagend-hgn->insides.s;
          *i += hgn->tag.s;
          char *ending = memchr(f+*i,'>',s-*i);
          if (!ending) {
            *i = s;
            flexarr_dec(nodes);
            return 0;
          }
          *i = ending-f;
          hgn->all.s = (f+*i+1)-hgn->all.b;
          goto END;
        }

        if (!index) {
          foundend = 0;
          continue;
        }

        hgrep_cstr endname;
        hgrep_node *nodesv = (hgrep_node*)nodes->v;
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
            hgn->insides.s = *i-hgn->insides.s;
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
          #ifdef HGREP_AUTOCLOSING
          if (autoclosing) {
            hgrep_cstr name;

            while_is(isspace,f,*i,s);
            name_handle(f,i,s,&name);

            if (strcomp(hgn->tag,name)) {
              *i = tagend-1;
              hgn->insides.s = *i-hgn->insides.s+1;
              hgn->all.s = (f+*i+1)-hgn->all.b;
              goto END;
            }
          }
          #endif
          *i = tagend;
          ulong rettmp = html_struct_handle(f,i,s,lvl+1,nodes,hg,err);
          if (*err)
            goto END;
          ret += rettmp&0xffffffff;
          hgn = &((hgrep_node*)nodes->v)[index];
          if (rettmp>>32) {
            (*i)--;
            hgn->insides.s = *i-hgn->insides.s+1;
            hgn->all.s = (f+*i+1)-hgn->all.b;
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
    hgn->all.s = s-(hgn->all.b-f)-1;
  } else if (!hgn->all.s)
    hgn->all.s = f+*i-hgn->all.b;
  if (!foundend)
    hgn->insides.s = hgn->all.s;

  size_t size = a->size-attrib_start;
  hgn->attribl = size;
  hgn->child_count = ret-1;
  if (hg->flags&HGREP_SAVE) {
    hgn->attrib = size ?
        memdup(a->v+(attrib_start*a->elsize),size*a->elsize)
        : NULL;
  } else {
    hgn->attrib = a->v+(attrib_start*a->elsize);
    hgrep_pattern const *pattern = hg->pattern;
    if (pattern && hgrep_match(hgn,pattern)) {
      *err = node_output(hgn,hg->nodef,hg->nodefl,hg->output,hg->data);
    }
    flexarr_dec(nodes);
  }
  a->size = attrib_start;
  return ret;
}
