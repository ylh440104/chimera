typedef unsigned long NSUInteger;
typedef signed char BOOL;
typedef struct { double x, y; } CGPoint;
typedef struct { double width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;

__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (id)init;
- (BOOL)respondsToSelector:(SEL)sel;
- (BOOL)isKindOfClass:(Class)cls;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)i;
@end
@interface UIColor : NSObject
+ (id)colorWithRed:(double)r green:(double)g blue:(double)b alpha:(double)a;
@end
@interface UIView : NSObject
- (id)initWithFrame:(CGRect)frame;
- (CGRect)frame;
- (CGRect)bounds;
- (void)setFrame:(CGRect)frame;
- (void)setBackgroundColor:(UIColor *)color;
- (void)addSubview:(UIView *)view;
- (UIView *)superview;
- (NSArray *)subviews;
@end
@interface UIViewController : NSObject
- (void)loadView;
- (void)viewDidLoad;
- (UIView *)view;
@end
@interface UIWindowScene : NSObject
- (id)session;
- (id)delegate;
@end
@interface UIWindow : UIView
- (id)initWithWindowScene:(UIWindowScene *)scene;
- (void)setRootViewController:(UIViewController *)controller;
- (UIViewController *)rootViewController;
- (UIWindowScene *)windowScene;
- (void)makeKeyAndVisible;
- (BOOL)isKeyWindow;
@end
@interface UIApplication : NSObject
+ (id)sharedApplication;
- (UIWindow *)keyWindow;
- (NSArray *)windows;
@end
@interface UISceneSession : NSObject
- (NSString *)role;
- (id)configuration;
- (id)scene;
@end
@interface UISceneConfiguration : NSObject
- (id)initWithName:(NSString *)name sessionRole:(NSString *)role;
- (Class)delegateClass;
- (NSString *)name;
@end
extern NSString *UIWindowSceneSessionRoleApplication;
extern NSString *NSStringFromClass(Class cls);
extern int UIApplicationMain(int, char **, NSString *, NSString *);
extern int strcmp(const char *, const char *);
static int step, failure, loads;
static UIWindow *main_window;
static UIViewController *root_controller;
static UIWindowScene *main_scene;

@interface GuardedView : UIView { unsigned long guard; }
- (BOOL)intact;
@end
@implementation GuardedView
- (id)initWithFrame:(CGRect)r
{
    guard = 0x12345678abcdef00UL;
    return [super initWithFrame:r];
}
- (BOOL)intact { return guard == 0x12345678abcdef00UL; }
@end

@interface FixtureController : UIViewController { unsigned long guard; }
- (BOOL)intact;
@end
@implementation FixtureController
- (id)init { guard = 0x7766554433221100UL; return [super init]; }
- (void)loadView { [super loadView]; }
- (void)viewDidLoad
{
    [super viewDidLoad];
    if (++loads != 1 || step != 1 || ![self intact]) failure = 64;
    UIView *view = [self view];
    [view setBackgroundColor:[UIColor colorWithRed:1 green:0 blue:0 alpha:1]];
    GuardedView *child = [[GuardedView alloc] initWithFrame:(CGRect){{10,20},{30,40}}];
    [child setFrame:(CGRect){{10,20},{30,40}}];
    CGRect r = [child frame];
    if (![child intact] || r.origin.x != 10 || r.origin.y != 20 || r.size.width != 30 || r.size.height != 40) failure = 65;
    [child setBackgroundColor:[UIColor colorWithRed:0 green:0 blue:1 alpha:1]];
    [view addSubview:child];
    [view addSubview:child];
    if ([child superview] != view || [[view subviews] count] != 1 || [[view subviews] objectAtIndex:0] != child) failure = 66;
}
- (BOOL)intact { return guard == 0x7766554433221100UL; }
@end

@interface FixtureWindowDelegate : NSObject
- (void)scene:(UIWindowScene *)scene willConnectToSession:(UISceneSession *)session options:(id)options;
- (void)sceneWillEnterForeground:(UIWindowScene *)scene;
- (void)sceneDidBecomeActive:(UIWindowScene *)scene;
@end
@implementation FixtureWindowDelegate
- (void)scene:(UIWindowScene *)scene willConnectToSession:(UISceneSession *)session options:(id)options
{
    if (step != 1 || !options || [scene delegate] != self || [scene session] != session || [session scene] != scene) failure = 67;
    if (![[session configuration] delegateClass] || ![scene isKindOfClass:[UIWindowScene class]]) failure = 68;
    main_scene = scene;
    main_window = [[UIWindow alloc] initWithWindowScene:scene];
    root_controller = [FixtureController new];
    [main_window setRootViewController:root_controller];
    [main_window makeKeyAndVisible];
    if (!main_window || [main_window windowScene] != scene || [main_window rootViewController] != root_controller) failure = 69;
    CGRect b = [main_window bounds];
    if (b.size.width != 720 || b.size.height != 1280) failure = 70;
    step = 2;
}
- (void)sceneWillEnterForeground:(UIWindowScene *)scene
{
    if (step != 2 || scene != main_scene) failure = 71;
    step = 3;
}
- (void)sceneDidBecomeActive:(UIWindowScene *)scene
{
    if (step != 3 || scene != main_scene) failure = 72;
    step = 4;
}
@end

@interface FixtureAppDelegate : NSObject
- (id)application:(id)app configurationForConnectingSceneSession:(UISceneSession *)session options:(id)options;
@end
@implementation FixtureAppDelegate
- (id)application:(id)app configurationForConnectingSceneSession:(UISceneSession *)session options:(id)options
{
    if (step || !app || !options || strcmp([[session role] UTF8String], [UIWindowSceneSessionRoleApplication UTF8String])) failure = 61;
    step = 1;
    return [[UISceneConfiguration alloc] initWithName:@"Main & Window" sessionRole:[session role]];
}
@end

int main(int argc, char **argv)
{
    FixtureAppDelegate *probe = [FixtureAppDelegate new];
    if (![probe respondsToSelector:@selector(application:configurationForConnectingSceneSession:options:)]) return 62;
    if ([probe respondsToSelector:@selector(application:didFinishLaunchingWithOptions:)]) return 63;
    if (UIApplicationMain(argc, argv, (id)0, NSStringFromClass([FixtureAppDelegate class]))) return 73;
    if (failure) return failure;
    if (step != 4 || loads != 1 || ![main_window isKeyWindow]) return 74;
    if ([[UIApplication sharedApplication] keyWindow] != main_window || [[[UIApplication sharedApplication] windows] objectAtIndex:0] != main_window) return 75;
    if ([[root_controller view] superview] != main_window || ![(FixtureController *)root_controller intact] || loads != 1) return 76;
    return 0;
}
