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

#ifndef CLI_PRETTY_H
#define CLI_PRETTY_H

#include "../lib/reliq.h"

struct pretty_settings {
  size_t maxline;
  size_t indent;
  size_t cycle_indent;
  uint8_t color : 2;
  bool wrap_script : 1;
  bool wrap_style : 1;
  bool wrap_text : 1;
  bool wrap_comments : 1;
  bool trim_tags : 1;
  bool trim_attribs : 1;
  bool trim_comments : 1;
  bool normal_case : 1;
  bool fix : 1;
  bool order_attribs : 1;
  bool remove_comments : 1;
  bool overlap_ending : 1;
};

void pretty_settings_init(struct pretty_settings *settings);

void print_pretty(const reliq *rq, const struct pretty_settings *s, FILE *out);

#endif
