#include "chm_format.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} chm_sink;

static chm_desc_fn g_describer;

void chm_format_set_describer(chm_desc_fn fn)
{
    g_describer = fn;
}

static void sink_putc(chm_sink *s, char c)
{
    if (s->len + 1 < s->cap)
        s->buf[s->len++] = c;
}

static void sink_put(chm_sink *s, const char *p, size_t n)
{
    size_t i;

    if (!p)
        return;
    for (i = 0; i < n; i++)
        sink_putc(s, p[i]);
}

static void sink_pad(chm_sink *s, char c, int n)
{
    int i;

    for (i = 0; i < n; i++)
        sink_putc(s, c);
}

static int u64_digits(uint64_t v, unsigned base, int upper, char *out)
{
    static const char low[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *d = upper ? up : low;
    char tmp[32];
    int n = 0;
    int i;

    if (v == 0) {
        out[0] = '0';
        return 1;
    }
    while (v && n < (int)sizeof(tmp)) {
        tmp[n++] = d[v % base];
        v /= base;
    }
    for (i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    return n;
}

static void emit_int(chm_sink *s, uint64_t mag, int negative, unsigned base, int upper,
                     int width, int prec, int left, int zero, int plus, int space, int alt)
{
    char digits[32];
    char prefix[3];
    int np = 0;
    int nd;
    int zeros = 0;
    int total;
    int pad;
    char sign = 0;

    nd = u64_digits(mag, base, upper, digits);
    if (prec == 0 && mag == 0)
        nd = 0;

    if (alt && base == 16 && mag != 0) {
        prefix[0] = '0';
        prefix[1] = upper ? 'X' : 'x';
        np = 2;
    } else if (alt && base == 8 && (nd == 0 || digits[0] != '0')) {
        prefix[0] = '0';
        np = 1;
    }

    if (negative)
        sign = '-';
    else if (plus)
        sign = '+';
    else if (space)
        sign = ' ';

    if (prec > nd)
        zeros = prec - nd;

    total = (sign ? 1 : 0) + np + zeros + nd;
    pad = width > total ? width - total : 0;

    if (left) {
        if (sign)
            sink_putc(s, sign);
        sink_put(s, prefix, (size_t)np);
        sink_pad(s, '0', zeros);
        sink_put(s, digits, (size_t)nd);
        sink_pad(s, ' ', pad);
        return;
    }
    if (zero && prec < 0) {
        if (sign)
            sink_putc(s, sign);
        sink_put(s, prefix, (size_t)np);
        sink_pad(s, '0', pad);
        sink_put(s, digits, (size_t)nd);
        return;
    }
    sink_pad(s, ' ', pad);
    if (sign)
        sink_putc(s, sign);
    sink_put(s, prefix, (size_t)np);
    sink_pad(s, '0', zeros);
    sink_put(s, digits, (size_t)nd);
}

static void spec_add_uint(char *spec, int *n, unsigned v)
{
    char rev[12];
    int rl = 0;
    int i;

    if (!v)
        rev[rl++] = '0';
    while (v && rl < (int)sizeof(rev)) {
        rev[rl++] = (char)('0' + v % 10);
        v /= 10;
    }
    for (i = 0; i < rl; i++)
        spec[(*n)++] = rev[rl - 1 - i];
}

static void emit_double(chm_sink *s, double v, char conv, int width, int prec,
                        int left, int zero, int plus, int space, int alt)
{
    char spec[48];
    char tmp[512];
    int n = 0;

    spec[n++] = '%';
    if (left)
        spec[n++] = '-';
    if (plus)
        spec[n++] = '+';
    if (space)
        spec[n++] = ' ';
    if (alt)
        spec[n++] = '#';
    if (zero)
        spec[n++] = '0';
    if (width > 0)
        spec_add_uint(spec, &n, (unsigned)width);
    if (prec >= 0) {
        spec[n++] = '.';
        spec_add_uint(spec, &n, (unsigned)prec);
    }
    spec[n++] = conv;
    spec[n] = 0;

    tmp[0] = 0;
    snprintf(tmp, sizeof(tmp), spec, v);
    sink_put(s, tmp, strlen(tmp));
}

static void emit_lit(chm_sink *s, const char *lit, int width, int left)
{
    size_t n = strlen(lit);
    int pad = width > (int)n ? width - (int)n : 0;

    if (!left)
        sink_pad(s, ' ', pad);
    sink_put(s, lit, n);
    if (left)
        sink_pad(s, ' ', pad);
}

static void emit_cstr(chm_sink *s, const char *p, int width, int prec, int left)
{
    size_t n;
    int pad;

    if (!p) {
        emit_lit(s, "(null)", width, left);
        return;
    }
    if ((uintptr_t)p < 0x10000u) {
        emit_lit(s, "(bad-str)", width, left);
        return;
    }
    n = strlen(p);
    if (prec >= 0 && (size_t)prec < n)
        n = (size_t)prec;
    pad = width > (int)n ? width - (int)n : 0;
    if (!left)
        sink_pad(s, ' ', pad);
    sink_put(s, p, n);
    if (left)
        sink_pad(s, ' ', pad);
}

void chm_format(char *dst, size_t cap, const char *fmt, va_list ap0)
{
    chm_sink s;
    va_list ap;

    if (!dst || !cap)
        return;
    dst[0] = 0;
    if (!fmt)
        return;
    s.buf = dst;
    s.cap = cap;
    s.len = 0;
    va_copy(ap, ap0);

    while (*fmt) {
        int left = 0;
        int plus = 0;
        int space = 0;
        int alt = 0;
        int zero = 0;
        int width = 0;
        int prec = -1;
        int lmod = 0;
        char conv;

        if (*fmt != '%') {
            sink_putc(&s, *fmt++);
            continue;
        }
        fmt++;

        for (;;) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '+')
                plus = 1;
            else if (*fmt == ' ')
                space = 1;
            else if (*fmt == '#')
                alt = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
            fmt++;
        }

        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                if (width < 100000)
                    width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    if (prec < 100000)
                        prec = prec * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        if (*fmt == 'h') {
            fmt++;
            lmod = 1;
            if (*fmt == 'h') {
                fmt++;
                lmod = 2;
            }
        } else if (*fmt == 'l') {
            fmt++;
            lmod = 3;
            if (*fmt == 'l') {
                fmt++;
                lmod = 4;
            }
        } else if (*fmt == 'q') {
            fmt++;
            lmod = 4;
        } else if (*fmt == 'z') {
            fmt++;
            lmod = 5;
        } else if (*fmt == 't') {
            fmt++;
            lmod = 6;
        } else if (*fmt == 'j') {
            fmt++;
            lmod = 7;
        } else if (*fmt == 'L') {
            fmt++;
            lmod = 8;
        }

        conv = *fmt;
        if (!conv) {
            sink_putc(&s, '%');
            break;
        }
        fmt++;

        switch (conv) {
        case '%':
            sink_putc(&s, '%');
            break;
        case '@': {
            void *obj = va_arg(ap, void *);
            const char *desc = NULL;

            if (g_describer && obj && (uintptr_t)obj >= 0x10000u)
                desc = g_describer(obj);
            if (!desc)
                desc = obj ? "(object)" : "(null)";
            emit_cstr(&s, desc, width, prec, left);
            break;
        }
        case 's':
            emit_cstr(&s, va_arg(ap, const char *), width, prec, left);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);

            if (!left)
                sink_pad(&s, ' ', width > 1 ? width - 1 : 0);
            sink_putc(&s, c);
            if (left)
                sink_pad(&s, ' ', width > 1 ? width - 1 : 0);
            break;
        }
        case 'd':
        case 'i': {
            int64_t v;

            if (lmod == 4 || lmod == 7 || lmod == 8)
                v = va_arg(ap, long long);
            else if (lmod == 3)
                v = va_arg(ap, long);
            else if (lmod == 5)
                v = (int64_t)va_arg(ap, size_t);
            else if (lmod == 6)
                v = (int64_t)va_arg(ap, ptrdiff_t);
            else
                v = va_arg(ap, int);
            emit_int(&s, v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v, v < 0, 10, 0,
                     width, prec, left, zero, plus, space, 0);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            uint64_t v;
            unsigned base = conv == 'u' ? 10 : (conv == 'o' ? 8 : 16);

            if (lmod == 4 || lmod == 7 || lmod == 8)
                v = va_arg(ap, unsigned long long);
            else if (lmod == 3)
                v = va_arg(ap, unsigned long);
            else if (lmod == 5)
                v = va_arg(ap, size_t);
            else if (lmod == 6)
                v = (uint64_t)va_arg(ap, ptrdiff_t);
            else
                v = va_arg(ap, unsigned int);
            emit_int(&s, v, 0, base, conv == 'X', width, prec, left, zero, 0, 0,
                     alt && (conv == 'x' || conv == 'X' || conv == 'o'));
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);

            sink_put(&s, "0x", 2);
            if (p) {
                char digits[32];
                int nd = u64_digits((uint64_t)(uintptr_t)p, 16, 0, digits);

                sink_put(&s, digits, (size_t)nd);
            } else {
                sink_putc(&s, '0');
            }
            break;
        }
        case 'n': {
            void *slot = va_arg(ap, void *);

            if (slot)
                *(int *)slot = (int)s.len;
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            emit_double(&s, va_arg(ap, double), conv, width, prec, left, zero, plus, space, alt);
            break;
        default: {
            char lit[3];

            lit[0] = '%';
            lit[1] = conv;
            lit[2] = 0;
            sink_put(&s, lit, 2);
            break;
        }
        }
    }

    va_end(ap);
    dst[s.len < cap ? s.len : cap - 1] = 0;
}

void chm_format_args(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    chm_format(dst, cap, fmt, ap);
    va_end(ap);
}