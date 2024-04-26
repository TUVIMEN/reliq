/*
    hgrep - html searching tool
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

#ifndef HGREP_H
#define HGREP_H

#define HGREP_SAVE 0x8

#define HGREP_ERROR_MESSAGE_LENGTH 512

typedef struct {
  void *arg[4];
  unsigned char flags;
} hgrep_format_func;

typedef struct {
  char *b;
  unsigned char s;
} hgrep_str8;

typedef struct {
  char *b;
  size_t s;
} hgrep_str;

typedef struct {
  char const *b;
  size_t s;
} hgrep_cstr;

typedef struct {
  hgrep_cstr f;
  hgrep_cstr s;
} hgrep_cstr_pair;

typedef struct {
  char msg[HGREP_ERROR_MESSAGE_LENGTH];
  int code;
} hgrep_error;

typedef struct {
  hgrep_cstr all;
  hgrep_cstr tag;
  hgrep_cstr insides;
  hgrep_cstr_pair *attribs;
  unsigned int child_count;
  unsigned short attribsl;
  unsigned short lvl;
} hgrep_hnode; //html node

struct hgrep_range_node {
  unsigned int v[3];
  unsigned char flags;
};

typedef struct {
  struct hgrep_range_node *b;
  size_t s;
} hgrep_range;

typedef struct {
  union {
    hgrep_str str;
    regex_t reg;
  } match;
  hgrep_range range;
  unsigned short flags;
} hgrep_pattern;

struct hgrep_pattrib {
  hgrep_pattern r[2];
  hgrep_range position;
  unsigned char flags;
};

typedef struct {
  union {
    hgrep_pattern pattern;
    hgrep_range range;
  } match;
  unsigned short flags;
} hgrep_hook;

typedef struct {
  hgrep_pattern tag;
  struct hgrep_pattrib *attribs;
  size_t attribsl;
  hgrep_hook *hooks;
  size_t hooksl;
  hgrep_range position;
  unsigned char flags;
} hgrep_node;

typedef struct {
  void *e; //either points to hgrep_exprs or hgrep_node
  #ifdef HGREP_EDITING
  hgrep_format_func *nodef;
  hgrep_format_func *exprf;
  #else
  char *nodef;
  #endif
  size_t nodefl;
  #ifdef HGREP_EDITING
  size_t exprfl;
  #endif
  unsigned char istable;
} hgrep_expr;

typedef struct {
  hgrep_expr *b;
  size_t s;
} hgrep_exprs;

#pragma pack(push, 1)
typedef struct {
  size_t id;
  unsigned short lvl;
} hgrep_compressed;
#pragma pack(pop)

typedef struct {
  char const *data;
  FILE *output;
  hgrep_hnode *nodes;
  hgrep_node const *expr; //node passed to process at parsing
  #ifdef HGREP_EDITING
  hgrep_format_func *nodef;
  #else
  char *nodef;
  #endif
  size_t nodefl;
  void *attrib_buffer;
  size_t size;
  size_t nodesl;
  unsigned char flags;
} hgrep;

hgrep hgrep_init(const char *ptr, const size_t size, FILE *output);
hgrep_error *hgrep_fmatch(const char *ptr, const size_t size, FILE *output, const hgrep_node *node,
#ifdef HGREP_EDITING
  hgrep_format_func *nodef,
#else
  char *nodef,
#endif
  size_t nodefl);
hgrep_error *hgrep_efmatch(char *script, const size_t size, FILE *output, const hgrep_exprs *exprs, int (*freeptr)(void *ptr, size_t size));
hgrep_error *hgrep_ncomp(const char *script, size_t size, hgrep_node *node);
hgrep_error *hgrep_ecomp(const char *script, size_t size, hgrep_exprs *exprs, const unsigned char flags);
int hgrep_match(const hgrep_hnode *hgn, const hgrep_node *node);
hgrep_error *hgrep_ematch(hgrep *hg, const hgrep_exprs *expr, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl);
void hgrep_printf(FILE *outfile, const char *format, const size_t formatl, const hgrep_hnode *hgn, const char *reference);
void hgrep_print(FILE *outfile, const hgrep_hnode *hg);
void hgrep_nfree(hgrep_node *node);
void hgrep_efree(hgrep_exprs *expr);
void hgrep_free(hgrep *hg);
hgrep_error *hgrep_set_error(const int code, const char *fmt, ...);

#endif
