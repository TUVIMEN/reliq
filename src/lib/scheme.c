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

#include "output.h"
#include "reliq.h"
#include "exprs.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#define SCHEME_INC -64

static const reliq_expr *
scheme_last_chainlink(const reliq_expr *expr)
{
  if (!EXPR_TYPE_IS(expr->flags,EXPR_CHAIN))
    return NULL;

  const flexarr *exprs = (flexarr*)expr->e;
  const size_t exprsl = exprs->size;
  if (!exprsl)
    return NULL;

  return ((reliq_expr*)exprs->v)+exprsl-1;
}

static uchar
scheme_is_repeating(flexarr *fields, const size_t index, uint16_t lvl)
{
  const size_t fieldsl = fields->size;
  struct reliq_scheme_field *fieldsv = fields->v;

  for (size_t i = index; i < fieldsl && fieldsv[i].lvl >= lvl; i++) {
    if (fieldsv[i].lvl != lvl)
      continue;

    const char *name = fieldsv[i].field->name.b;
    const size_t namel = fieldsv[i].field->name.s;

    for (size_t j = i+1; j < fieldsl && fieldsv[j].lvl >= lvl; j++) {
      if (fieldsv[j].lvl != lvl)
        continue;

      if (memeq(name,fieldsv[j].field->name.b,namel,fieldsv[j].field->name.s))
        return 1;
    }
  }

  return 0;
}

static void reliq_scheme_r(const reliq_expr *expr, flexarr *fields, uchar *leaking, uchar *repeating, uint16_t lvl);

static void
scheme_search_block(flexarr *exprs, flexarr *fields, uchar *leaking, uchar *repeating, const uint16_t lvl) //exprs: reliq_expr, fields: struct reliq_scheme_field
{
  const reliq_expr *exprsv = exprs->v;
  const size_t exprsl = exprs->size;
  size_t index = fields->size;

  for (size_t i = 0; i < exprsl; i++)
    reliq_scheme_r(exprsv+i,fields,leaking,repeating,lvl);
  if (!*repeating)
    *repeating = scheme_is_repeating(fields,index+1,lvl+1);
}

static void
reliq_scheme_r(const reliq_expr *expr, flexarr *fields, uchar *leaking, uchar *repeating, uint16_t lvl) //fields: struct reliq_scheme_field
{
  if (!expr)
    return;
  if (expr->outfield.isset) {
    if (!expr->outfield.name.b) {
      *leaking = 1;
      return;
    }

    uint8_t type = RELIQ_SCHEME_FIELD_TYPE_NORMAL;
    if (expr->childfields > 1) {
      type = RELIQ_SCHEME_FIELD_TYPE_OBJECT;
      if (EXPR_TYPE_IS(expr->flags,EXPR_CHAIN)) {
        const reliq_expr *lastlink = scheme_last_chainlink(expr);
        if (lastlink && EXPR_TYPE_IS(lastlink->flags,EXPR_SINGULAR))
          type = RELIQ_SCHEME_FIELD_TYPE_ARRAY;
      }
    }

    *(struct reliq_scheme_field*)flexarr_inc(fields) = (struct reliq_scheme_field){
      .field = &expr->outfield,
      .lvl = lvl,
      .type = type
    };
    lvl++;
  } else if (expr->childfields == 0) {
    *leaking = 1;
    return;
  }

  uchar type = expr->flags&EXPR_TYPE;
  if (type == EXPR_NPATTERN || type == EXPR_BLOCK_CONDITION)
    return;

  const reliq_expr *lastlink = scheme_last_chainlink(expr);
  if (lastlink) {
    if (expr->childfields == 1)
      return;
    reliq_scheme_r(lastlink,fields,leaking,repeating,lvl);
    return;
  }

  scheme_search_block((flexarr*)expr->e,fields,leaking,repeating,lvl);
}

reliq_scheme_t
reliq_scheme(const reliq_expr *expr)
{
  flexarr fields_arr = flexarr_init(sizeof(struct reliq_scheme_field),SCHEME_INC);
  uchar leaking=0,repeating=0;

  scheme_search_block((flexarr*)expr->e,&fields_arr,&leaking,&repeating,0);
  if (!repeating)
    repeating = scheme_is_repeating(&fields_arr,0,0);

  struct reliq_scheme_field *fields;
  size_t fieldsl;
  flexarr_conv(&fields_arr,(void**)&fields,&fieldsl);

  return (reliq_scheme_t){
    .fields = fields,
    .fieldsl = fieldsl,
    .leaking = (leaking > 0),
    .repeating = (repeating > 0)
  };
}

void
reliq_scheme_free(reliq_scheme_t *scheme)
{
  if (scheme->fields)
    free(scheme->fields);
}
