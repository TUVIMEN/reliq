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

#ifndef RELIQ_NPATTERN_INTR_H
#define RELIQ_NPATTERN_INTR_H

#include "range.h"
#include "pattern.h"
#include "exprs.h"

//reliq_npattern flags
#define N_EMPTY 0x1 //ignore matching
#define N_POSITION_ABSOLUTE 0x2

//nmatchers type
#define NM_DEFAULT 0
#define NM_TAG 1
#define NM_COMMENT 2
#define NM_TEXT 3
#define NM_TEXT_NOERR 4
#define NM_TEXT_ERR 5
#define NM_TEXT_EMPTY 6
#define NM_TEXT_ALL 7
#define NM_MULTIPLE 8

//match_hook flags
#define H_RANGE_SIGNED 0x1
#define H_RANGE_UNSIGNED 0x2
#define H_PATTERN 0x4
#define H_EXPRS 0x8
#define H_NOARG 0x10

#define H_ACCESS 0x20
#define H_TYPE 0x40
#define H_GLOBAL 0x80
#define H_MATCH_NODE 0x100
#define H_MATCH_COMMENT 0x200
#define H_MATCH_TEXT 0x400

#define H_MATCH_NODE_MAIN 0x800
#define H_MATCH_COMMENT_MAIN 0x1000
#define H_MATCH_TEXT_MAIN 0x2000

#define MATCHES_TYPE_HOOK 1
#define MATCHES_TYPE_ATTRIB 2
#define MATCHES_TYPE_GROUPS 3

//pattrib flags
#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

#include "npattern.h"

struct hook_t {
  cstr8 name;
  uint16_t flags; //H_
  uintptr_t arg1;
  uintptr_t arg2;
};
typedef struct hook_t hook_t;

typedef struct {
  union {
    reliq_expr expr;
    reliq_pattern pattern;
    reliq_range range;
  } match;
  const hook_t *hook;
  uint8_t invert : 1;
} reliq_hook;

typedef struct {
  nmatchers *list;
  size_t size;
} nmatchers_groups;

struct nmatchers_node {
  union {
    reliq_hook *hook;
    struct pattrib *attrib;
    nmatchers_groups *groups;
  } data;
  uint8_t type; //MATCHES_TYPE_
};

struct pattrib {
  reliq_pattern r[2];
  reliq_range position;
  uint8_t flags; //A_
};

#endif
