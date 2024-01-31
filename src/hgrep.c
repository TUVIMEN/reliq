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

static int
pattrib_match(const hgrep_node *hgn, const struct hgrep_pattrib *attrib, size_t attribl)
{
  regmatch_t pmatch;
  const hgrep_cstr_pair *a = hgn->attrib;
  for (size_t i = 0; i < attribl; i++) {
    uchar found = 0;
    for (ushort j = 0; j < hgn->attribl; j++) {
      if (!ranges_match(j,attrib[i].position_r,attrib[i].position_rl,hgn->attribl-1))
        continue;

      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)a[j].f.s;
      if (regexec(&attrib[i].r[0],a[j].f.b,1,&pmatch,REG_STARTEND) != 0)
        continue;

      if (attrib[i].flags&A_VAL_MATTERS) {
        pmatch.rm_so = 0;
        pmatch.rm_eo = (int)a[j].s.s;
        if (regexec(&attrib[i].r[1],a[j].s.b,1,&pmatch,REG_STARTEND) != 0)
          continue;
      }

      found = 1;
      break;
    }
    if (((attrib[i].flags&A_INVERT)==A_INVERT)^found)
      return 0;
  }
  return 1;
}

int
hgrep_match(const hgrep_node *hgn, const hgrep_pattern *p)
{
  if (p->flags&P_EMPTY)
    return 1;
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

  if (!pattrib_match(hgn,attrib,p->attribl))
    return 0^rev;

  if (p->flags&P_MATCH_INSIDES) {
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)hgn->insides.s;
    if ((regexec(&p->insides,hgn->insides.b,1,&pmatch,REG_STARTEND) != 0)^((p->flags&P_INVERT_INSIDES)==P_INVERT_INSIDES))
      return 0^rev;
  }

  return 1^rev;
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
    if (memcomp(match_functions[i].name.b,src+t,match_functions[i].name.s,*pos-t)) {
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
      err = ranges_comp(src,pos,*size,&p->attribute_r,&p->attribute_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_LEVEL) {
      err = ranges_comp(src,pos,*size,&p->position_r,&p->position_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_SIZE) {
      err = ranges_comp(src,pos,*size,&p->size_r,&p->size_rl);
      if (err)
        return err;
    } else if (match_functions[i].flags&F_CHILD_COUNT) {
      err = ranges_comp(src,pos,*size,&p->child_count_r,&p->child_count_rl);
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
get_pattribs(char *pat, size_t *size, hgrep_pattern *p, struct hgrep_pattrib **attrib, size_t *attribl, char *regex_tmp, int regexflags)
{
  hgrep_error *err = NULL;
  struct hgrep_pattrib pa;
  flexarr *pattrib = flexarr_init(sizeof(struct hgrep_pattrib),PATTRIB_INC);
  for (size_t i = 0; i < *size;) {
    while_is(isspace,pat,i,*size);
    if (i >= *size)
      break;

    if (pat[i] == '@') {
      err = match_function_handle(p,pat,&i,size,regexflags);
      if (err)
        break;
      continue;
    }

    memset(&pa,0,sizeof(struct hgrep_pattrib));
    char isset = 0;

    if (pat[i] == '+') {
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
      err = ranges_comp(pat,&i,*size,&pa.position_r,&pa.position_rl);
      if (err)
        break;
    } else if (pat[i] == '\\' && pat[i+1] == '[')
      i++;

    if (i >= *size)
      break;

    if (isset == 0)
      pa.flags |= A_INVERT;

    if (pat[i] == '@')
      continue;

    if (shortcut == '.' || shortcut == '#') {
      pa.flags |= A_VAL_MATTERS;
      if (shortcut == '.') {
        memcpy(regex_tmp,"^class$",8);
      } else
        memcpy(regex_tmp,"^id$",5);
      if (regcomp(&pa.r[0],regex_tmp,regexflags))
        break;

      err = get_pattern_word(regex_tmp,pat,&i,size,' ',(regexflags&REG_EXTENDED) ? 1 : 0);
      if (err || regcomp(&pa.r[1],regex_tmp,regexflags))
        break;
    } else {
      err = get_pattern_fullline(regex_tmp,pat,&i,size,'=');
      if (err || regcomp(&pa.r[0],regex_tmp,regexflags))
        break;

      while_is(isspace,pat,i,*size);
      if (i >= *size)
        goto ADD;
      if (pat[i] == '=') {
        i++;
        while_is(isspace,pat,i,*size);
        if (i >= *size)
          break;
    
        err = get_pattern_fullline(regex_tmp,pat,&i,size,' ');
        if (err || regcomp(&pa.r[1],regex_tmp,regexflags))
          break;
        pa.flags |= A_VAL_MATTERS;
      } else {
        pa.flags &= ~A_VAL_MATTERS;
        goto ADD_SKIP;
      }
    }

    ADD: ;
    if (pat[i] != '+' && pat[i] != '-' && pat[i] != '@')
      i++;
    ADD_SKIP: ;
    memcpy(flexarr_inc(pattrib),&pa,sizeof(struct hgrep_pattrib));
  }
  flexarr_conv(pattrib,(void**)attrib,attribl);
  return err;
}

hgrep_error *
hgrep_pcomp(const char *pattern, size_t size, hgrep_pattern *p, const uchar flags)
{
  if (size == 0)
    return NULL;
  hgrep_error *err = NULL;
  memset(p,0,sizeof(hgrep_pattern));
  int regexflags = REG_NEWLINE|REG_NOSUB;
  if (flags&HGREP_ICASE)
    regexflags |= REG_ICASE;
  if (flags&HGREP_EREGEX)
    regexflags |= REG_EXTENDED;

  char regex_tmp[REGEX_PATTERN_SIZE];
  char *pat = memdup(pattern,size);
  size_t pos=0;
  while_is(isspace,pat,pos,size);

  if (pos >= size) {
    p->flags |= P_EMPTY;
    goto END;
  }
  if (pat[pos] == '!') {
    p->flags |= P_INVERT;
    pos++;
  }

  err = get_pattern_fullline(regex_tmp,pat,&pos,&size,' ');
  if (err || regcomp(&p->tag,regex_tmp,regexflags))
    goto END;

  size_t s = size-pos;
  err = get_pattribs(pat+pos,&s,p,&p->attrib,&p->attribl,regex_tmp,regexflags);
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
}*/

static void
hgrep_epattern_free(hgrep_epattern *p)
{
  #ifdef HGREP_EDITING
  format_free(p->nodef,p->nodefl);
  #else
  if (p->nodef)
    free(p->nodef);
  #endif
  hgrep_pfree((hgrep_pattern*)p->p);
}

static void
hgrep_epatterns_free_pre(flexarr *epatterns)
{
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
  uchar next = 0;

  hgrep_str nodef,exprf;

  while_is(isspace,src,*pos,s);
  for (size_t j; i < s; i++) {
    j = i;

    nodef.b = NULL;
    nodef.s = 0;
    exprf.b = NULL;
    exprf.s = 0;
    uchar haspattern = 0;
    patternl = 0;

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
        } else
          goto PASS;
      }
      if (src[i] == '[') {
        i++;
        while (i < s && src[i] != ']')
          i++;
        if (i < s && src[i] == ']') {
          i++;
        } else
          goto PASS;
      }

      if (src[i] == ',' || src[i] == ';' || src[i] == '{' || src[i] == '}') {
        if (exprf.b) {
          exprf.s = i-(exprf.b-src);
        } else if (nodef.b)
          nodef.s = i-(nodef.b-src);

        if (src[i] == '{') {
          next = 2;
        } else if (src[i] == '}') {
          next = 3;
        } else {
          next = (src[i] == ',') ? 1 : 0;
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

    if (next != 2) {
      if (nodef.b) {
        size_t g=0,t=nodef.s;
        *err = format_comp(nodef.b,&g,&t,&epattern.nodef,&epattern.nodefl);
        s -= nodef.s-t;
        if (*err)
          goto EXIT1;
      }
      #ifdef HGREP_EDITING
      if (exprf.b) {
        size_t g=0,t=exprf.s;
        *err = format_comp(exprf.b,&g,&t,&epattern.exprf,&epattern.exprfl);
        s -= nodef.s-t;
        if (*err)
          goto EXIT1;
      }
      #endif
    }

    hgrep_epattern *new = NULL;
    if ((next != 3 || src[j] != '}') && (next >= 2 || patternl || haspattern)) {
      epattern.p = malloc(sizeof(hgrep_pattern));
      if (!patternl) {
        ((hgrep_pattern*)epattern.p)->flags |= P_EMPTY;
      } else if (next != 2 || next < 2) {
        *err = hgrep_pcomp(src+j,patternl,(hgrep_pattern*)epattern.p,flags);
        if (*err)
          goto EXIT1;
      }

      new = (hgrep_epattern*)flexarr_inc(acurrent->p);
      *new = epattern;
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
    flexarr_conv(ret,(void**)&epatterns->b,&epatterns->s);
  } else
    epatterns->s = 0;
  return err;
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
        x->lvl = lvl;
        x->id = n;
      }
      nodes[n].lvl += lvl;
    }
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
    return;
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
}

static hgrep_error *
hgrep_ematch_pre(hgrep *hg, const hgrep_epattern *patterns, size_t patternsl, flexarr *source, flexarr *dest, flexarr *pcollector)
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
      err = hgrep_ematch_pre(hg,(hgrep_epattern*)((flexarr*)patterns[i].p)->v,((flexarr*)patterns[i].p)->size,buf[0],buf[1],pcollector);
      if (err)
        goto ERR;
    } else if (patterns[i].p) {
      lastformat = &patterns[i];
      pattern_exec(hg,(hgrep_pattern*)patterns[i].p,buf[0],buf[1]);
    }

    if ((patterns[i].istable&EPATTERN_TABLE &&
      !(patterns[i].istable&EPATTERN_NEWBLOCK)) || i == patternsl-1) {
      pcollector_add(pcollector,buf[2],buf[1],startp,lastp,lastformat,patterns[i].istable);
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
    /*err = pcollector_check(pcollector,buf[2]->size);
    if (err) //just for debugging
      goto ERR;*/

    err = nodes_output(hg,buf[2],pcollector);
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
    err = hgrep_ematch_pre(hg,patterns->b,patterns->s,fsource,fdest,pcollector);
    flexarr_free(pcollector);
    return err;
}

static void
pattrib_free(struct hgrep_pattrib *attrib, size_t attribl)
{
  for (size_t i = 0; i < attribl; i++) {
    regfree(&attrib[i].r[0]);
    if (attrib[i].flags&A_VAL_MATTERS)
      regfree(&attrib[i].r[1]);
    if (attrib[i].position_rl)
      free(attrib[i].position_r);
  }
  free(attrib);
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
    pattrib_free(p->attrib,p->attribl);
    if (p->position_rl)
      free(p->position_r);
    if (p->attribute_rl)
      free(p->attribute_r);
    if (p->size_rl)
      free(p->size_r);
    if (p->child_count_rl)
      free(p->child_count_r);
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
