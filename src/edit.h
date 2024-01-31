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

#ifndef EDIT_H
#define EDIT_H

//hgrep_format_func flags
#define FORMAT_FUNC 0xf
#define FORMAT_ARG0_ISSTR   0x10
#define FORMAT_ARG1_ISSTR   0x20
#define FORMAT_ARG2_ISSTR   0x40
#define FORMAT_ARG3_ISSTR   0x80

struct hgrep_format_function {
  hgrep_str8 name;
  hgrep_error *(*func)(char*,size_t,FILE*,const void*[4],const unsigned char);
};

//hgrep_error *htmldecode_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
hgrep_error *trim_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
hgrep_error *tr_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
hgrep_error *cut_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
hgrep_error *sed_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);

extern const struct hgrep_format_function format_functions[];

hgrep_error *format_exec(char *input, size_t inputl, FILE *output, const hgrep_node *hgn, const hgrep_format_func *format, const size_t formatl, const char *reference);
void format_free(hgrep_format_func *format, size_t formatl);
hgrep_error *format_get_funcs(flexarr *format, char *src, size_t *pos, size_t *size);

#endif
