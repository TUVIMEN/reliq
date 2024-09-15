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

#ifndef UTILS_H
#define UTILS_H

//reliq_range flags
#define R_RELATIVE(x) (1<<(x))
#define R_RANGE 0x8
#define R_NOTEMPTY 0x10
#define R_INVERT 0x20

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

void print_uint(unsigned long num, FILE *outfile);
void strrev(char *v, size_t size);
void uint_to_str(char *dest, size_t *destl, const size_t max_destl, unsigned long num);
void memtrim(void const **dest, size_t *destsize, const void *src, const size_t size);
void memwordtok_r(const void *ptr, const size_t plen, void const **saveptr, size_t *saveptrlen, void const **word, size_t *wordlen);
int memcasecmp(const void *v1, const void *v2, const size_t n);
void const *memcasemem(void const *haystack, size_t const haystackl, const void *needle, const size_t needlel);
void *memdup(void const *src, size_t size);
unsigned int enc16utf8(const short c);
unsigned long enc32utf8(const int c);
char splchar(const char c);
char splchar2(const char *src, const size_t maxsize, size_t *traversed);
void splchar3(const char *src, const size_t maxsize, char *result, size_t *resultl, size_t *traversed);
char *delstr(char *src, const size_t pos, size_t *size, const size_t count);
char *delchar(char *src, const size_t pos, size_t *size);
unsigned int get_dec(const char *src, size_t size, size_t *traversed);
unsigned int number_handle(const char *src, size_t *pos, const size_t size);
reliq_error *get_quoted(const char *src, size_t *pos, const size_t size, const char delim, char **result, size_t *resultl);
void splchars_conv(char *src, size_t *size);
reliq_error *range_comp(const char *src, size_t *pos, const size_t size, reliq_range *range);
unsigned char range_match(const uint matched, const reliq_range *range, const size_t last);
void range_free(reliq_range *range);
unsigned int predict_range_max(const reliq_range *range);

#endif
