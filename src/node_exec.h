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

#ifndef RELIQ_NODE_EXEC_H
#define RELIQ_NODE_EXEC_H

#include "reliq.h"
#include "flexarr.h"
#include "npattern.h"

#define AXIS_SELF (1<<0)
#define AXIS_CHILDREN (1<<1)
#define AXIS_DESCENDANTS (1<<2)
#define AXIS_ANCESTORS (1<<3)
#define AXIS_PARENT (1<<4)
#define AXIS_RELATIVE_PARENT (1<<5)
#define AXIS_SIBLINGS_PRECEDING (1<<6)
#define AXIS_SIBLINGS_SUBSEQUENT (1<<7)
#define AXIS_FULL_SIBLINGS_PRECEDING (1<<8)
#define AXIS_FULL_SIBLINGS_SUBSEQUENT (1<<9)
#define AXIS_PRECEDING (1<<10)
#define AXIS_BEFORE (1<<11)
#define AXIS_AFTER (1<<12)
#define AXIS_SUBSEQUENT (1<<13)
#define AXIS_EVERYTHING (1<<14)

typedef void (*axis_func_t)(const reliq*, const reliq_npattern*, const reliq_chnode*, const reliq_chnode*, flexarr*, uint32_t*, const uint32_t);

void axis_comp_functions(uint16_t type, axis_func_t *out);

void node_exec(const reliq *rq, const reliq_chnode *parent, reliq_npattern *nodep, const flexarr *source, flexarr *dest); //source: reliq_compressed, dest: reliq_compressed

#endif
