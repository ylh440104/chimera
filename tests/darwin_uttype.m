typedef unsigned long Size;
typedef signed char BOOL;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
- (void)release;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
extern id kUTTagClassFilenameExtension;
extern id kUTTagClassMIMEType;
extern id UTTypeCreatePreferredIdentifierForTag(id, id, id);
extern id UTTypeCopyPreferredTagWithClass(id, id);
extern id CFRetain(id);
extern void CFRelease(id);
extern long CFStringGetLength(id);
extern unsigned char CFStringGetCString(id, char *, long, unsigned int);
extern int memcmp(const void *, const void *, Size);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, Size);
extern int puts(const char *);

static int same(id value, const char *expected)
{
    const char *text = value ? [value UTF8String] : "<nil>";
    if (!strcmp(text, expected)) return 1;
    puts(text);
    puts(expected);
    return 0;
}

static int declared(id tag_class, id tag, id parent, const char *expected, const char *mime)
{
    id type = UTTypeCreatePreferredIdentifierForTag(tag_class, tag, parent);
    if (!type) return 0;
    int ok = same(type, expected);
    id result = UTTypeCopyPreferredTagWithClass(type, kUTTagClassMIMEType);
    ok = ok && result && same(result, mime);
    if (result) CFRelease(result);
    CFRelease(type);
    return ok;
}

static int dynamic(id tag_class, id tag, id parent)
{
    id first = UTTypeCreatePreferredIdentifierForTag(tag_class, tag, parent);
    id second = UTTypeCreatePreferredIdentifierForTag(tag_class, tag, parent);
    if (!first || !second) {
        if (first) CFRelease(first);
        if (second) CFRelease(second);
        return 0;
    }
    const char *identifier = [first UTF8String];
    int ok = identifier && !strncmp(identifier, "dyn.", 4) && same(second, identifier);
    id roundtrip = UTTypeCopyPreferredTagWithClass(first, tag_class);
    ok = ok && roundtrip && same(roundtrip, [tag UTF8String]);
    if (roundtrip) CFRelease(roundtrip);
    if (CFRetain(first) != first) ok = 0;
    CFRelease(first);
    ok = ok && same(first, [second UTF8String]);
    CFRelease(second);
    CFRelease(first);
    return ok;
}

int main(void)
{
    if (!same(kUTTagClassFilenameExtension, "public.filename-extension") ||
        !same(kUTTagClassMIMEType, "public.mime-type")) return 101;
    if (!declared(kUTTagClassFilenameExtension, @"jpg", 0, "public.jpeg", "image/jpeg")) return 102;
    if (!declared(kUTTagClassFilenameExtension, @"jpeg", @"public.image", "public.jpeg", "image/jpeg")) return 103;
    if (!declared(kUTTagClassFilenameExtension, @"JPG", 0, "public.jpeg", "image/jpeg")) return 104;
    if (!declared(kUTTagClassFilenameExtension, @"png", @"public.data", "public.png", "image/png")) return 105;
    if (!declared(kUTTagClassMIMEType, @"image/jpeg", 0, "public.jpeg", "image/jpeg")) return 106;
    if (!declared(kUTTagClassMIMEType, @"image/png", @"public.image", "public.png", "image/png")) return 107;
    if (!declared(kUTTagClassFilenameExtension, @"pdf", 0, "com.adobe.pdf", "application/pdf")) return 108;
    if (!dynamic(kUTTagClassFilenameExtension, @"chimera-unknown-482", 0)) return 109;
    if (!dynamic(kUTTagClassFilenameExtension, @"chimera-unknown-482", @"public.image")) return 110;
    if (!dynamic(kUTTagClassMIMEType, @"application/x-chimera-482", 0)) return 111;
    id mime = UTTypeCopyPreferredTagWithClass(@"public.jpeg", kUTTagClassMIMEType);
    if (!mime || !same(mime, "image/jpeg")) return 112;
    [mime release];
    id held = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, @"png", 0);
    if (!held || !same(held, "public.png")) return 113;
    id extension = UTTypeCopyPreferredTagWithClass(@"PUBLIC.JPEG", kUTTagClassFilenameExtension);
    if (!extension || !same(extension, "jpeg")) return 114;
    CFRelease(extension);
    if (!declared(kUTTagClassFilenameExtension, @"gz", @"public.archive", "org.gnu.gnu-zip-archive", "application/x-gzip")) return 115;
    if (!declared(kUTTagClassFilenameExtension, @"jar", @"public.executable", "com.sun.java-archive", "application/java-archive")) return 116;
    id restricted = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, @"png", @"public.text");
    if (!restricted || !same(restricted, "dyn.ah62d4r34ge81a5xh")) return 117;
    if (UTTypeCopyPreferredTagWithClass(restricted, kUTTagClassMIMEType)) return 118;
    extension = UTTypeCopyPreferredTagWithClass(restricted, kUTTagClassFilenameExtension);
    if (!extension || !same(extension, "png")) return 119;
    CFRelease(extension);
    CFRelease(restricted);
    struct { id tag; const char *identifier; const char *normalized; } cases[] = {
        {@"CHM-UNKNOWN-EXTENSION-9F7C", "dyn.age80g4drfz40645sr7506ppftb4gn5xxrf106pm3q250g", "chm-unknown-extension-9f7c"},
        {@"a:b=c", "dyn.age80c1b4qmsd422", "a:b=c"},
        {@"文件", "dyn.age88rfyh6w75q", "文件"},
        {@".png", "dyn.age8w66dsq6", ".png"},
        {@" png ", "dyn.age8wa6dsq6ua", " png "},
        {@"tar.gz", "dyn.age81k2pwf3x1y", "tar.gz"}
    };
    for (Size i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        id type = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, cases[i].tag, 0);
        if (!type || !same(type, cases[i].identifier)) return 120;
        id tag = UTTypeCopyPreferredTagWithClass(type, kUTTagClassFilenameExtension);
        if (!tag || !same(tag, cases[i].normalized)) return 121;
        if (UTTypeCopyPreferredTagWithClass(type, kUTTagClassMIMEType)) return 122;
        CFRelease(tag);
        CFRelease(type);
    }
    id nul = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, @"a\0b", 0);
    if (!nul || !same(nul, "dyn.age80c1aaqk")) return 123;
    extension = UTTypeCopyPreferredTagWithClass(@"dyn.age80c1aaqk", kUTTagClassFilenameExtension);
    char bytes[5] = {7, 7, 7, 7, 7};
    const char expected[4] = {'a', 0, 'b', 0};
    if (!extension || CFStringGetLength(extension) != 3 || !CFStringGetCString(extension, bytes, 4, 0x08000100) ||
        memcmp(bytes, expected, 4) || bytes[4] != 7) return 124;
    CFRelease(extension);
    CFRelease(nul);
    id empty = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, @"", 0);
    if (!empty || !same(empty, "dyn.age8u") || UTTypeCopyPreferredTagWithClass(empty, kUTTagClassFilenameExtension)) return 125;
    CFRelease(empty);
    if (UTTypeCopyPreferredTagWithClass(@"com.apple.property-list", kUTTagClassMIMEType) ||
        UTTypeCopyPreferredTagWithClass(@"com.sun.java-class", kUTTagClassMIMEType) ||
        UTTypeCopyPreferredTagWithClass(@"com.chimera.unknown", kUTTagClassMIMEType)) return 126;
    id invalid[] = {@"dyn.invalid", @"dyn.bge8u", @"dyn.ai", @"dyn.age8v", @"dyn.a", @"dyn.a000000000"};
    for (Size i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
        if (UTTypeCopyPreferredTagWithClass(invalid[i], kUTTagClassFilenameExtension)) return 127;
    if (UTTypeCreatePreferredIdentifierForTag(0, @"png", 0) ||
        UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, 0, 0) ||
        UTTypeCopyPreferredTagWithClass(0, kUTTagClassMIMEType) ||
        UTTypeCopyPreferredTagWithClass(@"public.png", 0)) return 128;
    return 0;
}
