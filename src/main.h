/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2021 TUVIMEN <suchora.dominik7@gmail.com>

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
#include <ctype.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <limits.h>
#include <regex.h>
#include <ftw.h>
#include <err.h>
#include "flexarr.h"

#define F_REVERSE 0x1
#define F_LIST 0x2
#define F_RECURSIVE 0x4

#define BUFF_INC_VALUE (1<<15)
#define PATTERN_SIZE (1<<9)
#define PATTERN_SIZE_INC 16
#define BRACKETS_SIZE (1<<4)

typedef unsigned char uchar;
typedef unsigned short ushort;

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
  uchar flags;
  ushort px;
  ushort py;
  regex_t r[2];
};

struct pat {
  ushort px;
  ushort py;
  ushort ax;
  ushort ay;
  regex_t r;
  flexarr *attrib;
};

#endif