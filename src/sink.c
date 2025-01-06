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

#include <stdlib.h>
#include "sink.h"

SINK *
sink_open(char **ptr, size_t *ptrl)
{
  SINK *sn = malloc(sizeof(SINK));
  sn->type = SINK_TYPE_FLEXARR;
  sn->v.sf.fl = flexarr_init(sizeof(char),SINK_FLEXARR_INC);
  *ptr = NULL;
  *ptrl = 0;
  sn->v.sf.ptr = ptr;
  sn->v.sf.ptrl = ptrl;
  return sn;
}

SINK *
sink_from_file(FILE *f)
{
  SINK *sn = malloc(sizeof(SINK));
  sn->type = SINK_TYPE_FILE;
  sn->v.file = f;
  return sn;
}

void
sink_set(SINK *sn, const size_t size) //makes sure that SINK has at least size bytes allocated
{
  if (!size || sn->type != SINK_TYPE_FLEXARR)
    return;

  flexarr *fl = sn->v.sf.fl;
  if (fl->asize < size)
    flexarr_set(fl,size);
}

void
sink_write(SINK *sn, const char *src, const size_t size)
{
  if (!size)
    return;

  if (sn->type == SINK_TYPE_FLEXARR) {
    flexarr_append(sn->v.sf.fl,src,size);
  } else
    fwrite(src,1,size,sn->v.file);
}

void
sink_put(SINK *sn, const char c)
{
  if (sn->type == SINK_TYPE_FLEXARR) {
    *(char*)flexarr_inc(sn->v.sf.fl) = c;
  } else
    fputc(c,sn->v.file);
}

static inline void
sink_flexarr_flush(SINK *sn)
{
  *sn->v.sf.ptr = sn->v.sf.fl->v;
  *sn->v.sf.ptrl = sn->v.sf.fl->size;
}

void
sink_flush(SINK *sn)
{
  if (sn->type == SINK_TYPE_FLEXARR) {
    sink_flexarr_flush(sn);
  } else if (sn->type == SINK_TYPE_FILE)
    fflush(sn->v.file);
}

SINK *
sink_change(SINK *sn, char **ptr, size_t *ptrl, const size_t size)
{
  if (sn->type != SINK_TYPE_FLEXARR)
    return sn;
  sn->v.sf.fl->size = size;
  sn->v.sf.ptr = ptr;
  sn->v.sf.ptrl = ptrl;
  sink_flexarr_flush(sn);
  return sn;
}

void
sink_close(SINK *sn)
{
  if (!sn)
    return;

  if (sn->type == SINK_TYPE_FLEXARR) {
    flexarr_conv(sn->v.sf.fl,(void**)sn->v.sf.ptr,sn->v.sf.ptrl);
  } else
    fflush(sn->v.file);

  free(sn);
}

void
sink_destroy(SINK *sn)
{
  if (!sn)
    return;

  if (sn->type == SINK_TYPE_FLEXARR) {
    flexarr_free(sn->v.sf.fl);
  } else
    fclose(sn->v.file);
  free(sn);
}
