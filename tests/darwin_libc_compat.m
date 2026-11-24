typedef unsigned long Size;
typedef __builtin_va_list Args;
typedef struct GuestFILE GuestFILE;
extern int sscanf(const char *, const char *, ...), vsscanf(const char *, const char *, Args);
extern int __sprintf_chk(char *, int, Size, const char *, ...);
extern int __vsprintf_chk(char *, int, Size, const char *, Args);
extern int strcmp(const char *, const char *), memcmp(const void *, const void *, Size);
extern int setenv(const char *, const char *, int), unsetenv(const char *), abs(int);
extern char *getenv(const char *);
extern int *__error(void);
extern int posix_memalign(void **, Size, Size);
extern void free(void *);
extern unsigned arc4random(void);
extern GuestFILE *fopen(const char *, const char *);
extern int fclose(GuestFILE *), fseek(GuestFILE *, long, int), feof(GuestFILE *), ferror(GuestFILE *);
extern long ftell(GuestFILE *);
extern Size fread(void *, Size, Size, GuestFILE *), fwrite(const void *, Size, Size, GuestFILE *);
extern void rewind(GuestFILE *);
extern GuestFILE *__stdoutp;
extern int open(const char *, int, ...), dup2(int, int), close(int);
extern const void *_dyld_get_image_header(unsigned);
extern unsigned char *getsectiondata(const void *, const char *, const char *, Size *);

__attribute__((used,section("__DATA,__chm_probe")))
static const unsigned char section_probe[] = {0x19,0x42,0xfe,0x81,0x00,0x53};
static char overflow_buffer[4] = {'Q','Q','Q','Q'};

static int scan_list(const char *input, const char *format, ...)
{
    Args args;
    __builtin_va_start(args, format);
    int result = vsscanf(input, format, args);
    __builtin_va_end(args);
    return result;
}

static int print_list(char *buffer, Size capacity, const char *format, ...)
{
    Args args;
    __builtin_va_start(args, format);
    int result = __vsprintf_chk(buffer, 0, capacity, format, args);
    __builtin_va_end(args);
    return result;
}

static int check_scan(void)
{
    int a = 0, n = -1;
    long b = 0;
    double d = 0;
    char text[8] = {0}, ch = 0;
    if (sscanf("-12 4294967301 1.25 abc!", "%d %ld %lf %3[a-z]%c%n", &a, &b, &d, text, &ch, &n) != 5) return 1;
    if (a != -12 || b != 4294967301L || d != 1.25 || strcmp(text, "abc") || ch != '!' || n != (int)sizeof("-12 4294967301 1.25 abc!") - 1) return 2;
    int values[9] = {0};
    if (sscanf("1 2 3 4 5 6 7 8 9", "%d %d %d %d %d %d %d %d %d", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8]) != 9) return 3;
    for (int i = 0; i < 9; i++) if (values[i] != i + 1) return 4;
    if (scan_list("10 20", "%2$d %1$d", &values[0], &values[1]) != 2 || values[0] != 20 || values[1] != 10) return 5;
    long double extended = 0;
    unsigned long guard = 0x1122334455667788UL;
    struct { long double value; unsigned long guard; } guarded = {0,guard};
    if (sscanf("2.5", "%Lf", &guarded.value) != 1 || guarded.value != 2.5 || guarded.guard != guard) return 6;
    if (scan_list("skip -4 0x1.8p+1", "%*s %d %La", &a, &extended) != 2 || a != -4 || extended != 3.0) return 7;
    if (sscanf("", "%d", &a) != -1 || sscanf("no", "%d", &a) != 0) return 8;
    if (sscanf("]ab9", "%3[]ab]%d", text, &a) != 2 || strcmp(text,"]ab") || a != 9) return 9;
    return 0;
}

static int check_print(void)
{
    char buffer[2048];
    int count = -1;
    int result = __sprintf_chk(buffer, 0, sizeof(buffer), "%d/%.2f/%s/%ld%n", -7, 1.25, "ok", 4294967301L, &count);
    if (result != (int)sizeof("-7/1.25/ok/4294967301") - 1 || count != result || strcmp(buffer, "-7/1.25/ok/4294967301")) return 10;
    if (print_list(buffer, sizeof(buffer), "%2$d:%1$.1Lf", (long double)3.5, 17) != 6 || strcmp(buffer, "17:3.5")) return 11;
    if (__sprintf_chk(buffer, 0, sizeof(buffer), "%*.*f", 8, 3, 1.25) != 8 || strcmp(buffer, "   1.250")) return 12;
    if (print_list(buffer, sizeof(buffer), "%0600.2f", 1.25) != 600 || buffer[0] != '0' || strcmp(buffer + 596, "1.25")) return 13;
    if (__sprintf_chk(buffer, 0, sizeof(buffer), "%d%d%d%d%d%d%d%d%d", 1,2,3,4,5,6,7,8,9) != 9 || strcmp(buffer, "123456789")) return 14;
    return 0;
}

static int check_file(void)
{
    GuestFILE *file = fopen("/var/mobile/Documents/rewind-test", "w+b");
    char text[8] = {0};
    if (!file || fwrite("abc",1,3,file) != 3 || fseek(file,0,0) || fread(text,1,8,file) != 3 || !feof(file)) return 20;
    rewind(file);
    if (feof(file) || ferror(file) || ftell(file) || fread(text,1,3,file) != 3 || memcmp(text,"abc",3) || fclose(file)) return 21;
    int fd = open("/var/mobile/Documents/rewind-output", 0x602, 0600);
    if (fd < 0 || dup2(fd,1) != 1 || close(fd)) return 22;
    if (fwrite("abc",1,3,__stdoutp) != 3 || fseek(__stdoutp,0,0) || fread(text,1,8,__stdoutp) != 3 || !feof(__stdoutp)) return 23;
    rewind(__stdoutp);
    if (feof(__stdoutp) || ferror(__stdoutp) || ftell(__stdoutp) || fread(text,1,3,__stdoutp) != 3 || memcmp(text,"abc",3)) return 24;
    return 0;
}

static int check_misc(void)
{
    void *memory = (void *)0x1234;
    *__error() = 99;
    if (posix_memalign(&memory,3,32) != 22 || memory != (void *)0x1234 || *__error() != 99) return 30;
    if (posix_memalign(&memory,64,1024) || !memory || ((Size)memory & 63)) return 31;
    ((char *)memory)[1023] = 7;
    free(memory);
    if (abs(-17) != 17 || abs(0) || abs(23) != 23) return 32;
    if (setenv("CHM_COMPAT_TEST", "present", 1) || !getenv("CHM_COMPAT_TEST") || unsetenv("CHM_COMPAT_TEST") || getenv("CHM_COMPAT_TEST")) return 33;
    if (unsetenv("bad=name") != -1 || *__error() != 22 || unsetenv("" ) != -1 || *__error() != 22) return 34;
    const void *header = _dyld_get_image_header(0);
    Size size = 0;
    unsigned char *bytes = getsectiondata(header, "__DATA", "__chm_probe", &size);
    if (!bytes || size != sizeof(section_probe) || memcmp(bytes,section_probe,size)) return 35;
    size = 123;
    if (getsectiondata(header,"__DATA","__not_present",&size) || size) return 36;
    size = 123;
    if (getsectiondata((void *)0x1234,"__DATA","__chm_probe",&size) || size) return 37;
    *__error() = 99;
    for (int i = 0; i < 64; i++) arc4random();
    if (*__error() != 99) return 38;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1],"overflow")) {
        __sprintf_chk(overflow_buffer, 0, sizeof(overflow_buffer), "%s", "abcdef");
        return 90;
    }
    int rc = check_scan();
    if (!rc) rc = check_print();
    if (!rc) rc = check_file();
    if (!rc) rc = check_misc();
    return rc;
}

int libc_buffer_intact(void)
{
    return overflow_buffer[0] == 'Q' && overflow_buffer[1] == 'Q' && overflow_buffer[2] == 'Q' && overflow_buffer[3] == 'Q';
}
