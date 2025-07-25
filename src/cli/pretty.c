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

#include "../ext.h"
#include "../flexarr.h"

#include <ctype.h>
#include <string.h>
#include <assert.h>

#include "pretty.h"

#define ATTRS_INC -32

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define while_isnt(w,x,y,z) while ((y) < (z) && !w((x)[(y)])) {(y)++;}

#define print_whitespace(prevpos, pos, base, basel) \
  prevpos = pos; \
  while_is(isspace,base,pos,basel); \
  if (!trim_tags && print(base+prevpos,pos-prevpos,st,linesize,0)) \
    return 1

#define COLOR_COMMENT "\033[36;3m"

#define COLOR_TEXT NULL
#define COLOR_TEXT_ERROR "\033[31;1m"

#define COLOR_BRACKETS "\033[34m"
#define COLOR_TAGNAME "\033[33m"
#define COLOR_ATTRIB_KEY "\033[35m"
#define COLOR_ATTRIB_SEPARATOR "\033[32m"
#define COLOR_ATTRIB_VALUE "\033[31m"
#define COLOR_ATTRIB_CLASS "\033[34m"
#define COLOR_ATTRIB_ID "\033[36m"

#define COLOR_CLEAR "\033[0m"

bool should_colorize(FILE *o);

struct print_state {
  size_t lvl;
  bool newline : 1;
  bool justnewline : 1;
  bool not_first : 1;
};

void
pretty_settings_init(struct pretty_settings *settings)
{
  *settings = (struct pretty_settings){
    .maxline = 90,
    .indent = 2,
    .cycle_indent = 0,
    .wrap_script = 0,
    .wrap_style = 0,
    .wrap_text = 1,
    .wrap_comments = 1,
    .trim_tags = 1,
    .trim_attribs = 1,
    .trim_comments = 1,
    .color = 1,
    .normal_case = 1,
    .fix = 1,
    .order_attribs = 1,
    .remove_comments = 0,
    .overlap_ending = 0,
  };
}

struct pretty_state {
  flexarr *attrs_buf; //const reliq_cattrib
  const reliq *rq;
  const struct pretty_settings *s;
  FILE *out;
  struct print_state *p_st;
  bool *linesize;
  const bool color;
};

static void
print_indents(const struct pretty_state *st, const size_t size, size_t *linesize)
{
  struct print_state *p_st = st->p_st;
  FILE *out = st->out;

  if (st->s->maxline != 0 && p_st->not_first && !p_st->justnewline) {
    fputc('\n',out);
    size_t lvl = p_st->lvl;
    if (st->s->cycle_indent != 0)
      lvl %= st->s->cycle_indent;

    const size_t indent = st->s->indent;
    for (size_t i = 0; i < lvl; i++)
      for (size_t j = 0; j < indent; j++)
        fputc(' ',out);
    *linesize = 0;
  }
  p_st->justnewline = (size == 0);
  p_st->not_first = 1;
}

static void
color(const char *col, const struct pretty_state *st)
{
  if (!col || *st->linesize || !st->color)
    return;
  fputs(col,st->out);
}

#define colret(x,y) do { \
    color(x,st); \
    bool r = y; \
    color(COLOR_CLEAR,st); \
    if (r) \
      return 1; \
  } while (0)

#define colretr(x,y) do { \
    color(x,st); \
    bool r = y; \
    color(COLOR_CLEAR,st); \
    return r; \
  } while (0)

static bool
print_r(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize, const bool newline, int (*transform)(int))
{
  if (*st->linesize) {
    *linesize += size;
    return *linesize >= st->s->maxline;
  }

  struct print_state *p_st = st->p_st;
  FILE *out = st->out;

  if (newline && p_st->newline)
    print_indents(st,size,linesize);

  if (size && !st->s->overlap_ending)
    p_st->justnewline = 0;

  if (transform) {
    for (size_t i = 0; i < size; i++)
      fputc(transform(src[i]),out);
  } else
    fwrite(src,1,size,out);

  return 0;
}

static bool
print(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize, const bool newline)
{
  return print_r(src,size,st,linesize,newline,NULL);
}

static bool
print_lower(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize)
{
  return print_r(src,size,st,linesize,0,tolower);
}

static bool
print_case(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize, const bool newline)
{
  if (st->s->normal_case)
    return print_lower(src,size,st,linesize);
  return print(src,size,st,linesize,newline);
}

static bool
print_minified(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize)
{
  for (size_t i = 0; i < size; ) {
    if (isspace(src[i])) {
      if (print_r(" ",1,st,linesize,0,NULL))
        return 1;
      i++;
      while (i < size && isspace(src[i]))
        i++;
    } else {
      if (print_r(src+i,1,st,linesize,0,NULL))
        return 1;
      i++;
    }
  }
  return 0;
}

static size_t
get_word(const char *src, size_t *pos, const size_t size)
{
  while_is(isspace,src,*pos,size);
  size_t ret = *pos;
  while_isnt(isspace,src,*pos,size);
  return ret;
}

static void
get_trimmed(const char *src, size_t size, char const** dest, size_t *destl)
{
  size_t pos = 0;
  while_is(isspace,src,pos,size);
  src += pos;
  size -= pos;

  if (size == 0)
    goto END;

  while (isspace(src[size-1]))
    size--;

  END: ;
  *dest = src;
  *destl = size;
}

static bool
print_wrapped(const char *src, const size_t size, const struct pretty_state *st, const bool wrap, size_t *linesize)
{
  const size_t maxline = st->s->maxline;
  if (!maxline)
    return print_minified(src,size,st,linesize);

  if (!wrap || *st->linesize || *linesize+size < maxline)
    return print(src,size,st,linesize,1);

  size_t pos = 0;
  size_t prevnewline = st->p_st->newline;
  st->p_st->newline = 1;
  while (pos < size) {
    size_t prevpos = pos;

    while (pos < size && pos-prevpos < maxline)
      if (src[pos++] == '\n')
        break;

    if (pos-prevpos >= maxline) {
      pos = prevpos;
      while (pos < size && pos-prevpos < maxline)
        get_word(src,&pos,size);
    }

    const char *line = src+prevpos;
    size_t linel = pos-prevpos;
    get_trimmed(line,linel,&line,&linel);

    print(line,linel,st,linesize,1);
  }
  st->p_st->newline = prevnewline;
  return 0;
}

static bool
print_pretty_text_r(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const char *src;
  size_t size;
  get_trimmed(node->all.b,node->all.s,&src,&size);

  if (size == 0)
    return 0;

  return print_wrapped(src,size,st,st->s->wrap_text,linesize);
}

static bool
print_pretty_text(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  if (node->type == RELIQ_HNODE_TYPE_TEXT_EMPTY)
    return 0;

  colretr(
    (node->type == RELIQ_HNODE_TYPE_TEXT_ERR)
    ? COLOR_TEXT_ERROR : COLOR_TEXT,
    print_pretty_text_r(node,st,linesize));
}

static reliq_cstr
comment_start(const reliq_hnode *node)
{
  const char *src = node->insides.b;
  size_t size = 0;

  while (src-1 != node->all.b && !isspace(*(src-1))) {
    size++;
    src--;
  }

  return (reliq_cstr){
    .b = src,
    .s = size
  };
}

static bool
print_pretty_comment_start(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize, bool *small)
{
  reliq_cstr start = comment_start(node);
  *small = (node->all.s-start.s < 3 || memcmp(start.b,"!--",3) != 0);

  if (st->s->trim_comments) {
    if (print("<!",2,st,linesize,0))
      return 1;
    return (!*small && print("--",2,st,linesize,0));
  } else
    return print(node->all.b,node->insides.b-node->all.b,st,linesize,0);
}

static bool
print_pretty_comment_end(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize, const bool small)
{
  const char *src = node->all.b;
  size_t size = node->all.s;
  reliq_cstr end={ .b=src+size, .s=0 };

  bool ending = 0;
  if (src[size-1] == '>') {
    ending = 1;
    end.b -= 1;
    end.s += 1;
    if (!small && size > 3 && memcmp(src+size-3,"--",2) == 0) {
      end.b -= 2;
      end.s += 2;
    }
  }

  if (ending) {
    return print(end.b,end.s,st,linesize,st->s->wrap_comments);
  } else if (small) {
    return print(">",1,st,linesize,st->s->wrap_comments);
  } else
    return print("-->",3,st,linesize,st->s->wrap_comments);
}

static bool
print_pretty_comment_insides(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const struct pretty_settings *s = st->s;
  const char *src = node->insides.b;
  size_t size = node->insides.s;
  if (s->trim_comments)
    get_trimmed(src,size,&src,&size);

  if (!s->wrap_comments) {
    if (!st->s->maxline)
      return print_minified(src,size,st,linesize);
    return print(src,size,st,linesize,0);
  }
  return print_wrapped(src,size,st,1,linesize);
}

static bool
print_pretty_comment_r(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  bool small;
  if (print_pretty_comment_start(node,st,linesize,&small))
    return 1;

  st->p_st->lvl++;
  bool r = print_pretty_comment_insides(node,st,linesize);
  st->p_st->lvl--;
  if (r)
    return 1;

  return print_pretty_comment_end(node,st,linesize,small);
}

static bool
print_pretty_comment(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  if (st->s->remove_comments || !node->all.s)
    return 0;

  colretr(COLOR_COMMENT,
    print_pretty_comment_r(node,st,linesize));
}

static const reliq_cattrib *
order_cattribs_find(const reliq_cattrib *attribs, const size_t attribsl, const reliq_cattrib *attr)
{
  const uint32_t key = attr->key;
  for (size_t i = 0 ; i < attribsl; i++)
    if (attribs[i].key == key)
      return attribs+i;
  return NULL;
}

static int
memcasecmp(const void *v1, const void *v2, const size_t n)
{
  const char *s1 = v1,
    *s2 = v2;
  for (size_t i = 0; i < n; i++) {
    char u1 = toupper(s1[i]);
    char u2 = toupper(s2[i]);
    char diff = u1-u2;
    if (diff != 0)
      return diff;
  }
  return 0;
}

static const reliq_cattrib *
order_attribs_find(const reliq *rq, const reliq_cattrib *attribs, const size_t attribsl, const reliq_attrib *attr)
{
  for (size_t i = 0 ; i < attribsl; i++) {
    reliq_attrib a;
    reliq_cattrib_conv(rq,attribs+i,&a);

    if (a.key.s == attr->key.s
      && memcasecmp(a.key.b,attr->key.b,a.key.s) == 0)
      return attribs+i;
  }
  return NULL;
}

static void
order_attribs_add(const reliq *rq, flexarr *buf, const size_t pos, const reliq_attrib *attr, reliq_cattrib const *attribs, const size_t attribsl) //buf: reliq_cattrib
{
  *(reliq_cattrib*)flexarr_inc(buf) = attribs[pos];
  size_t j = pos+1;

  while (j < attribsl) {
    const reliq_cattrib *same = order_attribs_find(rq,attribs+j,attribsl-j,attr);
    if (!same)
      break;
    *(reliq_cattrib*)flexarr_inc(buf) = *same;
    j = same-attribs+1;
  }
}

static void
order_attribs(const reliq *rq, flexarr *buf, reliq_cattrib const **attribs, size_t *attribsl) //buf: reliq_cattrib
{
  reliq_cattrib const *a = *attribs;
  const size_t al = *attribsl;
  if (al < 2)
    return;
  buf->size = 0;

  for (size_t i = 0; i < al; i++) {
    const reliq_cattrib *cattr = a+i;
    if (order_cattribs_find(buf->v,buf->size,cattr))
      continue;

    reliq_attrib attr;
    reliq_cattrib_conv(rq,cattr,&attr);
    order_attribs_add(rq,buf,i,&attr,a,al);
  }

  *attribs = buf->v;
  *attribsl = buf->size;
}

static bool
print_pretty_attrib_value_trim(const reliq_attrib *attr, const struct pretty_state *st, size_t *linesize)
{
  size_t pos = 0;
  const char *src = attr->value.b;
  size_t size = attr->value.s;
  get_trimmed(src,size,&src,&size);

  for (size_t i=0; pos < size; i++) {
    if (i && print(" ",1,st,linesize,0))
      return 1;
    size_t start = get_word(src,&pos,size);
    if (print(src+start,pos-start,st,linesize,0))
      return 1;
  }
  return 0;
}

static bool
print_pretty_attrib_separator(const reliq_attrib *attr, const struct pretty_state *st, size_t *linesize, const char quote)
{
  const char *base = attr->key.b+attr->key.s;
  size_t basel = attr->value.b-base;

  if (quote)
    basel--;

  size_t pos=0,prevpos;

  prevpos = pos;
  while_is(isspace,base,pos,basel);
  if (print(base+prevpos,pos-prevpos,st,linesize,0))
    return 1;

  assert(pos < basel && base[pos] == '=');

  colret(COLOR_ATTRIB_SEPARATOR,
    print("=",1,st,linesize,0));
  pos++;

  prevpos = pos;
  while_is(isspace,base,pos,basel);
  return print(base+prevpos,pos-prevpos,st,linesize,0);
}

static bool
print_pretty_attrib_value(const reliq_attrib *attr, const struct pretty_state *st, size_t *linesize, const char quote)
{
  if (quote && print(&quote,1,st,linesize,0))
    return 1;

  if (st->s->trim_attribs) {
    if (print_pretty_attrib_value_trim(attr,st,linesize))
      return 1;
  } else if (print(attr->value.b,attr->value.s,st,linesize,0))
    return 1;

  if (!quote)
    return 0;

  if (attr->value.b+attr->value.s >= st->rq->data+st->rq->datal) {
    if (st->s->fix && print(&quote,1,st,linesize,0))
      return 1;
  } else if (print(&quote,1,st,linesize,0))
    return 1;

  return 0;
}

static bool
print_pretty_attrib_after(const reliq_attrib *attr, const struct pretty_state *st, size_t *linesize)
{
  if (!attr->value.b)
    return 0;

  const struct pretty_settings *s = st->s;
  const bool trim_tags = s->trim_tags;

  char quote = *(attr->value.b-1);
  if (attr->key.b+attr->key.s == attr->value.b-1
    || (quote != '\'' && quote != '"'))
    quote = 0;

  if (!quote && attr->value.s == 0)
    return 0;

  if (trim_tags) {
    colret(COLOR_ATTRIB_SEPARATOR,
      print("=",1,st,linesize,0));
  } else if (print_pretty_attrib_separator(attr,st,linesize,quote))
    return 1;

  colretr(COLOR_ATTRIB_VALUE,
    print_pretty_attrib_value(attr,st,linesize,quote));
}

static bool
print_pretty_attrib(const reliq_attrib *attr, const struct pretty_state *st, size_t *linesize)
{
  const char *key = attr->key.b;
  const size_t keyl = attr->key.s;
  const char *col = COLOR_ATTRIB_KEY;

  if (keyl == 5 && memcasecmp(key,"class",5) == 0) {
    col = COLOR_ATTRIB_CLASS;
  } else if (keyl == 2 && memcasecmp(key,"id",2) == 0)
    col = COLOR_ATTRIB_ID;

  colret(col,
    print_case(key,keyl,st,linesize,0));

  return print_pretty_attrib_after(attr,st,linesize);
}

static bool
print_pretty_attribs_r(const reliq_cattrib *attribs, const size_t attribsl, const struct pretty_state *st, const reliq_cstr *tag, size_t *linesize)
{
  const struct pretty_settings *s = st->s;
  const char *base;
  size_t space;
  reliq_attrib prevattr;
  const bool trim_tags = s->trim_tags;

  for (size_t i = 0; i < attribsl; i++) {
    reliq_attrib attr;
    reliq_cattrib_conv(st->rq,attribs+i,&attr);

    if (trim_tags || s->order_attribs) {
      base = " ";
      space = 1;
    } else {
      if (i == 0) {
        base = tag->b+tag->s;
      } else {
        base = prevattr.value.b+prevattr.value.s;
        if (*base == '"' || *base == '\'')
          base++;
      }
      space = attr.key.b-base;

      prevattr = attr;
    }

    if (print(base,space,st,linesize,0))
      return 1;

    print_pretty_attrib(&attr,st,linesize);
  }

  return 0;
}

static bool
print_pretty_attribs(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  reliq_cattrib const *attribs = node->attribs;
  size_t attribsl = node->attribsl;
  if (st->s->order_attribs)
    order_attribs(st->rq,st->attrs_buf,&attribs,&attribsl);

  return print_pretty_attribs_r(attribs,attribsl,st,&node->tag,linesize);
}

static bool
print_endtag_none(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  colret(COLOR_BRACKETS,
    print("</",2,st,linesize,1));

  colret(COLOR_TAGNAME,
    print_case(node->tag.b,node->tag.s,st,linesize,0));

  colretr(COLOR_BRACKETS,
    print(">",1,st,linesize,0));
}

static size_t
tag_start_before_insides(const reliq_hnode *node, const struct pretty_state *st)
{
  const reliq_cattrib *attribs = node->attribs;
  const size_t attribsl = node->attribsl;

  if (attribsl == 0)
    return node->tag.b+node->tag.s+1-node->all.b;

  reliq_attrib attr;
  reliq_cattrib_conv(st->rq,attribs+attribsl-1,&attr);
  if (attr.value.b) {
    size_t ret = attr.value.b-node->all.b+attr.value.s;
    if (node->all.b[ret] == '"' || node->all.b[ret] == '\'')
      ret++;
    return ret+1;
  }
  return attr.key.b+attr.key.s+1-node->all.b;
}

static bool
tag_selfclosing(const char *tag, const size_t tagl)
{
  static const struct {
    const char *b;
    uint8_t s;
  } selfclosing[] = {
    {"br",2},{"img",3},{"input",5},
    {"link",4},{"meta",4},{"hr",2},{"col",3},{"embed",5},
    {"area",4},{"base",4},{"param",5},
    {"source",6},{"track",5},{"wbr",3},{"command",7},
    {"keygen",6},{"menuitem",8}
  };
  static const size_t selfclosingl = sizeof(selfclosing)/sizeof(*selfclosing);

  for (size_t i = 0; i < selfclosingl; i++)
    if (tagl == selfclosing[i].s
      && memcasecmp(tag,selfclosing[i].b,tagl) == 0)
      return 1;
  return 0;
}

static bool
print_pretty_tag_start_slash(const char *base, size_t basel, const struct pretty_state *st, size_t *linesize)
{
  size_t pos=0,prevpos;

  prevpos = pos;
  while_is(isspace,base,pos,basel);
  if (print(base+prevpos,pos-prevpos,st,linesize,0))
    return 1;

  if (pos < basel && base[pos] == '/') {
    colret(COLOR_BRACKETS,
      print("/",1,st,linesize,0));
    pos++;

    prevpos = pos;
    while_is(isspace,base,pos,basel);
    return print(base+prevpos,pos-prevpos,st,linesize,0);
  }
  return 0;
}

static bool
print_pretty_tag_start_finish(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const struct pretty_settings *s = st->s;
  bool closed = 0;
  size_t size = (node->insides.b)
    ? (size_t)(node->insides.b-node->all.b) : node->all.s;

  if (node->all.b[size-1] == '>')
    closed = 1;

  size_t pos = tag_start_before_insides(node,st);
  const char *ending_slash = NULL;
  const bool ended = (pos < node->all.s
      && (ending_slash = memchr(node->all.b+pos-closed,'/',size-pos)) != NULL);

  if (s->trim_tags) {
    if (ended) {
      if (isspace(*(ending_slash-1))
        && print(" ",1,st,linesize,0))
        return 1;
      colret(COLOR_BRACKETS,
        print("/",1,st,linesize,0));
    }
  } else if (print_pretty_tag_start_slash(node->all.b+pos-closed,size-pos,st,linesize))
    return 1;

  if (closed || s->fix)
    colret(COLOR_BRACKETS,
      print(">",1,st,linesize,0));

  if (!ended && !node->insides.b && s->fix
    && !tag_selfclosing(node->tag.b,node->tag.s))
    return print_endtag_none(node,st,linesize);

  return 0;
}

static bool
tag_start_phplike(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize, bool *phplike)
{
  const bool trim_tags = st->s->trim_tags;
  const char *base = node->all.b;
  size_t basel = node->all.s;
  size_t pos=1,prevpos;

  print_whitespace(prevpos,pos,base,basel);

  if ((*phplike = (base+pos != node->tag.b && base[pos] == '?'))) {
    colret(COLOR_BRACKETS,
      print("?",1,st,linesize,0));
    pos++;
    print_whitespace(prevpos,pos,base,basel);
  }

  return 0;
}

static bool
print_pretty_tag_start(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize, bool *phplike)
{
  colret(COLOR_BRACKETS,
    print("<",1,st,linesize,0));

  if (tag_start_phplike(node,st,linesize,phplike))
    return 1;

  colret(COLOR_TAGNAME,
    print_case(node->tag.b,node->tag.s,st,linesize,0));

  if (*phplike)
    return 0;

  if (print_pretty_attribs(node,st,linesize))
    return 1;

  return print_pretty_tag_start_finish(node,st,linesize);
}

static bool
print_pretty_tag_end(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const struct pretty_settings *s = st->s;
  const bool fix = s->fix;
  size_t endl;
  const char *end = reliq_hnode_endtag(node,&endl);
  if (!end) {
    if (fix && print_endtag_none(node,st,linesize))
      return 1;
    return 0;
  }

  const bool trim_tags = s->trim_tags;

  colret(COLOR_BRACKETS,
    print("<",1,st,linesize,1));

  size_t pos = 1;
  size_t prevpos = pos;
  print_whitespace(prevpos,pos,end,endl);

  assert(end[pos] == '/');
  colret(COLOR_BRACKETS,
    print("/",1,st,linesize,0));
  pos++;

  print_whitespace(prevpos,pos,end,endl);

  prevpos = pos;
  while (pos < endl && !isspace(end[pos]) && end[pos] != '>')
    pos++;

  bool r;
  color(COLOR_TAGNAME,st);
  if (fix || s->normal_case) {
    r = print_case(node->tag.b,node->tag.s,st,linesize,0);
  } else
    r = print(end+prevpos,pos-prevpos,st,linesize,0);
  color(COLOR_CLEAR,st);
  if (r)
    return 1;

  print_whitespace(prevpos,pos,end,endl); //changes prevpos

  if (fix || (pos < endl && end[pos] == '>'))
    colretr(COLOR_BRACKETS,
      print(">",1,st,linesize,0));

  if (s->trim_tags && prevpos != pos)
    print(" ",1,st,linesize,0);

  bool ended = (end[endl-1] == '>');

  if (ended)
    endl--;
  r = print(end+pos,endl-pos,st,linesize,0);
  if (ended)
    colretr(COLOR_BRACKETS,
      print(">",1,st,linesize,0));
  return r;
}

static bool print_pretty_broad(const reliq_chnode *nodes, const size_t nodesl, const struct pretty_state *st, size_t *linesize);

static bool
print_minified_script(const char *src, const size_t size, const struct pretty_state *st, size_t *linesize)
{
  for (size_t i = 0; i < size; ) {
    if (isspace(src[i])) {
      size_t spaces=0,previ=i;
      do {
        if (src[i] == ' ')
          spaces++;
        i++;
      } while (i < size && isspace(src[i]));

      if (i-previ == spaces) {
        for (size_t j = previ; j < i; j++)
          if (print(" ",1,st,linesize,0))
            return 1;
      } else if (print(" ",1,st,linesize,0))
        return 1;
    } else {
      if (print(src+i,1,st,linesize,0))
        return 1;
      i++;
    }
  }

  return 0;
}

static uint8_t
handle_tag_script(const reliq_chnode *next, const reliq_hnode *node, const struct pretty_state *st, size_t *linesize, const size_t desc)
{
  bool script=0,style=0;
  if (node->tag.s == 6 && memcasecmp(node->tag.b,"script",6) == 0) {
    script = 1;
  } else if (node->tag.s == 5 && memcasecmp(node->tag.b,"style",5) == 0)
    style = 1;

  if (!script && !style)
    return (uint8_t)-1;

  assert(desc == 1);

  reliq_hnode text;
  reliq_chnode_conv(st->rq,next,&text);

  uint8_t type = text.type;
  assert(type == RELIQ_HNODE_TYPE_TEXT
    || type == RELIQ_HNODE_TYPE_TEXT_EMPTY
    || type == RELIQ_HNODE_TYPE_TEXT_ERR);

  if (type == RELIQ_HNODE_TYPE_TEXT_EMPTY)
    return 0;

  const char *src;
  size_t size;
  get_trimmed(text.all.b,text.all.s,&src,&size);
  if (size == 0)
    return 0;


  if (!st->s->maxline)
    return print_minified_script(src,size,st,linesize);
  return print_wrapped(src,size,st,
    (script && st->s->wrap_script) || (style && st->s->wrap_style),
    linesize);
}

static bool
print_pretty_tag_insides(const reliq_chnode *chnode, const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const size_t desc = node->tag_count+node->comment_count+node->text_count;
  if (!desc)
    return 0;

  const reliq_chnode *next = chnode+1;

  uint8_t r = handle_tag_script(next,node,st,linesize,desc);
  if (r != (uint8_t)-1)
    return r;

  return print_pretty_broad(next,desc,st,linesize);
}

static bool
phplike_insides(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const bool trim_tags = st->s->trim_tags;
  const char *src = node->insides.b;
  size_t size = node->insides.s;
  size_t pos=(node->tag.b+node->tag.s)-node->all.b,
    prevpos;

  print_whitespace(prevpos,pos,node->all.b,node->all.s);

  if (!st->s->maxline)
    return print_minified(src,size,st,linesize);
  return print(src,size,st,linesize,0);
}

static bool
phplike_end(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  const bool trim_tags = st->s->trim_tags;
  const char *base = node->all.b;
  size_t basel = node->all.s;
  size_t pos=(node->insides.b+node->insides.s)-node->all.b,
    prevpos;

  print_whitespace(prevpos,pos,node->all.b,node->all.s);

  if (pos >= basel) {
    if (st->s->fix) {
      if (print(" ",1,st,linesize,0))
        return 0;
      PRINT_END: ;
      colretr(COLOR_BRACKETS,
        print("?>",2,st,linesize,0));
    }
    return 0;
  }

  assert(pos+2 == basel && base[pos] == '?' && base[pos+1] == '>');
  goto PRINT_END;
}

static bool
phplike_finish(const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  if (phplike_insides(node,st,linesize))
    return 1;
  return phplike_end(node,st,linesize);
}

static bool
print_pretty_tag(const reliq_chnode *chnode, const reliq_hnode *node, const struct pretty_state *st, size_t *linesize)
{
  bool phplike;
  if (print_pretty_tag_start(node,st,linesize,&phplike))
    return 1;

  if (phplike)
    return phplike_finish(node,st,linesize);

  if (!node->insides.b)
    return 0;

  st->p_st->lvl++;
  bool r = print_pretty_tag_insides(chnode,node,st,linesize);
  st->p_st->lvl--;
  if (r)
    return 1;

  return print_pretty_tag_end(node,st,linesize);
}

static size_t
print_pretty_node_r(const reliq_chnode *chnode, const struct pretty_state *st, size_t *linesize, bool *hasnewline)
{
  reliq_hnode node;
  reliq_chnode_conv(st->rq,chnode,&node);

  if (node.type == RELIQ_HNODE_TYPE_TAG) {
    *hasnewline = print_pretty_tag(chnode,&node,st,linesize);
  } else if (node.type == RELIQ_HNODE_TYPE_COMMENT)  {
    *hasnewline = print_pretty_comment(&node,st,linesize);
  } else
    *hasnewline = print_pretty_text(&node,st,linesize);

  return node.tag_count+node.text_count+node.comment_count;
}

static size_t
print_pretty_node(const reliq_chnode *chnode, const struct pretty_state *st, size_t *linesize)
{
  bool hasnewline = 0;
  size_t r;

  bool prevlinesize = *st->linesize;
  size_t prevlsize = *linesize;
  *st->linesize = 1;
  r = print_pretty_node_r(chnode,st,linesize,&hasnewline);
  *st->linesize = prevlinesize;
  size_t size = *linesize;
  *linesize = prevlsize;

  if (*st->linesize) {
    *linesize = size;
  } else {
    if (size-prevlsize > 0)
      print("",0,st,linesize,1);
    const struct pretty_settings *s = st->s;
    const bool newline = st->p_st->newline;
    st->p_st->newline = s->maxline != 0 && size >= s->maxline;
    r = print_pretty_node_r(chnode,st,linesize,&hasnewline);
    st->p_st->newline = newline;
  }

  return r;
}

static bool
print_pretty_broad(const reliq_chnode *nodes, const size_t nodesl, const struct pretty_state *st, size_t *linesize)
{
  for (size_t i = 0; i < nodesl;) {
    i += print_pretty_node(nodes+i,st,linesize)+1;
    if (*st->linesize && *linesize >= st->s->maxline)
      return 1;
  }
  return 0;
}

void
print_pretty(const reliq *rq, const struct pretty_settings *s, FILE *out)
{
  bool color = (s->color == 1)
    ? should_colorize(out)
    : (s->color == 2);

  flexarr attr_buf = flexarr_init(sizeof(const reliq_cattrib),ATTRS_INC);
  struct print_state p_st = {
    .lvl=0,
    .newline=1,
    .justnewline=0,
    .not_first=0
  };
  bool linesize = 0;
  struct pretty_state st = {
    .rq = rq,
    .s = s,
    .out = out,
    .color = color,
    .attrs_buf = &attr_buf,
    .p_st = &p_st,
    .linesize = &linesize
  };

  size_t lsize = 0;
  print_pretty_broad(rq->nodes,rq->nodesl,&st,&lsize);

  flexarr_free(st.attrs_buf);

  if (s->maxline != 0)
    fputc('\n',out);
}
