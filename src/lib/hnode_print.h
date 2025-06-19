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

#ifndef RELIQ_HNODE_PRINT_H
#define RELIQ_HNODE_PRINT_H

#include "sink.h"
#include "types.h"

void chnode_printf(SINK *outfile, const char *format, const size_t formatl, const reliq_chnode *chnode, const reliq_chnode *parent, const reliq *rq);
void chnode_print(SINK *outfile, const reliq_chnode *chnode, const reliq *rq);

#endif
