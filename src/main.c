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

#include "main.h"
#include "arg.h"
#include "ctype.h"

#define while_is(w,x,y,z) while ((y) < (z) && w((x)[(y)])) {(y)++;}
#define LENGHT(x) (sizeof(x)/(sizeof(*x)))

char *argv0;
flexarr *patterns = NULL;
char *fpattern = NULL;
char last_pattern = 0;

unsigned int settings = 0;
int regexflags = REG_NEWLINE|REG_NOSUB;
int nftwflags = FTW_ACTIONRETVAL|FTW_PHYS;
char *reference;
FILE* outfile;

str8 selfclosing_s[] = { //tags that doesn't end with </tag>
  {"br",2},{"hr",2},{"img",3},{"input",5},{"col",3},{"embed",5},
  {"area",4},{"base",4},{"link",4},{"meta",4},{"param",5},
  {"source",6},{"track",5},{"wbr",3},{"command",7},
  {"keygen",6},{"menuitem",8}
};

str8 script_s[] = { //tags which insides should be ommited
  {"script",6},{"style",5}
};

#ifdef AUTOCLOSING
str8 autoclosing_s[] = { //tags that doesn't need to be closed
  {"p",1},{"tr",2},{"td",2},{"th",2},{"tbody",5},{"tfoot",5},
  {"thead",5},{"rt",2},{"rp",2},{"caption",7},
  {"colgroup",8},{"option",6},{"optgroup",8}
};
#endif

static int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

static void
die(const char *s, ...)
{
  va_list ap;
  va_start(ap,s);
  vfprintf(stderr,s,ap);
  va_end(ap);
  fputc('\n',stderr);
  exit(1);
}

static void
usage()
{
  die("Usage: %s [OPTION]... PATTERNS [FILE]...\n"\
      "Search for PATTERNS in each html FILE.\n"\
      "Example: %s -i 'div +id; a +href=\".*\\.org\"' index.html\n\n"\
      "Options:\n"\
      "  -i\t\t\tignore case distinctions in patterns and data\n"\
      "  -v\t\t\tselect non-matching blocks\n"\
      "  -l\t\t\tlist structure of FILE\n"\
      "  -o FILE\t\tchange output to a FILE instead of stdout\n"\
      "  -f FILE\t\tobtain PATTERNS from FILE\n"\
      "  -printf FORMAT\tprint output according to FORMAT\n"\
      "  -E\t\t\tuse extended regular expressions\n"\
      "  -H\t\t\tfollow symlinks\n"\
      "  -r\t\t\tread all files under each directory, recursively\n"\
      "  -R\t\t\tlikewise but follow all symlinks\n"\
      "  -h\t\t\tshow help\n"\
      "  -V\t\t\tshow version\n\n"\
      "When FILE isn't specified, FILE will become standard input.",argv0,argv0,argv0);
}

static void
handle_comment(char *f, size_t *i, size_t s)
{
  (*i)++;
  if (f[*i] == '-' && f[*i+1] == '-') {
    *i += 2;
    while (s-*i > 2 && memcmp(f+*i,"-->",3) != 0)
      (*i)++;
    *i += 3;
  } else {
    while (*i < s && f[*i] != '>')
      (*i)++;
  }
}

static uchar
matchxy(uint x, uint y, const uint z, const size_t last)
{
  if (x == UINT_MAX)
      x = last;
  if (y == UINT_MAX)
      y = last;
  if (y == 0) {
    if (z == x)
      return 1;
  } else if (z <= y && z >= x)
    return 1;

  return 0;
}

static uchar
ismatching(const struct html_s *t, const struct pat *p, const ushort lvl)
{
  flexarr *a = t->attrib;
  str_pair *av;
  const struct pel *attrib = (struct pel*)p->attrib->v;
  uchar rev = ((p->flags&P_INVERT) == P_INVERT);
  regmatch_t pmatch;

  if (!matchxy(p->px,p->py,lvl,-1)
    ||
  #ifdef PHPTAGS
     (a &&
  #endif
     !matchxy(p->ax,p->ay,t->attrib->size,-1)
  #ifdef PHPTAGS
     )
  #endif
    || !matchxy(p->sx,p->sy,t->insides.s,-1))
    return 0^rev;

  pmatch.rm_so = 0;
  pmatch.rm_eo = (int)t->tag.s;
  if (regexec(&p->r,t->tag.b,1,&pmatch,REG_STARTEND) != 0)
    return 0^rev;

  #ifdef PHPTAGS
  if (a) {
  #endif
  av = (str_pair*)a->v;
  for (size_t i = 0; i < p->attrib->size; i++) {
    uchar found = 0;
    for (size_t j = 0; j < t->attrib->size; j++) {
      if (!matchxy(attrib[i].px,attrib[i].py,j,t->attrib->size-1))
        continue;

      pmatch.rm_eo = (int)av[j].f.s;
      if (regexec(&attrib[i].r[0],av[j].f.b,1,&pmatch,REG_STARTEND) != 0)
        continue;

      if (attrib[i].flags&A_VAL_MATTERS) {
        pmatch.rm_eo = (int)av[j].s.s;
        if (regexec(&attrib[i].r[1],av[j].s.b,1,&pmatch,REG_STARTEND) != 0)
          continue;
      }
      
      found = 1;
    }

    if (((attrib[i].flags&A_INVERT)==A_INVERT)^found)
      return 0^rev;
  }
  #ifdef PHPTAGS
  }
  #endif

  if (p->flags&P_MATCH_INSIDES) {
    pmatch.rm_so = 0;
    pmatch.rm_eo = (int)t->insides.s;
    if ((regexec(&p->in,t->insides.b,1,&pmatch,REG_STARTEND) != 0)^((p->flags&P_INVERT_INSIDES)==P_INVERT_INSIDES))
      return 0^rev;
  }

  return 1^rev;
}

static char *
delchar(char *src, const size_t pos, size_t *size)
{
  size_t s = *size-1;
  for (size_t i = pos; i < s; i++)
    src[i] = src[i+1];
  src[s] = 0;
  *size = s;
  return src;
}

int handle_number(const char *src, size_t *pos, const size_t size)
{
  size_t t = *pos, s;
  int ret = atoi(src+t);
  while_is(isdigit,src,*pos,size);
  s = *pos-t;
  if (s == 0)
    return -1;
  return ret;
}

static char
special_character(const char c)
{
  char r;
  switch (c) {
    case '0': r = '\0'; break;
    case 'a': r = '\a'; break;
    case 'b': r = '\b'; break;
    case 't': r = '\t'; break;
    case 'n': r = '\n'; break;
    case 'v': r = '\v'; break;
    case 'f': r = '\f'; break;
    case 'r': r = '\r'; break;
    default: r = c;
  }
  return r;
}

static
void
print_attribs(const struct html_s *t)
{
  #ifdef PHPTAGS
  if (!t->attrib)
    return;
  #endif
  flexarr *a = t->attrib;
  str_pair *av = (str_pair*)a->v;
  for (size_t j = 0; j < a->size; j++) {
    fputc(' ',outfile);
    fwrite(av[j].f.b,av[j].f.s,1,outfile);
    fputs("=\"",outfile);
    fwrite(av[j].s.b,av[j].s.s,1,outfile);
    fputc('"',outfile);
  }
}

static void
handle_printf(const struct html_s *t, const ushort lvl)
{
  size_t i = 0, s = strlen(fpattern);
  while (i < s) {
    if (fpattern[i] == '\\') {
      fputc(special_character(fpattern[++i]),outfile);
      i++;
      continue;
    }
    if (fpattern[i] == '%') {
      if (++i >= s)
        break;
      char cont[PATTERN_SIZE];
      size_t contl = 0;
      int num = -1;
      if (isdigit(fpattern[i])) {
        num = handle_number(fpattern,&i,s);
      } else if (fpattern[i] == '(') {
        for (i++; i < s && fpattern[i] != ')' && contl < PATTERN_SIZE-1; i++) {
          if (fpattern[i] == '\\') {
            if (i >= s)
              break;
            cont[contl++] = fpattern[i++];
            continue;
          }
          cont[contl++] = fpattern[i];
        }
        cont[contl] = 0;
        if (fpattern[i] == ')')
          i++;
      }

      switch (fpattern[i++]) {
        case '%': fputc('%',outfile); break;
        case 'A': fwrite(t->all.b,t->all.s,1,outfile); break;
        case 't': fwrite(t->tag.b,t->tag.s,1,outfile); break;
        case 'i': fwrite(t->insides.b,t->insides.s,1,outfile); break;
        case 'l': fprintf(outfile,"%u",lvl); break;
        case 's': fprintf(outfile,"%lu",t->all.s); break;
        case 'p': fprintf(outfile,"%lu",t->all.b-reference); break;
        case 'I': print_attribs(t); break;
        case 'a': {
          flexarr *a = t->attrib;
          str_pair *av = (str_pair*)a->v;
          if (num != -1) {
            if ((size_t)num < a->size)
              fwrite(av[num].s.b,av[num].s.s,1,outfile);
          } else if (contl != 0) {
            for (size_t i = 0; i < a->size; i++)
              if (av[i].f.s == contl && memcmp(av[i].f.b,cont,contl) == 0)
                fwrite(av[i].s.b,av[i].s.s,1,outfile);
          } else for (size_t i = 0; i < a->size; i++) {
            fwrite(av[i].s.b,av[i].s.s,1,outfile);
            fputc('"',outfile);
          }
        }
        break;
      }
      continue;
    }
    fputc(fpattern[i++],outfile);
  }
}

static void
print_struct(const struct html_s *t, const struct pat *p, const ushort lvl)
{
  if (settings&F_LIST) {
    fwrite(t->tag.b,t->tag.s,1,outfile);
    print_attribs(t);
    fprintf(outfile," - %lu/%lu\n",t->all.s,t->all.b-reference);
  } else if (p && (((settings&F_INVERT)==F_INVERT)^ismatching(t,p,lvl))) {
    if (last_pattern && fpattern) {
        handle_printf(t,lvl);
    } else {
      fwrite(t->all.b,t->all.s,1,outfile);
      fputc('\n',outfile);
    }
  }
}

static void
handle_name(char *f, size_t *i, const size_t s, str *tag)
{
    tag->b = f+*i;
    while (*i < s && (isalnum(f[*i]) || f[*i] == '-'))
      (*i)++;
    tag->s = (f+*i)-tag->b;
}

#ifdef PHPTAGS
static void
handle_phptag(char *f, size_t *i, const size_t s, struct html_s *t)
{
  (*i)++;
  while_is(isspace,f,*i,s);
  handle_name(f,i,s,&t->tag);

  char *ending;
  for (; *i < s; (*i)++) {
    if (f[*i] == '\\') {
      *i += 2;
      continue;
    }
    if (f[*i] == '?' && f[*i+1] == '>') {
      (*i)++;
      break;
    }
    if (f[*i] == '"') {
      (*i)++;
      size_t n,jumpv;
      while (1) {
        ending = memchr(f+*i,'"',s-*i);
        if (!ending) {
          *i = s;
          return;
        }
        jumpv = ending-f-*i;
        if (!jumpv) {
          (*i)++;
          break;
        }
        *i = ending-f;
        n = 1;
        while (jumpv && f[*i-n] == '\\')
          n++;
        if ((n-1)&1)
          continue;
        break;
      }
    } else if (f[*i] == '\'') {
      (*i)++;
      ending = memchr(f+*i,'\'',s-*i);
      if (ending) {
        *i = ending-f;
      } else {
        *i = s;
        return;
      }
    }
  }
  t->all.s = (f+*i)-t->all.b+1;
}
#endif

static void
handle_attrib(char *f, size_t *i, const size_t s, flexarr *attribs)
{
  str_pair *ac = (str_pair*)flexarr_inc(attribs);
  handle_name(f,i,s,&ac->f);
  if (f[*i] != '=') {
    ac->s.b = NULL;
    ac->s.s = 0;
    return;
  }
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '>') {
    attribs->size--;
    (*i)++;
    return;
  }
  if (f[*i] == '\'' || f[*i] == '"') {
    char delim = f[(*i)++];
    ac->s.b = f+*i;
    char *ending = memchr(f+*i,delim,s-*i);
    if (!ending) {
      *i = s;
      return;
    }
    *i = ending-f;
    ac->s.s = (f+*i)-ac->s.b;
    if (f[*i] == delim)
      (*i)++;
  } else {
    ac->s.b = f+*i;
    while (*i < s && !isspace(f[*i]) && f[*i] != '>')
      (*i)++;
     ac->s.s = (f+*i)-ac->s.b;
  }
}

static void
handle_struct(char *f, size_t *i, const size_t s, const struct pat *p, const ushort lvl)
{
  struct html_s t;
  memset(&t,0,sizeof(struct html_s));
  flexarr *a;

  t.all.b = f+*i;
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '!') {
    handle_comment(f,i,s);
    return;
  }

  #ifdef PHPTAGS
  if (f[*i] == '?') {
    handle_phptag(f,i,s,&t);
    goto END;
  }
  #endif

  a = t.attrib = flexarr_init(sizeof(str_pair),PATTERN_SIZE_INC);

  handle_name(f,i,s,&t.tag);
  for (; *i < s && f[*i] != '>';) {
    while_is(isspace,f,*i,s);
    if (f[*i] == '/') {
      char *r = memchr(f+*i,'>',s-*i);
      if (r != NULL)
        t.all.s = r-t.all.b+1;
      goto END;
    }
    
    if (!isalpha(f[*i])) {
      if (f[*i] == '>')
          break;
      (*i)++;
      continue;
    }
    
    handle_attrib(f,i,s,a);
  }

  #define strcomp(x,y) (x.s == y.s && memcmp(y.b,x.b,y.s) == 0)
  #define search_array(x,y) for (uint _j = 0; _j < (uint)LENGHT(x); _j++) \
    if (strcomp(x[_j],y))
  
  search_array(selfclosing_s,t.tag) {
    t.all.s = f+*i-t.all.b+1;
    goto END;
  }

  uchar script = 0;
  search_array(script_s,t.tag) {
    script = 1;
    break;
  }
  
  #ifdef AUTOCLOSING
  uchar autoclosing = 0;
  search_array(autoclosing_s,t.tag) {
    autoclosing = 1;
    break;
  }
  #endif

  (*i)++;
  t.insides.b = f+*i;
  t.insides.s = *i;
  while (*i < s) {
    if (f[*i] == '<') {
      if (f[*i+1] == '/') {
        if (memcmp(t.tag.b,f+*i+2,t.tag.s) == 0) {
          t.insides.s = *i-t.insides.s;
          *i += 2+t.tag.s;
          char *ending = memchr(f+*i,'>',s-*i);
          if (!ending) {
            *i = s;
            return;
          }
          *i = ending-f;
          t.all.s = (f+*i+1)-t.all.b;
          goto END;
        }
      } else if (!script) {
        if (f[*i+1] == '!') {
          (*i)++;
          handle_comment(f,i,s);
          continue;
        } else {
          #ifdef AUTOCLOSING
          if (autoclosing) {
            size_t ti = *i;
            str name;

            ti++;
            while_is(isspace,f,ti,s);
            handle_name(f,&ti,s,&name);

            if (strcomp(t.tag,name)) {
              goto END;
            }
          }
          #endif
          handle_struct(f,i,s,p,lvl+1);
        }
      }
    }
    (*i)++;
  }

  END: ;

  if (*i >= s) {
    t.all.s = s-(t.all.b-f)-1;
  } else if (!t.all.s) {
    t.all.s = f+*i-t.all.b;
  }

  print_struct(&t,p,lvl);
  #ifdef PHPTAGS
  if (t.attrib)
  #endif
    flexarr_free(t.attrib);
}

static void
handle_sbrackets(const char *src, size_t *pos, const size_t size, uint *x, uint *y)
{
  *x = 0;
  *y = 0;
  int r;

  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == '-')
    goto NEXT_VALUE;
  if (src[*pos] == '$') {
    *x = -1;
    (*pos)++;
    goto END;
  }

  r = handle_number(src,pos,size);
  if (r == -1)
    return;
  *x = (uint)r;

  while_is(isspace,src,*pos,size);

  if (src[*pos] != '-')
    goto END;

  NEXT_VALUE: ;
  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == ']') {
    (*pos)++;
    *y = -1;
    goto END;
  }
  if (src[*pos] == '$') {
    (*pos)++;
    *y = -1;
    while_is(isspace,src,*pos,size);
    while (src[*pos] != ']' && *pos < size)
        (*pos)++;
    if (src[*pos] == ']' && *pos < size)
        (*pos)++;
    goto END;
  }

  r = handle_number(src,pos,size);
  if (r == -1)
    return;
  *y = (uint)r;

  END: ;
  while_is(isspace,src,*pos,size);
}

static void
parse_pattern(char *pattern, size_t size, struct pat *p)
{
  struct pel *pe;
  ushort attribcount = 0;
  p->attrib = flexarr_init(sizeof(struct pel),PATTERN_SIZE_INC);
  p->py = -1;
  p->px = 0;
  p->ay = -1;
  p->ax = 0;
  p->sy = -1;
  p->sx = 0;
  char tmp[PATTERN_SIZE];
  tmp[0] = '^';

  size_t pos = 0,t;
  while_is(isspace,pattern,pos,size);
  if (pattern[pos] == '!') {
      p->flags |= P_INVERT;
      pos++;
  }
  t = pos;
  while (pos < size && pos-t < PATTERN_SIZE-2 && !isspace(pattern[pos]))
    pos++;
  memcpy(tmp+1,pattern+t,pos-t);
  tmp[pos+1-t] = '$';
  tmp[pos+2-t] = 0;
  if (regcomp(&p->r,tmp,regexflags) != 0) {
    regfree(&p->r);
    return;
  }

  for (size_t i = pos; i < size; i++) {
    while_is(isspace,pattern,pos,size);
    if (pattern[i] == '/' || pattern[i] == '#' || pattern[i] == ',') {
      char delim = pattern[i++];
      while_is(isspace,pattern,i,size);
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&p->ax,&p->ay);
        while_is(isspace,pattern,i,size);
      }
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&p->px,&p->py);
        while_is(isspace,pattern,i,size);
      }
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&p->sx,&p->sy);
        while_is(isspace,pattern,i,size);
      }
      if (i < size && pattern[i] == '!') {
        p->flags |= P_INVERT_INSIDES;
        i++;
        while_is(isspace,pattern,i,size);
      }
      if (pattern[i] == delim) {
        i++;
        p->flags |= P_MATCH_INSIDES;
        size_t j = 0;
        for (; i < size && j < PATTERN_SIZE; i++) {
          if (pattern[i] == '\\' && i+1 < size && pattern[i+1] == delim)
            i++;
          if (pattern[i] == delim)
            break;
          tmp[j++] = pattern[i];
        }
        tmp[j] = 0;
        if (regcomp(&p->in,tmp,regexflags) != 0) {
          regfree(&p->in);
          return;
        }
      }
      if (p->ax < attribcount)
        p->ax = attribcount;
      return;
    }
    if (pattern[i] == '+' || pattern[i] == '-') {
      uchar tf = pattern[i++]; 
      uint x=0,y=-1;
      while_is(isspace,pattern,i,size);
      if (pattern[i] == '[')
        handle_sbrackets(pattern,&i,size,&x,&y);
      t = i;
      while (i < size && i < PATTERN_SIZE-2 && !isspace(pattern[i]) && pattern[i] != '=')
        i++;
      memcpy(tmp+1,pattern+t,i-t);
      tmp[i+1-t] = '$';
      tmp[i+2-t] = 0;
      pe = (struct pel*)flexarr_inc(p->attrib);
      pe->flags = 0;
      if (tf == '+') {
        attribcount++;
        pe->flags |= A_INVERT;
      } else {
        pe->flags &= ~A_INVERT;
      }

      if (regcomp(&pe->r[0],tmp,regexflags) != 0) {
        regfree(&pe->r[0]);
        return;
      }
      pe->px = x;
      pe->py = y;

      if (pattern[i] == '=' && pattern[i+1] == '"') {
        i += 2;
        t = i;
        while (i < size && i < PATTERN_SIZE-2 && pattern[i] != '"') {
          if (pattern[i] == '\\')
  	    delchar(pattern,i,&size);
          i++;
        }
        memcpy(tmp+1,pattern+t,i-t);
        tmp[i+1-t] = '$';
        tmp[i+2-t] = 0;
        if (regcomp(&pe->r[1],tmp,regexflags) != 0) {
          regfree(&pe->r[1]);
          return;
        }
        pe->flags |= A_VAL_MATTERS;
        i++;
      } else
        pe->flags &= ~A_VAL_MATTERS;
    }
  }
  if (p->ax < attribcount)
    p->ax = attribcount;
}

static flexarr *
split_patterns(char *src, size_t s)
{
  if (s == 0)
    return NULL;
  flexarr *ret = flexarr_init(sizeof(struct pat),PATTERN_SIZE_INC);
  for (size_t i = 0, j; i < s; i++) {
    j = i;

    while (i < s) {
      if (src[i] == '\\' && src[i+1] == ';')
	delchar(src,i,&s);
      if (src[i] == ';')
        break;
      i++;
    }

    parse_pattern(src+j,i-j,(struct pat*)flexarr_inc(ret));
    while (i < s && src[i] == ';')
      i++;
    i--;
  }
  flexarr_clearb(ret);
  return ret;
}

static void
go_through(char *f, struct pat *p, const size_t s)
{
  for (size_t i = 0; i < s; i++) {
    while (f[i] == '<' && i < s)
      handle_struct(f,&i,s,p,0);
  }
  fflush(outfile); 
}

static void
pattern_free(flexarr *f)
{
  if (f == NULL)
      return;
  for (size_t i = 0; i < f->size; i++) {
    struct pat *flv = &((struct pat*)f->v)[i];
    regfree(&flv->r);
    for (size_t j = 0; j < flv->attrib->size; j++) {
      regfree(&((struct pel*)flv->attrib->v)[j].r[0]);
      if (((struct pel*)flv->attrib->v)[j].flags&A_VAL_MATTERS)
        regfree(&((struct pel*)flv->attrib->v)[j].r[1]);
    }
    flexarr_free(flv->attrib);
  }
  flexarr_free(f);
}

static void
analyze(char *f, size_t s, uchar inpipe)
{
  if (f == NULL || s == 0)
    return;
  if (patterns == NULL) {
    reference = f;
    go_through(f,NULL,s);
    return;
  }

  FILE *t = outfile;
  char **ptr = malloc(patterns->size<<3);
  size_t *fsize = malloc(patterns->size<<3);
  
  for (size_t i = 0; i < patterns->size; i++) {
    if (i == patterns->size-1) {
      outfile = t;
      last_pattern = 1;
    } else {
      outfile = open_memstream(&ptr[i],&fsize[i]);
    }

    struct pat *flv = &((struct pat*)patterns->v)[i];
    reference = f;
    go_through(f,flv,s);

    if (!inpipe && i == 0)
      munmap(f,s);
    else
      free(f);
    if (i != patterns->size-1)
      fclose(outfile);

    f = ptr[i];
    s = fsize[i];
  }
  free(ptr);
  free(fsize);
  fflush(outfile);
}

static void
pipe_to_str(int fd, char **file, size_t *size)
{
  *file = NULL;
  register size_t readbytes = 0;
  *size = 0;

  do {
    *size += readbytes;
    *file = realloc(*file,*size+BUFF_INC_VALUE);
    readbytes = read(fd,*file+*size,BUFF_INC_VALUE);
  } while (readbytes > 0);
  *file = realloc(*file,*size);
}

void
handle_file(const char *f)
{
  int fd;
  struct stat st;
  char *file;
  last_pattern = 0;

  if (f == NULL) {
    size_t size;
    pipe_to_str(0,&file,&size);
    analyze(file,size,1);
    return;
  }

  if ((fd = open(f,O_RDONLY)) == -1) {
    warn("%s",f);
    return;
  }

  if (fstat(fd,&st) == -1) {
    warn("%s",f);
    close(fd);
    return;
  }

  if ((st.st_mode&S_IFMT) == S_IFDIR) {
    close(fd);
    if (settings&F_RECURSIVE) {
      if (nftw(f,nftw_func,16,nftwflags) == -1)
        warn("%s",f);
    } else
      fprintf(stderr,"%s: -R not specified: omitting directory '%s'\n",argv0,f);
    return;
  }

  file = mmap(NULL,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
  if (file == NULL) {
    warn("%s",f);
    close(fd);
  } else {
    close(fd);
    analyze(file,st.st_size,0);
  }
}

int
nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    handle_file(fpath);

  return FTW_CONTINUE;
}

int
main(int argc, char **argv)
{
  argv0 = argv[0];
  if (argc < 2)
    usage();
  char **__argv = argv;
  outfile = stdout;

  ARGBEGIN {
    case 'v': settings |= F_INVERT; break;
    case 'l': settings |= F_LIST; break;
    case 'E': regexflags |= REG_EXTENDED; break;
    case 'i': regexflags |= REG_ICASE; break;
    case 'o': {
      char *fname;
      brk_= 1;
      if (argv[0][i_+1]) {
        fname = argv[0]+i_+1;
      } else {
        if (argc == 1)
            die("%s: option requires an argument -- 'o'",argv0);
        argc--;
        fname = *(++argv);
      }
      outfile = fopen(fname,"w");
      if (outfile == NULL)
        err(1,"%s",fname);
    }
    break;
    case 'p': if (strcmp(argv[0]+i_,"printf") == 0) {
      if (argc == 1)
        die("%s: option requires an argument -- 'printf'",argv0);
      brk_ = 1;
      argc--;
      fpattern = *(++argv);
    }
    break;
    case 'f': {
      char *fname;
      brk_= 1;
      if (argv[0][i_+1]) {
        fname = argv[0]+i_+1;
      } else {
        if (argc == 1)
          die("%s: option requires an argument -- 'f'",argv0);
        argc--;
        fname = *(++argv);
      }
      int fd = open(fname,O_RDONLY);
      if (fd == -1)
        err(1,"%s",fname);
      size_t s;
      char *p;
      pipe_to_str(fd,&p,&s);
      close(fd);
      patterns = split_patterns(p,s);
      free(p);
    }
    break;
    case 'H': nftwflags &= ~FTW_PHYS; break;
    case 'r': settings |= F_RECURSIVE; break;
    case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
    case 'V': die(VERSION); break;
    case 'h':
      usage();
      break;
    default:
      die("%s: invalid option -- '%c'",argv0,argc_);
  } ARGEND;
  
  int g = 0,j,i,brk_;
  for (i = 1;  __argv[i]; i++) {
    if (__argv[i][0] == '-') {
      for (j = 1, brk_ = 0; __argv[i][j] && !brk_; j++) {
        if (__argv[i][j] == 'o' || __argv[i][j] == 'f')
          brk_ = 1;
        if ((brk_ && !__argv[i][j+1]) || strcmp(__argv[i]+j,"printf") == 0) {
          i++;
          break;
        }
      }
      continue;
    }
    if (!(settings&F_LIST) && g == 0 && patterns == NULL) {
      patterns = split_patterns(__argv[i],strlen(__argv[i]));
      continue;
    }

    handle_file(__argv[i]);
    g++;
  }

  if (g == 0)
    handle_file(NULL);
  fclose(outfile);
  pattern_free(patterns);

  return 0;
}
