#include "chm_session.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static chm_session *active;
static int (*guest_check)(int);

static void fail(const char *message, int result)
{
    fprintf(stderr, "url-storage: %s rc=%d\n%s\n", message, result, active ? active->log_buf : "");
    exit(1);
}

static int descriptors(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) fail("fd enumeration", -1);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') count++;
    closedir(directory);
    return count;
}

static int callback(void *context)
{
    return guest_check(*(int *)context);
}

static void find_cache_file(const char *root, char *path, size_t capacity)
{
    char directory_path[2048];
    int n = snprintf(directory_path, sizeof(directory_path), "%s/mobile/Library/Caches/url-storage-test", root);
    if (n < 0 || (size_t)n >= sizeof(directory_path)) fail("path length", n);
    DIR *directory = opendir(directory_path);
    if (!directory) fail("cache directory", -1);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(directory))) {
        if (strncmp(entry->d_name, "chm-url-", 8)) continue;
        n = snprintf(path, capacity, "%s/%s", directory_path, entry->d_name);
        if (n < 0 || (size_t)n >= capacity) fail("cache filename", n);
        count++;
    }
    closedir(directory);
    if (count != 1) fail("persisted record count", count);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;

    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (!root) return 2;
    int baseline = descriptors();
    for (int iteration = 0; iteration < 3; iteration++) {
        active = chm_session_create();
        if (!active) fail("session create", -1);
        chm_foundation_set_root(root);
        if (chm_session_load(active, argv[1])) fail("load", -1);
        int result = chm_session_run(active, 1, argv + 1);
        if (result) fail("entry", result);
        guest_check = NULL;
        for (int i = 0; i < active->export_count; i++)
            if (!strcmp(active->exports[i].name, "_url_storage_check")) guest_check = (int (*)(int))(uintptr_t)active->exports[i].addr;
        if (!guest_check) fail("check export", -1);
        char path[2304], signature[8];
        find_cache_file(root, path, sizeof(path));
        FILE *file = fopen(path, "rb");
        if (!file || fread(signature, 1, sizeof(signature), file) != sizeof(signature) || memcmp(signature, "bplist00", 8)) fail("binary cache record", -1);
        fclose(file);
        int stage = 0;
        if ((result = chm_session_callback(active, callback, &stage))) fail("reopened disk cache", result);
        file = fopen(path, "wb");
        if (!file || fwrite("bplist00corrupt", 1, 15, file) != 15 || fclose(file)) fail("corrupt fixture", -1);
        stage = 1;
        if ((result = chm_session_callback(active, callback, &stage))) fail("malformed disk record", result);
        chm_session_destroy(active);
        active = NULL;
        if (descriptors() != baseline) fail("descriptor leak", descriptors() - baseline);
    }
    puts("url-storage: three sessions, guest behavior, persisted bplist, independent reopen, corrupt record rejection and descriptor cleanup passed");
    return 0;
}