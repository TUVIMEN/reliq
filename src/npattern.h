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

#ifndef RELIQ_NPATTERN_H
#define RELIQ_NPATTERN_H

#include "pattern.h"
#include "range.h"
#include "types.h"
#include "exprs.h"

//reliq_npattern flags
#define N_MATCHED_TYPE 0xf
#define N_FULL 1
#define N_SELF 2
#define N_CHILD 3
#define N_DESCENDANT 4
#define N_ANCESTOR 5
#define N_PARENT 6
#define N_RELATIVE_PARENT 7
#define N_SIBLING 8
#define N_SIBLING_PRECEDING 9
#define N_SIBLING_SUBSEQUENT 10
#define N_FULL_SIBLING 11
#define N_FULL_SIBLING_PRECEDING 12
#define N_FULL_SIBLING_SUBSEQUENT 13

#define N_EMPTY 0x10 //ignore matching
#define N_POSITION_ABSOLUTE 0x20

typedef struct {
  union {
    reliq_expr expr;
    reliq_pattern pattern;
    reliq_range range;
  } match;
  uint16_t flags; //H_
} reliq_hook;

typedef struct reliq_node_matches reliq_node_matches;

typedef struct {
  reliq_node_matches *list;
  size_t size;
} reliq_node_matches_groups;

typedef struct {
  union {
    reliq_pattern *tag;
    reliq_hook *hook;
    struct reliq_pattrib *attrib;
    reliq_node_matches_groups *groups;
  } data;
  uint8_t type; //MATCHES_TYPE_
} reliq_node_matches_node;

struct reliq_node_matches {
  reliq_node_matches_node *list;
  size_t size;
};

struct reliq_pattrib {
  reliq_pattern r[2];
  reliq_range position;
  uint8_t flags; //A_
};

typedef struct reliq_npattern reliq_npattern;
struct reliq_npattern {
  reliq_node_matches matches;
  reliq_range position;

  uint32_t position_max;
  uint8_t flags; //N_
};

reliq_error *reliq_ncomp(const char *script, const size_t size, reliq_npattern *nodep);
void node_exec(const reliq *rq, const reliq_hnode *parent, reliq_npattern *nodep, const flexarr *source, flexarr *dest); //source: reliq_compressed, dest: reliq_compressed
int reliq_nexec(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_npattern *nodep);
void reliq_nfree(reliq_npattern *nodep);

#endif
