/*
    hgrep - simple html searching tool
    Copyright (C) 2020-2023 Dominik Stanisław Suchora <suchora.dominik7@gmail.com>

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

#ifndef HGREP_H
#define HGREP_H

#define HGREP_EREGEX 0x1
#define HGREP_ICASE 0x2

typedef struct {
  char *b;
  size_t s;
} hgrep_str;

typedef struct {
  hgrep_str f;
  hgrep_str s;
} hgrep_str_pair;

typedef struct {
  hgrep_str all;
  hgrep_str tag;
  hgrep_str insides;
  hgrep_str_pair *attrib;
  unsigned int child_count;
  unsigned short attribl;
  unsigned short lvl;
} hgrep_node;

struct hgrep_range {
  unsigned int v[3];
  unsigned char flags;
};

struct hgrep_pattrib {
  regex_t r[2];
  struct hgrep_range *position_r;
  size_t position_rl;
  unsigned char flags;
};

typedef struct {
  regex_t tag;
  regex_t insides;
  hgrep_str format;
  struct hgrep_pattrib *attrib;
  size_t attribl;
  struct hgrep_range *position_r;
  struct hgrep_range *attribute_r;
  struct hgrep_range *size_r;
  struct hgrep_range *child_count_r;
  size_t position_rl;
  size_t attribute_rl;
  size_t size_rl;
  size_t child_count_rl;
  unsigned char flags;
} hgrep_pattern;

typedef struct {
  void *p;
  unsigned char istable;
} hgrep_epattern;

#pragma pack(push, 1)
typedef struct {
  //hgrep_pattern *pattern;
  size_t id;
  ushort lvl;
} hgrep_compressed;
#pragma pack(pop)

typedef struct {
  char *data;
  FILE *output;
  hgrep_node *nodes;
  hgrep_pattern *pattern;
  void *attrib_buffer;
  size_t size;
  size_t nodesl;
  unsigned char flags;
} hgrep;

hgrep hgrep_init(char *ptr, const size_t size, FILE *output);
void hgrep_fmatch(char *ptr, const size_t size, FILE *output, hgrep_pattern *pattern);
void hgrep_pcomp(char *pattern, size_t size, hgrep_pattern *p, const unsigned char flags);
void hgrep_epcomp(char *src, size_t *pos, size_t s, const unsigned char flags, hgrep_epattern **epatterns, size_t *epatternsl);
int hgrep_match(const hgrep_node *hgn, const hgrep_pattern *p);
void hgrep_ematch(hgrep *hg, hgrep_epattern *patterns, const size_t size, hgrep_compressed *source, size_t sourcel, hgrep_compressed *dest, size_t destl);
void hgrep_printf(FILE *outfile, const char *format, const size_t formatl, const hgrep_node *hgn, const char *reference);
void hgrep_print(FILE *outfile, const hgrep_node *hg);
void hgrep_pfree(hgrep_pattern *p);
void hgrep_epattern_free(hgrep_epattern *epatterns, const size_t epatternsl);
void hgrep_free(hgrep *hg);

#endif
