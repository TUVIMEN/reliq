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

#ifndef FLEXARR_H
#define FLEXARR_H

typedef struct {
  void *v;
  size_t asize; //allocated size
  size_t size; //used size
  size_t elsize; //size of a single element
  size_t inc_r; //increase rate
} flexarr;

flexarr *flexarr_init(const size_t elsize, const size_t inc_r);
void *flexarr_inc(flexarr *f);
void *flexarr_dec(flexarr *f);
void *flexarr_set(flexarr *f, const size_t s);
void *flexarr_alloc(flexarr *f, const size_t s);
void *flexarr_add(flexarr *dst, const flexarr *src);
void *flexarr_clearb(flexarr *f);
void flexarr_conv(flexarr *f, void **v, size_t *s);
void flexarr_free(flexarr *f);

#endif
