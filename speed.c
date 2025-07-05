#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <err.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include "reliq.h"

#define LENGTH(x) (sizeof(x)/sizeof(*x))

struct test {
    char *expr;
    char *exprs;
    size_t exprsl;

    char *files[64];
    char *contents[64];
    size_t contentsl[64];

    reliq rqs[64];
    reliq_expr *rexprs;
};

#define X "tests/advanced/"

struct test tests[] = {
    {
        .files = {
            X "boards-forums/invision/b01",
            X "boards-forums/invision/b02",
            X "boards-forums/invision/b03",
            X "boards-forums/invision/b04",
            X "boards-forums/invision/b05",
            X "boards-forums/invision/b06",
            X "boards-forums/invision/b07",
            X "boards-forums/invision/b08",
            X "boards-forums/invision/b09",
            X "boards-forums/invision/b10",
            X "boards-forums/invision/b11",
            X "boards-forums/invision/b12",
            X "boards-forums/invision/b13",
            X "boards-forums/invision/b14",
            X "boards-forums/invision/b15",
            X "boards-forums/invision/f01",
            X "boards-forums/invision/f02",
            X "boards-forums/invision/f03",
            X "boards-forums/invision/f04",
            X "boards-forums/invision/f05",
            X "boards-forums/invision/f06",
            X "boards-forums/invision/f07",
            X "boards-forums/invision/f08",
            X "boards-forums/invision/f09",
            X "boards-forums/invision/f10",
            X "boards-forums/invision/f11",
            X "boards-forums/invision/f12",
            X "boards-forums/invision/f13",
            X "boards-forums/invision/f14",
            X "boards-forums/invision/f15",
            X "boards-forums/invision/f16",
            X "boards-forums/invision/f17",
            X "boards-forums/invision/f18",
        },
        .expr = X "boards-forums/invision.reliq",
    },
    {
        .files = {
            X "boards-forums/phpbb/b00",
            X "boards-forums/phpbb/b01",
            X "boards-forums/phpbb/b02",
            X "boards-forums/phpbb/b03",
            X "boards-forums/phpbb/b04",
            X "boards-forums/phpbb/b05",
            X "boards-forums/phpbb/b06",
            X "boards-forums/phpbb/b07",
            X "boards-forums/phpbb/b08",
            X "boards-forums/phpbb/b09",
            X "boards-forums/phpbb/b10",
            X "boards-forums/phpbb/b11",
            X "boards-forums/phpbb/b12",
            X "boards-forums/phpbb/f00",
            X "boards-forums/phpbb/f01",
            X "boards-forums/phpbb/f02",
            X "boards-forums/phpbb/f03",
            X "boards-forums/phpbb/f04",
            X "boards-forums/phpbb/f05",
            X "boards-forums/phpbb/f06",
            X "boards-forums/phpbb/f07",
            X "boards-forums/phpbb/f08",
            X "boards-forums/phpbb/f09",
            X "boards-forums/phpbb/f10",
            X "boards-forums/phpbb/f11",
            X "boards-forums/phpbb/f12",
            X "boards-forums/phpbb/f13",
        },
        .expr = X "boards-forums/phpbb.reliq",
    },
    {
        .files = {
            X "boards-forums/smf1/b01",
            X "boards-forums/smf1/b02",
            X "boards-forums/smf1/b03",
            X "boards-forums/smf1/b04",
            X "boards-forums/smf1/b05",
            X "boards-forums/smf1/b06",
            X "boards-forums/smf1/b07",
            X "boards-forums/smf1/b08",
            X "boards-forums/smf1/b09",
            X "boards-forums/smf1/b10",
            X "boards-forums/smf1/b11",
            X "boards-forums/smf1/f01",
            X "boards-forums/smf1/f02",
            X "boards-forums/smf1/f03",
            X "boards-forums/smf1/f04",
            X "boards-forums/smf1/f05",
            X "boards-forums/smf1/f06",
            X "boards-forums/smf1/f07",
            X "boards-forums/smf1/f08",
            X "boards-forums/smf1/f09",
            X "boards-forums/smf1/f10",
            X "boards-forums/smf1/f11",
        },
        .expr = X "boards-forums/smf1.reliq",
    },
    {
        .files = {
            X "boards-forums/smf2/b01",
            X "boards-forums/smf2/b02",
            X "boards-forums/smf2/b03",
            X "boards-forums/smf2/b04",
            X "boards-forums/smf2/b05",
            X "boards-forums/smf2/b06",
            X "boards-forums/smf2/b07",
            X "boards-forums/smf2/b08",
            X "boards-forums/smf2/b09",
            X "boards-forums/smf2/b10",
            X "boards-forums/smf2/b11",
            X "boards-forums/smf2/b12",
            X "boards-forums/smf2/b13",
            X "boards-forums/smf2/b14",
            X "boards-forums/smf2/b15",
            X "boards-forums/smf2/b16",
            X "boards-forums/smf2/b17",
            X "boards-forums/smf2/b18",
            X "boards-forums/smf2/b19",
            X "boards-forums/smf2/b20",
            X "boards-forums/smf2/b21",
            X "boards-forums/smf2/b22",
            X "boards-forums/smf2/f01",
            X "boards-forums/smf2/f02",
            X "boards-forums/smf2/f03",
            X "boards-forums/smf2/f04",
            X "boards-forums/smf2/f05",
            X "boards-forums/smf2/f06",
            X "boards-forums/smf2/f07",
            X "boards-forums/smf2/f08",
            X "boards-forums/smf2/f09",
            X "boards-forums/smf2/f10",
            X "boards-forums/smf2/f11",
            X "boards-forums/smf2/f12",
            X "boards-forums/smf2/f13",
            X "boards-forums/smf2/f15",
            X "boards-forums/smf2/f16",
            X "boards-forums/smf2/f17",
            X "boards-forums/smf2/f18",
            X "boards-forums/smf2/f19",
            X "boards-forums/smf2/f20",
            X "boards-forums/smf2/f21",
            X "boards-forums/smf2/f22",
        },
        .expr = X "boards-forums/smf2.reliq",
    },
    {
        .files = {
            X "boards-forums/stackexchange/1",
            X "boards-forums/stackexchange/2",
            X "boards-forums/stackexchange/3",
            X "boards-forums/stackexchange/4",
        },
        .expr = X "boards-forums/stackexchange.reliq",
    },
    {
        .files = {
            X "boards-forums/xenforo1/b01",
            X "boards-forums/xenforo1/b02",
            X "boards-forums/xenforo1/b03",
            X "boards-forums/xenforo1/b04",
            X "boards-forums/xenforo1/b05",
            X "boards-forums/xenforo1/b06",
            X "boards-forums/xenforo1/b07",
            X "boards-forums/xenforo1/b08",
            X "boards-forums/xenforo1/b09",
            X "boards-forums/xenforo1/b10",
            X "boards-forums/xenforo1/b11",
            X "boards-forums/xenforo1/b12",
            X "boards-forums/xenforo1/b13",
            X "boards-forums/xenforo1/b14",
            X "boards-forums/xenforo1/b15",
            X "boards-forums/xenforo1/b16",
            X "boards-forums/xenforo1/b17",
            X "boards-forums/xenforo1/b18",
            X "boards-forums/xenforo1/f01",
            X "boards-forums/xenforo1/f02",
            X "boards-forums/xenforo1/f03",
            X "boards-forums/xenforo1/f04",
            X "boards-forums/xenforo1/f05",
            X "boards-forums/xenforo1/f06",
            X "boards-forums/xenforo1/f07",
            X "boards-forums/xenforo1/f08",
            X "boards-forums/xenforo1/f09",
            X "boards-forums/xenforo1/f10",
            X "boards-forums/xenforo1/f11",
            X "boards-forums/xenforo1/f12",
            X "boards-forums/xenforo1/f13",
            X "boards-forums/xenforo1/f14",
            X "boards-forums/xenforo1/f15",
            X "boards-forums/xenforo1/f16",
            X "boards-forums/xenforo1/f17",
            X "boards-forums/xenforo1/f18",
        },
        .expr = X "boards-forums/xenforo1.reliq",
    },
    {
        .files = {
            X "boards-forums/xenforo2/b01",
            X "boards-forums/xenforo2/b02",
            X "boards-forums/xenforo2/b03",
            X "boards-forums/xenforo2/b04",
            X "boards-forums/xenforo2/b05",
            X "boards-forums/xenforo2/b06",
            X "boards-forums/xenforo2/b07",
            X "boards-forums/xenforo2/b08",
            X "boards-forums/xenforo2/b09",
            X "boards-forums/xenforo2/b10",
            X "boards-forums/xenforo2/b11",
            X "boards-forums/xenforo2/b12",
            X "boards-forums/xenforo2/b13",
            X "boards-forums/xenforo2/b14",
            X "boards-forums/xenforo2/b15",
            X "boards-forums/xenforo2/b16",
            X "boards-forums/xenforo2/b17",
            X "boards-forums/xenforo2/b18",
            X "boards-forums/xenforo2/b19",
            X "boards-forums/xenforo2/b20",
            X "boards-forums/xenforo2/b21",
            X "boards-forums/xenforo2/b22",
            X "boards-forums/xenforo2/b23",
            X "boards-forums/xenforo2/f01",
            X "boards-forums/xenforo2/f02",
            X "boards-forums/xenforo2/f03",
            X "boards-forums/xenforo2/f04",
            X "boards-forums/xenforo2/f05",
            X "boards-forums/xenforo2/f06",
            X "boards-forums/xenforo2/f07",
            X "boards-forums/xenforo2/f08",
            X "boards-forums/xenforo2/f09",
            X "boards-forums/xenforo2/f10",
            X "boards-forums/xenforo2/f11",
            X "boards-forums/xenforo2/f12",
            X "boards-forums/xenforo2/f13",
            X "boards-forums/xenforo2/f14",
            X "boards-forums/xenforo2/f15",
            X "boards-forums/xenforo2/f16",
            X "boards-forums/xenforo2/f17",
            X "boards-forums/xenforo2/f18",
            X "boards-forums/xenforo2/f19",
            X "boards-forums/xenforo2/f20",
            X "boards-forums/xenforo2/f21",
            X "boards-forums/xenforo2/f22",
            X "boards-forums/xenforo2/f23",
            X "boards-forums/xenforo2/f24",
            X "boards-forums/xenforo2/f25",
        },
        .expr = X "boards-forums/xenforo2.reliq",
    },
    {
        .files = {
            X "boards-forums/xmb/b01",
            X "boards-forums/xmb/b02",
            X "boards-forums/xmb/b03",
            X "boards-forums/xmb/b04",
            X "boards-forums/xmb/b05",
            X "boards-forums/xmb/b06",
            X "boards-forums/xmb/b07",
            X "boards-forums/xmb/b08",
            X "boards-forums/xmb/b09",
            X "boards-forums/xmb/b10",
            X "boards-forums/xmb/b11",
            X "boards-forums/xmb/b12",
            X "boards-forums/xmb/b13",
            X "boards-forums/xmb/b14",
        },
        .expr = X "boards-forums/xmb-boards.reliq",
    },
    {
        .files = {
            X "boards-forums/xmb/f01",
            X "boards-forums/xmb/f02",
            X "boards-forums/xmb/f03",
            X "boards-forums/xmb/f04",
            X "boards-forums/xmb/f05",
            X "boards-forums/xmb/f06",
            X "boards-forums/xmb/f07",
            X "boards-forums/xmb/f08",
            X "boards-forums/xmb/f09",
            X "boards-forums/xmb/f10",
            X "boards-forums/xmb/f11",
            X "boards-forums/xmb/f12",
        },
        .expr = X "boards-forums/xmb-forums.reliq",
    },
    {
        .files = {
            X "users/invision/01",
            X "users/invision/02",
            X "users/invision/03",
            X "users/invision/04",
            X "users/invision/05",
            X "users/invision/06",
            X "users/invision/07",
            X "users/invision/08",
            X "users/invision/09",
            X "users/invision/10",
        },
        .expr = X "users/invision.reliq",
    },
    {
        .files = {
            X "users/stackexchange/00",
            X "users/stackexchange/01",
            X "users/stackexchange/02",
            X "users/stackexchange/03",
            X "users/stackexchange/04",
            X "users/stackexchange/05",
            X "users/stackexchange/06",
            X "users/stackexchange/07",
            X "users/stackexchange/08",
            X "users/stackexchange/09",
            X "users/stackexchange/10",
        },
        .expr = X "users/stackexchange.reliq",
    },
    {
        .files = {
            X "users/xenforo/01",
            X "users/xenforo/02",
            X "users/xenforo/03",
            X "users/xenforo/04",
            X "users/xenforo/05",
            X "users/xenforo/06",
            X "users/xenforo/07",
            X "users/xenforo/08",
            X "users/xenforo/09",
            X "users/xenforo/10",
            X "users/xenforo/11",
            X "users/xenforo/12",
            X "users/xenforo/13",
            X "users/xenforo/14",
            X "users/xenforo/15",
            X "users/xenforo/16",
            X "users/xenforo/17",
            X "users/xenforo/18",
        },
        .expr = X "users/xenforo.reliq",
    },
    {
        .files = {
            X "threads/vbulletin/01",
            X "threads/vbulletin/02",
            X "threads/vbulletin/03",
            X "threads/vbulletin/04",
            X "threads/vbulletin/05",
            X "threads/vbulletin/06",
            X "threads/vbulletin/07",
            X "threads/vbulletin/08",
            X "threads/vbulletin/09",
            X "threads/vbulletin/10",
            X "threads/vbulletin/11",
            X "threads/vbulletin/12",
            X "threads/vbulletin/13",
            X "threads/vbulletin/14",
            X "threads/vbulletin/15",
            X "threads/vbulletin/16",
            X "threads/vbulletin/17",
            X "threads/vbulletin/18",
            X "threads/vbulletin/19",
            X "threads/vbulletin/20",
            X "threads/vbulletin/21",
            X "threads/vbulletin/22",
            X "threads/vbulletin/23",
            X "threads/vbulletin/24",
            X "threads/vbulletin/25",
            X "threads/vbulletin/26",
        },
        .expr = X "threads/vbulletin.reliq"
    },
    {
        .files = {
            X "boards-forums/vbulletin/b01",
            X "boards-forums/vbulletin/b02",
            X "boards-forums/vbulletin/b03",
            X "boards-forums/vbulletin/b04",
            X "boards-forums/vbulletin/b05",
            X "boards-forums/vbulletin/b06",
            X "boards-forums/vbulletin/b07",
            X "boards-forums/vbulletin/b08",
            X "boards-forums/vbulletin/b09",
            X "boards-forums/vbulletin/b10",
            X "boards-forums/vbulletin/b11",
            X "boards-forums/vbulletin/b12",
            X "boards-forums/vbulletin/b13",
            X "boards-forums/vbulletin/b14",
            X "boards-forums/vbulletin/b15",
            X "boards-forums/vbulletin/b16",
            X "boards-forums/vbulletin/b17",
            X "boards-forums/vbulletin/b18",
            X "boards-forums/vbulletin/b19",
            X "boards-forums/vbulletin/b20",
            X "boards-forums/vbulletin/b21",
            X "boards-forums/vbulletin/b22",
            X "boards-forums/vbulletin/b23",
            X "boards-forums/vbulletin/b24",
            X "boards-forums/vbulletin/f01",
            X "boards-forums/vbulletin/f02",
            X "boards-forums/vbulletin/f03",
            X "boards-forums/vbulletin/f04",
            X "boards-forums/vbulletin/f05",
            X "boards-forums/vbulletin/f06",
            X "boards-forums/vbulletin/f07",
            X "boards-forums/vbulletin/f08",
            X "boards-forums/vbulletin/f09",
            X "boards-forums/vbulletin/f10",
            X "boards-forums/vbulletin/f11",
            X "boards-forums/vbulletin/f12",
            X "boards-forums/vbulletin/f13",
            X "boards-forums/vbulletin/f14",
            X "boards-forums/vbulletin/f15",
            X "boards-forums/vbulletin/f16",
            X "boards-forums/vbulletin/f17",
            X "boards-forums/vbulletin/f18",
            X "boards-forums/vbulletin/f19",
            X "boards-forums/vbulletin/f20",
            X "boards-forums/vbulletin/f21",
            X "boards-forums/vbulletin/f22",
            X "boards-forums/vbulletin/f23",
            X "boards-forums/vbulletin/f24",
            X "boards-forums/vbulletin/f25",
            X "boards-forums/vbulletin/f26",
            X "boards-forums/vbulletin/f27",
        },
        .expr = X "boards-forums/vbulletin.reliq"
    }
};

#undef X

void
die(const char *format, ...)
{
    va_list l;
    va_start(l,format);
    vfprintf(stderr,format,l);
    va_end(l);

    fputc('\n',stderr);
    exit(-1);
}

void
loadfile(const char *path, char **data, size_t *datal)
{
    *data = NULL;
    *datal = 0;

    struct stat st;
    stat(path,&st);

    if ((st.st_mode&S_IFMT) != S_IFREG)
        die("'%s' not a file",path);
    if (st.st_size == 0)
        die("'%s' empty file",path);

    FILE *f = fopen(path,"rb");
    if (!f)
        err(-1,"%s",path);

    *data = malloc(st.st_size);

    assert(fread(*data,1,st.st_size,f) == st.st_size);

    fclose(f);
    *datal = st.st_size;
}

size_t testcasesrun;

size_t
expr_comp_test()
{
    const size_t testsl = LENGTH(tests);
    for (size_t i = 0; i < testsl; i++)
        assert(reliq_ecomp(tests[i].exprs,tests[i].exprsl,&tests[i].rexprs) == NULL);
    return testsl;
}

void
free_exprs()
{
    const size_t testsl = LENGTH(tests);
    for (size_t i = 0; i < testsl; i++)
        reliq_efree(tests[i].rexprs);
}

void
free_rqs()
{
    const size_t testsl = LENGTH(tests);
    for (size_t i = 0; i < testsl; i++)
        for (size_t j = 0; tests[i].files[j]; j++)
            assert(reliq_free(&tests[i].rqs[j]) == 0);
}

size_t
html_parse_test()
{
    const size_t testsl = LENGTH(tests);
    size_t ret = 0;
    for (size_t i = 0; i < testsl; i++) {
        for (size_t j = 0; tests[i].files[j]; j++) {
            assert(reliq_init(tests[i].contents[j],tests[i].contentsl[j],&tests[i].rqs[j]) == NULL);
            ret++;
        }
    }
    return ret;
}

size_t
exec_test()
{
    const size_t testsl = LENGTH(tests);
    size_t ret = 0;
    for (size_t i = 0; i < testsl; i++) {
        for (size_t j = 0; tests[i].files[j]; j++) {
            char *f;
            size_t fl = 0;
            assert(reliq_exec_str(&tests[i].rqs[j],NULL,0,tests[i].rexprs,&f,&fl) == NULL);
            assert(f != NULL);
            assert(fl != 0);
            free(f);
            ret++;
        }
    }
    return ret;
}

double
timediff(struct timespec *t1, struct timespec *t2)
{
    int sd = t2->tv_sec-t1->tv_sec;
    long nsd = t2->tv_nsec-t1->tv_nsec;
    if (nsd < 0) {
        sd--;
        nsd += 1000000000;
    }

    if (nsd == 0)
        return (double)sd;

    return (double)sd+((double)nsd)/1000000000;
}

size_t
repeat(const size_t num, size_t (*func)(void), void (*freefunc)(void))
{
    size_t ret = 0;
    for (size_t i = 0; i < num; i++) {
        if (i > 0 && freefunc)
            freefunc();
        testcasesrun = func();
    }
    return ret;
}

double
measuretime(const size_t repeats, size_t (*func)(void), void (*freefunc)(void))
{
    struct timespec t1,t2;
    clock_gettime(CLOCK_REALTIME,&t1);

    repeat(repeats,func,freefunc);

    clock_gettime(CLOCK_REALTIME,&t2);
    return timediff(&t1,&t2);
}

void
measuretest(const char *name, const size_t repeats, size_t (*func)(void), void (*freefunc)(void))
{
    double t = measuretime(repeats,func,freefunc);
    fprintf(stderr,"%s amount(%lu*%lu) %f\n",name,repeats,testcasesrun,t);
}

int
main(int argc, char *argv[])
{
    const size_t testsl = LENGTH(tests);

    for (size_t i = 0; i < testsl; i++) {
        loadfile(tests[i].expr,&tests[i].exprs,&tests[i].exprsl);

        for (size_t j = 0; tests[i].files[j] != NULL; j++)
            loadfile(tests[i].files[j],&tests[i].contents[j],&tests[i].contentsl[j]);
    }

    measuretest("exprs",500*12,expr_comp_test,free_exprs);
    measuretest("html",18*12,html_parse_test,free_rqs);
    measuretest("exec",1*12,exec_test,NULL);

    free_exprs();
    free_rqs();

    for (size_t i = 0; i < testsl; i++) {
        free(tests[i].exprs);

        for (size_t j = 0; tests[i].files[j] != NULL; j++)
            free(tests[i].contents[j]);
    }

    return 0;
}
