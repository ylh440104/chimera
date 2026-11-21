#ifndef CHM_FORMAT_H
#define CHM_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

typedef const char *(*chm_desc_fn)(void *obj);

void chm_format_set_describer(chm_desc_fn fn);
void chm_format(char *dst, size_t cap, const char *fmt, va_list ap);
void chm_format_args(char *dst, size_t cap, const char *fmt, ...);

#endif