typedef unsigned long Size;
__attribute__((objc_root_class))
@interface NSObject
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (_Bool)isKindOfClass:(Class)cls;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)string;
- (const char *)UTF8String;
- (id)dataUsingEncoding:(Size)encoding;
@end
@interface NSMutableString : NSString
- (void)appendFormat:(NSString *)format, ...;
@end
@interface NSNumber : NSObject
+ (id)numberWithInt:(int)value;
+ (id)numberWithUnsignedLongLong:(unsigned long long)value;
+ (id)numberWithDouble:(double)value;
+ (id)numberWithBool:(_Bool)value;
- (long long)longLongValue;
- (unsigned long long)unsignedLongLongValue;
- (double)doubleValue;
- (_Bool)boolValue;
@end
@interface NSData : NSObject
+ (id)dataWithBytes:(const void *)bytes length:(Size)length;
+ (id)dataWithContentsOfFile:(NSString *)path;
- (const void *)bytes;
- (Size)length;
- (_Bool)writeToFile:(NSString *)path atomically:(_Bool)atomic;
@end
@interface NSDate : NSObject
+ (id)dateWithTimeIntervalSince1970:(double)value;
- (double)timeIntervalSince1970;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(Size)count;
- (id)objectAtIndexedSubscript:(Size)index;
- (Size)count;
@end
@interface NSDictionary : NSObject
+ (id)dictionaryWithObjects:(const id *)objects forKeys:(const id *)keys count:(Size)count;
- (id)objectForKeyedSubscript:(id)key;
@end
@interface NSMutableDictionary : NSDictionary
- (void)setObject:(id)object forKey:(id)key;
@end
@interface NSPropertyListSerialization : NSObject
+ (id)propertyListWithData:(NSData *)data options:(Size)options format:(Size *)format error:(id *)error;
+ (NSData *)dataWithPropertyList:(id)object format:(Size)format options:(Size)options error:(id *)error;
+ (_Bool)propertyList:(id)object isValidForFormat:(Size)format;
@end
extern int strcmp(const char *, const char *), memcmp(const void *, const void *, Size);
extern void *memcpy(void *, const void *, Size);

static const unsigned char payload[5] = {0,255,0,17,42};
static int check(NSDictionary *object)
{
    if (!object || strcmp([object[@"unicode"] UTF8String], "中文 😀<&>")) return 1;
    if ([object[@"signed"] longLongValue] != -123456789012345LL || [object[@"unsigned"] unsignedLongLongValue] != 0xfedcba9876543210ULL) return 2;
    if ([object[@"real"] doubleValue] != 1.25 || ![object[@"yes"] boolValue] || [object[@"no"] boolValue]) return 3;
    NSData *bytes = object[@"data"];
    if ([bytes length] != 5 || memcmp([bytes bytes], payload, 5)) return 4;
    if ([object[@"date"] timeIntervalSince1970] != 1704067200.0) return 5;
    NSArray *array = object[@"array"];
    if ([array count] != 3 || [array[0] longLongValue] != 7 || strcmp([array[1] UTF8String], "nested") || [array[2] boolValue]) return 6;
    return 0;
}

int main(void)
{
    id error = (id)0;
    for (int i = 0; i < 2; i++) {
        NSString *path = i ? @"/var/mobile/input.binary.plist" : @"/var/mobile/input.xml.plist";
        NSData *input = [NSData dataWithContentsOfFile:path];
        Size format = 0;
        NSDictionary *object = [NSPropertyListSerialization propertyListWithData:input options:0 format:&format error:&error];
        if (error || format != (i ? 200ul : 100ul)) return 1;
        int rc = check(object); if (rc) return 10 + rc;
        if (![NSPropertyListSerialization propertyList:object isValidForFormat:100] || ![NSPropertyListSerialization propertyList:object isValidForFormat:200]) return 2;
        for (int j = 0; j < 2; j++) {
            NSData *output = [NSPropertyListSerialization dataWithPropertyList:object format:j ? 200 : 100 options:0 error:&error];
            if (!output || error) return 3;
            Size detected = 0;
            NSDictionary *copy = [NSPropertyListSerialization propertyListWithData:output options:0 format:&detected error:&error];
            if (error || detected != (j ? 200ul : 100ul) || check(copy)) return 4;
            if (i && ![output writeToFile:j ? @"/var/mobile/output.binary.plist" : @"/var/mobile/output.xml.plist" atomically:1]) return 5;
        }
        NSDictionary *mutable = [NSPropertyListSerialization propertyListWithData:input options:2 format:0 error:&error];
        if (!mutable || ![mutable isKindOfClass:[NSMutableDictionary class]] || ![mutable[@"unicode"] isKindOfClass:[NSMutableString class]]) return 6;
        [(NSMutableString *)mutable[@"unicode"] appendFormat:@" %@", @"tail"];
        if (strcmp([mutable[@"unicode"] UTF8String], "中文 😀<&> tail")) return 7;
    }
    const char *bad[] = {"<plist><dict><key>x</key></dict></plist>", "<plist><array><key>x</key></array></plist>",
        "<plist><string><string>x</string></string></plist>", "<plist><data>AA=A</data></plist>",
        "<plist><date>2024-02-30T00:00:00Z</date></plist>", "<plist><integer>18446744073709551616</integer></plist>",
        "<plist><true>yes</true></plist>", "bplist00", "<plist><real>nan</real></plist>"};
    for (Size i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        Size format = 77;
        error = (id)0;
        NSData *data = [[NSString stringWithUTF8String:bad[i]] dataUsingEncoding:4];
        if ([NSPropertyListSerialization propertyListWithData:data options:0 format:&format error:&error] || !error || format != 77) return 30 + (int)i;
    }
    NSData *binary = [NSData dataWithContentsOfFile:@"/var/mobile/input.binary.plist"];
    unsigned char corrupt[4096];
    Size length = [binary length]; if (length < 40 || length > sizeof(corrupt)) return 40;
    memcpy(corrupt, [binary bytes], length); corrupt[length - 1] = 0xff;
    if ([NSPropertyListSerialization propertyListWithData:[NSData dataWithBytes:corrupt length:length] options:0 format:0 error:&error] || !error) return 41;
    if ([NSPropertyListSerialization dataWithPropertyList:[NSObject new] format:200 options:0 error:&error] || !error) return 42;
    if ([NSPropertyListSerialization dataWithPropertyList:@"text" format:1 options:0 error:&error] || !error) return 43;
    return 0;
}