typedef unsigned long NSUInteger;
typedef signed char BOOL;
typedef struct { double x, y, w, h; } Rect;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
- (id)init;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)i;
@end
@interface UIColor : NSObject
+ (id)colorWithRed:(double)r green:(double)g blue:(double)b alpha:(double)a;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)r;
- (Rect)frame;
- (void)setBackgroundColor:(UIColor *)color;
- (id)superview;
- (NSArray *)subviews;
@end
@interface UITabBarItem : NSObject
- (id)initWithTitle:(NSString *)title image:(id)image selectedImage:(id)selected;
- (NSString *)title;
@end
@interface UIViewController : NSObject
- (UIView *)view;
- (void)viewDidLoad;
- (void)viewWillAppear:(BOOL)animated;
- (void)viewDidAppear:(BOOL)animated;
- (void)viewWillDisappear:(BOOL)animated;
- (void)viewDidDisappear:(BOOL)animated;
- (id)parentViewController;
- (id)navigationController;
- (id)tabBarController;
- (void)setTabBarItem:(UITabBarItem *)item;
- (UITabBarItem *)tabBarItem;
@end
@interface UINavigationController : UIViewController
- (id)initWithRootViewController:(UIViewController *)root;
- (NSArray *)viewControllers;
- (id)topViewController;
- (id)visibleViewController;
- (void)pushViewController:(UIViewController *)vc animated:(BOOL)animated;
- (id)popViewControllerAnimated:(BOOL)animated;
- (void)setViewControllers:(NSArray *)controllers;
- (UIView *)navigationBar;
@end
@interface UITabBarController : UIViewController
- (void)setViewControllers:(NSArray *)controllers;
- (NSArray *)viewControllers;
- (NSUInteger)selectedIndex;
- (void)setSelectedIndex:(NSUInteger)index;
- (id)selectedViewController;
- (void)setSelectedViewController:(UIViewController *)controller;
- (UIView *)tabBar;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(UIViewController *)controller;
- (void)makeKeyAndVisible;
@end
extern int strcmp(const char *, const char *);
static int loads[3], appears[3], disappears[3], failure;
@interface ContainerPage : UIViewController { unsigned long guard; int index; }
- (id)initWithIndex:(int)i;
- (BOOL)intact;
@end
@implementation ContainerPage
- (id)initWithIndex:(int)i { guard = 0x7766554433221100UL; index = i; return [super init]; }
- (BOOL)intact { return guard == 0x7766554433221100UL; }
- (void)viewDidLoad
{
    [super viewDidLoad];
    if (![self intact] || index < 0 || index > 2) { failure = 1; return; }
    ++loads[index];
    [[self view] setBackgroundColor:[UIColor colorWithRed:index == 0 green:index == 2 blue:index == 1 alpha:1]];
}
- (void)viewWillAppear:(BOOL)animated { (void)animated; if (![self parentViewController]) failure = 2; }
- (void)viewDidAppear:(BOOL)animated { (void)animated; ++appears[index]; }
- (void)viewWillDisappear:(BOOL)animated { (void)animated; }
- (void)viewDidDisappear:(BOOL)animated { (void)animated; ++disappears[index]; }
@end

int main(int argc, char **argv)
{
    ContainerPage *a = [[ContainerPage alloc] initWithIndex:0];
    ContainerPage *b = [[ContainerPage alloc] initWithIndex:1];
    ContainerPage *c = [[ContainerPage alloc] initWithIndex:2];
    UINavigationController *first = [[UINavigationController alloc] initWithRootViewController:a];
    UINavigationController *second = [[UINavigationController alloc] initWithRootViewController:b];
    UITabBarController *tabs = [UITabBarController new];
    [tabs setViewControllers:@[ first, second ]];
    if (!first || !second || !tabs || [a parentViewController] != first || [a navigationController] != first || [a tabBarController] != tabs) return 91;
    if (loads[0] || loads[1] || loads[2] || [[tabs viewControllers] count] != 2) return 92;
    UITabBarItem *item = [[UITabBarItem alloc] initWithTitle:@"First" image:(id)0 selectedImage:(id)0];
    [first setTabBarItem:item];
    if (!item || [first tabBarItem] != item || strcmp([[item title] UTF8String], "First")) return 93;
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    [window setRootViewController:tabs];
    [window makeKeyAndVisible];
    if (failure || loads[0] != 1 || loads[1] || appears[0] != 1 || [tabs selectedIndex] != 0) return 94;
    Rect r = [[a view] frame];
    if (r.x || r.y != 44 || r.w != 720 || r.h != 1187) return 95;
    if ([[a view] superview] != [first view] || [[first view] superview] != [tabs view]) return 96;
    if (argc > 1 && !strcmp(argv[1], "first")) return 0;
    [tabs setSelectedIndex:1];
    if ([tabs selectedViewController] != second || [[first view] superview] || [[second view] superview] != [tabs view]) return 97;
    if (loads[0] != 1 || loads[1] != 1 || disappears[0] != 1 || appears[1] != 1) return 98;
    [tabs setSelectedIndex:999];
    if ([tabs selectedIndex] != 1) return 99;
    if (argc > 1 && !strcmp(argv[1], "second")) return 0;
    [tabs setSelectedViewController:first];
    [first pushViewController:c animated:0];
    if ([first topViewController] != c || [first visibleViewController] != c || [c parentViewController] != first || [[a view] superview]) return 100;
    if (loads[2] != 1 || appears[2] != 1 || [[first viewControllers] count] != 2) return 101;
    if (argc > 1 && !strcmp(argv[1], "push")) return 0;
    if ([first popViewControllerAnimated:0] != c || [first topViewController] != a || [c parentViewController]) return 102;
    if ([[c view] superview] || [[a view] superview] != [first view] || [first popViewControllerAnimated:0]) return 103;
    if (loads[0] != 1 || loads[1] != 1 || loads[2] != 1 || failure || ![a intact] || ![b intact] || ![c intact]) return 104;
    if (appears[0] != 3 || disappears[0] != 2 || appears[1] != 1 || disappears[1] != 1 || disappears[2] != 1) return 105;
    [tabs setViewControllers:@[ first, first ]];
    if ([[tabs viewControllers] count] != 2) return 106;
    [first pushViewController:tabs animated:0];
    if ([[first viewControllers] count] != 1) return 107;
    [tabs setViewControllers:@[ second, first ]];
    if ([tabs selectedViewController] != first || [tabs selectedIndex] != 1) return 108;
    if ([[[tabs view] subviews] count] != 2 || ![tabs tabBar] || ![first navigationBar]) return 109;
    return 0;
}