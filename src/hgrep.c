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

#define PASSED_INC (1<<14)
#define PATTERN_SIZE_INC (1<<8)
#define PATTRIB_INC 8
#define HOOK_INC 8

#define UINT_TO_STR_MAX 32

//hgrep_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//hgrep_node flags
#define P_EMPTY 0x4

//hgrep_match_function flags
#define F_KINDS 0x7

#define F_ATTRIBUTES 0x1
#define F_LEVEL 0x2
#define F_CHILD_COUNT 0x3
#define F_MATCH_INSIDES 0x4

#define F_RANGE 0x8
#define F_REGEX 0x10

//hgrep_expr flags
#define EXPR_TABLE 0x1
#define EXPR_NEWBLOCK 0x2
#define EXPR_NEWCHAIN 0x4
#define EXPR_SINGULAR 0x8

#define ATTRIB_INC (1<<3)
#define HGREP_NODES_INC (1<<13)

//hgrep_pattern flags
#define HGRP_PATTERN_TRIM 0x2
#define HGRP_PATTERN_CASE_INSENSITIVE 0x4
#define HGRP_PATTERN_INVERT 0x8

#define HGRP_PATTERN_MATCH 0x70

#define HGRP_PATTERN_MATCH_FULL 0x10
#define HGRP_PATTERN_MATCH_ALL 0x20
#define HGRP_PATTERN_MATCH_WORD 0x30
#define HGRP_PATTERN_MATCH_BEGINNING 0x40
#define HGRP_PATTERN_MATCH_ENDING 0x50

#define HGRP_PATTERN_TYPE 0x180

#define HGRP_PATTERN_TYPE_STR 0x80
#define HGRP_PATTERN_TYPE_BRE 0x100
#define HGRP_PATTERN_TYPE_ERE 0x180

#define HGRP_PATTERN_EMPTY 0x200
#define HGRP_PATTERN_ALL 0x400

struct hgrep_match_function {
  hgrep_str8 name;
  ushort flags;
};

const struct hgrep_match_function match_functions[] = {
    {{"m",1},F_REGEX|F_MATCH_INSIDES},
    {{"a",1},F_RANGE|F_ATTRIBUTES},
    {{"l",1},F_RANGE|F_LEVEL},
    {{"c",1},F_RANGE|F_CHILD_COUNT},

    {{"match",5},F_REGEX|F_MATCH_INSIDES},
    {{"attributes",10},F_RANGE|F_ATTRIBUTES},
    {{"level",5},F_RANGE|F_LEVEL},
    {{"children",8},F_RANGE|F_CHILD_COUNT},
};

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
hgrep_regcomp_set_flags(ushort *flags, const char *src, const size_t len)
{
  for (size_t i = 0; i < len; i++) {
    switch (src[i]) {
      case 't':
          *flags |= HGRP_PATTERN_TRIM;
          break;
      case 'u':
          *flags &= ~HGRP_PATTERN_TRIM;
          break;
      case 'i':
          *flags |= HGRP_PATTERN_CASE_INSENSITIVE;
          break;
      case 'c':
          *flags &= ~HGRP_PATTERN_CASE_INSENSITIVE;
          break;
      case 'v':
          *flags |= HGRP_PATTERN_INVERT;
          break;
      case 'n':
          *flags &= ~HGRP_PATTERN_INVERT;
          break;
      case 'a':
          *flags &= ~HGRP_PATTERN_MATCH;
          *flags |= HGRP_PATTERN_MATCH_ALL;
          break;
      case 'f':
          *flags &= ~HGRP_PATTERN_MATCH;
          *flags |= HGRP_PATTERN_MATCH_FULL;
          break;
      case 'w':
          *flags &= ~HGRP_PATTERN_MATCH;
          *flags |= HGRP_PATTERN_MATCH_WORD;
          break;
      case 'b':
          *flags &= ~HGRP_PATTERN_MATCH;
          *flags |= HGRP_PATTERN_MATCH_BEGINNING;
          break;
      case 'e':
          *flags &= ~HGRP_PATTERN_MATCH;
          *flags |= HGRP_PATTERN_MATCH_ENDING;
          break;
      case 's':
          *flags &= ~HGRP_PATTERN_TYPE;
          *flags |= HGRP_PATTERN_TYPE_STR;
          break;
      case 'B':
          *flags &= ~HGRP_PATTERN_TYPE;
          *flags |= HGRP_PATTERN_TYPE_BRE;
          break;
      case 'E':
          *flags &= ~HGRP_PATTERN_TYPE;
          *flags |= HGRP_PATTERN_TYPE_ERE;
          break;
      /*case 'P':
          *flags &= ~HGRP_PATTERN_TYPE;
          *flags |= HGRP_PATTERN_TYPE_PCRE;
          break;*/
    }
  }
}

static void
hgrep_regcomp_get_flags(hgrep_pattern *pattern, const char *src, size_t *pos, const size_t size, const char *flags)
{
  size_t p = *pos;
  pattern->flags = HGRP_PATTERN_TRIM|HGRP_PATTERN_MATCH_FULL|HGRP_PATTERN_TYPE_STR;
  pattern->range.s= 0;

  if (flags)
    hgrep_regcomp_set_flags(&pattern->flags,flags,strlen(flags));

  if (src[p] == '\'' || src[p] == '"' || src[p] == '*')
    return;

  while (p < size && isalpha(src[p]))
    p++;
  if (p >= size || src[p] != '>')
    return;

  hgrep_regcomp_set_flags(&pattern->flags,src+*pos,p-*pos);

  *pos = p+1;
  return;
}

static hgrep_error *
hgrep_regcomp_add_pattern(hgrep_pattern *pattern, const char *src, const size_t size)
{
  ushort match = pattern->flags&HGRP_PATTERN_MATCH,
    type = pattern->flags&HGRP_PATTERN_TYPE;

  if (!size) {
    pattern->flags |= HGRP_PATTERN_EMPTY;
    return NULL;
  }

  if (type == HGRP_PATTERN_TYPE_STR) {
    pattern->match.str.b = memdup(src,size);
    pattern->match.str.s = size;
  } else {
    int regexflags = REG_NOSUB;
    if (pattern->flags&HGRP_PATTERN_CASE_INSENSITIVE)
      regexflags |= REG_ICASE;
    if (type == HGRP_PATTERN_TYPE_ERE)
      regexflags |= REG_EXTENDED;

    size_t addedspace = 0;
    uchar fullmatch =
      (match == HGRP_PATTERN_MATCH_FULL || match == HGRP_PATTERN_MATCH_WORD) ? 1 : 0;

    if (fullmatch)
      addedspace = 2;
    else if (match == HGRP_PATTERN_MATCH_BEGINNING || match == HGRP_PATTERN_MATCH_ENDING)
      addedspace = 1;

    char *tmp = malloc(size+addedspace+1);
    size_t p = 0;

    if (fullmatch || match == HGRP_PATTERN_MATCH_BEGINNING)
      tmp[p++] = '^';

    memcpy(tmp+p,src,size);
    p += size;

    if (fullmatch || match == HGRP_PATTERN_MATCH_ENDING)
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

static hgrep_error *
hgrep_regcomp(hgrep_pattern *pattern, char *src, size_t *pos, size_t *size, const char delim, const char *flags)
{
  hgrep_error *err;

  hgrep_regcomp_get_flags(pattern,src,pos,*size,flags);

  if (*pos && *pos < *size && src[*pos-1] == '>' && src[*pos] == '[') {
    err = range_comp(src,pos,*size,&pattern->range);
    if (err)
      return err;
    if (*pos >= *size || src[*pos] == delim || isspace(src[*pos])) {
      pattern->flags |= HGRP_PATTERN_ALL;
      return NULL;
    }
  }

  if (*pos < *size && src[*pos] == '*') {
    if (*pos+1 < *size)
      if (!isspace(src[*pos+1]) && src[*pos+1] != delim)
        goto NOT_ALL;
    (*pos)++;
    pattern->flags |= HGRP_PATTERN_ALL;
    return NULL;
  }

  NOT_ALL: ;

  size_t start,len;
  err = get_quoted(src,pos,size,delim,&start,&len);
  if (err)
    return err;

  return hgrep_regcomp_add_pattern(pattern,src+start,len);
}

static int
hgrep_regexec_match_word(const hgrep_pattern *pattern, hgrep_cstr *str)
{
  ushort type = pattern->flags&HGRP_PATTERN_TYPE;
  uchar icase = pattern->flags&HGRP_PATTERN_CASE_INSENSITIVE;
  const char *ptr = str->b;
  size_t plen = str->s;
  char const *saveptr,*word;
  size_t saveptrlen,wordlen;

  while (1) {
    memwordtok_r(ptr,plen,(void const**)&saveptr,&saveptrlen,(void const**)&word,&wordlen);
    if (!word)
      return 0;

    if (type == HGRP_PATTERN_TYPE_STR) {
      if (pattern->match.str.s == wordlen) {
        if (icase) {
          if (memcasecmp(word,pattern->match.str.b,wordlen) == 0)
            return 1;
        } else if (memcmp(word,pattern->match.str.b,wordlen) == 0)
          return 1;
      }
    } else {
      regmatch_t pmatch;
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)wordlen;

      if (regexec(&pattern->match.reg,word,1,&pmatch,REG_STARTEND) == 0)
        return 1;
    }

    ptr = NULL;
  }
}

static int
hgrep_regexec_match_str(const hgrep_pattern *pattern, hgrep_cstr *str)
{
  hgrep_pattern const *p = pattern;
  ushort match = p->flags&HGRP_PATTERN_MATCH;
  uchar icase = p->flags&HGRP_PATTERN_CASE_INSENSITIVE;

  if (!p->match.str.s)
    return 1;
  if (!str->s)
    return 0;

  switch (match) {
    case HGRP_PATTERN_MATCH_ALL:
      if (icase) {
        if (memcasemem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
          return 1;
      } else if (memmem(str->b,str->s,p->match.str.b,p->match.str.s) != NULL)
        return 1;
      break;
    case HGRP_PATTERN_MATCH_FULL:
      if (str->s != p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,str->s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,str->s) == 0)
        return 1;
      break;
    case HGRP_PATTERN_MATCH_BEGINNING:
      if (str->s < p->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,p->match.str.b,p->match.str.s) == 0)
          return 1;
      } else if (memcmp(str->b,p->match.str.b,p->match.str.s) == 0)
        return 1;
      break;
    case HGRP_PATTERN_MATCH_ENDING:
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
hgrep_regexec(const hgrep_pattern *pattern, const char *src, const size_t size)
{
  uchar invert = (pattern->flags&HGRP_PATTERN_INVERT) ? 1 : 0;
  if ((!range_match(size,&pattern->range,-1)))
    return invert;

  if (pattern->flags&HGRP_PATTERN_ALL)
    return !invert;

  if (pattern->flags&HGRP_PATTERN_EMPTY)
    return (size == 0) ? !invert : invert;

  ushort match = pattern->flags&HGRP_PATTERN_MATCH,
    type = pattern->flags&HGRP_PATTERN_TYPE;

  hgrep_cstr str = {src,size};

  if (match == HGRP_PATTERN_MATCH_WORD)
    return hgrep_regexec_match_word(pattern,&str)^invert;

  if (pattern->flags&HGRP_PATTERN_TRIM)
    memtrim((void const**)&str.b,&str.s,src,size);

  if (type == HGRP_PATTERN_TYPE_STR) {
    return hgrep_regexec_match_str(pattern,&str)^invert;
  } else {
    if (!str.s)
      return invert;

    regmatch_t pmatch;
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)str.s;

    if (regexec(&pattern->match.reg,str.b,1,&pmatch,REG_STARTEND) == 0)
      return !invert;
  }
  return invert;
}

static void
hgrep_regfree(hgrep_pattern *pattern)
{
  if (!pattern)
    return;

  range_free(&pattern->range);

  if (pattern->flags&HGRP_PATTERN_EMPTY || pattern->flags&HGRP_PATTERN_ALL)
    return;

  if ((pattern->flags&HGRP_PATTERN_TYPE) == HGRP_PATTERN_TYPE_STR) {
    if (pattern->match.str.b)
      free(pattern->match.str.b);
  } else
    regfree(&pattern->match.reg);
}

static void
pattrib_free(struct hgrep_pattrib *attrib) {
  if (!attrib)
    return;
  hgrep_regfree(&attrib->r[0]);
  if (attrib->flags&A_VAL_MATTERS)
    hgrep_regfree(&attrib->r[1]);
  range_free(&attrib->position);
}

static void
pattribs_free(struct hgrep_pattrib *attribs, size_t attribsl)
{
  for (size_t i = 0; i < attribsl; i++)
    pattrib_free(&attribs[i]);
  free(attribs);
}

static void
hgrep_free_hooks(hgrep_hook *hooks, const size_t hooksl)
{
  if (!hooksl || !hooks)
    return;
  for (size_t i = 0; i < hooksl; i++) {
    if (hooks[i].flags&F_RANGE) {
      range_free(&hooks[i].match.range);
    } else
      hgrep_regfree(&hooks[i].match.pattern);
  }
  free(hooks);
}

void
hgrep_nfree(hgrep_node *node)
{
  if (node == NULL || node->flags&P_EMPTY)
    return;

  hgrep_regfree(&node->tag);

  pattribs_free(node->attribs,node->attribsl);

  hgrep_free_hooks(node->hooks,node->hooksl);

  range_free(&node->position);
}

void
hgrep_free(hgrep *hg)
{
    if (hg == NULL)
      return;
    for (size_t i = 0; i < hg->nodesl; i++)
      free(hg->nodes[i].attribs);
    if (hg->nodesl)
      free(hg->nodes);
}

static int
pattrib_match(const hgrep_hnode *hgn, const struct hgrep_pattrib *attribs, size_t attribsl)
{
  const hgrep_cstr_pair *a = hgn->attribs;
  for (size_t i = 0; i < attribsl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hgn->attribsl; j++) {
      if (!range_match(j,&attribs[i].position,hgn->attribsl-1))
        continue;

      if (!hgrep_regexec(&attribs[i].r[0],a[j].f.b,a[j].f.s))
        continue;

      if (attribs[i].flags&A_VAL_MATTERS && !hgrep_regexec(&attribs[i].r[1],a[j].s.b,a[j].s.s))
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
hgrep_match_hooks(const hgrep_hnode *hgn, const hgrep_hook *hooks, const size_t hooksl)
{
  for (size_t i = 0; i < hooksl; i++) {
    char const *src = NULL;
    size_t srcl = 0;

    switch (hooks[i].flags&F_KINDS) {
      case F_ATTRIBUTES:
        srcl = hgn->attribsl;
        break;
      case F_LEVEL:
        srcl = hgn->lvl;
        break;
      case F_CHILD_COUNT:
        srcl = hgn->child_count;
        break;
      case F_MATCH_INSIDES:
        src = hgn->insides.b;
        srcl = hgn->insides.s;
        break;
    }

    if (hooks[i].flags&F_RANGE) {
      if (!range_match(srcl,&hooks[i].match.range,-1))
        return 0;
    } else if (!hgrep_regexec(&hooks[i].match.pattern,src,srcl))
      return 0;
  }
  return 1;
}

int
hgrep_match(const hgrep_hnode *hgn, const hgrep_node *node)
{
  if (node->flags&P_EMPTY)
    return 1;

  if (!hgrep_regexec(&node->tag,hgn->tag.b,hgn->tag.s))
    return 0;

  if (!hgrep_match_hooks(hgn,node->hooks,node->hooksl))
    return 0;

  if (!pattrib_match(hgn,node->attribs,node->attribsl))
    return 0;

  return 1;
}

static void
print_trimmed_if(const hgrep_cstr *str, const uchar trim, FILE *outfile)
{
  char const *dest = str->b;
  size_t destl = str->s;
  if (trim)
    memtrim((void const**)&dest,&destl,str->b,str->s);
  if (destl)
    fwrite(dest,1,destl,outfile);
}

static void
print_attribs(const hgrep_hnode *hgn, const uchar trim, FILE *outfile)
{
  hgrep_cstr_pair *a = hgn->attribs;
  for (ushort j = 0; j < hgn->attribsl; j++) {
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
print_attrib_value(const hgrep_cstr_pair *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uchar trim, FILE *outfile)
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

void
hgrep_printf(FILE *outfile, const char *format, const size_t formatl, const hgrep_hnode *hgn, const char *reference)
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
        case 't': fwrite(hgn->all.b,1,hgn->all.s,outfile); break;
        case 'n': fwrite(hgn->tag.b,1,hgn->tag.s,outfile); break;
        case 'i':
          trim = 1;
        case 'I': print_trimmed_if(&hgn->insides,trim,outfile); break;
        case 'l': print_uint(hgn->lvl,outfile); break;
        case 's': print_uint(hgn->all.s,outfile); break;
        case 'c': print_uint(hgn->child_count,outfile); break;
        case 'p': print_uint(hgn->all.b-reference,outfile); break;
        case 'a':
          trim = 1;
        case 'A': print_attribs(hgn,trim,outfile); break;
        case 'v':
          trim = 1;
        case 'V':
          print_attrib_value(hgn->attribs,hgn->attribsl,text,textl,num,trim,outfile);
          break;
      }
      continue;
    }
    fputc(format[i++],outfile);
  }
}

void
hgrep_print(FILE *outfile, const hgrep_hnode *hgn)
{
  fwrite(hgn->all.b,1,hgn->all.s,outfile);
  fputc('\n',outfile);
}

static hgrep_error *
format_comp(char *src, size_t *pos, size_t *size,
#ifdef HGREP_EDITING
  hgrep_format_func **format,
#else
  char **format,
#endif
  size_t *formatl)
{
  hgrep_error *err;
  if (*pos >= *size || !src)
    return NULL;
  #ifndef HGREP_EDITING
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
  flexarr *f = flexarr_init(sizeof(hgrep_format_func),8);
  err = format_get_funcs(f,src,pos,size);
  flexarr_conv(f,(void**)format,formatl);
  if (err)
    return err;
  #endif
  return NULL;
}

static hgrep_error *
match_function_handle(char *src, size_t *pos, size_t *size, flexarr *hooks)
{
  hgrep_error *err;
  size_t p = *pos;

  while (*pos < *size && isalpha(src[*pos]))
    (*pos)++;

  size_t func_len = *pos-p;

  if (!func_len || src[*pos] != '@') {
    *pos = p;
    return NULL;
  }
  if (*pos+1 >= *size)
    return hgrep_set_error(1,"hook \"%.*s\" expected argument",func_len,src+p);
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
    return hgrep_set_error(1,"hook \"%.*s\" does not exists",(int)func_len,src+p);

  uchar islist = 0;
  hgrep_hook hook;
  hook.flags = match_functions[i].flags;

  if (src[*pos] == '[') {
    if (hook.flags&F_REGEX)
      return hgrep_set_error(1,"hook \"%.*s\" expected regex argument",(int)func_len,src+p);

    islist = 1;
    err = range_comp(src,pos,*size,&hook.match.range);
  } else {
    if (hook.flags&F_RANGE)
      return hgrep_set_error(1,"hook \"%.*s\" expected list argument",(int)func_len,src+p);

    err = hgrep_regcomp(&hook.match.pattern,src,pos,size,' ',"ucas");
  }
  if (err)
    return err;

  if (!islist && !hook.match.pattern.range.s && hook.match.pattern.flags&HGRP_PATTERN_ALL) { //ignore if it matches everything
    hgrep_regfree(&hook.match.pattern);
    return NULL;
  }

  memcpy(flexarr_inc(hooks),&hook,sizeof(hook));
  return NULL;
}

static hgrep_error *
get_pattribs(char *src, size_t *size, struct hgrep_pattrib **attrib, size_t *attribl, hgrep_hook **hooks, size_t *hooksl, hgrep_range *range)
{
  hgrep_error *err = NULL;
  struct hgrep_pattrib pa;
  flexarr *pattrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  flexarr *phooks = flexarr_init(sizeof(hgrep_hook),HOOK_INC);

  uchar tofree = 0;

  for (size_t i = 0; i < *size;) {
    while_is(isspace,src,i,*size);
    if (i >= *size)
      break;

    memset(&pa,0,sizeof(struct hgrep_pattrib));
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
      if ((err = hgrep_regcomp(&pa.r[0],t_name,&t_pos,&t_size,' ',"ufsi")))
        break;

      if ((err = hgrep_regcomp(&pa.r[1],src,&i,size,' ',"uws")))
        break;
      pa.flags |= A_VAL_MATTERS;
    } else {
      if ((err = hgrep_regcomp(&pa.r[0],src,&i,size,'=',NULL))) //!
        break;

      while_is(isspace,src,i,*size);
      if (i >= *size)
        goto ADD;
      if (src[i] == '=') {
        i++;
        while_is(isspace,src,i,*size);
        if (i >= *size)
          break;

        if ((err = hgrep_regcomp(&pa.r[1],src,&i,size,' ',NULL)))
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
    memcpy(flexarr_inc(pattrib),&pa,sizeof(struct hgrep_pattrib));
    tofree = 0;
  }
  if (tofree)
    pattrib_free(&pa);

  flexarr_conv(pattrib,(void**)attrib,attribl);
  flexarr_conv(phooks,(void**)hooks,hooksl);
  return err;
}

hgrep_error *
hgrep_ncomp(const char *script, size_t size, hgrep_node *node)
{
  if (size == 0)
    return NULL;
  hgrep_error *err = NULL;
  memset(node,0,sizeof(hgrep_node));

  char *nscript = memdup(script,size);
  size_t pos=0;
  while_is(isspace,nscript,pos,size);

  if (pos >= size) {
    node->flags |= P_EMPTY;
    goto END;
  }

  if ((err = hgrep_regcomp(&node->tag,nscript,&pos,&size,' ',NULL))) { //!
    //fprintf(stderr,"hgrep_regcomp went wrong\n");
    goto END;
  }

  size_t s = size-pos;
  err = get_pattribs(nscript+pos,&s,&node->attribs,&node->attribsl,&node->hooks,&node->hooksl,&node->position);
  size = s+pos;

  END: ;
  if (err)
    hgrep_nfree(node);
  free(nscript);
  return err;
}

/*void //just for debugging
hgrep_expr_print(flexarr *exprs, size_t tab)
{
  hgrep_expr *a = (hgrep_expr*)exprs->v;
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
  fprintf(stderr,"%% %lu",exprs->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < exprs->size; i++) {
    for (size_t j = 0; j < tab; j++)
      fputs("  ",stderr);
    if (a[i].istable&1) {
      fprintf(stderr,"%%x %d node(%u) expr(%u)\n",a[i].istable,a[i].nodefl,a[i].exprfl);
      hgrep_expr_print((flexarr*)a[i].e,tab);
    } else {
      fprintf(stderr,"%%j node(%u) expr(%u)\n",a[i].nodefl,a[i].exprfl);
    }
  }
}*/

static void
hgrep_expr_free(hgrep_expr *expr)
{
  #ifdef HGREP_EDITING
  format_free(expr->nodef,expr->nodefl);
  format_free(expr->exprf,expr->exprfl);
  #else
  if (expr->nodef)
    free(expr->nodef);
  #endif
  hgrep_nfree((hgrep_node*)expr->e);
}

static void
hgrep_exprs_free_pre(flexarr *exprs)
{
  if (!exprs)
    return;
  hgrep_expr *e = (hgrep_expr*)exprs->v;
  for (size_t i = 0; i < exprs->size; i++) {
    if (e[i].istable&EXPR_TABLE) {
      hgrep_exprs_free_pre((flexarr*)e[i].e);
    } else
      hgrep_expr_free(&e[i]);
  }
  flexarr_free(exprs);
}

void
hgrep_efree(hgrep_exprs *exprs)
{
  for (size_t i = 0; i < exprs->s; i++) {
    if (exprs->b[i].istable&EXPR_TABLE) {
      hgrep_exprs_free_pre((flexarr*)exprs->b[i].e);
    } else
      hgrep_expr_free(&exprs->b[i]);
  }
  free(exprs->b);
}

static flexarr *
hgrep_ecomp_pre(const char *csrc, size_t *pos, size_t s, const uchar flags, hgrep_error **err)
{
  if (s == 0)
    return NULL;

  size_t tpos = 0;
  if (pos == NULL)
    pos = &tpos;

  flexarr *ret = flexarr_init(sizeof(hgrep_expr),PATTERN_SIZE_INC);
  hgrep_expr *acurrent = (hgrep_expr*)flexarr_inc(ret);
  acurrent->e = flexarr_init(sizeof(hgrep_expr),PATTERN_SIZE_INC);
  acurrent->istable = EXPR_TABLE|EXPR_NEWCHAIN;
  hgrep_expr expr;
  char *src = memdup(csrc,s);
  size_t exprl;
  size_t i = *pos;
  enum {
    typePassed,
    typeNextNode,
    typeGroupStart,
    typeGroupEnd
  } next = typePassed;

  hgrep_str nodef,exprf;

  while_is(isspace,src,*pos,s);
  for (size_t j; i < s; i++) {
    j = i;
    uchar hasexpr=0,hasended=0;
    hgrep_expr *new = NULL;
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

    #ifdef HGREP_EDITING
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
        new->istable |= EXPR_SINGULAR;
        new->nodef = expr.nodef;
        new->nodefl = expr.nodefl;
      }
    }
    #ifdef HGREP_EDITING
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
      expr.e = malloc(sizeof(hgrep_node));
      if (!exprl) {
        ((hgrep_node*)expr.e)->flags |= P_EMPTY;
      } else if (next != typeGroupStart) {
        *err = hgrep_ncomp(src+j,exprl,(hgrep_node*)expr.e);
        if (*err)
          goto EXIT1;
      }

      new = (hgrep_expr*)flexarr_inc(acurrent->e);
      *new = expr;
    }

    if (next == typeGroupStart) {
      new->istable = EXPR_TABLE|EXPR_NEWBLOCK;
      next = typePassed;
      *pos = i;
      new->e = hgrep_ecomp_pre(src,pos,s,flags,err);
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
    } else if (new)
      new->istable = 0;

    if (next == typeNextNode) {
      NEXT_NODE: ;
      next = typePassed;
      acurrent = (hgrep_expr*)flexarr_inc(ret);
      acurrent->e = flexarr_init(sizeof(hgrep_expr),PATTERN_SIZE_INC);
      acurrent->istable = EXPR_TABLE|EXPR_NEWCHAIN;
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
    hgrep_exprs_free_pre(ret);
    return NULL;
  }
  return ret;
}

hgrep_error *
hgrep_ecomp(const char *src, size_t size, hgrep_exprs *exprs, const uchar flags)
{
  hgrep_error *err = NULL;
  flexarr *ret = hgrep_ecomp_pre(src,NULL,size,flags,&err);
  if (ret) {
    //hgrep_expr_print(ret,0);
    flexarr_conv(ret,(void**)&exprs->b,&exprs->s);
  } else
    exprs->s = 0;
  return err;
}

static void
dest_match_position(const hgrep_range *range, flexarr *dest, const size_t start, const size_t end) {
  hgrep_compressed *x = (hgrep_compressed*)dest->v;
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
first_match(hgrep *hg, hgrep_node *node, flexarr *dest)
{
  hgrep_hnode *nodes = hg->nodes;
  size_t nodesl = hg->nodesl;
  for (size_t i = 0; i < nodesl; i++) {
    if (hgrep_match(&nodes[i],node)) {
      hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
      x->lvl = 0;
      x->id = i;
    }
  }
  if (node->position.s)
    dest_match_position(&node->position,dest,0,dest->size);
}


static void
node_exec(hgrep *hg, hgrep_node *node, flexarr *source, flexarr *dest)
{
  if (source->size == 0) {
    first_match(hg,node,dest);
    return;
  }

  ushort lvl;
  hgrep_hnode *nodes = hg->nodes;
  size_t current,n;
  for (size_t i = 0; i < source->size; i++) {
    current = ((hgrep_compressed*)source->v)[i].id;
    lvl = nodes[current].lvl;
    size_t prevdestsize = dest->size;
    for (size_t j = 0; j <= nodes[current].child_count; j++) {
      n = current+j;
      nodes[n].lvl -= lvl;
      if (hgrep_match(&nodes[n],node)) {
        hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
        x->lvl = lvl;
        x->id = n;
      }
      nodes[n].lvl += lvl;
    }
    if (node->position.s)
      dest_match_position(&node->position,dest,prevdestsize,dest->size);
  }
}

/*static hgrep_error *
ncollector_check(flexarr *ncollector, size_t correctsize)
{
  size_t ncollector_sum = 0;
  hgrep_cstr *pcol = (hgrep_cstr*)ncollector->v;
  for (size_t j = 0; j < ncollector->size; j++)
    ncollector_sum += pcol[j].s;
  if (ncollector_sum != correctsize)
    return hgrep_set_error(1,"ncollector does not match count of found tags, %lu != %lu",ncollector_sum,correctsize);
  return NULL;
}*/

static void
ncollector_add(flexarr *ncollector, flexarr *dest, flexarr *source, size_t startn, size_t lastn, const hgrep_expr *lastformat, uchar istable)
{
  if (!source->size)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (!lastformat)
    goto END;
  if (istable) {
    if (startn != lastn) { //truncate previously added, now useless ncollector
      for (size_t i = lastn; i < ncollector->size; i++)
        ((hgrep_cstr*)ncollector->v)[i-lastn] = ((hgrep_cstr*)ncollector->v)[i];
      ncollector->size -= lastn-startn;
    }
  } else {
    ncollector->size = startn;
    *(hgrep_cstr*)flexarr_inc(ncollector) = (hgrep_cstr){(char const*)lastformat,dest->size-prevsize};
  }
  END: ;
  source->size = 0;
}

#ifdef HGREP_EDITING
static void
fcollector_add(const size_t lastn, const uchar isnodef, const hgrep_expr *expr, flexarr *ncollector, flexarr *fcollector)
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

static hgrep_error *hgrep_ematch_pre(hgrep *hg, const hgrep_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr *ncollector
    #ifdef HGREP_EDITING
    , flexarr *fcollector
    #endif
    );

static hgrep_error *
hgrep_ematch_table(hgrep *hg, const hgrep_expr *expr, flexarr *source, flexarr *dest, flexarr *ncollector
    #ifdef HGREP_EDITING
    , flexarr *fcollector
    #endif
    )
{
  flexarr *exprs = (flexarr*)expr->e;
  hgrep_expr *exprsv = (hgrep_expr*)exprs->v;
  size_t exprsl = exprs->size;

  if (source->size && expr->istable&EXPR_SINGULAR && expr->nodefl) {
    hgrep_error *err = NULL;
    flexarr *in = flexarr_init(sizeof(hgrep_compressed),1);
    hgrep_compressed *inv = (hgrep_compressed*)flexarr_inc(in);
    hgrep_compressed *sourcev = (hgrep_compressed*)source->v;

    for (size_t i = 0; i < source->size; i++) {
      *inv = sourcev[i];
      #ifdef HGREP_EDITING
      size_t lastn = ncollector->size;
      #endif
      err = hgrep_ematch_pre(hg,exprsv,exprsl,in,dest,ncollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
        );
      if (err)
        break;
      #ifdef HGREP_EDITING
      if (ncollector->size-lastn)
        fcollector_add(lastn,1,expr,ncollector,fcollector);
      #endif
    }
    flexarr_free(in);

    return err;
  }
  return hgrep_ematch_pre(hg,exprsv,exprsl,source,dest,ncollector
      #ifdef HGREP_EDITING
      ,fcollector
      #endif
      );
}

static hgrep_error *
hgrep_ematch_pre(hgrep *hg, const hgrep_expr *exprs, size_t exprsl, flexarr *source, flexarr *dest, flexarr *ncollector
    #ifdef HGREP_EDITING
    , flexarr *fcollector
    #endif
    )
{
  flexarr *buf[3];
  hgrep_error *err;

  buf[0] = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  if (source) {
    buf[0]->asize = source->asize;
    buf[0]->size = source->size;
    buf[0]->v = memdup(source->v,source->asize*sizeof(hgrep_compressed));
  }

  buf[1] = flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  buf[2] = dest ? dest : flexarr_init(sizeof(hgrep_compressed),PASSED_INC);
  flexarr *buf_unchanged[2];
  buf_unchanged[0] = buf[0];
  buf_unchanged[1] = buf[1];

  size_t startn = ncollector->size;
  size_t lastn = startn;

  hgrep_expr const *lastformat = NULL;

  for (size_t i = 0; i < exprsl; i++) {
    if (exprs[i].istable&EXPR_TABLE) {
      lastn = ncollector->size;
      err = hgrep_ematch_table(hg,&exprs[i],buf[0],buf[1],ncollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
              );
      if (err)
        goto ERR;
    } else if (exprs[i].e) {
      lastformat = &exprs[i];
      node_exec(hg,(hgrep_node*)exprs[i].e,buf[0],buf[1]);
    }

    //fprintf(stderr,"ematch %p %lu %lu %lu %u %lu\n",exprs,startn,lastn,ncollector->size,(exprs[i].istable&EXPR_NEWBLOCK),exprs[i].exprfl);

    #ifdef HGREP_EDITING
    if (exprs[i].istable&EXPR_NEWBLOCK && exprs[i].exprfl)
      fcollector_add(lastn,0,&exprs[i],ncollector,fcollector);
    #endif

    if ((exprs[i].istable&EXPR_TABLE &&
      !(exprs[i].istable&EXPR_NEWBLOCK)) || i == exprsl-1) {
      ncollector_add(ncollector,buf[2],buf[1],startn,lastn,lastformat,exprs[i].istable);
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

    err = nodes_output(hg,buf[2],ncollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
        );
    if (err)
      goto ERR;
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
hgrep_ematch(hgrep *hg, const hgrep_exprs *exprs, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl)
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
    flexarr *ncollector = flexarr_init(sizeof(hgrep_cstr),32);
    #ifdef HGREP_EDITING
    flexarr *fcollector = flexarr_init(sizeof(struct fcollector_expr),16);
    #endif
    err = hgrep_ematch_pre(hg,exprs->b,exprs->s,fsource,fdest,ncollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
        );
    flexarr_free(ncollector);
    #ifdef HGREP_EDITING
    flexarr_free(fcollector);
    #endif
    return err;
}

static hgrep_error *
hgrep_analyze(const char *ptr, const size_t size, flexarr *nodes, hgrep *hg)
{
  hgrep_error *err;
  for (size_t i = 0; i < size; i++) {
    while (i < size && ptr[i] != '<')
        i++;
    while (i < size && ptr[i] == '<') {
      html_struct_handle(ptr,&i,size,0,nodes,hg,&err);
      if (err)
        return err;
    }
  }
  return NULL;
}

hgrep_error *
hgrep_fmatch(const char *ptr, const size_t size, FILE *output, const hgrep_node *node,
#ifdef HGREP_EDITING
  hgrep_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl)
{
  hgrep t;
  t.data = ptr;
  t.size = size;
  t.expr = node;
  t.nodef = nodef;
  t.nodefl = nodefl;
  t.flags = 0;
  if (output == NULL)
    output = stdout;
  t.output = output;
  flexarr *nodes = flexarr_init(sizeof(hgrep_hnode),HGREP_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_cstr_pair),ATTRIB_INC);
  hgrep_error *err = hgrep_analyze(ptr,size,nodes,&t);
  flexarr_free(nodes);
  t.nodes = NULL;
  t.nodesl = 0;
  flexarr_free((flexarr*)t.attrib_buffer);
  return err;
}

hgrep_error *
hgrep_efmatch(char *script, size_t size, FILE *output, const hgrep_exprs *exprs, int (*freeptr)(void *ptr, size_t size))
{
  if (exprs->s == 0)
    return NULL;
  if (exprs->s > 1)
    return hgrep_set_error(1,"fast mode cannot run in non linear mode");

  FILE *t = output;
  char *nscript;
  size_t fsize;

  flexarr *first = (flexarr*)exprs->b[0].e;
  for (size_t i = 0; i < first->size; i++) {
    output = (i == first->size-1) ? t : open_memstream(&nscript,&fsize);

    if (((hgrep_expr*)first->v)[i].istable&1)
      return hgrep_set_error(1,"fast mode cannot run in non linear mode");
    hgrep_error *err = hgrep_fmatch(script,size,output,(hgrep_node*)((hgrep_expr*)first->v)[i].e,
      ((hgrep_expr*)first->v)[i].nodef,((hgrep_expr*)first->v)[i].nodefl);
    fflush(output);

    if (i == 0) {
      (*freeptr)(script,size);
    } else
      free(script);

    if (i != first->size-1)
      fclose(output);

    if (err)
      return err;

    script = nscript;
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
  t.expr = NULL;
  t.flags = HGREP_SAVE;
  if (output == NULL)
    output = stdout;
  t.output = output;

  flexarr *nodes = flexarr_init(sizeof(hgrep_hnode),HGREP_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_cstr_pair),ATTRIB_INC);

  hgrep_analyze(ptr,size,nodes,&t);

  flexarr_conv(nodes,(void**)&t.nodes,&t.nodesl);
  flexarr_free((flexarr*)t.attrib_buffer);
  return t;
}
