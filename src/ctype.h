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

#ifndef CTYPE_H
#define CTYPE_H

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

#define isalnum(x) IS_ALNUM[(unsigned char)x]
#define isalpha(x) IS_ALPHA[(unsigned char)x]
#define isdigit(x) IS_DIGIT[(unsigned char)x]
#define isspace(x) IS_SPACE[(unsigned char)x]
#define isxdigit(x) IS_XDIGIT[(unsigned char)x]
#define isupper(x) IS_UPPER[(unsigned char)x]
#define islower(x) IS_LOWER[(unsigned char)x]
#define isblank(x) IS_BLANK[(unsigned char)x]
#define iscntrl(x) IS_CNTRL[(unsigned char)x]
#define isgraph(x) IS_GRAPH[(unsigned char)x]
#define isprint(x) IS_PRINT[(unsigned char)x]
#define ispunct(x) IS_PUNCT[(unsigned char)x]

#define toupper_inline(x) (islower(x) ? (x)-32 : (x))
#define tolower_inline(x) (isupper(x) ? (x)+32 : (x))

int toupper(int c);
int tolower(int c);

#endif

