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

#ifndef RELIQ_EDIT_H
#define RELIQ_EDIT_H

#include "types.h"
#include "format.h"
#include "range.h"

typedef reliq_format_func edit_args;

reliq_error *edit_arg_str(const edit_args *args, const char *argv0, const uchar num, reliq_cstr **dest);
reliq_error *edit_arg_delim(const edit_args *args, const char *argv0, const uchar num, char *delim, uchar *found);
reliq_error *edit_arg_range(const edit_args *args, const char *argv0, const uchar num, reliq_range const **dest);
reliq_error *edit_missing_arg(const char *argv0);

reliq_cstr edit_cstr_get_line(const char *src, const size_t size, size_t *saveptr, const char delim);

reliq_error *tr_strrange(const char *src1, const size_t size1, const char *src2, const size_t size2, unsigned char arr[256], unsigned char arr_enabled[256], unsigned char complement);

reliq_error *sed_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *decode_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *encode_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *wc_edit(const reliq_cstr *src, SINK *output, const edit_args *args);

reliq_error *tr_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *lower_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *upper_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *cut_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *trim_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *line_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *sort_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *uniq_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *echo_edit(const reliq_cstr *src, SINK *output, const edit_args *args);
reliq_error *rev_edit(const reliq_str *src, SINK *output, const edit_args *args);
reliq_error *tac_edit(const reliq_cstr *src, SINK *output, const edit_args *args);

#endif
