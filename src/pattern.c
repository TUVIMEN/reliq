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
#include <regex.h>

#include "ctype.h"
#include "utils.h"
#include "pattern.h"

typedef unsigned char uchar;

static void
regcomp_set_flags(uint16_t *flags, const char *src, const size_t len)
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
regcomp_get_flags(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char *flags)
{
  size_t p = *pos;
  pattern->flags = RELIQ_PATTERN_TRIM|RELIQ_PATTERN_PASS_WHOLE|RELIQ_PATTERN_MATCH_FULL|RELIQ_PATTERN_TYPE_STR;
  pattern->range.s= 0;

  if (flags)
    regcomp_set_flags(&pattern->flags,flags,strlen(flags));

  if (src[p] == '\'' || src[p] == '"' || src[p] == '*')
    return;

  while (p < size && isalpha(src[p]))
    p++;
  if (p >= size || src[p] != '>')
    return;

  regcomp_set_flags(&pattern->flags,src+*pos,p-*pos);

  *pos = p+1;
  return;
}

static reliq_error *
regcomp_add_pattern(reliq_pattern *pattern, const char *src, const size_t size, uchar (*checkstrclass)(char))
{
  uint16_t match = pattern->flags&RELIQ_PATTERN_MATCH,
    type = pattern->flags&RELIQ_PATTERN_TYPE;

  if (!size) {
    pattern->flags |= RELIQ_PATTERN_EMPTY;
    return NULL;
  }

  if (type == RELIQ_PATTERN_TYPE_STR) {
    if (checkstrclass)
      for (size_t i = 0; i < size; i++)
        if (!checkstrclass(src[i]))
          return script_err("pattern %lu: '%c' is a character impossible to find in searched field",i,src[i]);
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

    int r = regcomp(&pattern->match.reg,tmp,regexflags);
    free(tmp);
    if (r != 0)
      return script_err("pattern: regcomp: could not compile pattern");
  }
  return NULL;
}

void
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

reliq_error *
reliq_regcomp(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char delim, const char *flags, uchar (*checkstrclass)(char))
{
  reliq_error *err=NULL;
  size_t i = *pos;

  memset(pattern,0,sizeof(reliq_pattern));
  regcomp_get_flags(pattern,src,&i,size,flags);

  if (i && i < size && src[i-1] == '>' && src[i] == '[') {
    if ((err = range_comp(src,&i,size,&pattern->range)))
      goto END;
    if (i >= size || src[i] == delim || isspace(src[i])) {
      pattern->flags |= RELIQ_PATTERN_ALL;
      goto END;
    }
  }

  if ((i+1 == size || (i+1 < size &&
    (isspace(src[i+1]) || src[i+1] == delim)))
    && src[i] == '*') {
    i++;
    pattern->flags |= RELIQ_PATTERN_ALL;
    goto END;
  }

  char *str;
  size_t strl;
  if ((err = get_quoted(src,&i,size,delim,&str,&strl)))
    goto END;

  err = regcomp_add_pattern(pattern,str,strl,checkstrclass);
  free(str);
  END: ;
  *pos = i;
  if (err)
    reliq_regfree(pattern);
  return err;
}

static int
regexec_match_str(const reliq_pattern *pattern, reliq_cstr *str)
{
  reliq_pattern const *p = pattern;
  uint16_t match = p->flags&RELIQ_PATTERN_MATCH,
    icase = p->flags&RELIQ_PATTERN_CASE_INSENSITIVE;

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
regexec_match_pattern(const reliq_pattern *pattern, reliq_cstr *str)
{
  uint16_t type = pattern->flags&RELIQ_PATTERN_TYPE;
  if (type == RELIQ_PATTERN_TYPE_STR) {
    return regexec_match_str(pattern,str);
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
regexec_match_word(const reliq_pattern *pattern, reliq_cstr *str)
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
    if (regexec_match_pattern(pattern,&t))
      return 1;

    ptr = NULL;
  }
  return 0;
}

int
reliq_regexec(const reliq_pattern *pattern, const char *src, const size_t size)
{
  uint16_t pass = pattern->flags&RELIQ_PATTERN_PASS;
  uchar invert = (pattern->flags&RELIQ_PATTERN_INVERT) ? 1 : 0;
  if ((!range_match(size,&pattern->range,-1)))
    return invert;

  if (pattern->flags&RELIQ_PATTERN_ALL)
    return !invert;

  if (pattern->flags&RELIQ_PATTERN_EMPTY)
    return (size == 0) ? !invert : invert;

  if (!src)
    return invert;

  reliq_cstr str = {src,size};

  if (pass == RELIQ_PATTERN_PASS_WORD)
    return regexec_match_word(pattern,&str)^invert;

  if (pattern->flags&RELIQ_PATTERN_TRIM)
    memtrim((void const**)&str.b,&str.s,src,size);

  return regexec_match_pattern(pattern,&str)^invert;
}