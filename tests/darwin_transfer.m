typedef signed char BOOL;
typedef unsigned long Count;
typedef struct { double x, y, w, h; } Rect;
__attribute__((objc_root_class)) @interface NSObject { Class isa; }
+ (id)new;
+ (id)alloc;
@end
@interface NSString : NSObject
+ (id)stringWithFormat:(NSString *)fmt, ...;
- (const char *)UTF8String;
@end
@interface NSMutableString : NSString
+ (id)stringWithCapacity:(Count)size;
- (void)appendFormat:(NSString *)fmt, ...;
@end
@interface NSPredicate : NSObject
+ (id)predicateWithFormat:(NSString *)fmt, ...;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(Count)count;
- (id)filteredArrayUsingPredicate:(NSPredicate *)predicate;
- (id)firstObject;
- (id)objectAtIndexedSubscript:(Count)index;
@end
@interface NSDictionary : NSObject
+ (id)dictionaryWithObjects:(const id *)values forKeys:(const id *)keys count:(Count)count;
- (id)objectForKeyedSubscript:(id)key;
@end
@interface NSData : NSObject
+ (id)dataWithContentsOfURL:(id)url;
- (Count)length;
- (const void *)bytes;
@end
@interface NSURL : NSObject
+ (id)URLWithString:(id)string;
+ (id)fileURLWithPath:(id)path;
- (NSString *)path;
- (BOOL)isFileURL;
@end
@interface NSURLRequest : NSObject
+ (id)requestWithURL:(NSURL *)url;
@end
@interface NSURLResponse : NSObject
- (long)statusCode;
- (long long)expectedContentLength;
@end
@interface NSError : NSObject
- (long)code;
@end
@interface NSURLSessionConfiguration : NSObject
+ (id)defaultSessionConfiguration;
- (void)setTimeoutIntervalForRequest:(double)value;
@end
@interface NSProgress : NSObject
+ (id)progressWithTotalUnitCount:(long long)value;
- (void)addObserver:(id)obj forKeyPath:(id)key options:(Count)flags context:(void *)context;
- (void)removeObserver:(id)obj forKeyPath:(id)key context:(void *)context;
- (void)addChild:(NSProgress *)child withPendingUnitCount:(long long)weight;
- (double)fractionCompleted;
@end
@interface NSURLSessionDownloadTask : NSObject
- (void)resume;
- (void)cancel;
- (NSProgress *)progress;
@end
@interface AFURLSessionManager : NSObject
- (id)initWithSessionConfiguration:(id)config;
- (NSProgress *)downloadProgressForTask:(id)task;
- (id)downloadTaskWithRequest:(id)req progress:(void (^)(NSProgress *))update destination:(NSURL *(^)(NSURL *, NSURLResponse *))dest completionHandler:(void (^)(NSURLResponse *, NSURL *, NSError *))done;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)rect;
- (void)addSubview:(id)view;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UILabel : UIView
- (void)setText:(id)text;
@end
@interface UITableViewCell : UIView
- (id)initWithStyle:(long)style reuseIdentifier:(id)name;
- (UILabel *)textLabel;
@end
@interface NSIndexPath : NSObject
- (long)row;
@end
@interface UITableView : UIView
- (void)setDataSource:(id)value;
- (void)setDelegate:(id)value;
- (void)deselectRowAtIndexPath:(id)index animated:(BOOL)animated;
@end
@interface NSRunLoop : NSObject
+ (id)mainRunLoop;
@end
@interface CADisplayLink : NSObject
+ (id)displayLinkWithTarget:(id)target selector:(SEL)sel;
- (void)addToRunLoop:(id)loop forMode:(id)mode;
- (void)setPreferredFramesPerSecond:(long)rate;
- (void)setPaused:(BOOL)value;
- (void)invalidate;
- (double)timestamp;
- (double)targetTimestamp;
- (double)duration;
@end
extern int strcmp(const char *, const char *);
extern unsigned char *CC_SHA1(const void *, unsigned, unsigned char *);
extern NSString *NSRunLoopCommonModes;
extern double CACurrentMediaTime(void);

static AFURLSessionManager *manager;
static NSURLSessionDownloadTask *task;
static NSProgress *parent;
static CADisplayLink *link;
static NSArray *versions;
static UILabel *label;
static int attempts, errors, saved, destinationCalls, notifications, updates, ticks, bad;
static int watchContext;
static double previous;

@interface Client : NSObject
@end
@implementation Client
- (long)tableView:(id)table numberOfRowsInSection:(long)section { (void)table; return section ? 0 : 2; }
- (id)tableView:(id)table cellForRowAtIndexPath:(NSIndexPath *)index
{
    (void)table;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:0 reuseIdentifier:@"version"];
    [[cell textLabel] setText:versions[[index row]][@"id"]];
    return cell;
}
- (void)frame:(CADisplayLink *)clock
{
    if (clock != link || [clock timestamp] <= previous || [clock targetTimestamp] <= [clock timestamp] || [clock duration] <= 0) bad = 1;
    previous = [clock timestamp];
    [label setText:[NSString stringWithFormat:@"frame %d", ++ticks]];
}
- (void)observeValueForKeyPath:(id)key ofObject:(id)obj change:(id)change context:(void *)context
{
    (void)key; (void)change;
    if (obj != parent || context != &watchContext) bad = 2;
    notifications++;
}
- (void)tableView:(UITableView *)table didSelectRowAtIndexPath:(NSIndexPath *)index
{
    [table deselectRowAtIndexPath:index animated:0];
    if (task) { bad = 3; return; }
    NSString *version = versions[[index row]][@"id"];
    NSDictionary *selected = [[versions filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"(id == %@)", version]] firstObject];
    NSString *name = selected[@"id"];
    NSString *path = [NSString stringWithFormat:@"%1$s/%2$@-%2$@.bin", "/var/mobile/Documents", name];
    if (!name || strcmp([path UTF8String], "/var/mobile/Documents/sample-sample.bin")) { bad = 4; return; }
    int attempt = ++attempts;
    NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:selected[@"url"]]];
    parent = [NSProgress progressWithTotalUnitCount:10];
    task = [manager downloadTaskWithRequest:request progress:^(NSProgress *p) {
        if (p != [manager downloadProgressForTask:task]) bad = 5;
        updates++;
    } destination:^NSURL *(NSURL *temporary, NSURLResponse *response) {
        if (![temporary isFileURL] || [response expectedContentLength] != 33*1024*1024+1) bad = 6;
        destinationCalls++;
        return [NSURL fileURLWithPath:path];
    } completionHandler:^(NSURLResponse *response, NSURL *file, NSError *error) {
        [parent removeObserver:self forKeyPath:@"fractionCompleted" context:&watchContext];
        task = (id)0;
        if (attempt == 1 || attempt == 3) {
            if (!error || file || (attempt == 1 && [response statusCode] != 503) || (attempt == 3 && [error code] != -999)) bad = 7;
            errors++;
            return;
        }
        if (error || !file || strcmp([[file path] UTF8String], [path UTF8String]) || [response statusCode] != 200) { bad = 8; return; }
        NSData *data = [NSData dataWithContentsOfURL:file];
        unsigned char hash[20];
        if ([data length] != 33*1024*1024+1 || CC_SHA1([data bytes], (unsigned)[data length], hash) != hash) { bad = 9; return; }
        NSMutableString *hex = [NSMutableString stringWithCapacity:40];
        for (int i = 0; i < 20; i++) [hex appendFormat:@"%02x", (unsigned)hash[i]];
        if (strcmp([hex UTF8String], "c73d1086a940fe6a14f909211904932ff9c74e6e")) { bad = 10; return; }
        saved++;
    }];
    if (!task || ![task progress]) { bad = 11; return; }
    [parent addChild:[task progress] withPendingUnitCount:10];
    [parent addObserver:self forKeyPath:@"fractionCompleted" options:5 context:&watchContext];
    [task resume];
}
@end

int main(void)
{
    versions = @[@{@"id":@"other", @"url":@"https://example.invalid/wrong"}, @{@"id":@"sample", @"url":@"https://example.invalid/sample"}];
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
    [config setTimeoutIntervalForRequest:7.5];
    manager = [[AFURLSessionManager alloc] initWithSessionConfiguration:config];
    Client *client = [Client new];
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    UITableView *table = [[UITableView alloc] initWithFrame:(Rect){0,0,320,100}];
    [table setDataSource:client]; [table setDelegate:client]; [window addSubview:table];
    label = [[UILabel alloc] initWithFrame:(Rect){0,120,320,40}];
    [label setText:@"waiting"]; [window addSubview:label]; [window makeKeyAndVisible];
    link = [CADisplayLink displayLinkWithTarget:client selector:@selector(frame:)];
    [link setPreferredFramesPerSecond:30];
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    return !manager || !link || CACurrentMediaTime() <= 0;
}
int transfer_check(int stage)
{
    if (bad) return 20 + bad;
    switch (stage) {
    case 0: return ticks != 2;
    case 1: return attempts != 1 || errors != 1 || task || destinationCalls;
    case 2: return attempts != 2 || !task || updates < 1 || notifications < 3 ||
                   [parent fractionCompleted] < 0.49999 || [parent fractionCompleted] > 0.50001;
    case 3: return saved != 1 || errors != 1 || destinationCalls != 1 || task || [parent fractionCompleted] != 1;
    case 4: [task cancel]; [link setPaused:1]; return attempts != 3 || !task;
    case 5: [link setPaused:0]; return errors != 2 || task || saved != 1 || destinationCalls != 1;
    case 6: [link invalidate]; return ticks != 3;
    default: return 2;
    }
}
