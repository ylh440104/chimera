typedef __builtin_va_list guest_va_list;
extern int strcmp(const char *, const char *);

__attribute__((objc_root_class))
@interface NSString
+ (id)stringWithFormat:(NSString *)fmt, ...;
- (const char *)UTF8String;
@end

typedef struct { double d[5]; } Result;

@interface DispatchParent : NSString
+ (id)receiver;
+ (id)hostFormat;
+ (Result)fixed:(double)a b:(double)b c:(double)c d:(double)d e:(double)e f:(double)f g:(double)g h:(double)h;
@end

@interface DispatchChild : DispatchParent
+ (id)callSuper;
@end

static id expected_receiver;
static NSString *marker;

@implementation DispatchParent
+ (id)receiver { return self; }
+ (id)hostFormat { return [super stringWithFormat:@"super:%s/%@", "home", @"child"]; }
+ (id)stringWithFormat:(NSString *)fmt, ...
{
    if (self != expected_receiver || fmt != marker)
        return (id)0;
    guest_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = __builtin_va_arg(ap, int);
    double d = __builtin_va_arg(ap, double);
    NSString *str = __builtin_va_arg(ap, NSString *);
    __builtin_va_end(ap);
    return n == -23 && d == 1.25 ? str : (id)0;
}
+ (Result)fixed:(double)a b:(double)b c:(double)c d:(double)d e:(double)e f:(double)f g:(double)g h:(double)h
{
    return (Result){{a+b, c+d, e+f, g+h, self == expected_receiver}};
}
@end

@implementation DispatchChild
+ (id)callSuper { return [super stringWithFormat:marker, -23, 1.25, @"via-super"]; }
@end

static int equals(NSString *obj, const char *want)
{
    const char *s = [obj UTF8String];
    return s && !strcmp(s, want);
}

int main(void)
{
    expected_receiver = [DispatchChild receiver];
    marker = @"marker";
    if (!expected_receiver || !equals([DispatchChild stringWithFormat:marker, -23, 1.25, @"via-send"], "via-send"))
        return 31;
    if (!equals([DispatchChild callSuper], "via-super"))
        return 32;
    if (!equals([DispatchChild hostFormat], "super:home/child"))
        return 33;
    Result r = [DispatchChild fixed:1 b:2 c:4 d:8 e:16 f:32 g:64 h:128];
    return r.d[0] == 3 && r.d[1] == 12 && r.d[2] == 48 && r.d[3] == 192 && r.d[4] == 1 ? 0 : 34;
}
