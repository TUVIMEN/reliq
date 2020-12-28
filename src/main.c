#include "main.h"
#include "arg.h"

char *argv0;

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
  die("%s <pattern> <files ...>\n",argv0);
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

static _Bool ismaching(struct html_s *t, const char *p)
{
  size_t size, j;
  for (register int i = 0; p[i]; i++) {
    size = 0;
    if (isalpha(p[i])) {
      while (isalpha(p[i])) {
        i++;
        size++;
      }
      if (size != t->n.s || strncmp(p+i-size,t->n.b,size) != 0)
        return 0;
    }
    else if (p[i] == '-') {
      i++;
      while (isalpha(p[i])) {
        i++;
        size++;
      }
      _Bool found = 0;
      for (j = 0; j < t->s; j++) {
        if (size == t->a[j].f.s && strncmp(p+i-size,t->a[j].f.b,size) == 0) {
          found = 1;
          break;
        }
      }
      if (!found)
        return 0;
      
      if (p[i] == '=') {
        i += 2;
        size = 0;
        while (p[i] && p[i] != '"') {
          i++;
          size++;
        }
        if (t->a[j].s.s != size || strncmp(p+i-size,t->a[j].s.b,size) != 0)
          return 0;
      }
    }
  }

  return 1;
}

static void handle_struct(const char *p, char *f, off_t *i, off_t s)
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
          handle_struct(p,f,i,s);
      }
    }
    (*i)++;
  }

  END: ;

  /*write(1,t.n.b,t.n.s);
  for (size_t j = 0; j < t.s; j++) {
    write(1," ",1);
    write(1,t.a[j].f.b,t.a[j].f.s);
    write(1,"=",1);
    write(1,t.a[j].s.b,t.a[j].s.s);
  }
  write(1," - ",3);
  printf("%ld\n",t.m.s);
  fflush(stdout);*/

  if (ismaching(&t,p)) {
    write(1,t.m.b,t.m.s);
    write(1,"\n",1);
  }

  FREE: ;
  free(t.a);
}

static void analyze(const char *p, char *f, off_t s)
{
  for (off_t i = 0; i < s; i++) {
    if (f[i] == '<')
      handle_struct(p,f,&i,s);
  }
}

static void go_through_files(const char *p, char **f, int s)
{
  int fd;
  struct stat st;
  char *file;

  if (*f == NULL) {
    ssize_t readbytes, size = 1<<15;
    file = malloc(size);
    while ((readbytes = read(0,file,1<<15)) > 0)
      file = realloc(file,size+=readbytes);
    analyze(p,file,size);
    free(file);
    return;
  }

  for (register int i = 0; i < s; i++) {
    if ((fd = open(f[i],O_RDONLY)) == -1) {
      warn("%s",f[i]);
      continue;
    }

    if (fstat(fd,&st) == -1) {
      warn("%s",f[i]);
      close(fd);
      continue;
    }

    file = mmap(NULL,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);

    analyze(p,file,st.st_size);

    munmap(file,st.st_size);
    close(fd);
  }
}

int main(int argc, char **argv)
{
  argv0 = argv[0];
  if (argc < 2)
    usage();

  go_through_files(argv[1],argv+2,argc-2);

  return 0;
}
