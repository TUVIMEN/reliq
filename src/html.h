/*
    hgrep - html searching tool
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
  const hgrep_expr *e;
  size_t start;
  size_t end;
  unsigned short lvl;
  unsigned char isnodef;
};

hgrep_error *node_output(hgrep_hnode *hgn,
        #ifdef HGREP_EDITING
        const hgrep_format_func *format
        #else
        const char *format
        #endif
        , const size_t formatl, FILE *output, const char *reference);
hgrep_error *nodes_output(hgrep *hg, flexarr *compressed_nodes, flexarr *pcollector
        #ifdef HGREP_EDITING
        , flexarr *fcollector
        #endif
        );
ulong html_struct_handle(const char *f, size_t *i, const size_t s, const ushort lvl, flexarr *nodes, hgrep *hg, hgrep_error **err);

#endif

