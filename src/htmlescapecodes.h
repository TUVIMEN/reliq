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

#ifndef RELIQ_HTMLESCAPECODES_H
#define RELIQ_HTMLESCAPECODES_H

#include "sink.h"
#include "types.h"

#define HTMLESCAPECODES_MAXSIZE_VAL 16
#define HTMLESCAPECODES_MAXSIZE_NAME 31
#define HTMLESCAPECODES_MAXSIZE_DIGITS 10
#define HTMLESCAPECODES_MAXSIZE_XDIGITS 8

int htmlescapecode(const char *src, const size_t srcl, size_t *traversed, char *result, const size_t resultl, size_t *written);
void htmlescapecodes_file(const char *src, const size_t srcl, SINK *out);

#endif
