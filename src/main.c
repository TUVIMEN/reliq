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

#define BUFF_INC_VALUE (1<<15)
#define PATTERN_SIZE (1<<10)
#define PATTERN_SIZE_INC 16

#define while_is(w,x,y,z) while ((y) < (z) && (w)((x)[(y)])) {(y)++;} \
  if ((y) >= (z)) return

char *argv0, *patterns = NULL;

unsigned int settings = 0;
int regexflags = REG_NEWLINE;
int nftwflags = FTW_ACTIONRETVAL|FTW_PHYS;
FILE* outfile;

char *without_end_s[] = {
  "area","base","br","col","embed","hr",
  "img","input","link","meta","param",
  "source","track","wbr","command",
  "keygen","menuitem",NULL
};

char *script_s[] = {
  "script","style",NULL
};

static void die(const char *s, ...)
{
  va_list ap;
  va_start(ap,s);
  vfprintf(stderr,s,ap);
  va_end(ap);
  exit(1);
}

static void usage()
{
  die("Usage: %s [OPTION]... PATTERNS [FILE]...\n"\
      "Search for PATTERN in each html FILE.\n"\
      "Example: %s -i 'div +id; a +href=\".*\\.org\"' index.html\n\n"\
      "Options:\n"\
      "  -i\t\tignore case distinctions in patterns and data\n"\
      "  -v\t\tselect non-matching blocks\n"\
      "  -l\t\tlist structure of FILE\n"\
      "  -o FILE\tchange output to a FILE instead of stdout\n"\
      "  -f FILE\tobtain PATTERNS from FILE\n"\
      "  -E\t\tuse extended regular expressions\n"\
      "  -H\t\tfollow symlinks\n"\
      "  -r\t\tread all files under each directory, recursively\n"\
      "  -R\t\tlikewise but follow all symlinks\n"\
      "  -h\t\tshow help\n"\
      "  -V\t\tshow version\n\n"\
      "When FILE isn't specified, FILE will become standard input.\n",argv0,argv0,argv0);
}

static void handle_comment(char *f, size_t *i, size_t s)
{
  (*i)++;
  if (f[*i] == '-' && f[*i+1] == '-') {
    *i += 2;
    while (s-*i > 2 && strncmp(f+*i,"-->",3) != 0)
      (*i)++;
    *i += 3;
  }
  else while (*i < s && f[*i] != '>')
      (*i)++;
}

static _Bool matchxy(ushort x, ushort y, const ushort z, const size_t last)
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

static _Bool ismatching(struct html_s *t, struct pat *p, const ushort lvl)
{
  flexarr *a = t->a;
  cc *av = (cc*)a->v;
  struct pel *args = (struct pel*)p->args->v;

  if (!matchxy(p->px,p->py,lvl,-1) || !matchxy(p->ax,p->ay,p->args->size,t->a->size))
      return 0;

  char tc = t->n.b[t->n.s];
  t->n.b[t->n.s] = 0;
  if (regexec(&p->r,t->n.b,0,NULL,0) != 0) {
    t->n.b[t->n.s] = tc;
    return 0;
  }
  t->n.b[t->n.s] = tc;

  for (size_t i = 0; i < p->args->size; i++) {
    _Bool found = 0;
    for (size_t j = 0; j < t->a->size; j++) {
      //if (args[i].n ? !av[j].s.s : av[j].s.s)
        //continue;
      if (!matchxy(args[i].px,args[i].py,j,t->a->size-1))
        continue;

      tc = av[j].f.b[av[j].f.s];
      av[j].f.b[av[j].f.s] = 0;
      if (regexec(&args[i].r[0],av[j].f.b,0,NULL,0) != 0) {
        av[j].f.b[av[j].f.s] = tc;
        continue;
      }
      av[j].f.b[av[j].f.s] = tc;

      if (args[i].flags&2) {
        tc = av[j].s.b[av[j].s.s];
        av[j].s.b[av[j].s.s] = 0;
        if (regexec(&args[i].r[1],av[j].s.b,0,NULL,0) != 0) {
          av[j].s.b[av[j].s.s] = tc;
          continue;
        }
        av[j].s.b[av[j].s.s] = tc;
      }
      
      found = 1;
    }

    if ((args[i].flags&1) ? !found : found)
      return 0;
  }

  return 1;
}

static void print_struct(struct html_s *t, struct pat *p, const ushort lvl)
{
  if (settings&F_LIST) {
    fwrite(t->n.b,t->n.s,1,outfile);
    flexarr *a = t->a;
    cc *av = (cc*)a->v;
    for (size_t j = 0; j < a->size; j++) {
      fputc(' ',outfile);
      fwrite(av[j].f.b,av[j].f.s,1,outfile);
      fputs("=\"",outfile);
      fwrite(av[j].s.b,av[j].s.s,1,outfile);
      fputc('"',outfile);
    }
    fprintf(outfile," - %lu\n",t->m.s);
  }
  else if (p && (settings&F_REVERSE) == 0 ? ismatching(t,p,lvl) : !ismatching(t,p,lvl)) {
    fwrite(t->m.b,t->m.s,1,outfile);
    fputc('\n',outfile);
  }
}

static void handle_struct(char *f, size_t *i, size_t s, struct pat *p, const ushort lvl)
{
  struct html_s t;
  memset(&t,0,sizeof(struct html_s));
  t.a = flexarr_init(sizeof(cc),PATTERN_SIZE_INC);
  flexarr *a = t.a;
  cc *ac;

  t.m.b = f+*i;
  (*i)++;
  while (*i < s && isspace(f[*i]))
    (*i)++;
  if (f[*i] == '!') {
    handle_comment(f,i,s);
	  return;
  }
  t.n.b = f+*i;
  while (*i < s && !isspace(f[*i]) && f[*i] != '>')
    (*i)++;
  t.n.s = (f+*i)-t.n.b;

  for (; *i < s && f[*i] != '>'; (*i)++) {
    if (f[*i] == '/')
      return;
    
    while (*i < s && isspace(f[*i]))
      (*i)++;
    if (!isalpha(f[*i]))
      continue;
    
    ac = (cc*)flexarr_inc(a);
    memset(ac,0,sizeof(cc));
    ac->f.b = f+*i;
    while (*i < s && isalpha(f[*i]))
      (*i)++;
    ac->f.s = (f+*i)-ac->f.b;
    if (f[*i] != '=') {
      a->size--;
      continue;
    }
    (*i)++;
    while (*i < s && isspace(f[*i]))
      (*i)++;
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
    if (strlen(without_end_s[j]) == t.n.s && strncmp(t.n.b,without_end_s[j],t.n.s) == 0) {
      t.m.s = f+*i-t.m.b+1;
      goto END;
    }
  }

  _Bool script = 0;
  for (int j = 0; script_s[j]; j++) {
    if (strlen(script_s[j]) == t.n.s && strncmp(t.n.b,script_s[j],t.n.s) == 0) {
      script = 1;
      break;
    }
  }

  (*i)++;

  while (*i < s) {
    if (f[*i] == '<') {
      if (f[*i+1] == '/') {
        if (strncmp(t.n.b,f+*i+2,t.n.s) == 0) {
          *i += 2+t.n.s;
          while (*i < s && f[*i] != '>')
            (*i)++;
          t.m.s = (f+*i+1)-t.m.b;
          goto END;
        }
      }
      else if (!script) {
        if (f[*i+1] == '!') {
          (*i)++;
          handle_comment(f,i,s);
          continue;
        }
        else
          handle_struct(f,i,s,p,lvl+1);
      }
    }
    (*i)++;
  }

  END: ;
  print_struct(&t,p,lvl);
  
  flexarr_free(t.a);
}

static char *delchar(char *src, const size_t pos, size_t *size)
{
  size_t s = *size-1;
  for (size_t g = pos; g < s; g++)
    src[g] = src[g+1];
  src[s] = 0;
  *size = s;
  return src;
}

static void handle_sbrackets(const char *src, size_t *pos, const size_t size, ushort *x, ushort *y)
{
  *x = 0;
  *y = 0;
  char tmp[32];
  size_t t1,t2;
  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == '-')
    goto L1;
  if (src[*pos] == '$') {
    *x = -1;
    (*pos)++;
    goto L2;
  }

  t1 = *pos;
  while_is(isdigit,src,*pos,size);
  t2 = *pos-t1;
  if (t2 == 0) {
    *pos = t1;
    goto L2;
  }
  if (t2 > 31)
    return;
  memcpy(tmp,src+t1,t2);
  tmp[t2] = 0;
  *x = atoi(tmp);
  while_is(isspace,src,*pos,size);

  if (src[*pos] != '-')
    goto L2;

  L1: ;
  (*pos)++;
  while_is(isspace,src,*pos,size);
  if (src[*pos] == '$') {
    (*pos)++;
    *y = -1;
    goto L2;
  } else if (src[*pos] == ']') {
    *y = -1;
    goto L2;
  }

  t1 = *pos;
  while_is(isdigit,src,*pos,size);
  t2 = *pos-t1;
  if (t2 == 0) {
    *pos = t1;
    goto L2;
  }
  if (t2 > 31)
    return;
  memcpy(tmp,src+t1,t2);
  tmp[t2] = 0;
  *y = atoi(tmp);

  L2: ;
  while_is(isspace,src,*pos,size);
  (*pos)++;
}

static void parse_pattern(char *pattern, size_t size, struct pat *p)
{
  struct pel *pe;
  p->args = flexarr_init(sizeof(struct pel),PATTERN_SIZE_INC);
  p->py = -1;
  p->px = 0;
  p->ay = -1;
  p->ax = 0;
  char tmp[512];
  tmp[0] = '^';

  size_t pos = 0,t;
  while_is(isspace,pattern,pos,size);
  t = pos;
  while (pos < size && pos-t < 509 && !isspace(pattern[pos]))
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
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&p->ax,&p->ay);
        while_is(isspace,pattern,i,size);
      }
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&p->px,&p->py);
        while_is(isspace,pattern,i,size);
      }
      return;
    }
    if (pattern[i] == '+' || pattern[i] == '-') {
      uchar tf = pattern[i]; 
      ushort x=0,y=-1;
      i++;
      while_is(isspace,pattern,i,size);
      if (pattern[i] == '[') {
        handle_sbrackets(pattern,&i,size,&x,&y);
        while_is(isspace,pattern,i,size);
      }
      t = i;
      while (i < size && i < 509 && !isspace(pattern[i]) && pattern[i] != '=')
          i++;
      memcpy(tmp+1,pattern+t,i-t);
      tmp[i+1-t] = '$';
      tmp[i+2-t] = 0;
      pe = (struct pel*)flexarr_inc(p->args);
      pe->flags = 0;
      if (tf == '+')
          pe->flags |= 1;
      else
          pe->flags &= ~1;

      if (regcomp(&pe->r[0],tmp,regexflags) != 0) {
        regfree(&pe->r[0]);
        return;
      }
      pe->px = x;
      pe->py = y;

      if (pattern[i] == '=' && pattern[i+1] == '"') {
        i += 2;
        t = i;
        while (i < size && i < 509 && pattern[i] != '"') {
          if (patterns[i] == '\\')
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

static void split_patterns(flexarr **pattern)
{
  *pattern = flexarr_init(sizeof(struct pat),PATTERN_SIZE_INC);
  size_t len = strlen(patterns);
  if (len == 0)
    return;
  for (size_t i = 0, j; i < len; i++) {
    j = i;

    while (i < len) {
      if (patterns[i] == '\\' && patterns[i] == ';')
	delchar(patterns,i,&len);
      if (patterns[i] == ';')
        break;
      i++;
    }

    parse_pattern(patterns+j,i-j,(struct pat*)flexarr_inc(*pattern));
    while (i < len && patterns[i] == ';')
      i++;
    i--;
  }
  flexarr_clearb(*pattern);
}

void go_through(char *f, struct pat *p, const size_t s)
{
  for (size_t i = 0; i < s; i++) {
    if (f[i] == '<')
      handle_struct(f,&i,s,p,0);
  }
  fflush(outfile); 
}

static void analyze(char *f, size_t s, _Bool inpipe)
{
  if (f == NULL || s == 0)
    return;
  flexarr *fl;
  size_t size = 0;
  if (patterns != NULL) {
    split_patterns(&fl);
    size = fl->size;
  }
  if (size == 0)
    go_through(f,NULL,s);
  else {
    FILE *t1 = outfile;
    char **ptr = malloc(size<<3);
    size_t *fsize = malloc(size<<3);
    
    for (size_t i = 0; i < size; i++) {
      outfile = (i == size-1) ? t1 : open_memstream(&ptr[i],&fsize[i]);

      struct pat *flv = &((struct pat*)fl->v)[i];
      go_through(f,flv,s);

      if (!inpipe && i == 0)
        munmap(f,s);
      else
        free(f);
      if (i != size-1)
        fclose(outfile);

      f = ptr[i];
      s = fsize[i];
      regfree(&flv->r);
      for (size_t j = 0; j < flv->args->size; j++) {
        regfree(&((struct pel*)flv->args->v)[j].r[0]);
        if (((struct pel*)flv->args->v)[j].flags&2)
          regfree(&((struct pel*)flv->args->v)[j].r[1]);
      }
      flexarr_free(flv->args);
    }
    free(ptr);
    free(fsize);
    fflush(outfile);
    flexarr_free(fl);
  }
}

int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

static void pipe_to_str(int fd, char **file, size_t *size)
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

void handle_file(const char *f)
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
    }
    else 
      fprintf(stderr,"%s: -R not specified: omitting directory '%s'\n",argv0,f);
    return;
  }

  file = mmap(NULL,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
  if (file == NULL)
    warn("%s",f);
  else
    analyze(file,st.st_size,0);

  close(fd);
}

int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  if (typeflag == FTW_F || typeflag == FTW_SL)
    handle_file(fpath);

  return FTW_CONTINUE;
}

int main(int argc, char **argv)
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
      if (argv[0][i_+1]) {
        fname = argv[0]+i_+1;
        brk_= 1;
      }
      else {
        argc--;
        fname = *(++argv);
      }
      outfile = fopen(fname,"w");
      if (outfile == NULL)
          err(1,"%s\n",fname);
    }
    break;
    case 'f': {
      char *fname;
      if (argv[0][i_+1]) {
        fname = argv[0]+i_+1;
        brk_= 1;
      }
      else {
        argc--;
        fname = *(++argv);
      }
      int fd = open(fname,O_RDONLY);
      if (fd == -1)
          err(1,"%s\n",fname);
      size_t s;
      pipe_to_str(fd,&patterns,&s);
      patterns[s] = 0;
      close(fd);
    }
    break;
    case 'H': nftwflags &= ~FTW_PHYS; break;
    case 'r': settings |= F_RECURSIVE; break;
    case 'R': settings |= F_RECURSIVE; nftwflags &= ~FTW_PHYS; break;
    case 'V': die("v1.0\n"); break;
    case 'h': default:
      usage();
  } ARGEND;
  
  int g = 0;
  for (int i = 1; __argv[i]; i++) {
    if (__argv[i][0] == '-')
    {
      if (__argv[i][1] == 'o' || __argv[i][1] == 'f')
        i++;
      continue;
    }
    if (!(settings&F_LIST) && g == 0 && patterns == NULL) {
      patterns = __argv[i];
      continue;
    }

    handle_file(__argv[i]);
    g++;
  }

  if (g == 0)
    handle_file(NULL);

  fclose(outfile);

  fclose(outfile);

  return 0;
}
