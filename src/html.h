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

#ifndef RELIQ_HTML_H
#define RELIQ_HTML_H

#include "flexarr.h"
#include "types.h"

struct html_process_expr {
  SINK *output;
  reliq_npattern const *expr;
  void *nodef;
  reliq *rq;
  size_t nodefl;
};

reliq_error *html_handle(const char *data, const size_t size, reliq_chnode **nodes, size_t *nodesl, reliq_cattrib **attribs, size_t *attribsl, struct html_process_expr *expr);

#endif

