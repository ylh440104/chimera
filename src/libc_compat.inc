extern int vasprintf(char **, const char *, va_list);
extern void chm_sscanf_entry(void);
extern void chm_sprintf_chk_entry(void);
extern void chm_snprintf_entry(void);

typedef struct {
    void *stack, *gr_top, *vr_top;
    int gr_offset, vr_offset;
} chm_host_va_state;

_Static_assert(sizeof(void *) == 8 && sizeof(va_list) == 32 &&
               sizeof(chm_host_va_state) == 32 && offsetof(chm_host_va_state, gr_offset) == 24,
               "AAPCS64 variadic state required");

static char *libc_format(const char *format, int scanning)
{
    if (!format) { file_failure(EINVAL); return NULL; }
    size_t length = strlen(format);
    if (length > (SIZE_MAX - 1) / 2) { file_failure(EOVERFLOW); return NULL; }
    char *translated = malloc(length * 2 + 1);
    if (!translated) { file_failure(ENOMEM); return NULL; }
    const char *p = format;
    char *out = translated;
    while (*p) {
        if (*p != '%') { *out++ = *p++; continue; }
        *out++ = *p++;
        if (*p == '%') { *out++ = *p++; continue; }
        const char *prefix = scanning ? "0123456789$*'" : "0123456789$*'.-+ #";
        while (*p && strchr(prefix, *p)) *out++ = *p++;
        const char *modifier = p;
        if (*p && strchr("hljztLq", *p)) {
            char first = *p++;
            if ((first == 'h' || first == 'l') && *p == first) p++;
        }
        size_t modifiers = (size_t)(p - modifier);
        char conversion = *p;
        if (!conversion) goto invalid;
        p++;
        int floating = strchr("aAeEfFgG", conversion) != NULL;
        if (modifiers && *modifier == 'L') {
            if (!floating) goto invalid;
            if (scanning) *out++ = 'l';
        } else if (modifiers && *modifier == 'q') {
            if (!strchr("diouxXn", conversion)) goto invalid;
            *out++ = 'l'; *out++ = 'l';
        } else {
            memcpy(out, modifier, modifiers);
            out += modifiers;
        }
        if (strchr("DOUCS", conversion)) {
            if (modifiers) goto invalid;
            *out++ = 'l';
            conversion = conversion == 'D' ? 'd' : conversion == 'O' ? 'o' :
                         conversion == 'U' ? 'u' : conversion == 'C' ? 'c' : 's';
        }
        if (scanning && conversion == '[') {
            *out++ = conversion;
            if (*p == '^') *out++ = *p++;
            if (*p == ']') *out++ = *p++;
            while (*p && *p != ']') *out++ = *p++;
            if (*p != ']') goto invalid;
            *out++ = *p++;
        } else {
            if (!strchr("diouxXaAeEfFgGcspn%", conversion)) goto invalid;
            *out++ = conversion;
        }
    }
    *out = 0;
    return translated;
invalid:
    free(translated);
    file_failure(EINVAL);
    return NULL;
}

int chm_snprintf_list(char *buffer, size_t capacity, const char *format, const void *slots)
{
    if (!buffer && capacity) return file_failure(EINVAL);
    char *translated = libc_format(format, 0);
    if (!translated) return -1;
    chm_host_va_state state = {.stack = (void *)slots};
    va_list args;
    memcpy(&args, &state, sizeof(args));
    int saved = errno;
    errno = 0;
    int result = vsnprintf(buffer, capacity, translated, args);
    int error = errno;
    va_end(args);
    free(translated);
    if (error) file_failure(error);
    else errno = saved;
    return result;
}

int chm_sscanf_list(const char *input, const char *format, const void *slots)
{
    if (!input) return file_failure(EINVAL);
    char *translated = libc_format(format, 1);
    if (!translated) return -1;
    chm_host_va_state state = {.stack = (void *)slots};
    va_list args;
    memcpy(&args, &state, sizeof(args));
    int saved = errno;
    errno = 0;
    int result = vsscanf(input, translated, args);
    int error = errno;
    va_end(args);
    free(translated);
    if (error) file_failure(error);
    else errno = saved;
    return result;
}

int chm_sprintf_chk_list(char *buffer, int flags, size_t capacity, const char *format, const void *slots)
{
    (void)flags;
    if (!buffer) return file_failure(EINVAL);
    char *translated = libc_format(format, 0);
    if (!translated) return -1;
    chm_host_va_state state = {.stack = (void *)slots};
    va_list args;
    memcpy(&args, &state, sizeof(args));
    char *text = NULL;
    int count = vasprintf(&text, translated, args);
    int error = errno;
    va_end(args);
    free(translated);
    if (count < 0) { free(text); return file_failure(error); }
    if ((size_t)count >= capacity) {
        free(text);
        chm_session_log(g_null_log_session, "libc: __sprintf_chk buffer overflow");
        abort();
    }
    memcpy(buffer, text, (size_t)count + 1);
    free(text);
    return count;
}

static void rewind_impl(FILE *stream)
{
    if (!stream) { file_failure(EBADF); return; }
    fseeko_impl(stream, 0, SEEK_SET);
    clearerr_impl(stream);
}

static int posix_memalign_impl(void **out, size_t alignment, size_t size)
{
    if (!out || alignment < sizeof(void *) || (alignment & (alignment - 1))) return io_errno(EINVAL);
    void *memory = NULL;
    int saved = errno;
    int result = posix_memalign(&memory, alignment, size);
    errno = saved;
    if (result) return io_errno(result);
    *out = memory;
    return 0;
}

static int unsetenv_impl(const char *name)
{
    if (!name || !*name || strchr(name, '=')) return file_failure(EINVAL);
    return unsetenv(name) ? file_failure(errno) : 0;
}

static uint32_t arc4random_impl(void)
{
    int saved = errno;
    int fd;
    do fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC); while (fd < 0 && errno == EINTR);
    if (fd < 0) abort();
    uint32_t value;
    size_t offset = 0;
    while (offset < sizeof(value)) {
        ssize_t count = read(fd, (unsigned char *)&value + offset, sizeof(value) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); abort(); }
        offset += (size_t)count;
    }
    close(fd);
    errno = saved;
    return value;
}

static unsigned char *getsectiondata_impl(const void *header, const char *segment, const char *section, unsigned long *size)
{
    if (size) *size = 0;
    if (!header || !segment || !section || !size || !g_null_log_session) return NULL;
    for (int i = 0; i < g_null_log_session->image_count; i++) {
        chm_image *image = &g_null_log_session->images[i];
        if (header != (const void *)(uintptr_t)(image->vm_min + image->slide)) continue;
        for (uint32_t j = 0; j < image->nsegs; j++) {
            const chm_segment *seg = &image->segs[j];
            if (strcmp(segment, seg->name) || !seg->mapped) continue;
            for (uint32_t k = 0; k < seg->nsects; k++) {
                const chm_section *entry = &seg->sects[k];
                if (strcmp(section, entry->sect) || entry->addr < seg->vmaddr) continue;
                uint64_t offset = entry->addr - seg->vmaddr;
                if (offset > seg->vmsize || entry->size > seg->vmsize - offset) return NULL;
                *size = entry->size;
                return (unsigned char *)seg->mapped + offset;
            }
        }
    }
    return NULL;
}
