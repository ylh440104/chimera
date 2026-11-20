#include "chm_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *family_of(const char *n)
{
    if (!strncmp(n, "_OBJC_CLASS_$_", 14))
        return "objc-class";
    if (!strncmp(n, "_OBJC_METACLASS_$_", 18))
        return "objc-metaclass";
    if (!strncmp(n, "_OBJC_EHTYPE_$_", 15))
        return "objc-ehtype";
    if (!strncmp(n, "_objc_", 6))
        return "objc-runtime";
    if (!strncmp(n, "_CF", 3) || !strncmp(n, "_kCF", 4))
        return "corefoundation";
    if (!strncmp(n, "_CG", 3))
        return "coregraphics";
    if (!strncmp(n, "_NS", 3))
        return "foundation";
    if (!strncmp(n, "_UI", 3))
        return "uikit";
    if (!strncmp(n, "_CA", 3))
        return "quartzcore";
    if (!strncmp(n, "_AV", 3))
        return "avfoundation";
    if (!strncmp(n, "_GC", 3))
        return "gamecontroller";
    if (!strncmp(n, "_Sec", 4) || !strncmp(n, "_kSec", 5))
        return "security";
    if (!strncmp(n, "_CC_", 4))
        return "commoncrypto";
    if (!strncmp(n, "_dispatch", 9))
        return "libdispatch";
    if (!strncmp(n, "_IO", 3) || !strncmp(n, "_kIO", 4))
        return "iokit";
    if (!strncmp(n, "_Block_", 7))
        return "blocks";
    return "libc-and-other";
}

typedef struct {
    const char *name;
    int syms;
    int hits;
} fam;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: chmdemand <app-exe> [csv]\n");
        return 2;
    }
    chm_session *s = chm_session_create();
    if (!s)
        return 1;
    if (chm_session_load(s, argv[1])) {
        char out[16384];
        chm_session_report(s, out, sizeof(out));
        fprintf(stderr, "%s", out);
        chm_session_destroy(s);
        return 1;
    }

    fam fams[32];
    int nf = 0;
    for (int i = 0; i < s->missing_count; ++i) {
        const char *f = family_of(s->missing[i].name);
        int j;
        for (j = 0; j < nf; ++j) {
            if (!strcmp(fams[j].name, f)) {
                fams[j].syms++;
                fams[j].hits += (int)s->missing[i].hits;
                break;
            }
        }
        if (j == nf && nf < 32) {
            fams[nf].name = f;
            fams[nf].syms = 1;
            fams[nf].hits = (int)s->missing[i].hits;
            nf++;
        }
    }
    for (int i = 0; i < nf; ++i)
        for (int j = i + 1; j < nf; ++j)
            if (fams[j].hits > fams[i].hits) {
                fam t = fams[i];
                fams[i] = fams[j];
                fams[j] = t;
            }

    printf("images=%d classes=%d exports=%d\n", s->image_count, s->class_count, s->export_count);
    printf("main bind=%u missing=%u rebase=%u executable=%d\n",
           s->images[0].nfixup_bind, s->images[0].nfixup_missing,
           s->images[0].nfixup_rebase, s->images[0].executable);
    printf("distinct unresolved=%d\n\n", s->missing_count);
    printf("%-22s %8s %8s\n", "family", "symbols", "slots");
    for (int i = 0; i < nf; ++i)
        printf("%-22s %8d %8d\n", fams[i].name, fams[i].syms, fams[i].hits);

    if (argc > 2) {
        FILE *f = fopen(argv[2], "w");
        if (f) {
            fprintf(f, "symbol,hits,family,weak\n");
            for (int i = 0; i < s->missing_count; ++i)
                fprintf(f, "\"%s\",%u,%s,%d\n", s->missing[i].name, s->missing[i].hits,
                        family_of(s->missing[i].name), s->missing[i].weak ? 1 : 0);
            fclose(f);
        }
    }
    chm_session_destroy(s);
    return 0;
}