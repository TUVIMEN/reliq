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

#include "flexarr.h"
#include "ctype.h"
#include "utils.h"

#define UINT_TO_STR_MAX 32
#define QUOTE_INCR 512

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
uint_to_str(char *dest, size_t *destl, const size_t max_destl, unsigned long num)
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
print_uint(unsigned long num, SINK *outfile)
{
  char str[UINT_TO_STR_MAX];
  size_t len = 0;
  uint_to_str(str,&len,UINT_TO_STR_MAX,num);
  if (len)
    sink_write(outfile,str,len);
}

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
get_fromdec(const char *src, const size_t srcl, size_t *traversed, const uchar maxlength)
{
  *traversed = 0;
  if (!maxlength || !srcl)
    return 0;
  size_t i = 0;
  uint64_t ret = 0;
  const size_t size = (srcl < maxlength) ? srcl : maxlength;

  for (; i < size && isdigit(src[i]); i++)
    ret = (ret*10)+(src[i]-'0');

  *traversed = i;
  return ret;
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
get_fromhex(const char *src, const size_t srcl, size_t *traversed, const uchar maxlength)
{
  *traversed = 0;
  if (!maxlength || !srcl)
    return 0;
  size_t i = 0;
  uint64_t ret = 0;
  const size_t size = (srcl < maxlength) ? srcl : maxlength;

  for (; i < size; i++) {
    int val = hextodec(src[i]);
    if (val == -1)
      goto END;
    ret = (ret<<4)|val;
  }

  END: ;
  *traversed = i;
  return ret;
}

static uint64_t
splchar2_fromhex(const char *src, const size_t srcl, size_t *traversed, const uchar maxlength) {
  if (srcl < 1) {
    *traversed = 1;
    return *src;
  }
  uint64_t ret = get_fromhex(src+1,srcl-1,traversed,maxlength);
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

uint32_t
enc16utf8(const uint16_t c)
{
  uint32_t d = 0;
  char bcount = 15;
  while (bcount != -1 && ((c>>bcount)&1) == 0)
    bcount--;
  bcount++;
  if (bcount < 8) {
    d = c;
  } else if (bcount < 12) {
    d |= 0xc080|(c&0x3f)|((c&0x7c0)<<2);
  } else
    d |= 0xe08080|(c&0x3f)|((c&0xfc0)<<2)|((c&0xf000)<<4);
  return d;
}

uint64_t
enc32utf8(const uint32_t c)
{
  char msf = 31;
  while (msf != -1 && ((c>>msf)&1) == 0)
    msf--;
  msf++;
  if (msf < 8) {
    return c;
  } else if (msf < 12) {
    return 0xc081|(c&0x3f)|((c&0x7c0)<<2);
  } else if (msf < 17) {
    return 0xe08080|(c&0x3f)|((c&0xfc0)<<2)|((c&0xf000)<<4);
  } else if (msf < 22) {
    return 0xf0808080|(c&0x3f)|((c&0xfc0)<<2)|((c&0x3f000)<<4)|((c&0x1c0000)<<6);
  } else if (msf < 27)
    return 0xf480808080|(c&0x3f)|((c&0xfc0)<<2)|((c&0x3f000)<<4)|((c&0xfc0000)<<6)|((c&0x3000000)<<8);
  return 0xf68080808080|(c&0x3f)|((c&0xfc0)<<2)|((c&0x3f000)<<4)|((c&0xfc0000)<<6)|((c&0xcf000000)<<8)|((c&400000000)<<10);
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

  if (*src == 'u') {
    splchar3_unicode(src,srcl,result,resultl,traversed,4);
    return;
  }
  if (*src == 'U') {
    splchar3_unicode(src,srcl,result,resultl,traversed,8);
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
  if (pos >= *size)
    return src;
  if (*size-pos <= count) {
    src[pos] = 0;
    *size = pos;
    return src;
  }

  const size_t s = *size-count;
  for (size_t i = pos; i < s; i++)
    src[i] = src[i+count];
  src[s] = 0;
  *size = s;
  return src;
}

char *
delchar(char *src, const size_t pos, size_t *size)
{
  return delstr(src,pos,size,1);
}

unsigned int
get_dec(const char *src, const size_t size, size_t *traversed)
{
    size_t pos=0;
    unsigned int r = 0;
    while (pos < size && isdigit(src[pos]))
        r = (r*10)+(src[pos++]-48);
    *traversed = pos;
    return r;
}

unsigned int
number_handle(const char *src, size_t *pos, const size_t size)
{
  size_t s;
  int ret = get_dec(src+*pos,size-*pos,&s);
  if (s == 0)
    return -1;
  *pos += s;
  return ret;
}

reliq_error *
get_quoted(const char *src, size_t *pos, const size_t size, const char delim, char **result, size_t *resultl)
{
  size_t i=*pos;
  reliq_error *err = NULL;
  flexarr *res = flexarr_init(sizeof(char),QUOTE_INCR);

  for (; i < size && !isspace(src[i]) && src[i] != delim; i++) {
    if (i+1 < size && src[i] == '\\') {
      if (src[i+1] == '\\') {
        *(char*)flexarr_inc(res) = src[++i];
        continue;
      } else if (isspace(src[i+1]) || src[i+1] == delim)
        i++;
    } else if (src[i] == '"' || src[i] == '\'') {
      char tf = src[i++];
      for (; i < size && src[i] != tf; i++) {
        if (i+1 < size && src[i] == '\\') {
          if (src[i+1] == '\\') {
            *(char*)flexarr_inc(res) = src[i++];
          } if (src[i+1] == tf)
            i++;
        }
        *(char*)flexarr_inc(res) = src[i];
      }
      if (src[i] != tf)
        goto_script_seterr(END,"string: could not find the end of %c quote",tf);
      continue;
    }
    *(char*)flexarr_inc(res) = src[i];
  }
  END: ;
  *pos = i;
  if (err) {
    *resultl = 0;
    flexarr_free(res);
  } else
    flexarr_conv(res,(void**)result,resultl);
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
