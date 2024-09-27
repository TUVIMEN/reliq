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

#ifndef RELIQ_PATTERN_H
#define RELIQ_PATTERN_H

#include <regex.h>

#include "sink.h"
#include "types.h"
#include "range.h"

//reliq_pattern flags
#define RELIQ_PATTERN_TRIM 0x1
#define RELIQ_PATTERN_CASE_INSENSITIVE 0x2
#define RELIQ_PATTERN_INVERT 0x4

#define RELIQ_PATTERN_MATCH ( \
        RELIQ_PATTERN_MATCH_FULL | \
        RELIQ_PATTERN_MATCH_ALL | \
        RELIQ_PATTERN_MATCH_BEGINNING | \
        RELIQ_PATTERN_MATCH_ENDING \
        )

#define RELIQ_PATTERN_MATCH_FULL 0x8
#define RELIQ_PATTERN_MATCH_ALL 0x10
#define RELIQ_PATTERN_MATCH_BEGINNING 0x18
#define RELIQ_PATTERN_MATCH_ENDING 0x20

#define RELIQ_PATTERN_PASS ( \
        RELIQ_PATTERN_PASS_WHOLE | \
        RELIQ_PATTERN_PASS_WORD \
        )

#define RELIQ_PATTERN_PASS_WHOLE 0x40
#define RELIQ_PATTERN_PASS_WORD 0x80

#define RELIQ_PATTERN_TYPE ( \
        RELIQ_PATTERN_TYPE_STR | \
        RELIQ_PATTERN_TYPE_BRE | \
        RELIQ_PATTERN_TYPE_ERE \
        )

#define RELIQ_PATTERN_TYPE_STR 0x100
#define RELIQ_PATTERN_TYPE_BRE 0x200
#define RELIQ_PATTERN_TYPE_ERE 0x300

#define RELIQ_PATTERN_EMPTY 0x400
#define RELIQ_PATTERN_ALL 0x800

typedef struct {
  union {
    reliq_str str;
    regex_t reg;
  } match;
  reliq_range range;
  uint16_t flags; //RELIQ_PATTERN_
} reliq_pattern;

int reliq_regexec(const reliq_pattern *pattern, const char *src, const size_t size);

reliq_error *reliq_regcomp(reliq_pattern *pattern, const char *src, size_t *pos, const size_t size, const char delim, const char *flags, unsigned char (*checkstrclass)(char));

void reliq_regfree(reliq_pattern *pattern);

#endif
