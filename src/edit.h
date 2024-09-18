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

//reliq_format_func flags
#define FORMAT_FUNC         0xf
#define FORMAT_ARG0_ISSTR   0x10
#define FORMAT_ARG1_ISSTR   0x20
#define FORMAT_ARG2_ISSTR   0x40
#define FORMAT_ARG3_ISSTR   0x80

struct reliq_format_function {
  reliq_str8 name;
  reliq_error *(*func)(char*,size_t,FILE*,const void*[4],const unsigned char);
};

reliq_error *trim_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *tr_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *cut_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *sed_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *line_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *sort_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *uniq_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *echo_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *rev_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *tac_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);
reliq_error *wc_edit(char *src, size_t size, FILE *output, const void *arg[4], const unsigned char flag);

extern const struct reliq_format_function format_functions[];

reliq_error *format_exec(char *input, size_t inputl, FILE *output, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_format_func *format, const size_t formatl, const reliq *rq);
void format_free(reliq_format_func *format, size_t formatl);
reliq_error *format_get_funcs(flexarr *format, const char *src, size_t *pos, const size_t size);

#endif
