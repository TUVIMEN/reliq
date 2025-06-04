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

#ifndef RELIQ_DECODE_ENTITIES_H
#define RELIQ_DECODE_ENTITIES_H

#include <stdbool.h>

#include "sink.h"
#include "types.h"
#include "reliq.h"

void reliq_decode_entities_sink(const char *src, const size_t srcl, SINK *out, bool no_nbsp);
void reliq_encode_entities_sink(const char *src, const size_t srcl, SINK *out, bool full);

#endif
