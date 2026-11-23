typedef unsigned long Size;
typedef signed char Bool;
@class NSData, NSURL, NSString;
__attribute__((objc_root_class))
@interface NSObject
@end
@interface NSString : NSObject
+ (id)stringWithFormat:(NSString *)format, ...;
- (const char *)UTF8String;
- (NSData *)dataUsingEncoding:(Size)encoding;
@end
@interface NSURL : NSObject
- (NSString *)path;
- (NSURL *)URLByAppendingPathComponent:(NSString *)name;
@end
@interface NSData : NSObject
+ (NSData *)dataWithContentsOfURL:(NSURL *)url;
- (Bool)writeToURL:(NSURL *)url atomically:(Bool)atomic;
- (Size)length;
- (const void *)bytes;
@end
@interface NSArray : NSObject
- (id)firstObject;
@end
@interface NSNumber : NSObject
- (long)integerValue;
@end
@interface NSDictionary : NSObject
- (NSNumber *)objectForKey:(id)key;
@end
@interface NSFileManager : NSObject
+ (id)defaultManager;
- (NSURL *)URLForDirectory:(Size)directory inDomain:(Size)domain appropriateForURL:(NSURL *)other create:(Bool)create error:(id *)error;
- (Bool)changeCurrentDirectoryPath:(NSString *)path;
- (NSDictionary *)attributesOfItemAtPath:(NSString *)path error:(id *)error;
@end
extern NSString *NSHomeDirectory(void);
extern NSArray *NSSearchPathForDirectoriesInDomains(Size, Size, Bool);
extern id NSFileSize, NSFilePosixPermissions;
extern char *getenv(const char *);
extern int setenv(const char *, const char *, int);
extern int strcmp(const char *, const char *), memcmp(const void *, const void *, Size);
extern int open(const char *, int, ...), close(int), access(const char *, int), fcntl(int, int, ...);
extern long read(int, void *, Size), write(int, const void *, Size);
extern long long lseek(int, long long, int);
extern int stat(const char *, void *), fstat(int, void *), remove(const char *), unlink(const char *);
extern int *__error(void);
typedef struct GuestFILE GuestFILE;
extern GuestFILE *fopen(const char *, const char *), *fdopen(int, const char *);
extern int fileno(GuestFILE *), fclose(GuestFILE *), ferror(GuestFILE *), feof(GuestFILE *);
extern void clearerr(GuestFILE *);
extern int fseek(GuestFILE *, long, int), fseeko(GuestFILE *, long long, int);
extern long ftell(GuestFILE *);
extern long long ftello(GuestFILE *);
extern Size fread(void *, Size, Size, GuestFILE *), fwrite(const void *, Size, Size, GuestFILE *);
extern void *opendir(const char *);
extern const unsigned char *readdir(void *);
extern int closedir(void *);

enum { WriteOnly = 1, ReadWrite = 2, Append = 8, Create = 0x200, Truncate = 0x400,
       Exclusive = 0x800, CloseOnExec = 0x1000000 };
typedef struct {
    unsigned long before;
    unsigned char data[144];
    unsigned long after;
} Status;
static const unsigned long guard = 0xa1b2c3d4a1b2c3d4UL;
static unsigned short u16(const unsigned char *p)
{
    unsigned short value;
    __builtin_memcpy(&value, p, sizeof(value));
    return value;
}
static unsigned long u64(const unsigned char *p)
{
    unsigned long value;
    __builtin_memcpy(&value, p, sizeof(value));
    return value;
}
static int matching(NSString *value, const char *expected)
{
    const char *bytes = [value UTF8String];
    return bytes && !strcmp(bytes, expected);
}
static int status_matches(const Status *s, unsigned mode, unsigned long size)
{
    return s->before == guard && s->after == guard && u16(s->data + 4) == (0100000 | mode) &&
           u16(s->data + 6) == 1 && u64(s->data + 8) && u64(s->data + 48) &&
           u64(s->data + 56) < 1000000000 && u64(s->data + 96) == size;
}

static int check_directory(unsigned long inode)
{
    void *dir = opendir(".");
    if (!dir) return 21;
    int found = 0;
    *__error() = 0;
    const unsigned char *entry;
    while ((entry = readdir(dir))) {
        if (strcmp((const char *)entry + 21, "notice.txt")) continue;
        if (u64(entry) != inode || u16(entry + 18) != 10 || u16(entry + 16) < 32 ||
            (entry[20] != 8 && entry[20] != 0)) return 22;
        found++;
    }
    if (*__error() || found != 1 || closedir(dir)) return 23;
    return 0;
}

static int check_foundation_write(NSFileManager *fm, NSURL *docs, NSString *mapped)
{
    NSURL *file = [docs URLByAppendingPathComponent:@"notice.txt"];
    NSData *sample = [@"round trip" dataUsingEncoding:4];
    if ([sample length] != 10 || ![sample writeToURL:file atomically:1]) return 10;
    if (![fm changeCurrentDirectoryPath:[docs path]] || access("notice.txt", 4)) return 11;
    Status path = {.before=guard, .after=guard}, fdstat = {.before=guard, .after=guard};
    int fd = open("./notice.txt", 0);
    char bytes[10];
    if (fd < 0 || read(fd, bytes, sizeof(bytes)) != 10 || memcmp(bytes, "round trip", 10)) return 12;
    if (fstat(fd, fdstat.data) || !status_matches(&fdstat, 0600, 10) || close(fd)) return 13;
    const char *names[] = {[[file path] UTF8String], "/private/var/mobile/Profiles/Secondary/Documents/notice.txt", [mapped UTF8String]};
    for (Size i = 0; i < sizeof(names) / sizeof(*names); i++) {
        if (access(names[i], 4) || stat(names[i], path.data) || !status_matches(&path, 0600, 10) ||
            u64(path.data + 8) != u64(fdstat.data + 8)) return 14;
    }
    GuestFILE *fp = fopen("notice.txt", "rb");
    if (!fp || fseek(fp, 6, 0) || ftell(fp) != 6 || fread(bytes, 1, 4, fp) != 4 || memcmp(bytes, "trip", 4)) return 15;
    if (fread(bytes, 1, 1, fp) || !feof(fp) || ferror(fp)) return 16;
    if (fseeko(fp, -10, 2) || feof(fp) || ftello(fp) != 0 || fclose(fp)) return 17;
    return check_directory(u64(path.data + 8));
}

static int check_c_write(NSFileManager *fm, NSURL *docs)
{
    const char *name = "created.bin";
    const unsigned char sample[] = {0, 0xfe, 9, 0x41};
    char last = '!';
    int fd = open(name, Create | Exclusive | ReadWrite | CloseOnExec, 0666);
    if (fd < 0 || fcntl(fd, 1) != 1 || write(fd, sample, sizeof(sample)) != sizeof(sample)) return 30;
    if (open(name, Create | Exclusive | WriteOnly, 0600) != -1 || *__error() != 17) return 31;
    Status s = {.before=guard, .after=guard};
    if (fstat(fd, s.data) || !status_matches(&s, 0640, 4) || close(fd)) return 32;
    fd = open(name, WriteOnly | Append);
    if (fd < 0 || lseek(fd, 0, 0) || write(fd, &last, 1) != 1 || close(fd)) return 33;
    NSURL *url = [docs URLByAppendingPathComponent:@"created.bin"];
    NSData *data = [NSData dataWithContentsOfURL:url];
    if ([data length] != 5 || memcmp([data bytes], sample, 4) || ((const char *)[data bytes])[4] != last) return 34;
    id error = (id)0;
    NSDictionary *attrs = [fm attributesOfItemAtPath:[url path] error:&error];
    if (error || [[attrs objectForKey:NSFileSize] integerValue] != 5 ||
        [[attrs objectForKey:NSFilePosixPermissions] integerValue] != 0640) return 35;
    if (open("absent/child", Create | WriteOnly, 0600) != -1 || *__error() != 2) return 36;
    if (access("absent", 0) != -1 || *__error() != 2 || fstat(-1, s.data) != -1 || *__error() != 9) return 37;
    return 0;
}

static int check_offsets(void)
{
    const long long offset = 4294967313LL;
    Status s = {.before=guard, .after=guard};
    int fd = open("wide.tmp", Create | Exclusive | ReadWrite, 0600);
    if (fd < 0 || lseek(fd, offset, 0) != offset || write(fd, "Z", 1) != 1) return 40;
    if (lseek(fd, 0, 1) != offset + 1 || fstat(fd, s.data) || !status_matches(&s, 0600, offset + 1)) return 41;
    GuestFILE *fp = fdopen(fd, "r+b");
    char byte = 0;
    if (!fp || fileno(fp) != fd || ftello(fp) != offset + 1 || fseeko(fp, -1, 2)) return 42;
    if (ftell(fp) != offset || fread(&byte, 1, 1, fp) != 1 || byte != 'Z') return 43;
    if (fseek(fp, offset - 1, 0) || ftello(fp) != offset - 1 || fread(&byte, 1, 1, fp) != 1 || byte) return 44;
    if (fclose(fp) || fstat(fd, s.data) != -1 || *__error() != 9) return 45;
    fd = open("wide.tmp", WriteOnly | Truncate);
    if (fd < 0 || fstat(fd, s.data) || !status_matches(&s, 0600, 0)) return 46;
    fp = fdopen(fd, "w");
    if (!fp || fread(&byte, 1, 1, fp) || !ferror(fp) || feof(fp)) return 47;
    clearerr(fp);
    if (ferror(fp) || fwrite("x", 1, 1, fp) != 1 || fclose(fp) || remove("wide.tmp")) return 48;
    if (unlink("wide.tmp") != -1 || *__error() != 2) return 49;
    return 0;
}

int main(void)
{
    const char *initial = getenv("HOME");
    if (!initial || !matching(NSHomeDirectory(), "/var/mobile")) return 1;
    NSString *mapped = [NSString stringWithFormat:@"%s/Profiles/Secondary/Documents/notice.txt", initial];
    const char *base = "/var/mobile/Profiles/Secondary";
    if (setenv("HOME", base, 1) || !matching(NSHomeDirectory(), base)) return 2;
    NSFileManager *fm = [NSFileManager defaultManager];
    id error = (id)0;
    NSURL *docs = [fm URLForDirectory:9 inDomain:1 appropriateForURL:(id)0 create:1 error:&error];
    if (error || !matching([docs path], "/var/mobile/Profiles/Secondary/Documents")) return 3;
    if (!matching([NSSearchPathForDirectoriesInDomains(9, 1, 0) firstObject], "~/Documents")) return 4;
    int rc = check_foundation_write(fm, docs, mapped);
    if (!rc) rc = check_c_write(fm, docs);
    if (!rc) rc = check_offsets();
    return rc;
}
