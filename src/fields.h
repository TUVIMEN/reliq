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

#ifndef RELIQ_FIELDS
#define RELIQ_FIELDS

#include "types.h"
#include "reliq.h"

typedef struct reliq_output_field_type reliq_output_field_type;
struct reliq_output_field_type {
  char type;
  reliq_str *args;
  size_t argsl;
  reliq_output_field_type *subtype;
};

typedef struct {
  reliq_output_field_type type;
  reliq_str name;
  reliq_str annotation;
  unsigned char isset; //signifies that compilation was successful, if set to 0 field should be ignored
} reliq_output_field;

struct outfield {
  SINK f;
  char *v;
  size_t s;
  reliq_output_field const *o;
  uint16_t lvl;
  uchar code;

  //set when something attempted to write, and if not set
  //  tells field types that expression didn't find any hnodes
  uchar notempty : 1;
};

void reliq_output_field_free(reliq_output_field *outfield);

reliq_error *reliq_output_field_comp(const char *src, size_t *pos, const size_t s, reliq_output_field *outfield);

void outfields_print(const reliq *rq, flexarr *fields, SINK *out); //fields: struct outfield*

void outfields_free(flexarr *outfields); //struct outfield*

#endif
