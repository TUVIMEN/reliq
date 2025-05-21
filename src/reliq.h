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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//#RELIQ_COMPILE_FLAGS

#ifdef RELIQ_SMALL_STACK
#define RELIQ_MAX_NODE_LEVEL 256
#define RELIQ_MAX_GROUP_LEVEL 256
#define RELIQ_MAX_BLOCK_LEVEL 256
#else
#define RELIQ_MAX_NODE_LEVEL 8192 //stack overflows at 32744 at 8192kb stack
#define RELIQ_MAX_GROUP_LEVEL 3552 //stack overflows at 14160 at 8192kb stack
#define RELIQ_MAX_BLOCK_LEVEL 6892 //stack overflows at 27576 at 8192kb stack
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
  stores small integers subtracted from UINT32_MAX i.e. (UINT32_MAX-6), and .parent stores
  pointer to reliq_output_field
*/
typedef struct {
  uint32_t hnode;
  uintptr_t parent; //uint32_t
} reliq_compressed;
#pragma pack(pop)

typedef struct reliq_expr reliq_expr;

typedef struct {
  reliq_str url;

  reliq_cstr scheme; //should be treated without case distinction
  reliq_cstr netloc;
  reliq_cstr path;
  reliq_cstr params;
  reliq_cstr query;
  reliq_cstr fragment;

  size_t allocated; // allocated size for .url
} reliq_url;

//scheme is optional so scheme and schemel can be set to NULL and 0
//if reuse is set then dest structure will be reused, but it has to be
//  initiated either by setting it to zero or calling reliq_url_parse
//  without reuse set
void reliq_url_parse(const char *url, const size_t urll, const char *scheme, const size_t schemel, const bool reuse, reliq_url *dest);

//if url and dest point to the same location url will be reused
//url should be initialized by passing scheme and schemel from ref e.g.
//  void urljoin(const reliq_url *ref, const char *url, const size_t urll, reliq_url *dest) {
//    reliq_url u;
//    reliq_url_parse(url,urll,ref->scheme,ref->schemel,false,&u);
//    reliq_url_join(ref,&u,dest);
//  }
void reliq_url_join(const reliq_url *ref, const reliq_url *url, reliq_url *dest);

void reliq_url_free(reliq_url *url);

//if reliq.freedata is set than it will be called with reliq_free() to free reliq.data
typedef struct {
  reliq_url url;

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
int reliq_free(reliq *rq); //returns result of .freedata() otherwise 0

//sets reliq.url, which automatically gets freed by reliq_free()
//if reliq.url was already set, it will be reused so reliq.url
//  doesn't have to be deallocated before calling this function
void reliq_set_url(reliq *rq, const char *url, const size_t urll);

uint32_t reliq_chnode_attribsl(const reliq *rq, const reliq_chnode *hnode);
uint32_t reliq_chnode_insides(const reliq *rq, const reliq_chnode *hnode, const uint8_t type);
uint8_t reliq_chnode_type(const reliq_chnode *c);
const char *reliq_hnode_starttag(const reliq_hnode *hn, size_t *len);
const char *reliq_hnode_endtag(const reliq_hnode *hn, size_t *len);
const char *reliq_hnode_endtag_strip(const reliq_hnode *hn, size_t *len);

void reliq_chnode_conv(const reliq *rq, const reliq_chnode *c, reliq_hnode *d);
void reliq_cattrib_conv(const reliq *rq, const reliq_cattrib *c, reliq_attrib *d);

// creates reliq with independent reliq.nodes
reliq reliq_from_compressed(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);
//creates reliq with independent reliq.nodes and reliq.data
reliq reliq_from_compressed_independent(const reliq_compressed *compressed, const size_t compressedl, const reliq *rq);

reliq_error *reliq_ecomp(const char *script, const size_t size, reliq_expr **expr);

//input and inputl can be set to NULL and 0 if unused
reliq_error *reliq_exec_file(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, FILE *output);
reliq_error *reliq_exec_str(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, char **str, size_t *strl);
reliq_error *reliq_exec(const reliq *rq, const reliq_compressed *input, const size_t inputl, const reliq_expr *expr, reliq_compressed **nodes, size_t *nodesl);

void reliq_efree(reliq_expr *expr);

#define RELIQ_SCHEME_TYPE_NULL 1
#define RELIQ_SCHEME_TYPE_STRING 2
#define RELIQ_SCHEME_TYPE_UNSIGNED 3
#define RELIQ_SCHEME_TYPE_INT 4
#define RELIQ_SCHEME_TYPE_NUM 5
#define RELIQ_SCHEME_TYPE_BOOL 6
#define RELIQ_SCHEME_TYPE_DATE 7
#define RELIQ_SCHEME_TYPE_URL 8
#define RELIQ_SCHEME_TYPE_OBJECT 9
#define RELIQ_SCHEME_TYPE_UNKNOWN 10

//objects of this type should have their subtype used instead
#define RELIQ_SCHEME_TYPE_ARRAY 127

typedef struct reliq_scheme_field_type reliq_scheme_field_type;
struct reliq_scheme_field_types {
  reliq_scheme_field_type *b;
  size_t s;
};

struct reliq_scheme_field_type {
  union {
    unsigned char type;
    struct reliq_scheme_field_types types;
  } type;
  unsigned char isarray : 1;
};

struct reliq_scheme_field {
  reliq_cstr name;
  reliq_cstr annotation;
  uint16_t lvl;

  struct reliq_scheme_field_types types;
};

typedef struct {
  struct reliq_scheme_field *fields;
  size_t fieldsl;

  //is set to 1 if some output isn't guarded by a field which creates incorrect json
  unsigned char leaking : 1;
  //field name repeats in the same block which creates incorrect json
  unsigned char repeating : 1;
} reliq_scheme;

reliq_scheme reliq_json_scheme(const reliq_expr *expr);
void reliq_json_scheme_free(reliq_scheme *scheme);

reliq_error *reliq_set_error(const int code, const char *fmt, ...);

//if no_nbsp is set then &nbsp; entity will be converted to space instead of \u00a0, this is desirable unless you're a browser

#define RELIQ_DECODE_ENTITY_MAXSIZE 7 //maximum size that decoded entity can take
//resultl ideally should be at least RELIQ_DECODE_ENTITY_MAXSIZE
//it returns 0 on success and -1 on size conflict
int reliq_decode_entity(const char *src, const size_t srcl, size_t *traversed, char *result, const size_t resultl, size_t *written, bool no_nbsp);

void reliq_decode_entities_file(const char *src, const size_t srcl, FILE *out, bool no_nbsp);
void reliq_decode_entities_str(const char *src, const size_t srcl, char **str, size_t *strl, bool no_nbsp);


//if full is set than all entities will be considered, there's probably no reason to use it

#define RELIQ_ENCODE_ENTITY_MAXSIZE 31 //maximum size that encoded entity can take
//resultl ideally should be at least RELIQ_ENCODE_ENTITY_MAXSIZE
//it returns 0 on success and -1 on size conflict
int reliq_encode_entity(const char *src, const size_t srcl, size_t *traversed, char *result, const size_t resultl, size_t *written, bool full);

void reliq_encode_entities_file(const char *src, const size_t srcl, FILE *out, bool full);
void reliq_encode_entities_str(const char *src, const size_t srcl, char **str, size_t *strl, bool full);


#ifdef __cplusplus
}
#endif

#endif
