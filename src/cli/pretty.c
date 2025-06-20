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

#include "../ext.h"
#include "pretty.h"

typedef unsigned char uchar;

void
pretty_settings_init(struct pretty_settings *settings)
{
  *settings = (struct pretty_settings){
    .maxline = 90,
    .indent = 2,
    .cycle_indent = 0,
    .indent_script = 1,
    .indent_style = 1,
    .wrap_text = 1,
    .wrap_comments = 1,
    .trim_tags = 1,
    .trim_attribs = 1,
    .trim_comments = 1,
    .color = 1,
    .normal_case = 1,
    .fix = 1,
    .merge_attribs = 1,
    .remove_comments = 0,
    .show_hidden = 0,
    .overlap_ending = 0,
  };
}
