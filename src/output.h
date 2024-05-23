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

#ifndef HTML_H
#define HTML_H

struct fcollector_expr {
  const reliq_expr *e;
  size_t start;
  size_t end;
  unsigned short lvl;
  unsigned char isnodef;
};

enum outfieldCode {
  ofUnnamed,
  ofNamed,
  ofBlock,
  ofArray,
  ofNoFieldsBlock,
  ofBlockEnd
};

reliq_error *node_output(const reliq_hnode *hnode, const reliq_hnode *parent,
        #ifdef RELIQ_EDITING
        const reliq_format_func *format
        #else
        const char *format
        #endif
        , const size_t formatl, FILE *output, const reliq *rq);
reliq_error *nodes_output(const reliq *rq, flexarr *compressed_nodes, flexarr *pcollector
        #ifdef RELIQ_EDITING
        , flexarr *fcollector
        #endif
        );

#endif

