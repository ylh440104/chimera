typedef unsigned long NSUInteger;
typedef signed char BOOL;
typedef struct { double x,y,w,h; } Rect;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
- (id)init;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)bytes;
- (const char *)UTF8String;
- (id)dataUsingEncoding:(NSUInteger)encoding;
@end
@interface NSNumber : NSObject
+ (id)numberWithBool:(BOOL)value;
+ (id)numberWithInt:(int)value;
- (long)integerValue;
- (BOOL)boolValue;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (id)firstObject;
- (NSUInteger)count;
- (id)sortedArrayUsingSelector:(SEL)selector;
@end
@interface NSDictionary : NSObject
+ (id)dictionaryWithObjects:(const id *)values forKeys:(const id *)keys count:(NSUInteger)count;
+ (id)dictionaryWithContentsOfFile:(id)path;
- (id)objectForKeyedSubscript:(id)key;
- (BOOL)writeToFile:(id)path atomically:(BOOL)atomic;
@end
@interface NSUserDefaults : NSObject
+ (id)standardUserDefaults;
- (void)setObject:(id)object forKey:(id)key;
- (id)objectForKey:(id)key;
- (void)setBool:(BOOL)value forKey:(id)key;
- (BOOL)boolForKey:(id)key;
- (BOOL)synchronize;
@end
@interface NSMutableArray : NSArray
- (void)addObjectsFromArray:(id)array;
@end
@interface NSData : NSObject
+ (id)dataWithContentsOfFile:(id)path;
- (BOOL)writeToFile:(id)path options:(NSUInteger)options error:(id *)error;
@end
@interface NSBundle : NSObject
+ (id)mainBundle;
- (id)infoDictionary;
- (id)bundleIdentifier;
- (id)objectForInfoDictionaryKey:(id)key;
@end
@interface NSOutputStream : NSObject
- (id)initToFileAtPath:(id)path append:(BOOL)append;
- (void)open;
- (void)close;
- (long)streamStatus;
@end
@interface NSJSONSerialization : NSObject
+ (id)JSONObjectWithData:(id)data options:(NSUInteger)options error:(id *)error;
+ (id)dataWithJSONObject:(id)object options:(NSUInteger)options error:(id *)error;
+ (long)writeJSONObject:(id)object toStream:(id)stream options:(NSUInteger)options error:(id *)error;
@end
@interface NSFileManager : NSObject
+ (id)defaultManager;
- (BOOL)createDirectoryAtPath:(id)path withIntermediateDirectories:(BOOL)intermediate attributes:(id)attributes error:(id *)error;
- (id)contentsOfDirectoryAtPath:(id)path error:(id *)error;
- (BOOL)changeCurrentDirectoryPath:(id)path;
- (BOOL)fileExistsAtPath:(id)path isDirectory:(BOOL *)directory;
- (BOOL)createSymbolicLinkAtPath:(id)path withDestinationPath:(id)destination error:(id *)error;
@end
@interface NSNotification : NSObject
- (id)name;
- (id)object;
- (id)userInfo;
@end
@interface NSNotificationCenter : NSObject
+ (id)defaultCenter;
- (void)addObserver:(id)observer selector:(SEL)selector name:(id)name object:(id)sender;
- (void)removeObserver:(id)observer;
- (void)postNotificationName:(id)name object:(id)object userInfo:(id)info;
@end
@interface NSURL : NSObject
+ (id)URLWithString:(id)string;
@end
@interface NSURLSessionTask : NSObject
- (void)resume;
- (void)cancel;
@end
@interface NSURLResponse : NSObject
- (long)statusCode;
@end
@interface NSURLSession : NSObject
+ (id)sharedSession;
- (id)dataTaskWithURL:(id)url completionHandler:(void (^)(NSData *, NSURLResponse *, id))handler;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
@end
@interface UIViewController : NSObject
- (void)presentViewController:(id)controller animated:(BOOL)animated completion:(void (^)(void))completion;
- (id)presentedViewController;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(id)controller;
- (void)makeKeyAndVisible;
@end
@interface UITextField : UIView
- (void)setText:(id)text;
- (id)text;
- (void)setPlaceholder:(id)placeholder;
@end
@interface UIAlertAction : NSObject
+ (id)actionWithTitle:(id)title style:(long)style handler:(void (^)(UIAlertAction *))handler;
- (void)setEnabled:(BOOL)enabled;
@end
@interface UIAlertController : UIViewController
+ (id)alertControllerWithTitle:(id)title message:(id)message preferredStyle:(long)style;
- (void)addAction:(id)action;
- (void)addTextFieldWithConfigurationHandler:(void (^)(UITextField *))handler;
- (NSArray *)textFields;
@end
extern int strcmp(const char *, const char *);
extern void *dispatch_get_main_queue(void);
extern void *dispatch_get_global_queue(long, unsigned long);
extern void dispatch_async(void *, void (^)(void));
extern void dispatch_async_f(void *, void *, void (*)(void *));
extern void *_Block_copy(const void *);
extern void _Block_release(const void *);
static int queued, noted, confirmed, failed, network, cancelled;
static int (^kept)(void);
static UIViewController *root;
static UIAlertController *alert;
static id sender;

@interface ServiceObserver : NSObject
- (void)receive:(NSNotification *)notification;
@end
@implementation ServiceObserver
- (void)receive:(NSNotification *)n
{
    if ([n object] != sender || strcmp([[n name] UTF8String], "Updated") || [[n userInfo][@"value"] integerValue] != 7) failed = 201;
    noted++;
}
@end
static void function_job(void *context) { if ((unsigned long)context != 19) failed = 202; queued++; }
__attribute__((noinline)) static void setup_callbacks(int capture)
{
    __block int value = 5;
    kept = _Block_copy(^{ return ++value; });
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        if (capture != 37) failed = 203;
        queued++;
        dispatch_async(dispatch_get_main_queue(), ^{ queued++; });
    });
    dispatch_async_f(dispatch_get_main_queue(), (void *)19, function_job);
    alert = [UIAlertController alertControllerWithTitle:@"Generic editor" message:@"Edit a value" preferredStyle:1];
    [alert addTextFieldWithConfigurationHandler:^(UITextField *field) { [field setPlaceholder:@"Name"]; [field setText:@"initial"]; }];
    UIAlertAction *disabled = [UIAlertAction actionWithTitle:@"Disabled" style:0 handler:^(UIAlertAction *a) { (void)a; failed = 204; }];
    [disabled setEnabled:0];
    [alert addAction:disabled];
    [alert addAction:[UIAlertAction actionWithTitle:@"Confirm" style:0 handler:^(UIAlertAction *a) {
        (void)a;
        UITextField *field = [[alert textFields] firstObject];
        if (capture != 37 || strcmp([[field text] UTF8String], "edited 中文") || [root presentedViewController]) failed = 205;
        confirmed++;
        [[NSNotificationCenter defaultCenter] postNotificationName:@"Updated" object:sender userInfo:@{@"value":@7}];
    }]];
    [root presentViewController:alert animated:0 completion:(id)0];
    NSURLSessionTask *task = [[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:@"https://example.com/data"] completionHandler:^(NSData *data, NSURLResponse *response, id error) {
        id parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:(id *)0];
        if (capture != 37 || error || [response statusCode] != 200 || [parsed[@"value"] integerValue] != 7) failed = 206;
        network++;
        [[NSNotificationCenter defaultCenter] postNotificationName:@"Updated" object:sender userInfo:parsed];
    }];
    [task resume];
    NSURLSessionTask *stop = [[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:@"https://example.com/cancel"] completionHandler:^(NSData *data, NSURLResponse *response, id error) {
        if (!error || data || response) failed = 207;
        cancelled++;
    }];
    [stop resume]; [stop cancel];
}

int main(void)
{
    id error = (id)0;
    NSBundle *bundle = [NSBundle mainBundle];
    if (strcmp([[bundle bundleIdentifier] UTF8String], "dev.chimera.services-fixture")) return 232;
    if (strcmp([[[bundle infoDictionary] objectForKeyedSubscript:@"CFBundleExecutable"] UTF8String], "darwin_services")) return 233;
    if (strcmp([[bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"] UTF8String], "Independent & Generic")) return 234;
    NSFileManager *fm = [NSFileManager defaultManager];
    if (![fm createDirectoryAtPath:@"/var/mobile/Documents/services" withIntermediateDirectories:1 attributes:(id)0 error:&error] || error) return 210;
    if (![fm changeCurrentDirectoryPath:@"/var/mobile/Documents/services"]) return 211;
    NSDictionary *dict = @{@"value":@7, @"enabled":[NSNumber numberWithBool:1], @"unicode":@"中文 😀", @"array":@[@"z",@"a"]};
    NSData *data = [NSJSONSerialization dataWithJSONObject:dict options:0 error:&error];
    id parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (!data) return 224;
    if (!parsed) return 225;
    if (error) return 226;
    if (strcmp([parsed[@"unicode"] UTF8String], "中文 😀")) return 227;
    if (![parsed[@"enabled"] boolValue]) return 228;
    if ([NSJSONSerialization JSONObjectWithData:[@"{\"x\":1,}" dataUsingEncoding:4] options:0 error:&error] || !error) return 213;
    NSOutputStream *stream = [[NSOutputStream alloc] initToFileAtPath:@"account.json" append:0];
    [stream open];
    if ([stream streamStatus] != 2 || [NSJSONSerialization writeJSONObject:dict toStream:stream options:0 error:&error] <= 0 || error) return 214;
    [stream close];
    parsed = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@"account.json"] options:0 error:&error];
    if (!parsed || [parsed[@"value"] integerValue] != 7) return 215;
    if (![data writeToFile:@"account.json" options:1 error:&error] || error) return 235;
    NSArray *names = [@[@"z", @"Beta", @"alice"] sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    if (strcmp([[names firstObject] UTF8String], "alice")) return 236;
    if (![dict writeToFile:@"prefs.plist" atomically:1]) return 216;
    parsed = [NSDictionary dictionaryWithContentsOfFile:@"prefs.plist"];
    if (!parsed || strcmp([parsed[@"unicode"] UTF8String], "中文 😀") || ![parsed[@"enabled"] boolValue]) return 217;
    NSArray *files = [[fm contentsOfDirectoryAtPath:@"." error:&error] sortedArrayUsingSelector:@selector(compare:)];
    if ([files count] != 2 || strcmp([[files firstObject] UTF8String], "account.json")) return 218;
    BOOL isdir = 1;
    if (![fm fileExistsAtPath:@"account.json" isDirectory:&isdir] || isdir) return 219;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:@"stored" forKey:@"test"];
    [defaults setBool:1 forKey:@"flag"];
    if (![defaults synchronize] || ![defaults boolForKey:@"flag"] || strcmp([[defaults objectForKey:@"test"] UTF8String], "stored")) return 229;
    id preferences = [NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/preferences.plist"];
    if (!preferences || strcmp([preferences[@"test"] UTF8String], "stored")) return 230;
    NSMutableArray *merged = [NSMutableArray new];
    [merged addObjectsFromArray:@[@"a", @"b"]];
    [merged addObjectsFromArray:merged];
    if ([merged count] != 4 || strcmp([merged[2] UTF8String], "a")) return 231;
    sender = [NSObject new];
    ServiceObserver *observer = [ServiceObserver new];
    [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(receive:) name:@"Updated" object:sender];
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    root = [UIViewController new];
    [window setRootViewController:root]; [window makeKeyAndVisible];
    setup_callbacks(37);
    return failed;
}

int services_check(int stage)
{
    if (failed) return failed;
    if (stage == 0) return queued == 3 && kept() == 6 && kept() == 7 ? 0 : 220;
    if (stage == 1) return confirmed == 1 && noted == 1 ? 0 : 221;
    if (stage == 2) { _Block_release(kept); kept = 0; return network == 1 && cancelled == 1 && noted == 2 ? 0 : 222; }
    return 223;
}