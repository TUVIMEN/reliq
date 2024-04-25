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

//hgrep_pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

//hgrep_pattern flags
#define P_MATCH_INSIDES 0x1
#define P_INVERT_INSIDES 0x2
#define P_EMPTY 0x4

//hgrep_match_function flags
#define F_KINDS 0x7

#define F_ATTRIBUTES 0x1
#define F_LEVEL 0x2
#define F_CHILD_COUNT 0x3
#define F_MATCH_INSIDES 0x4

#define F_LIST 0x8
#define F_REGEX 0x10

//hgrep_epattern flags
#define EPATTERN_TABLE 0x1
#define EPATTERN_NEWBLOCK 0x2
#define EPATTERN_NEWCHAIN 0x4
#define EPATTERN_SINGULAR 0x8

#define ATTRIB_INC (1<<3)
#define HGREP_NODES_INC (1<<13)

//hgrep_regex flags
#define HGREP_REGEX_TRIM 0x2
#define HGREP_REGEX_CASE_INSENSITIVE 0x4
#define HGREP_REGEX_INVERT 0x8

#define HGREP_REGEX_MATCH 0x70

#define HGREP_REGEX_MATCH_FULL 0x10
#define HGREP_REGEX_MATCH_ALL 0x20
#define HGREP_REGEX_MATCH_WORD 0x30
#define HGREP_REGEX_MATCH_BEGINNING 0x40
#define HGREP_REGEX_MATCH_ENDING 0x50

#define HGREP_REGEX_TYPE 0x180

#define HGREP_REGEX_TYPE_STR 0x80
#define HGREP_REGEX_TYPE_BRE 0x100
#define HGREP_REGEX_TYPE_ERE 0x180

#define HGREP_REGEX_EMPTY 0x200
#define HGREP_REGEX_ALL 0x400

struct hgrep_match_function {
  hgrep_str8 name;
  ushort flags;
};

const struct hgrep_match_function match_functions[] = {
    {{"m",1},F_REGEX|F_MATCH_INSIDES},
    {{"a",1},F_LIST|F_ATTRIBUTES},
    {{"l",1},F_LIST|F_LEVEL},
    {{"c",1},F_LIST|F_CHILD_COUNT},

    {{"match",5},F_REGEX|F_MATCH_INSIDES},
    {{"attributes",10},F_LIST|F_ATTRIBUTES},
    {{"level",5},F_LIST|F_LEVEL},
    {{"children",8},F_LIST|F_CHILD_COUNT},
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
          *flags |= HGREP_REGEX_TRIM;
          break;
      case 'u':
          *flags &= ~HGREP_REGEX_TRIM;
          break;
      case 'i':
          *flags |= HGREP_REGEX_CASE_INSENSITIVE;
          break;
      case 'c':
          *flags &= ~HGREP_REGEX_CASE_INSENSITIVE;
          break;
      case 'v':
          *flags |= HGREP_REGEX_INVERT;
          break;
      case 'n':
          *flags &= ~HGREP_REGEX_INVERT;
          break;
      case 'a':
          *flags &= ~HGREP_REGEX_MATCH;
          *flags |= HGREP_REGEX_MATCH_ALL;
          break;
      case 'f':
          *flags &= ~HGREP_REGEX_MATCH;
          *flags |= HGREP_REGEX_MATCH_FULL;
          break;
      case 'w':
          *flags &= ~HGREP_REGEX_MATCH;
          *flags |= HGREP_REGEX_MATCH_WORD;
          break;
      case 'b':
          *flags &= ~HGREP_REGEX_MATCH;
          *flags |= HGREP_REGEX_MATCH_BEGINNING;
          break;
      case 'e':
          *flags &= ~HGREP_REGEX_MATCH;
          *flags |= HGREP_REGEX_MATCH_ENDING;
          break;
      case 's':
          *flags &= ~HGREP_REGEX_TYPE;
          *flags |= HGREP_REGEX_TYPE_STR;
          break;
      case 'B':
          *flags &= ~HGREP_REGEX_TYPE;
          *flags |= HGREP_REGEX_TYPE_BRE;
          break;
      case 'E':
          *flags &= ~HGREP_REGEX_TYPE;
          *flags |= HGREP_REGEX_TYPE_ERE;
          break;
      /*case 'P':
          *flags &= ~HGREP_REGEX_TYPE;
          *flags |= HGREP_REGEX_TYPE_PCRE;
          break;*/
    }
  }
}

static void
hgrep_regcomp_get_flags(hgrep_regex *r, const char *src, size_t *pos, const size_t size, const char *flags)
{
  size_t p = *pos;
  r->flags = HGREP_REGEX_TRIM|HGREP_REGEX_MATCH_FULL|HGREP_REGEX_TYPE_STR;
  r->rangesl = 0;

  if (flags)
    hgrep_regcomp_set_flags(&r->flags,flags,strlen(flags));

  if (src[p] == '\'' || src[p] == '"' || src[p] == '*')
    return;

  while (p < size && isalpha(src[p]))
    p++;
  if (p >= size || src[p] != '>')
    return;

  hgrep_regcomp_set_flags(&r->flags,src+*pos,p-*pos);

  *pos = p+1;
  return;
}

static hgrep_error *
hgrep_regcomp_add_pattern(hgrep_regex *r, const char *src, const size_t size)
{
  ushort match = r->flags&HGREP_REGEX_MATCH,
    type = r->flags&HGREP_REGEX_TYPE;

  if (!size) {
    r->flags |= HGREP_REGEX_EMPTY;
    return NULL;
  }

  if (type == HGREP_REGEX_TYPE_STR) {
    r->match.str.b = memdup(src,size);
    r->match.str.s = size;
  } else {
    int regexflags = REG_NOSUB;
    if (r->flags&HGREP_REGEX_CASE_INSENSITIVE)
      regexflags |= REG_ICASE;
    if (type == HGREP_REGEX_TYPE_ERE)
      regexflags |= REG_EXTENDED;

    size_t addedspace = 0;
    uchar fullmatch =
      (match == HGREP_REGEX_MATCH_FULL || match == HGREP_REGEX_MATCH_WORD) ? 1 : 0;

    if (fullmatch)
      addedspace = 2;
    else if (match == HGREP_REGEX_MATCH_BEGINNING || match == HGREP_REGEX_MATCH_ENDING)
      addedspace = 1;

    char *tmp = malloc(size+addedspace+1);
    size_t p = 0;

    if (fullmatch || match == HGREP_REGEX_MATCH_BEGINNING)
      tmp[p++] = '^';

    memcpy(tmp+p,src,size);
    p += size;

    if (fullmatch || match == HGREP_REGEX_MATCH_ENDING)
      tmp[p++] = '$';
    tmp[p] = 0;

    /*int ret =
    if (ret) //check for compiling error */
    //fprintf(stderr,"regcomp %s\n",tmp);
    regcomp(&r->match.reg,tmp,regexflags); //!
      //fprintf(stderr,"kkkkkkkkkkk\n");
    free(tmp);
  }
  return NULL;
}

static hgrep_error *
hgrep_regcomp(hgrep_regex *r, char *src, size_t *pos, size_t *size, const char delim, const char *flags)
{
  hgrep_error *err;

  hgrep_regcomp_get_flags(r,src,pos,*size,flags);

  if (*pos && *pos < *size && src[*pos-1] == '>' && src[*pos] == '[') {
    err = ranges_comp(src,pos,*size,&r->ranges,&r->rangesl);
    if (err)
      return err;
    if (*pos >= *size || src[*pos] == delim || isspace(src[*pos])) {
      r->flags |= HGREP_REGEX_ALL;
      return NULL;
    }
  }

  if (*pos < *size && src[*pos] == '*') {
    if (*pos+1 < *size)
      if (!isspace(src[*pos+1]) && src[*pos+1] != delim)
        goto NOT_ALL;
    (*pos)++;
    r->flags |= HGREP_REGEX_ALL;
    return NULL;
  }

  NOT_ALL: ;

  size_t start,len;
  err = get_quoted(src,pos,size,delim,&start,&len);
  if (err)
    return err;

  return hgrep_regcomp_add_pattern(r,src+start,len);
}

static int
hgrep_regexec_match_word(const hgrep_regex *r, hgrep_cstr *str)
{
  ushort type = r->flags&HGREP_REGEX_TYPE;
  uchar icase = r->flags&HGREP_REGEX_CASE_INSENSITIVE;
  const char *ptr = str->b;
  size_t plen = str->s;
  char const *saveptr,*word;
  size_t saveptrlen,wordlen;

  while (1) {
    memwordtok_r(ptr,plen,(void const**)&saveptr,&saveptrlen,(void const**)&word,&wordlen);
    if (!word)
      return 0;

    if (type == HGREP_REGEX_TYPE_STR) {
      if (r->match.str.s == wordlen) {
        if (icase) {
          if (memcasecmp(word,r->match.str.b,wordlen) == 0)
            return 1;
        } else if (memcmp(word,r->match.str.b,wordlen) == 0)
          return 1;
      }
    } else {
      regmatch_t pmatch;
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)wordlen;

      if (regexec(&r->match.reg,word,1,&pmatch,REG_STARTEND) == 0)
        return 1;
    }

    ptr = NULL;
  }
}

static int
hgrep_regexec_match_str(const hgrep_regex *r, hgrep_cstr *str)
{
  ushort match = r->flags&HGREP_REGEX_MATCH;
  uchar icase = r->flags&HGREP_REGEX_CASE_INSENSITIVE;

  if (!r->match.str.s)
    return 1;
  if (!str->s)
    return 0;

  switch (match) {
    case HGREP_REGEX_MATCH_ALL:
      if (icase) {
        if (memcasemem(str->b,str->s,r->match.str.b,r->match.str.s) != NULL)
          return 1;
      } else if (memmem(str->b,str->s,r->match.str.b,r->match.str.s) != NULL)
        return 1;
      break;
    case HGREP_REGEX_MATCH_FULL:
      if (str->s != r->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,r->match.str.b,str->s) == 0)
          return 1;
      } else if (memcmp(str->b,r->match.str.b,str->s) == 0)
        return 1;
      break;
    case HGREP_REGEX_MATCH_BEGINNING:
      if (str->s < r->match.str.s)
        return 0;
      if (icase) {
        if (memcasecmp(str->b,r->match.str.b,r->match.str.s) == 0)
          return 1;
      } else if (memcmp(str->b,r->match.str.b,r->match.str.s) == 0)
        return 1;
      break;
    case HGREP_REGEX_MATCH_ENDING:
      if (str->s < r->match.str.s)
        return 0;
      const char *start = str->b+(str->s-r->match.str.s);
      size_t len = r->match.str.s;
      if (icase) {
        if (memcasecmp(start,r->match.str.b,len) == 0)
          return 1;
      } else if (memcmp(start,r->match.str.b,len) == 0)
        return 1;
      break;
  }
  return 0;
}

static int
hgrep_regexec(const hgrep_regex *r, const char *src, const size_t size)
{
  uchar invert = (r->flags&HGREP_REGEX_INVERT) ? 1 : 0;
  if ((!ranges_match(size,r->ranges,r->rangesl,-1)))
    return invert;

  if (r->flags&HGREP_REGEX_ALL)
    return !invert;

  if (r->flags&HGREP_REGEX_EMPTY)
    return (size == 0) ? !invert : invert;

  ushort match = r->flags&HGREP_REGEX_MATCH,
    type = r->flags&HGREP_REGEX_TYPE;

  hgrep_cstr str = {src,size};

  if (match == HGREP_REGEX_MATCH_WORD)
    return hgrep_regexec_match_word(r,&str)^invert;

  if (r->flags&HGREP_REGEX_TRIM)
    memtrim((void const**)&str.b,&str.s,src,size);

  if (type == HGREP_REGEX_TYPE_STR) {
    return hgrep_regexec_match_str(r,&str)^invert;
  } else {
    if (!str.s)
      return invert;

    regmatch_t pmatch;
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)str.s;

    if (regexec(&r->match.reg,str.b,1,&pmatch,REG_STARTEND) == 0)
      return !invert;
  }
  return invert;
}

static void
hgrep_regfree(hgrep_regex *r)
{
  if (!r)
    return;
  if (r->rangesl)
    free(r->ranges);

  if (r->flags&HGREP_REGEX_EMPTY || r->flags&HGREP_REGEX_ALL)
    return;

  if ((r->flags&HGREP_REGEX_TYPE) == HGREP_REGEX_TYPE_STR) {
    if (r->match.str.b)
      free(r->match.str.b);
  } else
    regfree(&r->match.reg);
}

static int
pattrib_match(const hgrep_node *hgn, const struct hgrep_pattrib *attrib, size_t attribl)
{
  const hgrep_cstr_pair *a = hgn->attrib;
  for (size_t i = 0; i < attribl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hgn->attribl; j++) {
      if (!ranges_match(j,attrib[i].position.b,attrib[i].position.s,hgn->attribl-1))
        continue;

      if (!hgrep_regexec(&attrib[i].r[0],a[j].f.b,a[j].f.s))
        continue;

      if (attrib[i].flags&A_VAL_MATTERS && !hgrep_regexec(&attrib[i].r[1],a[j].s.b,a[j].s.s))
        continue;

      found = 1;
      break;
    }
    if (((attrib[i].flags&A_INVERT)==A_INVERT)^found)
      return 0;
  }
  return 1;
}

static int
hgrep_match_hooks(const hgrep_node *hgn, const hgrep_hook *hooks, const size_t hooksl)
{
  for (size_t i = 0; i < hooksl; i++) {
    char const *src = NULL;
    size_t srcl = 0;

    switch (hooks[i].flags&F_KINDS) {
      case F_ATTRIBUTES:
        srcl = hgn->attribl;
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

    if (hooks[i].flags&F_LIST) {
    if (!ranges_match(srcl,hooks[i].match.l.b,hooks[i].match.l.s,-1))
        return 0;
    } else if (!hgrep_regexec(&hooks[i].match.r,src,srcl))
      return 0;
  }
  return 1;
}

int
hgrep_match(const hgrep_node *hgn, const hgrep_pattern *p)
{
  if (p->flags&P_EMPTY)
    return 1;

  if (!hgrep_regexec(&p->tag,hgn->tag.b,hgn->tag.s))
    return 0;

  if (!hgrep_match_hooks(hgn,p->hooks,p->hooksl))
    return 0;

  if (!pattrib_match(hgn,p->attrib,p->attribl))
    return 0;

  return 1;
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
              if (memcomp(a[i].f.b,format+cont,contl,a[i].f.s))
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
    err = ranges_comp(src,pos,*size,&hook.match.l.b,&hook.match.l.s);
  } else {
    if (hook.flags&F_LIST)
      return hgrep_set_error(1,"hook \"%.*s\" expected list argument",(int)func_len,src+p);

    err = hgrep_regcomp(&hook.match.r,src,pos,size,' ',"ucas");
  }
  if (err)
    return err;

  if (!islist && !hook.match.r.rangesl && hook.match.r.flags&HGREP_REGEX_ALL) { //ignore if it matches everything
    hgrep_regfree(&hook.match.r);
    return NULL;
  }

  memcpy(flexarr_inc(hooks),&hook,sizeof(hook));
  return NULL;
}

static hgrep_error *
get_pattribs(char *pat, size_t *size, struct hgrep_pattrib **attrib, size_t *attribl, hgrep_hook **hooks, size_t *hooksl, hgrep_list *list)
{
  hgrep_error *err = NULL;
  struct hgrep_pattrib pa;
  flexarr *pattrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  flexarr *phooks = flexarr_init(sizeof(hgrep_hook),HOOK_INC);

  for (size_t i = 0; i < *size;) {
    while_is(isspace,pat,i,*size);
    if (i >= *size)
      break;

    memset(&pa,0,sizeof(struct hgrep_pattrib));
    char isset = 0;
    uchar isattrib = 0;

    if (isalpha(pat[i])) {
      size_t prev = i;
      err = match_function_handle(pat,&i,size,phooks);
      if (err)
        break;
      if (i != prev)
        continue;
    }

    if (pat[i] == '+') {
      isattrib = 1;
      pa.flags |= A_INVERT;
      isset = 1;
      i++;
    } else if (pat[i] == '-') {
      isattrib = 1;
      isset = -1;
      pa.flags &= ~A_INVERT;
      i++;
    } else if (pat[i] == '\\' && (pat[i+1] == '+' || pat[i+1] == '-'))
      i++;

    char shortcut = 0;
    if (i >= *size)
      break;

    if (pat[i] == '.' || pat[i] == '#') {
      shortcut = pat[i++];
    } else if (pat[i] == '\\' && (pat[i+1] == '.' || pat[i+1] == '#'))
      i++;

    while_is(isspace,pat,i,*size);
    if (i >= *size)
      break;

    if (pat[i] == '[') {
      err = ranges_comp(pat,&i,*size,&pa.position.b,&pa.position.s);
      if (err)
        break;
      if (!isattrib && (i >= *size || isspace(pat[i]))) {
        list->b = pa.position.b;
        list->s = pa.position.s;
        continue;
      }
    } else if (pat[i] == '\\' && pat[i+1] == '[')
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

      if ((err = hgrep_regcomp(&pa.r[1],pat,&i,size,' ',"uws")))
        break;
      pa.flags |= A_VAL_MATTERS;
    } else {
      if ((err = hgrep_regcomp(&pa.r[0],pat,&i,size,'=',NULL))) //!
        break;

      while_is(isspace,pat,i,*size);
      if (i >= *size)
        goto ADD;
      if (pat[i] == '=') {
        i++;
        while_is(isspace,pat,i,*size);
        if (i >= *size)
          break;

        if ((err = hgrep_regcomp(&pa.r[1],pat,&i,size,' ',NULL)))
          break;
        pa.flags |= A_VAL_MATTERS;
      } else {
        pa.flags &= ~A_VAL_MATTERS;
        goto ADD_SKIP;
      }
    }

    ADD: ;
    if (pat[i] != '+' && pat[i] != '-')
      i++;
    ADD_SKIP: ;
    memcpy(flexarr_inc(pattrib),&pa,sizeof(struct hgrep_pattrib));
  }
  flexarr_conv(pattrib,(void**)attrib,attribl);
  flexarr_conv(phooks,(void**)hooks,hooksl);
  return err;
}

hgrep_error *
hgrep_pcomp(const char *pattern, size_t size, hgrep_pattern *p)
{
  if (size == 0)
    return NULL;
  hgrep_error *err = NULL;
  memset(p,0,sizeof(hgrep_pattern));

  char *pat = memdup(pattern,size);
  size_t pos=0;
  while_is(isspace,pat,pos,size);

  if (pos >= size) {
    p->flags |= P_EMPTY;
    goto END;
  }

  if ((err = hgrep_regcomp(&p->tag,pat,&pos,&size,' ',NULL))) { //!
    //fprintf(stderr,"hgrep_regcomp went wrong\n");
    goto END;
  }

  size_t s = size-pos;
  err = get_pattribs(pat+pos,&s,&p->attrib,&p->attribl,&p->hooks,&p->hooksl,&p->position);
  size = s+pos;

  END: ;
  if (err)
    hgrep_pfree(p);
  free(pat);
  return err;
}

/*void //just for debugging
hgrep_epattern_print(flexarr *epatterns, size_t tab)
{
  hgrep_epattern *a = (hgrep_epattern*)epatterns->v;
  for (size_t j = 0; j < tab; j++)
    fputs("  ",stderr);
  fprintf(stderr,"%% %lu",epatterns->size);
  fputc('\n',stderr);
  tab++;
  for (size_t i = 0; i < epatterns->size; i++) {
    for (size_t j = 0; j < tab; j++)
      fputs("  ",stderr);
    if (a[i].istable&1) {
      fprintf(stderr,"%%x %d node(%u) expr(%u)\n",a[i].istable,a[i].nodefl,a[i].exprfl);
      hgrep_epattern_print((flexarr*)a[i].p,tab);
    } else {
      fprintf(stderr,"%%j node(%u) expr(%u)\n",a[i].nodefl,a[i].exprfl);
    }
  }
}*/

static void
hgrep_epattern_free(hgrep_epattern *p)
{
  #ifdef HGREP_EDITING
  format_free(p->nodef,p->nodefl);
  format_free(p->exprf,p->exprfl);
  #else
  if (p->nodef)
    free(p->nodef);
  #endif
  hgrep_pfree((hgrep_pattern*)p->p);
}

static void
hgrep_epatterns_free_pre(flexarr *epatterns)
{
  if (!epatterns)
    return;
  hgrep_epattern *a = (hgrep_epattern*)epatterns->v;
  for (size_t i = 0; i < epatterns->size; i++) {
    if (a[i].istable&EPATTERN_TABLE) {
      hgrep_epatterns_free_pre((flexarr*)a[i].p);
    } else
      hgrep_epattern_free(&a[i]);
  }
  flexarr_free(epatterns);
}

void
hgrep_epatterns_free(hgrep_epatterns *epatterns)
{
  for (size_t i = 0; i < epatterns->s; i++) {
    if (epatterns->b[i].istable&EPATTERN_TABLE) {
      hgrep_epatterns_free_pre((flexarr*)epatterns->b[i].p);
    } else
      hgrep_epattern_free(&epatterns->b[i]);
  }
  free(epatterns->b);
}

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
  char *src = memdup(csrc,s);
  size_t patternl;
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
    uchar haspattern=0,hasended=0;
    hgrep_epattern *new = NULL;
    patternl = 0;

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
        patternl = (i-j)-nodef.s-((nodef.b) ? 1 : 0);
        continue;
      }
      if ((i == j || (i && isspace(src[i-1]))) && !exprf.b &&
        ((src[i] == '|' && !nodef.b) || src[i] == '/')) {
        if (i == j)
          haspattern = 1;
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
          patternl = i-j;
          patternl -= nodef.s+((nodef.b) ? 1 : 0);
          patternl -= exprf.s+((exprf.b) ? 1 : 0);
        }
        i++;
        break;
      }
      i++;
      if (!nodef.b && !exprf.b)
        patternl = i-j;
    }

    PASS: ;
    if (j+patternl > s)
      patternl = s-j;
    if (i > s)
      i = s;
    epattern.nodef = NULL;
    epattern.nodefl = 0;
    if (!nodef.s)
      nodef.s = i-(nodef.b-src);

    #ifdef HGREP_EDITING
    epattern.exprf = NULL;
    epattern.exprfl = 0;
    if (!exprf.s)
      exprf.s = i-(exprf.b-src);
    #endif

    if (nodef.b) {
      size_t g=0,t=nodef.s;
      *err = format_comp(nodef.b,&g,&t,&epattern.nodef,&epattern.nodefl);
      s -= nodef.s-t;
      if (*err)
        goto EXIT1;
      if (hasended) {
        new->istable |= EPATTERN_SINGULAR;
        new->nodef = epattern.nodef;
        new->nodefl = epattern.nodefl;
      }
    }
    #ifdef HGREP_EDITING
    if (exprf.b) {
      size_t g=0,t=exprf.s;
      *err = format_comp(exprf.b,&g,&t,&epattern.exprf,&epattern.exprfl);
      s -= exprf.s-t;
      if (*err)
        goto EXIT1;
      if (hasended) {
        new->exprf = epattern.exprf;
        new->exprfl = epattern.exprfl;
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
        || next == typeGroupEnd || patternl || haspattern)) {
      epattern.p = malloc(sizeof(hgrep_pattern));
      if (!patternl) {
        ((hgrep_pattern*)epattern.p)->flags |= P_EMPTY;
      } else if (next != typeGroupStart) {
        *err = hgrep_pcomp(src+j,patternl,(hgrep_pattern*)epattern.p);
        if (*err)
          goto EXIT1;
      }

      new = (hgrep_epattern*)flexarr_inc(acurrent->p);
      *new = epattern;
    }

    if (next == typeGroupStart) {
      new->istable = EPATTERN_TABLE|EPATTERN_NEWBLOCK;
      next = typePassed;
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
      acurrent = (hgrep_epattern*)flexarr_inc(ret);
      acurrent->p = flexarr_init(sizeof(hgrep_epattern),PATTERN_SIZE_INC);
      acurrent->istable = EPATTERN_TABLE|EPATTERN_NEWCHAIN;
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
    hgrep_epatterns_free_pre(ret);
    return NULL;
  }
  return ret;
}

hgrep_error *
hgrep_epcomp(const char *src, size_t size, hgrep_epatterns *epatterns, const uchar flags)
{
  hgrep_error *err = NULL;
  flexarr *ret = hgrep_epcomp_pre(src,NULL,size,flags,&err);
  if (ret) {
    //hgrep_epattern_print(ret,0);
    flexarr_conv(ret,(void**)&epatterns->b,&epatterns->s);
  } else
    epatterns->s = 0;
  return err;
}

static void
dest_match_position(const struct hgrep_range *range, const size_t rangel, flexarr *dest, const size_t start, const size_t end) {
  hgrep_compressed *x = (hgrep_compressed*)dest->v;
  size_t found = start;
  for (size_t i = start; i < end; i++) {
    if (!ranges_match(i-start,range,rangel,end-start))
      continue;
    if (found != i)
      x[found] = x[i];
    found++;
  }
  dest->size = found;
}

static void
first_match(hgrep *hg, hgrep_pattern *pattern, flexarr *dest)
{
  hgrep_node *nodes = hg->nodes;
  size_t nodesl = hg->nodesl;
  for (size_t i = 0; i < nodesl; i++) {
    if (hgrep_match(&nodes[i],pattern)) {
      hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
      x->lvl = 0;
      x->id = i;
    }
  }
  if (pattern->position.s)
    dest_match_position(pattern->position.b,pattern->position.s,dest,0,dest->size);
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
    size_t prevdestsize = dest->size;
    for (size_t j = 0; j <= nodes[current].child_count; j++) {
      n = current+j;
      nodes[n].lvl -= lvl;
      if (hgrep_match(&nodes[n],pattern)) {
        hgrep_compressed *x = (hgrep_compressed*)flexarr_inc(dest);
        x->lvl = lvl;
        x->id = n;
      }
      nodes[n].lvl += lvl;
    }
    if (pattern->position.s)
      dest_match_position(pattern->position.b,pattern->position.s,dest,prevdestsize,dest->size);
  }
}

/*static hgrep_error *
pcollector_check(flexarr *pcollector, size_t correctsize)
{
  size_t pcollector_sum = 0;
  hgrep_cstr *pcol = (hgrep_cstr*)pcollector->v;
  for (size_t j = 0; j < pcollector->size; j++)
    pcollector_sum += pcol[j].s;
  if (pcollector_sum != correctsize)
    return hgrep_set_error(1,"pcollector does not match count of found tags, %lu != %lu",pcollector_sum,correctsize);
  return NULL;
}*/

static void
pcollector_add(flexarr *pcollector, flexarr *dest, flexarr *source, size_t startp, size_t lastp, const hgrep_epattern *lastformat, uchar istable)
{
  if (!source->size)
    return;
  size_t prevsize = dest->size;
  flexarr_add(dest,source);
  if (!lastformat)
    goto END;
  if (istable) {
    if (startp != lastp) { //truncate previously added, now useless pcollector
      for (size_t i = lastp; i < pcollector->size; i++)
        ((hgrep_cstr*)pcollector->v)[i-lastp] = ((hgrep_cstr*)pcollector->v)[i];
      pcollector->size -= lastp-startp;
    }
  } else {
    pcollector->size = startp;
    *(hgrep_cstr*)flexarr_inc(pcollector) = (hgrep_cstr){(char const*)lastformat,dest->size-prevsize};
  }
  END: ;
  source->size = 0;
}

#ifdef HGREP_EDITING
static void
fcollector_add(const size_t lastp, const uchar isnodef, const hgrep_epattern *pattern, flexarr *pcollector, flexarr *fcollector)
{
  struct fcollector_pattern *fcols = (struct fcollector_pattern*)fcollector->v;
  for (size_t i = fcollector->size; i > 0; i--) {
    if (fcols[i-1].start < lastp)
      break;
    fcols[i-1].lvl++;
  }
  *(struct fcollector_pattern*)flexarr_inc(fcollector) = (struct fcollector_pattern){pattern,lastp,pcollector->size-1,0,isnodef};
}
#endif

static hgrep_error *hgrep_ematch_pre(hgrep *hg, const hgrep_epattern *patterns, size_t patternsl, flexarr *source, flexarr *dest, flexarr *pcollector
    #ifdef HGREP_EDITING
    , flexarr *fcollector
    #endif
    );

static hgrep_error *
hgrep_ematch_table(hgrep *hg, const hgrep_epattern *pattern, flexarr *source, flexarr *dest, flexarr *pcollector
    #ifdef HGREP_EDITING
    , flexarr *fcollector
    #endif
    )
{
  flexarr *patterns = (flexarr*)pattern->p;
  hgrep_epattern *patternsv = (hgrep_epattern*)patterns->v;
  size_t patternsl = patterns->size;

  if (source->size && pattern->istable&EPATTERN_SINGULAR && pattern->nodefl) {
    hgrep_error *err = NULL;
    flexarr *in = flexarr_init(sizeof(hgrep_compressed),1);
    hgrep_compressed *inv = (hgrep_compressed*)flexarr_inc(in);
    hgrep_compressed *sourcev = (hgrep_compressed*)source->v;

    for (size_t i = 0; i < source->size; i++) {
      *inv = sourcev[i];
      size_t lastp = pcollector->size;
      err = hgrep_ematch_pre(hg,patternsv,patternsl,in,dest,pcollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
        );
      if (err)
        break;
      #ifdef HGREP_EDITING
      if (pcollector->size-lastp)
        fcollector_add(lastp,1,pattern,pcollector,fcollector);
      #endif
    }
    flexarr_free(in);

    return err;
  }
  return hgrep_ematch_pre(hg,patternsv,patternsl,source,dest,pcollector
      #ifdef HGREP_EDITING
      ,fcollector
      #endif
      );
}

static hgrep_error *
hgrep_ematch_pre(hgrep *hg, const hgrep_epattern *patterns, size_t patternsl, flexarr *source, flexarr *dest, flexarr *pcollector
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

  size_t startp = pcollector->size;
  size_t lastp = startp;

  hgrep_epattern const *lastformat = NULL;

  for (size_t i = 0; i < patternsl; i++) {
    if (patterns[i].istable&EPATTERN_TABLE) {
      lastp = pcollector->size;
      err = hgrep_ematch_table(hg,&patterns[i],buf[0],buf[1],pcollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
              );
      if (err)
        goto ERR;
    } else if (patterns[i].p) {
      lastformat = &patterns[i];
      pattern_exec(hg,(hgrep_pattern*)patterns[i].p,buf[0],buf[1]);
    }

    //fprintf(stderr,"ematch %p %lu %lu %lu %u %lu\n",patterns,startp,lastp,pcollector->size,(patterns[i].istable&EPATTERN_NEWBLOCK),patterns[i].exprfl);

    #ifdef HGREP_EDITING
    if (patterns[i].istable&EPATTERN_NEWBLOCK && patterns[i].exprfl)
      fcollector_add(lastp,0,&patterns[i],pcollector,fcollector);
    #endif

    if ((patterns[i].istable&EPATTERN_TABLE &&
      !(patterns[i].istable&EPATTERN_NEWBLOCK)) || i == patternsl-1) {
      pcollector_add(pcollector,buf[2],buf[1],startp,lastp,lastformat,patterns[i].istable);
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
    /*err = pcollector_check(pcollector,buf[2]->size);
    if (err) //just for debugging
      goto ERR;*/

    err = nodes_output(hg,buf[2],pcollector
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
hgrep_ematch(hgrep *hg, const hgrep_epatterns *patterns, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl)
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
    #ifdef HGREP_EDITING
    flexarr *fcollector = flexarr_init(sizeof(struct fcollector_pattern),16);
    #endif
    err = hgrep_ematch_pre(hg,patterns->b,patterns->s,fsource,fdest,pcollector
        #ifdef HGREP_EDITING
        ,fcollector
        #endif
        );
    flexarr_free(pcollector);
    #ifdef HGREP_EDITING
    flexarr_free(fcollector);
    #endif
    return err;
}

static void
pattrib_free(struct hgrep_pattrib *attrib, size_t attribl)
{
  for (size_t i = 0; i < attribl; i++) {
    hgrep_regfree(&attrib[i].r[0]);
    if (attrib[i].flags&A_VAL_MATTERS)
      hgrep_regfree(&attrib[i].r[1]);
    if (attrib[i].position.s)
      free(attrib[i].position.b);
  }
  free(attrib);
}

void
hgrep_pfree(hgrep_pattern *p)
{
  if (p == NULL)
    return;
  if (!(p->flags&P_EMPTY)) {
    hgrep_regfree(&p->tag);

    pattrib_free(p->attrib,p->attribl);

    if (p->hooksl && p->hooks) {
      for (size_t i = 0; i < p->hooksl; i++) {
        if (p->hooks[i].flags&F_LIST) {
          if (p->hooks[i].match.l.s)
            free(p->hooks[i].match.l.b);
        } else
          hgrep_regfree(&p->hooks[i].match.r);
      }
      free(p->hooks);
    }
    if (p->position.s)
      free(p->position.b);
  }
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
      html_struct_handle(ptr,&i,size,0,nodes,hg,&err);
      if (err)
        return err;
    }
  }
  return NULL;
}

hgrep_error *
hgrep_fmatch(const char *ptr, const size_t size, FILE *output, const hgrep_pattern *pattern,
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
  t.pattern = pattern;
  t.nodef = nodef;
  t.nodefl = nodefl;
  t.flags = 0;
  if (output == NULL)
    output = stdout;
  t.output = output;
  flexarr *nodes = flexarr_init(sizeof(hgrep_node),HGREP_NODES_INC);
  t.attrib_buffer = (void*)flexarr_init(sizeof(hgrep_cstr_pair),ATTRIB_INC);
  hgrep_error *err = hgrep_analyze(ptr,size,nodes,&t);
  flexarr_free(nodes);
  t.nodes = NULL;
  t.nodesl = 0;
  flexarr_free((flexarr*)t.attrib_buffer);
  return err;
}

hgrep_error *
hgrep_efmatch(char *ptr, size_t size, FILE *output, const hgrep_epatterns *epatterns, int (*freeptr)(void *ptr, size_t size))
{
  if (epatterns->s == 0)
    return NULL;
  if (epatterns->s > 1)
    return hgrep_set_error(1,"fast mode cannot run in non linear mode");

  FILE *t = output;
  char *nptr;
  size_t fsize;

  flexarr *first = (flexarr*)epatterns->b[0].p;
  for (size_t i = 0; i < first->size; i++) {
    output = (i == first->size-1) ? t : open_memstream(&nptr,&fsize);

    if (((hgrep_epattern*)first->v)[i].istable&1)
      return hgrep_set_error(1,"fast mode cannot run in non linear mode");
    hgrep_error *err = hgrep_fmatch(ptr,size,output,(hgrep_pattern*)((hgrep_epattern*)first->v)[i].p,
      ((hgrep_epattern*)first->v)[i].nodef,((hgrep_epattern*)first->v)[i].nodefl);
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
