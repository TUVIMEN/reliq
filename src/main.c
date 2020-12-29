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

#include "main.h"
#include "arg.h"

char *argv0, *pattern = NULL;

unsigned int settings = 0;
int regexflags = REG_NEWLINE;
int nftwflags = FTW_ACTIONRETVAL;

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
      "Example: %s -i 'a +href=\".*\\.org\"' index.html\n\n"\
      "  -i\tignore case distinctions in patterns and data\n"\
      "  -v\tselect non-matching blocks\n"\
      "  -l\tlist structure of file\n"\
      "  -E\tuse extended regular expressions\n"\
      "  -H\tdo not follow symlinks\n"\
      "  -R\trecursive\n\n"\
      "When you don't specify FILE, FILE will become stdin\n",argv0,argv0,argv0);
}

static void handle_comment(char *f, off_t *i, off_t s)
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

static _Bool ismatching(struct html_s *t)
{
  size_t size, j;
  regex_t regex[2];
  static char temp[NAME_MAX];

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

static void handle_struct(char *f, off_t *i, off_t s)
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

  for (int j = 0; without_end_s[j]; j++)
    if (strlen(without_end_s[j]) == t.n.s && strncmp(t.n.b,without_end_s[j],t.n.s) == 0)
      goto FREE;

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
          handle_struct(f,i,s);
      }
    }
    (*i)++;
  }

  END: ;

  if (settings&F_LIST) {
    write(1,t.n.b,t.n.s);
    for (size_t j = 0; j < t.s; j++) {
      write(1," ",1);
      write(1,t.a[j].f.b,t.a[j].f.s);
      write(1,"=\"",2);
      write(1,t.a[j].s.b,t.a[j].s.s);
    }
    printf("\" - %ld\n",t.m.s);
    fflush(stdout);
  }
  else if ((settings&F_REVERSE) == 0 ? ismatching(&t) : !ismatching(&t)) {
    write(1,t.m.b,t.m.s);
    write(1,"\n",1);
  }

  FREE: ;
  free(t.a);
}

static void analyze(char *f, off_t s)
{
  for (off_t i = 0; i < s; i++) {
    if (f[i] == '<')
      handle_struct(f,&i,s);
  }
}

int nftw_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

void handle_file(const char *f)
{
  int fd;
  struct stat st;
  char *file;

  if (f == NULL) {
    ssize_t readbytes, size = 1<<15;
    file = malloc(size);
    while ((readbytes = read(0,file,1<<15)) > 0)
      file = realloc(file,size+=readbytes);
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

  analyze(file,st.st_size);

  munmap(file,st.st_size);
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
      continue;
    if (!(settings&F_LIST) && g == 0) {
      pattern = __argv[i];
      g++;
      continue;
    }
    
    handle_file(__argv[i]);
    g++;
  }

  if (g == 1 || settings&F_LIST)
    handle_file(NULL);

  return 0;
}
