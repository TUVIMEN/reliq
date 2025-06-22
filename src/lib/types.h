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

#ifndef RELIQ_TYPES_H
#define RELIQ_TYPES_H

#include <stdint.h>
#include <stddef.h>

#include "../flexarr.h"
#include "sink.h"

typedef unsigned char uchar;

typedef struct {
  char *const b;
  uint8_t s;
} cstr8;

//#define SCHEME_DEBUG
//#define EXPR_DEBUG
//#define TOKEN_DEBUG
//#define NCOLLECTOR_DEBUG
//#define FCOLLECTOR_DEBUG
//#define PRINT_CODE_DEBUG


#include "reliq.h"

#endif
