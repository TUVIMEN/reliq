/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#include "ext.h"

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "ctype.h"
#include "utils.h"
#include "flexarr.h"
#include "sink.h"
#include "format.h"
#include "edit.h"

#define SED_MAX_PATTERN_SPACE (1<<20) //!! this causes huge memory allocation, should be replaced by reusing buffers for the whole expression when it's executed

#define SED_A_EMPTY 0x0
#define SED_A_REVERSE 0x1
#define SED_A_NUM1 0x2
#define SED_A_CHECKFIRST 0x4
#define SED_A_REG1 0x8
#define SED_A_NUM2 0x10
#define SED_A_STEP 0x20
#define SED_A_ADD 0x40
#define SED_A_MULTIPLE 0x80
#define SED_A_END 0x100
#define SED_A_REG2 0x200
#define SED_A_FOUND1 0x400
#define SED_A_FOUND2 0x800

struct sed_address {
  unsigned int num[2];
  regex_t reg[2];
  unsigned int fline;
  uint16_t flags; //SED_A_
};

static void
sed_address_comp_number(const char *src, size_t *pos, const size_t size, uint32_t *result)
{
  size_t s;
  *result = get_dec(src+*pos,size-*pos,&s);
  *pos += s;
}

static reliq_error *
sed_address_comp_regex(const char *src, size_t *pos, const size_t size, regex_t *preg, const int eflags)
{
  char regex_delim = '/';
  if (*pos+1 < size && src[*pos] == '\\')
    regex_delim = src[++(*pos)];
  (*pos)++;
  size_t regex_end = *pos;
  while (regex_end < size && src[regex_end] != regex_delim && src[regex_end] != '\n') {
    if (src[regex_end] == '\\')
      regex_end++;
    regex_end++;
  }
  if (regex_end >= size || src[regex_end] != regex_delim)
    return script_err("sed: char %lu: unterminated address regex",*pos);
  if (regex_end == *pos)
    return script_err("sed: char %lu: no previous regular expression",*pos);
  char tmp[REGEX_PATTERN_SIZE];
  if (regex_end-*pos >= REGEX_PATTERN_SIZE-1)
    return script_err("sed: char %lu: regex is too long",regex_end);

  size_t len = regex_end-*pos;
  memcpy(tmp,src+*pos,len);
  splchars_conv(tmp,&len);
  tmp[len] = 0;

  *pos = regex_end+1;
  if (regcomp(preg,tmp,eflags))
    return script_err("sed: char %lu: couldn't compile regex",regex_end);
  return NULL;
}

static reliq_error *
sed_address_comp_reverse(const char *src, size_t *pos, const size_t size, struct sed_address *address)
{
  while_is(isspace,src,*pos,size);
  if (*pos < size && src[*pos] == '!') {
    address->flags |= SED_A_REVERSE;
    (*pos)++;
  }
  if (address->flags&SED_A_NUM1 && address->num[0] == 0) {
    if (!(address->flags&SED_A_REG2))
      return script_err("sed: char %lu: invalid use of line address 0",*pos);
    address->flags |= SED_A_CHECKFIRST;
  }
  return NULL;
}

static reliq_error *
sed_address_comp_pre(const char *src, size_t *pos, const size_t size, struct sed_address *address, int eflags)
{
  reliq_error *err = NULL;
  address->flags = 0;
  while_is(isspace,src,*pos,size);
  if (*pos >= size)
    return NULL;
  if (isdigit(src[*pos])) {
    sed_address_comp_number(src,pos,size,&address->num[0]);
    address->flags |= SED_A_NUM1;
  } else if (src[*pos] == '\\' || src[*pos] == '/') {
    if ((err = sed_address_comp_regex(src,pos,size,&address->reg[0],eflags)))
      return err;
    address->flags |= SED_A_REG1;
  } else if (src[*pos] == '$') {
    address->flags |= SED_A_END;
    (*pos)++;
  }

  while_is(isspace,src,*pos,size);

  if (*pos >= size || src[*pos] == '!')
    return NULL;
  if (src[*pos] == '~') {
    if (address->flags&SED_A_REG1)
      return NULL;
    (*pos)++;
    while_is(isspace,src,*pos,size);
    sed_address_comp_number(src,pos,size,&address->num[1]);
    address->flags |= SED_A_NUM2|SED_A_STEP;
    return NULL;
  }

  uchar onlynumber = 0;

  if (src[*pos] != ',')
    return NULL;
  (*pos)++;
  if (*pos >= size)
    return NULL;
  if (src[*pos] == '+' || src[*pos] == '~') {
    onlynumber = 1;
    address->flags |= ((src[*pos] == '+') ? SED_A_ADD : SED_A_MULTIPLE);
    (*pos)++;
    while_is(isspace,src,*pos,size);
  } else  if (src[*pos] == '\\' || src[*pos] == '/') {
    err = sed_address_comp_regex(src,pos,size,&address->reg[1],eflags);
    address->flags |= SED_A_REG2;
    return err;
  }

  if (*pos >= size)
    return NULL;
  if (isdigit(src[*pos])) {
    sed_address_comp_number(src,pos,size,&address->num[1]);
    address->flags |= SED_A_NUM2;
  } else if (onlynumber)
    return NULL;

  if (*pos < size && src[*pos] == '$') {
    address->flags |= SED_A_END;
    (*pos)++;
  }

  return NULL;
}

static void
sed_address_free(struct sed_address *a)
{
  if (a->flags&SED_A_REG1)
    regfree(&a->reg[0]);
  if (a->flags&SED_A_REG2)
    regfree(&a->reg[1]);
}

static reliq_error *
sed_address_comp(const char *src, size_t *pos, const size_t size, struct sed_address *address, int eflags)
{
  reliq_error *err = sed_address_comp_pre(src,pos,size,address,eflags|REG_NOSUB);
  if (err) {
    sed_address_free(address);
    return err;
  }
  return sed_address_comp_reverse(src,pos,size,address);
}

static int
sed_address_exec(const char *src, const size_t size, const uint32_t line, const uchar islast, struct sed_address *address)
{
  if (address->flags == SED_A_EMPTY)
    return 1;
  regmatch_t pmatch;
  uchar rev=0,range=0,first=0;
  uint16_t flags = address->flags;

  if (flags&SED_A_REVERSE)
    rev = 1;
  if (flags&(SED_A_REG2|SED_A_NUM2) || (flags&(SED_A_NUM1|SED_A_REG1) && flags&SED_A_END))
    range = 1;

  if (flags&SED_A_STEP)
    return ((line-address->num[0])%address->num[1] == 0)^rev;

  if (!range && flags&SED_A_END)
    return (islast > 0)^rev;

  if (flags&SED_A_NUM1) {
    first = (range ? (line >= address->num[0]) : (address->num[0] == line));
  } else if (flags&SED_A_REG1) {
    if (range && flags&SED_A_FOUND1) {
      first = 1;
    } else {
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)size;
      first = (regexec(&address->reg[0],src,1,&pmatch,REG_STARTEND) == 0);
      if (first) {
        address->flags |= SED_A_FOUND1;
        address->flags &= ~SED_A_FOUND2;
        flags = address->flags;
        address->fline = line;
      }
    }
  }

  if (!range || (!first && !rev))
    return first^rev;

  if (flags&SED_A_ADD) {
    if (flags&SED_A_NUM1)
      return (line <= address->num[0]+address->num[1])^rev;

    if (!(flags&SED_A_FOUND1))
      return rev;
    uchar r = (line <= address->fline+address->num[1]);
    if (!r)
      address->flags &= ~SED_A_FOUND1;
    return r^rev;
  }
  if (flags&SED_A_MULTIPLE) {
    size_t prevline = address->num[0];
    if (flags&SED_A_FOUND1)
      prevline = address->fline;
    if (line == prevline)
      return !rev;
    if (flags&SED_A_FOUND2)
      return rev;
    if ((line%address->num[1]) == 0) {
      address->flags |= SED_A_FOUND2;
      address->flags &= ~SED_A_FOUND1;
    }
    return !rev;
  }
  if (flags&SED_A_END)
    return first^rev;
  if (flags&SED_A_NUM2)
    return (first&(line <= address->num[1]))^rev;
  if (flags&flags&SED_A_REG2) {
    if (flags&SED_A_REG1 && !(flags&SED_A_FOUND1))
      return rev;
    if (line == 1 && !(flags&SED_A_CHECKFIRST))
      return first^rev;
    if (flags&SED_A_FOUND2) {
      return rev;
    } else {
      pmatch.rm_so = 0;
      pmatch.rm_eo = (int)size;
      if (!regexec(&address->reg[1],src,1,&pmatch,REG_STARTEND)) {
        address->flags |= SED_A_FOUND2;
        address->flags &= ~SED_A_FOUND1;
      }
      return first^rev;
    }
  }

  return 0^rev;
}

#define SED_EXPRESSION_S_NUMBER 0x00ffffff
#define SED_EXPRESSION_S_GLOBAL 0x01000000
#define SED_EXPRESSION_S_ICASE 0x02000000
#define SED_EXPRESSION_S_PRINT 0x04000000

struct sed_expression {
  uint16_t lvl;
  struct sed_address address;
  char name;
  reliq_cstr arg;
  void *arg1;
  void *arg2; //SED_EXPRESSION_S_
};

static void
sed_expression_free(struct sed_expression *e)
{
  sed_address_free(&e->address);
  if (e->name == 'y') {
    if (e->arg1)
      free(e->arg1);
    if (e->arg2)
      free(e->arg2);
  } else if (e->name == 's' && e->arg1) {
    regfree(e->arg1);
    free(e->arg1);
  }
}

#define SC_ONLY_NEWLINE 0x1 //can be ended only by a newline eg. i, a, c commands
#define SC_ARG 0x2
#define SC_ARG_OPTIONAL 0x4
#define SC_NOADDRESS 0x8
#define SC_ESCAPE_NEWLINE 0x10

struct sed_command {
  char name;
  uint16_t flags; //SC_
} sed_commands[] = {
  {'{',0},
  {'}',SC_NOADDRESS},
  {'#',SC_ONLY_NEWLINE|SC_NOADDRESS|SC_ARG},
  {':',SC_ARG|SC_NOADDRESS},
  {'=',0},
  {'a',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'i',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'q',0},
  {'c',SC_ARG|SC_ONLY_NEWLINE|SC_ESCAPE_NEWLINE},
  {'z',0},
  {'d',0},
  {'D',0},
  {'h',0},
  {'H',0},
  {'g',0},
  {'G',0},
  {'n',0},
  {'N',0},
  {'p',0},
  {'P',0},
  {'s',SC_ARG},
  {'b',SC_ARG|SC_ARG_OPTIONAL},
  {'t',SC_ARG|SC_ARG_OPTIONAL},
  {'T',SC_ARG|SC_ARG_OPTIONAL},
  {'x',0},
  {'y',SC_ARG},
};


struct sed_command *
sed_get_command(char name)
{
  for (size_t i = 0; i < LENGTH(sed_commands); i++)
    if (name == sed_commands[i].name)
      return &sed_commands[i];
  return NULL;
}

static void
sed_script_free(flexarr *script) //script: struct sed_expression
{
  const size_t size = script->size;
  for (size_t i = 0; i < size; i++)
    sed_expression_free(&((struct sed_expression*)script->v)[i]);
  flexarr_free(script);
}

static reliq_error *
sed_UNTERMINATED(const size_t pos, const char name, const char foundchar)
{
  return script_err(name == ':' ? "sed: char %lu: \"%c\" lacks a label" :
    "sed: char %lu: unterminated `%c' command",pos,foundchar);
}

static reliq_error *
sed_DIFFERENT_LENGHTS(const size_t pos, const char name)
{
  return script_err("sed: char %lu: strings for `%c' command are different lenghts",pos,name);
}

static reliq_error *
sed_EXTRACHARS(const size_t pos)
{
  return script_err("sed: char %lu: extra characters after command",pos);
}

static void
sed_comp_onlynewline(const char *src, size_t *pos, const size_t size, const uint16_t flags)
{
  size_t p = *pos;
  if (flags&SC_ESCAPE_NEWLINE) {
    while (p < size && src[p] != '\n') {
      if (src[p] == '\\')
        p++;
      p++;
    }
  } else while (p < size && src[p] != '\n')
    p++;
  *pos = p;
}

static reliq_error *
sed_comp_sy_arg(const char *src, size_t *pos, const size_t size, const char argdelim, const char name, const char foundchar, reliq_cstr *result)
{
  reliq_error *err = NULL;
  size_t p = (*pos)+1;
  const char *res = src+p;
  size_t resl=0;

  while (p < size && src[p] != argdelim && src[p] != '\n') {
    if (src[p] == '\\')
      p++;
    p++;
  }
  resl = p-(res-src);
  if (p >= size || src[p] != argdelim)
    err = sed_UNTERMINATED(p,name,foundchar);

  *pos = p;
  result->b = res;
  result->s = resl;

  return err;
}

static reliq_error *
sed_comp_y(const size_t pos, const char name, struct sed_expression *sedexpr, reliq_cstr *second, reliq_cstr *third)
{
  if (third->s)
    return sed_EXTRACHARS(pos);

  sedexpr->arg1 = calloc(256,sizeof(char));
  sedexpr->arg2 = calloc(256,sizeof(uchar));
  reliq_cstr first = sedexpr->arg;
  size_t i=0,j=0;

  const size_t size = second->s;
  for (; i < first.s && j < size; i++, j++) {
    char c1 = first.b[i];
    size_t traversed;
    if (c1 == '\\') {
      i++;
      c1 = splchar2(first.b+i,first.s-i,&traversed);
      i += traversed-1;
    }
    char c2 = second->b[j];
    if (c2 == '\\') {
      j++;
      c2 = splchar2(second->b+j,second->s-j,&traversed);
      j += traversed-1;
    }

    ((uchar*)sedexpr->arg2)[(uchar)c1] = 1;
    ((char*)sedexpr->arg1)[(uchar)c1] = c2;
  }

  if (i != first.s || j != second->s)
    return sed_DIFFERENT_LENGHTS(pos,name);

  return NULL;
}

static reliq_error *
sed_comp_s_flags(const char *src, const size_t size, const size_t pos, int *eflags, uint64_t *args)
{
  uint64_t arg2 = 0;
  for (size_t i = 0; i < size; i++) {
    if (src[i] == 'i') {
      if (arg2&SED_EXPRESSION_S_ICASE)
        goto S_ARG_REPEAT;
      arg2 |= SED_EXPRESSION_S_ICASE;
      *eflags |= REG_ICASE;
    } else if (src[i] == 'g') {
      if (arg2&SED_EXPRESSION_S_GLOBAL)
        goto S_ARG_REPEAT;
      arg2 |= SED_EXPRESSION_S_GLOBAL;
    } else if (src[i] == 'p') {
      if (arg2&SED_EXPRESSION_S_PRINT) {
        S_ARG_REPEAT: ;
        return script_err("sed: char %lu: multiple `%c' options to `s' command",pos,src[i]);
      }
      arg2 |= SED_EXPRESSION_S_PRINT;
    } else if (isdigit(src[i])) {
      if (arg2&SED_EXPRESSION_S_NUMBER)
        return script_err("sed: char %lu: multiple number options to `s' command",pos);
      uint32_t c = number_handle(src,&i,size);
      if (!c)
        return script_err("sed: char %lu: number option to `s' may not be zero",pos);
      arg2 |= c&SED_EXPRESSION_S_NUMBER;
    } else if (!isspace(src[i]))
      return script_err("sed: char %lu: unknown option to `s'",pos);
  }
  *args = arg2;
  return NULL;
}

static reliq_error *
sed_comp_s(const char *src, const size_t pos, int eflags, struct sed_expression *sedexpr, reliq_cstr *second, reliq_cstr *third)
{
  reliq_error *err = NULL;
  char tmp[REGEX_PATTERN_SIZE];

  if (sedexpr->arg.s >= REGEX_PATTERN_SIZE-1)
    return script_err("sed: `s' pattern is too big");

  if ((err = sed_comp_s_flags(third->b,third->s,pos,&eflags,(uint64_t*)(void*)&sedexpr->arg2)))
    return err;

  size_t len = sedexpr->arg.s;
  memcpy(tmp,sedexpr->arg.b,sedexpr->arg.s);
  splchars_conv(tmp,&len);
  tmp[len] = 0;

  sedexpr->arg1 = malloc(sizeof(regex_t));
  if (regcomp(sedexpr->arg1,tmp,eflags)) {
    free(sedexpr->arg1);
    sedexpr->arg1 = NULL;
    return script_err("sed: char %lu: couldn't compile regex",sedexpr->arg.b-src);
  }

  sedexpr->arg = *second;
  return err;
}

static reliq_error *
sed_comp_sy(const char *src, size_t *pos, const size_t size, const char name, int eflags, struct sed_expression *sedexpr)
{
  reliq_error *err = NULL;
  size_t p = *pos;
  char argdelim = (p < size) ? src[p] : 0;

  if ((err = sed_comp_sy_arg(src,&p,size,argdelim,name,sedexpr->name,&sedexpr->arg)))
    goto END;

  if (!sedexpr->arg.s) {
    if (name == 'y') {
      err = sed_DIFFERENT_LENGHTS(p,name);
    } else
      err = script_err("sed: char %lu: no previous regular expression",p);
    goto END;
  }

  reliq_cstr second,third;

  if ((err = sed_comp_sy_arg(src,&p,size,argdelim,name,sedexpr->name,&second)))
    goto END;

  third.b = src+(++p);
  while (p < size && src[p] != '\n' && src[p] != '#' && src[p] != ';' && src[p] != '}')
    p++;
  third.s = p-(third.b-src);

  if (name == 'y') {
    err = sed_comp_y(p,name,sedexpr,&second,&third);
  } else
    err = sed_comp_s(src,p,eflags,sedexpr,&second,&third);

  END:
  *pos = p;
  return err;
}

static reliq_error *
sed_comp_check_labels(flexarr *script) //script: struct sed_expression
{
  struct sed_expression *scriptv = (struct sed_expression*)script->v;
  const size_t size = script->size;
  for (size_t i = 0; i < size; i++) {
    if ((scriptv[i].name == 'b' || scriptv[i].name == 't' || scriptv[i].name == 'T') && scriptv[i].arg.s) {
      uchar found = 0;
      for (size_t j = 0; j < size; j++) {
        if (scriptv[j].name == ':' &&
          streq(scriptv[i].arg,scriptv[j].arg)) {
          found = 1;
          break;
        }
      }
      if (!found)
        return script_err("sed: can't find label for jump to `%.*s'",scriptv[i].arg.s,scriptv[i].arg.b);
    }
  }
  return NULL;
}

static reliq_error *
sed_script_comp_pre(const char *src, const size_t size, int eflags, flexarr **script) //script: struct sed_expression
{
  reliq_error *err;
  size_t pos = 0;
  *script = flexarr_init(sizeof(struct sed_expression),2<<4);
  struct sed_expression *sedexpr = (struct sed_expression*)flexarr_inc(*script);
  sedexpr->address.flags = 0;
  sedexpr->arg1 = NULL;
  sedexpr->arg2 = NULL;
  struct sed_command *command;
  uint16_t lvl = 0;
  while (pos < size) {
    sedexpr->name = '\0';
    while (pos < size && (isspace(src[pos]) || src[pos] == ';'))
      pos++;

    size_t addrdiff = pos;
    if ((err = sed_address_comp(src,&pos,size,&sedexpr->address,eflags)))
      return err;
    while_is(isspace,src,pos,size);
    if (pos >= size) {
      if (pos-addrdiff)
        return script_err("sed: char %lu: missing command",pos);
      sed_expression_free(sedexpr);
      break;
    }
    command = sed_get_command(src[pos]);
    if (!command)
      return script_err("sed: char %lu: unknown command: `%c'",pos,src[pos]);
    if (command->flags&SC_NOADDRESS && sedexpr->address.flags)
      return script_err("sed: char %lu: %c doesn't want any addresses",pos,src[pos]);
    sedexpr->name = src[pos];
    sedexpr->lvl = lvl;
    if (sedexpr->name == '{' || sedexpr->name == '}') {
      if (sedexpr->name == '}') {
        CLOSING_BRACKET: ;
        if (!lvl)
          return script_err("sed: char %lu: unexpected `}'",pos);
        lvl--;
      } else
        lvl++;
      sedexpr = (struct sed_expression*)flexarr_inc(*script);
      sedexpr->address.flags = 0;
      pos++;
      continue;
    }
    pos++;
    while (pos < size && src[pos] != '\n' && isspace(src[pos]))
      pos++;

    char const *argstart = src+pos;
    if (command->flags&SC_ONLY_NEWLINE) {
      sed_comp_onlynewline(src,&pos,size,command->flags);
    } else if (command->name == 's' || command->name == 'y') {
      if ((err = sed_comp_sy(src,&pos,size,command->name,eflags,sedexpr)))
        return err;
    } else if (command->name == ':') {
      while (pos < size && src[pos] != '\n' && src[pos] != '#' && src[pos] != ';' && src[pos] != '}' && !isspace(src[pos]))
        pos++;
    } else
      while (pos < size && src[pos] != '\n' && src[pos] != '#' && src[pos] != ';' && src[pos] != '}')
        pos++;
    if (command->name != 's' && command->name != 'y') {
      size_t argend = pos;
      if (!(command->flags&SC_ONLY_NEWLINE))
        while (argend > (size_t)(argstart-src) && isspace(src[argend-1]))
          argend--;
      sedexpr->arg.b = argstart;
      sedexpr->arg.s = argend-(argstart-src);
      if (command->name != '#') {
        if (!sedexpr->arg.s && command->flags&SC_ARG && !(command->flags&SC_ARG_OPTIONAL))
          return sed_UNTERMINATED(pos,command->name,sedexpr->name);
        if (sedexpr->arg.s && !(command->flags&SC_ARG))
          return sed_EXTRACHARS(pos);
      }
    }
    sedexpr = (struct sed_expression*)flexarr_inc(*script);
    sedexpr->address.flags = 0;
    sedexpr->arg1 = NULL;
    sedexpr->arg2 = NULL;
    if (pos >= size)
      break;
    if (src[pos] == '}') {
      sedexpr->name = src[pos];
      sedexpr->lvl = lvl;
      goto CLOSING_BRACKET;
    }
  }
  /*if (sedexpr->arg1) //random solution for it giving segfault
    sed_expression_free(sedexpr);*/
  if (lvl)
    return script_err("sed: char %lu: unmatched `{'",pos);
  flexarr_dec(*script);

  return sed_comp_check_labels(*script);
}

static reliq_error *
sed_script_comp(const char *src, const size_t size, int eflags, flexarr **script) //script: script: struct sed_expression
{
  reliq_error *err = NULL;
  if ((err = sed_script_comp_pre(src,size,eflags,script))) {
    sed_script_free(*script);
    *script = NULL;
  }
  return err;
}

static reliq_error *
sed_pre_edit(const char *src, const size_t size, SINK *output, char *buffers[3], flexarr *script, const char linedelim, uchar silent) //script: struct sed_expression
{
  char *patternsp = buffers[0],
    *buffersp = buffers[1],
    *holdsp = buffers[2];
  size_t patternspl=0,bufferspl=0,holdspl=0;

  size_t line=0,lineend;
  uchar islastline,appendnextline=0,successfulsub=0;

  uchar patternsp_delim=0,
    buffersp_delim=0,
    holdsp_delim=0;

  uint32_t linenumber = 0;
  struct sed_expression *scriptv = (struct sed_expression*)script->v;
  size_t cycle = 0;

  while (1) {
    patternsp_delim = 0;
    if (line < size) {
      linenumber++;
    } else if (cycle == 0)
      break;

    lineend = line;

    islastline = 0;
    size_t start=line,end=lineend,offset=0;
    if (lineend < size) {
      while (lineend < size && src[lineend] != linedelim)
        lineend++;
      if (lineend < size && src[lineend] == linedelim)
        patternsp_delim = 1;

      end = lineend;

      if (appendnextline)
        offset = patternspl;
      if ((end-start)+offset >= SED_MAX_PATTERN_SPACE) {
        BIGLINE: ;
        return script_err("sed: line too big to process");
      }
      patternspl = (end-start)+offset;
      if (end-start)
        memcpy(patternsp+offset,src+start,end-start);
    }

    if (lineend+1 >= size)
      islastline = 1;

    appendnextline = 0;
    const size_t scriptsize = script->size;
    for (; cycle < scriptsize; cycle++) {
      if (!sed_address_exec(patternsp,patternspl,linenumber,islastline,&scriptv[cycle].address)) {
        if (scriptv[cycle].name == '{') {
          uint16_t lvl = scriptv[++cycle].lvl;
          while (cycle+1 < script->size && lvl <= scriptv[cycle+1].lvl)
            cycle++;
          if (cycle < script->size)
            cycle--;
        }
        continue;
      }

      offset = 0;

      switch (scriptv[cycle].name) {
        case 'H':
          offset = holdspl+1;
          if (offset+patternspl > SED_MAX_PATTERN_SPACE)
            goto BIGLINE;
          holdsp[holdspl] = linedelim;
        case 'h':
          memcpy(holdsp+offset,patternsp,patternspl);
          holdspl = patternspl+offset;
          holdsp_delim = patternsp_delim;
          break;
        case 'G':
          offset = patternspl+1;
          if (offset+holdspl > SED_MAX_PATTERN_SPACE)
            goto BIGLINE;
          patternsp[patternspl] = linedelim;
        case 'g':
          memcpy(patternsp+offset,holdsp,holdspl);
          patternspl = holdspl+offset;
          patternsp_delim = holdsp_delim;
          break;
        case 'd':
          patternspl = 0;
          cycle = 0;
          goto NEXT;
          break;
        case 'D': {
            size_t i = 0;
            while (i < patternspl && patternsp[i] != linedelim)
              i++;
            if (i >= patternspl || patternsp[i] != linedelim) {
              patternspl = 0;
              cycle = 0;
              goto NEXT;
            }
            i++;
            patternspl -= i;
            memcpy(buffersp,patternsp+i,patternspl);
            memcpy(patternsp,buffersp,patternspl);
          }
          break;
        case 'P':
          offset = 0;
          while (offset < patternspl && patternsp[offset] != linedelim)
            offset++;
        case 'p':
          if (scriptv[cycle].name == 'p') {
            COMMAND_PRINT: ;
            offset = patternspl;
          }
          if (offset)
            sink_write(output,patternsp,offset);
          if (!silent || patternsp_delim)
            sink_put(output,linedelim);
          break;
        case 'N':
          appendnextline = 1;
          cycle++;
          goto NEXT_PRINT;
          break;
        case 'n':
          cycle++;
          goto NEXT_PRINT;
          break;
        case 'z':
          patternspl = 0;
          break;
        case 'x':
          memcpy(buffersp,patternsp,patternspl);
          memcpy(patternsp,holdsp,holdspl);
          memcpy(holdsp,buffersp,patternspl);
          bufferspl = patternspl;
          patternspl = holdspl;
          holdspl = bufferspl;

          buffersp_delim = patternsp_delim;
          patternsp_delim = holdsp_delim;
          holdsp_delim = buffersp_delim;
          break;
        case 'q':
          goto END;
          break;
        case '=':
          print_uint(linenumber,output);
          sink_put(output,linedelim);
          break;
        case 't':
          if (!successfulsub)
            break;
        case 'T':
          if (scriptv[cycle].name == 'T' && successfulsub)
            break;
        case 'b':
          if (!scriptv[cycle].arg.s) {
            cycle = 0;
            goto NEXT;
          }
          for (size_t i = 0; i < scriptsize; i++)
            if (scriptv[i].name == ':' && streq(scriptv[cycle].arg,scriptv[i].arg))
              cycle = i;
          break;
        case 'y':
          for (size_t i = 0; i < patternspl; i++)
            patternsp[i] = (((uchar*)scriptv[cycle].arg2)[(uchar)patternsp[i]]) ?
                ((char*)scriptv[cycle].arg1)[(uchar)patternsp[i]] : patternsp[i];
          break;
        case 's': {
          successfulsub = 0;
          uchar global = ((((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_GLOBAL) ? 1 : 0),
            print = ((((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_PRINT) ? 1 : 0);
          uint32_t matchnum = ((uint64_t)scriptv[cycle].arg2)&SED_EXPRESSION_S_NUMBER,
            matchfound = 0;
          size_t after = 0;
          do {
          regmatch_t pmatch[10];
          pmatch[0].rm_so = 0;
          pmatch[0].rm_eo = (int)patternspl-after;
          if (regexec((regex_t*)scriptv[cycle].arg1,patternsp+after,10,pmatch,REG_STARTEND) != 0)
            break;
          successfulsub = 1;
          pmatch[0].rm_so += (int)after;
          pmatch[0].rm_eo += (int)after;
          matchfound++;
          if (matchnum && ((!global || matchfound < matchnum) && matchfound != matchnum)) {
            after = pmatch[0].rm_so+(pmatch[0].rm_eo-pmatch[0].rm_so);
            continue;
          }

          bufferspl = pmatch[0].rm_so;
          if (bufferspl)
            memcpy(buffersp,patternsp,bufferspl);
          if (scriptv[cycle].arg.s) {
            reliq_cstr arg = scriptv[cycle].arg;
            for (size_t i = 0; i < arg.s; i++) {
              char c = arg.b[i];
              if (c == '&' || (i+1 < arg.s && c == '\\')) {
                char unchanged_c = '0';
                if (c == '\\') {
                  unchanged_c = arg.b[++i];
                  size_t traversed,resultl;
                  char result[8];
                  splchar3(arg.b+i,arg.s-i,result,&resultl,&traversed);
                  i += traversed-1;
                  if (resultl == 0) {
                    c = unchanged_c;
                  } else if (resultl > 1) {
                    memcpy(buffersp+bufferspl,result,resultl);
                    bufferspl += resultl;
                    continue;
                  } else
                    c = result[0];
                }

                if (isdigit(unchanged_c)) {
                  c = unchanged_c-'0';
                  if (pmatch[(uchar)c].rm_so == -1 || pmatch[(uchar)c].rm_eo == -1)
                    continue;
                  if (bufferspl+(pmatch[(uchar)c].rm_eo-pmatch[(uchar)c].rm_so) >= SED_MAX_PATTERN_SPACE)
                    goto BIGLINE;
                  int loop_start=pmatch[(uchar)c].rm_so,loop_end=pmatch[(uchar)c].rm_eo;
                  if (c) { //shift by after if not \0
                    loop_start += after;
                    loop_end += after;
                  }
                  for (int j = loop_start; j < loop_end; j++)
                    buffersp[bufferspl++] = patternsp[j];
                  continue;
                }
              }
              buffersp[bufferspl++] = c;
            }
          }
          after = bufferspl;
          if (patternspl-pmatch[0].rm_eo) {
            if (bufferspl+(patternspl-pmatch[0].rm_eo) >= SED_MAX_PATTERN_SPACE)
              goto BIGLINE;
            memcpy(buffersp+bufferspl,patternsp+pmatch[0].rm_eo,patternspl-pmatch[0].rm_eo);
            bufferspl += patternspl-pmatch[0].rm_eo;
          }
          patternspl = bufferspl;
          if (bufferspl) {
            memcpy(patternsp,buffersp,bufferspl);
            bufferspl = 0;
          }
          } while((global || (matchnum && matchfound != matchnum)) && after < patternspl);
          if (successfulsub && print)
            goto COMMAND_PRINT;
          }
          break;
        case 'i':
        case 'c':
        case 'a':
          break;
      }
    }
    if (cycle >= script->size)
      cycle = 0;

    NEXT_PRINT: ;
    if (appendnextline) {
      if (patternsp_delim && patternspl < SED_MAX_PATTERN_SPACE)
        patternsp[patternspl++] = linedelim;
    } else {
      if (!silent) {
        if (patternspl)
          sink_write(output,patternsp,patternspl);
        if (patternsp_delim)
          sink_put(output,linedelim);
      }
      patternspl = 0;
    }
    if (lineend >= size)
      break;

    NEXT: ;
    if (patternsp_delim)
      lineend++;
    line = lineend;
  }

  END: ;
  if (!silent && patternspl) {
    sink_write(output,patternsp,patternspl);
    if (patternsp_delim)
      sink_put(output,linedelim);
  }

  return NULL;
}

reliq_error *
sed_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  reliq_error *err;
  const char argv0[] = "sed";
  uchar extendedregex=0,silent=0;
  flexarr *script = NULL; //struct sed_expression

  char linedelim = '\n';

  reliq_cstr *flags;
  if ((err = edit_arg_str(args,argv0,1,&flags)))
    return err;

  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 'E') {
        extendedregex = 1;
      } else if (flags->b[i] == 'z') {
        linedelim = '\0';
      } else if (flags->b[i] == 'n')
        silent = 1;
    }
  }

  if ((err = edit_arg_delim(args,argv0,2,&linedelim,NULL)))
    return err;

  reliq_cstr *scr;
  if ((err = edit_arg_str(args,argv0,0,&scr)))
    return err;

  if (scr && scr->s)
    if ((err = sed_script_comp(scr->b,scr->s,extendedregex ? REG_EXTENDED : 0,&script)))
      return err;

  if (script == NULL)
    return edit_missing_arg(argv0);

  char *buffers[3];
  for (size_t i = 0; i < 3; i++)
    buffers[i] = malloc(SED_MAX_PATTERN_SPACE);

  err = sed_pre_edit(src->b,src->s,output,buffers,script,linedelim,silent);

  for (size_t i = 0; i < 3; i++)
    free(buffers[i]);
  sed_script_free(script);
  return err;
}
