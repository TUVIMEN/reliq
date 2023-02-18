/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2022 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <limits.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "hgrep.h"
#include "flexarr.h"
#include "ctype.h"

#define HGREP_SAVE 0x8

#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

#define PATTERN_SIZE (1<<9)
#define PATTRIB_INC 8
#define BRACKETS_SIZE (1<<6)

#define P_INVERT 0x1
#define P_MATCH_INSIDES 0x2
#define P_INVERT_INSIDES 0x4
#define P_EMPTY 0x8

#define F_SBRACKET 0x1
#define F_STRING 0x2
#define F_ATTRIBUTES 0x4
#define F_LEVEL 0x8
#define F_CHILD_COUNT 0x10
#define F_SIZE 0x20
#define F_PRINTF 0x40
#define F_MATCH_INSIDES 0x80

#define ATTRIB_INC (1<<3)
#define HGREP_NODES_INC (1<<3)

#define toggleflag(x,y,z) (y) = (x) ? (y)|(z) : (y)&~(z)
#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGHT(x) (sizeof(x)/(sizeof(*x)))

typedef struct {
    char *b;
    uchar s;
} str8;

typedef struct {
    str8 name;
    ushort flags;
    uchar flagstoset;
} hgrep_function;

const hgrep_function functions[] = {
    {{"p",1},F_STRING|F_PRINTF,0},
    {{"m",1},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES},
    {{"M",1},F_STRING|F_MATCH_INSIDES,P_MATCH_INSIDES|P_INVERT_INSIDES},
    {{"a",1},F_SBRACKET|F_ATTRIBUTES,0},
    {{"l",1},F_SBRACKET|F_LEVEL,0},
    {{"s",1},F_SBRACKET|F_SIZE,0},
    {{"c",1},F_SBRACKET|F_CHILD_COUNT,0},

    {{"printf",6},F_STRING|F_PRINTF,0},
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

#ifdef AUTOCLOSING
const str8 autoclosing_s[] = { //tags that doesn't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},{"tfoot",5},
  {"thead",5},{"rt",2},{"rp",2},{"caption",7},
  {"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

static uchar
matchxy(uint x, uint y, const uint z, const size_t last)
{
  if (x == UINT_MAX)
      x = last;
  if (y == UINT_MAX)
      y = last;
  if (y == 0) {
    if (z == x)
      return 1;
  } else if (z <= y && z >= x)
    return 1;

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

int number_handle(const char *src, size_t *pos, const size_t size)
{
  size_t t = *pos, s;
  int ret = atoi(src+t);
  while_is(isdigit,src,*pos,size);
  s = *pos-t;
  if (s == 0)
    return -1;
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
    while (*i < s && (isalnum(f[*i]) || f[*i] == '-' || f[*i] == '_'))
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

#ifdef PHPTAGS
static void
phptag_handle(char *f, size_t *i, const size_t s, hgrep_node *hgn)
{
  (*i)++;
  while_is(isspace,f,*i,s);
  name_handle(f,i,s,&hgn->tag);

  char *ending;
  for (; *i < s; (*i)++) {
    if (f[*i] == '\\') {
      *i += 2;
      continue;
    }
    if (f[*i] == '?' && f[*i+1] == '>') {
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

  if (!matchxy(p->px,p->py,hgn->lvl,-1)
    ||
  #ifdef PHPTAGS
     (a &&
  #endif
     !matchxy(p->ax,p->ay,hgn->attribl,-1)
  #ifdef PHPTAGS
     )
  #endif
    || !matchxy(p->cx,p->cy,hgn->child_count,-1)
    || !matchxy(p->sx,p->sy,hgn->insides.s,-1))
    return 0^rev;

  pmatch.rm_so = 0;
  pmatch.rm_eo = (int)hgn->tag.s;
  if (regexec(&p->r,hgn->tag.b,1,&pmatch,REG_STARTEND) != 0)
    return 0^rev;

  for (size_t i = 0; i < p->attribl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hgn->attribl; j++) {
      if (!matchxy(attrib[i].px,attrib[i].py,j,hgn->attribl-1))
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
    if ((regexec(&p->in,hgn->insides.b,1,&pmatch,REG_STARTEND) != 0)^((p->flags&P_INVERT_INSIDES)==P_INVERT_INSIDES))
      return 0^rev;
  }

  return 1^rev;
}

static uint
struct_handle(char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, hgrep *hg)
{
  uint ret = 1;
  hgrep_node *hgn = flexarr_inc(nodes);
  memset(hgn,0,sizeof(hgrep_node));
  hgn->lvl = lvl;
  size_t index = nodes->size-1;
  flexarr *a = (flexarr*)hg->attrib_buffer;
  size_t attrib_start = a->size;

  hgn->all.b = f+*i;
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '!') {
    comment_handle(f,i,s);
    flexarr_dec(nodes);
    return 0;
  }

  #ifdef PHPTAGS
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
  #define search_array(x,y) for (uint _j = 0; _j < (uint)LENGHT(x); _j++) \
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
  
  #ifdef AUTOCLOSING
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
      } else if (!script) {
        if (f[*i] == '!') {
          (*i)++;
          comment_handle(f,i,s);
          continue;
        } else {
          #ifdef AUTOCLOSING
          if (autoclosing) {
            size_t ti = *i;
            hgrep_str name;

            ti++;
            while_is(isspace,f,ti,s);
            name_handle(f,&ti,s,&name);

            if (strcomp(hgn->tag,name))
              goto END;
          }
          #endif
          *i = tagend;
          ret += struct_handle(f,i,s,lvl+1,nodes,hg);
          hgn = &((hgrep_node*)nodes->v)[index];
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
      if (pattern->format.b)
        hgrep_printf(hg->output,pattern->format.b,pattern->format.s,hgn,hg->data);
      else
        hgrep_print(hg->output,hgn);
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
    fwrite(a[j].f.b,a[j].f.s,1,outfile);
    fputs("=\"",outfile);
    fwrite(a[j].s.b,a[j].s.s,1,outfile);
    fputc('"',outfile);
  }
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
        case 'A': fwrite(hgn->all.b,hgn->all.s,1,outfile); break;
        case 't': fwrite(hgn->tag.b,hgn->tag.s,1,outfile); break;
        case 'i': fwrite(hgn->insides.b,hgn->insides.s,1,outfile); break;
        case 'l': fprintf(outfile,"%u",hgn->lvl); break;
        case 's': fprintf(outfile,"%lu",hgn->all.s); break;
        case 'p': fprintf(outfile,"%lu",hgn->all.b-reference); break;
        case 'I': print_attribs(outfile,hgn); break;
        case 'a': {
          hgrep_str_pair *a = hgn->attrib;
          if (num != -1) {
            if ((size_t)num < hgn->attribl)
              fwrite(a[num].s.b,a[num].s.s,1,outfile);
          } else if (contl != 0) {
            for (size_t i = 0; i < hgn->attribl; i++)
              if (a[i].f.s == contl && memcmp(a[i].f.b,format+cont,contl) == 0)
                fwrite(a[i].s.b,a[i].s.s,1,outfile);
          } else for (size_t i = 0; i < hgn->attribl; i++) {
            fwrite(a[i].s.b,a[i].s.s,1,outfile);
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
  fwrite(hgn->all.b,hgn->all.s,1,outfile);
  fputc('\n',outfile);
}

static void
sbrackets_handle(const char *src, size_t *pos, const size_t size, uint *x, uint *y)
{
  *x = 0;
  *y = 0;
  int r;

  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == '-')
    goto NEXT_VALUE;
  if (src[*pos] == '$') {
    *x = -1;
    (*pos)++;
    goto END;
  }

  r = number_handle(src,pos,size);
  if (r == -1)
    return;
  *x = (uint)r;

  while_is(isspace,src,*pos,size);

  if (src[*pos] != '-')
    goto END;

  NEXT_VALUE: ;
  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == ']') {
    (*pos)++;
    *y = -1;
    goto END;
  }
  if (src[*pos] == '$') {
    (*pos)++;
    *y = -1;
    while_is(isspace,src,*pos,size);
    while (src[*pos] != ']' && *pos < size)
        (*pos)++;
    if (src[*pos] == ']' && *pos < size)
        (*pos)++;
    goto END;
  }

  r = number_handle(src,pos,size);
  if (r == -1)
    return;
  *y = (uint)r;

  END: ;
  while_is(isspace,src,*pos,size);
}

static void
function_handle(hgrep_pattern *p, char *func, size_t *pos, size_t *size, const int regexflags)
{
  if (*pos >= *size || !func || func[*pos] != '@')
    return;
  size_t t = ++(*pos);
  while (*pos < *size && (isalnum(func[*pos]) || func[*pos] == '-'))
    (*pos)++;
  if (!(*pos-t))
    return;
  char found = 0;
  size_t i = 0;
  for (; i < LENGHT(functions); i++) {
    if (functions[i].name.s == *pos-t && memcmp(functions[i].name.b,func+t,*pos-t) == 0) {
      found = 1;
      break;
    }
  }
  if (!found)
    return;

  uint x,y;

  while_is(isspace,func,*pos,*size);
  if (functions[i].flags&F_SBRACKET) {
    if (func[*pos] != '[')
      return;
    sbrackets_handle(func,pos,*size,&x,&y);
  } else if (functions[i].flags&F_STRING) {
    if (func[*pos] != '"')
      return;
    t = ++(*pos);
    while (*pos < *size && func[*pos] != '"') {
      if (func[*pos] == '\\' && func[*pos+1] == '"')
        delchar(func,*pos,size);
      (*pos)++;
    }
    (*pos)++;
    if (*pos > *size)
      return;
  }

  p->flags |= functions[i].flagstoset;
  if (functions[i].flags&F_ATTRIBUTES) {
    p->ax = x;
    p->ay = y;
  } else if (functions[i].flags&F_LEVEL) {
    p->px = x;
    p->py = y;
  } else if (functions[i].flags&F_SIZE) {
    p->sx = x;
    p->sy = y;
  } else if (*pos-t) {
      size_t s = *pos-t-1;
      if (functions[i].flags&F_PRINTF) {
        p->format.b = memcpy(malloc(s),func+t,s);
        p->format.s = s;
      } else if (functions[i].flags&F_MATCH_INSIDES) {
        char *tmp = func+t;
        x = tmp[s];
        tmp[s] = 0;
        int r = regcomp(&p->in,tmp,regexflags);
        tmp[s] = x;
        if (r)
          return;
      }
  }

  return;
}

void
hgrep_pcomp(char *pattern, size_t size, hgrep_pattern *p, const uchar flags)
{
  struct hgrep_pattrib *pa;
  p->flags = flags;
  int regexflags = REG_NEWLINE|REG_NOSUB;
  if (flags&HGREP_ICASE)
      regexflags |= REG_ICASE;
  if (flags&HGREP_EREGEX)
      regexflags |= REG_EXTENDED;
  p->format.b = NULL;
  ushort attribcount = 0;
  p->py = -1;
  p->px = 0;
  p->ay = -1;
  p->ax = 0;
  p->sy = -1;
  p->sx = 0;
  p->cy = -1;
  p->cx = 0;
  char tmp[PATTERN_SIZE];
  tmp[0] = '^';

  size_t pos = 0,t;
  while_is(isspace,pattern,pos,size);
  if (pattern[pos] == '!') {
      p->flags |= P_INVERT;
      pos++;
  }
  t = pos;
  while (pos < size && pos-t < PATTERN_SIZE-2 && !isspace(pattern[pos]) && pattern[pos] != '@')
    pos++;
  if (!(pos-t)) {
    for (size_t i = pos; i < size && !isalnum(pattern[i]); i++)
        if (pattern[pos] == '@')
          function_handle(p,pattern,&i,&size,regexflags);
    p->flags |= P_EMPTY;
    return;
  }
  memcpy(tmp+1,pattern+t,pos-t);
  tmp[pos+1-t] = '$';
  tmp[pos+2-t] = 0;
  if (regcomp(&p->r,tmp,regexflags))
    return;

  flexarr *attrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  for (size_t i = pos; i < size;) {
    while_is(isspace,pattern,i,size);
    if (pattern[i] == '@') {
      function_handle(p,pattern,&i,&size,regexflags);
      continue;
    }

    if (pattern[i] == '+' || pattern[i] == '-') {
      uchar tf = pattern[i++]; 
      uint x=0,y=-1;
      while_is(isspace,pattern,i,size);
      if (pattern[i] == '[')
        sbrackets_handle(pattern,&i,size,&x,&y);
      t = i;
      while (i < size && i < PATTERN_SIZE-2 && !isspace(pattern[i]) && pattern[i] != '=')
        i++;
      memcpy(tmp+1,pattern+t,i-t);
      tmp[i+1-t] = '$';
      tmp[i+2-t] = 0;
      pa = (struct hgrep_pattrib*)flexarr_inc(attrib);
      pa->flags = 0;
      if (tf == '+') {
        attribcount++;
        pa->flags |= A_INVERT;
      } else {
        pa->flags &= ~A_INVERT;
      }

      if (regcomp(&pa->r[0],tmp,regexflags))
        goto CONV;
      pa->px = x;
      pa->py = y;

      if (pattern[i] == '=' && pattern[i+1] == '"') {
        i += 2;
        t = i;
        while (i < size && i < PATTERN_SIZE-2 && pattern[i] != '"') {
          if (pattern[i] == '\\' && pattern[i+1] == '"')
  	        delchar(pattern,i,&size);
          i++;
        }
        memcpy(tmp+1,pattern+t,i-t);
        tmp[i+1-t] = '$';
        tmp[i+2-t] = 0;
        if (regcomp(&pa->r[1],tmp,regexflags))
          goto CONV;
        pa->flags |= A_VAL_MATTERS;
        i++;
      } else
        pa->flags &= ~A_VAL_MATTERS;
    }
    i++;
  }
  if (p->ax < attribcount)
    p->ax = attribcount;
  CONV: ;
  size_t attribl;
  flexarr_conv(attrib,(void**)&p->attrib,&attribl);
  p->attribl = attribl;
  return;
}

void
hgrep_pfree(hgrep_pattern *p)
{
  if (p == NULL)
    return;
  if (!(p->flags&P_EMPTY)) {
    regfree(&p->r);
    if (p->flags&P_MATCH_INSIDES)
      regfree(&p->in);
    for (size_t j = 0; j < p->attribl; j++) {
      regfree(&p->attrib[j].r[0]);
      if (p->attrib[j].flags&A_VAL_MATTERS)
        regfree(&p->attrib[j].r[1]);
    }
    free(p->attrib);
  }
  if (p->format.b)
    free(p->format.b);
}

void
hgrep_free(hgrep *hg)
{
    if (hg == NULL)
        return;
    for (size_t i = 0; i < hg->nodesl; i++)
        free(hg->nodes[i].attrib);
    free(hg->nodes);
}

void
hgrep_init(hgrep *hg, char *ptr, const size_t size, FILE *output, hgrep_pattern *pattern, const uchar flags)
{
  hgrep t;
  t.data = ptr;
  t.size = size;
  t.pattern = pattern;
  t.flags = flags;
  toggleflag(hg,t.flags,HGREP_SAVE);
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
  if (t.flags&HGREP_SAVE) {
    flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  } else {
    flexarr_free(nodes);
    t.nodes = 0;
    t.nodesl = 0;
  }
  flexarr_free((flexarr*)t.attrib_buffer);
  if (hg)
      memcpy(hg,&t,sizeof(hgrep));
}
