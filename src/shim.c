#include "chm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static ssize_t sh_write(int fd, const void *buf, size_t n)
{
    return write(fd, buf, n);
}

static ssize_t sh_read(int fd, void *buf, size_t n)
{
    return read(fd, buf, n);
}

static int *sh_error(void)
{
    return &errno;
}

static void sh_bzero(void *p, size_t n)
{
    memset(p, 0, n);
}

static uint64_t sh_absolute_time(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct {
    const char *name;
    void *addr;
} chm_shim_entry;

static const chm_shim_entry chm_shims[] = {
    { "_write", (void *)sh_write },
    { "_read", (void *)sh_read },
    { "___error", (void *)sh_error },
    { "___errno", (void *)sh_error },
    { "_mach_absolute_time", (void *)sh_absolute_time },
    { "_malloc", (void *)malloc },
    { "_free", (void *)free },
    { "_calloc", (void *)calloc },
    { "_realloc", (void *)realloc },
    { "_memcpy", (void *)memcpy },
    { "_memmove", (void *)memmove },
    { "_memset", (void *)memset },
    { "_bzero", (void *)sh_bzero },
    { "_strlen", (void *)strlen },
    { "_strcmp", (void *)strcmp },
    { "_strcpy", (void *)strcpy },
    { "_abort", (void *)abort },
    { "_exit", (void *)exit },
    { "_getenv", (void *)getenv },
    { NULL, NULL }
};

void *chm_resolve_import(const char *name)
{
    const chm_shim_entry *e;

    for (e = chm_shims; e->name; e++) {
        const char *a = e->name;
        const char *b = name;

        if (a[0] == '_' && b[0] != '_')
            a++;
        if (strcmp(a, b) == 0)
            return e->addr;
        if (strcmp(e->name, name) == 0)
            return e->addr;
    }
    return NULL;
}