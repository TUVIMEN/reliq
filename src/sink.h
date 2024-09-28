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

#ifndef RELIQ_SINK_H
#define RELIQ_SINK_H

#include <stdio.h>
#include <stdint.h>
#include "flexarr.h"

/*
    SINK type was created to replace open_memstream(), since it's not portable and slow.

    If created by sink_from_file(), sink_close() will not run fclose() on passed file.

    Accessing ptr after writing to SINK will produce segfault if sink_flush() or sink_close() is not called.

    After using sink_close() ptr has to be freed with free().
*/

#define SINK_TYPE_FLEXARR 1
#define SINK_TYPE_FILE 2

#define SINK_FLEXARR_INC (1<<15)

struct sink_flexarr_t {
  flexarr *fl; //char*
  char **ptr;
  size_t *ptrl;
};

struct sink_t {
  union {
    FILE *file;
    struct sink_flexarr_t sf;
  } v;
  uint8_t type; //SINK_TYPE_
};

typedef struct sink_t SINK;

SINK *sink_open(char **ptr, size_t *ptrl);
SINK *sink_from_file(FILE *f);
SINK *sink_change(SINK *sn, char **ptr, size_t *ptrl, const size_t size);

void sink_set(SINK *sn, const size_t size);

void sink_write(SINK *sn, const char *src, const size_t size);
void sink_put(SINK *sn, const char c);

void sink_flush(SINK *sn);
void sink_close(SINK *sn);
void sink_destroy(SINK *sn);

#endif
