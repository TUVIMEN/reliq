/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <hexderm@gmail.com>

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

#ifndef RELIQ_FIELDS
#define RELIQ_FIELDS

#include "types.h"
#include "reliq.h"

struct outfield {
  SINK f;
  char *v;
  size_t s;
  reliq_field const *o;
  uint16_t lvl;
  uchar code;

  //set when something attempted to write, and if not set
  //  tells field types that expression didn't find any hnodes
  uchar notempty : 1;
};

void reliq_field_free(reliq_field *outfield);

reliq_error *reliq_field_comp(const char *src, size_t *pos, const size_t size, reliq_field *outfield);

void outfields_print(const reliq *rq, flexarr *fields, SINK *out); //fields: struct outfield*

void outfields_free(flexarr *outfields); //struct outfield*

#endif
