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

#ifndef RELIQ_NPATTERN_H
#define RELIQ_NPATTERN_H

#include "pattern.h"
#include "range.h"

typedef struct nmatchers_node nmatchers_node;

typedef struct {
  nmatchers_node *list;
  size_t size;
  uint8_t type; //NM_
} nmatchers;

typedef struct {
  nmatchers matches;
  reliq_range position;

  uint32_t position_max;
  uint16_t flags; //N_
} reliq_npattern;

reliq_error *reliq_ncomp(const char *script, const size_t size, reliq_npattern *nodep);
int reliq_nexec(const reliq *rq, const reliq_chnode *chnode, const reliq_chnode *parent, const reliq_npattern *nodep);
void reliq_nfree(reliq_npattern *nodep);

#endif
