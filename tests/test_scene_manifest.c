#define _GNU_SOURCE
#include "chm_scene.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_case(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    size_t n = strlen(text);
    assert(fwrite(text, 1, n, f) == n);
    assert(!fclose(f));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    chm_scene_manifest m;
    assert(chm_scene_read(argv[1], &m) == 1);
    assert(m.has_manifest && m.count == 2);
    assert(!strcmp(m.entries[0].name, "Secondary"));
    assert(!strcmp(m.entries[1].name, "Main & Window"));
    assert(!strcmp(m.entries[1].delegate, "FixtureWindowDelegate"));
    assert(!strcmp(m.identifier, "dev.chimera.fixture"));
    assert(!strcmp(m.version, "2.7"));
    char directory[] = "/tmp/chm-manifest-XXXXXX";
    assert(mkdtemp(directory));
    char path[256];
    snprintf(path, sizeof(path), "%s/Info.plist", directory);
    assert(chm_scene_read(directory, &m) == 0 && !m.count);
    const char *bad[] = {
        "bplist00",
        "<plist><dict><key>UIApplicationSceneManifest</key></dict></plist>",
        "<plist><dict><key>UIApplicationSceneManifest</key><string>x</string></dict></plist>",
        "<plist><dict><key>CFBundleIdentifier</key><string>&unknown;</string></dict></plist>",
        "<plist><dict></array></plist>",
        "<plist><dict></dict><dict></dict></plist>"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        write_case(path, bad[i]);
        assert(chm_scene_read(directory, &m) == -1 && !m.count && !m.has_manifest);
    }
    write_case(path, "<?xml version=\"1.0\"?><plist><dict><key>CFBundleIdentifier</key><string>a&#x26;b</string></dict></plist>");
    assert(chm_scene_read(directory, &m) == 1 && !strcmp(m.identifier, "a&b") && !m.has_manifest);
    assert(!unlink(path));
    assert(!rmdir(directory));
    puts("scene manifest: selection, entities and rejection passed");
    return 0;
}
