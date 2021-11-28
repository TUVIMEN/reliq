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

#include "main.h"
#include "arg.h"

#define while_is(w,x,y,z) while ((y) < (z) && (w)((x)[(y)])) {(y)++;} \
  if ((y) > (z)) return
#define toggleflag(x,y,z) (x) ? ((y) |= (z)) : ((y) &= ~(z))

char *argv0;
flexarr *patterns = NULL;
char *fpattern = NULL;
char last_pattern = 0;

unsigned int settings = 0;
int regexflags = REG_NEWLINE|REG_NOSUB;
int nftwflags = FTW_ACTIONRETVAL|FTW_PHYS;
FILE* outfile;

char *without_end_s[] = { //tags that doesn't end with </tag>
  "area","base","br","col","embed","hr",
  "img","input","link","meta","param",
  "source","track","wbr","command",
  "keygen","menuitem",NULL
};

char *script_s[] = { //tags which insides should be ommited
  "script","style",NULL
};

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
    while (s-*i > 2 && strncmp(f+*i,"-->",3) != 0)
      (*i)++;
    *i += 3;
  } else {
    while (*i < s && f[*i] != '>')
      (*i)++;
  }
}

static uchar
matchxy(ushort x, ushort y, const ushort z, const size_t last)
{
  if (x == USHRT_MAX)
      x = last;
  if (y == USHRT_MAX)
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
  str_pair *av = (str_pair*)a->v;
  const struct pel *attrib = (struct pel*)p->attrib->v;

  if (!matchxy(p->px,p->py,lvl,-1)
    || !matchxy(p->ax,p->ay,p->attrib->size,t->attrib->size))
    return 0;

  regmatch_t pmatch;

  pmatch.rm_so = 0;
  pmatch.rm_eo = (int)t->tag.s;
  if (regexec(&p->r,t->tag.b,1,&pmatch,REG_STARTEND) != 0)
    return 0;

  for (size_t i = 0; i < p->attrib->size; i++) {
    uchar found = 0;
    for (size_t j = 0; j < t->attrib->size; j++) {
      if (!matchxy(attrib[i].px,attrib[i].py,j,t->attrib->size-1))
        continue;

      pmatch.rm_eo = (int)av[j].f.s;
      if (regexec(&attrib[i].r[0],av[j].f.b,1,&pmatch,REG_STARTEND) != 0)
        continue;

      if (attrib[i].flags&2) {
        pmatch.rm_eo = (int)av[j].s.s;
        if (regexec(&attrib[i].r[1],av[j].s.b,1,&pmatch,REG_STARTEND) != 0)
          continue;
      }
      
      found = 1;
    }

    if ((attrib[i].flags&1) ? !found : found)
      return 0;
  }

  return 1;
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
  while_is(isdigit,src,*pos,size) -1;
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
    flexarr *a = t->attrib;
    str_pair *av = (str_pair*)a->v;
    for (size_t j = 0; j < a->size; j++) {
      fputc(' ',outfile);
      fwrite(av[j].f.b,av[j].f.s,1,outfile);
      fputs("=\"",outfile);
      fwrite(av[j].s.b,av[j].s.s,1,outfile);
      fputc('"',outfile);
    }
    fprintf(outfile," - %lu\n",t->all.s);
  } else if (p && (settings&F_REVERSE) == 0 ? ismatching(t,p,lvl) : !ismatching(t,p,lvl)) {
    if (last_pattern && fpattern) {
        handle_printf(t,lvl);
    } else {
      fwrite(t->all.b,t->all.s,1,outfile);
      fputc('\n',outfile);
    }
  }
}

static void
handle_struct(char *f, size_t *i, const size_t s, const struct pat *p, const ushort lvl)
{
  struct html_s t;
  memset(&t,0,sizeof(struct html_s));
  t.attrib = flexarr_init(sizeof(str_pair),PATTERN_SIZE_INC);
  flexarr *a = t.attrib;
  str_pair *ac;

  t.all.b = f+*i;
  (*i)++;
  while_is(isspace,f,*i,s);
  if (f[*i] == '!') {
    handle_comment(f,i,s);
    return;
  }
  t.tag.b = f+*i;
  while (*i < s && !isspace(f[*i]) && f[*i] != '>')
    (*i)++;
  t.tag.s = (f+*i)-t.tag.b;

  for (; *i < s && f[*i] != '>'; (*i)++) {
    if (f[*i] == '/')
      return;
    
    while_is(isspace,f,*i,s);
    if (!isalpha(f[*i]))
      continue;
    
    ac = (str_pair*)flexarr_inc(a);
    ac->f.b = f+*i;
    while (f[*i] && !isspace(f[*i]) && f[*i] != '=')
        (*i)++;
    ac->f.s = (f+*i)-ac->f.b;
    if (f[*i] != '=') {
      a->size--;
      continue;
    }
    (*i)++;
    while_is(isspace,f,*i,s);
    if (f[*i] != '"' && f[*i] != '\'') {
      a->size--;
      continue;
    }
    char ctc = '\'';
    if (f[*i] == '"')
        ctc = '"';
    (*i)++;
    ac->s.b = f+*i;
    while (*i < s && f[*i] != ctc)
      (*i)++;
    ac->s.s = (f+*i)-ac->s.b;
  }

  for (int j = 0; without_end_s[j]; j++) {
    if (strlen(without_end_s[j]) == t.tag.s
      && strncmp(t.tag.b,without_end_s[j],t.tag.s) == 0) {
      t.all.s = f+*i-t.all.b+1;
      goto END;
    }
  }

  uchar script = 0;
  for (int j = 0; script_s[j]; j++) {
    if (strlen(script_s[j]) == t.tag.s
      && strncmp(t.tag.b,script_s[j],t.tag.s) == 0) {
      script = 1;
      break;
    }
  }

  (*i)++;
  t.insides.b = f+*i;
  t.insides.s = *i;
  while (*i < s) {
    if (f[*i] == '<') {
      if (f[*i+1] == '/') {
        if (strncmp(t.tag.b,f+*i+2,t.tag.s) == 0) {
          t.insides.s = *i-t.insides.s;
          *i += 2+t.tag.s;
          while (*i < s && f[*i] != '>')
            (*i)++;
          t.all.s = (f+*i+1)-t.all.b;
          goto END;
        }
      } else if (!script) {
        if (f[*i+1] == '!') {
          (*i)++;
          handle_comment(f,i,s);
          continue;
        } else {
          handle_struct(f,i,s,p,lvl+1);
        }
      }
    }
    (*i)++;
  }

  END: ;
  print_struct(&t,p,lvl);
  flexarr_free(t.attrib);
}

static void
handle_sbrackets(const char *src, size_t *pos, const size_t size, ushort *x, ushort *y)
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
  *x = (ushort)r;

  while_is(isspace,src,*pos,size);

  if (src[*pos] != '-')
    goto END;

  NEXT_VALUE: ;
  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == '$' || src[*pos] == ']') {
    (*pos)++;
    *y = -1;
    goto END;
  }

  r = handle_number(src,pos,size);
  if (r == -1)
    return;
  *y = (ushort)r;

  END: ;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == ']')
    (*pos)++;
  while_is(isspace,src,*pos,size);
}

static void
parse_pattern(char *pattern, size_t size, struct pat *p)
{
  struct pel *pe;
  p->attrib = flexarr_init(sizeof(struct pel),PATTERN_SIZE_INC);
  p->py = -1;
  p->px = 0;
  p->ay = -1;
  p->ax = 0;
  char tmp[PATTERN_SIZE];
  tmp[0] = '^';

  size_t pos = 0,t;
  while_is(isspace,pattern,pos,size);
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
    if (pattern[i] == '/') {
      i++;
      while_is(isspace,pattern,pos,size);
      if (pattern[i] == '[')
        handle_sbrackets(pattern,&i,size,&p->ax,&p->ay);
      if (pattern[i] == '[')
        handle_sbrackets(pattern,&i,size,&p->px,&p->py);
      return;
    }
    if (pattern[i] == '+' || pattern[i] == '-') {
      uchar tf = pattern[i++]; 
      ushort x=0,y=-1;
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
      toggleflag(tf == '+',pe->flags,1);

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
        pe->flags |= 2;
      } else
        pe->flags &= ~2;
      i++;
    }
  }
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
    if (f[i] == '<')
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
      if (((struct pel*)flv->attrib->v)[j].flags&2)
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
    case 'v': settings |= F_REVERSE; break;
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
      j = 1;
      brk_ = 0;
      while (__argv[i][j] && !brk_) {
        if (__argv[i][j] == 'o' || __argv[i][j] == 'f')
          brk_ = 1;
        if ((brk_ && !__argv[i][j+1]) || strcmp(__argv[i]+j,"printf") == 0) {
          i++;
          break;
        }
        j++;
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
