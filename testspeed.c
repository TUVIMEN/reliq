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

struct test tests[] = {
    {
        .files = {
            "test/advanced/boards-forums/invision/b01",
            "test/advanced/boards-forums/invision/b02",
            "test/advanced/boards-forums/invision/b03",
            "test/advanced/boards-forums/invision/b04",
            "test/advanced/boards-forums/invision/b05",
            "test/advanced/boards-forums/invision/b06",
            "test/advanced/boards-forums/invision/b07",
            "test/advanced/boards-forums/invision/b08",
            "test/advanced/boards-forums/invision/b09",
            "test/advanced/boards-forums/invision/b10",
            "test/advanced/boards-forums/invision/b11",
            "test/advanced/boards-forums/invision/b12",
            "test/advanced/boards-forums/invision/b13",
            "test/advanced/boards-forums/invision/b14",
            "test/advanced/boards-forums/invision/b15",
            "test/advanced/boards-forums/invision/f01",
            "test/advanced/boards-forums/invision/f02",
            "test/advanced/boards-forums/invision/f03",
            "test/advanced/boards-forums/invision/f04",
            "test/advanced/boards-forums/invision/f05",
            "test/advanced/boards-forums/invision/f06",
            "test/advanced/boards-forums/invision/f07",
            "test/advanced/boards-forums/invision/f08",
            "test/advanced/boards-forums/invision/f09",
            "test/advanced/boards-forums/invision/f10",
            "test/advanced/boards-forums/invision/f11",
            "test/advanced/boards-forums/invision/f12",
            "test/advanced/boards-forums/invision/f13",
            "test/advanced/boards-forums/invision/f14",
            "test/advanced/boards-forums/invision/f15",
            "test/advanced/boards-forums/invision/f16",
            "test/advanced/boards-forums/invision/f17",
            "test/advanced/boards-forums/invision/f18",
        },
        .expr = "test/advanced/boards-forums/invision.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/phpbb/b00",
            "test/advanced/boards-forums/phpbb/b01",
            "test/advanced/boards-forums/phpbb/b02",
            "test/advanced/boards-forums/phpbb/b03",
            "test/advanced/boards-forums/phpbb/b04",
            "test/advanced/boards-forums/phpbb/b05",
            "test/advanced/boards-forums/phpbb/b06",
            "test/advanced/boards-forums/phpbb/b07",
            "test/advanced/boards-forums/phpbb/b08",
            "test/advanced/boards-forums/phpbb/b09",
            "test/advanced/boards-forums/phpbb/b10",
            "test/advanced/boards-forums/phpbb/b11",
            "test/advanced/boards-forums/phpbb/b12",
            "test/advanced/boards-forums/phpbb/f00",
            "test/advanced/boards-forums/phpbb/f01",
            "test/advanced/boards-forums/phpbb/f02",
            "test/advanced/boards-forums/phpbb/f03",
            "test/advanced/boards-forums/phpbb/f04",
            "test/advanced/boards-forums/phpbb/f05",
            "test/advanced/boards-forums/phpbb/f06",
            "test/advanced/boards-forums/phpbb/f07",
            "test/advanced/boards-forums/phpbb/f08",
            "test/advanced/boards-forums/phpbb/f09",
            "test/advanced/boards-forums/phpbb/f10",
            "test/advanced/boards-forums/phpbb/f11",
            "test/advanced/boards-forums/phpbb/f12",
            "test/advanced/boards-forums/phpbb/f13",
        },
        .expr = "test/advanced/boards-forums/phpbb.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/smf1/b01",
            "test/advanced/boards-forums/smf1/b02",
            "test/advanced/boards-forums/smf1/b03",
            "test/advanced/boards-forums/smf1/b04",
            "test/advanced/boards-forums/smf1/b05",
            "test/advanced/boards-forums/smf1/b06",
            "test/advanced/boards-forums/smf1/b07",
            "test/advanced/boards-forums/smf1/b08",
            "test/advanced/boards-forums/smf1/b09",
            "test/advanced/boards-forums/smf1/b10",
            "test/advanced/boards-forums/smf1/b11",
            "test/advanced/boards-forums/smf1/f01",
            "test/advanced/boards-forums/smf1/f02",
            "test/advanced/boards-forums/smf1/f03",
            "test/advanced/boards-forums/smf1/f04",
            "test/advanced/boards-forums/smf1/f05",
            "test/advanced/boards-forums/smf1/f06",
            "test/advanced/boards-forums/smf1/f07",
            "test/advanced/boards-forums/smf1/f08",
            "test/advanced/boards-forums/smf1/f09",
            "test/advanced/boards-forums/smf1/f10",
            "test/advanced/boards-forums/smf1/f11",
        },
        .expr = "test/advanced/boards-forums/smf1.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/smf2/b01",
            "test/advanced/boards-forums/smf2/b02",
            "test/advanced/boards-forums/smf2/b03",
            "test/advanced/boards-forums/smf2/b04",
            "test/advanced/boards-forums/smf2/b05",
            "test/advanced/boards-forums/smf2/b06",
            "test/advanced/boards-forums/smf2/b07",
            "test/advanced/boards-forums/smf2/b08",
            "test/advanced/boards-forums/smf2/b09",
            "test/advanced/boards-forums/smf2/b10",
            "test/advanced/boards-forums/smf2/b11",
            "test/advanced/boards-forums/smf2/b12",
            "test/advanced/boards-forums/smf2/b13",
            "test/advanced/boards-forums/smf2/b14",
            "test/advanced/boards-forums/smf2/b15",
            "test/advanced/boards-forums/smf2/b16",
            "test/advanced/boards-forums/smf2/b17",
            "test/advanced/boards-forums/smf2/b18",
            "test/advanced/boards-forums/smf2/b19",
            "test/advanced/boards-forums/smf2/b20",
            "test/advanced/boards-forums/smf2/b21",
            "test/advanced/boards-forums/smf2/b22",
            "test/advanced/boards-forums/smf2/f01",
            "test/advanced/boards-forums/smf2/f02",
            "test/advanced/boards-forums/smf2/f03",
            "test/advanced/boards-forums/smf2/f04",
            "test/advanced/boards-forums/smf2/f05",
            "test/advanced/boards-forums/smf2/f06",
            "test/advanced/boards-forums/smf2/f07",
            "test/advanced/boards-forums/smf2/f08",
            "test/advanced/boards-forums/smf2/f09",
            "test/advanced/boards-forums/smf2/f10",
            "test/advanced/boards-forums/smf2/f11",
            "test/advanced/boards-forums/smf2/f12",
            "test/advanced/boards-forums/smf2/f13",
            "test/advanced/boards-forums/smf2/f15",
            "test/advanced/boards-forums/smf2/f16",
            "test/advanced/boards-forums/smf2/f17",
            "test/advanced/boards-forums/smf2/f18",
            "test/advanced/boards-forums/smf2/f19",
            "test/advanced/boards-forums/smf2/f20",
            "test/advanced/boards-forums/smf2/f21",
            "test/advanced/boards-forums/smf2/f22",
        },
        .expr = "test/advanced/boards-forums/smf2.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/stackexchange/1",
            "test/advanced/boards-forums/stackexchange/2",
            "test/advanced/boards-forums/stackexchange/3",
            "test/advanced/boards-forums/stackexchange/4",
        },
        .expr = "test/advanced/boards-forums/stackexchange.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/xenforo1/b01",
            "test/advanced/boards-forums/xenforo1/b02",
            "test/advanced/boards-forums/xenforo1/b03",
            "test/advanced/boards-forums/xenforo1/b04",
            "test/advanced/boards-forums/xenforo1/b05",
            "test/advanced/boards-forums/xenforo1/b06",
            "test/advanced/boards-forums/xenforo1/b07",
            "test/advanced/boards-forums/xenforo1/b08",
            "test/advanced/boards-forums/xenforo1/b09",
            "test/advanced/boards-forums/xenforo1/b10",
            "test/advanced/boards-forums/xenforo1/b11",
            "test/advanced/boards-forums/xenforo1/b12",
            "test/advanced/boards-forums/xenforo1/b13",
            "test/advanced/boards-forums/xenforo1/b14",
            "test/advanced/boards-forums/xenforo1/b15",
            "test/advanced/boards-forums/xenforo1/b16",
            "test/advanced/boards-forums/xenforo1/b17",
            "test/advanced/boards-forums/xenforo1/b18",
            "test/advanced/boards-forums/xenforo1/f01",
            "test/advanced/boards-forums/xenforo1/f02",
            "test/advanced/boards-forums/xenforo1/f03",
            "test/advanced/boards-forums/xenforo1/f04",
            "test/advanced/boards-forums/xenforo1/f05",
            "test/advanced/boards-forums/xenforo1/f06",
            "test/advanced/boards-forums/xenforo1/f07",
            "test/advanced/boards-forums/xenforo1/f08",
            "test/advanced/boards-forums/xenforo1/f09",
            "test/advanced/boards-forums/xenforo1/f10",
            "test/advanced/boards-forums/xenforo1/f11",
            "test/advanced/boards-forums/xenforo1/f12",
            "test/advanced/boards-forums/xenforo1/f13",
            "test/advanced/boards-forums/xenforo1/f14",
            "test/advanced/boards-forums/xenforo1/f15",
            "test/advanced/boards-forums/xenforo1/f16",
            "test/advanced/boards-forums/xenforo1/f17",
            "test/advanced/boards-forums/xenforo1/f18",
        },
        .expr = "test/advanced/boards-forums/xenforo1.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/xenforo2/b01",
            "test/advanced/boards-forums/xenforo2/b02",
            "test/advanced/boards-forums/xenforo2/b03",
            "test/advanced/boards-forums/xenforo2/b04",
            "test/advanced/boards-forums/xenforo2/b05",
            "test/advanced/boards-forums/xenforo2/b06",
            "test/advanced/boards-forums/xenforo2/b07",
            "test/advanced/boards-forums/xenforo2/b08",
            "test/advanced/boards-forums/xenforo2/b09",
            "test/advanced/boards-forums/xenforo2/b10",
            "test/advanced/boards-forums/xenforo2/b11",
            "test/advanced/boards-forums/xenforo2/b12",
            "test/advanced/boards-forums/xenforo2/b13",
            "test/advanced/boards-forums/xenforo2/b14",
            "test/advanced/boards-forums/xenforo2/b15",
            "test/advanced/boards-forums/xenforo2/b16",
            "test/advanced/boards-forums/xenforo2/b17",
            "test/advanced/boards-forums/xenforo2/b18",
            "test/advanced/boards-forums/xenforo2/b19",
            "test/advanced/boards-forums/xenforo2/b20",
            "test/advanced/boards-forums/xenforo2/b21",
            "test/advanced/boards-forums/xenforo2/b22",
            "test/advanced/boards-forums/xenforo2/b23",
            "test/advanced/boards-forums/xenforo2/f01",
            "test/advanced/boards-forums/xenforo2/f02",
            "test/advanced/boards-forums/xenforo2/f03",
            "test/advanced/boards-forums/xenforo2/f04",
            "test/advanced/boards-forums/xenforo2/f05",
            "test/advanced/boards-forums/xenforo2/f06",
            "test/advanced/boards-forums/xenforo2/f07",
            "test/advanced/boards-forums/xenforo2/f08",
            "test/advanced/boards-forums/xenforo2/f09",
            "test/advanced/boards-forums/xenforo2/f10",
            "test/advanced/boards-forums/xenforo2/f11",
            "test/advanced/boards-forums/xenforo2/f12",
            "test/advanced/boards-forums/xenforo2/f13",
            "test/advanced/boards-forums/xenforo2/f14",
            "test/advanced/boards-forums/xenforo2/f15",
            "test/advanced/boards-forums/xenforo2/f16",
            "test/advanced/boards-forums/xenforo2/f17",
            "test/advanced/boards-forums/xenforo2/f18",
            "test/advanced/boards-forums/xenforo2/f19",
            "test/advanced/boards-forums/xenforo2/f20",
            "test/advanced/boards-forums/xenforo2/f21",
            "test/advanced/boards-forums/xenforo2/f22",
            "test/advanced/boards-forums/xenforo2/f23",
            "test/advanced/boards-forums/xenforo2/f24",
            "test/advanced/boards-forums/xenforo2/f25",
        },
        .expr = "test/advanced/boards-forums/xenforo2.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/xmb/b01",
            "test/advanced/boards-forums/xmb/b02",
            "test/advanced/boards-forums/xmb/b03",
            "test/advanced/boards-forums/xmb/b04",
            "test/advanced/boards-forums/xmb/b05",
            "test/advanced/boards-forums/xmb/b06",
            "test/advanced/boards-forums/xmb/b07",
            "test/advanced/boards-forums/xmb/b08",
            "test/advanced/boards-forums/xmb/b09",
            "test/advanced/boards-forums/xmb/b10",
            "test/advanced/boards-forums/xmb/b11",
            "test/advanced/boards-forums/xmb/b12",
            "test/advanced/boards-forums/xmb/b13",
            "test/advanced/boards-forums/xmb/b14",
        },
        .expr = "test/advanced/boards-forums/xmb-boards.reliq",
    },
    {
        .files = {
            "test/advanced/boards-forums/xmb/f01",
            "test/advanced/boards-forums/xmb/f02",
            "test/advanced/boards-forums/xmb/f03",
            "test/advanced/boards-forums/xmb/f04",
            "test/advanced/boards-forums/xmb/f05",
            "test/advanced/boards-forums/xmb/f06",
            "test/advanced/boards-forums/xmb/f07",
            "test/advanced/boards-forums/xmb/f08",
            "test/advanced/boards-forums/xmb/f09",
            "test/advanced/boards-forums/xmb/f10",
            "test/advanced/boards-forums/xmb/f11",
            "test/advanced/boards-forums/xmb/f12",
        },
        .expr = "test/advanced/boards-forums/xmb-forums.reliq",
    },
    {
        .files = {
            "test/advanced/users/invision/01",
            "test/advanced/users/invision/02",
            "test/advanced/users/invision/03",
            "test/advanced/users/invision/04",
            "test/advanced/users/invision/05",
            "test/advanced/users/invision/06",
            "test/advanced/users/invision/07",
            "test/advanced/users/invision/08",
            "test/advanced/users/invision/09",
            "test/advanced/users/invision/10",
        },
        .expr = "test/advanced/users/invision.reliq",
    },
    {
        .files = {
            "test/advanced/users/stackexchange/00",
            "test/advanced/users/stackexchange/01",
            "test/advanced/users/stackexchange/02",
            "test/advanced/users/stackexchange/03",
            "test/advanced/users/stackexchange/04",
            "test/advanced/users/stackexchange/05",
            "test/advanced/users/stackexchange/06",
            "test/advanced/users/stackexchange/07",
            "test/advanced/users/stackexchange/08",
            "test/advanced/users/stackexchange/09",
            "test/advanced/users/stackexchange/10",
        },
        .expr = "test/advanced/users/stackexchange.reliq",
    },
    {
        .files = {
            "test/advanced/users/xenforo/01",
            "test/advanced/users/xenforo/02",
            "test/advanced/users/xenforo/03",
            "test/advanced/users/xenforo/04",
            "test/advanced/users/xenforo/05",
            "test/advanced/users/xenforo/06",
            "test/advanced/users/xenforo/07",
            "test/advanced/users/xenforo/08",
            "test/advanced/users/xenforo/09",
            "test/advanced/users/xenforo/10",
            "test/advanced/users/xenforo/11",
            "test/advanced/users/xenforo/12",
            "test/advanced/users/xenforo/13",
            "test/advanced/users/xenforo/14",
            "test/advanced/users/xenforo/15",
            "test/advanced/users/xenforo/16",
            "test/advanced/users/xenforo/17",
            "test/advanced/users/xenforo/18",
        },
        .expr = "test/advanced/users/xenforo.reliq",
    },
    {
        .files = {
            "test/advanced/threads/vbulletin/01",
            "test/advanced/threads/vbulletin/02",
            "test/advanced/threads/vbulletin/03",
            "test/advanced/threads/vbulletin/04",
            "test/advanced/threads/vbulletin/05",
            "test/advanced/threads/vbulletin/06",
            "test/advanced/threads/vbulletin/07",
            "test/advanced/threads/vbulletin/08",
            "test/advanced/threads/vbulletin/09",
            "test/advanced/threads/vbulletin/10",
            "test/advanced/threads/vbulletin/11",
            "test/advanced/threads/vbulletin/12",
            "test/advanced/threads/vbulletin/13",
            "test/advanced/threads/vbulletin/14",
            "test/advanced/threads/vbulletin/15",
            "test/advanced/threads/vbulletin/16",
            "test/advanced/threads/vbulletin/17",
            "test/advanced/threads/vbulletin/18",
            "test/advanced/threads/vbulletin/19",
            "test/advanced/threads/vbulletin/20",
            "test/advanced/threads/vbulletin/21",
            "test/advanced/threads/vbulletin/22",
            "test/advanced/threads/vbulletin/23",
            "test/advanced/threads/vbulletin/24",
            "test/advanced/threads/vbulletin/25",
            "test/advanced/threads/vbulletin/26",
        },
        .expr = "test/advanced/threads/vbulletin.reliq"
    },
    {
        .files = {
            "test/advanced/boards-forums/vbulletin/b01",
            "test/advanced/boards-forums/vbulletin/b02",
            "test/advanced/boards-forums/vbulletin/b03",
            "test/advanced/boards-forums/vbulletin/b04",
            "test/advanced/boards-forums/vbulletin/b05",
            "test/advanced/boards-forums/vbulletin/b06",
            "test/advanced/boards-forums/vbulletin/b07",
            "test/advanced/boards-forums/vbulletin/b08",
            "test/advanced/boards-forums/vbulletin/b09",
            "test/advanced/boards-forums/vbulletin/b10",
            "test/advanced/boards-forums/vbulletin/b11",
            "test/advanced/boards-forums/vbulletin/b12",
            "test/advanced/boards-forums/vbulletin/b13",
            "test/advanced/boards-forums/vbulletin/b14",
            "test/advanced/boards-forums/vbulletin/b15",
            "test/advanced/boards-forums/vbulletin/b16",
            "test/advanced/boards-forums/vbulletin/b17",
            "test/advanced/boards-forums/vbulletin/b18",
            "test/advanced/boards-forums/vbulletin/b19",
            "test/advanced/boards-forums/vbulletin/b20",
            "test/advanced/boards-forums/vbulletin/b21",
            "test/advanced/boards-forums/vbulletin/b22",
            "test/advanced/boards-forums/vbulletin/b23",
            "test/advanced/boards-forums/vbulletin/b24",
            "test/advanced/boards-forums/vbulletin/f01",
            "test/advanced/boards-forums/vbulletin/f02",
            "test/advanced/boards-forums/vbulletin/f03",
            "test/advanced/boards-forums/vbulletin/f04",
            "test/advanced/boards-forums/vbulletin/f05",
            "test/advanced/boards-forums/vbulletin/f06",
            "test/advanced/boards-forums/vbulletin/f07",
            "test/advanced/boards-forums/vbulletin/f08",
            "test/advanced/boards-forums/vbulletin/f09",
            "test/advanced/boards-forums/vbulletin/f10",
            "test/advanced/boards-forums/vbulletin/f11",
            "test/advanced/boards-forums/vbulletin/f12",
            "test/advanced/boards-forums/vbulletin/f13",
            "test/advanced/boards-forums/vbulletin/f14",
            "test/advanced/boards-forums/vbulletin/f15",
            "test/advanced/boards-forums/vbulletin/f16",
            "test/advanced/boards-forums/vbulletin/f17",
            "test/advanced/boards-forums/vbulletin/f18",
            "test/advanced/boards-forums/vbulletin/f19",
            "test/advanced/boards-forums/vbulletin/f20",
            "test/advanced/boards-forums/vbulletin/f21",
            "test/advanced/boards-forums/vbulletin/f22",
            "test/advanced/boards-forums/vbulletin/f23",
            "test/advanced/boards-forums/vbulletin/f24",
            "test/advanced/boards-forums/vbulletin/f25",
            "test/advanced/boards-forums/vbulletin/f26",
            "test/advanced/boards-forums/vbulletin/f27",
        },
        .expr = "test/advanced/boards-forums/vbulletin.reliq"
    }
};

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
            assert(reliq_exec_str(&tests[i].rqs[j],tests[i].rexprs,&f,&fl) == NULL);
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
