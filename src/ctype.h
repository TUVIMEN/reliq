/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2022 TUVIMEN <suchora.dominik7@gmail.com>

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

#define isalnum(x) IS_ALNUM[(unsigned char)x]
#define isalpha(x) IS_ALPHA[(unsigned char)x]
#define isdigit(x) IS_DIGIT[(unsigned char)x]
#define isspace(x) IS_SPACE[(unsigned char)x]

#endif

