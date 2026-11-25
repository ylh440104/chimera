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
+ (id)new;
- (id)copy;
- (void)release;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)value;
@end
@interface NSData : NSObject
+ (id)dataWithContentsOfFile:(id)path;
+ (id)dataWithBytes:(const void *)bytes length:(NSUInteger)length;
- (const void *)bytes;
- (NSUInteger)length;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (NSUInteger)count;
@end
@interface NSMutableArray : NSArray
- (void)addObject:(id)object;
- (void)insertObject:(id)object atIndex:(NSUInteger)index;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeAllObjects;
- (void)addObjectsFromArray:(id)other;
@end
typedef void *SecCertificateRef;
typedef void *SecPolicyRef;
typedef void *SecTrustRef;
typedef void *SecKeyRef;
typedef void *CFTypeRef;
typedef void *CFArrayRef;
typedef void *CFDataRef;
typedef void *CFStringRef;
typedef unsigned int SecTrustResultType;
extern SecCertificateRef SecCertificateCreateWithData(void *, CFDataRef);
extern CFDataRef SecCertificateCopyData(SecCertificateRef);
extern SecPolicyRef SecPolicyCreateBasicX509(void);
extern SecPolicyRef SecPolicyCreateSSL(unsigned char, CFStringRef);
extern int SecTrustCreateWithCertificates(CFTypeRef, CFTypeRef, SecTrustRef *);
extern int SecTrustSetPolicies(SecTrustRef, CFTypeRef);
extern int SecTrustSetAnchorCertificates(SecTrustRef, CFArrayRef);
extern int SecTrustEvaluate(SecTrustRef, SecTrustResultType *);
extern long SecTrustGetCertificateCount(SecTrustRef);
extern SecCertificateRef SecTrustGetCertificateAtIndex(SecTrustRef, long);
extern SecKeyRef SecTrustCopyPublicKey(SecTrustRef);
extern CFTypeRef CFRetain(CFTypeRef);
extern void CFRelease(CFTypeRef);
extern unsigned char CFEqual(CFTypeRef, CFTypeRef);
extern unsigned long CFHash(CFTypeRef);
extern int puts(const char *);
extern int snprintf(char *, NSUInteger, const char *, ...);
extern int memcmp(const void *, const void *, NSUInteger);
#endif

static const char *directory = "/var/mobile/security";

static NSData *read_data(const char *file)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s.der", directory, file);
    return [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
}

static SecCertificateRef read_certificate(const char *file)
{
    return SecCertificateCreateWithData(0, (CFDataRef)read_data(file));
}

static NSArray *array(id first, id second)
{
    id items[] = {first, second};
    return [NSArray arrayWithObjects:items count:second ? 2 : first ? 1 : 0];
}

static int evaluation(SecTrustRef trust, int expected)
{
    SecTrustResultType result = 0;
    int status = SecTrustEvaluate(trust, &result);
    int valid = result == 1 || result == 4;
#ifdef CHIMERA_APPLE_REFERENCE
    printf("evaluate status=%d result=%u expected=%d%c", status, (unsigned)result, expected, 10);
    if (status || valid != expected) {
        CFErrorRef error = NULL;
        Boolean accepted = SecTrustEvaluateWithError(trust, &error);
        printf("accepted=%d%c", accepted, 10);
        if (error) { CFShow(error); CFRelease(error); }
        CFArrayRef properties = SecTrustCopyProperties(trust);
        if (properties) { CFShow(properties); CFRelease(properties); }
    }
#endif
    return status == 0 && valid == expected;
}

static int scenario(const char *file, const char *issuer, const char *root, id hostname, int ssl, int server, int expected)
{
    SecCertificateRef leaf = read_certificate(file), intermediate = issuer ? read_certificate(issuer) : 0;
    SecCertificateRef anchor = root ? read_certificate(root) : 0;
    SecPolicyRef policy = ssl ? SecPolicyCreateSSL(server, (CFStringRef)hostname) : SecPolicyCreateBasicX509();
    SecTrustRef trust = 0;
    int ok = leaf && policy && (!issuer || intermediate) && (!root || anchor) &&
        SecTrustCreateWithCertificates((CFTypeRef)array((id)leaf, (id)intermediate), policy, &trust) == 0;
    if (ok && root) ok = SecTrustSetAnchorCertificates(trust, (CFArrayRef)array((id)anchor, 0)) == 0;
    if (leaf) CFRelease(leaf);
    if (intermediate) CFRelease(intermediate);
    if (anchor) CFRelease(anchor);
    if (policy) CFRelease(policy);
    if (ok) ok = evaluation(trust, expected);
    if (trust) CFRelease(trust);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc > 1) directory = argv[1];
    NSData *der = read_data("leaf");
    SecCertificateRef leaf = SecCertificateCreateWithData(0, (CFDataRef)der);
    if (!leaf) return 1;
    CFDataRef copy = SecCertificateCopyData(leaf);
    if (!copy || [(NSData *)copy length] != [der length] || memcmp([(NSData *)copy bytes], [der bytes], [der length])) return 2;
    SecCertificateRef duplicate = SecCertificateCreateWithData(0, copy);
    if (!duplicate || !CFEqual(leaf, duplicate) || ![(id)leaf isEqual:(id)duplicate] || CFHash(leaf) != CFHash(duplicate)) return 3;
    CFRelease(copy);
    CFRelease(duplicate);
    unsigned char invalid[] = {0x30, 0x03, 0x01};
    if (SecCertificateCreateWithData(0, (CFDataRef)[NSData dataWithBytes:invalid length:sizeof(invalid)])) return 4;
    if (SecCertificateCreateWithData(0, (CFDataRef)[NSData dataWithBytes:[der bytes] length:[der length] - 1])) return 5;
    if (!scenario("leaf", "intermediate", "root", @"chimera.test", 1, 1, 1)) return 10;
    if (!scenario("leaf", "intermediate", "root", @"wrong.test", 1, 1, 0)) return 11;
    if (!scenario("leaf", "intermediate", "root", @"unused-cn.test", 1, 1, 0)) return 12;
    if (!scenario("leaf", "intermediate", "unrelated", @"chimera.test", 1, 1, 0)) return 13;
    if (!scenario("leaf", "intermediate", 0, @"chimera.test", 1, 1, 0)) return 14;
    if (!scenario("expired", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 15;
    if (!scenario("future", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 16;
    if (!scenario("tampered", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 17;
    if (!scenario("client", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 18;
    if (!scenario("client", "intermediate", "root", 0, 1, 0, 1)) return 19;
    if (!scenario("client", "intermediate", "root", 0, 0, 0, 1)) return 20;
    if (!scenario("leaf", "nonca-intermediate", "root", @"chimera.test", 1, 1, 0)) return 21;
    if (!scenario("leaf", "intermediate", "short-root", @"chimera.test", 1, 1, 0)) return 22;
    if (!scenario("leaf", 0, "root", @"chimera.test", 1, 1, 0)) return 23;
    if (!scenario("leaf", "intermediate", "root", @"one.wild.chimera.test", 1, 1, 1)) return 24;
    if (!scenario("leaf", "intermediate", "root", @"two.one.wild.chimera.test", 1, 1, 0)) return 25;
    if (!scenario("leaf", "intermediate", "root", @"127.0.0.1", 1, 1, 1)) return 26;
    if (!scenario("leaf", "intermediate", "root", @"127.0.0.2", 1, 1, 0)) return 27;
    if (!scenario("ec-leaf", "intermediate", "root", @"chimera.test", 1, 1, 1)) return 28;
    if (!scenario("leaf", "intermediate", "intermediate", @"chimera.test", 1, 1, 1)) return 29;
    if (!scenario("long-lived", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 45;
    if (!scenario("cn-only", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 46;
    if (!scenario("weak-rsa", "intermediate", "root", @"chimera.test", 1, 1, 0)) return 47;
    if (!scenario("medium-lived", "intermediate", "root", @"chimera.test", 1, 1, 1)) return 48;
    if (!scenario("long-lived", "intermediate", "root", 0, 0, 0, 1)) return 49;
    if (!scenario("system-root", 0, 0, 0, 0, 0, 1)) return 50;
    SecCertificateRef intermediate = read_certificate("intermediate"), root = read_certificate("root");
    SecPolicyRef policy = SecPolicyCreateSSL(1, (CFStringRef)@"chimera.test");
    SecTrustRef trust = 0;
    NSMutableArray *certificates = [NSMutableArray new], *policies = [NSMutableArray new];
    [certificates addObject:(id)leaf];
    [certificates insertObject:(id)intermediate atIndex:1];
    [policies addObject:(id)policy];
    CFRelease(leaf); CFRelease(intermediate); CFRelease(policy);
    NSArray *snapshot = [certificates copy];
    [certificates removeAllObjects];
    [certificates addObjectsFromArray:snapshot];
    [snapshot release];
    if (SecTrustCreateWithCertificates((CFTypeRef)certificates, (CFTypeRef)policies, &trust)) return 30;
    [certificates removeObjectAtIndex:1];
    [certificates release]; [policies release];
    if (SecTrustSetAnchorCertificates(trust, (CFArrayRef)array((id)root, 0)) || !evaluation(trust, 1)) return 31;
    if (SecTrustGetCertificateCount(trust) != 3) return 32;
#ifndef CHIMERA_APPLE_REFERENCE
    if (SecTrustGetCertificateAtIndex(trust, -1) || SecTrustGetCertificateAtIndex(trust, 3)) return 32;
#endif
    SecCertificateRef retained = (SecCertificateRef)CFRetain(SecTrustGetCertificateAtIndex(trust, 0));
    SecKeyRef key = SecTrustCopyPublicKey(trust);
    if (!key) return 33;
    SecPolicyRef wrong = SecPolicyCreateSSL(1, (CFStringRef)@"wrong.test"), basic = SecPolicyCreateBasicX509();
    if (SecTrustSetPolicies(trust, wrong) || !evaluation(trust, 0)) return 34;
    if (SecTrustSetPolicies(trust, basic) || !evaluation(trust, 1)) return 35;
    if (SecTrustSetPolicies(trust, (CFTypeRef)array((id)basic, (id)wrong)) || !evaluation(trust, 0)) return 36;
    if (SecTrustSetPolicies(trust, basic) || SecTrustSetAnchorCertificates(trust, (CFArrayRef)array(0, 0)) || !evaluation(trust, 0)) return 37;
    if (SecTrustSetAnchorCertificates(trust, (CFArrayRef)array((id)root, 0)) || !evaluation(trust, 1)) return 38;
    if (SecTrustSetAnchorCertificates(trust, 0) || !evaluation(trust, 0)) return 39;
    CFRelease(trust); CFRelease(wrong); CFRelease(root);
    copy = SecCertificateCopyData(retained);
    if (!copy || !CFEqual(copy, (CFDataRef)der)) return 40;
    CFRelease(copy); CFRelease(retained);
    SecCertificateRef same = read_certificate("same-key"), different = read_certificate("ec-leaf");
    SecTrustRef a = 0, b = 0;
    if (SecTrustCreateWithCertificates(same, basic, &a) || SecTrustCreateWithCertificates(different, basic, &b)) return 41;
    if (!evaluation(a, 0) || !evaluation(b, 0)) return 42;
    SecKeyRef key_a = SecTrustCopyPublicKey(a), key_b = SecTrustCopyPublicKey(b);
#ifdef CHIMERA_APPLE_REFERENCE
    printf("keys present=%d,%d,%d equal=%d objc=%d hash=%lu,%lu different=%d%c", key != NULL, key_a != NULL, key_b != NULL, key && key_a ? CFEqual(key, key_a) : -1, key && key_a ? [(id)key isEqual:(id)key_a] : -1, key ? CFHash(key) : 0, key_a ? CFHash(key_a) : 0, key && key_b ? CFEqual(key, key_b) : -1, 10);
#endif
    if (!key_a || !key_b || !CFEqual(key, key_a) || ![(id)key isEqual:(id)key_a] || CFEqual(key, key_b)) return 43;
#ifndef CHIMERA_APPLE_REFERENCE
    if (CFHash(key) != CFHash(key_a)) return 44;
#endif
    CFRelease(key); CFRelease(key_a); CFRelease(key_b); CFRelease(a); CFRelease(b);
    CFRelease(same); CFRelease(different); CFRelease(basic);
    puts("security: certificate roundtrip, RSA/EC keys, trusted/untrusted chains, signatures, dates, hostname, IP, wildcard, EKU, CA constraints, policy invalidation and collection ownership passed");
    return 0;
}