#include "chm_format.h"

#include <stdio.h>
#include <string.h>

static int g_fail;
static int g_total;

static void check(const char *want, const char *fmt, ...)
{
    char got[512];
    va_list ap;

    va_start(ap, fmt);
    chm_format(got, sizeof(got), fmt, ap);
    va_end(ap);
    g_total++;
    if (strcmp(want, got) != 0) {
        g_fail++;
        printf("FAIL fmt=[%s]\n  want=[%s]\n  got =[%s]\n", fmt, want, got);
    }
}

static const char *describe(void *obj)
{
    (void)obj;
    return "OBJ";
}

int main(void)
{
    chm_format_set_describer(describe);

    check("hello", "hello");
    check("a%z", "a%%z");
    check("x=5", "x=%d", 5);
    check("x=-5", "x=%d", -5);
    check("x=+5", "x=%+d", 5);
    check("  5", "%3d", 5);
    check("005", "%03d", 5);
    check("5  ", "%-3d", 5);
    check("0.50", "%.2f", 0.5);
    check("3.14", "%.2f", 3.14159);
    check("   3.14", "%7.2f", 3.14159);
    check("abc", "%s", "abc");
    check("(null)", "%s", (const char *)NULL);
    check("ab", "%.2s", "abcdef");
    check("   ab", "%5.2s", "abcdef");
    check("ff", "%x", 255);
    check("FF", "%X", 255);
    check("0xff", "%#x", 255);
    check("-1", "%d", -1);
    check("4294967295", "%u", 4294967295u);
    check("-9223372036854775808", "%lld", (-9223372036854775807LL - 1));
    check("1234567890123", "%lld", 1234567890123LL);
    check("1234567890123", "%ld", 1234567890123L);
    check("42", "%zu", (size_t)42);
    check("x", "%c", 'x');
    check("  x", "%3c", 'x');
    check("[OBJ]", "[%@]", (void *)1);
    check("[(null)]", "[%@]", (void *)NULL);
    check("a=1 b=2", "a=%d b=%d", 1, 2);
    check("100%", "%d%%", 100);
    check("1.0e+00", "%.1e", 1.0);
    check("trailing%", "trailing%");
    check("", "");
    check("pad:*     *", "pad:*%5s*", "");
    check("-0.0", "%+.1f", -0.0);
    check("0x0", "%p", (void *)0);
    check("0x1f", "%p", (void *)0x1f);
    check("  12|34  |", "%4d|%-4d|", 12, 34);
    check("00010203", "%02x%02x%02x%02x", 0, 1, 2, 3);

    printf("%d/%d passed\n", g_total - g_fail, g_total);
    if (g_fail) {
        printf("%d FAILED\n", g_fail);
        return 1;
    }
    printf("test_format: all passed\n");
    return 0;
}