/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2022 TUVIMEN <suchora.dominik7@gmail.com>

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

#ifndef MAIN_H
#define MAIN_H

#define _GNU_SOURCE
#define __USE_XOPEN
#define __USE_XOPEN_EXTENDED
#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <limits.h>
#include <regex.h>
#include <ftw.h>
#include <err.h>
#include "flexarr.h"

#define F_INVERT 0x8
#define F_LIST 0x2
#define F_RECURSIVE 0x4

#define A_INVERT 0x1
#define A_VAL_MATTERS 0x2

#define P_INVERT 0x1
#define P_MATCH_INSIDES 0x2
#define P_INVERT_INSIDES 0x4

#define BUFF_INC_VALUE (1<<15)
#define PATTERN_SIZE (1<<9)
#define PATTERN_SIZE_INC 8
#define BRACKETS_SIZE (1<<6)

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long int ulong;

typedef struct {
    char *b;
    uchar s;
} str8;

typedef struct {
  char *b;
  size_t s;
} str;

typedef struct {
  str f;
  str s;
} str_pair;

struct html_s {
  str all;
  str tag;
  str insides;
  flexarr *attrib;
};

struct pel {
  regex_t r[2];
  uint px; //position
  uint py;
  uchar flags;
};

struct pat {
  regex_t r;
  regex_t in;
  flexarr *attrib;
  uint px; //position
  uint py;
  uint ax; //atribute
  uint ay;
  uint sx; //size
  uint sy;
  uchar flags;
};

#endif
