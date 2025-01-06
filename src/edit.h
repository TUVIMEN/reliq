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

#ifndef RELIQ_EDIT_H
#define RELIQ_EDIT_H

#include "flexarr.h"
#include "sink.h"
#include "types.h"

#include "edit_sed.h"
#include "edit_wc.h"
#include "edit_tr.h"

int edit_get_arg_delim(const void *args[4], const int num, const uint8_t flag, char *delim);
reliq_cstr edit_cstr_get_line(const char *src, const size_t size, size_t *saveptr, const char delim);

reliq_error *trim_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *cut_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *line_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *decode_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *sort_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *uniq_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *echo_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *rev_edit(char *src, size_t size, SINK *output, const void *arg[4], const uint8_t flag);
reliq_error *tac_edit(const char *src, const size_t size, SINK *output, const void *arg[4], const uint8_t flag);

#endif
