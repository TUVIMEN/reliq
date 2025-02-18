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

#include "ext.h"

#include "ctype.h"
#include "htmlescapecodes.h"
#include "utils.h"
#include "hnode.h"
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
    htmlescapecodes_file(src,size,outfile);
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
print_attrib_value(const reliq *rq, const reliq_cattrib *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uint8_t flags, SINK *outfile)
{
  const reliq_cattrib *a = attribs;
  if (num != -1) {
    if ((size_t)num < attribsl) {
      const char *base = rq->data+a[num].key+a[num].keyl+a[num].value;
      print_chars(base,a[num].valuel,flags,outfile);
    }
  } else if (textl != 0) {
    for (size_t i = 0; i < attribsl; i++) {
      char const *base = rq->data+a[i].key;
      if (memcomp(base,text,textl,a[i].keyl)) {
        base += a[i].keyl+a[i].value;
        print_chars(base,a[i].valuel,flags,outfile);
      }
    }
  } else for (size_t i = 0; i < attribsl; i++) {
    char const *base = rq->data+a[i].key+a[i].keyl+a[i].value;
    print_chars(base,a[i].valuel,flags,outfile);
    sink_put(outfile,'"');
  }
}

static void
print_text(const reliq *rq, const reliq_chnode *hnode, uint8_t flags, SINK *outfile, uchar recursive)
{
  if (hnode->text_count == 0)
    return;

  const char *data = rq->data;
  flags |= PC_UNTRIM;

  const size_t size = hnode->tag_count+hnode->text_count+hnode->comment_count;
  for (size_t i = 1; i <= size; i++) {
    const reliq_chnode *n = hnode+i;

    uint8_t type = chnode_type(n);
    if (type == RELIQ_HNODE_TYPE_TEXT || type == RELIQ_HNODE_TYPE_TEXT_ERR || type == RELIQ_HNODE_TYPE_TEXT_EMPTY) {
        print_chars(data+n->all,n->all_len,flags,outfile);
    } else if (recursive && type == RELIQ_HNODE_TYPE_TAG)
      print_text(rq,n,flags,outfile,recursive);

    i += n->tag_count+n->text_count+n->comment_count;
  }
}

void
chnode_printf(SINK *outfile, const char *format, const size_t formatl, const reliq_chnode *chnode, const reliq_chnode *parent, const reliq *rq)
{
  size_t i = 0;
  char const *text=NULL;
  size_t textl=0;
  int num = -1;
  reliq_hnode hnode;
  chnode_conv(rq,chnode,&hnode);

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

      uint8_t printflags=0, //PC_
        endinsides=0;
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
        case 'l': {
          uint16_t lvl = hnode.lvl;
          if (parent) {
            if (lvl < parent->lvl) {
              lvl = parent->lvl-lvl; //happens when passed from ancestor
            } else
              lvl -= parent->lvl;
          }
          print_uint(lvl,outfile);
          }
          break;
        case 'L': print_uint(hnode.lvl,outfile); break;
        case 'a':
          print_attribs(rq,hnode.attribs,hnode.attribsl,printflags,outfile); break;
        case 'v':
          print_attrib_value(rq,hnode.attribs,hnode.attribsl,text,textl,num,printflags,outfile);
          break;
        case 's': print_uint(hnode.all.s,outfile); break;
        case 'c': print_uint(hnode.tag_count,outfile); break;
        case 'C': print_chars(hnode.all.b,hnode.all.s,printflags|PC_UNTRIM,outfile); break;
        case 'S':
          src = hnode.all.b;
          if (hnode.insides.b) {
            srcl = hnode.insides.b-hnode.all.b;
          } else
            srcl = hnode.all.s;
          print_chars(src,srcl,printflags|PC_UNTRIM,outfile);
          break;
        case 'e':
          endinsides = 1;
        case 'E':
          if (!hnode.insides.b)
            break;
          srcl = hnode.all.s-(hnode.insides.b-hnode.all.b)-hnode.insides.s;
          src = hnode.insides.b+hnode.insides.s;
          if (!srcl)
            break;
          if (endinsides) {
            if (srcl < 1)
              break;
            src++;
            srcl--;
            if (srcl > 0 && src[srcl-1] == '>')
              srcl--;
          }

          print_chars(src,srcl,printflags|(endinsides ? 0 : PC_UNTRIM),outfile);
          break;
        case 'I': print_uint(hnode.all.b-rq->data,outfile); break;
        case 'p': {
          uint32_t pos = chnode-rq->nodes;
          if (parent) {
            if (chnode < parent) {
              pos = parent-chnode;
            } else
              pos = chnode-parent;
          }
          print_uint(pos,outfile);
          }
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
