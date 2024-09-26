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

#ifndef RELIQ_H
#define RELIQ_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <regex.h>

//#RELIQ_COMPILE_FLAGS

#ifndef RELIQ_SMALL_STACK
#define RELIQ_MAX_NODE_LEVEL 8192 //stack overflows at 32744 at 8192kb stack
#define RELIQ_MAX_GROUP_LEVEL 3552 //stack overflows at 14160 at 8192kb stack
#define RELIQ_MAX_BLOCK_LEVEL 6892 //stack overflows at 27576 at 8192kb stack
#else
#define RELIQ_MAX_NODE_LEVEL 256
#define RELIQ_MAX_GROUP_LEVEL 256
#define RELIQ_MAX_BLOCK_LEVEL 256
#endif

#define RELIQ_SAVE 0x1

#define RELIQ_ERROR_MESSAGE_LENGTH 512

#define RELIQ_ERROR_SYS 5
#define RELIQ_ERROR_HTML 10
#define RELIQ_ERROR_SCRIPT 15

typedef struct {
  void *arg[4];
  uint8_t flags; //FORMAT_
} reliq_format_func;

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

struct reliq_range_node {
  uint32_t v[4];
  uint8_t flags; //R_
};

typedef struct {
  struct reliq_range_node *b;
  size_t s;
} reliq_range;

typedef struct {
  union {
    reliq_str str;
    regex_t reg;
  } match;
  reliq_range range;
  uint16_t flags; //RELIQ_PATTERN_
} reliq_pattern;

struct reliq_pattrib {
  reliq_pattern r[2];
  reliq_range position;
  uint8_t flags; //A_
};

typedef struct {
  reliq_str name;
  char type;
  char arr_delim;
  char arr_type;
  unsigned char isset;
} reliq_output_field;

typedef struct {
  reliq_output_field outfield;
  void *e; //either points to reliq_exprs or reliq_npattern
  #ifdef RELIQ_EDITING
  reliq_format_func *nodef; //node format
  reliq_format_func *exprf; //expression format
  #else
  char *nodef;
  #endif
  size_t nodefl;
  #ifdef RELIQ_EDITING
  size_t exprfl;
  #endif
  uint16_t childfields;
  uint8_t flags; //EXPR_
} reliq_expr;

typedef struct {
  reliq_expr *b;
  size_t s;
} reliq_exprs;

typedef struct {
  union {
    reliq_exprs exprs;
    reliq_pattern pattern;
    reliq_range range;
  } match;
  uint16_t flags; //H_
} reliq_hook;

typedef struct reliq_node_matches reliq_node_matches;

typedef struct {
  reliq_node_matches *list;
  size_t size;
} reliq_node_matches_groups;

typedef struct {
  union {
    reliq_pattern *tag;
    reliq_hook *hook;
    struct reliq_pattrib *attrib;
    reliq_node_matches_groups *groups;
  } data;
  uint8_t type; //MATCHES_TYPE_
} reliq_node_matches_node;

struct reliq_node_matches {
  reliq_node_matches_node *list;
  size_t size;
};

typedef struct reliq_npattern reliq_npattern;
struct reliq_npattern {
  reliq_node_matches matches;
  reliq_range position;

  uint32_t position_max;
  uint8_t flags; //N_
};

typedef struct {
  reliq_hnode *hnode;
  reliq_hnode *parent;
} reliq_compressed;

typedef struct {
  char const *data;
  int (*freedata)(void *addr, size_t len);
  reliq_hnode *nodes;

  void *output;
  reliq_npattern const *expr; //node passed to process at parsing

  void *attrib_buffer; //used as temporary buffer for attribs

  reliq_hnode const *parent;

  #ifdef RELIQ_EDITING
  reliq_format_func *nodef;
  #else
  char *nodef;
  #endif
  size_t nodefl; //format used for output at parsing

  size_t nodesl;
  size_t datal; //length of data
  uint8_t flags; //RELIQ_
} reliq;

int reliq_std_free(void *addr, size_t len); //mapping to free(3) that can be used for freedata

reliq_error *reliq_init(const char *data, const size_t size, int (*freedata)(void *addr, size_t len), reliq *rq);
int reliq_free(reliq *rq);

reliq reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);
reliq reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl);

//node pattern
reliq_error *reliq_ncomp(const char *script, const size_t size, reliq_npattern *nodep);
int reliq_nexec(const reliq *rq, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_npattern *nodep);
void reliq_nfree(reliq_npattern *nodep);

//expression
reliq_error *reliq_ecomp(const char *script, const size_t size, reliq_exprs *exprs);

reliq_error *reliq_fexec_file(char *data, const size_t size, FILE *output, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len));
reliq_error *reliq_fexec_str(char *data, const size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freedata)(void *addr, size_t len));

reliq_error *reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs);
reliq_error *reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs);
reliq_error *reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_exprs *exprs);

void reliq_efree(reliq_exprs *exprs);

reliq_error *reliq_set_error(const int code, const char *fmt, ...);

#endif
