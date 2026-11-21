typedef __builtin_va_list guest_va_list;

__attribute__((objc_root_class))
@interface NSString
+ (id)stringWithFormat:(NSString *)fmt, ...;
+ (id)localizedStringWithFormat:(NSString *)fmt, ...;
- (id)initWithFormat:(NSString *)fmt, ...;
- (id)initWithFormat:(NSString *)fmt arguments:(guest_va_list)args;
- (id)stringByAppendingFormat:(NSString *)fmt, ...;
- (const char *)UTF8String;
@end

__attribute__((objc_root_class))
@interface NSFileManager
+ (id)defaultManager;
- (signed char)createDirectoryAtPath:(NSString *)path withIntermediateDirectories:(signed char)create attributes:(id)attrs error:(id *)error;
@end

extern int strcmp(const char *, const char *);
extern int printf(const char *, ...);
extern void *fopen(const char *, const char *);
extern int fprintf(void *, const char *, ...);
extern int fclose(void *);

static int matches(NSString *str, const char *expected)
{
    const char *actual = [str UTF8String];
    return actual && strcmp(actual, expected) == 0;
}

__attribute__((noinline))
static NSString *forward_list(NSString *fmt, ...)
{
    guest_va_list args;
    __builtin_va_start(args, fmt);
    NSString *result = [@"" initWithFormat:fmt arguments:args];
    __builtin_va_end(args);
    return result;
}

int main(void)
{
    NSString *path = [NSString stringWithFormat:@"%s/Documents%@", "/var/mobile", @"/profiles"];
    if (!matches(path, "/var/mobile/Documents/profiles"))
        return 11;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:path withIntermediateDirectories:1 attributes:(id)0 error:(id *)0])
        return 12;
    if (!matches([NSString stringWithFormat:@"100%%"], "100%"))
        return 13;
    if (!matches([NSString localizedStringWithFormat:@"%d|%*.*f|%@|%lld", -3, 6, 2, 1.25, @"ok", 4294967297LL], "-3|  1.25|ok|4294967297"))
        return 14;
    if (!matches([@"prefix:" stringByAppendingFormat:@"%@/%s", @"dir", "file"], "prefix:dir/file"))
        return 15;
    if (!matches(forward_list(@"%s:%d:%.1Lf", "list", -7, (long double)2.5), "list:-7:2.5"))
        return 16;
    if (!matches([@"" initWithFormat:@"%d%d%d%d%d%d%d%d%d", 1,2,3,4,5,6,7,8,9], "123456789"))
        return 17;
    if (printf("guest:%d:%.1f", 17, 2.5) != 12)
        return 18;
    void *file = fopen("/var/mobile/Documents/profiles/abi.txt", "w");
    if (!file)
        return 19;
    int written = fprintf(file, "%s=%d %.2f\n", "guest", -7, 1.25);
    if (fclose(file) != 0 || written != 14)
        return 20;
    return 0;
}
