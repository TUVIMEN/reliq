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

#ifndef EDIT_H
#define EDIT_H

#include <stdio.h>
#include <string.h>
#include "flexarr.h"
#include "sink.h"
#include "reliq.h"

//reliq_format_func flags
#define FORMAT_FUNC         0xf
#define FORMAT_ARG0_ISSTR   0x10
#define FORMAT_ARG1_ISSTR   0x20
#define FORMAT_ARG2_ISSTR   0x40
#define FORMAT_ARG3_ISSTR   0x80

typedef reliq_error *(*reliq_format_function_t)(char*,size_t,SINK*,const void*[4],const uint8_t);

struct reliq_format_function {
  reliq_str8 name;
  reliq_format_function_t func;
};

reliq_error *trim_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *tr_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *cut_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *sed_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *line_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *decode_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *sort_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *uniq_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *echo_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *rev_edit(char *src, size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *tac_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *wc_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);

extern const struct reliq_format_function format_functions[];

reliq_error *format_exec(char *input, size_t inputl, SINK *output, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq);
void format_free(reliq_format_func *format, const size_t formatl);
reliq_error *format_get_funcs(flexarr *format, const char *src, size_t *pos, const size_t size);

#endif
