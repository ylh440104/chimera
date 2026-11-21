#include "chm_scene.h"
#include <yxml.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum scope { IGNORE, PLIST, ROOT, MANIFEST, CONFIGS, MAIN_ARRAY, ENTRY, KEY, VALUE };
typedef struct { enum scope scope; char key[192]; char *dest; size_t used; } node;

static char *field(chm_scene_manifest *m, enum scope scope, const char *key)
{
    if (scope == ROOT) {
        if (!strcmp(key, "CFBundleIdentifier")) return m->identifier;
        if (!strcmp(key, "CFBundleExecutable")) return m->executable;
        if (!strcmp(key, "CFBundleShortVersionString")) return m->version;
        if (!strcmp(key, "CFBundleVersion")) return m->build;
    } else if (scope == ENTRY && m->count) {
        chm_scene_entry *e = &m->entries[m->count - 1];
        if (!strcmp(key, "UISceneConfigurationName")) return e->name;
        if (!strcmp(key, "UISceneDelegateClassName")) return e->delegate;
        if (!strcmp(key, "UISceneClassName")) return e->scene;
        if (!strcmp(key, "UISceneStoryboardFile")) return e->storyboard;
    }
    return NULL;
}

int chm_scene_read(const char *directory, chm_scene_manifest *out)
{
    char path[1024], stack[4096];
    unsigned char block[2048];
    yxml_t xml;
    node nodes[48] = {0};
    size_t depth = 0, bytes = 0;
    int seen_root = 0, rc = -1;
    if (!out || !directory) return -1;
    memset(out, 0, sizeof(*out));
    int n = snprintf(path, sizeof(path), "%s/Info.plist", directory);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    yxml_init(&xml, stack, sizeof(stack));
    for (;;) {
        size_t got = fread(block, 1, sizeof(block), f);
        bytes += got;
        if (bytes > 2u * 1024u * 1024u) goto done;
        for (size_t i = 0; i < got; i++) {
            yxml_ret_t r = yxml_parse(&xml, block[i]);
            if (r < 0) goto done;
            if (r == YXML_ELEMSTART) {
                if (depth == sizeof(nodes)/sizeof(nodes[0])) goto done;
                node *parent = depth ? &nodes[depth - 1] : NULL;
                node *v = &nodes[depth++];
                memset(v, 0, sizeof(*v));
                if (!parent) { if (strcmp(xml.elem, "plist")) goto done; v->scope = PLIST; }
                else if (parent->scope == PLIST) {
                    if (seen_root || strcmp(xml.elem, "dict")) goto done;
                    seen_root = 1; v->scope = ROOT;
                } else if (parent->scope == MAIN_ARRAY && !strcmp(xml.elem, "dict")) {
                    if (out->count == CHM_SCENE_CONFIGS) goto done;
                    ++out->count; v->scope = ENTRY;
                } else if (parent->scope == ROOT || parent->scope == MANIFEST || parent->scope == CONFIGS || parent->scope == ENTRY) {
                    if (!strcmp(xml.elem, "key")) {
                        if (parent->key[0]) goto done;
                        v->scope = KEY; v->dest = parent->key;
                    } else {
                        if (!parent->key[0]) goto done;
                        if (parent->scope == ROOT && !strcmp(parent->key, "UIApplicationSceneManifest")) {
                            if (strcmp(xml.elem, "dict")) goto done;
                            v->scope = MANIFEST; out->has_manifest = 1;
                        } else if (parent->scope == MANIFEST && !strcmp(parent->key, "UISceneConfigurations")) {
                            if (strcmp(xml.elem, "dict")) goto done;
                            v->scope = CONFIGS;
                        } else if (parent->scope == CONFIGS && !strcmp(parent->key, CHM_APPLICATION_ROLE)) {
                            if (strcmp(xml.elem, "array")) goto done;
                            v->scope = MAIN_ARRAY;
                        } else if (!strcmp(xml.elem, "string")) {
                            v->dest = field(out, parent->scope, parent->key);
                            v->scope = VALUE; if (v->dest) v->dest[0] = 0;
                        }
                        parent->key[0] = 0;
                    }
                } else if (parent->scope == KEY || parent->scope == VALUE) goto done;
            } else if (r == YXML_CONTENT && depth && nodes[depth-1].dest) {
                node *v = &nodes[depth-1];
                size_t len = strlen(xml.data);
                if (len >= sizeof(v->key) - v->used) goto done;
                memcpy(v->dest + v->used, xml.data, len + 1); v->used += len;
            } else if (r == YXML_ELEMEND) {
                if (!depth || nodes[depth-1].key[0]) goto done;
                --depth;
            }
        }
        if (got < sizeof(block)) break;
    }
    if (!ferror(f) && !depth && seen_root && yxml_eof(&xml) >= 0) rc = 1;
done:
    fclose(f);
    if (rc < 0) memset(out, 0, sizeof(*out));
    return rc;
}
