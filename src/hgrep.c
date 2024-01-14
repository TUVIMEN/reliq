/*
    hgrep - simple html searching tool
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
#include <limits.h>
#include <stdarg.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "hgrep.h"
#include "flexarr.h"
#include "ctype.h"

#define HGREP_SAVE 0x8

#define PASSED_INC (1<<14)
#define PATTERN_SIZE (1<<9)
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

//hgrep_range flags
#define R_RANGE 0x8
#define R_NOTEMPTY 0x10

//hgrep_epattern flags
#define EPATTERN_TABLE 0x1
#define EPATTERN_NEWBLOCK 0x2
#define EPATTERN_NEWCHAIN 0x4

//hgrep_format_func flags
#define FORMAT_FUNC 0xf
#define FORMAT_ARG0_ISSTR   0x10
#define FORMAT_ARG1_ISSTR   0x20
#define FORMAT_ARG2_ISSTR   0x40
#define FORMAT_ARG3_ISSTR   0x80

#define ATTRIB_INC (1<<3)
#define HGREP_NODES_INC (1<<13)
#define RANGES_INC (1<<4)

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGTH(x) (sizeof(x)/(sizeof(*x)))

typedef struct {
  char *b;
  uchar s;
} str8;

struct hgrep_match_function {
  str8 name;
  ushort flags;
  uchar flagstoset;
};

#ifdef HGREP_EDITING
struct hgrep_format_function {
  str8 name;
  int (*func)(char*,size_t,FILE*,void*[4],unsigned char);
};

//int htmldecode_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag);
static int trim_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag);
static int tr_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag);
static int cut_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag);

const struct hgrep_format_function format_functions[] = {
    //{{"htmldecode",10},htmldecode_edit},
    {{"trim",4},trim_edit},
    {{"tr",2},tr_edit},
    {{"cut",3},cut_edit},
    {{"ed",2},NULL},
    {{"match",5},NULL},
    {{"error",5},NULL},
};
#endif

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

const str8 selfclosing_s[] = { //tags that doesn't end with </tag>
  {"br",2},{"hr",2},{"img",3},{"input",5},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"link",4},{"meta",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

const str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef HGREP_AUTOCLOSING
const str8 autoclosing_s[] = { //tags that doesn't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},
  {"tfoot",5},{"thead",5},{"rt",2},{"rp",2},
  {"caption",7},{"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

static void
hgrep_error(const int ret, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  vfprintf(stderr,fmt,ap);
  fputc('\n',stderr);
  va_end(ap);
  exit(ret);
}

static uchar
ranges_match(const uint matched, const struct hgrep_range *ranges, const size_t rangesl, const size_t last)
{
  if (!rangesl)
    return 1;
  struct hgrep_range const *r;
  uint x,y;
  for (size_t i = 0; i < rangesl; i++) {
    r = &ranges[i];
    x = r->v[0];
    y = r->v[1];
    if (!(r->flags&8)) {
      if (r->flags&1)
        x = ((uint)last < r->v[0]) ? 0 : last-r->v[0];
      if (matched == x)
        return 1;
    } else {
      if (r->flags&1)
        x = ((uint)last < r->v[0]) ? 0 : last-r->v[0];
      if (r->flags&2) {
        if ((uint)last < r->v[1])
          continue;
        y = last-r->v[1];
      }
      if (matched >= x && matched <= y)
        if (r->v[2] < 2 || matched%r->v[2] == 0)
          return 1;
    }
  }
  return 0;
}

static char *
delchar(char *src, const size_t pos, size_t *size)
{
  size_t s = *size-1;
  for (size_t i = pos; i < s; i++)
    src[i] = src[i+1];
  src[s] = 0;
  *size = s;
  return src;
}

int
get_dec(const char *src, size_t size, size_t *traversed)
{
    size_t pos=0;
    int r = 0;
    while (pos < size && isdigit(src[pos]))
        r = (r*10)+(src[pos++]-48);
    *traversed = pos;
    return r;
}

int number_handle(const char *src, size_t *pos, const size_t size)
{
  size_t s;
  int ret = get_dec(src+*pos,size-*pos,&s);
  if (s == 0)
    return -1;
  *pos += s;
  return ret;
}

static void
comment_handle(char *f, size_t *i, size_t s)
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
name_handle(char *f, size_t *i, const size_t s, hgrep_str *tag)
{
    tag->b = f+*i;
    while (*i < s && (isalnum(f[*i]) || f[*i] == '-' || f[*i] == '_' || f[*i] == ':'))
      (*i)++;
    tag->s = (f+*i)-tag->b;
}

static void
attrib_handle(char *f, size_t *i, const size_t s, flexarr *attribs)
{
  hgrep_str_pair *ac = (hgrep_str_pair*)flexarr_inc(attribs);
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

#ifdef HGREP_EDITING
static void
format_exec(FILE *output, const hgrep_node *hgn, hgrep_format_func *format, size_t formatl, const char *reference)
{
  if (!format || (formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && (!format[0].arg[0] || !((hgrep_str*)format[0].arg[0])->b))) {
    hgrep_print(output,hgn);
    return;
  }
  if (formatl == 1 && (format[0].flags&FORMAT_FUNC) == 0 && format[0].arg[0] && ((hgrep_str*)format[0].arg[0])->b) {
    hgrep_printf(output,((hgrep_str*)format[0].arg[0])->b,((hgrep_str*)format[0].arg[0])->s,hgn,reference);
    return;
  }

  FILE *out;
  char *ptr[2];
  ptr[1] = NULL;
  size_t fsize[2];

  for (size_t i = 0; i < formatl; i++) {
    out = (i == formatl-1) ? output : open_memstream(&ptr[1],&fsize[1]);
    if (i == 0 && (format[i].flags&FORMAT_FUNC) == 0) {
        hgrep_printf(out,((hgrep_str*)format[i].arg[0])->b,((hgrep_str*)format[i].arg[0])->s,hgn,reference);
    } else {
      if (i == 0) {
        FILE *t = open_memstream(&ptr[0],&fsize[0]);
        hgrep_print(t,hgn);
        fclose(t);
      }
      format_functions[(format[i].flags&FORMAT_FUNC)-1].func(ptr[0],fsize[0],out,format[i].arg,format[i].flags);
    }

    if (i != formatl-1)
      fclose(out);
    if (i != 0 || (format[i].flags&FORMAT_FUNC) != 0)
      free(ptr[0]);
    ptr[0] = ptr[1];
    fsize[0] = fsize[1];
  }

  /*char *ptr;
  size_t fsize;
    outfile = (i == first->size-1) ? t : open_memstream(&ptr,&fsize);
    fflush(outfile);
      fclose(outfile);*/
}
#endif

#ifdef HGREP_PHPTAGS
static void
phptag_handle(char *f, size_t *i, const size_t s, hgrep_node *hgn)
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
  const hgrep_str_pair *a = hgn->attrib;
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
struct_handle(char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, hgrep *hg)
{
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
          hgrep_str endname;
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
            hgrep_str name;

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
          ulong rettmp = struct_handle(f,i,s,lvl+1,nodes,hg);
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
        memcpy(malloc(size*a->nmemb),a->v+(attrib_start*a->nmemb),size*a->nmemb)
        : NULL;
  } else {
    hgn->attrib = a->v+(attrib_start*a->nmemb);
    hgrep_pattern *pattern = hg->pattern;
    if (pattern && hgrep_match(hgn,pattern)) {
      #ifdef HGREP_EDITING
      format_exec(hg->output,hgn,pattern->format,pattern->formatl,hg->data);
      #else
      if (pattern->format.b)
        hgrep_printf(hg->output,pattern->format.b,pattern->format.s,hgn,hg->data);
      else
        hgrep_print(hg->output,hgn);
      #endif
    }
    flexarr_dec(nodes);
  }
  a->size = attrib_start;
  return ret;
}

static char
special_character(const char c)
{
  char r;
  switch (c) {
    case '0': r = '\0'; break;
    case 'a': r = '\a'; break;
    case 'b': r = '\b'; break;
    case 't': r = '\t'; break;
    case 'n': r = '\n'; break;
    case 'v': r = '\v'; break;
    case 'f': r = '\f'; break;
    case 'r': r = '\r'; break;
    default: r = c;
  }
  return r;
}

static void
print_attribs(FILE *outfile, const hgrep_node *hgn)
{
  hgrep_str_pair *a = hgn->attrib;
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
print_clearinsides(FILE *outfile, const hgrep_str *insides)
{
  hgrep_str t = *insides;
  if (t.s == 0)
    return;
  void *start,*end;
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
          hgrep_str_pair *a = hgn->attrib;
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

static void
range_handle(const char *src, const size_t size, struct hgrep_range *range)
{
  memset(range->v,0,sizeof(struct hgrep_range));
  size_t pos = 0;

  for (int i = 0; i < 3; i++) {
    while_is(isspace,src,pos,size);
    if (i == 1)
      range->flags |= R_RANGE; //is a range
    if (src[pos] == '-') {
      pos++;
      while_is(isspace,src,pos,size);
      range->flags |= 1<<i; //starts from the end
      range->flags |= R_NOTEMPTY; //not empty
    }
    if (isdigit(src[pos])) {
      range->v[i] = number_handle(src,&pos,size);
      while_is(isspace,src,pos,size);
      range->flags |= R_NOTEMPTY; //not empty
    } else if (i == 1)
      range->flags |= 1<<i;

    if (pos >= size || src[pos] != ':')
      break;
    pos++;
  }
}

static void
ranges_handle(const char *src, size_t *pos, const size_t size, struct hgrep_range **ranges, size_t *rangesl) {
  if (src[*pos] != '[')
    return;
  (*pos)++;
  size_t end;
  struct hgrep_range range, *new_ptr;
  *rangesl = 0;
  *ranges = NULL;
  size_t rangesl_buffer = 0;

  while (*pos < size && src[*pos] != ']') {
    while_is(isspace,src,*pos,size);
    end = *pos;
    while (end < size && (isspace(src[end]) || isdigit(src[end]) || src[end] == ':' || src[end] == '-') && src[end] != ',')
      end++;
    if (src[end] != ',' && src[end] != ']')
      hgrep_error(1,"range: char %u(%c): not a number",end,src[end]);

    range_handle(src+(*pos),end-(*pos),&range);
    if (range.flags&(R_RANGE|R_NOTEMPTY)) {
      (*rangesl)++;
      if (*rangesl > rangesl_buffer)
        rangesl_buffer += RANGES_INC;
      if ((new_ptr = realloc(*ranges,rangesl_buffer*sizeof(struct hgrep_range))) == NULL) {
        if (*rangesl > 0)
          free(*ranges);
        *ranges = NULL;
        *rangesl = 0;
        return;
      }
      *ranges = new_ptr;
      memcpy(&(*ranges)[*rangesl-1],&range,sizeof(struct hgrep_range));
    }
    *pos = end+((src[end] == ',') ? 1 : 0);
  }
  if (src[*pos] != ']')
    hgrep_error(1,"range: char %d: unprecedented end of range",*pos);
  (*pos)++;

  if (*rangesl != rangesl_buffer) {
    if ((new_ptr = realloc(*ranges,(*rangesl)*sizeof(struct hgrep_range))) == NULL) {
      if (*rangesl > 0)
        free(*ranges);
      *ranges = NULL;
      *rangesl = 0;
      return;
    }
    *ranges = new_ptr;
  }
}

static void
get_pattern(char *src, size_t *i, size_t *size, const char delim, size_t *start, size_t *len) {
  *len = 0;
  if (src[*i] == '"' || src[*i] == '\'') {
    char tf = src[*i];
    *start = ++(*i);
    while (*i < *size && src[*i] != tf) {
      if (src[*i] == '\\' && src[*i+1] == '\\') {
        (*i)++;
      } else if (src[*i] == '\\' && src[*i+1] == tf)
        delchar(src,*i,size);
      (*i)++;
    }
    if (src[*i] != tf)
      hgrep_error(1,"pattern: could not find the end of %c quote [%lu:]",tf,*start);
    *len = ((*i)++)-(*start);
  } else {
    *start = *i;
    while (*i < *size && !isspace(src[*i]) && src[*i] != delim) {
      if (src[*i] == '\\' && src[*i+1] == '\\') {
        (*i)++;
      } else if (src[*i] == '\\' && (isspace(src[*i+1]) || src[*i+1] == delim)) {
        delchar(src,*i,size);
      }
      (*i)++;
    }
    *len = *i-*start;
  }
}

static void
get_pattern_fullline(char *dest, char *src, size_t *i, size_t *size, const char delim)
{
  size_t start,len;
  get_pattern(src,i,size,delim,&start,&len);

  if (len > PATTERN_SIZE-3)
    hgrep_error(1,"pattern: [%lu:%lu] is too large",start,start+len);

  dest[0] = '^';
  memcpy(dest+1,src+start,len);
  dest[len+1] = '$';
  dest[len+2] = 0;
}

static void
get_pattern_word(char *dest, char *src, size_t *i, size_t *size, const char delim, const char eregex)
{
  size_t start,len;
  get_pattern(src,i,size,delim,&start,&len);

  if (len > PATTERN_SIZE-(eregex ? 29 : 33))
    hgrep_error(1,"pattern: [%lu:%lu] is too large",start,start+len);

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
}

#ifdef HGREP_EDITING
static void
format_get_func_args(hgrep_format_func *f, char *src, size_t *pos, size_t *size)
{
  for (size_t i = 0; *pos < *size; i++) {
    if (i >= 4)
      hgrep_error(1,"too many arguments passed to a function");

    if (src[*pos] == '"' || src[*pos] == '\'') {
      size_t start,len;
      get_pattern(src,pos,size,' ',&start,&len);

      if (len) {
        hgrep_str *str = f->arg[i] = malloc(sizeof(hgrep_str));
        str->b = memcpy(malloc(len),src+start,len);
        str->s = len;
        f->flags |= (FORMAT_ARG0_ISSTR<<i);
      }
    } else if (src[*pos] == '[') {
      hgrep_str *str = f->arg[i] = malloc(sizeof(hgrep_str));
      ranges_handle(src,pos,*size,(struct hgrep_range**)&str->b,&str->s);
    }
    if (*pos >= *size)
      return;
    if (src[*pos] != '|') {
      if (src[*pos] == ' ')
        return;
      hgrep_error(1,"bad argument %x",src[*pos]);
    }
    (*pos)++;
  }
}



static void
format_get_funcs(flexarr *format, char *src, size_t *pos, size_t *size)
{
  hgrep_format_func *f;
  char *fname;
  size_t fnamel;

  while (*pos < *size) {
    while_is(isspace,src,*pos,*size);
    if (*pos >= *size)
      break;

    if (isalnum(src[*pos])) {
      fname = src+*pos;
      while_is(isalnum,src,*pos,*size);
      fnamel = *pos-(fname-src);
    } else
      fname = NULL;

    f = (hgrep_format_func*)flexarr_inc(format);
    memset(f,0,sizeof(hgrep_format_func));

    format_get_func_args(f,src,pos,size);

    if (fname) {
      char found = 0;
      size_t i = 0;
      for (; i < LENGTH(format_functions); i++) {
        if (format_functions[i].name.s == fnamel && memcmp(format_functions[i].name.b,fname,fnamel) == 0) {
          found = 1;
          break;
        }
      }
      if (!found)
        hgrep_error(1,"function does not exist: \"%.*s\"",fnamel,fname);
      f->flags |= i+1;
    } else if (format->size > 1)
      hgrep_error(1,"printf defined two times %u",format->size);
    (*pos)++;
  }
}
#endif

static void
format_comp(hgrep_pattern *p, char *src, size_t *pos, size_t *size)
{
  #ifndef HGREP_EDITING
  if (*pos >= *size || !src || src[*pos] != '|')
    return;
  (*pos)++;
  while_is(isspace,src,*pos,*size);

  if (src[*pos] != '"' && src[*pos] != '\'')
    return;

  size_t start,len;
  get_pattern(src,pos,size,' ',&start,&len);

  if (len) {
    p->format.b = memcpy(malloc(len),src+start,len);
    p->format.s = len;
  }
  #else
  if (*pos >= *size || !src || src[*pos] != '|')
    return;
  (*pos)++;


  flexarr *format = flexarr_init(sizeof(hgrep_format_func),8);
  format_get_funcs(format,src,pos,size);
  flexarr_conv(format,(void**)&p->format,&p->formatl);
  #endif
}

static void
match_function_handle(hgrep_pattern *p, char *src, size_t *pos, size_t *size, const int regexflags)
{
  if (*pos >= *size || !src || src[*pos] != '@')
    return;
  size_t t = ++(*pos);
  while (*pos < *size && (isalnum(src[*pos]) || src[*pos] == '-'))
    (*pos)++;
  if (!(*pos-t))
    return;
  char found = 0;
  size_t i = 0;
  for (; i < LENGTH(match_functions); i++) {
    if (match_functions[i].name.s == *pos-t && memcmp(match_functions[i].name.b,src+t,*pos-t) == 0) {
      found = 1;
      break;
    }
  }
  if (!found)
    return;

  uint x;
  p->flags |= match_functions[i].flagstoset;

  while_is(isspace,src,*pos,*size);
  if (match_functions[i].flags&F_SBRACKET) {
    if (src[*pos] != '[')
      return;
    if (match_functions[i].flags&F_ATTRIBUTES) {
      ranges_handle(src,pos,*size,&p->attribute_r,&p->attribute_rl);
    } else if (match_functions[i].flags&F_LEVEL) {
      ranges_handle(src,pos,*size,&p->position_r,&p->position_rl);
    } else if (match_functions[i].flags&F_SIZE) {
      ranges_handle(src,pos,*size,&p->size_r,&p->size_rl);
    } else if (match_functions[i].flags&F_CHILD_COUNT)
      ranges_handle(src,pos,*size,&p->child_count_r,&p->child_count_rl);
  } else if (match_functions[i].flags&F_STRING) {
    if (src[*pos] != '"' && src[*pos] != '\'')
      return;
    size_t start,len;
    get_pattern(src,pos,size,' ',&start,&len);

    if (len && match_functions[i].flags&F_MATCH_INSIDES) {
      char *tmp = src+start;
      x = tmp[len];
      tmp[len] = 0;
      int r = regcomp(&p->insides,tmp,regexflags);
      tmp[len] = x;
      if (r)
        return;
    }
  }

  return;
}

void
hgrep_pcomp(char *pattern, size_t size, hgrep_pattern *p, const uchar flags)
{
  if (size == 0)
    return;
  memset(p,0,sizeof(hgrep_pattern));
  struct hgrep_pattrib pa;
  int regexflags = REG_NEWLINE|REG_NOSUB;
  if (flags&HGREP_ICASE)
      regexflags |= REG_ICASE;
  if (flags&HGREP_EREGEX)
      regexflags |= REG_EXTENDED;
  //ushort attribcount = 0;

  char regex_tmp[PATTERN_SIZE];

  pattern = memcpy(malloc(size),pattern,size);

  size_t pos=0;
  while_is(isspace,pattern,pos,size);

  if (pattern[pos] == '|') {
    format_comp(p,pattern,&pos,&size);
    p->flags |= P_EMPTY;
    free(pattern);
    return;
  }

  if (pattern[pos] == '!') {
      p->flags |= P_INVERT;
      pos++;
  }
  get_pattern_fullline(regex_tmp,pattern,&pos,&size,' ');
  if (regcomp(&p->tag,regex_tmp,regexflags)) {
    free(pattern);
    return;
  }

  flexarr *attrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  for (size_t i = pos; i < size;) {
    while_is(isspace,pattern,i,size);
    if (i >= size)
      break;

    if (pattern[i] == '@') {
      match_function_handle(p,pattern,&i,&size,regexflags);
      continue;
    }

    if (pattern[i] == '|') {
      format_comp(p,pattern,&i,&size);
      goto CONV;
    }

    memset(&pa,0,sizeof(struct hgrep_pattrib));
    pa.flags = 0;
    char isset = 0;

    if (pattern[i] == '+') {
      //attribcount++;
      pa.flags |= A_INVERT;
      isset = 1;
      i++;
    } else if (pattern[i] == '-') {
      isset = -1;
      pa.flags &= ~A_INVERT;
      i++;
    } else if (pattern[i] == '\\' && (pattern[i+1] == '+' || pattern[i+1] == '-'))
      i++;

    char shortcut = 0;
    if (i >= size)
      break;

    if (pattern[i] == '.' || pattern[i] == '#') {
      shortcut = pattern[i++]; 
    } else if (pattern[i] == '\\' && (pattern[i+1] == '.' || pattern[i+1] == '#'))
      i++;

    while_is(isspace,pattern,i,size);
    if (i >= size)
      break;

    if (pattern[i] == '[') {
      ranges_handle(pattern,&i,size,&pa.position_r,&pa.position_rl);
    } else if (pattern[i] == '\\' && pattern[i+1] == '[')
      i++;

    if (i >= size)
      break;

    if (isset == 0)
      pa.flags |= A_INVERT;

    if (pattern[i] == '@' || pattern[i] == '|')
      continue;

    if (shortcut == '.' || shortcut == '#') {
      pa.flags |= A_VAL_MATTERS;
      if (shortcut == '.') {
        memcpy(regex_tmp,"^class$\0",8);
      } else
        memcpy(regex_tmp,"^id$\0",5);
      if (regcomp(&pa.r[0],regex_tmp,regexflags))
        goto CONV;

      get_pattern_word(regex_tmp,pattern,&i,&size,' ',(flags&HGREP_EREGEX) ? 1 : 0);
      if (regcomp(&pa.r[1],regex_tmp,regexflags))
        goto CONV;
    } else {
      get_pattern_fullline(regex_tmp,pattern,&i,&size,'=');
      if (regcomp(&pa.r[0],regex_tmp,regexflags))
        goto CONV;

      while_is(isspace,pattern,i,size);

      if (pattern[i] == '=') {
        i++;
        while_is(isspace,pattern,i,size);
        if (i >= size)
          break;
    
        get_pattern_fullline(regex_tmp,pattern,&i,&size,' ');
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

    if (pattern[i] != '+' && pattern[i] != '-' && pattern[i] != '@' && pattern[i] != '|')
      i++;
  }
  /*if (p->ax < attribcount) //match patterns even if @[]a is lower then count of +tags (obsolete)
    p->ax = attribcount;*/
  CONV: ;
  size_t attribl;
  flexarr_conv(attrib,(void**)&p->attrib,&attribl);
  p->attribl = attribl;
  free(pattern);
  return;
}

static flexarr *
hgrep_epcomp_pre(char *src, size_t *pos, size_t s, const uchar flags)
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
  src = memcpy(malloc(s),src,s);
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
      if (patternl && (next != 2 || next < 2))
        hgrep_pcomp(src+j,patternl,(hgrep_pattern*)epattern.p,flags);

      new = (hgrep_epattern*)flexarr_inc(acurrent->p);
      new->p = epattern.p;
    }

    if (next == 2) {
      next = 0;
      new->istable = EPATTERN_TABLE|EPATTERN_NEWBLOCK;
      *pos = i;
      new->p = hgrep_epcomp_pre(src,pos,s,flags);
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
    } else
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
  free(src);
  return ret;
}

void
hgrep_epcomp(char *src, size_t *pos, size_t s, const uchar flags, hgrep_epattern **epatterns, size_t *epatternsl)
{
  flexarr *ret = hgrep_epcomp_pre(src,pos,s,flags);
  if (!ret) {
    *epatternsl = 0;
    return;
  }
  flexarr_conv(ret,(void**)epatterns,epatternsl);
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

static void
hgrep_ematch_pre(hgrep *hg, hgrep_epattern *patterns, const size_t patternsl, flexarr *source, flexarr *dest, flexarr *pcollector)
{
  flexarr *buf[3];

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
      hgrep_ematch_pre(hg,(hgrep_epattern*)((flexarr*)patterns[i].p)->v,((flexarr*)patterns[i].p)->size,buf[0],buf[1],pcollector);
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
                    ((hgrep_str*)pcollector->v)[c-lastp] = ((hgrep_str*)pcollector->v)[c];
                pcollector->size -= lastp-startp;
            }
          } else {
            pcollector->size = startp;
            *(hgrep_str*)flexarr_inc(pcollector) = (hgrep_str){(char*)lastformat,buf[2]->size-prevsize};
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
    hgrep_str *pcol = (hgrep_str*)pcollector->v;
    //ONLY NEEDED FOR DEBUGGING PURPOSES
    size_t pcollector_sum = 0;
    for (size_t j = 0; j < pcollector->size; j++)
      pcollector_sum += pcol[j].s;
    if (pcollector_sum != buf[2]->size)
      hgrep_error(1,"pcollector does not match count of found tags, %lu != %lu",pcollector_sum,buf[2]->size);
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
      #ifdef HGREP_EDITING
      format_exec(hg->output,&hg->nodes[x->id],((hgrep_pattern*)pcol[pcurrent].b)->format,((hgrep_pattern*)pcol[pcurrent].b)->formatl,hg->data);
      #else
      if (((hgrep_pattern*)pcol[pcurrent].b)->format.b) {
        hgrep_printf(hg->output,((hgrep_pattern*)pcol[pcurrent].b)->format.b,((hgrep_pattern*)pcol[pcurrent].b)->format.s,&hg->nodes[x->id],hg->data);
      } else
        hgrep_print(hg->output,&hg->nodes[x->id]);
      #endif
      hg->nodes[x->id].lvl += x->lvl;
    }
    flexarr_free(buf[2]);
  }

  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);

  return;
}

void
hgrep_ematch(hgrep *hg, hgrep_epattern *patterns, const size_t patternsl, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl)
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
    flexarr *pcollector = flexarr_init(sizeof(hgrep_str),32);
    hgrep_ematch_pre(hg,patterns,patternsl,fsource,fdest,pcollector);
    flexarr_free(pcollector);
}

#ifdef HGREP_EDITING
static void
format_free(hgrep_format_func *format, size_t formatl)
{
  if (!format)
    return;
  for (size_t i = 0; i < formatl; i++) {
    for (size_t j = 0; j < 4; j++) {
      if (!format[i].arg[j])
        continue;
      if (((hgrep_str*)format[i].arg[j])->b)
        free(((hgrep_str*)format[i].arg[j])->b);
      free(format[i].arg[j]);
    }
  }
  free(format);
}

const struct { char *name; size_t namel; const char *arr; } tr_ctypes[] = {
  {"space",5,IS_SPACE},
  {"alnum",5,IS_ALNUM},
  {"alpha",5,IS_ALPHA},
  {"blank",5,IS_BLANK},
  {"cntrl",5,IS_CNTRL},
  {"digit",5,IS_DIGIT},
  {"graph",5,IS_GRAPH},
  {"lower",5,IS_LOWER},
  {"print",5,IS_PRINT},
  {"punct",5,IS_PUNCT},
  {"upper",5,IS_UPPER},
  {"xdigit",6,IS_XDIGIT}
};

static char const *
tr_match_ctypes(const char *name, const size_t namel) {
  for (size_t i = 0; i < LENGTH(tr_ctypes); i++)
    if (namel == tr_ctypes[i].namel && memcmp(name,tr_ctypes[i].name,namel) == 0)
      return tr_ctypes[i].arr;
  return NULL;
}

static int
strrange_next(const char *src, const size_t size, size_t *pos, int *rstart, int *rend, char const**array, int *repeat, int *hasended)
{
  if (*repeat != -1 && *rstart != -1) {
    if (*pos >= size && *repeat == 0) {
      *hasended = 1;
      return *rstart;
    }
    if (*repeat) {
      (*repeat)--;
      return *rstart;
    } else {
      *repeat = -1;
      size_t t = *rstart;
      *rstart = -1;
      if (*rend == 0)
        return t;
    }
  }
  if (*rstart == -1 && *rend != -1) {
    *hasended = 1;
    return *rend;
  }
  if (*rstart != -1 && *rend != -1) {
    int out = -1;
    if (*rstart == *rend) {
      out = *rstart;
      *rstart = -1;
      *rend = -1;
      if (*pos >= size) {
        *rend = out;
      }
    } else if (*rstart < *rend) {
      (*rstart)++;
      out = *rstart-1;
    } else {
      (*rstart)--;
      out = *rstart+1;
    }
    return out;
  }
  if (*rstart != -1 && *array != NULL) {
    for (; *rstart < 256; (*rstart)++)
      if ((*array)[(uchar)*rstart])
        return (*rstart)++;
    if (*pos >= size) {
      *hasended = 1;
      for (; *rstart > 0; (*rstart)--) {
        if ((*array)[(uchar)*rstart-1]) {
          int r = *rstart-1;
          *rstart = 256;
          return r;
        }
      }
      return -1;
    } else {
      *rstart = -1;
      *array = NULL;
    }
  }

  if (*pos >= size) {
    *hasended = 1;
    if (2 < size && src[size-1] == '\\') {
        size_t x = 0;
        while (size-2-x && src[size-2-x] == '\\')
          x++;
        return (x&1) ? -1 : special_character(src[size-1]);
    } else
      return src[size-1];
  }

  char ch,och;
  och = src[*pos];
  if (src[*pos] == '\\' && *pos+1 < size) {
    ch = special_character(src[++(*pos)]);
  } else
    ch = src[*pos];
  if (*pos+2 < size && src[(*pos)+1] == '-' && (src[(*pos)+2] != '\\' || *pos+3 < size)) {
    char second = src[(*pos)+2];
    if (second == '\\') {
      second = special_character(src[(*pos)+3]);
      (*pos)++;
    }
    *rstart = ch;
    *rend = second;
    *pos += 3;
    return strrange_next(src,size,pos,rstart,rend,array,repeat,hasended);
  } else {
    if (och != '\\' && *pos+5 < size && ch == '[' && src[(*pos)+1] == ':') {
      size_t j = *pos+2;
      while (j < size && src[j] != ':')
        j++;
      if (src[j] == ':' && j+1 < size && src[j+1] == ']') {
        char const *class = src+*pos+2;
        size_t classl = j-(*pos+2);
        char const * ctype = tr_match_ctypes(class,classl);
        *pos = j+2;
        if (ctype) {
          *array = ctype;
          *rstart = 0;
          return strrange_next(src,size,pos,rstart,rend,array,repeat,hasended);
        } else
          hgrep_error(1,"tr: invalid character class '%.*s'",(int)classl,class);
      }
    } else if (och != '\\' && *pos+3 < size && ch == '[' && (src[(*pos)+1] != '\\' || *pos+4 < size)) {
      size_t prevpos = *pos;
      char cha = src[(*pos)+1];
      if (cha == '\\') {
        cha = special_character(src[(*pos)+2]);
        (*pos)++;
      }
      if (src[(*pos)+2] == '*') {
        *pos += 3;
        int num = number_handle(src,pos,size);
        if (num == -1) {
          *rend = 0;
          num = 1;
        }
        if (src[*pos] == ']') {
          *repeat = num;
          *rstart = cha;
          (*pos)++;
          return strrange_next(src,size,pos,rstart,rend,array,repeat,hasended);
        }
      }
      *rend = -1;
      *repeat = -1;
      *pos = prevpos;
    }
    (*pos)++;
    return ch;
  }
  return -1;
}

static void
strrange(const char *src1, const size_t size1, const char *src2, const size_t size2, uchar arr[256], uchar arr_enabled[256], uchar complement)
{
  size_t pos[2]={0};
  int rstart[2]={-1,-1},rend[2]={-1,-1},repeat[2]={-1,-1},hasended[2]={0};
  char const *array[2] = {NULL};

  while (!hasended[0]) {
    int r1 = strrange_next(src1,size1,&pos[0],&rstart[0],&rend[0],&array[0],&repeat[0],&hasended[0]);
    if (r1 == -1 || hasended[0])
      break;
    int r2 = -1;
    if (src2 && !complement) {
      r2 = strrange_next(src2,size2,&pos[1],&rstart[1],&rend[1],&array[1],&repeat[1],&hasended[1]);
      if (r2 == -1)
        break;
    }
    if (!complement && arr_enabled)
      arr_enabled[(uchar)r1] = 1;

    arr[(uchar)r1] = (src2 && !complement) ? (uchar)r2 : 1;
  }
  if (complement) {
    int last = 0;
    if (src2) {
      while (!hasended[1]) {
        int r = strrange_next(src2,size2,&pos[1],&rstart[1],&rend[1],&array[1],&repeat[1],&hasended[1]);
        if (r == -1 || hasended[1])
          break;
        last = r;
      }
    }
    for (ushort i = 0; i < 256; i++) {
      if (arr[(uchar)i]) {
        arr[(uchar)i] = 0;
      } else {
        arr[(uchar)i] = last;
        if (arr_enabled)
          arr_enabled[(uchar)i] = 1;
      }
    }
  }
}

static int
cut_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag)
{
  uchar delim[256]={0},linedelim[256]={0};
  uchar complement=0,onlydelimited=0,zeroterminated=0,delimited=0;

  struct hgrep_range *list = NULL;
  size_t listl;

  if (arg[0] && !(flag&FORMAT_ARG0_ISSTR)) {
    list = (struct hgrep_range*)((hgrep_str*)arg[0])->b;
    listl = ((hgrep_str*)arg[0])->s;
  }
  if (arg[1] && flag&FORMAT_ARG1_ISSTR && ((hgrep_str*)arg[1])->b && ((hgrep_str*)arg[1])->s) {
    hgrep_str *str = (hgrep_str*)arg[1];
    strrange(str->b,str->s,NULL,0,delim,NULL,0);
    delimited = 1;
  }
  if (arg[2] && flag&FORMAT_ARG2_ISSTR && ((hgrep_str*)arg[2])->b) {
    hgrep_str *str = (hgrep_str*)arg[2];
    for (size_t i = 0; i < str->s; i++) {
        if (str->b[i] == 's') {
          onlydelimited = 1;
        } else if (str->b[i] == 'c') {
          complement = 1;
        } else if (str->b[i] == 'z')
          zeroterminated = 1;
    }
  }
  if (arg[3] && flag&FORMAT_ARG3_ISSTR && ((hgrep_str*)arg[3])->b) {
    hgrep_str *str = (hgrep_str*)arg[3];
    strrange(str->b,str->s,NULL,0,linedelim,NULL,0);
  } else
    linedelim[(zeroterminated) ? '\0' : '\n'] = 1;

  if (!list)
    return 1;

  size_t line=0,lineend,delimstart;
  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;
  uchar printlinedelim;

  while (line < size) {
    lineend = line;
    printlinedelim = 1;
    while (lineend < size && !linedelim[(uchar)src[lineend]])
      lineend++;
    size_t start=line,end=lineend;
    if (delimited) {
      size_t dstart,dend,dlength,dcount=0,dprevend=0,dprevendlength=0;
      if (onlydelimited)
        printlinedelim = 0;
      while (1) {
        dstart = start;
        dend = dstart;
        while (!delim[(uchar)src[dend]] && dend < end)
          dend++;
        dlength = dend-dstart;
        if (delim[(uchar)src[dend]] && dend < end)
          dend++;
        if (dlength != dend-dstart)
          printlinedelim = 1;
        start = dend;
        if (ranges_match(dcount+1,list,listl,-1)^complement) {
          if (dprevendlength)
            fwrite(src+dprevend,1,1,output);
          if (dlength)
            fwrite(src+dstart,1,dlength,output);
          dprevendlength = 1;
        }
        dprevend = dstart+dlength;
        if (dprevend >= end)
          break;
        dcount++;
      }
    } else {
      for (size_t i = start; i < end; i++) {
        if (ranges_match(i+1-start,list,listl,end-start)^complement) {
          buf[bufcurrent++] = src[i];
          if (bufcurrent == bufsize) {
            fwrite(buf,1,bufcurrent,output);
            bufcurrent = 0;
          }
        }
      }
      if (bufcurrent) {
        fwrite(buf,1,bufcurrent,output);
        bufcurrent = 0;
      }
    }
    line = lineend;
    delimstart = line;
    while (line < size && linedelim[(uchar)src[line]])
      line++;
    if (line-delimstart && printlinedelim) {
      fwrite(src+delimstart,1,(delimited && onlydelimited) ? 1 : line-delimstart,output);
    }
  }

  return 0;
}

static int
tr_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag)
{
  uchar array[256] = {0};
  hgrep_str *string[2] = {NULL};
  uchar complement=0,squeeze=0;
  if (arg[0] && flag&FORMAT_ARG0_ISSTR && ((hgrep_str*)arg[0])->b && ((hgrep_str*)arg[0])->s)
    string[0] = (hgrep_str*)arg[0];
  if (arg[1] && flag&FORMAT_ARG1_ISSTR && ((hgrep_str*)arg[1])->b && ((hgrep_str*)arg[1])->s)
    string[1] = (hgrep_str*)arg[1];
  if (arg[2] && flag&FORMAT_ARG2_ISSTR && ((hgrep_str*)arg[2])->b) {
    hgrep_str *str = (hgrep_str*)arg[2];
    for (size_t i = 0; i < str->s; i++) {
        if (str->b[i] == 's') {
          squeeze = 1;
        } else if (str->b[i] == 'c')
          complement = 1;
    }
  }

  if (!string[0])
    return 1;

  const size_t bufsize = 8192;
  char buf[bufsize];
  size_t bufcurrent = 0;

  if (string[0] && !string[1]) {
    strrange(string[0]->b,string[0]->s,NULL,0,array,NULL,complement);
    for (size_t i = 0; i < size; i++) {
      if (!array[(uchar)src[i]]) {
        buf[bufcurrent++] = src[i];
        if (bufcurrent == bufsize) {
          fwrite(buf,1,bufcurrent,output);
          bufcurrent = 0;
        }
      }
    }
    if (bufcurrent) {
      fwrite(buf,1,bufcurrent,output);
      bufcurrent = 0;
    }
    return 0;
  }

  uchar array_enabled[256] = {0};
  strrange(string[0]->b,string[0]->s,string[1]->b,string[1]->s,array,array_enabled,complement);

  for (size_t i = 0; i < size; i++) {
    buf[bufcurrent++] = (array_enabled[(uchar)src[i]]) ? array[(uchar)src[i]] : src[i];
    if (bufcurrent == bufsize) {
      fwrite(buf,1,bufcurrent,output);
      bufcurrent = 0;
    }
    if (squeeze) {
      char previous = src[i];
      while (i+1 < size && src[i+1] == previous)
        i++;
    }
  }
  if (bufcurrent) {
    fwrite(buf,1,bufcurrent,output);
    bufcurrent = 0;
  }

  return 0;
}

static int
trim_edit(char *src, size_t size, FILE *output, void *arg[4], unsigned char flag)
{
  size_t line = 0;
  size_t lineend;
  size_t delimstart;

  uchar delim[256] = {0};
  if (arg[0] && flag&FORMAT_ARG0_ISSTR && ((hgrep_str*)arg[0])->b) {
    hgrep_str *str = (hgrep_str*)arg[0];
    strrange(str->b,str->s,NULL,0,delim,NULL,0);
  }

  while (line < size) {
    delimstart = line;
    while (line < size && delim[(uchar)src[line]])
      line++;
    if (line-delimstart)
      fwrite(src+delimstart,1,line-delimstart,output);
    lineend = line;
    while (lineend < size && !delim[(uchar)src[lineend]])
      lineend++;
    if (lineend-line) {
      size_t start=line,end=lineend;
      while (start < end && isspace(src[start]))
        start++;
      while (end-1 > start && isspace(src[end-1]))
        end--;
      if (end-start)
        fwrite(src+start,1,end-start,output);
    }
    line = lineend;
  }

  return 0;
}
#endif

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
  if (p->format.b)
    free(p->format.b);
  #endif
}

void
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

void
hgrep_fmatch(char *ptr, const size_t size, FILE *output, hgrep_pattern *pattern)
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
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_str_pair),ATTRIB_INC);
  for (size_t i = 0; i < size; i++) {
    while (ptr[i] != '<' && i < size)
        i++;
    while (i < size && ptr[i] == '<')
      struct_handle(ptr,&i,size,0,nodes,&t);
  }
  flexarr_free(nodes);
  t.nodes = NULL;
  t.nodesl = 0;
  flexarr_free((flexarr*)t.attrib_buffer);
}

hgrep
hgrep_init(char *ptr, const size_t size, FILE *output)
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
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_str_pair),ATTRIB_INC);
  for (size_t i = 0; i < size; i++) {
    while (ptr[i] != '<' && i < size)
        i++;
    while (i < size && ptr[i] == '<')
      struct_handle(ptr,&i,size,0,nodes,&t);
  }
  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  flexarr_free((flexarr*)t.attrib_buffer);
  return t;
}
