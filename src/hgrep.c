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

#define HGREP_SAVE 0x8

#define PASSED_INC (1<<14)
#define PATTERN_SIZE_INC (1<<8)
#define PATTRIB_INC 8

//hgrep_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//hgrep_pattern flags
#define P_INVERT 0x1
#define P_MATCH_INSIDES 0x2
#define P_INVERT_INSIDES 0x4
#define P_EMPTY 0x8

//hgrep_match_function flags
#define F_SBRACKET 0x1
#define F_STRING 0x2
#define F_ATTRIBUTES 0x4
#define F_LEVEL 0x8
#define F_CHILD_COUNT 0x10
#define F_SIZE 0x20
#define F_MATCH_INSIDES 0x40

//hgrep_epattern flags
#define EPATTERN_TABLE 0x1
#define EPATTERN_NEWBLOCK 0x2
#define EPATTERN_NEWCHAIN 0x4

#define ATTRIB_INC (1<<3)
#define HGREP_NODES_INC (1<<13)

struct hgrep_match_function {
  hgrep_str8 name;
  ushort flags;
  uchar flagstoset;
};

const struct hgrep_match_function match_functions[] = {
    {{"m",1},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES},
    {{"M",1},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES|P_INVERT_INSIDES},
    {{"a",1},F_SBRACKET|F_ATTRIBUTES,0},
    {{"l",1},F_SBRACKET|F_LEVEL,0},
    {{"s",1},F_SBRACKET|F_SIZE,0},
    {{"c",1},F_SBRACKET|F_CHILD_COUNT,0},

    {{"match_insides",13},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES},
    {{"rev_match_insides",17},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES|P_INVERT_INSIDES},
    {{"attributes",10},F_SBRACKET|F_ATTRIBUTES,0},
    {{"level",5},F_SBRACKET|F_LEVEL,0},
    {{"size",4},F_SBRACKET|F_SIZE,0},
    {{"child_count",11},F_SBRACKET|F_CHILD_COUNT,0},
};

const hgrep_str8 selfclosing_s[] = { //tags that doesn't end with </tag>
  {"br",2},{"hr",2},{"img",3},{"input",5},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"link",4},{"meta",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const hgrep_str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef HGREP_AUTOCLOSING
const hgrep_str8 autoclosing_s[] = { //tags that doesn't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},
  {"tfoot",5},{"thead",5},{"rt",2},{"rp",2},
  {"caption",7},{"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

hgrep_error *
hgrep_set_error(const int code, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  hgrep_error *err = malloc(sizeof(hgrep_error));
  vsnprintf(err->msg,LENGTH(err->msg),fmt,ap);
  err->code = code;
  va_end(ap);
  return err;
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

static hgrep_error *
node_output_handle(hgrep_node *hgn, 
        #ifdef HGREP_EDITING
        const hgrep_format_func * format
        #else
        const char *format
        #endif
        , const size_t formatl, FILE *output, const char *reference) {
  #ifdef HGREP_EDITING
  return format_exec(output,hgn,format,formatl,reference);
  #else
  if (format) {
    hgrep_printf(output,format,formatl,hgn,reference);
  } else
    hgrep_print(output,hgn);
  return NULL;
  #endif
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

int
hgrep_match(const hgrep_node *hgn, const hgrep_pattern *p)
{
  if (p->flags&P_EMPTY)
    return 1;
  const hgrep_cstr_pair *a = hgn->attrib;
  const struct hgrep_pattrib *attrib = p->attrib;
  uchar rev = ((p->flags&P_INVERT) == P_INVERT);
  regmatch_t pmatch;

  if (!ranges_match(hgn->lvl,p->position_r,p->position_rl,-1)
    || !ranges_match(hgn->attribl,p->attribute_r,p->attribute_rl,-1)
    || !ranges_match(hgn->child_count,p->child_count_r,p->child_count_rl,-1)
    || !ranges_match(hgn->insides.s,p->size_r,p->size_rl,-1))
    return 0^rev;

  pmatch.rm_so = 0;
  pmatch.rm_eo = (int)hgn->tag.s;
  if (regexec(&p->tag,hgn->tag.b,1,&pmatch,REG_STARTEND) != 0)
    return 0^rev;

  for (size_t i = 0; i < p->attribl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hgn->attribl; j++) {
      if (!ranges_match(j,attrib[i].position_r,attrib[i].position_rl,hgn->attribl-1))
        continue;

      pmatch.rm_eo = (int)a[j].f.s;
      if (regexec(&attrib[i].r[0],a[j].f.b,1,&pmatch,REG_STARTEND) != 0)
        continue;

      if (attrib[i].flags&A_VAL_MATTERS) {
        pmatch.rm_eo = (int)a[j].s.s;
        if (regexec(&attrib[i].r[1],a[j].s.b,1,&pmatch,REG_STARTEND) != 0)
          continue;
      }
      
      found = 1;
    }

    if (((attrib[i].flags&A_INVERT)==A_INVERT)^found)
      return 0^rev;
  }

  if (p->flags&P_MATCH_INSIDES) {
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)hgn->insides.s;
    if ((regexec(&p->insides,hgn->insides.b,1,&pmatch,REG_STARTEND) != 0)^((p->flags&P_INVERT_INSIDES)==P_INVERT_INSIDES))
      return 0^rev;
  }

  return 1^rev;
}

static ulong
struct_handle(const char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, hgrep *hg, hgrep_error **err)
{
  *err = NULL;
  ulong ret = 1;
  hgrep_node *hgn = flexarr_inc(nodes);
  memset(hgn,0,sizeof(hgrep_node));
  hgn->lvl = lvl;
  size_t index = nodes->size-1;
  flexarr *a = (flexarr*)hg->attrib_buffer;
  size_t attrib_start = a->size;

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

  #define strcomp(x,y) (x.s == y.s && memcmp(y.b,x.b,y.s) == 0)
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

        if (memcmp(hgn->tag.b,f+*i,hgn->tag.s) == 0) {
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

        if (index > 0) {
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
            if (nodesv[j].tag.s == endname.s && memcmp(nodesv[j].tag.b,endname.b,endname.s) == 0) {
              *i = tagend;
              hgn->insides.s = *i-hgn->insides.s;
              ret = (ret&0xffffffff)+((ulong)(lvl-nodesv[j].lvl-1)<<32);
              goto END;
            }
            if (!j || !nodesv[j].lvl)
              break;
          }
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

            if (hgn->tag.s == name.s && memcmp(hgn->tag.b,name.b,name.s) == 0) {
              *i = tagend-1;
              hgn->insides.s = *i-hgn->insides.s+1;
              hgn->all.s = (f+*i+1)-hgn->all.b;
              goto END;
            }
          }
          #endif
          *i = tagend;
          ulong rettmp = struct_handle(f,i,s,lvl+1,nodes,hg,err);
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
  } else if (!hgn->all.s) {
    hgn->all.s = f+*i-hgn->all.b;
  }

  size_t size = a->size-attrib_start;
  hgn->attribl = size;
  hgn->child_count = ret-1;
  if (hg->flags&HGREP_SAVE) {
    hgn->attrib = size ?
        memcpy(malloc(size*a->elsize),a->v+(attrib_start*a->elsize),size*a->elsize)
        : NULL;
  } else {
    hgn->attrib = a->v+(attrib_start*a->elsize);
    hgrep_pattern const *pattern = hg->pattern;
    if (pattern && hgrep_match(hgn,pattern)) {
      *err = node_output_handle(hgn,pattern->format,pattern->formatl,hg->output,hg->data);
    }
    flexarr_dec(nodes);
  }
  a->size = attrib_start;
  return ret;
}

static void
print_attribs(FILE *outfile, const hgrep_node *hgn)
{
  hgrep_cstr_pair *a = hgn->attrib;
  if (!a)
    return;
  for (ushort j = 0; j < hgn->attribl; j++) {
    fputc(' ',outfile);
    fwrite(a[j].f.b,1,a[j].f.s,outfile);
    fputs("=\"",outfile);
    fwrite(a[j].s.b,1,a[j].s.s,outfile);
    fputc('"',outfile);
  }
}

static void
print_clearinsides(FILE *outfile, const hgrep_cstr *insides)
{
  hgrep_cstr t = *insides;
  if (t.s == 0)
    return;
  void const *start,*end;
  size_t i = 0;
  while (isspace(t.b[i]) && i < t.s)
    i++;
  start = t.b+i;
  i = t.s-1;
  while (isspace(t.b[i]) && i > 0)
    i--;
  end = t.b+i;
  if (end-start+1 > 0)
    fwrite(start,1,end-start+1,outfile);
}

void
hgrep_printf(FILE *outfile, const char *format, const size_t formatl, const hgrep_node *hgn, const char *reference)
{
  size_t i = 0;
  size_t cont=0,contl=0;
  int num = -1;
  while (i < formatl) {
    if (format[i] == '\\') {
      fputc(special_character(format[++i]),outfile);
      i++;
      continue;
    }
    if (format[i] == '%') {
      if (++i >= formatl)
        break;
      if (isdigit(format[i])) {
        num = number_handle(format,&i,formatl);
      } else if (format[i] == '(') {
        cont = ++i;
        char *t = memchr(format+i,')',formatl-i);
        if (!t)
          return;
        contl = t-(format+i);
        i = t-format+1;
      }
      if (i >= formatl)
          return;

      switch (format[i++]) {
        case '%': fputc('%',outfile); break;
        case 't': fwrite(hgn->all.b,1,hgn->all.s,outfile); break;
        case 'n': fwrite(hgn->tag.b,1,hgn->tag.s,outfile); break;
        case 'i': fwrite(hgn->insides.b,1,hgn->insides.s,outfile); break;
        case 'I': print_clearinsides(outfile,&hgn->insides); break;
        case 'l': fprintf(outfile,"%u",hgn->lvl); break;
        case 's': fprintf(outfile,"%lu",hgn->all.s); break;
        case 'c': fprintf(outfile,"%u",hgn->child_count); break;
        case 'p': fprintf(outfile,"%lu",hgn->all.b-reference); break;
        case 'A': print_attribs(outfile,hgn); break;
        case 'a': {
          hgrep_cstr_pair *a = hgn->attrib;
          if (num != -1) {
            if ((size_t)num < hgn->attribl)
              fwrite(a[num].s.b,1,a[num].s.s,outfile);
          } else if (contl != 0) {
            for (size_t i = 0; i < hgn->attribl; i++)
              if (a[i].f.s == contl && memcmp(a[i].f.b,format+cont,contl) == 0)
                fwrite(a[i].s.b,1,a[i].s.s,outfile);
          } else for (size_t i = 0; i < hgn->attribl; i++) {
            fwrite(a[i].s.b,1,a[i].s.s,outfile);
            fputc('"',outfile);
          }
        }
        break;
      }
      continue;
    }
    fputc(format[i++],outfile);
  }
}

void
hgrep_print(FILE *outfile, const hgrep_node *hgn)
{
  fwrite(hgn->all.b,1,hgn->all.s,outfile);
  fputc('\n',outfile);
}

static hgrep_error *
get_pattern_fullline(char *dest, char *src, size_t *i, size_t *size, const char delim)
{
  size_t start,len;
  hgrep_error *err;
  err = get_quoted(src,i,size,delim,&start,&len);
  if (err)
    return err;

  if (len > REGEX_PATTERN_SIZE-3)
    return hgrep_set_error(1,"pattern: [%lu:%lu] is too large",start,start+len);

  dest[0] = '^';
  memcpy(dest+1,src+start,len);
  dest[len+1] = '$';
  dest[len+2] = 0;
  return NULL;
}

static hgrep_error *
get_pattern_word(char *dest, char *src, size_t *i, size_t *size, const char delim, const char eregex)
{
  size_t start,len;
  hgrep_error *err;
  err = get_quoted(src,i,size,delim,&start,&len);
  if (err)
    return err;

  if (len > REGEX_PATTERN_SIZE-(eregex ? 29 : 33))
    return hgrep_set_error(1,"pattern: [%lu:%lu] is too large",start,start+len);

  size_t t;
  if (eregex) {
    t = 14;
    memcpy(dest,"([ \\t\\n\\r].*)*",t);
  } else {
    t = 16;
    memcpy(dest,"\\([ \\t\\n\\r].*\\)*",t);
  }
  memcpy(dest+t,src+start,len);
  t += len;
  if (eregex) {
    memcpy(dest+t,"(.*[ \\t\\n\\r])*",15);
  } else {
    memcpy(dest+t,"\\(.*[ \\t\\n\\r]\\)*",17);
  }
  return NULL;
}

static hgrep_error *
format_comp(hgrep_pattern *p, char *src, size_t *pos, size_t *size)
{
  hgrep_error *err;
  if (*pos >= *size || !src || src[*pos] != '|')
    return NULL;
  (*pos)++;
  #ifndef HGREP_EDITING
  while_is(isspace,src,*pos,*size);

  if (src[*pos] != '"' && src[*pos] != '\'')
    return NULL;

  size_t start,len;
  err = get_quoted(src,pos,size,' ',&start,&len);
  if (err)
    return err;

  if (len) {
    p->format = memcpy(malloc(len),src+start,len);
    p->formatl = len;
  }
  #else

  flexarr *format = flexarr_init(sizeof(hgrep_format_func),8);
  err = format_get_funcs(format,src,pos,size);
  flexarr_conv(format,(void**)&p->format,&p->formatl);
  if (err)
    return err;
  #endif
  return NULL;
}

static hgrep_error *
match_function_handle(hgrep_pattern *p, char *src, size_t *pos, size_t *size, const int regexflags)
{
  if (*pos >= *size || !src || src[*pos] != '@')
    return NULL;
  size_t t = ++(*pos);
  hgrep_error *err;
  while (*pos < *size && (isalnum(src[*pos]) || src[*pos] == '-'))
    (*pos)++;
  if (!(*pos-t))
    return NULL;
  char found = 0;
  size_t i = 0;
  for (; i < LENGTH(match_functions); i++) {
    if (match_functions[i].name.s == *pos-t && memcmp(match_functions[i].name.b,src+t,*pos-t) == 0) {
      found = 1;
      break;
    }
  }
  if (!found)
    return hgrep_set_error(1,"function \"%.*s\" does not exists",(int)*pos-t,src+t);

  uint x;
  p->flags |= match_functions[i].flagstoset;

  while_is(isspace,src,*pos,*size);
  if (match_functions[i].flags&F_SBRACKET) {
    if (src[*pos] != '[')
      return NULL;
    if (match_functions[i].flags&F_ATTRIBUTES) {
      err = ranges_handle(src,pos,*size,&p->attribute_r,&p->attribute_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_LEVEL) {
      err = ranges_handle(src,pos,*size,&p->position_r,&p->position_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_SIZE) {
      err = ranges_handle(src,pos,*size,&p->size_r,&p->size_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_CHILD_COUNT) {
      err = ranges_handle(src,pos,*size,&p->child_count_r,&p->child_count_rl);
      if (err)
        return err;
    }
  } else if (match_functions[i].flags&F_STRING) {
    if (src[*pos] != '"' && src[*pos] != '\'')
      return NULL;
    size_t start,len;
    err = get_quoted(src,pos,size,' ',&start,&len);
    if (err)
      return err;

    if (len && match_functions[i].flags&F_MATCH_INSIDES) {
      char *tmp = src+start;
      x = tmp[len];
      tmp[len] = 0;
      int r = regcomp(&p->insides,tmp,regexflags);
      tmp[len] = x;
      if (r)
        return NULL;
    }
  }

  return NULL;
}

hgrep_error *
hgrep_pcomp(const char *pattern, size_t size, hgrep_pattern *p, const uchar flags)
{
  if (size == 0)
    return NULL;
  hgrep_error *err = NULL;
  memset(p,0,sizeof(hgrep_pattern));
  struct hgrep_pattrib pa;
  int regexflags = REG_NEWLINE|REG_NOSUB;
  if (flags&HGREP_ICASE)
      regexflags |= REG_ICASE;
  if (flags&HGREP_EREGEX)
      regexflags |= REG_EXTENDED;
  //ushort attribcount = 0;

  char regex_tmp[REGEX_PATTERN_SIZE];

  char *pat = memcpy(malloc(size),pattern,size);

  size_t pos=0;
  while_is(isspace,pat,pos,size);

  if (pat[pos] == '|') {
    err = format_comp(p,pat,&pos,&size);
    p->flags |= P_EMPTY;
    goto EXIT1;
  }

  if (pat[pos] == '!') {
      p->flags |= P_INVERT;
      pos++;
  }
  err = get_pattern_fullline(regex_tmp,pat,&pos,&size,' ');
  if (err || regcomp(&p->tag,regex_tmp,regexflags))
    goto EXIT1;

  flexarr *attrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  for (size_t i = pos; i < size;) {
    while_is(isspace,pat,i,size);
    if (i >= size)
      break;

    if (pat[i] == '@') {
      err = match_function_handle(p,pat,&i,&size,regexflags);
      if (err)
        goto EXIT1;
      continue;
    }

    if (pat[i] == '|') {
      err = format_comp(p,pat,&i,&size);
      goto CONV;
    }

    memset(&pa,0,sizeof(struct hgrep_pattrib));
    pa.flags = 0;
    char isset = 0;

    if (pat[i] == '+') {
      //attribcount++;
      pa.flags |= A_INVERT;
      isset = 1;
      i++;
    } else if (pat[i] == '-') {
      isset = -1;
      pa.flags &= ~A_INVERT;
      i++;
    } else if (pat[i] == '\\' && (pat[i+1] == '+' || pat[i+1] == '-'))
      i++;

    char shortcut = 0;
    if (i >= size)
      break;

    if (pat[i] == '.' || pat[i] == '#') {
      shortcut = pat[i++]; 
    } else if (pat[i] == '\\' && (pat[i+1] == '.' || pat[i+1] == '#'))
      i++;

    while_is(isspace,pat,i,size);
    if (i >= size)
      break;

    if (pat[i] == '[') {
      err = ranges_handle(pat,&i,size,&pa.position_r,&pa.position_rl);
      if (err)
        goto EXIT1;
    } else if (pat[i] == '\\' && pat[i+1] == '[')
      i++;

    if (i >= size)
      break;

    if (isset == 0)
      pa.flags |= A_INVERT;

    if (pat[i] == '@' || pat[i] == '|')
      continue;

    if (shortcut == '.' || shortcut == '#') {
      pa.flags |= A_VAL_MATTERS;
      if (shortcut == '.') {
        memcpy(regex_tmp,"^class$\0",8);
      } else
        memcpy(regex_tmp,"^id$\0",5);
      if (regcomp(&pa.r[0],regex_tmp,regexflags))
        goto CONV;

      err = get_pattern_word(regex_tmp,pat,&i,&size,' ',(flags&HGREP_EREGEX) ? 1 : 0);
      if (err)
        goto EXIT1;
      if (regcomp(&pa.r[1],regex_tmp,regexflags))
        goto CONV;
    } else {
      err = get_pattern_fullline(regex_tmp,pat,&i,&size,'=');
      if (err)
        goto EXIT1;
      if (regcomp(&pa.r[0],regex_tmp,regexflags))
        goto CONV;

      while_is(isspace,pat,i,size);

      if (pat[i] == '=') {
        i++;
        while_is(isspace,pat,i,size);
        if (i >= size)
          break;
    
        err = get_pattern_fullline(regex_tmp,pat,&i,&size,' ');
        if (err)
          goto EXIT1;
        if (regcomp(&pa.r[1],regex_tmp,regexflags))
          goto CONV;
        pa.flags |= A_VAL_MATTERS;
      } else {
        pa.flags &= ~A_VAL_MATTERS;
        memcpy(flexarr_inc(attrib),&pa,sizeof(struct hgrep_pattrib));
        continue;
      }
    }

    memcpy(flexarr_inc(attrib),&pa,sizeof(struct hgrep_pattrib));

    if (pat[i] != '+' && pat[i] != '-' && pat[i] != '@' && pat[i] != '|')
      i++;
  }
  /*if (p->ax < attribcount) //match patterns even if @[]a is lower then count of +tags (obsolete)
    p->ax = attribcount;*/
  CONV: ;
  size_t attribl;
  flexarr_conv(attrib,(void**)&p->attrib,&attribl);
  p->attribl = attribl;
  EXIT1: ;
  free(pat);
  return err;
}

/*void
hgrep_epattern_print(flexarr *epatterns, size_t tab)
{
  hgrep_epattern *a = (hgrep_patterns*)epatterns->v;
  for (size_t j = 0; j < tab; j++)
    fputc('\t',stderr);
  fprintf(stderr,"%% %lu",epatterns->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < epatterns->size; i++) {
    if (a[i].istable&1) {
      fprintf(stderr,"%%x %d\n",a[i].istable);
      hgrep_epattern_print((flexarr*)a[i].p,tab);
    } else {
      for (size_t j = 0; j < tab; j++)
        fputc('\t',stderr);
      fprintf(stderr,"%%j\n");
    }
  }
} //used for debugging*/

static flexarr *
hgrep_epcomp_pre(const char *csrc, size_t *pos, size_t s, const uchar flags, hgrep_error **err)
{
  if (s == 0)
    return NULL;

  size_t tpos = 0;
  if (pos == NULL)
    pos = &tpos;

  flexarr *ret = flexarr_init(sizeof(hgrep_epattern),PATTERN_SIZE_INC);
  hgrep_epattern *acurrent = (hgrep_epattern*)flexarr_inc(ret);
  acurrent->p = flexarr_init(sizeof(hgrep_epattern),PATTERN_SIZE_INC);
  acurrent->istable = EPATTERN_TABLE|EPATTERN_NEWCHAIN;
  hgrep_epattern epattern;
  char *src = memcpy(malloc(s),csrc,s);
  size_t patternl=0;
  size_t i = *pos;
  uchar next = 0;
  while_is(isspace,src,*pos,s);
  for (size_t j; i < s; i++) {
    j = i;

    while (i < s) {
      if (src[i] == '\\' && src[i+1] == '\\') {
        i += 2;
        continue;
      }
      if (src[i] == '\\' && (src[i+1] == ',' || src[i+1] == ';' || src[i+1] == '"' || src[i+1] == '\'' || src[i+1] == '{' || src[i+1] == '}')) {
        delchar(src,i++,&s);
        patternl = i-j;
        continue;
      }
      if (src[i] == '"' || src[i] == '\'') {
        char tf = src[i];
        i++;
        while (i < s && src[i] != tf) {
          if (src[i] == '\\' && (src[i+1] == '\\' || src[i+1] == tf))
            i++;
          i++;
        }
        if (src[i] == tf)
          i++;
      }
      if (src[i] == '[') {
        i++;
        while (i < s && src[i] != ']')
          i++;
        if (src[i] == ']')
          i++;
      }

      if (src[i] == ',') {
        next = 1;
        patternl = i-j;
        i++;
        break;
      }
      if (src[i] == ';') {
        patternl = i-j;
        i++;
        break;
      }
      if (src[i] == '{') {
        i++;
        next = 2;
        break;
      }
      if (src[i] == '}') {
        i++;
        next = 3;
        break;
      }
      i++;
      patternl = i-j;
    }

    if (j+patternl > s)
      patternl = s-j;

    hgrep_epattern *new = NULL;
    if ((next != 3 || src[j] != '}') && (next >= 2 || patternl)) {
      epattern.p = malloc(sizeof(hgrep_pattern));
      if (patternl && (next != 2 || next < 2)) {
        *err = hgrep_pcomp(src+j,patternl,(hgrep_pattern*)epattern.p,flags);
        if (*err)
          goto EXIT1;
      }

      new = (hgrep_epattern*)flexarr_inc(acurrent->p);
      new->p = epattern.p;
    }

    if (next == 2) {
      next = 0;
      new->istable = EPATTERN_TABLE|EPATTERN_NEWBLOCK;
      *pos = i;
      new->p = hgrep_epcomp_pre(src,pos,s,flags,err);
      if (*err)
        goto EXIT1;
      i = *pos;
      while_is(isspace,src,i,s);
      if (i < s) {
        if (src[i] == ',') {
          i++;
          goto NEXT_NODE;
        } else if (src[i] == '}') {
          i++;
          goto END_BRACKET;
        } else if (src[i] == ';')
          i++;
      }
      //hgrep_epattern_print(new->p,0);
    } else if (new)
      new->istable = 0;

    if (next == 1) {
      NEXT_NODE: ;
      next = 0;
      acurrent = (hgrep_epattern*)flexarr_inc(ret);
      acurrent->p = flexarr_init(sizeof(hgrep_epattern),PATTERN_SIZE_INC);
      acurrent->istable = EPATTERN_TABLE|EPATTERN_NEWCHAIN;
    }
    if (next == 3)
      goto END_BRACKET;

    while_is(isspace,src,i,s);
    i--;
  }

  END_BRACKET:
  *pos = i;
  flexarr_clearb(ret);
  EXIT1:
  free(src);
  if (*err) {
    flexarr_free(ret);
    return NULL;
  }
  return ret;
}

hgrep_error *
hgrep_epcomp(const char *src, size_t size, hgrep_epattern **epatterns, size_t *epatternsl, const uchar flags)
{
  hgrep_error *err = NULL;
  flexarr *ret = hgrep_epcomp_pre(src,NULL,size,flags,&err);
  if (ret) {
    flexarr_conv(ret,(void**)epatterns,epatternsl);
  } else
    *epatternsl = 0;
  return err;
}

static void
first_match(hgrep *hg, hgrep_pattern *pattern, flexarr *dest)
{
  hgrep_node *nodes = hg->nodes;
  size_t nodesl = hg->nodesl;
  for (size_t i = 0; i < nodesl; i++) {
    //print_or_add(i,pattern);
    if (hgrep_match(&nodes[i],pattern)) {
      hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
      //x->pattern = pattern;
      x->lvl = 0;
      x->id = i;
      //*(size_t*)flexarr_inc(dest) = i;
    }
  }
}


static void
pattern_exec(hgrep *hg, hgrep_pattern *pattern, flexarr *source, flexarr *dest)
{
  if (source->size == 0) {
    first_match(hg,pattern,dest);
    return;
  }
  
  ushort lvl;
  hgrep_node *nodes = hg->nodes;
  size_t current,n;
  for (size_t i = 0; i < source->size; i++) {
    current = ((hgrep_compressed*)source->v)[i].id;
    lvl = nodes[current].lvl;
    for (size_t j = 0; j <= nodes[current].child_count; j++) {
      n = current+j;
      nodes[n].lvl -= lvl;
      if (hgrep_match(&nodes[n],pattern)) {
        hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
        //x->pattern = pattern;
        x->lvl = lvl;
        x->id = n;
      }
      nodes[n].lvl += lvl;
    }
  }
}

static hgrep_error *
hgrep_ematch_pre(hgrep *hg, const hgrep_epattern *patterns, const size_t patternsl, flexarr *source, flexarr *dest, flexarr *pcollector)
{
  flexarr *buf[3];
  hgrep_error *err;

  buf[0] = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  if (source) {
    buf[0]->asize = source->asize;
    buf[0]->size = source->size;
    buf[0]->v = memcpy(malloc(source->asize*sizeof(hgrep_compressed)),source->v,source->asize*sizeof(hgrep_compressed));
  }

  buf[1] = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  buf[2] = dest ? dest : flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  flexarr *buf_unchanged[2];
  buf_unchanged[0] = buf[0];
  buf_unchanged[1] = buf[1];

  size_t startp = pcollector->size;
  size_t lastp = startp;

  hgrep_pattern *lastformat = NULL;

  for (size_t i = 0; i < patternsl; i++) {
    //flexarr *out = buf[1];
    //flexarr *out = (source == NULL || (dest == NULL && i == size-1)) ? NULL : buf[1];
    if (patterns[i].istable&EPATTERN_TABLE) {
      /*flexarr *copy = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
      copy->asize = buf[0]->asize;
      copy->size = buf[0]->size;
      copy->v = memcpy(malloc(buf[0]->asize*sizeof(hgrep_compressed)),buf[0]->v,buf[0]->asize*sizeof(hgrep_compressed));*/
      lastp = pcollector->size;
      err = hgrep_ematch_pre(hg,(hgrep_epattern*)((flexarr*)patterns[i].p)->v,((flexarr*)patterns[i].p)->size,buf[0],buf[1],pcollector);
      if (err)
        goto ERR;
      //fprintf(stderr,"pase %p %p %lu\n",copy,buf[1],lvl);
      //flexarr_free(copy);
    } else if (patterns[i].p) {
      lastformat = (hgrep_pattern*)patterns[i].p;
      pattern_exec(hg,(hgrep_pattern*)patterns[i].p,buf[0],buf[1]);
    }

    if ((patterns[i].istable&EPATTERN_TABLE && !(patterns[i].istable&EPATTERN_NEWBLOCK)) || i == patternsl-1) {
      if (buf[1]->size) {
        size_t prevsize = buf[2]->size;
        flexarr_add(buf[2],buf[1]);
        if (lastformat) {
          if (patterns[i].istable) {
            if (startp != lastp) { //truncate previously added, now useless pcollector
                for (size_t c = lastp; c < pcollector->size; c++)
                    ((hgrep_cstr*)pcollector->v)[c-lastp] = ((hgrep_cstr*)pcollector->v)[c];
                pcollector->size -= lastp-startp;
            }
          } else {
            pcollector->size = startp;
            *(hgrep_cstr*)flexarr_inc(pcollector) = (hgrep_cstr){(char*)lastformat,buf[2]->size-prevsize};
          }
        }
      }
      buf[1]->size = 0;
      continue;
    }
    if (!buf[1]->size)
      break;

    buf[0]->size = 0;
    flexarr *tmp = buf[0];
    buf[0] = buf[1];
    buf[1] = tmp;
  }

  if (!dest) {
    hgrep_cstr *pcol = (hgrep_cstr*)pcollector->v;
    //ONLY NEEDED FOR DEBUGGING PURPOSES
    size_t pcollector_sum = 0;
    for (size_t j = 0; j < pcollector->size; j++)
      pcollector_sum += pcol[j].s;
    if (pcollector_sum != buf[2]->size) {
      err = hgrep_set_error(1,"pcollector does not match count of found tags, %lu != %lu",pcollector_sum,buf[2]->size);
      goto ERR;
    }
    //ONLY NEEDED FOR DEBUGGING PURPOSES


    if (pcollector->size)
    for (size_t j = 0, pcurrent = 0, g = 0; j < buf[2]->size; j++, g++) {
      if (pcol[pcurrent].s == g) {
        g = 0;
        pcurrent++;
        /*if (pcurrent >= pcollector->size)
            break;*/
      }
      hgrep_compressed *x = &((hgrep_compressed*)buf[2]->v)[j];
      hg->nodes[x->id].lvl -= x->lvl;
      err = node_output_handle(&hg->nodes[x->id],((hgrep_pattern*)pcol[pcurrent].b)->format,((hgrep_pattern*)pcol[pcurrent].b)->formatl,hg->output,hg->data);
      if (err)
        goto ERR;

      hg->nodes[x->id].lvl += x->lvl;
    }
    flexarr_free(buf[2]);
  }

  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);

  return NULL;

  ERR: ;
  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);
  flexarr_free(buf[2]);
  return err;
}

hgrep_error *
hgrep_ematch(hgrep *hg, const hgrep_epattern *patterns, const size_t patternsl, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl)
{
    flexarr *fsource = NULL;
    if (source) {
      fsource = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
      fsource->v = source;
      fsource->size = sourcel;
      fsource->asize = sourcel;
    }
    flexarr *fdest = NULL;
    if (dest) {
      fdest = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
      fsource->v = dest;
      fsource->size = destl;
      fsource->asize = destl;
    }
    hgrep_error *err;
    flexarr *pcollector = flexarr_init(sizeof(hgrep_cstr),32);
    err = hgrep_ematch_pre(hg,patterns,patternsl,fsource,fdest,pcollector);
    flexarr_free(pcollector);
    return err;
}

void
hgrep_pfree(hgrep_pattern *p)
{
  if (p == NULL)
    return;
  if (!(p->flags&P_EMPTY)) {
    regfree(&p->tag);
    if (p->flags&P_MATCH_INSIDES)
      regfree(&p->insides);
    for (size_t j = 0; j < p->attribl; j++) {
      regfree(&p->attrib[j].r[0]);
      if (p->attrib[j].flags&A_VAL_MATTERS)
        regfree(&p->attrib[j].r[1]);
      if (p->attrib[j].position_rl)
        free(p->attrib[j].position_r);
    }
    free(p->attrib);
    if (p->position_rl)
      free(p->position_r);
    if (p->attribute_rl)
      free(p->attribute_r);
    if (p->size_rl)
      free(p->size_r);
    if (p->child_count_rl)
      free(p->child_count_r);
  }
  #ifdef HGREP_EDITING
  format_free(p->format,p->formatl);
  #else
  if (p->format)
    free(p->format);
  #endif
}

static void
hgrep_epattern_free_pre(flexarr *epatterns)
{
  hgrep_epattern *a = (hgrep_epattern*)epatterns->v;
  for (size_t i = 0; i < epatterns->size; i++) {
    if (a[i].istable&EPATTERN_TABLE) {
      hgrep_epattern_free_pre((flexarr*)a[i].p);
    } else {
      hgrep_pfree((hgrep_pattern*)a[i].p);
    }
  }
  flexarr_free(epatterns);
}

void
hgrep_epattern_free(hgrep_epattern *epatterns, const size_t epatternsl)
{
  for (size_t i = 0; i < epatternsl; i++) {
    if (epatterns[i].istable&EPATTERN_TABLE) {
      hgrep_epattern_free_pre((flexarr*)epatterns[i].p);
    } else {
      hgrep_pfree((hgrep_pattern*)epatterns[i].p);
    }
  }
  free(epatterns);
}

void
hgrep_free(hgrep *hg)
{
    if (hg == NULL)
      return;
    for (size_t i = 0; i < hg->nodesl; i++)
      free(hg->nodes[i].attrib);
    if (hg->nodesl)
      free(hg->nodes);
}

static hgrep_error *
hgrep_analyze(const char *ptr, const size_t size, flexarr *nodes, hgrep *hg)
{
  hgrep_error *err;
  for (size_t i = 0; i < size; i++) {
    while (i < size && ptr[i] != '<')
        i++;
    while (i < size && ptr[i] == '<') {
      struct_handle(ptr,&i,size,0,nodes,hg,&err);
      if (err)
        return err;
    }
  }
  return NULL;
}

hgrep_error *
hgrep_fmatch(const char *ptr, const size_t size, FILE *output, const hgrep_pattern *pattern)
{
  hgrep t;
  t.data = ptr;
  t.size = size;
  t.pattern = pattern;
  t.flags = 0;
  if (output == NULL)
    output = stdout;
  t.output = output;
  flexarr *nodes = flexarr_init(sizeof(hgrep_node),HGREP_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_cstr_pair),ATTRIB_INC);
  hgrep_error *err = hgrep_analyze(ptr,size,nodes,&t);
  if (err)
    return err;
  flexarr_free(nodes);
  t.nodes = NULL;
  t.nodesl = 0;
  flexarr_free((flexarr*)t.attrib_buffer);
  return NULL;
}

hgrep_error *
hgrep_efmatch(char *ptr, size_t size, FILE *output, const hgrep_epattern *epatterns, const size_t epatternsl, int (*freeptr)(void *ptr, size_t size))
{
  if (epatternsl == 0)
    return NULL;
  if (epatternsl > 1)
    return hgrep_set_error(1,"fast mode cannot run in non linear mode");

  FILE *t = output;
  char *nptr;
  size_t fsize;

  flexarr *first = (flexarr*)epatterns[0].p;
  for (size_t i = 0; i < first->size; i++) {
    output = (i == first->size-1) ? t : open_memstream(&nptr,&fsize);

    if (((hgrep_epattern*)first->v)[i].istable&1)
      return hgrep_set_error(1,"fast mode cannot run in non linear mode");
    hgrep_error *err = hgrep_fmatch(ptr,size,output,(hgrep_pattern*)((hgrep_epattern*)first->v)[i].p);
    fflush(output);

    if (i == 0) {
      (*freeptr)(ptr,size);
    } else
      free(ptr);
    
    if (i != first->size-1)
      fclose(output);

    if (err)
      return err;

    ptr = nptr;
    size = fsize;
  }
  return NULL;
}


hgrep
hgrep_init(const char *ptr, const size_t size, FILE *output)
{
  hgrep t;
  t.data = ptr;
  t.size = size;
  t.pattern = NULL;
  t.flags = HGREP_SAVE;
  if (output == NULL)
    output = stdout;
  t.output = output;
  flexarr *nodes = flexarr_init(sizeof(hgrep_node),HGREP_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_cstr_pair),ATTRIB_INC);
  hgrep_analyze(ptr,size,nodes,&t);
  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  flexarr_free((flexarr*)t.attrib_buffer);
  return t;
}
