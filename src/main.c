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
#define PATTERN_SIZE (1<<9)
#define PATTERN_SIZE_INC 16

char *argv0, *patterns = NULL;

unsigned int settings = 0;
int regexflags = REG_NEWLINE;
int nftwflags = FTW_ACTIONRETVAL;
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
  die("Usage: %s [OPTION]... PATTERN [FILE]...\n"\
      "Search for PATTERN in each html FILE.\n"\
      "Example: %s -i 'div +id; a +href=\".*\\.org\"' index.html\n\n"\
      "  -i\t\tignore case distinctions in patterns and data\n"\
      "  -v\t\tselect non-matching blocks\n"\
      "  -l\t\tlist structure of file\n"\
      "  -o [FILE]\tchange output to a [FILE] instead of stdout\n"\
      "  -E\t\tuse extended regular expressions\n"\
      "  -H\t\tdo not follow symlinks\n"\
      "  -R\t\tread all files under directory, recursively\n\n"\
      "When you don't specify FILE, FILE will become stdin\n",argv0,argv0,argv0);
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

static _Bool ismatching(struct html_s *t, char *pattern)
{
  size_t size, j;
  regex_t regex[2];
  static char temp[PATTERN_SIZE];

  for (int i = 0; pattern[i]; i++) {
    size = 0;
    if (pattern[i] == '+') {
      i++;
      while (pattern[i] && !isspace(pattern[i]) && pattern[i] != '=') {
        i++;
        size++;
      }

      temp[0] = '^';
      memcpy(temp+1,pattern+i-size,size);
      temp[++size] = '$';
      temp[++size] = 0;

      if (regcomp(&regex[0],temp,regexflags) != 0) {
        regfree(&regex[0]);
        return 0;
      }

      _Bool found = 0, g1 = 0, g2 = 0;

      if (pattern[i] == '=') {
        i += 2;
        size = 0;
        while (pattern[i] && pattern[i] != '"') {
          i++;
          size++;
        }
        g1 = 1;
      
        temp[0] = '^';
        memcpy(temp+1,pattern+i-size,size);
        temp[++size] = '$';
        temp[++size] = 0;

        if (regcomp(&regex[1],temp,regexflags) != 0) {
          regfree(&regex[0]);
          regfree(&regex[1]);
          return 0;
        }
      }

      for (j = 0; j < t->s; j++) {
        memcpy(temp,t->a[j].f.b,t->a[j].f.s);
        temp[t->a[j].f.s] = 0;

        if (regexec(&regex[0],temp,0,NULL,0) == 0) {
          found = 1;
          if (g1) {
            memcpy(temp,t->a[j].s.b,t->a[j].s.s);
            temp[t->a[j].s.s] = 0;
            if (regexec(&regex[1],temp,0,NULL,0) == 0) {
              g2 = 1;
              break;
            }
          }
          else
            break;
        }
        if (size == t->a[j].f.s && strncmp(pattern+i-size,t->a[j].f.b,size) == 0) {
          found = 1;
          break;
        }
      }
      
      regfree(&regex[0]);
      if (g1) {
        regfree(&regex[1]);
        if (!g2)
          return 0;
      }

      if (!found)
        return 0;

      if (!g1)
        i--;
    }
    else if (!isspace(pattern[i])) {
      while (pattern[i] && !isspace(pattern[i])) {
        i++;
        size++;
      }

      temp[0] = '^';
      memcpy(temp+1,pattern+i-size,size);
      temp[++size] = '$';
      temp[++size] = 0;

      if (regcomp(&regex[0],temp,regexflags) != 0) {  
        regfree(&regex[0]);
        return 0;
      }

      memcpy(temp,t->n.b,t->n.s);
      temp[t->n.s] = 0;

      if (regexec(&regex[0],temp,0,NULL,0) != 0) {
        regfree(&regex[0]);
        return 0;
      }
    
      regfree(&regex[0]);
      i--;
    }
  }

  return 1;
}

static void handle_struct(char *f, size_t *i, size_t s, char *pattern)
{
  struct html_s t;
  memset(&t,0,sizeof(struct html_s));

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
    
    t.a = realloc(t.a,++t.s*sizeof(cc));
    t.a[t.s-1].f.b = f+*i;
    while (*i < s && isalpha(f[*i]))
      (*i)++;
    t.a[t.s-1].f.s = (f+*i)-t.a[t.s-1].f.b;
    if (f[*i] != '=')
    {
      t.s--;
      continue;
    }
    (*i)++;
    while (*i < s && isspace(f[*i]))
      (*i)++;
    if (f[*i] != '"')
    {
      t.s--;
      continue;
    }
    (*i)++;
    t.a[t.s-1].s.b = f+*i;
    while (*i < s && f[*i] != '"')
      (*i)++;
    t.a[t.s-1].s.s = (f+*i)-t.a[t.s-1].s.b;
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
          handle_struct(f,i,s,pattern);
      }
    }
    (*i)++;
  }

  END: ;

  if (settings&F_LIST) {
    fwrite(t.n.b,t.n.s,1,outfile);
    _Bool par = 0;
    for (size_t j = 0; j < t.s; j++) {
      par = 1;
      fputc(' ',outfile);
      fwrite(t.a[j].f.b,t.a[j].f.s,1,outfile);
      fputs("=\"",outfile);
      fwrite(t.a[j].s.b,t.a[j].s.s,1,outfile);
    }
    if (par)
      fputc('"',outfile);
    fprintf(outfile," - %lu\n",t.m.s);
  }
  else
  {
    if ((settings&F_REVERSE) == 0 ? ismatching(&t,pattern) : !ismatching(&t,pattern)) {
      fwrite(t.m.b,t.m.s,1,outfile);
      fputc('\n',outfile);
    }
  }

  fflush(outfile);

  free(t.a);
}

static void split_patterns(char ***pattern, size_t *size)
{
  *pattern = NULL;
  *size = 0;
  size_t lsize = 0, len = strlen(patterns);
  if (len < 1)
    return;
  register size_t t;
  for (size_t i = 0, j; i < len; i++) {
    j = i;
    if (lsize == *size) {
      lsize += PATTERN_SIZE_INC;
      *pattern = realloc(*pattern,lsize<<3);
    }
    while (i < len) {
      if (patterns[i] == ';') {
        if (i != 0 && patterns[i-1] == '\\') {
          for (size_t g = i; g < len; g++)
            patterns[g-1] = patterns[g];
          len--;
          i++;
        }
        else
          break;
      }
      i++;
    }

    t = (i-j)+1;
    (*pattern)[*size] = malloc(t);
    memcpy((*pattern)[*size],patterns+j,i-j);
    (*pattern)[*size][t-1] = 0;
    (*size)++;
  }
  *pattern = realloc(*pattern,*size<<3);
}

static void analyze(char *f, size_t s)
{
  if (f == NULL || s == 0)
    return;
  char **pattern = NULL;
  size_t size = 0;
  if (patterns != 0)
    split_patterns(&pattern,&size);
  if (size == 0) {
    for (size_t i = 0; i < s; i++)
      if (f[i] == '<')
        handle_struct(f,&i,s,patterns);
  }
  else {
    FILE *t1 = outfile;
    char **ptr = malloc(size<<3);
    size_t *fsize = malloc(size<<3);
    
    for (size_t i = 0, j; i < size; i++) {
      outfile = (size == 1 || i == size-1) ? t1 : open_memstream(&ptr[i],&fsize[i]);

      for (j = 0; j < s; j++) {
        if (f[j] == '<')
          handle_struct(f,&j,s,pattern[i]);
      }

      if (size != 1 && i == size-1)
      {
        free(f);
        fclose(outfile);
      }

      f = ptr[i];
      s = fsize[i];
    }
    free(ptr);
    free(fsize);
    fflush(outfile);
  }
  free(pattern);
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
    analyze(file,size);
    free(file);
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
  {
    analyze(file,st.st_size);
    munmap(file,st.st_size);
  }

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
    case 'v':
      settings |= F_REVERSE;
      break;
    case 'l':
      settings |= F_LIST;
      break;
    case 'E':
      regexflags |= REG_EXTENDED;
      break;
    case 'i':
      regexflags |= REG_ICASE;
      break;
    case 'o':
    {
      char *fname = NULL;
      if (argv[0][i_+1])
      {
        fname = argv[0]+i_+1;
        brk_= 1;
      }
      else
      {
        argc--;
        fname = *(++argv);
      }
      outfile = fopen(fname,"w");
    }
    break;
    case 'H':
      nftwflags |= FTW_PHYS;
      break;
    case 'R':
      settings |= F_RECURSIVE;
      break;
    case 'h':
    default:
      usage();
  } ARGEND;

  int g = 0;

  for (int i = 1; __argv[i]; i++) {
    if (__argv[i][0] == '-')
    {
      if (__argv[i][1] == 'o')
        i++;
      continue;
    }
    if (!(settings&F_LIST) && g == 0) {
      patterns = __argv[i];
      g++;
      continue;
    }

    handle_file(__argv[i]);
    g++;
  }

  if (settings&F_LIST ? g == 0 : g == 1)
    handle_file(NULL);

  fclose(outfile);

  return 0;
}
