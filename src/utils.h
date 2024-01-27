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

#ifndef UTILS_H
#define UTILS_H

//hgrep_range flags
#define R_RANGE 0x8
#define R_NOTEMPTY 0x10

#define REGEX_PATTERN_SIZE (1<<9)

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGTH(x) (sizeof(x)/(sizeof(*x)))

#define memcomp(w,x,y,z) ((y) == (z) && memcmp(w,x,y) == 0)
#define strcomp(x,y) memcomp(x.b,y.b,x.s,y.s)

void *memdup(void const *src, size_t size);
char special_character(const char c);
char *delchar(char *src, const size_t pos, size_t *size);
unsigned int get_dec(const char *src, size_t size, size_t *traversed);
unsigned int number_handle(const char *src, size_t *pos, const size_t size);
hgrep_error *get_quoted(char *src, size_t *i, size_t *size, const char delim, size_t *start, size_t *len);
void conv_special_characters(char *src, size_t *size);
unsigned char ranges_match(const uint matched, const struct hgrep_range *ranges, const size_t rangesl, const size_t last);
hgrep_error *ranges_comp(const char *src, size_t *pos, const size_t size, struct hgrep_range **ranges, size_t *rangesl);

#endif
