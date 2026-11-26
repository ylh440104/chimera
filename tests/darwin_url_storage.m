#ifdef CHIMERA_APPLE_REFERENCE
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <stdio.h>
#include <string.h>
#else
typedef unsigned long NSUInteger;
typedef signed char BOOL;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (id)init;
- (id)copy;
- (id)mutableCopy;
- (id)retain;
- (void)release;
- (BOOL)isEqual:(id)other;
- (BOOL)isKindOfClass:(Class)other;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)bytes;
- (const char *)UTF8String;
- (id)stringByAppendingPathComponent:(id)other;
@end
@interface NSData : NSObject
+ (id)dataWithContentsOfFile:(id)path;
+ (id)dataWithBytes:(const void *)bytes length:(NSUInteger)length;
- (NSUInteger)length;
- (const void *)bytes;
@end
@interface NSDictionary : NSObject
+ (id)dictionaryWithObject:(id)object forKey:(id)key;
- (id)objectForKey:(id)key;
@end
@interface NSMutableDictionary : NSDictionary
- (void)setObject:(id)object forKey:(id)key;
@end
@interface NSURL : NSObject
+ (id)URLWithString:(id)value;
- (id)absoluteString;
@end
@interface NSURLRequest : NSObject
+ (id)requestWithURL:(id)url;
- (id)URL;
@end
@interface NSMutableURLRequest : NSURLRequest
- (void)setHTTPMethod:(id)method;
- (void)setURL:(id)url;
- (void)setValue:(id)value forHTTPHeaderField:(id)header;
@end
@interface NSURLResponse : NSObject
- (id)initWithURL:(id)url MIMEType:(id)mime expectedContentLength:(long)length textEncodingName:(id)encoding;
- (id)URL;
- (id)MIMEType;
- (id)textEncodingName;
- (long)expectedContentLength;
@end
@interface NSHTTPURLResponse : NSURLResponse
- (id)initWithURL:(id)url statusCode:(long)code HTTPVersion:(id)version headerFields:(id)headers;
- (long)statusCode;
- (id)allHeaderFields;
@end
@interface NSCachedURLResponse : NSObject
- (id)initWithResponse:(id)response data:(id)data;
- (id)initWithResponse:(id)response data:(id)data userInfo:(id)info storagePolicy:(NSUInteger)policy;
- (id)response;
- (id)data;
- (id)userInfo;
- (NSUInteger)storagePolicy;
@end
@interface NSURLCache : NSObject
+ (id)sharedURLCache;
+ (void)setSharedURLCache:(id)cache;
- (id)initWithMemoryCapacity:(NSUInteger)memory diskCapacity:(NSUInteger)disk diskPath:(id)path;
- (void)storeCachedResponse:(id)response forRequest:(id)request;
- (id)cachedResponseForRequest:(id)request;
- (void)removeCachedResponseForRequest:(id)request;
- (void)removeAllCachedResponses;
- (NSUInteger)memoryCapacity;
- (NSUInteger)diskCapacity;
- (NSUInteger)currentMemoryUsage;
- (NSUInteger)currentDiskUsage;
- (void)setMemoryCapacity:(NSUInteger)capacity;
- (void)setDiskCapacity:(NSUInteger)capacity;
@end
@interface NSURLCredential : NSObject
+ (id)credentialWithUser:(id)user password:(id)password persistence:(NSUInteger)persistence;
+ (id)credentialForTrust:(void *)trust;
- (id)user;
- (id)password;
- (id)certificates;
- (void *)identity;
- (BOOL)hasPassword;
- (NSUInteger)persistence;
@end
@interface NSURLSession : NSObject
+ (id)sessionWithConfiguration:(id)configuration;
- (id)configuration;
@end
@interface NSURLSessionConfiguration : NSObject
+ (id)defaultSessionConfiguration;
+ (id)ephemeralSessionConfiguration;
- (void)setURLCache:(id)cache;
- (id)URLCache;
- (void)setRequestCachePolicy:(NSUInteger)policy;
- (NSUInteger)requestCachePolicy;
- (void)setHTTPShouldSetCookies:(BOOL)value;
- (BOOL)HTTPShouldSetCookies;
- (void)setHTTPShouldUsePipelining:(BOOL)value;
- (BOOL)HTTPShouldUsePipelining;
- (void)setAllowsCellularAccess:(BOOL)value;
- (BOOL)allowsCellularAccess;
- (void)setTimeoutIntervalForRequest:(double)value;
- (double)timeoutIntervalForRequest;
@end
typedef void *CFDataRef;
typedef void *SecCertificateRef;
typedef void *SecPolicyRef;
typedef void *SecTrustRef;
extern SecCertificateRef SecCertificateCreateWithData(void *, CFDataRef);
extern SecPolicyRef SecPolicyCreateBasicX509(void);
extern int SecTrustCreateWithCertificates(void *, void *, SecTrustRef *);
extern void CFRelease(void *);
extern int puts(const char *);
extern int strcmp(const char *, const char *);
extern int memcmp(const void *, const void *, NSUInteger);
#endif

static NSString *cache_path;
static const unsigned char payload[] = {0, 1, 2, 0xfe, 0xff, 0, 42};

static NSMutableURLRequest *make_request(NSString *url)
{
    return [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];
}

static NSCachedURLResponse *make_response(NSURLRequest *request, NSUInteger policy)
{
    NSDictionary *headers = [NSDictionary dictionaryWithObject:@"text/plain; charset=utf-8" forKey:@"Content-Type"];
    NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:[request URL] statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:headers];
    NSData *data = [NSData dataWithBytes:payload length:sizeof(payload)];
    NSCachedURLResponse *result = [[NSCachedURLResponse alloc] initWithResponse:response data:data userInfo:[NSDictionary dictionaryWithObject:@"saved" forKey:@"label"] storagePolicy:policy];
    [response release];
    return result;
}

static int content(id cached)
{
    if (!cached) return 0;
    NSData *data = [cached data];
    NSHTTPURLResponse *response = (NSHTTPURLResponse *)[cached response];
    return [data length] == sizeof(payload) && !memcmp([data bytes], payload, sizeof(payload)) &&
        [response statusCode] == 200 && [[response MIMEType] isEqual:@"text/plain"] &&
        [[response textEncodingName] isEqual:@"utf-8"] && [[[cached userInfo] objectForKey:@"label"] isEqual:@"saved"];
}

static int credentials(NSString *certificate_path)
{
    NSURLCredential *password = [NSURLCredential credentialWithUser:@"user" password:@"" persistence:1];
    if (!password || ![password hasPassword] || ![[password user] isEqual:@"user"] || ![[password password] isEqual:@""] || [password persistence] != 1) return 1;
    if (![password isEqual:[NSURLCredential credentialWithUser:@"user" password:@"" persistence:1]] ||
        [password isEqual:[NSURLCredential credentialWithUser:@"user" password:@"" persistence:0]]) return 2;
    id copy = [password copy];
    if (![copy isEqual:password] || ![copy isKindOfClass:[NSURLCredential class]]) return 3;
    [copy release];
    NSData *der = [NSData dataWithContentsOfFile:certificate_path];
    SecCertificateRef certificate = SecCertificateCreateWithData(0, (CFDataRef)der);
    SecPolicyRef policy = SecPolicyCreateBasicX509();
    SecTrustRef trust = 0;
    if (!certificate || !policy || SecTrustCreateWithCertificates(certificate, policy, &trust)) return 4;
    NSURLCredential *credential = [NSURLCredential credentialForTrust:trust];
    CFRelease(trust); CFRelease(policy); CFRelease(certificate);
    if (!credential || [credential hasPassword] || [credential user] || [credential password] ||
        [credential identity] || [credential certificates] || [credential persistence] != 1) return 5;
    copy = [credential copy];
    if (![copy isEqual:credential]) return 6;
    [copy release];
    return 0;
}

int url_storage_check(int stage)
{
    NSURLCache *cache = [[NSURLCache alloc] initWithMemoryCapacity:0 diskCapacity:4 * 1024 * 1024 diskPath:cache_path];
    NSURLRequest *request = make_request(@"https://Example.COM:443/image?p=1");
    id value = [cache cachedResponseForRequest:request];
    int result = stage ? value != 0 : !content(value);
    [cache release];
    return result;
}

int main(int argc, char **argv)
{
    cache_path = @"/var/mobile/Library/Caches/url-storage-test";
    NSString *certificate = @"/var/mobile/security/system-root.der";
    if (argc > 1) cache_path = [NSString stringWithUTF8String:argv[1]];
    if (argc > 2) certificate = [NSString stringWithUTF8String:argv[2]];
    int rc = credentials(certificate);
    if (rc) return rc;
    NSURLCache *cache = [[NSURLCache alloc] initWithMemoryCapacity:1024 * 1024 diskCapacity:4 * 1024 * 1024 diskPath:cache_path];
    if (!cache || [cache memoryCapacity] != 1024 * 1024 || [cache diskCapacity] != 4 * 1024 * 1024) return 10;
    [cache removeAllCachedResponses];
    NSMutableURLRequest *base = make_request(@"https://Example.COM:443/image?p=1");
    NSCachedURLResponse *response = make_response(base, 1);
    if (!response || [response storagePolicy] != 1) return 11;
    [cache storeCachedResponse:response forRequest:base];
    [response release];
    if (!content([cache cachedResponseForRequest:base]) || [cache currentMemoryUsage] != sizeof(payload) || [cache currentDiskUsage]) return 12;
    if ([cache cachedResponseForRequest:make_request(@"https://Example.COM:443/image?p=2")] ||
        [cache cachedResponseForRequest:make_request(@"https://example.com/image?p=1")] ||
        [cache cachedResponseForRequest:make_request(@"https://Example.COM:443/image?p=1#fragment")]) return 13;
    NSMutableURLRequest *changed = [base mutableCopy];
    [changed setHTTPMethod:@"POST"];
    [changed setValue:@"fr" forHTTPHeaderField:@"Accept-Language"];
    if (!content([cache cachedResponseForRequest:changed])) return 14;
    [changed setURL:[NSURL URLWithString:@"https://example.com/different"]];
    if ([cache cachedResponseForRequest:changed] || !content([cache cachedResponseForRequest:base])) return 15;
    [changed release];
    response = make_response(base, 2);
    [cache storeCachedResponse:response forRequest:base]; [response release];
    if (!content([cache cachedResponseForRequest:base])) return 16;
    id held = [[cache cachedResponseForRequest:base] retain];
    [cache removeCachedResponseForRequest:base];
    if ([cache cachedResponseForRequest:base] || !content(held) || [cache currentMemoryUsage]) return 17;
    [held release];
    response = make_response(base, 1);
    [cache storeCachedResponse:response forRequest:base];
    [cache setMemoryCapacity:0];
    if (!content([cache cachedResponseForRequest:base])) return 18;
    [cache removeAllCachedResponses];
    [cache storeCachedResponse:response forRequest:base]; [response release];
    if ([cache cachedResponseForRequest:base]) return 19;
    [cache setMemoryCapacity:1024 * 1024];
    NSURLCache *old = [[NSURLCache sharedURLCache] retain];
    [NSURLCache setSharedURLCache:cache];
    if ([NSURLCache sharedURLCache] != cache) return 20;
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
    [config setURLCache:cache]; [config setRequestCachePolicy:3];
    [config setHTTPShouldSetCookies:0]; [config setHTTPShouldUsePipelining:1]; [config setAllowsCellularAccess:0];
    [config setTimeoutIntervalForRequest:45];
    NSURLSessionConfiguration *snapshot = [config copy];
    [config setURLCache:0];
    if ([snapshot URLCache] != cache || [snapshot requestCachePolicy] != 3 || [snapshot HTTPShouldSetCookies] ||
        ![snapshot HTTPShouldUsePipelining] || [snapshot allowsCellularAccess] || [snapshot timeoutIntervalForRequest] != 45) return 21;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:snapshot];
    NSURLSessionConfiguration *sessionConfig = [session configuration];
    if (!session || !sessionConfig || [sessionConfig URLCache] != cache ||
        [sessionConfig requestCachePolicy] != 3 || [sessionConfig HTTPShouldSetCookies] ||
        ![sessionConfig HTTPShouldUsePipelining] || [sessionConfig allowsCellularAccess] ||
        [sessionConfig timeoutIntervalForRequest] != 45) return 28;
    [snapshot release];
    [NSURLCache setSharedURLCache:old]; [old release];
#ifndef CHIMERA_APPLE_REFERENCE
    NSURLCache *tiny = [[NSURLCache alloc] initWithMemoryCapacity:14 diskCapacity:0 diskPath:0];
    NSURLRequest *second = make_request(@"https://example.com/second"), *third = make_request(@"https://example.com/third");
    response = make_response(base, 1);
    [tiny storeCachedResponse:response forRequest:base];
    [tiny storeCachedResponse:response forRequest:second];
    if (!content([tiny cachedResponseForRequest:base])) return 22;
    [tiny storeCachedResponse:response forRequest:third];
    if (!content([tiny cachedResponseForRequest:base]) || [tiny cachedResponseForRequest:second] || !content([tiny cachedResponseForRequest:third]) || [tiny currentMemoryUsage] != 14) return 23;
    [response release]; [tiny release];
    [cache setMemoryCapacity:0];
    response = make_response(base, 0);
    [cache storeCachedResponse:response forRequest:base]; [response release];
    if ([cache currentMemoryUsage] || ![cache currentDiskUsage] || !content([cache cachedResponseForRequest:base])) return 24;
    [cache release];
    cache = [[NSURLCache alloc] initWithMemoryCapacity:0 diskCapacity:4 * 1024 * 1024 diskPath:cache_path];
    if (!content([cache cachedResponseForRequest:base])) return 25;
    [cache removeCachedResponseForRequest:base];
    if ([cache cachedResponseForRequest:base] || [cache currentDiskUsage]) return 26;
    response = make_response(base, 0);
    [cache storeCachedResponse:response forRequest:base]; [response release];
    if (!content([cache cachedResponseForRequest:base])) return 27;
#endif
    [cache release];
    puts("url-storage: credentials, trust ownership, response snapshots, URL keys, storage policies, shared cache and configuration passed");
    return 0;
}