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

#ifndef RELIQ_EXPRS_H
#define RELIQ_EXPRS_H

#include "types.h"

typedef struct {
  reliq_str name;
  char type;
  char arr_delim;
  char arr_type;
  unsigned char isset;
} reliq_output_field;

typedef struct {
  reliq_output_field outfield;
  void *e; //either points to reliq_exprs or reliq_npattern
  #ifdef RELIQ_EDITING
  reliq_format_func *nodef; //node format
  reliq_format_func *exprf; //expression format
  #else
  char *nodef;
  #endif
  size_t nodefl;
  #ifdef RELIQ_EDITING
  size_t exprfl;
  #endif
  uint16_t childfields;
  uint8_t flags; //EXPR_
} reliq_expr;

#include "output.h"

typedef struct {
  reliq_hnode *hnode;
  reliq_hnode *parent;
} reliq_compressed;

typedef struct {
  reliq_expr *b;
  size_t s;
} reliq_exprs;

reliq_error *reliq_ecomp(const char *script, const size_t size, reliq_exprs *exprs);

reliq_error *reliq_fexec_file(char *data, const size_t size, FILE *output, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len));
reliq_error *reliq_fexec_str(char *data, const size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len));

reliq_error *reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs);
reliq_error *reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs);
reliq_error *reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_exprs *exprs);

void reliq_efree(reliq_exprs *exprs);

#endif
