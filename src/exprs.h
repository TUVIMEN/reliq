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

#ifndef RELIQ_EXPRS_H
#define RELIQ_EXPRS_H

#include "types.h"

#include "sink.h"

//reliq_expr flags
//.e = reliq_npattern
#define EXPR_NPATTERN 1

//.e = flexarr*(reliq_expr)
#define EXPR_BLOCK 2
#define EXPR_BLOCK_CONDITION 3
#define EXPR_CHAIN 4
#define EXPR_SINGULAR 5
#define EXPR_TYPE 0x7

#define EXPR_TYPE_IS(x,y) (((x)&EXPR_TYPE) == (y))
#define EXPR_IS_TABLE(x) (((x)&EXPR_TYPE) >= EXPR_BLOCK && ((x)&EXPR_TYPE) <= EXPR_SINGULAR)
#define EXPR_TYPE_SET(x,y) (x = ((y)|((x)&(~EXPR_TYPE))))

#define EXPR_CONDITION_EXPR 0x8
#define EXPR_AND 0x10 // &
#define EXPR_AND_BLANK 0x20 // &&
#define EXPR_OR 0x40 // ||
#define EXPR_ALL 0x80 // ^ succeed if everything succeedes. If not set, succeed if anything succeedes
#define EXPR_CONDITION (EXPR_CONDITION_EXPR|EXPR_AND|EXPR_AND_BLANK|EXPR_OR|EXPR_ALL)

#include "fields.h"
#include "output.h"

struct reliq_expr {
  reliq_output_field outfield;
  void *e; //either points to flexarr*(reliq_expr) or reliq_npattern
  reliq_format_func *nodef; //node format
  reliq_format_func *exprf; //expression format
  size_t nodefl;
  size_t exprfl;
  uint16_t childfields;
  uint16_t childformats;
  uint8_t flags; //EXPR_
};

reliq_error *reliq_exec_r(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, SINK *output, reliq_compressed **outnodes, size_t *outnodesl);

reliq_error *expr_check_chain(const reliq_expr *expr);

void reliq_efree_intr(reliq_expr *expr);
reliq_error *reliq_ecomp_intr(const char *src, const size_t size, reliq_expr *expr);

#endif
