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

#define RELIQ_SAVE 0x1
#define RELIQ_IMMUTABLE 0x2

#define RELIQ_ERROR_MESSAGE_LENGTH 512

typedef struct {
  void *arg[4];
  unsigned char flags;
} reliq_format_func;

typedef struct {
  char *b;
  unsigned char s;
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
  unsigned int child_count;
  unsigned short attribsl;
  unsigned short lvl;
} reliq_hnode; //html node

struct reliq_range_node {
  unsigned int v[4];
  unsigned char flags;
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
  unsigned short flags;
} reliq_pattern;

struct reliq_pattrib {
  reliq_pattern r[2];
  reliq_range position;
  unsigned char flags;
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
  void *e; //either points to reliq_exprs or reliq_node
  #ifdef RELIQ_EDITING
  reliq_format_func *nodef;
  reliq_format_func *exprf;
  #else
  char *nodef;
  #endif
  size_t nodefl;
  #ifdef RELIQ_EDITING
  size_t exprfl;
  #endif
  ushort childfields;
  unsigned char flags;
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
  unsigned char flags;
} reliq_hook;

typedef struct reliq_node reliq_node;
struct reliq_node {
  reliq_pattern tag;
  reliq_range position;
  reliq_range siblings_preceding;
  reliq_range siblings_subsequent;
  struct reliq_pattrib *attribs;
  reliq_hook *hooks;
  reliq_node *node;

  size_t hooksl;
  size_t attribsl;
  unsigned char flags;
};

typedef struct {
  reliq_hnode *hnode;
  reliq_hnode *parent;
} reliq_compressed;

typedef struct {
  char const *data;
  reliq_hnode *nodes;

  FILE *output;
  reliq_node const *expr; //node passed to process at parsing

  void *attrib_buffer; //used as temporary buffer for attribs

  #ifdef RELIQ_EDITING
  reliq_format_func *nodef;
  #else
  char *nodef;
  #endif
  size_t nodefl; //format used for output at parsing

  size_t nodesl;
  size_t size; //length of data
  unsigned char flags;
} reliq;

reliq reliq_init(const char *ptr, const size_t size);

reliq_error *reliq_ncomp(const char *script, size_t size, reliq_node *node);
reliq_error *reliq_ecomp(const char *script, size_t size, reliq_exprs *exprs);

reliq reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);
reliq reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl, char **ptr, size_t *size);

int reliq_match(const reliq_hnode *hnode, const reliq_hnode *parent, const reliq_node *node);

reliq_error *reliq_fexec_file(char *ptr, const size_t size, FILE *output, const reliq_exprs *exprs, int (*freeptr)(void *ptr, size_t size));
reliq_error *reliq_fexec_str(char *ptr, const size_t size, char **str, size_t *strl, const reliq_exprs *exprs, int (*freeptr)(void *ptr, size_t size));

reliq_error *reliq_exec_file(reliq *rq, FILE *output, const reliq_exprs *exprs);
reliq_error *reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_exprs *exprs);
reliq_error *reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_exprs *exprs);

void reliq_printf(FILE *outfile, const char *format, const size_t formatl, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq *rq);
void reliq_print(FILE *outfile, const reliq_hnode *hnode);

void reliq_nfree(reliq_node *node);
void reliq_efree(reliq_exprs *expr);
void reliq_free(reliq *rq);

reliq_error *reliq_set_error(const int code, const char *fmt, ...);

#endif
