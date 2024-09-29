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

#ifndef RELIQ_OUTPUT_H
#define RELIQ_OUTPUT_H

#include "types.h"
#include "flexarr.h"
#include "sink.h"
#include "exprs.h"

struct fcollector_expr {
  const reliq_expr *e;
  size_t start;
  size_t end;
  uint16_t lvl;
  unsigned char isnodef;
};

enum outfieldCode {
  ofUnnamed = 1, //start of unnamed field that manifests only when search fails, it's not terminated by ofBlockEnd
  ofNamed, //start of the named field
  ofBlock, //start of block with fields
  ofArray, //start of fielded array
  ofNoFieldsBlock, //start of block with no fields
  ofBlockEnd //end of all the above
};

reliq_error *reliq_output_field_comp(const char *src, size_t *pos, const size_t s, reliq_output_field *outfield);

reliq_error *node_output(const reliq_hnode *hnode, const reliq_hnode *parent,
#ifdef RELIQ_EDITING
  const reliq_format_func *format,
#else
  const char *format,
#endif
  const size_t formatl, SINK *output, const reliq *rq);

reliq_error *nodes_output(const reliq *rq, SINK *output, flexarr *compressed_nodes, flexarr *ncollector
  #ifdef RELIQ_EDITING
  , flexarr *fcollector //struct fcollector_expr
  #endif
  ); //compressed_nodes: reliq_compressed, ncollector: reliq_cstr

#endif

