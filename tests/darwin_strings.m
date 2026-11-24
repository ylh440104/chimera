typedef unsigned long Size;
typedef signed char BOOL;
typedef struct { long location, length; } Range;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (Class)class;
- (id)copy;
- (id)mutableCopy;
- (BOOL)isKindOfClass:(Class)value;
- (void)release;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)value;
- (const char *)UTF8String;
- (Size)length;
- (BOOL)canBeConvertedToEncoding:(Size)encoding;
@end
@interface NSMutableString : NSString
@end
extern id CFStringCreateMutableCopy(void *, long, id);
extern id CFStringCreateCopy(void *, id);
extern long CFStringGetLength(id);
extern const char *CFStringGetCStringPtr(id, unsigned int);
extern unsigned char CFStringGetCString(id, char *, long, unsigned int);
extern unsigned char CFStringTransform(id, Range *, id, unsigned char);
extern id CFRetain(id);
extern void CFRelease(id);
extern int strcmp(const char *, const char *);
extern int memcmp(const void *, const void *, Size);
extern int puts(const char *);

static int same(id string, const char *expected)
{
    const char *actual = string ? [string UTF8String] : "<nil>";
    if (!strcmp(actual, expected)) return 1;
    puts(actual);
    puts(expected);
    return 0;
}

int main(void)
{
    NSString *source = @"Chimera/2.2 (中文; café 😀)";
    if ([source canBeConvertedToEncoding:1] || ![source canBeConvertedToEncoding:4]) return 101;
    id user_agent = [source mutableCopy];
    if (!user_agent || ![user_agent isKindOfClass:[NSMutableString class]]) return 102;
    if (!CFStringTransform(user_agent, 0, @"Any-Latin; Latin-ASCII; [:^ASCII:] Remove", 0)) return 103;
    if (!same(user_agent, "Chimera/2.2 (zhong wen; cafe )") || ![user_agent canBeConvertedToEncoding:1]) return 104;
    if (!same(source, "Chimera/2.2 (中文; café 😀)")) return 105;
    [user_agent release];
    if (CFStringGetLength(@"A😀é中") != 5 || [@"A😀é中" length] != 5) return 106;
    id copied = [@"A😀é中" copy];
    if (!copied || [copied isKindOfClass:[NSMutableString class]] || [copied length] != 5) return 107;
    if (CFStringTransform(copied, 0, @"Latin-ASCII", 0)) return 108;
    [copied release];
    id partial = CFStringCreateMutableCopy(0, 0, @"😀straße|é");
    Range range = {2, 6};
    if (!CFStringTransform(partial, &range, @"Latin-ASCII", 0) || range.location != 2 || range.length != 7) return 109;
    if (!same(partial, "😀strasse|é") || CFStringGetLength(partial) != 11 || [partial length] != 11) return 110;
    range = (Range){9, 2};
    if (!CFStringTransform(partial, &range, @"Latin-ASCII", 0) || range.length != 2 || !same(partial, "😀strasse|e")) return 111;
    Range invalid[] = {{-1, 2}, {0, -1}, {12, 0}, {10, 2}, {1, 0x7fffffffffffffffL}};
    for (Size i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        Range saved = invalid[i];
        if (CFStringTransform(partial, &invalid[i], @"Latin-ASCII", 0) || invalid[i].location != saved.location || invalid[i].length != saved.length) return 112;
    }
    range = (Range){2, 7};
    if (CFStringTransform(partial, &range, @"Invalid-Chimera-Transform", 0) || range.length != 7 || !same(partial, "😀strasse|e")) return 113;
    range = (Range){11, 0};
    if (!CFStringTransform(partial, &range, @"Latin-ASCII", 0) || range.length != 0 || !same(partial, "😀strasse|e")) return 114;
    CFRelease(partial);
    id combining = CFStringCreateMutableCopy(0, 0, @"x e\u0301 y");
    range = (Range){2, 2};
    if (!CFStringTransform(combining, &range, @"NFD; [:Mn:] Remove; NFC", 0) || range.length != 1 || !same(combining, "x e y")) return 115;
    CFRelease(combining);
    id reversible = CFStringCreateMutableCopy(0, 0, @"ABC123");
    if (!CFStringTransform(reversible, 0, @"Fullwidth-Halfwidth", 1) || !same(reversible, "ＡＢＣ１２３")) return 116;
    if (!CFStringTransform(reversible, 0, @"Fullwidth-Halfwidth", 0) || !same(reversible, "ABC123")) return 117;
    if (CFRetain(reversible) != reversible) return 118;
    CFRelease(reversible);
    if (!same(reversible, "ABC123")) return 119;
    CFRelease(reversible);
    char long_text[2049];
    for (int i = 0; i < 1024; i++) { long_text[2 * i] = (char)0xc3; long_text[2 * i + 1] = (char)0x9f; }
    long_text[2048] = 0;
    id expanded = [[NSString stringWithUTF8String:long_text] mutableCopy];
    range = (Range){0, 1024};
    if (!CFStringTransform(expanded, &range, @"Latin-ASCII", 0) || range.length != 2048 || [expanded length] != 2048) return 120;
    const char *bytes = [expanded UTF8String];
    for (int i = 0; i < 2048; i++) if (bytes[i] != 's') return 121;
    if (bytes[2048]) return 122;
    [expanded release];
    id bounded = CFStringCreateMutableCopy(0, 1, @"ß");
    range = (Range){0, 1};
    if (!bounded || CFStringTransform(bounded, &range, @"Latin-ASCII", 0) || range.length != 1 || !same(bounded, "ß")) return 123;
    CFRelease(bounded);
    if (CFStringCreateMutableCopy(0, 1, @"two") || CFStringCreateMutableCopy(0, -1, @"x") || CFStringCreateMutableCopy(0, 0, 0)) return 124;
    id empty = CFStringCreateMutableCopy(0, 0, @"");
    if (!empty || !CFStringTransform(empty, 0, @"Latin-ASCII", 0) || !same(empty, "")) return 125;
    CFRelease(empty);
    id nul = CFStringCreateMutableCopy(0, 0, @"a\0é");
    if (!nul || CFStringGetLength(nul) != 3 || !CFStringTransform(nul, 0, @"Latin-ASCII", 0)) return 126;
    char buffer[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    const char expected[4] = {'a', 0, 'e', 0};
    if (!CFStringGetCString(nul, buffer, sizeof(buffer), 0x08000100) || memcmp(buffer, expected, sizeof(expected)) || CFStringGetLength(nul) != 3) return 127;
    id nul_copy = CFStringCreateCopy(0, nul);
    if (!nul_copy || CFStringGetLength(nul_copy) != 3 || memcmp([nul_copy UTF8String], expected, 4)) return 128;
    CFRelease(nul);
    CFRelease(nul_copy);
    buffer[0] = 7;
    if (CFStringGetCString(@"hello", buffer, 5, 0x08000100) || buffer[0] != 7) return 129;
    if (!CFStringGetCString(@"hello", buffer, 6, 0x08000100) || strcmp(buffer, "hello") || buffer[6] != 7) return 130;
    if (CFStringGetCString(@"é", buffer, sizeof(buffer), 0x0600) || CFStringGetCStringPtr(@"é", 0x0600)) return 131;
    const char *direct = CFStringGetCStringPtr(@"hello", 0x0600);
    if (!direct || strcmp(direct, "hello") || CFStringGetCString(@"hello", buffer, sizeof(buffer), 0xffffffff)) return 132;
    id retained_by_session = CFStringCreateMutableCopy(0, 0, @"café");
    if (!retained_by_session || !CFStringTransform(retained_by_session, 0, @"Latin-ASCII", 0)) return 133;
    return 0;
}
