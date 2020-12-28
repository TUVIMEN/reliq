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
#include <err.h>

#define BUFFER_MAX (1<<15)

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
