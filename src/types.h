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

#ifndef RELIQ_TYPES_H
#define RELIQ_TYPES_H

#include <stdint.h>
#include <stddef.h>

#include "flexarr.h"
#include "sink.h"

typedef unsigned char uchar;

#ifndef RELIQ_SMALL_STACK
#define RELIQ_MAX_NODE_LEVEL 8192 //stack overflows at 32744 at 8192kb stack
#define RELIQ_MAX_GROUP_LEVEL 3552 //stack overflows at 14160 at 8192kb stack
#define RELIQ_MAX_BLOCK_LEVEL 6892 //stack overflows at 27576 at 8192kb stack
#else
#define RELIQ_MAX_NODE_LEVEL 256
#define RELIQ_MAX_GROUP_LEVEL 256
#define RELIQ_MAX_BLOCK_LEVEL 256
#endif

#define RELIQ_ERROR_MESSAGE_LENGTH 512

#define RELIQ_ERROR_SYS 5
#define RELIQ_ERROR_HTML 10
#define RELIQ_ERROR_SCRIPT 15

typedef struct {
  char *b;
  uint8_t s;
} reliq_str8;

typedef struct {
  char *b;
  size_t s;
} reliq_str;

typedef struct {
  char const *b;
  size_t s;
} reliq_cstr;

typedef struct {
  reliq_cstr f;
  reliq_cstr s;
} reliq_cstr_pair;

typedef struct {
  char msg[RELIQ_ERROR_MESSAGE_LENGTH];
  int code;
} reliq_error;

typedef struct {
  reliq_cstr all;
  reliq_cstr tag;
  reliq_cstr insides;
  reliq_cstr_pair *attribs;
  uint32_t desc_count; //count of descendants
  uint16_t attribsl;
  uint16_t lvl;
} reliq_hnode; //html node

typedef struct reliq_npattern reliq_npattern;
typedef struct reliq_format_func reliq_format_func;

typedef struct {
  char const *data;
  int (*freedata)(void *addr, size_t len);
  reliq_hnode *nodes;

  size_t nodesl;
  size_t datal; //length of data
} reliq;

int reliq_std_free(void *addr, size_t len); //mapping to free(3) that can be used for freedata

reliq_error *reliq_init(const char *data, const size_t size, int (*freedata)(void *addr, size_t len), reliq *rq);
void reliq_free_hnodes(reliq_hnode *nodes, const size_t nodesl);
int reliq_free(reliq *rq);

reliq_error *reliq_set_error(const int code, const char *fmt, ...);

#endif
