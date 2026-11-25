#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <stdio.h>

static void record(NSString *key, id value)
{
    NSDictionary *row = @{ @"case": key, @"value": value ?: [NSNull null] };
    NSData *encoded = [NSJSONSerialization dataWithJSONObject:row options:NSJSONWritingSortedKeys error:NULL];
    fwrite(encoded.bytes, 1, encoded.length, stdout);
    putchar('\n');
}

static NSMutableURLRequest *request(NSString *address, NSString *method)
{
    NSMutableURLRequest *result = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:address]];
    result.HTTPMethod = method;
    return result;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc != 3) return 2;
        NSString *path = [NSString stringWithUTF8String:argv[1]];
        NSData *der = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];
        SecCertificateRef certificate = SecCertificateCreateWithData(NULL, (CFDataRef)der);
        SecPolicyRef policy = SecPolicyCreateBasicX509();
        SecTrustRef trust = NULL;
        if (!certificate || SecTrustCreateWithCertificates(certificate, policy, &trust)) return 3;
        NSURLCredential *credential = [NSURLCredential credentialForTrust:trust];
        record(@"trust-credential", @[@(credential != nil), @(credential.persistence), @(credential.hasPassword), credential.user ?: [NSNull null], credential.password ?: [NSNull null], @(credential.identity != NULL), credential.certificates ?: [NSNull null]]);
        NSURLCredential *password = [NSURLCredential credentialWithUser:@"user" password:@"" persistence:NSURLCredentialPersistenceForSession];
        record(@"empty-password", @[@(password.hasPassword), password.user ?: @"nil", password.password ?: @"nil", @(password.persistence)]);
        record(@"password-equality", @[@([password isEqual:[NSURLCredential credentialWithUser:@"user" password:@"" persistence:NSURLCredentialPersistenceForSession]]), @([password isEqual:[NSURLCredential credentialWithUser:@"user" password:@"" persistence:NSURLCredentialPersistenceNone]])]);
        record(@"trust-copy", @([[credential copy] isEqual:credential]));
        NSURLCache *cache = [[NSURLCache alloc] initWithMemoryCapacity:1024 * 1024 diskCapacity:4 * 1024 * 1024 diskPath:path];
        [cache removeAllCachedResponses];
        NSMutableURLRequest *base = request(@"https://Example.COM:443/image?p=1", @"GET");
        NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:base.URL statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:@{@"Content-Type": @"text/plain; charset=utf-8", @"Cache-Control": @"max-age=3600"}];
        NSCachedURLResponse *stored = [[NSCachedURLResponse alloc] initWithResponse:response data:[@"sample" dataUsingEncoding:NSUTF8StringEncoding] userInfo:@{@"label": @"cached"} storagePolicy:NSURLCacheStorageAllowedInMemoryOnly];
        [cache storeCachedResponse:stored forRequest:base];
        record(@"roundtrip", @[@([cache cachedResponseForRequest:base] != nil), @(cache.currentMemoryUsage), @(cache.currentDiskUsage)]);
        for (NSString *address in @[@"https://Example.COM:443/image?p=1", @"https://example.com/image?p=1", @"https://Example.COM:443/image?p=2", @"https://Example.COM:443/image?p=1#fragment", @"http://Example.COM:443/image?p=1"]) {
            record([@"key:" stringByAppendingString:address], @([cache cachedResponseForRequest:request(address, @"GET")] != nil));
        }
        record(@"head-key", @([cache cachedResponseForRequest:request(base.URL.absoluteString, @"HEAD")] != nil));
        record(@"post-key", @([cache cachedResponseForRequest:request(base.URL.absoluteString, @"POST")] != nil));
        NSMutableURLRequest *header = [base mutableCopy];
        [header setValue:@"fr" forHTTPHeaderField:@"Accept-Language"];
        record(@"header-key", @([cache cachedResponseForRequest:header] != nil));
        NSCachedURLResponse *denied = [[NSCachedURLResponse alloc] initWithResponse:response data:[@"denied" dataUsingEncoding:NSUTF8StringEncoding] userInfo:nil storagePolicy:NSURLCacheStorageNotAllowed];
        [cache storeCachedResponse:denied forRequest:base];
        record(@"not-allowed-overwrite", [[NSString alloc] initWithData:[cache cachedResponseForRequest:base].data encoding:NSUTF8StringEncoding]);
        [cache removeAllCachedResponses];
        NSMutableURLRequest *vary = request(@"https://example.com/vary", @"GET");
        [vary setValue:@"en" forHTTPHeaderField:@"Accept-Language"];
        NSHTTPURLResponse *varyResponse = [[NSHTTPURLResponse alloc] initWithURL:vary.URL statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:@{@"Vary": @"Accept-Language", @"Cache-Control": @"max-age=3600"}];
        [cache storeCachedResponse:[[NSCachedURLResponse alloc] initWithResponse:varyResponse data:stored.data userInfo:nil storagePolicy:NSURLCacheStorageAllowedInMemoryOnly] forRequest:vary];
        NSMutableURLRequest *different = [vary mutableCopy];
        [different setValue:@"fr" forHTTPHeaderField:@"Accept-Language"];
        record(@"vary", @[@([cache cachedResponseForRequest:vary] != nil), @([cache cachedResponseForRequest:different] != nil)]);
        cache.memoryCapacity = 0;
        record(@"zero-memory", @[@([cache cachedResponseForRequest:vary] != nil), @(cache.currentMemoryUsage)]);
        NSURLCache *old = [NSURLCache sharedURLCache];
        [NSURLCache setSharedURLCache:cache];
        record(@"shared", @([NSURLCache sharedURLCache] == cache));
        NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
        configuration.URLCache = cache;
        record(@"configuration", @[@(configuration.URLCache == cache), @(configuration.requestCachePolicy)]);
        [NSURLCache setSharedURLCache:old];
        CFRelease(trust); CFRelease(policy); CFRelease(certificate);
        [cache removeAllCachedResponses];
    }
    return 0;
}