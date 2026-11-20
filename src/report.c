#include "chm.h"
#include "chm_int.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void chm_report_line(chm_report *r, const char *text)
{
    size_t used = strlen(r->text);
    size_t room = CHM_REPORT_MAX > used ? CHM_REPORT_MAX - used : 0;

    if (room > 2) {
        snprintf(r->text + used, room, "%s\n", text);
    }
}

void chm_report_add(chm_report *r, const char *fmt, ...)
{
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    chm_report_line(r, line);
}