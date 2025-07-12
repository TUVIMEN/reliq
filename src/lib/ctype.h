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

#ifndef RELIQ_CTYPE_H
#define RELIQ_CTYPE_H

extern const char IS_ALNUM[];
extern const char IS_ALPHA[];
extern const char IS_DIGIT[];
extern const char IS_SPACE[];
extern const char IS_XDIGIT[];
extern const char IS_UPPER[];
extern const char IS_LOWER[];
extern const char IS_BLANK[];
extern const char IS_CNTRL[];
extern const char IS_GRAPH[];
extern const char IS_PRINT[];
extern const char IS_PUNCT[];

#define isalnum(x) IS_ALNUM[(uint8_t)x]
#define isalpha(x) IS_ALPHA[(uint8_t)x]
#define isdigit(x) IS_DIGIT[(uint8_t)x]
#define isspace(x) IS_SPACE[(uint8_t)x]
#define isxdigit(x) IS_XDIGIT[(uint8_t)x]
#define isupper(x) IS_UPPER[(uint8_t)x]
#define islower(x) IS_LOWER[(uint8_t)x]
#define isblank(x) IS_BLANK[(uint8_t)x]
#define iscntrl(x) IS_CNTRL[(uint8_t)x]
#define isgraph(x) IS_GRAPH[(uint8_t)x]
#define isprint(x) IS_PRINT[(uint8_t)x]
#define ispunct(x) IS_PUNCT[(uint8_t)x]

#define toupper_inline(x) (islower(x) ? (x)-32 : (x))
#define tolower_inline(x) (isupper(x) ? (x)+32 : (x))

#endif

