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
#include <string.h>

#include "ctype.h"
#include "utils.h"
#include "sink.h"
#include "format.h"
#include "edit.h"

reliq_error *
wc_edit(const reliq_cstr *src, SINK *output, const edit_args *args)
{
  char delim = '\n';
  const char argv0[] = "wc";
  uchar v[4];
  v[0] = 2; //lines
  v[1] = 2; //words
  v[2] = 2; //characters
  v[3] = 0; //maxline
  size_t r[4] = {0};
  r[2] = src->s;

  reliq_cstr *flags;
  reliq_error *err;
  if ((err = edit_arg_str(args,argv0,0,&flags)))
    return err;
  if (flags) {
    for (size_t i = 0; i < flags->s; i++) {
      if (flags->b[i] == 'c') {
        v[2] = 1;
      } else if (flags->b[i] == 'l') {
        v[0] = 1;
      } else if (flags->b[i] == 'L') {
        v[3] = 1;
      } else if (flags->b[i] == 'w')
        v[1] = 1;
    }
  }

  if ((err = edit_arg_delim(args,argv0,1,&delim,NULL)))
    return err;

  if (v[0] == 1 || v[1] == 1 || v[2] == 1 || v[3] == 1)
    for (uchar i = 0; i < 4; i++)
      if (v[i] == 2)
        v[i] = 0;

  if (v[0] || v[1] || v[3]) {
    size_t saveptr=0;
    reliq_cstr line;

    while (1) {
      line = edit_cstr_get_line(src->b,src->s,&saveptr,delim);
      if (!line.b)
        break;
      r[0]++;

      if (v[1] || v[3]) {
        for (size_t i = 0; i < line.s; i++) {
          if (line.b[i] != delim && !isspace(line.b[i])) {
            i++;
            while (line.b[i] != delim && !isspace(line.b[i]) && i < line.s)
              i++;
            r[1]++;
          }
          if (line.b[i] == delim) {
            if (r[3] < i)
              r[3] = i;
            break;
          }
        }
      }
    }
  }

  uchar amountset = 0;
  for (uchar i = 0; i < 4; i++)
    if (v[i])
      amountset++;

  char numbuf[22];
  size_t numl;

  if (amountset == 1) {
    for (uchar i = 0; i < 4; i++) {
      if (v[i]) {
        uint_to_str(numbuf,&numl,22,r[i]);
        sink_write(output,numbuf,numl);
        break;
      }
    }
  } else for (uchar i = 0; i < 4; i++) {
    if (v[i]) {
      uint_to_str(numbuf,&numl,22,r[i]);
      sink_put(output,'\t');
      sink_write(output,numbuf,numl);
    }
  }

  sink_put(output,'\n');

  return NULL;
}
