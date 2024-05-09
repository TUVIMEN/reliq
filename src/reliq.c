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
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <stdarg.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

#include "reliq.h"
#include "flexarr.h"
#include "ctype.h"
#include "utils.h"
#include "edit.h"
#include "html.h"

#define PASSED_INC (1<<14)
#define PATTERN_SIZE_INC (1<<8)
#define PATTRIB_INC 8
#define HOOK_INC 8
#define NCOLLECTOR_INC (1<<8)
#define FCOLLECTOR_INC (1<<5)

#define UINT_TO_STR_MAX 32

//reliq_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//reliq_node flags
#define N_EMPTY 0x1 //ignore matching
#define N_POSITION_ABSOLUTE 0x2

//reliq_match_function flags
#define F_KINDS 0x7

#define F_ATTRIBUTES 0x1
#define F_LEVEL 0x2
#define F_LEVEL_RELATIVE 0x3
#define F_CHILD_COUNT 0x4
#define F_MATCH_INSIDES 0x5

#define F_RANGE 0x8
#define F_PATTERN 0x10

//reliq_expr istable flags
#define EXPR_TABLE 0x1
#define EXPR_NEWBLOCK 0x2
#define EXPR_NEWCHAIN 0x4
#define EXPR_SINGULAR 0x8
#define EXPR_OUTPROTECTED 0x10

#define ATTRIB_INC (1<<3)
#define RELIQ_NODES_INC (1<<13)

//reliq_pattern flags
#define RELIQ_PATTERN_TRIM 0x1
#define RELIQ_PATTERN_CASE_INSENSITIVE 0x2
#define RELIQ_PATTERN_INVERT 0x4

#define RELIQ_PATTERN_MATCH ( \
        RELIQ_PATTERN_MATCH_FULL | \
        RELIQ_PATTERN_MATCH_ALL | \
        RELIQ_PATTERN_MATCH_BEGINNING | \
        RELIQ_PATTERN_MATCH_ENDING \
        )

#define RELIQ_PATTERN_MATCH_FULL 0x8
#define RELIQ_PATTERN_MATCH_ALL 0x10
#define RELIQ_PATTERN_MATCH_BEGINNING 0x18
#define RELIQ_PATTERN_MATCH_ENDING 0x20

#define RELIQ_PATTERN_PASS ( \
        RELIQ_PATTERN_PASS_WHOLE | \
        RELIQ_PATTERN_PASS_WORD \
        )

#define RELIQ_PATTERN_PASS_WHOLE 0x40
#define RELIQ_PATTERN_PASS_WORD 0x80

#define RELIQ_PATTERN_TYPE ( \
        RELIQ_PATTERN_TYPE_STR | \
        RELIQ_PATTERN_TYPE_BRE | \
        RELIQ_PATTERN_TYPE_ERE \
        )

#define RELIQ_PATTERN_TYPE_STR 0x100
#define RELIQ_PATTERN_TYPE_BRE 0x200
#define RELIQ_PATTERN_TYPE_ERE 0x300

#define RELIQ_PATTERN_EMPTY 0x400
#define RELIQ_PATTERN_ALL 0x800

struct reliq_match_function {
  reliq_str8 name;
  ushort flags;
};

const struct reliq_match_function match_functions[] = {
    {{"m",1},F_PATTERN|F_MATCH_INSIDES},
    {{"a",1},F_RANGE|F_ATTRIBUTES},
    {{"l",1},F_RANGE|F_LEVEL_RELATIVE},
    {{"L",1},F_RANGE|F_LEVEL},
    {{"c",1},F_RANGE|F_CHILD_COUNT},

    {{"match",5},F_PATTERN|F_MATCH_INSIDES},
    {{"attributes",10},F_RANGE|F_ATTRIBUTES},
    {{"levelrelative",13},F_RANGE|F_LEVEL_RELATIVE},
    {{"level",5},F_RANGE|F_LEVEL},
    {{"children",8},F_RANGE|F_CHILD_COUNT},
};

reliq_error *
reliq_set_error(const int code, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  reliq_error *err = malloc(sizeof(reliq_error));
  vsnprintf(err->msg,LENGTH(err->msg),fmt,ap);
  err->code = code;
  va_end(ap);
  return err;
}

static void
reliq_regcomp_set_flags(ushort *flags, const char *src, const size_t len)
{
  for (size_t i = 0; i < len; i++) {
    switch (src[i]) {
      case 't':
          *flags |= RELIQ_PATTERN_TRIM;
          break;
      case 'u':
          *flags &= ~RELIQ_PATTERN_TRIM;
          break;

      case 'i':
          *flags |= RELIQ_PATTERN_CASE_INSENSITIVE;
          break;
      case 'c':
          *flags &= ~RELIQ_PATTERN_CASE_INSENSITIVE;
          break;

      case 'v':
          *flags |= RELIQ_PATTERN_INVERT;
          break;
      case 'n':
          *flags &= ~RELIQ_PATTERN_INVERT;
          break;

      case 'a':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_ALL;
          break;
      case 'f':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_FULL;
          break;
      case 'b':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_BEGINNING;
          break;
      case 'e':
          *flags &= ~RELIQ_PATTERN_MATCH;
          *flags |= RELIQ_PATTERN_MATCH_ENDING;
          break;

      case 'W':
          *flags &= ~RELIQ_PATTERN_PASS;
          *flags |= RELIQ_PATTERN_PASS_WHOLE;
          break;
      case 'w':
          *flags &= ~RELIQ_PATTERN_PASS;
          *flags |= RELIQ_PATTERN_PASS_WORD;
          break;

      case 's':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_STR;
          break;
      case 'B':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_BRE;
          break;
      case 'E':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_ERE;
          break;
      /*case 'P':
          *flags &= ~RELIQ_PATTERN_TYPE;
          *flags |= RELIQ_PATTERN_TYPE_PCRE;
          break;*/
    }
  }
}

static void
reliq_regcomp_get_flags(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char *flags)
{
  size_t p = *pos;
  pattern->flags = RELIQ_PATTERN_TRIM|RELIQ_PATTERN_PASS_WHOLE|RELIQ_PATTERN_MATCH_FULL|RELIQ_PATTERN_TYPE_STR;
  pattern->range.s= 0;

  if (flags)
    reliq_regcomp_set_flags(&pattern->flags,flags,strlen(flags));

  if (src[p] == '\'' || src[p] == '"' || src[p] == '*')
    return;

  while (p < size && isalpha(src[p]))
    p++;
  if (p >= size || src[p] != '>')
    return;

  reliq_regcomp_set_flags(&pattern->flags,src+*pos,p-*pos);

  *pos = p+1;
  return;
}

static reliq_error *
reliq_regcomp_add_pattern(reliq_pattern *pattern, const char *src, const size_t size)
{
  ushort match = pattern->flags&RELIQ_PATTERN_MATCH,
    type = pattern->flags&RELIQ_PATTERN_TYPE;

  if (!size) {
    pattern->flags |= RELIQ_PATTERN_EMPTY;
    return NULL;
  }

  if (type == RELIQ_PATTERN_TYPE_STR) {
    pattern->match.str.b = memdup(src,size);
    pattern->match.str.s = size;
  } else {
    int regexflags = REG_NOSUB;
    if (pattern->flags&RELIQ_PATTERN_CASE_INSENSITIVE)
      regexflags |= REG_ICASE;
    if (type == RELIQ_PATTERN_TYPE_ERE)
      regexflags |= REG_EXTENDED;

    size_t addedspace = 0;
    uchar fullmatch = (match == RELIQ_PATTERN_MATCH_FULL) ? 1 : 0;

    if (fullmatch)
      addedspace = 2;
    else if (match == RELIQ_PATTERN_MATCH_BEGINNING || match == RELIQ_PATTERN_MATCH_ENDING)
      addedspace = 1;

    char *tmp = malloc(size+addedspace+1);
    size_t p = 0;

    if (fullmatch || match == RELIQ_PATTERN_MATCH_BEGINNING)
      tmp[p++] = '^';

    memcpy(tmp+p,src,size);
    p += size;

    if (fullmatch || match == RELIQ_PATTERN_MATCH_ENDING)
      tmp[p++] = '$';
    tmp[p] = 0;

    /*int ret =
    if (ret) //check for compiling error */
    //fprintf(stderr,"regcomp %s\n",tmp);
    regcomp(&pattern->match.reg,tmp,regexflags); //!
      //fprintf(stderr,"kkkkkkkkkkk\n");
    free(tmp);
  }
  return NULL;
}

static reliq_error *
reliq_regcomp(reliq_pattern *pattern, char *src, size_t *pos, size_t *size, const char delim, const char *flags)
{
  reliq_error *err;

  reliq_regcomp_get_flags(pattern,src,pos,*size,flags);

  if (*pos && *pos < *size && src[*pos-1] == '>' && src[*pos] == '[') {
    err = range_comp(src,pos,*size,&pattern->range);
    if (err)
      return err;
    if (*pos >= *size || src[*pos] == delim || isspace(src[*pos])) {
      pattern->flags |= RELIQ_PATTERN_ALL;
      return NULL;
    }
  }

  if ((*pos+1 == *size || (*pos+1 < *size &&
    (isspace(src[*pos+1]) || src[*pos+1] == delim)))
    && src[*pos] == '*') {
    (*pos)++;
    pattern->flags |= RELIQ_PATTERN_ALL;
    return NULL;
  }

  size_t start,len;
  err = get_quoted(src,pos,size,delim,&start,&len);
  if (err)
    return err;

  return reliq_regcomp_add_pattern(pattern,src+start,len);
}

static int
reliq_regexec_match_str(const reliq_pattern *pattern, reliq_cstr *str)
{
  reliq_pattern const *p = pattern;
  ushort match = p->flags&RELIQ_PATTERN_MATCH;
  uchar icase = p->flags&RELIQ_PATTERN_CASE_INSENSITIVE;

  if (!p->match.str.s)
    return 1;
  if (!str->s)
    return 0;

  switch (match) {
    case RELIQ_PATTERN_MATCH_ALL:
      if (icase) {
        if (memcasemem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
          return 1;
      } else if (memmem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_FULL:
      if (str->s != p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,str->s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,str->s) == 0)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_BEGINNING:
      if (str->s < p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,p->match.str.s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,p->match.str.s) == 0)
        return 1;
      break;
    case RELIQ_PATTERN_MATCH_ENDING:
      if (str->s < p->match.str.s)
        return 0;
      const char *start = str->b+(str->s-p->match.str.s);
      size_t len = p->match.str.s;
      if (icase) {
        if (memcasecmp(start,p->match.str.b,len) == 0)
          return 1;
      } else if (memcmp(start,p->match.str.b,len) == 0)
        return 1;
      break;
  }
  return 0;
}

static int
reliq_regexec_match_pattern(const reliq_pattern *pattern, reliq_cstr *str)
{
  ushort type = pattern->flags&RELIQ_PATTERN_TYPE;
  if (type == RELIQ_PATTERN_TYPE_STR) {
    return reliq_regexec_match_str(pattern,str);
  } else {
    if (!str->s)
      return 0;

    regmatch_t pmatch;
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)str->s;

    if (regexec(&pattern->match.reg,str->b,1,&pmatch,REG_STARTEND) == 0)
      return 1;
  }
  return 0;
}

static int
reliq_regexec_match_word(const reliq_pattern *pattern, reliq_cstr *str)
{
  const char *ptr = str->b;
  size_t plen = str->s;
  char const *saveptr,*word;
  size_t saveptrlen,wordlen;

  while (1) {
    memwordtok_r(ptr,plen,(void const**)&saveptr,&saveptrlen,(void const**)&word,&wordlen);
    if (!word)
      return 0;

    reliq_cstr t;
    t.b = word;
    t.s = wordlen;
    if (reliq_regexec_match_pattern(pattern,&t))
      return 1;

    ptr = NULL;
  }
  return 0;
}

static int
reliq_regexec(const reliq_pattern *pattern, const char *src, const size_t size)
{
  ushort pass = pattern->flags&RELIQ_PATTERN_PASS;
  uchar invert = (pattern->flags&RELIQ_PATTERN_INVERT) ? 1 : 0;
  if ((!range_match(size,&pattern->range,-1)))
    return invert;

  if (pattern->flags&RELIQ_PATTERN_ALL)
    return !invert;

  if (pattern->flags&RELIQ_PATTERN_EMPTY)
    return (size == 0) ? !invert : invert;

  reliq_cstr str = {src,size};

  if (pass == RELIQ_PATTERN_PASS_WORD)
    return reliq_regexec_match_word(pattern,&str)^invert;

  if (pattern->flags&RELIQ_PATTERN_TRIM)
    memtrim((void const**)&str.b,&str.s,src,size);

  return reliq_regexec_match_pattern(pattern,&str)^invert;
}

static void
reliq_regfree(reliq_pattern *pattern)
{
  if (!pattern)
    return;

  range_free(&pattern->range);

  if (pattern->flags&RELIQ_PATTERN_EMPTY || pattern->flags&RELIQ_PATTERN_ALL)
    return;

  if ((pattern->flags&RELIQ_PATTERN_TYPE) == RELIQ_PATTERN_TYPE_STR) {
    if (pattern->match.str.b)
      free(pattern->match.str.b);
  } else
    regfree(&pattern->match.reg);
}

static void
pattrib_free(struct reliq_pattrib *attrib) {
  if (!attrib)
    return;
  reliq_regfree(&attrib->r[0]);
  if (attrib->flags&A_VAL_MATTERS)
    reliq_regfree(&attrib->r[1]);
  range_free(&attrib->position);
}

static void
pattribs_free(struct reliq_pattrib *attribs, size_t attribsl)
{
  for (size_t i = 0; i < attribsl; i++)
    pattrib_free(&attribs[i]);
  free(attribs);
}

static void
reliq_free_hooks(reliq_hook *hooks, const size_t hooksl)
{
  if (!hooksl || !hooks)
    return;
  for (size_t i = 0; i < hooksl; i++) {
    if (hooks[i].flags&F_RANGE) {
      range_free(&hooks[i].match.range);
    } else
      reliq_regfree(&hooks[i].match.pattern);
  }
  free(hooks);
}

void
reliq_nfree(reliq_node *node)
{
  if (node == NULL || node->flags&N_EMPTY)
    return;

  reliq_regfree(&node->tag);

  pattribs_free(node->attribs,node->attribsl);

  reliq_free_hooks(node->hooks,node->hooksl);

  range_free(&node->position);
}

void
reliq_free(reliq *rq)
{
    if (rq == NULL)
      return;
    for (size_t i = 0; i < rq->nodesl; i++)
      free(rq->nodes[i].attribs);
    if (rq->nodesl)
      free(rq->nodes);
}

static int
pattrib_match(const reliq_hnode *hnode, const struct reliq_pattrib *attribs, size_t attribsl)
{
  const reliq_cstr_pair *a = hnode->attribs;
  for (size_t i = 0; i < attribsl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hnode->attribsl; j++) {
      if (!range_match(j,&attribs[i].position,hnode->attribsl-1))
        continue;

      if (!reliq_regexec(&attribs[i].r[0],a[j].f.b,a[j].f.s))
        continue;

      if (attribs[i].flags&A_VAL_MATTERS && !reliq_regexec(&attribs[i].r[1],a[j].s.b,a[j].s.s))
        continue;

      found = 1;
      break;
    }
    if (((attribs[i].flags&A_INVERT)==A_INVERT)^found)
      return 0;
  }
  return 1;
}

static int
reliq_match_hooks(const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_hook *hooks, const size_t hooksl)
{
  for (size_t i = 0; i < hooksl; i++) {
    char const *src = NULL;
    size_t srcl = 0;

    switch (hooks[i].flags&F_KINDS) {
      case F_ATTRIBUTES:
        srcl = hnode->attribsl;
        break;
      case F_LEVEL_RELATIVE:
        srcl = (parent) ? hnode->lvl-parent->lvl : hnode->lvl;
        break;
      case F_LEVEL:
        srcl = hnode->lvl;
        break;
      case F_CHILD_COUNT:
        srcl = hnode->child_count;
        break;
      case F_MATCH_INSIDES:
        src = hnode->insides.b;
        srcl = hnode->insides.s;
        break;
    }

    if (hooks[i].flags&F_RANGE) {
      if (!range_match(srcl,&hooks[i].match.range,-1))
        return 0;
    } else if (!reliq_regexec(&hooks[i].match.pattern,src,srcl))
      return 0;
  }
  return 1;
}

int
reliq_match(const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_node *node)
{
  if (node->flags&N_EMPTY)
    return 1;

  if (!reliq_regexec(&node->tag,hnode->tag.b,hnode->tag.s))
    return 0;

  if (!reliq_match_hooks(hnode,parent,node->hooks,node->hooksl))
    return 0;

  if (!pattrib_match(hnode,node->attribs,node->attribsl))
    return 0;

  return 1;
}

static void
print_trimmed_if(const reliq_cstr *str, const uchar trim, FILE *outfile)
{
  char const *dest = str->b;
  size_t destl = str->s;
  if (trim)
    memtrim((void const**)&dest,&destl,str->b,str->s);
  if (destl)
    fwrite(dest,1,destl,outfile);
}

static void
print_attribs(const reliq_hnode *hnode, const uchar trim, FILE *outfile)
{
  reliq_cstr_pair *a = hnode->attribs;
  for (ushort j = 0; j < hnode->attribsl; j++) {
    fputc(' ',outfile);
    fwrite(a[j].f.b,1,a[j].f.s,outfile);
    fputs("=\"",outfile);
    print_trimmed_if(&a[j].s,trim,outfile);
    fputc('"',outfile);
  }
}

static void
print_uint(unsigned long num, FILE *outfile)
{
  char str[UINT_TO_STR_MAX];
  size_t len = 0;
  uint_to_str(str,&len,UINT_TO_STR_MAX,num);
  if (len)
    fwrite(str,1,len,outfile);
}

static void
print_attrib_value(const reliq_cstr_pair *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uchar trim, FILE *outfile)
{
  if (num != -1) {
    if ((size_t)num < attribsl)
      print_trimmed_if(&attribs[num].s,trim,outfile);
  } else if (textl != 0) {
    for (size_t i = 0; i < attribsl; i++)
      if (memcomp(attribs[i].f.b,text,textl,attribs[i].f.s))
        print_trimmed_if(&attribs[i].s,trim,outfile);
  } else for (size_t i = 0; i < attribsl; i++) {
    print_trimmed_if(&attribs[i].s,trim,outfile);
    fputc('"',outfile);
  }
}

static void
print_text(const reliq_hnode *nodes, const reliq_hnode *hnode, FILE *outfile, uchar recursive)
{
  char const *start = hnode->insides.b;
  size_t id=hnode-nodes,end;

  for (size_t i = 1; i <= hnode->child_count; i++) {
    const reliq_hnode *n = &nodes[id+i];

    end = n->all.b-start;
    if (end)
      fwrite(start,1,end,outfile);

    if (recursive)
      print_text(nodes,n,outfile,recursive);

    i += n->child_count;
    start = n->all.b+n->all.s;
  }

  end = hnode->insides.s-(start-hnode->insides.b);
  if (end)
    fwrite(start,1,end,outfile);
}

void
reliq_printf(FILE *outfile, const char *format, const size_t formatl, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq *rq)
{
  size_t i = 0;
  char const *text;
  size_t textl=0;
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
        text = ++i+format;
        char *t = memchr(format+i,')',formatl-i);
        if (!t)
          return;
        textl = t-(format+i);
        i = t-format+1;
      }
      if (i >= formatl)
          return;

      uchar trim = 0;

      switch (format[i++]) {
        case '%': fputc('%',outfile); break;
        case 'i':
          trim = 1;
        case 'I': print_trimmed_if(&hnode->insides,trim,outfile); break;
        case 't': print_text(rq->nodes,hnode,outfile,0); break;
        case 'T': print_text(rq->nodes,hnode,outfile,1); break;
        case 'l': {
          ushort lvl = hnode->lvl;
          if (parent)
            lvl -= parent->lvl;
          print_uint(lvl,outfile);
          }
          break;
        case 'L': print_uint(hnode->lvl,outfile); break;
        case 'a':
          trim = 1;
        case 'A': print_attribs(hnode,trim,outfile); break;
        case 'v':
          trim = 1;
        case 'V':
          print_attrib_value(hnode->attribs,hnode->attribsl,text,textl,num,trim,outfile);
          break;
        case 's': print_uint(hnode->all.s,outfile); break;
        case 'c': print_uint(hnode->child_count,outfile); break;
        case 'C': fwrite(hnode->all.b,1,hnode->all.s,outfile); break;
        case 'p': print_uint(hnode->all.b-rq->data,outfile); break;
        case 'n': fwrite(hnode->tag.b,1,hnode->tag.s,outfile); break;
      }
      continue;
    }
    fputc(format[i++],outfile);
  }
}

void
reliq_print(FILE *outfile, const reliq_hnode *hnode)
{
  fwrite(hnode->all.b,1,hnode->all.s,outfile);
  fputc('\n',outfile);
}

static reliq_error *
format_comp(char *src, size_t *pos, size_t *size,
#ifdef RELIQ_EDITING
  reliq_format_func **format,
#else
  char **format,
#endif
  size_t *formatl)
{
  reliq_error *err;
  if (*pos >= *size || !src)
    return NULL;
  #ifndef RELIQ_EDITING
  while_is(isspace,src,*pos,*size);

  if (src[*pos] != '"' && src[*pos] != '\'')
    return NULL;

  size_t start,len;
  err = get_quoted(src,pos,size,' ',&start,&len);
  if (err)
    return err;

  if (len) {
    *format = memdup(src+start,len);
    *formatl = len;
  }
  #else
  flexarr *f = flexarr_init(sizeof(reliq_format_func),8);
  err = format_get_funcs(f,src,pos,size);
  flexarr_conv(f,(void**)format,formatl);
  if (err)
    return err;
  #endif
  return NULL;
}

static reliq_error *
match_function_handle(char *src, size_t *pos, size_t *size, flexarr *hooks)
{
  reliq_error *err;
  size_t p = *pos;

  while (*pos < *size && isalpha(src[*pos]))
    (*pos)++;

  size_t func_len = *pos-p;

  if (!func_len || src[*pos] != '@') {
    *pos = p;
    return NULL;
  }
  if (*pos+1 >= *size)
    return reliq_set_error(1,"hook \"%.*s\" expected argument",func_len,src+p);
  (*pos)++;

  char found = 0;
  size_t i = 0;
  for (; i < LENGTH(match_functions); i++) {
    if (memcomp(match_functions[i].name.b,src+p,match_functions[i].name.s,func_len)) {
      found = 1;
      break;
    }
  }
  if (!found)
    return reliq_set_error(1,"hook \"%.*s\" does not exists",(int)func_len,src+p);

  uchar islist = 0;
  reliq_hook hook;
  hook.flags = match_functions[i].flags;

  if (src[*pos] == '[') {
    if (hook.flags&F_PATTERN)
      return reliq_set_error(1,"hook \"%.*s\" expected regex argument",(int)func_len,src+p);

    islist = 1;
    err = range_comp(src,pos,*size,&hook.match.range);
  } else {
    if (hook.flags&F_RANGE)
      return reliq_set_error(1,"hook \"%.*s\" expected list argument",(int)func_len,src+p);

    err = reliq_regcomp(&hook.match.pattern,src,pos,size,' ',"uWcas");
  }
  if (err)
    return err;

  if (!islist && !hook.match.pattern.range.s && hook.match.pattern.flags&RELIQ_PATTERN_ALL) { //ignore if it matches everything
    reliq_regfree(&hook.match.pattern);
    return NULL;
  }

  memcpy(flexarr_inc(hooks),&hook,sizeof(hook));
  return NULL;
}

static reliq_error *
get_pattribs(char *src, size_t *size, struct reliq_pattrib **attrib, size_t *attribl, reliq_hook **hooks, size_t *hooksl, reliq_range *range)
{
  reliq_error *err = NULL;
  struct reliq_pattrib pa;
  flexarr *pattrib = flexarr_init(sizeof(struct reliq_pattrib),PATTRIB_INC);
  flexarr *phooks = flexarr_init(sizeof(reliq_hook),HOOK_INC);

  uchar tofree = 0;

  for (size_t i = 0; i < *size;) {
    while_is(isspace,src,i,*size);
    if (i >= *size)
      break;

    memset(&pa,0,sizeof(struct reliq_pattrib));
    char isset = 0;
    uchar isattrib = 0;
    tofree = 1;

    if (isalpha(src[i])) {
      size_t prev = i;
      err = match_function_handle(src,&i,size,phooks);
      if (err)
        break;
      if (i != prev) {
        tofree = 0;
        continue;
      }
    }

    if (src[i] == '+') {
      isattrib = 1;
      pa.flags |= A_INVERT;
      isset = 1;
      i++;
    } else if (src[i] == '-') {
      isattrib = 1;
      isset = -1;
      pa.flags &= ~A_INVERT;
      i++;
    } else if (src[i] == '\\' && (src[i+1] == '+' || src[i+1] == '-'))
      i++;

    char shortcut = 0;
    if (i >= *size)
      break;

    if (src[i] == '.' || src[i] == '#') {
      shortcut = src[i++];
    } else if (src[i] == '\\' && (src[i+1] == '.' || src[i+1] == '#'))
      i++;

    while_is(isspace,src,i,*size);
    if (i >= *size)
      break;

    if (src[i] == '[') {
      err = range_comp(src,&i,*size,&pa.position);
      if (err)
        break;
      if (!isattrib && (i >= *size || isspace(src[i]))) {
        if (range->s) {
          err = reliq_set_error(1,"node: position already declared");
          break;
        }
        memcpy(range,&pa.position,sizeof(*range));
        tofree = 0;
        continue;
      }
    } else if (src[i] == '\\' && src[i+1] == '[')
      i++;

    if (i >= *size)
      break;

    if (isset == 0)
      pa.flags |= A_INVERT;

    if (shortcut == '.' || shortcut == '#') {
      char *t_name = (shortcut == '.') ? "class" : "id";
      size_t t_pos=0,t_size=(shortcut == '.' ? 5 : 2);
      if ((err = reliq_regcomp(&pa.r[0],t_name,&t_pos,&t_size,' ',"uWsfi")))
        break;

      if ((err = reliq_regcomp(&pa.r[1],src,&i,size,' ',"uwsf")))
        break;
      pa.flags |= A_VAL_MATTERS;
    } else {
      if ((err = reliq_regcomp(&pa.r[0],src,&i,size,'=',NULL))) //!
        break;

      while_is(isspace,src,i,*size);
      if (i >= *size)
        goto ADD;
      if (src[i] == '=') {
        i++;
        while_is(isspace,src,i,*size);
        if (i >= *size)
          break;

        if ((err = reliq_regcomp(&pa.r[1],src,&i,size,' ',NULL)))
          break;
        pa.flags |= A_VAL_MATTERS;
      } else {
        pa.flags &= ~A_VAL_MATTERS;
        goto ADD_SKIP;
      }
    }

    ADD: ;
    if (src[i] != '+' && src[i] != '-')
      i++;
    ADD_SKIP: ;
    memcpy(flexarr_inc(pattrib),&pa,sizeof(struct reliq_pattrib));
    tofree = 0;
  }
  if (tofree)
    pattrib_free(&pa);

  flexarr_conv(pattrib,(void**)attrib,attribl);
  flexarr_conv(phooks,(void**)hooks,hooksl);
  return err;
}

reliq_error *
reliq_ncomp(const char *script, size_t size, reliq_node *node)
{
  reliq_error *err = NULL;
  memset(node,0,sizeof(reliq_node));
  if (!size) {
    node->flags |= N_EMPTY;
    return NULL;
  }

  char *nscript = memdup(script,size);
  size_t pos=0;

  for (size_t i=0; ; i++) { //execute 2 times but stop midway
    while_is(isspace,nscript,pos,size);
    if (pos >= size) {
      node->flags |= N_EMPTY;
      goto END;
    }
    if (i)
      break;

    if (nscript[pos] == '[') {
      err = range_comp(nscript,&pos,size,&node->position);
      if (err)
          return err;
      node->flags |= N_POSITION_ABSOLUTE;
    }
  }

  if ((err = reliq_regcomp(&node->tag,nscript,&pos,&size,' ',NULL)))
    goto END;

  size_t s = size-pos;
  err = get_pattribs(nscript+pos,&s,&node->attribs,&node->attribsl,&node->hooks,&node->hooksl,&node->position);
  size = s+pos;

  END: ;
  if (err)
    reliq_nfree(node);
  free(nscript);
  return err;
}

/*void //just for debugging
reliq_expr_print(flexarr *exprs, size_t tab)
{
  reliq_expr *a = (reliq_expr*)exprs->v;
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
  fprintf(stderr,"%% %lu",exprs->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < exprs->size; i++) {
    for (size_t j = 0; j < tab; j++)
      fputs("  ",stderr);
    if (a[i].flags&EXPR_TABLE) {
      fprintf(stderr,"%%x %d node(%u) expr(%u)\n",a[i].flags,a[i].nodefl,a[i].exprfl);
      reliq_expr_print((flexarr*)a[i].e,tab);
    } else {
      fprintf(stderr,"%%j node(%u) expr(%u)\n",a[i].nodefl,a[i].exprfl);
    }
  }
}*/

static void
reliq_expr_free(reliq_expr *expr)
{
  #ifdef RELIQ_EDITING
  format_free(expr->nodef,expr->nodefl);
  format_free(expr->exprf,expr->exprfl);
  #else
  if (expr->nodef)
    free(expr->nodef);
  #endif
  reliq_nfree((reliq_node*)expr->e);
}

static void
reliq_exprs_free_pre(flexarr *exprs)
{
  if (!exprs)
    return;
  reliq_expr *e = (reliq_expr*)exprs->v;
  for (size_t i = 0; i < exprs->size; i++) {
    if (e[i].flags&EXPR_TABLE) {
      reliq_exprs_free_pre((flexarr*)e[i].e);
    } else
      reliq_expr_free(&e[i]);
  }
  flexarr_free(exprs);
}

void
reliq_efree(reliq_exprs *exprs)
{
  for (size_t i = 0; i < exprs->s; i++) {
    if (exprs->b[i].flags&EXPR_TABLE) {
      reliq_exprs_free_pre((flexarr*)exprs->b[i].e);
    } else
      reliq_expr_free(&exprs->b[i]);
  }
  free(exprs->b);
}

static flexarr *
reliq_ecomp_pre(const char *csrc, size_t *pos, size_t s, reliq_error **err)
{
  if (s == 0)
    return NULL;

  size_t tpos = 0;
  if (pos == NULL)
    pos = &tpos;

  flexarr *ret = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  reliq_expr *acurrent = (reliq_expr*)flexarr_inc(ret);
  acurrent->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
  acurrent->flags = EXPR_TABLE|EXPR_NEWCHAIN;
  reliq_expr expr;
  char *src = memdup(csrc,s);
  size_t exprl;
  size_t i=*pos,first_pos=*pos;
  enum {
    typePassed,
    typeNextNode,
    typeGroupStart,
    typeGroupEnd
  } next = typePassed;

  reliq_str nodef,exprf;

  while_is(isspace,src,*pos,s);
  for (size_t j; i < s; i++) {
    j = i;

    if (next == typeNextNode) {
      first_pos = j;
      next = typePassed;
    }

    uchar hasexpr=0,hasended=0;
    reliq_expr *new = NULL;
    exprl = 0;

    REPEAT: ;

    nodef.b = NULL;
    nodef.s = 0;
    exprf.b = NULL;
    exprf.s = 0;
    while (i < s) {
      if (i+1 < s && src[i] == '\\' && src[i+1] == '\\') {
        i += 2;
        continue;
      }
      if (i+1 < s && src[i] == '\\' && (src[i+1] == ',' || src[i+1] == ';' ||
        src[i+1] == '"' || src[i+1] == '\'' || src[i+1] == '{' || src[i+1] == '}')) {
        delchar(src,i++,&s);
        exprl = (i-j)-nodef.s-((nodef.b) ? 1 : 0);
        continue;
      }
      if ((i == j || (i && isspace(src[i-1]))) && !exprf.b &&
        ((src[i] == '|' && !nodef.b) || src[i] == '/')) {
        if (i == j)
          hasexpr = 1;
        if (src[i] == '|') {
          nodef.b = src+(++i);
        } else {
          if (nodef.b)
            nodef.s = i-(nodef.b-src);
          exprf.b = src+(++i);
        }
        continue;
      }

      if (src[i] == '"' || src[i] == '\'') {
        char tf = src[i++];
        while (i < s && src[i] != tf) {
          if (src[i] == '\\' && (src[i+1] == '\\' || src[i+1] == tf))
            i++;
          i++;
        }
        if (i < s && src[i] == tf) {
          i++;
          if (i < s)
            continue;
        } else
          goto PASS;
      }
      if (src[i] == '[') {
        i++;
        while (i < s && src[i] != ']')
          i++;
        if (i < s && src[i] == ']') {
          i++;
          if (i < s)
            continue;
        } else
          goto PASS;
      }

      if (src[i] == ',' || src[i] == ';' || src[i] == '{' || src[i] == '}') {
        if (exprf.b) {
          exprf.s = i-(exprf.b-src);
        } else if (nodef.b)
          nodef.s = i-(nodef.b-src);

        if (src[i] == '{') {
          next = typeGroupStart;
        } else if (src[i] == '}') {
          next = typeGroupEnd;
        } else {
          next = (src[i] == ',') ? typeNextNode : typePassed;
          exprl = i-j;
          exprl -= nodef.s+((nodef.b) ? 1 : 0);
          exprl -= exprf.s+((exprf.b) ? 1 : 0);
        }
        i++;
        break;
      }
      i++;
      if (!nodef.b && !exprf.b)
        exprl = i-j;
    }

    PASS: ;
    if (j+exprl > s)
      exprl = s-j;
    if (i > s)
      i = s;
    expr.nodef = NULL;
    expr.nodefl = 0;
    if (!nodef.s)
      nodef.s = i-(nodef.b-src);

    #ifdef RELIQ_EDITING
    expr.exprf = NULL;
    expr.exprfl = 0;
    if (!exprf.s)
      exprf.s = i-(exprf.b-src);
    #endif

    if (nodef.b) {
      size_t g=0,t=nodef.s;
      *err = format_comp(nodef.b,&g,&t,&expr.nodef,&expr.nodefl);
      s -= nodef.s-t;
      if (*err)
        goto EXIT1;
      if (hasended) {
        new->flags |= EXPR_SINGULAR;
        new->nodef = expr.nodef;
        new->nodefl = expr.nodefl;
      }
    }
    #ifdef RELIQ_EDITING
    if (exprf.b) {
      size_t g=0,t=exprf.s;
      *err = format_comp(exprf.b,&g,&t,&expr.exprf,&expr.exprfl);
      s -= exprf.s-t;
      if (*err)
        goto EXIT1;
      if (hasended) {
        new->exprf = expr.exprf;
        new->exprfl = expr.exprfl;
      }
    }
    #endif

    if (hasended) {
      if (next == typeGroupEnd)
        goto END_BRACKET;
      if (next == typeNextNode)
        goto NEXT_NODE;
      continue;
    }

    if ((next != typeGroupEnd || src[j] != '}') && (next == typeGroupStart
        || next == typeGroupEnd || exprl || hasexpr)) {
      expr.e = malloc(sizeof(reliq_node));
      expr.flags = 0;

      if (!exprl) {
        ((reliq_node*)expr.e)->flags |= N_EMPTY;
      } else if (next != typeGroupStart) {
        //fprintf(stderr,"lllllllllll %lu %lu\n",j,first_pos);
        if (j == first_pos) {
          size_t g = j;
          while_is(isspace,src,g,s);
          if (((g+1 < s && isspace(src[g])) || g < s) && src[g] == '.') {
            g++;
            expr.flags |= EXPR_OUTPROTECTED;
            exprl -= g-j;
            j = g;
          }
        }
        *err = reliq_ncomp(src+j,exprl,(reliq_node*)expr.e);
        if (*err)
          goto EXIT1;
      }

      new = (reliq_expr*)flexarr_inc(acurrent->e);
      *new = expr;
    }

    if (next == typeGroupStart) {
      new->flags |= EXPR_TABLE|EXPR_NEWBLOCK;
      next = typePassed;
      *pos = i;
      new->e = reliq_ecomp_pre(src,pos,s,err);
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
        } else if (src[i] == ';' || src[i] == '{') {
          continue;
        } else if (src[i] == '|' || src[i] == '/') {
           hasended = 1;
           goto REPEAT;
        }
      }
    } /*else if (new)
      new->flags = 0;*/

    if (next == typeNextNode) {
      NEXT_NODE: ;
      acurrent = (reliq_expr*)flexarr_inc(ret);
      acurrent->e = flexarr_init(sizeof(reliq_expr),PATTERN_SIZE_INC);
      acurrent->flags |= EXPR_TABLE|EXPR_NEWCHAIN;
    }
    if (next == typeGroupEnd)
      goto END_BRACKET;

    while_is(isspace,src,i,s);
    i--;
  }

  END_BRACKET: ;
  *pos = i;
  flexarr_clearb(ret);
  EXIT1:
  free(src);
  if (*err) {
    reliq_exprs_free_pre(ret);
    return NULL;
  }
  return ret;
}

reliq_error *
reliq_ecomp(const char *src, size_t size, reliq_exprs *exprs)
{
  reliq_error *err = NULL;
  flexarr *ret = reliq_ecomp_pre(src,NULL,size,&err);
  if (ret) {
    //reliq_expr_print(ret,0);
    flexarr_conv(ret,(void**)&exprs->b,&exprs->s);
  } else
    exprs->s = 0;
  return err;
}

static void
dest_match_position(const reliq_range *range, flexarr *dest, const size_t start, const size_t end) {
  reliq_compressed *x = (reliq_compressed*)dest->v;
  size_t found = start;
  for (size_t i = start; i < end; i++) {
    if (!range_match(i-start,range,end-start))
      continue;
    if (found != i)
      x[found] = x[i];
    found++;
  }
  dest->size = found;
}

static void
add_compressed_blank(flexarr *dest)
{
  memset(flexarr_inc(dest),-1,sizeof(reliq_compressed)); //set everything to -1
}

static void
first_match(reliq *rq, reliq_node *node, flexarr *dest)
{
  reliq_hnode *nodes = rq->nodes;
  size_t nodesl = rq->nodesl;
  for (size_t i = 0; i < nodesl; i++) {
    if (reliq_match(&nodes[i],NULL,node)) {
      reliq_compressed *x = (reliq_compressed*)flexarr_inc(dest);
      x->parentid = -1;
      x->id = i;
    }
  }
  if (node->position.s)
    dest_match_position(&node->position,dest,0,dest->size);
}

static void
node_exec(reliq *rq, reliq_node *node, flexarr *source, flexarr *dest)
{
  if (source->size == 0) {
    first_match(rq,node,dest);
    return;
  }

  reliq_hnode *nodes = rq->nodes;
  size_t current,n;
  for (size_t i = 0; i < source->size; i++) {
    current = ((reliq_compressed*)source->v)[i].id;
    size_t prevdestsize = dest->size;
    for (size_t j = 0; j <= nodes[current].child_count; j++) {
      n = current+j;
      if (reliq_match(&nodes[n],&nodes[current],node)) {
        reliq_compressed *x = (reliq_compressed*)flexarr_inc(dest);
        x->id = n;
        x->parentid = current;
      }
    }
    if (!(node->flags&N_POSITION_ABSOLUTE) && node->position.s)
      dest_match_position(&node->position,dest,prevdestsize,dest->size);
  }
  if (node->flags&N_POSITION_ABSOLUTE && node->position.s)
    dest_match_position(&node->position,dest,0,dest->size);
}

/*static reliq_error *
ncollector_check(flexarr *ncollector, size_t correctsize)
{
  size_t ncollector_sum = 0;
  reliq_cstr *pcol = (reliq_cstr*)ncollector->v;
  for (size_t j = 0; j < ncollector->size; j++)
    ncollector_sum += pcol[j].s;
  if (ncollector_sum != correctsize)
    return reliq_set_error(1,"ncollector does not match count of found tags, %lu != %lu",ncollector_sum,correctsize);
  return NULL;
}*/

static void
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const reliq_expr *lastnode, uchar istable, uchar useformat)
{
  if (!source->size)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (useformat && !lastnode)
    goto END;
  if (istable&EXPR_TABLE) {
    if (startn != lastn) { //truncate previously added, now useless ncollector
      for (size_t i = lastn; i < ncollector->size; i++)
        ((reliq_cstr*)ncollector->v)[i-lastn] = ((reliq_cstr*)ncollector->v)[i];
      ncollector->size -= lastn-startn;
    }
  } else {
    ncollector->size = startn;
    *(reliq_cstr*)flexarr_inc(ncollector) = (reliq_cstr){(char const*)lastnode,dest->size-prevsize};
  }
  END: ;
  source->size = 0;
}

#ifdef RELIQ_EDITING
static void
fcollector_add(const size_t lastn, const uchar isnodef, const reliq_expr *expr, flexarr *ncollector, flexarr *fcollector)
{
  struct fcollector_expr *fcols = (struct fcollector_expr*)fcollector->v;
  for (size_t i = fcollector->size; i > 0; i--) {
    if (fcols[i-1].start < lastn)
      break;
    fcols[i-1].lvl++;
  }
  *(struct fcollector_expr*)flexarr_inc(fcollector) = (struct fcollector_expr){expr,lastn,ncollector->size-1,0,isnodef};
}
#endif

static reliq_error *reliq_exec_pre(reliq *rq, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    );

static reliq_error *
reliq_exec_table(reliq *rq, const reliq_expr *expr, flexarr *source, flexarr *dest, flexarr **out, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    )
{
  flexarr *exprs = (flexarr*)expr->e;
  reliq_expr *exprsv = (reliq_expr*)exprs->v;
  size_t exprsl = exprs->size;

  if (source->size && expr->flags&EXPR_SINGULAR && expr->nodefl) {
    reliq_error *err = NULL;
    flexarr *in = flexarr_init(sizeof(reliq_compressed),1);
    reliq_compressed *inv = (reliq_compressed*)flexarr_inc(in);
    reliq_compressed *sourcev = (reliq_compressed*)source->v;

    for (size_t i = 0; i < source->size; i++) {
      *inv = sourcev[i];
      #ifdef RELIQ_EDITING
      size_t lastn = ncollector->size;
      #endif
      err = reliq_exec_pre(rq,exprsv,exprsl,in,dest,out,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        );
      if (err)
        break;
      #ifdef RELIQ_EDITING
      if (ncollector->size-lastn)
        fcollector_add(lastn,1,expr,ncollector,fcollector);
      #endif
    }
    flexarr_free(in);

    return err;
  }
  return reliq_exec_pre(rq,exprsv,exprsl,source,dest,out,ncollector
      #ifdef RELIQ_EDITING
      ,fcollector
      #endif
      );
}

static reliq_error *
reliq_exec_pre(reliq *rq, const reliq_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr **out, flexarr *ncollector
    #ifdef RELIQ_EDITING
    , flexarr *fcollector
    #endif
    )
{
  flexarr *buf[3];
  reliq_error *err;

  buf[0] = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  if (source) {
    buf[0]->asize = source->asize;
    buf[0]->size = source->size;
    buf[0]->v = memdup(source->v,source->asize*sizeof(reliq_compressed));
  }

  buf[1] = flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  buf[2] = dest ? dest : flexarr_init(sizeof(reliq_compressed),PASSED_INC);
  flexarr *buf_unchanged[2];
  buf_unchanged[0] = buf[0];
  buf_unchanged[1] = buf[1];

  size_t startn = ncollector->size;
  size_t lastn = startn;

  reliq_expr const *lastnode = NULL;

  uchar outprotected = 0;

  for (size_t i = 0; i < exprsl; i++) {
    if (exprs[i].flags&EXPR_OUTPROTECTED)
      outprotected = 1;

    if (exprs[i].flags&EXPR_TABLE) {
      lastn = ncollector->size;
      err = reliq_exec_table(rq,&exprs[i],buf[0],buf[1],out,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        );
      if (err)
        goto ERR;
    } else if (exprs[i].e) {
      lastnode = &exprs[i];
      reliq_node *node = (reliq_node*)exprs[i].e;
      node_exec(rq,node,buf[0],buf[1]);

      if (outprotected && buf[1]->size == 0) {
        add_compressed_blank(buf[1]);
        ncollector_add(ncollector,buf[2],buf[1],startn,lastn,NULL,exprs[i].flags,0);
        break;
      }
    }

    //fprintf(stderr,"ematch %p %lu %lu %lu %u %lu\n",exprs,startn,lastn,ncollector->size,(exprs[i].flags&EXPR_NEWBLOCK),exprs[i].exprfl);

    #ifdef RELIQ_EDITING
    if (exprs[i].flags&EXPR_NEWBLOCK && exprs[i].exprfl)
      fcollector_add(lastn,0,&exprs[i],ncollector,fcollector);
    #endif

    if ((exprs[i].flags&EXPR_TABLE &&
      !(exprs[i].flags&EXPR_NEWBLOCK)) || i == exprsl-1) {
      ncollector_add(ncollector,buf[2],buf[1],startn,lastn,lastnode,exprs[i].flags,1);
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
    /*err = ncollector_check(ncollector,buf[2]->size);
    if (err) //just for debugging
      goto ERR;*/

    if (rq->output) {
      err = nodes_output(rq,buf[2],ncollector
          #ifdef RELIQ_EDITING
          ,fcollector
          #endif
          );
      if (err)
        goto ERR;
      flexarr_free(buf[2]);
    } else
      *out = buf[2];
  }

  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);
  return NULL;

  ERR: ;
  flexarr_free(buf_unchanged[0]);
  flexarr_free(buf_unchanged[1]);
  flexarr_free(buf[2]);
  buf[2] = NULL;
  return err;
}

reliq_error *
reliq_exec_r(reliq *rq, FILE *output, reliq_compressed **outnodes, size_t *outnodesl, const reliq_exprs *exprs)
{
    flexarr *fsource=NULL,*fdest=NULL,*compressed=NULL;
    rq->output = output;
    reliq_error *err;

    flexarr *ncollector = flexarr_init(sizeof(reliq_cstr),NCOLLECTOR_INC);
    #ifdef RELIQ_EDITING
    flexarr *fcollector = flexarr_init(sizeof(struct fcollector_expr),FCOLLECTOR_INC);
    #endif

    err = reliq_exec_pre(rq,exprs->b,exprs->s,fsource,fdest,&compressed,ncollector
        #ifdef RELIQ_EDITING
        ,fcollector
        #endif
        );
    if (!output && compressed)
      flexarr_conv(compressed,(void**)outnodes,outnodesl);

    flexarr_free(ncollector);
    #ifdef RELIQ_EDITING
    flexarr_free(fcollector);
    #endif
    return err;
}

reliq_error *
reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_exprs *exprs)
{
  return reliq_exec_r(rq,NULL,nodes,nodesl,exprs);
}

reliq_error *
reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs)
{
  return reliq_exec_r(rq,output,NULL,0,exprs);
}

reliq_error *
reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs)
{
  FILE *output = open_memstream(str,strl);
  reliq_error *err = reliq_exec_file(rq,output,exprs);
  fclose(output);
  return err;
}

static reliq_error *
reliq_analyze(const char *ptr, const size_t size, flexarr *nodes, reliq *rq)
{
  reliq_error *err;
  for (size_t i = 0; i < size; i++) {
    while (i < size && ptr[i] != '<')
        i++;
    while (i < size && ptr[i] == '<') {
      html_struct_handle(ptr,&i,size,0,nodes,rq,&err);
      if (err)
        return err;
    }
  }
  return NULL;
}

reliq_error *
reliq_fmatch_file(const char *ptr, const size_t size, FILE *output, const reliq_node *node,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl)
{
  reliq t;
  t.data = ptr;
  t.size = size;
  t.expr = node;
  t.nodef = nodef;
  t.nodefl = nodefl;
  t.flags = 0;
  if (output == NULL)
    output = stdout;
  t.output = output;
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);
  reliq_error *err = reliq_analyze(ptr,size,nodes,&t);
  flexarr_free(nodes);
  t.nodes = NULL;
  t.nodesl = 0;
  flexarr_free((flexarr*)t.attrib_buffer);
  return err;
}

reliq_error *
reliq_fmatch_str(const char *ptr, const size_t size, char **str, size_t *strl, const reliq_node *node,
#ifdef RELIQ_EDITING
  reliq_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl)
{
  FILE *output = open_memstream(str,strl);
  reliq_error *err = reliq_fmatch_file(ptr,size,output,node,nodef,nodefl);
  fclose(output);
  return err;
}

static reliq_error *
exprs_check_chain(const reliq_exprs *exprs)
{
  if (exprs->s > 1)
    goto ERR;

  flexarr *chain = exprs->b[0].e;
  reliq_expr *chainv = (reliq_expr*)chain->v;

  for (size_t i = 0; i < chain->size; i++)
    if (chainv[i].flags&EXPR_TABLE)
      goto ERR;

  return NULL;
  ERR: ;
  return reliq_set_error(1,"expression is not a chain");
}

reliq_error *
reliq_fexec_file(char *ptr, size_t size, FILE *output, const reliq_exprs *exprs, int (*freeptr)(void *ptr, size_t size))
{
  if (exprs->s == 0)
    return NULL;
  reliq_error *err = exprs_check_chain(exprs);
  if (err)
    return err;

  FILE *destination = output;
  char *nptr;
  size_t fsize;

  flexarr *chain = (flexarr*)exprs->b[0].e;
  reliq_expr *chainv = (reliq_expr*)chain->v;

  for (size_t i = 0; i < chain->size; i++) {
    output = (i == chain->size-1) ? destination : open_memstream(&nptr,&fsize);

    err = reliq_fmatch_file(ptr,size,output,(reliq_node*)chainv[i].e,
      chainv[i].nodef,chainv[i].nodefl);

    fflush(output);

    if (i == 0) {
      if (freeptr)
        (*freeptr)(ptr,size);
    } else
      free(ptr);

    if (i != chain->size-1)
      fclose(output);

    if (err)
      return err;

    ptr = nptr;
    size = fsize;
  }
  return NULL;
}

reliq_error *
reliq_fexec_str(char *ptr, size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freeptr)(void *ptr, size_t size))
{
  FILE *output = open_memstream(str,strl);
  reliq_error *err = reliq_fexec_file(ptr,size,output,exprs,freeptr);
  fclose(output);
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
  for (size_t i = 0; i < node->attribsl; i++) {
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
  for (size_t i = 0; i < node->attribsl; i++) {
    shift_cstr_p(node->attribs[i].f);
    shift_cstr_p(node->attribs[i].s);
  }
}

reliq
reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl, char **ptr, size_t *size, const reliq *rq)
{
  reliq t;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;

  size_t id,pos=0;
  ushort lvl;
  FILE *out = open_memstream(ptr,size);
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    id = compressed[i].id;
    if (id == (size_t)-1)
      continue;

    current = &rq->nodes[id];
    lvl = current->lvl;

    for (size_t j = 0; j <= rq->nodes[id].child_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,&rq->nodes[id+j],sizeof(reliq_hnode));

      size_t tpos = pos+(new->all.b-current->all.b);

      reliq_hnode_shift(new,tpos);
      new->lvl -= lvl;
    }

    fwrite(current->all.b,1,current->all.s,out);
    pos += current->all.s;
  }

  fclose(out);

  for (size_t i = 0; i < nodes->size; i++)
    reliq_hnode_shift_finalize(&((reliq_hnode*)nodes->v)[i],*ptr);

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  t.data = *ptr;
  t.size = *size;
  return t;
}

reliq
reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq)
{
  reliq t;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;
  t.data = rq->data;
  t.size = rq->size;

  size_t id;
  ushort lvl;
  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  reliq_hnode *current,*new;

  for (size_t i = 0; i < compressedl; i++) {
    id = compressed[i].id;
    if (id == (size_t)-1)
      continue;

    current = &rq->nodes[id];
    lvl = current->lvl;

    for (size_t j = 0; j <= rq->nodes[id].child_count; j++) {
      new = (reliq_hnode*)flexarr_inc(nodes);
      memcpy(new,&rq->nodes[id+j],sizeof(reliq_hnode));
      new->lvl -= lvl;
    }
  }

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  return t;
}

reliq
reliq_init(const char *ptr, const size_t size)
{
  reliq t;
  t.data = ptr;
  t.size = size;
  t.expr = NULL;
  t.flags = RELIQ_SAVE;
  t.output = NULL;

  flexarr *nodes = flexarr_init(sizeof(reliq_hnode),RELIQ_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(reliq_cstr_pair),ATTRIB_INC);

  reliq_analyze(ptr,size,nodes,&t);

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  flexarr_free((flexarr*)t.attrib_buffer);
  return t;
}
