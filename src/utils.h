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
#include "ctype.h"

#define MIN(x,y) ( ((x) < (y)) ? (x) : (y) )
#define MAX(x,y) ( ((x) < (y)) ? (y) : (x) )

#define LENGTH(x) (sizeof(x)/(sizeof(*x)))

#define REGEX_PATTERN_SIZE (1<<10)

#define RELIQ_DEBUG_SECTION_HEADER(x) \
    ( fputs("\033[34;2m//\033[0m\033[32;6m",stderr), \
      fputs(x,stderr), \
      fputs("\033[0m\n",stderr) )

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define while_isnt(w,x,y,z) while ((y) < (z) && !w((x)[(y)])) {(y)++;}

//so many ways to error
#define script_err(...) reliq_set_error(RELIQ_ERROR_SCRIPT,__VA_ARGS__)
#define goto_seterr(x,...) { err = reliq_set_error(__VA_ARGS__); goto x; }
#define goto_seterr_p(x,...) { *err = reliq_set_error(__VA_ARGS__); goto x; }
#define goto_script_seterr(x,...) goto_seterr(x,RELIQ_ERROR_SCRIPT,__VA_ARGS__)
#define goto_script_seterr_p(x,...)  goto_seterr_p(x,RELIQ_ERROR_SCRIPT,__VA_ARGS__)

void print_uint(uint64_t num, SINK *out);
void print_int(int64_t num, SINK *out);

void uint_to_str(char *dest, size_t *destl, const size_t max_destl, uint64_t num);

//convert str to int
uint64_t get_fromdec(const char *src, const size_t size, size_t *traversed);
uint64_t get_fromhex(const char *src, const size_t size, size_t *traversed);
uint64_t number_handle(const char *src, size_t *pos, const size_t size);

double get_point_of_double(const char *src, size_t *pos, const size_t size);
//this handles uint64_t, int64_t and double types, and returns 'u', 's', 'd' respectively, 0 is returned for error
char universal_number(const char *src, size_t *pos, const size_t size, void *result);

//str functions
void strnrev(char *v, const size_t size); //was previously named strrev but mingw has it defined for some reason
char *delstr(char *src, const size_t pos, size_t *size, const size_t count);
char *delchar(char *src, const size_t pos, size_t *size);

void memtrim(char const **dest, size_t *destsize, const char *src, const size_t size);
void memwordtok_r(const char *ptr, const size_t plen, char const **saveptr, size_t *saveptrlen, char const **word, size_t *wordlen);

#if defined(__MINGW32__) || defined(__MINGW64__)
char const *memmem(char const *haystack, size_t haystackl, const char *needle, const size_t needlel);
#endif
int memcasecmp(const void *v1, const void *v2, const size_t n);
char const *memcasemem_r(char const *restrict haystack, size_t haystackl, const char *restrict needle, const size_t needlel);
void *memdup(void const *src, const size_t size);

#define memeq(w,x,y,z) ((y) == (z) && memcmp(w,x,y) == 0)
#define memcaseeq(w,x,y,z) ((y) == (z) && memcasecmp(w,x,y) == 0)
#define streq(x,y) memeq(x.b,y.b,x.s,y.s)
#define strcaseeq(x,y) memcaseeq(x.b,y.b,x.s,y.s)

//utf8
uint32_t enc16utf8(const uint16_t c);
uint64_t enc32utf8(const uint32_t c);
int write_utf8(uint64_t data, char *result, size_t *traversed, const size_t maxlength);

//convert special characters in string
char splchar(const char c);
char splchar2(const char *src, const size_t srcl, size_t *traversed);
void splchar3(const char *src, const size_t srcl, char *result, size_t *resultl, size_t *traversed);

void splchars_conv(char *src, size_t *size);
void splchars_conv_sink(const char *src, const size_t size, SINK *sn);

//parse strings
reliq_error *skip_quotes(const char *src, size_t *pos, const size_t size);
reliq_error *get_quoted(const char *src, size_t *pos, const size_t size, const char delim, char **result, size_t *resultl);

reliq_cstr reliq_str_to_cstr(reliq_str str);

//inferior platforms bullshit
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(__APPLE__)
void *memrchr(void *restrict src, const int c, const size_t size);
#endif

#if defined(__APPLE__)
void *mempcpy(void *dest, void *src, const size_t n);
#endif

#endif
