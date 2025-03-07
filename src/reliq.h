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

#ifndef RELIQ_H
#define RELIQ_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#if RELIQ_HTML_SIZE == 2
#define RELIQ_HTML_OTHERSIZE(x,y)
#elif RELIQ_HTML_SIZE == 1
#define RELIQ_HTML_OTHERSIZE(x,y) : x
#elif RELIQ_HTML_SIZE == 0
#define RELIQ_HTML_OTHERSIZE(x,y) : y
#endif

#define RELIQ_ERROR_MESSAGE_LENGTH 512

#define RELIQ_ERROR_SYS 5
#define RELIQ_ERROR_HTML 10
#define RELIQ_ERROR_SCRIPT 15

typedef struct {
  char *b;
  size_t s;
} reliq_str;

typedef struct {
  char const *b;
  size_t s;
} reliq_cstr;

typedef struct {
  reliq_cstr key;
  reliq_cstr value;
} reliq_attrib;

#pragma pack(push,1)
typedef struct {
  uint32_t key; //key+hnode.all.b
  uint32_t valuel RELIQ_HTML_OTHERSIZE(24,16);
  uint32_t value RELIQ_HTML_OTHERSIZE(8,8); // value+key+keyl
  uint32_t keyl RELIQ_HTML_OTHERSIZE(8,8);
} reliq_cattrib; //compressed reliq_attrib, can be converted to reliq_attrib with reliq_cattrib_conv()
#pragma pack(pop)
extern const uint8_t reliq_cattrib_sz;

typedef struct {
  char msg[RELIQ_ERROR_MESSAGE_LENGTH];
  int code;
} reliq_error;

#define RELIQ_HNODE_TYPE_TAG 0
#define RELIQ_HNODE_TYPE_COMMENT 1
#define RELIQ_HNODE_TYPE_TEXT 2
#define RELIQ_HNODE_TYPE_TEXT_EMPTY 3
#define RELIQ_HNODE_TYPE_TEXT_ERR 4
typedef struct {
  reliq_cstr all;
  reliq_cstr tag;
  reliq_cstr insides;
  reliq_cattrib const *attribs;
  uint32_t attribsl;
  uint32_t tag_count;
  uint32_t text_count;
  uint32_t comment_count;
  uint16_t lvl;
  uint8_t type;
} reliq_hnode; //html node

#pragma pack(push,1)
/*
    compressed reliq_hnode, can be converted to reliq_hnode with reliq_chnode_conv()

    Most fields are indexes of rq->data starting from .all codependent on each other
    This means that to get the start of the tag you should use rq->data+chnode->all
    To get tag you should use rq->data+chnode->all+chnode->tag
    To get the start of the ending tag you should use rq->data+rq->all+rq->tag+rq->tagl
    +rq->endtag
*/
typedef struct {
  uint32_t all;
  uint32_t all_len; //length of all
  uint32_t endtag; //endtag+tag+tagl+all
  uint32_t attribs;
  uint16_t lvl;

  /*for comments it stores the beggining of the insides,
  for text it stores type: 0 - normal text, 1 - empty text, 2 - erroneous text*/
  uint32_t tagl RELIQ_HTML_OTHERSIZE(16,8);

  uint32_t tag RELIQ_HTML_OTHERSIZE(8,8); //tag+all
  uint32_t tag_count : 30;
  uint32_t text_count : 30;
  uint32_t comment_count : 28;
} reliq_chnode;
#pragma pack(pop)
extern const uint8_t reliq_chnode_sz;

#pragma pack(push,1)
/*Is used when passing around by reliq_exec functions, it holds parent
  inherited from previous expression.

  It's also used to define structure for nodes_output, then .hnode
  stores small integers subtracted from UINT32_MAX, and .parent stores
  pointer to reliq_output_field
*/
typedef struct {
  uint32_t hnode;
  uintptr_t parent; //uint32_t
} reliq_compressed;
#pragma pack(pop)

typedef struct reliq_expr reliq_expr;

typedef struct {
  int (*freedata)(void *addr, size_t len);
  char const *data;
  reliq_chnode *nodes;
  reliq_cattrib *attribs;

  size_t datal; //length of data
  size_t nodesl;
  size_t attribsl;
} reliq;

int reliq_std_free(void *addr, size_t len); //mapping to free(3) that can be used for reliq.freedata

reliq_error *reliq_init(const char *data, const size_t size, reliq *rq);
int reliq_free(reliq *rq);

uint32_t reliq_chnode_attribsl(const reliq *rq, const reliq_chnode *hnode);
uint32_t reliq_chnode_insides(const reliq *rq, const reliq_chnode *hnode, const uint8_t type);
uint8_t reliq_chnode_type(const reliq_chnode *c);
void reliq_chnode_conv(const reliq *rq, const reliq_chnode *c, reliq_hnode *d);
void reliq_cattrib_conv(const reliq *rq, const reliq_cattrib *c, reliq_attrib *d);
const char *reliq_hnode_starttag(const reliq_hnode *hn, size_t *len);
const char *reliq_hnode_endtag(const reliq_hnode *hn, size_t *len);
const char *reliq_hnode_endtag_strip(const reliq_hnode *hn, size_t *len);

reliq reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);
reliq reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);

reliq_error *reliq_ecomp(const char *script, const size_t size, reliq_expr **expr);

reliq_error *reliq_exec_file(reliq *rq, FILE *output, const reliq_expr *expr);
reliq_error *reliq_exec_str(reliq *rq, char **str, size_t *strl, const reliq_expr *expr);
reliq_error *reliq_exec(reliq *rq, reliq_compressed **nodes, size_t *nodesl, const reliq_expr *expr);

void reliq_efree(reliq_expr *expr);

reliq_error *reliq_set_error(const int code, const char *fmt, ...);

#define RELIQ_DECODE_ENTITIES_MAXSIZE 16
int reliq_decode_entities(const char *src, const size_t srcl, size_t *traversed, char *result, const size_t resultl, size_t *written);
void reliq_decode_entities_file(const char *src, const size_t srcl, FILE *out);
void reliq_decode_entities_str(const char *src, const size_t srcl, char **str, size_t *strl);


#ifdef __cplusplus
}
#endif

#endif
