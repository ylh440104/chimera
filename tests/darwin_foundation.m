typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef signed char BOOL;
typedef struct { NSUInteger state; id *itemsPtr; NSUInteger *mutationsPtr; NSUInteger extra[5]; } FastState;

__attribute__((objc_root_class))
@interface NSObject
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (id)init;
- (id)copy;
- (id)mutableCopy;
- (Class)class;
@end

@interface NSString : NSObject
+ (id)stringWithFormat:(NSString *)format, ...;
- (const char *)UTF8String;
@end

@interface NSNumber : NSObject
+ (id)numberWithBool:(BOOL)v;
+ (id)numberWithInt:(int)v;
+ (id)numberWithLongLong:(long long)v;
+ (id)numberWithUnsignedLongLong:(unsigned long long)v;
+ (id)numberWithFloat:(float)v;
+ (id)numberWithDouble:(double)v;
- (BOOL)boolValue;
- (int)intValue;
- (long long)longLongValue;
- (unsigned long long)unsignedLongLongValue;
- (float)floatValue;
- (double)doubleValue;
@end

@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (NSUInteger)countByEnumeratingWithState:(FastState *)state objects:(id *)objects count:(NSUInteger)count;
@end

@interface NSMutableArray : NSArray
- (id)initWithCapacity:(NSUInteger)capacity;
- (void)addObject:(id)object;
@end

@interface NSDictionary : NSObject
+ (id)dictionaryWithObjects:(const id *)objects forKeys:(const id *)keys count:(NSUInteger)count;
- (NSUInteger)count;
- (id)objectForKey:(id)key;
- (id)objectForKeyedSubscript:(id)key;
- (NSUInteger)countByEnumeratingWithState:(FastState *)state objects:(id *)objects count:(NSUInteger)count;
@end

@interface NSMutableDictionary : NSDictionary
- (id)initWithCapacity:(NSUInteger)capacity;
- (void)setObject:(id)value forKey:(id)key;
- (void)setObject:(id)value forKeyedSubscript:(id)key;
@end

extern NSString *NSStringFromClass(Class cls);
extern Class NSClassFromString(NSString *name);
extern NSString *NSStringFromSelector(SEL selector);
extern SEL NSSelectorFromString(NSString *name);
extern int strcmp(const char *, const char *);
extern int UIApplicationMain(int, char **, NSString *, NSString *);

static int launched;
static int active;
static int resigned;

@interface ABIAppDelegate : NSObject
- (BOOL)application:(id)application didFinishLaunchingWithOptions:(id)options;
- (void)applicationDidBecomeActive:(id)application;
- (void)applicationWillResignActive:(id)application;
@end

@implementation ABIAppDelegate
- (BOOL)application:(id)application didFinishLaunchingWithOptions:(id)options
{
    (void)options;
    launched = application != (id)0;
    return 1;
}
- (void)applicationDidBecomeActive:(id)application { active = application != (id)0; }
- (void)applicationWillResignActive:(id)application { (void)application; resigned++; }
@end

static int same(NSString *s, const char *v)
{
    const char *p = [s UTF8String];
    return p && !strcmp(p, v);
}

int main(int argc, char **argv)
{
    if (![[NSNumber numberWithBool:1] boolValue] || [[NSNumber numberWithBool:0] boolValue]) return 41;
    if ([[NSNumber numberWithInt:-23] intValue] != -23) return 42;
    if ([[NSNumber numberWithLongLong:-4294967297LL] longLongValue] != -4294967297LL) return 43;
    if ([[NSNumber numberWithUnsignedLongLong:18446744073709551615ULL] unsignedLongLongValue] != 18446744073709551615ULL) return 44;
    if ([[NSNumber numberWithFloat:1.25f] floatValue] != 1.25f || [[NSNumber numberWithDouble:-2.5] doubleValue] != -2.5) return 45;
    NSDictionary *defaults = @{ @"general": @{ @"debug_logging": @1, @"game_directory": @"default" }, @"scale": @1.25 };
    if ([defaults count] != 2 || ![defaults[@"general"] count]) return 46;
    if (![defaults[@"general"] objectForKey:@"debug_logging"] || [defaults objectForKey:@"missing"] != (id)0) return 47;
    NSMutableDictionary *copy = [defaults mutableCopy];
    copy[@"scale"] = @2.5;
    if ([copy count] != 2 || [copy[@"scale"] doubleValue] != 2.5 || [defaults[@"scale"] doubleValue] != 1.25) return 48;
    copy[@"scale"] = (id)0;
    if ([copy count] != 1 || copy[@"scale"] != (id)0) return 49;
    NSMutableDictionary *numbers = [[NSMutableDictionary alloc] initWithCapacity:300];
    NSMutableArray *array = [[NSMutableArray alloc] initWithCapacity:300];
    for (int i = 0; i < 300; i++) {
        [array addObject:@(i)];
        numbers[@(i)] = @(i + 7);
    }
    if ([array count] != 300 || [numbers count] != 300 || [array[299] intValue] != 299 || [numbers[@299] intValue] != 306) return 50;
    NSUInteger count = 0;
    for (id value in array) count += (NSUInteger)[value intValue];
    if (count != 44850) return 51;
    count = 0;
    for (id key in numbers) { if (!numbers[key]) return 52; count++; }
    if (count != 300) return 53;
    NSArray *literal = @[ @"first", @4 ];
    NSMutableArray *mutable = [literal mutableCopy];
    [mutable addObject:@"last"];
    if ([literal count] != 2 || [mutable count] != 3 || !same(mutable[2], "last")) return 54;
    if ([[NSMutableArray new] count] != 0 || ![NSDictionary new]) return 55;
    NSString *name = NSStringFromClass([ABIAppDelegate class]);
    if (!same(name, "ABIAppDelegate") || NSClassFromString(name) != [ABIAppDelegate class]) return 56;
    if (!same(NSStringFromClass([NSString class]), "NSString") || NSClassFromString(@"NSString") != [NSString class]) return 57;
    if (!same(NSStringFromSelector(NSSelectorFromString(@"description")), "description")) return 58;
    if (UIApplicationMain(argc, argv, (NSString *)0, name) != 0 || !launched || !active || resigned) return 59;
    return 0;
}
