typedef unsigned long Size;
typedef struct { Size location, length; } Range;
__attribute__((objc_root_class))
@interface NSObject
+ (id)alloc;
+ (id)new;
- (id)init;
- (id)copy;
- (id)mutableCopy;
- (_Bool)isEqual:(id)other;
- (Size)hash;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
@interface NSUUID : NSObject
+ (id)UUID;
- (id)initWithUUIDBytes:(const unsigned char *)bytes;
- (id)initWithUUIDString:(NSString *)string;
- (void)getUUIDBytes:(unsigned char *)bytes;
- (NSString *)UUIDString;
@end
@interface NSLock : NSObject
- (void)lock;
- (void)unlock;
- (_Bool)tryLock;
- (void)setName:(NSString *)name;
- (NSString *)name;
@end
@interface NSRecursiveLock : NSLock
@end
@interface NSIndexSet : NSObject
+ (id)indexSet;
+ (id)indexSetWithIndex:(Size)index;
+ (id)indexSetWithIndexesInRange:(Range)range;
- (id)initWithIndexesInRange:(Range)range;
- (Size)count;
- (Size)firstIndex;
- (Size)lastIndex;
- (_Bool)containsIndex:(Size)index;
- (_Bool)containsIndexesInRange:(Range)range;
- (Size)indexGreaterThanIndex:(Size)index;
- (Size)indexGreaterThanOrEqualToIndex:(Size)index;
- (Size)indexLessThanIndex:(Size)index;
- (Size)indexLessThanOrEqualToIndex:(Size)index;
@end
@interface NSMutableIndexSet : NSIndexSet
- (void)addIndex:(Size)index;
- (void)removeIndex:(Size)index;
- (void)addIndexesInRange:(Range)range;
- (void)removeIndexesInRange:(Range)range;
- (void)removeAllIndexes;
@end
extern int pthread_create(void **, const void *, void *(*)(void *), void *);
extern int pthread_join(void *, void **);
extern int objc_sync_enter(id), objc_sync_exit(id);
extern void *dispatch_semaphore_create(long);
extern long dispatch_semaphore_wait(void *, unsigned long);
extern long dispatch_semaphore_signal(void *);
extern unsigned long dispatch_time(unsigned long, long long);
extern int memcmp(const void *, const void *, Size), strcmp(const char *, const char *);

static NSLock *lock;
static id synchronized_object;
static void *pause_sem, *ready;
static int total, synchronized_total, failure;
static void *worker(void *argument)
{
    (void)argument;
    if ([lock tryLock]) failure = 1;
    if (objc_sync_exit(synchronized_object) != -1) failure = 2;
    dispatch_semaphore_signal(ready);
    for (int i = 0; i < 80; i++) {
        [lock lock];
        int old = total;
        dispatch_semaphore_wait(pause_sem, dispatch_time(0, 1000));
        total = old + 1;
        [lock unlock];
        if (objc_sync_enter(synchronized_object) || objc_sync_enter(synchronized_object)) failure = 3;
        old = synchronized_total;
        dispatch_semaphore_wait(pause_sem, dispatch_time(0, 1000));
        synchronized_total = old + 1;
        if (objc_sync_exit(synchronized_object) || objc_sync_exit(synchronized_object)) failure = 4;
    }
    return (void *)7;
}

int main(void)
{
    unsigned char bytes[16] = {0,1,2,3,4,5,0x46,7,0x88,9,10,11,12,13,14,15}, actual[16];
    NSUUID *uuid = [[NSUUID alloc] initWithUUIDBytes:bytes];
    if (!uuid || strcmp([[uuid UUIDString] UTF8String], "00010203-0405-4607-8809-0A0B0C0D0E0F")) return 1;
    NSUUID *parsed = [[NSUUID alloc] initWithUUIDString:@"00010203-0405-4607-8809-0a0b0c0d0e0f"];
    [parsed getUUIDBytes:actual];
    if (!parsed || memcmp(actual, bytes, 16) || ![uuid isEqual:parsed] || [uuid hash] != [parsed hash]) return 2;
    if ([[NSUUID alloc] initWithUUIDString:@"bad"] || [[NSUUID alloc] initWithUUIDString:@"00010203-0405-4607-8809-0A0B0C0D0E0G"]) return 3;
    NSUUID *random = [NSUUID UUID], *other = [NSUUID new];
    [random getUUIDBytes:actual];
    if (!random || !other || (actual[6] & 0xf0) != 0x40 || (actual[8] & 0xc0) != 0x80 || [random isEqual:other]) return 4;
    NSMutableIndexSet *indexes = [NSMutableIndexSet indexSetWithIndexesInRange:(Range){10,5}];
    [indexes addIndexesInRange:(Range){2,3}];
    [indexes addIndexesInRange:(Range){5,5}];
    if ([indexes count] != 13 || [indexes firstIndex] != 2 || [indexes lastIndex] != 14 || ![indexes containsIndexesInRange:(Range){3,10}]) return 5;
    [indexes removeIndexesInRange:(Range){6,3}];
    if ([indexes count] != 10 || [indexes containsIndex:7] || [indexes indexGreaterThanIndex:5] != 9 || [indexes indexLessThanIndex:9] != 5) return 6;
    if ([indexes indexGreaterThanOrEqualToIndex:5] != 5 || [indexes indexLessThanOrEqualToIndex:9] != 9 || [indexes indexLessThanIndex:0] != 0x7fffffffffffffffUL) return 7;
    NSIndexSet *copy = [indexes copy];
    NSMutableIndexSet *mutable = [copy mutableCopy];
    [indexes removeAllIndexes]; [mutable addIndex:1000000000000UL];
    if ([copy count] != 10 || [indexes count] || [mutable count] != 11 || [mutable lastIndex] != 1000000000000UL) return 8;
    [mutable removeIndex:1000000000000UL];
    if ([mutable count] != 10 || [indexes firstIndex] != 0x7fffffffffffffffUL || [[NSIndexSet indexSet] lastIndex] != 0x7fffffffffffffffUL) return 9;
    if ([[NSIndexSet alloc] initWithIndexesInRange:(Range){0x7ffffffffffffffeUL,4}]) return 10;
    lock = [NSLock new];
    NSRecursiveLock *recursive = [NSRecursiveLock new];
    [lock setName:@"counter"];
    if (!lock || !recursive || strcmp([[lock name] UTF8String], "counter") || ![lock tryLock] || [lock tryLock]) return 11;
    [lock unlock];
    [recursive lock]; [recursive lock];
    if (![recursive tryLock]) return 12;
    [recursive unlock]; [recursive unlock]; [recursive unlock];
    synchronized_object = [NSObject new];
    if (objc_sync_enter(0) || objc_sync_exit(0) || objc_sync_exit(synchronized_object) != -1) return 13;
    [lock lock];
    if (objc_sync_enter(synchronized_object)) return 14;
    pause_sem = dispatch_semaphore_create(0);
    ready = dispatch_semaphore_create(0);
    void *threads[4];
    for (int i = 0; i < 4; i++) if (pthread_create(&threads[i], 0, worker, 0)) return 15;
    for (int i = 0; i < 4; i++) if (dispatch_semaphore_wait(ready, dispatch_time(0, 3000000000LL))) return 19;
    if (objc_sync_exit(synchronized_object)) return 16;
    [lock unlock];
    for (int i = 0; i < 4; i++) {
        void *result;
        if (pthread_join(threads[i], &result) || result != (void *)7) return 17;
    }
    if (failure || total != 320 || synchronized_total != 320) return 18;
    return 0;
}