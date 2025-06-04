/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <hexderm@gmail.com>

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

#include "flexarr.h"
#include "ctype.h"
#include "utils.h"

#define UINT_TO_STR_MAX 32
#define QUOTE_INC 512

void
strnrev(char *v, const size_t size)
{
  if (!v || !size)
    return;
  for (size_t i = 0, j = size-1; i < j; i++, j--) {
    const char t = v[i];
    v[i] = v[j];
    v[j] = t;
  }
}

void
uint_to_str(char *dest, size_t *destl, const size_t max_destl, uint64_t num)
{
  *destl = 0;
  if (!max_destl)
    return;
  size_t p = 0;
  while (p < max_destl && num) {
    dest[p++] = (num%10)+'0';
    num /= 10;
  }
  *destl = p;
  if (p) {
    strnrev(dest,p);
  } else {
    dest[0] = '0';
    *destl = 1;
  }
}

void
memtrim(char const **dest, size_t *destsize, const char *src, const size_t size)
{
  *destsize = 0;
  if (!src || !size)
    return;

  size_t start=0,end=size;
  while (start < end && isspace(((char*)src)[start]))
    start++;

  while (end-1 > start && isspace(((char*)src)[end-1]))
    end--;

  *dest = src+start;
  *destsize = end-start;
}

static size_t
memwordtok_r_get_word(const char *ptr, const size_t plen, char const **word, size_t *wordlen)
{
  *word = NULL;
  *wordlen = 0;
  size_t p = 0;
  if (plen == 0)
    return p;
  while_is(isspace,ptr,p,plen);
  if (p >= plen)
    return p;
  *word = ptr+p;
  while_isnt(isspace,ptr,p,plen);
  *wordlen = p-(*word-ptr);
  return p;
}

void
memwordtok_r(const char *ptr, const size_t plen, char const **saveptr, size_t *saveptrlen, char const **word, size_t *wordlen)
{
  *word = NULL;
  *wordlen = 0;

  if (ptr) {
    size_t size = memwordtok_r_get_word(ptr,plen,word,wordlen);
    if (*wordlen) {
      *saveptr = ptr+size;
      *saveptrlen = plen-size;
    }
    return;
  }

  if (!*saveptr)
    return;

  size_t size = memwordtok_r_get_word(*saveptr,*saveptrlen,word,wordlen);
  if (*wordlen) {
    *saveptr += size;
    *saveptrlen -= size;
  }
  return;
}

void *
memdup(void const *src, const size_t size)
{
  return memcpy(malloc(size),src,size);
}

int
memcasecmp(const void *v1, const void *v2, const size_t n)
{
  const char *s1 = v1,
    *s2 = v2;
  for (size_t i = 0; i < n; i++) {
    char u1 = toupper_inline(s1[i]);
    char u2 = toupper_inline(s2[i]);
    char diff = u1-u2;
    if (diff)
      return diff;
  }
  return 0;
}

void
print_uint(uint64_t num, SINK *out)
{
  char str[UINT_TO_STR_MAX];
  size_t len = 0;
  uint_to_str(str,&len,UINT_TO_STR_MAX,num);
  if (len)
    sink_write(out,str,len);
}

void
print_int(int64_t num, SINK *out)
{
  if (num < 0) {
    sink_put(out,'-');
    num *= -1;
  }
  print_uint(num,out);
}

#if defined(__APPLE__)
void *
mempcpy(void *dest, void *src, const size_t n)
{
  memcpy(dest,src,n);
  return dest+n;
}
#endif

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(__APPLE__)
void *
memrchr(void *restrict src, const int c, const size_t size)
{
  uchar searched = c;
  if (!size)
    return NULL;

  char *restrict str = src;
  for (size_t i = size-1; ; i--) {
    if (str[i] == searched)
      return src+i;

    if (!i)
      break;
  }
  return NULL;
}
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
char const *
memmem(char const *haystack, size_t haystackl, const char *needle, const size_t needlel)
{
  if (!haystackl || !needlel)
    return NULL;
  for (char const *h=haystack; needlel <= haystackl; h++, haystackl--) {
    if (likely(needle[0] != h[0]))
      continue;

    for (size_t i=1; i < needlel; i++)
      if (likely(needle[i] != h[i]))
        goto CONTINUE;
    return h;
    CONTINUE: ;
  }
  return NULL;
}
#endif

char const *
memcasemem_r(char const *restrict haystack, size_t haystackl, const char *restrict needle, const size_t needlel)
{
  if (!haystackl || !needlel)
    return NULL;
  for (char const *h=haystack; needlel <= haystackl; h++, haystackl--) {
    if (likely(toupper_inline(needle[0]) != toupper_inline(h[0])))
      continue;

    for (size_t i=1; i < needlel; i++)
      if (likely(toupper_inline(needle[i]) != toupper_inline(h[i])))
        goto CONTINUE;
    return h;
    CONTINUE: ;
  }
  return NULL;
}

char
splchar(const char c) //convert special characters e.g. '\n'
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

uint64_t
get_fromdec(const char *src, const size_t size, size_t *traversed)
{
  size_t pos=0;
  uint64_t r = 0;
  while (pos < size && isdigit(src[pos]))
    r = (r*10)+(src[pos++]-'0');
  if (traversed)
    *traversed = pos;
  return r;
}

static int
hextodec(int n)
{
  if (n >= '0' && n <= '9')
    return n-48;
  if (n >= 'A' && n <= 'F')
    return n-55;
  if (n >= 'a' && n <= 'f')
    return n-87;
  return -1;
}

uint64_t
get_fromhex(const char *src, const size_t size, size_t *traversed)
{
  size_t pos=0;
  uint64_t r = 0;
  while (pos < size) {
    int val = hextodec(src[pos]);
    if (val == -1)
      break;
    r = (r<<4)|val;
    pos++;
  }
  if (traversed)
    *traversed = pos;
  return r;
}

static uint64_t
splchar2_fromhex(const char *src, const size_t srcl, size_t *traversed, const uchar maxlength) {
  if (srcl < 1) {
    *traversed = 1;
    return *src;
  }
  uint64_t ret = get_fromhex(src+1,MIN(maxlength,srcl-1),traversed);
  if (*traversed == 0)
    return ret = *src;
  (*traversed)++;
  return ret;
}

static char
splchar2_hex(const char *src, const size_t srcl, size_t *traversed)
{
  return splchar2_fromhex(src,srcl,traversed,2)&255;
}

static char
splchar2_oct(const char *src, const size_t srcl, size_t *traversed)
{
  size_t i = 1;
  char ret = 0;
  for (; i < srcl && i <= 3; i++) {
    char c = src[i];
    if (c < '0' || c > '7')
      goto END;
    c -= '0';
    ret = (ret<<3)|c;
  }

  END: ;
  if (i == 1)
    ret = 'o';
  *traversed = i;
  return ret;
}

char
splchar2(const char *src, const size_t srcl, size_t *traversed)
{
  size_t trav = 0;
  char ret;
  if (*src == 'o') {
    ret = splchar2_oct(src,srcl,&trav);
  } else if (*src == 'x') {
    ret = splchar2_hex(src,srcl,&trav);
  } else {
    trav = 1;
    ret = splchar(*src);
  }
  if (traversed)
    *traversed = trav;
  return ret;
}

static uchar
most_significant_bit(uint32_t n)
{
  uchar ret = 0;
  while (n >>= 1)
    ret++;
  return ret;
}

uint32_t
enc16utf8(const uint16_t c)
{
  uint32_t ret = 0;
  char msb = most_significant_bit(c);

  if (msb < 7)
    return c;

  ret |= c&0x3f;

  if (msb < 11)
    return ret|0xc080|((c&0x7c0)<<2);
  return ret|0xe08080|((c&0xfc0)<<2)|((c&0xf000)<<4);
}

uint64_t
enc32utf8(const uint32_t c)
{
  char msb = most_significant_bit(c);

  if (msb < 7)
    return c;

  uint64_t ret = (c&0x3f);
  if (msb < 11)
    return ret|0xc080|((c&0x7c0)<<2);
  ret |= (c&0xfc0)<<2;
  if (msb < 16)
    return ret|0xe08080|((c&0xf000)<<4);
  ret |= (c&0x3f000)<<4;
  if (msb < 21)
    return ret|0xf0808080|((c&0x1c0000)<<6);
  ret |= (c&0xfc0000)<<6;
  if (msb < 26)
    return ret|0xf480808080|((c&0x3000000)<<8);
  return ret|0xf68080808080|((c&0xcf000000)<<8)|((c&400000000)<<10);
}

int
write_utf8(uint64_t data, char *result, size_t *traversed, const size_t maxlength)
{
  *result = 0;
  if (data == 0) {
    *traversed = 1;
    return 0;
  }
  *traversed = 0;
  for (size_t i = 5; *traversed < maxlength; i--) {
    uint64_t mask = ((uint64_t)0xff)<<(i<<3);
    if (data&mask) {
      *result = ((data&mask)>>(i<<3))&255;
      result++;
      (*traversed)++;
    }
    if (!i)
      break;
  }
  if (*traversed >= maxlength)
    return -1;
  return 0;
}

static void
splchar3_unicode(const char *src, const size_t srcl, char *result, size_t *resultl, size_t *traversed, const uchar maxlength)
{
  uint64_t val = splchar2_fromhex(src,srcl,traversed,maxlength);
  if (*traversed == 0) {
    *resultl = 0;
    *result = *src;
    return;
  }
  uint64_t ret = (maxlength == 4) ?
    enc16utf8(val) :
    enc32utf8(val);

  write_utf8(ret,result,resultl,8);
}

void
splchar3(const char *src, const size_t srcl, char *result, size_t *resultl, size_t *traversed)
{
  *resultl = 0;
  if (srcl == 0) {
    *result = 0;
    *traversed = 0;
    return;
  }

  if (*src == 'u' || *src == 'U') {
    splchar3_unicode(src,srcl,result,resultl,traversed,(*src == 'u') ? 4 : 8);
    return;
  }

  char r = splchar2(src,srcl,traversed);
  if (r != *src || r == '\\') {
    *resultl = 1;
    *result = r;
  }
}

char *
delstr(char *src, const size_t pos, size_t *size, const size_t count)
{
  if (pos >= *size || !count)
    return src;
  if (*size-pos <= count) {
    src[pos] = 0;
    *size = pos;
    return src;
  }

  const size_t s = *size-pos-count;
  memmove(src+pos,src+pos+count,s);

  *size -= count;
  src[*size] = 0;
  return src;
}

char *
delchar(char *src, const size_t pos, size_t *size)
{
  return delstr(src,pos,size,1);
}

uint64_t
number_handle(const char *src, size_t *pos, const size_t size)
{
  size_t s;
  uint64_t ret = get_fromdec(src+*pos,size-*pos,&s);
  if (s == 0)
    return -1;
  *pos += s;
  return ret;
}

double
get_point_of_double(const char *src, size_t *pos, const size_t size)
{
  size_t i = *pos;

  double r = 0;
  double mult = 0.1;

  while (i < size && isdigit(src[i])) {
    double d = src[i]-'0';
    r += d*mult;
    mult /= 10;

    i++;
  }

  *pos = i;
  return r;
}

char
universal_number(const char *src, size_t *pos, const size_t size, void *result)
{
  char r = 0;
  size_t i = *pos;
  if (i >= size)
    return r;

  uint64_t t_unsigned;
  int64_t t_signed;
  double t_floating;
  uchar issigned = 0;

  if (src[i] == '-') {
    issigned = 1;
    i++;
  }

  t_unsigned = number_handle(src,&i,size);
  if (t_unsigned == (size_t)-1)
    return r;

  if (i+1 < size && src[i] == '.' && isdigit(src[i+1])) {
    i++;

    t_floating = get_point_of_double(src,&i,size);
    t_floating += t_unsigned;
    if (issigned)
      t_floating *= -1;
    *(double*)result = t_floating;
    r = 'd';
  } else if (issigned) {
    t_signed = t_unsigned;
    t_signed *= -1;
    *(int64_t*)result = t_signed;
    r = 's';
  } else {
    *(uint64_t*)result = t_unsigned;
    r = 'u';
  }

  *pos = i;
  return r;
}

reliq_error *
skip_quotes(const char *src, size_t *pos, const size_t size)
{
  size_t i = *pos;
  char quote = src[i++];
  reliq_error *err = NULL;

  while (i < size && src[i] != quote) {
    if (src[i] == '\\' && (src[i+1] == '\\' || src[i+1] == quote))
      i++;
    i++;
  }
  if (i < size && src[i] == quote) {
    i++;
  } else
    err = script_err("string: could not find the end of %c quote at %lu",quote,*pos);

  *pos = i;
  return err;
}

static char
get_quoted_skip(const char *src, size_t *pos, const size_t size, flexarr *res) //res: char
{
  size_t i = *pos;
  const char quote = src[i++];

  for (; i < size && src[i] != quote; i++) {
    if (i+1 < size && src[i] == '\\') {
      if (src[i+1] == '\\') {
        *(char*)flexarr_inc(res) = src[i++];
      } if (src[i+1] == quote)
        i++;
    }
    *(char*)flexarr_inc(res) = src[i];
  }
  *pos = i;
  if (src[i] != quote)
    return quote;
  return 0;
}

reliq_error *
get_quoted(const char *src, size_t *pos, const size_t size, const char delim, char **result, size_t *resultl)
{
  size_t i=*pos;
  reliq_error *err = NULL;
  flexarr res = flexarr_init(sizeof(char),QUOTE_INC);

  for (; i < size && !isspace(src[i]) && src[i] != delim; i++) {
    if (i+1 < size && src[i] == '\\') {
      if (src[i+1] == '\\' || isspace(src[i+1]) || src[i+1] == delim)
        i++;
    } else if (src[i] == '"' || src[i] == '\'') {
      char quote = get_quoted_skip(src,&i,size,&res);
      if (quote)
        goto_script_seterr(END,"string: could not find the end of %c quote",quote);
      continue;
    }
    *(char*)flexarr_inc(&res) = src[i];
  }
  END: ;
  *pos = i;
  if (err) {
    *resultl = 0;
    flexarr_free(&res);
  } else
    flexarr_conv(&res,(void**)result,resultl);
  return err;
}

void
splchars_conv(char *src, size_t *size)
{
  for (size_t i = 0; i < (*size)-1; i++) {
    if (src[i] != '\\')
      continue;

    size_t resultl,traversed;
    splchar3(src+i+1,*size-i-1,src+i,&resultl,&traversed);
    if (resultl == 0)
      continue;

    i += resultl-1;
    delstr(src,i+1,size,traversed-resultl+1);
  }
}

void
splchars_conv_sink(const char *src, const size_t size, SINK *sn)
{
  for (size_t i = 0; i < size; i++) {
    if (src[i] != '\\') {
      SINGLE: ;
      sink_put(sn,src[i]);
      continue;
    }

    size_t resultl,traversed;
    char buf[8];
    splchar3(src+i+1,size-i-1,buf,&resultl,&traversed);
    if (resultl == 0)
      goto SINGLE;

    sink_write(sn,buf,resultl);
    i += traversed;
  }
}

reliq_cstr
reliq_str_to_cstr(reliq_str str)
{
  return (reliq_cstr){
    .b = str.b,
    .s = str.s
  };
}
