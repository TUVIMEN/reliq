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

#ifndef RELIQ_RANGE_H
#define RELIQ_RANGE_H

#include <stddef.h>
#include <stdint.h>
#include "types.h"

//reliq_range flags
#define R_RELATIVE(x) (1<<(x))
#define R_NOTSPECIFIED(x) (4<<(x))
#define R_RANGE 0x10
#define R_NOTEMPTY 0x20
#define R_INVERT 0x40

#define RANGE_SIGNED ((size_t)-1)
#define RANGE_UNSIGNED ((size_t)-2)

struct reliq_range_node {
  uint32_t v[4];
  uint8_t flags; //R_
};

typedef struct {
  struct reliq_range_node *b;
  size_t s;
} reliq_range;

reliq_error *range_comp(const char *src, size_t *pos, const size_t size, reliq_range *range);
unsigned char range_match(const uint32_t matched, const reliq_range *range, const size_t last);
void range_free(reliq_range *range);
unsigned int predict_range_max(const reliq_range *range);

#endif
