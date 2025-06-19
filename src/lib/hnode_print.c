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

#include <stdlib.h>

#include "ctype.h"
#include "entities.h"
#include "utils.h"
#include "hnode_print.h"

#define PC_UNTRIM 0x1
#define PC_DECODE 0x2

static void
print_chars(char const *src, size_t size, const uint8_t flags, SINK *outfile)
{
  if (!(flags&PC_UNTRIM))
    memtrim(&src,&size,src,size);
  if (!size)
    return;
  if (flags&PC_DECODE) {
    reliq_decode_entities_sink(src,size,outfile,true);
  } else
    sink_write(outfile,src,size);
}

static void
print_attribs(const reliq *rq, const reliq_cattrib *attribs, const uint32_t attribsl, const uint8_t flags, SINK *outfile)
{
  const reliq_cattrib *a = attribs;
  if (!a)
    return;
  for (uint32_t j = 0; j < attribsl; j++) {
    sink_put(outfile,' ');
    char const *base = rq->data+a[j].key;
    sink_write(outfile,base,a[j].keyl);
    sink_write(outfile,"=\"",2);
    base += a[j].keyl+a[j].value;
    print_chars(base,a[j].valuel,flags,outfile);
    sink_put(outfile,'"');
  }
}

static void
print_attrib_value_text(const reliq *rq, const reliq_cattrib *attribs, const size_t attribsl, const char *text, const size_t textl, const uint8_t flags, SINK *outfile)
{
  const reliq_cattrib *a = attribs;
  for (size_t i = 0; i < attribsl; i++) {
    char const *base = rq->data+a[i].key;
    if (memcaseeq(base,text,textl,a[i].keyl)) {
      base += a[i].keyl+a[i].value;
      print_chars(base,a[i].valuel,flags,outfile);
    }
  }
}

static void
print_attrib_value(const reliq *rq, const reliq_cattrib *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uint8_t flags, SINK *outfile)
{
  const reliq_cattrib *a = attribs;
  if (num != -1) {
    if ((size_t)num < attribsl) {
      const char *base = rq->data+a[num].key+a[num].keyl+a[num].value;
      print_chars(base,a[num].valuel,flags,outfile);
    }
  } else if (textl != 0) {
    print_attrib_value_text(rq,a,attribsl,text,textl,flags,outfile);
  } else for (size_t i = 0; i < attribsl; i++) {
    char const *base = rq->data+a[i].key+a[i].keyl+a[i].value;
    print_chars(base,a[i].valuel,flags,outfile);
    sink_put(outfile,'"');
  }
}

static void
print_text_r(const reliq *rq, const reliq_chnode *hnode, uint8_t flags, SINK *outfile, uchar recursive)
{
  if (hnode->text_count == 0)
    return;

  const size_t size = hnode->tag_count+hnode->text_count+hnode->comment_count;
  for (size_t i = 1; i <= size; i++) {
    const reliq_chnode *n = hnode+i;

    uint8_t type = reliq_chnode_type(n);
    if (type == RELIQ_HNODE_TYPE_TEXT || type == RELIQ_HNODE_TYPE_TEXT_ERR || type == RELIQ_HNODE_TYPE_TEXT_EMPTY) {
      print_chars(rq->data+n->all,n->all_len,flags,outfile);
    } else if (recursive && type == RELIQ_HNODE_TYPE_TAG)
      print_text_r(rq,n,flags,outfile,recursive);

    i += n->tag_count+n->text_count+n->comment_count;
  }
}


static void
print_text(const reliq *rq, const reliq_chnode *hnode, uint8_t flags, SINK *outfile, uchar recursive)
{
  SINK *out = outfile;
  SINK t_out;
  const uchar trim = (flags&PC_UNTRIM) ? 0 : 1;
  char *ptr;
  size_t ptrl;
  if (trim) {
    t_out = sink_open(&ptr,&ptrl);
    out = &t_out;
  }

  print_text_r(rq,hnode,flags|PC_UNTRIM,out,recursive);

  if (trim) {
    sink_close(out);
    char const *ptr_r;
    size_t ptrl_r;
    memtrim(&ptr_r,&ptrl_r,ptr,ptrl);;
    sink_write(outfile,ptr_r,ptrl_r);
    free(ptr);
  }
}

static uchar
printf_C(const reliq_hnode *hn, char c, SINK *outfile)
{
  switch (c) {
    case 'a':
      print_uint(hn->tag_count+hn->text_count+hn->comment_count,outfile);
      break;
    case 't':
      print_uint(hn->text_count,outfile);
      break;
    case 'c':
      print_uint(hn->comment_count,outfile);
      break;
    default:
      return 1;
  }
  return 0;
}

void
chnode_printf(SINK *outfile, const char *format, const size_t formatl, const reliq_chnode *chnode, const reliq_chnode *parent, const reliq *rq)
{
  size_t i = 0;
  char const *text=NULL;
  size_t textl=0;
  int num = -1;
  reliq_hnode hnode;
  reliq_chnode_conv(rq,chnode,&hnode);

  while (i < formatl) {
    if (format[i] == '\\') {
      size_t resultl,traversed;
      char result[8];
      i++;
      splchar3(format+i,formatl-i,result,&resultl,&traversed);
      if (resultl != 0) {
        sink_write(outfile,result,resultl);
        i += traversed;
        continue;
      } else
        i--;
    }
    if (format[i] == '%') {
      if (++i >= formatl)
        break;
      if (isdigit(format[i])) {
        num = number_handle(format,&i,formatl);
      } else if (format[i] == '(') {
        text = ++i+format;
        char *t = memchr(format+i,')',formatl-i);
        if (!t)
          return;
        textl = t-(format+i);
        i = t-format+1;
      }

      uint8_t printflags=0; //PC_
      char const *src;
      size_t srcl;

      REPEAT: ;
      if (i >= formatl)
          return;
      switch (format[i++]) {
        case '%': sink_put(outfile,'%'); break;
        case 'U': printflags |= PC_UNTRIM; goto REPEAT; break;
        case 'D': printflags |= PC_DECODE; goto REPEAT; break;
        case 'i': print_chars(hnode.insides.b,hnode.insides.s,printflags,outfile); break;
        case 't': print_text(rq,chnode,printflags,outfile,0); break;
        case 'T': print_text(rq,chnode,printflags,outfile,1); break;
        case 'l':
          print_int((parent) ? hnode.lvl-parent->lvl : hnode.lvl,outfile);
          break;
        case 'L': print_uint(hnode.lvl,outfile); break;
        case 'a':
          print_attribs(rq,hnode.attribs,hnode.attribsl,printflags,outfile); break;
        case 'v':
          print_attrib_value(rq,hnode.attribs,hnode.attribsl,text,textl,num,printflags,outfile);
          break;
        case 's': print_uint(hnode.all.s,outfile); break;
        case 'c': print_uint(hnode.tag_count,outfile); break;
        case 'C':
          if (i >= formatl)
            break;
          if (!printf_C(&hnode,format[i],outfile))
            i++;
          break;
        case 'A':
          if (hnode.type == RELIQ_HNODE_TYPE_TAG) {
            print_chars(hnode.all.b,hnode.all.s,printflags|PC_UNTRIM,outfile);
          } else
            print_chars(hnode.all.b,hnode.all.s,printflags,outfile);
          break;
        case 'S':
          src = reliq_hnode_starttag(&hnode,&srcl);
          print_chars(src,srcl,printflags|PC_UNTRIM,outfile);
          break;
        case 'e':
          src = reliq_hnode_endtag_strip(&hnode,&srcl);
          if (!src)
            break;
          print_chars(src,srcl,printflags,outfile);
          break;
        case 'E':
          src = reliq_hnode_endtag(&hnode,&srcl);
          if (!src)
            break;
          print_chars(src,srcl,printflags|PC_UNTRIM,outfile);
          break;
        case 'I': print_uint(hnode.all.b-rq->data,outfile); break;
        case 'p':
          print_int(parent ? chnode-parent : chnode-rq->nodes,outfile);
          break;
        case 'P': print_uint(chnode-rq->nodes,outfile); break;
        case 'n': sink_write(outfile,hnode.tag.b,hnode.tag.s); break;
      }
      continue;
    }
    sink_put(outfile,format[i++]);
  }
}

void
chnode_print(SINK *outfile, const reliq_chnode *chnode, const reliq *rq)
{
  sink_write(outfile,chnode->all+rq->data,chnode->all_len);
  sink_put(outfile,'\n');
}
