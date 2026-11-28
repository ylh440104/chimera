typedef signed char BOOL;
typedef unsigned long NSUInteger;
typedef unsigned char u8;

__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
- (id)init;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
@end

@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)text;
- (const char *)UTF8String;
@end

@interface NSSet : NSObject
+ (id)setWithObjects:(id)first, ...;
- (NSUInteger)count;
- (BOOL)containsObject:(id)object;
- (id)allObjects;
- (void)enumerateObjectsUsingBlock:(void (^)(id object, BOOL *stop))block;
@end

@interface NSMutableSet : NSSet
+ (id)set;
- (void)addObject:(id)object;
@end

@interface NSArray : NSObject
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
@end

@interface NSNumber : NSObject
+ (NSNumber *)numberWithUnsignedInteger:(NSUInteger)value;
- (NSUInteger)unsignedIntegerValue;
@end

@interface NSMutableDictionary : NSObject
+ (id)dictionary;
- (void)setObject:(id)object forKeyedSubscript:(id)key;
- (id)objectForKeyedSubscript:(id)key;
- (NSUInteger)count;
@end

@interface NSMutableData : NSObject
+ (id)data;
- (void)appendData:(id)data;
- (void)appendBytes:(const void *)bytes length:(NSUInteger)length;
- (NSUInteger)length;
- (const void *)bytes;
@end

@interface NSURL : NSObject
+ (id)URLWithString:(NSString *)string;
+ (id)URLWithString:(NSString *)string relativeToURL:(NSURL *)base;
- (NSString *)absoluteString;
@end

extern int puts(const char *);

static int same_text(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right && *left == *right) { left++; right++; }
    return *left == *right;
}

static int support_check(void)
{
    NSString *get = [NSString stringWithUTF8String:"GET"];
    NSString *head = [NSString stringWithUTF8String:"HEAD"];
    NSString *again = [NSString stringWithUTF8String:"GET"];
    NSSet *methods = [NSSet setWithObjects:get, head, again, 0];
    if ([methods count] != 2) return 1;
    if (![methods containsObject:head]) return 2;
    if ([methods containsObject:[NSString stringWithUTF8String:"POST"]]) return 3;
    if ([[methods allObjects] count] != 2) return 4;

    __block NSUInteger seen = 0;
    [methods enumerateObjectsUsingBlock:^(id object, BOOL *stop) {
        (void)object;
        (void)stop;
        seen++;
    }];
    if (seen != 2) return 5;

    NSMutableSet *mutable = [NSMutableSet set];
    [mutable addObject:get];
    [mutable addObject:again];
    [mutable addObject:head];
    if ([mutable count] != 2) return 6;

    NSMutableData *data = [NSMutableData data];
    [data appendBytes:"ab" length:2];
    [data appendBytes:"cd" length:2];
    if ([data length] != 4) return 7;
    if (((const u8 *)[data bytes])[3] != 'd') return 8;

    NSURL *base = [NSURL URLWithString:[NSString stringWithUTF8String:"https://example.com/api/"]];
    NSURL *relative = [NSURL URLWithString:[NSString stringWithUTF8String:"v2/status"] relativeToURL:base];
    if (!same_text([[relative absoluteString] UTF8String], "https://example.com/api/v2/status")) return 9;

    NSURL *absolute = [NSURL URLWithString:[NSString stringWithUTF8String:"https://example.com/root"] relativeToURL:base];
    if (!same_text([[absolute absoluteString] UTF8String], "https://example.com/root")) return 10;

    NSMutableDictionary *map = [NSMutableDictionary dictionary];
    NSNumber *value = [NSNumber numberWithUnsignedInteger:7];
    [map setObject:value forKeyedSubscript:[NSNumber numberWithUnsignedInteger:9]];
    if ([map count] != 1) return 11;
    if ([map objectForKeyedSubscript:[NSNumber numberWithUnsignedInteger:9]] != value) return 12;
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int result = support_check();
    if (result) return result;
    puts("support: sets, mutable sets, block enumeration, mutable data and relative URLs passed");
    return 0;
}