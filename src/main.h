/*
    hgrep - simple html searching tool
    Copyright (C) 2020 TUVIMEN <suchora.dominik7@gmail.com>

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
#include <regex.h>
#include <ftw.h>
#include <err.h>

#define F_REVERSE 0x1
#define F_LIST 0x2
#define F_RECURSIVE 0x4

typedef struct {
  char *b;
  size_t s;
} vv;

typedef struct {
  vv f;
  vv s;
} cc;

struct html_s {
  vv m;
  vv n;
  cc *a;
  size_t s;
};

#endif
