#import <Foundation/Foundation.h>
#import <CoreServices/CoreServices.h>
#include <stdio.h>

static id nullable(id value)
{
    return value ? value : [NSNull null];
}

static int emit(NSDictionary *record)
{
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:&error];
    if (!data) {
        fprintf(stderr, "JSON error: %s\n", [[error description] UTF8String]);
        return 0;
    }
    return fwrite([data bytes], 1, [data length], stdout) == [data length] && fputc('\n', stdout) != EOF;
}

static NSDictionary *describe(CFStringRef type)
{
    if (!type) return @{ @"identifier": [NSNull null] };
    CFStringRef extension = UTTypeCopyPreferredTagWithClass(type, kUTTagClassFilenameExtension);
    CFStringRef mime = UTTypeCopyPreferredTagWithClass(type, kUTTagClassMIMEType);
    CFDictionaryRef declaration = UTTypeCopyDeclaration(type);
    NSMutableDictionary *parents = [NSMutableDictionary dictionary];
    for (NSString *parent in @[@"public.item", @"public.content", @"public.data", @"public.text", @"public.plain-text", @"public.image", @"public.archive", @"public.audio", @"public.movie", @"public.executable"])
        parents[parent] = @(UTTypeConformsTo(type, (CFStringRef)parent));
    NSDictionary *decl = (NSDictionary *)declaration;
    NSDictionary *record = @{
        @"identifier": (NSString *)type,
        @"extension": nullable((NSString *)extension),
        @"mime": nullable((NSString *)mime),
        @"dynamic": @(UTTypeIsDynamic(type)),
        @"declared": @(UTTypeIsDeclared(type)),
        @"parents": parents,
        @"declared_parents": nullable(decl[@"UTTypeConformsTo"]),
        @"tags": nullable(decl[@"UTTypeTagSpecification"])
    };
    if (extension) CFRelease(extension);
    if (mime) CFRelease(mime);
    if (declaration) CFRelease(declaration);
    return record;
}

static int tag_case(NSString *tagClass, NSString *tag, NSString *parent)
{
    CFStringRef type = UTTypeCreatePreferredIdentifierForTag((CFStringRef)tagClass, (CFStringRef)tag, (CFStringRef)parent);
    NSDictionary *record = @{
        @"operation": @"from_tag",
        @"tag_class": tagClass,
        @"tag": tag,
        @"constraint": nullable(parent),
        @"result": describe(type)
    };
    int result = emit(record);
    if (type) CFRelease(type);
    return result;
}

int main(void)
{
    @autoreleasepool {
        if (!emit(@{@"operation": @"environment", @"os": [[NSProcessInfo processInfo] operatingSystemVersionString]})) return 1;
        NSArray *extensions = @[@"txt", @"TXT", @"text", @"md", @"html", @"htm", @"css", @"js", @"json", @"xml", @"plist", @"pdf", @"png", @"PNG", @"jpg", @"jpeg", @"gif", @"bmp", @"tiff", @"webp", @"heic", @"svg", @"ico", @"zip", @"gz", @"tar", @"bz2", @"7z", @"jar", @"class", @"ipa", @"dylib", @"mp3", @"m4a", @"wav", @"ogg", @"mp4", @"mov", @"bin", @"dat", @"log", @"", @".png", @" png ", @"tar.gz", @"chm-unknown-extension-9f7c", @"CHM-UNKNOWN-EXTENSION-9F7C", @"a:b=c", @"文件", @"a\0b"];
        for (NSString *extension in extensions)
            if (!tag_case((NSString *)kUTTagClassFilenameExtension, extension, nil)) return 2;
        NSArray *mimes = @[@"text/plain", @"TEXT/PLAIN", @"text/html", @"text/css", @"text/javascript", @"application/javascript", @"application/json", @"application/xml", @"text/xml", @"application/pdf", @"image/png", @"image/jpeg", @"image/jpg", @"image/webp", @"image/svg+xml", @"application/zip", @"application/x-zip-compressed", @"application/gzip", @"application/java-archive", @"application/octet-stream", @"audio/mpeg", @"video/mp4", @"text/plain; charset=utf-8", @"application/x-chimera-unknown-9f7c", @"", @"invalid", @"*/*"];
        for (NSString *mime in mimes)
            if (!tag_case((NSString *)kUTTagClassMIMEType, mime, nil)) return 3;
        for (NSString *extension in @[@"png", @"txt", @"jar", @"chm-unknown-extension-9f7c"])
            for (NSString *parent in @[@"public.item", @"public.data", @"public.content", @"public.image", @"public.text", @"public.archive", @"com.chimera.unknown-parent"])
                if (!tag_case((NSString *)kUTTagClassFilenameExtension, extension, parent)) return 4;
        if (!tag_case(@"com.chimera.unsupported-tag", @"example", nil)) return 5;
        for (NSString *identifier in @[@"public.item", @"public.data", @"public.content", @"public.text", @"public.plain-text", @"public.utf8-plain-text", @"public.html", @"public.json", @"public.xml", @"public.png", @"PUBLIC.PNG", @"public.jpeg", @"public.zip-archive", @"com.sun.java-archive", @"public.executable", @"com.chimera.unknown-type", @"dyn.invalid"])
            if (!emit(@{@"operation": @"describe", @"input": identifier, @"result": describe((CFStringRef)identifier)})) return 6;
        CFStringRef png = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, CFSTR("png"), NULL);
        if (!png || !CFEqual(png, CFSTR("public.png"))) return 7;
        CFStringRef mime = UTTypeCopyPreferredTagWithClass(png, kUTTagClassMIMEType);
        int good = mime && CFEqual(mime, CFSTR("image/png"));
        if (mime) CFRelease(mime);
        CFRelease(png);
        if (!good) return 8;
        return ferror(stdout) ? 9 : 0;
    }
}
