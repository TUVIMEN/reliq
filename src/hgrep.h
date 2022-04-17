#ifndef HGREP_H
#define HGREP_H

#define HGREP_INVERT 0x1
#define HGREP_EREGEX 0x2
#define HGREP_ICASE 0x4
#define HGREP_SAVE 0x8

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
  size_t attribl;
} hgrep_node;

struct hgrep_pattrib {
  regex_t r[2];
  unsigned int px; //position
  unsigned int py;
  unsigned char flags;
};

typedef struct {
  regex_t r;
  regex_t in;
  struct hgrep_pattrib *attrib;
  size_t attribl;
  unsigned int px; //position
  unsigned int py;
  unsigned int ax; //atribute
  unsigned int ay;
  unsigned int sx; //size
  unsigned int sy;
  char *format;
  unsigned char flags;
} hgrep_pattern;

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

void hgrep_init(hgrep *hg, char *ptr, const size_t size, FILE *output, hgrep_pattern *pattern, const unsigned char flags);
void hgrep_pcomp(char *pattern, size_t size, hgrep_pattern *p, const unsigned char flags);
int hgrep_match(const hgrep_node *hgn, const hgrep_pattern *p, const unsigned short lvl);
void hgrep_printf(FILE *outfile, const char *format, const hgrep_node *hgn, const unsigned short lvl, const char *reference);
void hgrep_print(FILE *outfile, const hgrep_node *hg);
void hgrep_pfree(hgrep_pattern *p);
void hgrep_free(hgrep *hg);

#endif
