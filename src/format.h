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

#ifndef RELIQ_FORMAT_H
#define RELIQ_FORMAT_H

#include "flexarr.h"
#include "sink.h"
#include "types.h"

//reliq_format_func flags
#define FORMAT_FUNC         0xf
#define FORMAT_ARG0_ISSTR   0x10
#define FORMAT_ARG1_ISSTR   0x20
#define FORMAT_ARG2_ISSTR   0x40
#define FORMAT_ARG3_ISSTR   0x80

struct reliq_format_func {
  void *arg[4];
  uint8_t flags; //FORMAT_
};
typedef struct reliq_format_func reliq_format_func;

reliq_error *format_exec(char *input, size_t inputl, SINK *output, const reliq_chnode *hnode, const reliq_chnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq);
void format_free(reliq_format_func *format, const size_t formatl);

reliq_error *format_comp(const char *src, size_t *pos, const size_t size, reliq_format_func **format, size_t *formatl);

#endif
