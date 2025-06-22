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

#ifndef RELIQ_FLEXARR_H
#define RELIQ_FLEXARR_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  void *v;
  size_t asize; //allocated size
  size_t size; //used size
  uint32_t elsize; //size of a single element
  uint32_t inc_r; //increase rate
} flexarr;

//easier than an external inlined function
#define flexarr_init(x,y) (flexarr){ .elsize = (x), .inc_r = (y) }

void *flexarr_inc(flexarr *f);
void *flexarr_incz(flexarr *f); //same as above but zeroes the memory
void *flexarr_append(flexarr *f, const void *v, const size_t count); //append count amount of things from v
void *flexarr_add(flexarr *dst, const flexarr *src); //append contents of src to dst

void *flexarr_dec(flexarr *f);

void *flexarr_set(flexarr *f, const size_t s); //set number of allocated elements to s
void *flexarr_alloc(flexarr *f, const size_t s); //allocate additional s amount of elements

void *flexarr_clearb(flexarr *f); //clear buffer
void flexarr_conv(flexarr *f, void **v, size_t *s); //convert from flexarr to normal array
void flexarr_free(flexarr *f);

#endif
