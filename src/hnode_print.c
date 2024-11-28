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

#include "ext.h"

#include "ctype.h"
#include "htmlescapecodes.h"
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
    htmlescapecodes_file(src,size,outfile);
  } else
    sink_write(outfile,src,size);
}

static void
print_attribs(const reliq_hnode *hnode, const uint8_t flags, SINK *outfile)
{
  reliq_cstr_pair *a = hnode->attribs;
  const size_t size = hnode->attribsl;
  for (uint16_t j = 0; j < size; j++) {
    sink_put(outfile,' ');
    sink_write(outfile,a[j].f.b,a[j].f.s);
    sink_write(outfile,"=\"",2);
    print_chars(a[j].s.b,a[j].s.s,flags,outfile);
    sink_put(outfile,'"');
  }
}

static void
print_attrib_value(const reliq_cstr_pair *attribs, const size_t attribsl, const char *text, const size_t textl, const int num, const uint8_t flags, SINK *outfile)
{
  if (num != -1) {
    if ((size_t)num < attribsl)
      print_chars(attribs[num].s.b,attribs[num].s.s,flags,outfile);
  } else if (textl != 0) {
    for (size_t i = 0; i < attribsl; i++)
      if (memcomp(attribs[i].f.b,text,textl,attribs[i].f.s))
        print_chars(attribs[i].s.b,attribs[i].s.s,flags,outfile);
  } else for (size_t i = 0; i < attribsl; i++) {
    print_chars(attribs[i].s.b,attribs[i].s.s,flags,outfile);
    sink_put(outfile,'"');
  }
}

static void
print_text(const reliq_hnode *nodes, const reliq_hnode *hnode, uint8_t flags, SINK *outfile, uchar recursive)
{
  char const *start = hnode->insides.b;
  size_t end;
  flags |= PC_UNTRIM;

  const size_t size = hnode->desc_count;
  for (size_t i = 1; i <= size; i++) {
    const reliq_hnode *n = hnode+i;

    end = n->all.b-start;
    if (end)
      print_chars(start,end,flags,outfile);

    if (recursive)
      print_text(nodes,n,flags,outfile,recursive);

    i += n->desc_count;
    start = n->all.b+n->all.s;
  }

  end = hnode->insides.s-(start-hnode->insides.b);
  if (end)
    print_chars(start,end,flags,outfile);
}

void
hnode_printf(SINK *outfile, const char *format, const size_t formatl, const reliq_hnode *hnode, const reliq_hnode *parent, const reliq *rq)
{
  size_t i = 0;
  char const *text=NULL;
  size_t textl=0;
  int num = -1;
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
        case 'i': print_chars(hnode->insides.b,hnode->insides.s,printflags,outfile); break;
        case 't': print_text(rq->nodes,hnode,printflags,outfile,0); break;
        case 'T': print_text(rq->nodes,hnode,printflags,outfile,1); break;
        case 'l': {
          uint16_t lvl = hnode->lvl;
          if (parent) {
            if (lvl < parent->lvl) {
              lvl = parent->lvl-lvl; //happens when passed from ancestor
            } else
              lvl -= parent->lvl;
          }
          print_uint(lvl,outfile);
          }
          break;
        case 'L': print_uint(hnode->lvl,outfile); break;
        case 'a':
          print_attribs(hnode,printflags,outfile); break;
        case 'v':
          print_attrib_value(hnode->attribs,hnode->attribsl,text,textl,num,printflags,outfile);
          break;
        case 's': print_uint(hnode->all.s,outfile); break;
        case 'c': print_uint(hnode->desc_count,outfile); break;
        case 'C': print_chars(hnode->all.b,hnode->all.s,printflags|PC_UNTRIM,outfile); break;
        case 'S':
          src = hnode->all.b;
          if (hnode->insides.b) {
            srcl = hnode->insides.b-hnode->all.b;
          } else
            srcl = hnode->all.s;
          print_chars(src,srcl,printflags|PC_UNTRIM,outfile);
          break;
        case 'e':
          endinsides = 1;
        case 'E':
          if (!hnode->insides.b)
            break;
          srcl = hnode->all.s-(hnode->insides.b-hnode->all.b)-hnode->insides.s;
          src = hnode->insides.b+hnode->insides.s;
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
        case 'I': print_uint(hnode->all.b-rq->data,outfile); break;
        case 'p': {
          uint32_t pos = hnode-rq->nodes;
          if (parent) {
            if (hnode < parent) {
              pos = parent-hnode;
            } else
              pos = hnode-parent;
          }
          print_uint(pos,outfile);
          }
          break;
        case 'P': print_uint(hnode-rq->nodes,outfile); break;
        case 'n': sink_write(outfile,hnode->tag.b,hnode->tag.s); break;
      }
      continue;
    }
    sink_put(outfile,format[i++]);
  }
}

void
hnode_print(SINK *outfile, const reliq_hnode *hnode)
{
  sink_write(outfile,hnode->all.b,hnode->all.s);
  sink_put(outfile,'\n');
}
