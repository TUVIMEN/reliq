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

#ifndef RELIQ_UTILS_H
#define RELIQ_UTILS_H

#include <string.h>
#include "sink.h"
#include "types.h"

#define REGEX_PATTERN_SIZE (1<<9)

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define while_isnt(w,x,y,z) while ((y) < (z) && !w((x)[(y)])) {(y)++;}
#define LENGTH(x) (sizeof(x)/(sizeof(*x)))

#define script_err(...) reliq_set_error(RELIQ_ERROR_SCRIPT,__VA_ARGS__)
#define goto_seterr(x,...) { err = reliq_set_error(__VA_ARGS__); goto x; }
#define goto_seterr_p(x,...) { *err = reliq_set_error(__VA_ARGS__); goto x; }
#define goto_script_seterr(x,...) goto_seterr(x,RELIQ_ERROR_SCRIPT,__VA_ARGS__)
#define goto_script_seterr_p(x,...)  goto_seterr_p(x,RELIQ_ERROR_SCRIPT,__VA_ARGS__)

#define memcomp(w,x,y,z) ((y) == (z) && memcmp(w,x,y) == 0)
#define memcasecomp(w,x,y,z) ((y) == (z) && memcasecmp(w,x,y) == 0)
#define strcomp(x,y) memcomp(x.b,y.b,x.s,y.s)
#define strcasecomp(x,y) memcasecomp(x.b,y.b,x.s,y.s)

void print_uint(unsigned long num, SINK *outfile);
void print_int(long num, SINK *outfile);
void strnrev(char *v, const size_t size); //was previously named strrev but mingw has it defined for some reason
void uint_to_str(char *dest, size_t *destl, const size_t max_destl, unsigned long num);
void memtrim(char const **dest, size_t *destsize, const char *src, const size_t size);
void memwordtok_r(const char *ptr, const size_t plen, char const **saveptr, size_t *saveptrlen, char const **word, size_t *wordlen);
#if defined(__MINGW32__) || defined(__MINGW64__)
char const *memmem(char const *haystack, size_t haystackl, const char *needle, const size_t needlel);
#endif
int memcasecmp(const void *v1, const void *v2, const size_t n);
char const *memcasemem_r(char const *restrict haystack, size_t haystackl, const char *restrict needle, const size_t needlel);
void *memdup(void const *src, const size_t size);
uint32_t enc16utf8(const uint16_t c);
uint64_t enc32utf8(const uint32_t c);
int write_utf8(uint64_t data, char *result, size_t *traversed, const size_t maxlength);
char splchar(const char c);
char splchar2(const char *src, const size_t srcl, size_t *traversed);
void splchar3(const char *src, const size_t srcl, char *result, size_t *resultl, size_t *traversed);
char *delstr(char *src, const size_t pos, size_t *size, const size_t count);
char *delchar(char *src, const size_t pos, size_t *size);
unsigned int get_dec(const char *src, const size_t size, size_t *traversed);
uint64_t get_fromdec(const char *src, const size_t srcl, size_t *traversed, const unsigned char maxlength);
uint64_t get_fromhex(const char *src, const size_t srcl, size_t *traversed, const unsigned char maxlength);
unsigned int number_handle(const char *src, size_t *pos, const size_t size);
reliq_error *get_quoted(const char *src, size_t *pos, const size_t size, const char delim, char **result, size_t *resultl);
void splchars_conv(char *src, size_t *size);

#endif
