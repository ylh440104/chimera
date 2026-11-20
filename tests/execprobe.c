#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const unsigned int code[] = {
    0x52800540u,
    0xD65F03C0u,
};

int main(void)
{
    void *want = (void *)0x100000000ull;
    size_t size = 4096;
    void *got;

    got = mmap(want, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    printf("mmap  %p errno %d\n", got, errno);
    if (got == MAP_FAILED)
        return 1;

    memcpy(got, code, sizeof(code));
    __builtin___clear_cache((char *)got, (char *)got + sizeof(code));

    if (mprotect(got, size, PROT_READ | PROT_EXEC) != 0) {
        printf("mprotect failed errno %d\n", errno);
        return 1;
    }
    printf("calling %p\n", got);
    fflush(stdout);

    int (*fn)(void) = (int (*)(void))got;
    int rc = fn();
    printf("returned %d\n", rc);
    return 0;
}