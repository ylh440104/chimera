typedef unsigned long NSUInteger;
typedef signed char BOOL;
typedef struct { double x, y; } Point;
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
@end
@interface UIColor : NSObject
+ (id)colorWithRed:(double)r green:(double)g blue:(double)b alpha:(double)a;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
- (void)setFrame:(Rect)frame;
- (void)addSubview:(UIView *)child;
- (void)removeFromSuperview;
- (void)setHidden:(BOOL)value;
- (void)setAlpha:(double)value;
- (void)setUserInteractionEnabled:(BOOL)value;
- (void)setBackgroundColor:(id)color;
- (id)hitTest:(Point)point withEvent:(id)event;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
@end
@interface UIButton : UIView
+ (id)buttonWithType:(long)type;
- (void)setTitle:(NSString *)title forState:(NSUInteger)state;
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(NSUInteger)events;
- (void)removeTarget:(id)target action:(SEL)action forControlEvents:(NSUInteger)events;
- (void)setEnabled:(BOOL)enabled;
- (BOOL)isHighlighted;
@end
@interface UIBarButtonItem : NSObject
- (id)initWithTitle:(NSString *)title style:(long)style target:(id)target action:(SEL)action;
- (void)setEnabled:(BOOL)enabled;
@end
@interface UINavigationItem : NSObject
- (void)setRightBarButtonItem:(UIBarButtonItem *)item;
- (void)setLeftBarButtonItem:(UIBarButtonItem *)item;
@end
@interface UIViewController : NSObject
- (UIView *)view;
- (void)viewDidLoad;
- (UINavigationItem *)navigationItem;
- (void)presentViewController:(id)controller animated:(BOOL)animated completion:(void (^)(void))done;
- (void)dismissViewControllerAnimated:(BOOL)animated completion:(void (^)(void))done;
- (id)presentedViewController;
- (id)presentingViewController;
@end
@interface UINavigationController : UIViewController
- (id)initWithRootViewController:(UIViewController *)controller;
- (void)pushViewController:(UIViewController *)controller animated:(BOOL)animated;
- (UIViewController *)topViewController;
@end
@interface UITabBarController : UIViewController
- (void)setViewControllers:(NSArray *)controllers;
- (NSUInteger)selectedIndex;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(UIViewController *)controller;
- (void)makeKeyAndVisible;
@end

static UITabBarController *tabs;
static UINavigationController *nav;
static UIViewController *page, *modal, *pushed;
static UIWindow *window;
static UIButton *button;
static UIView *cover;
static UIBarButtonItem *open_item;
static id actions;
static int zero, one, two, cancel_count, outside_count, bad_sender, modal_open, modal_close;

@interface InputActions : NSObject
- (void)zero;
- (void)one:(id)sender;
- (void)two:(id)sender event:(id)event;
- (void)cancel:(id)sender;
- (void)outside:(id)sender;
- (void)open:(id)sender;
- (void)close;
- (void)crash;
@end
@implementation InputActions
- (void)zero { zero++; }
- (void)one:(id)sender
{
    one++;
    if (sender != button) bad_sender++;
    [button removeTarget:self action:@selector(zero) forControlEvents:64];
}
- (void)two:(id)sender event:(id)event
{
    two++;
    if (sender != button || event) bad_sender++;
}
- (void)cancel:(id)sender { if (sender != button) bad_sender++; cancel_count++; }
- (void)outside:(id)sender { if (sender != button) bad_sender++; outside_count++; }
- (void)open:(id)sender
{
    if (sender != open_item) bad_sender++;
    modal = [UIViewController new];
    [[modal view] setBackgroundColor:[UIColor colorWithRed:0 green:1 blue:0 alpha:1]];
    [[modal navigationItem] setLeftBarButtonItem:[[UIBarButtonItem alloc] initWithTitle:@"Close" style:0 target:self action:@selector(close)]];
    UINavigationController *container = [[UINavigationController alloc] initWithRootViewController:modal];
    [page presentViewController:container animated:0 completion:^{ modal_open++; }];
}
- (void)close
{
    [modal dismissViewControllerAnimated:0 completion:^{ modal_close++; }];
}
- (void)crash { __asm__ volatile("brk #0"); }
@end

int main(void)
{
    actions = [InputActions new];
    page = [UIViewController new];
    [[page view] setBackgroundColor:[UIColor colorWithRed:1 green:0 blue:0 alpha:1]];
    UIView *holder = [[UIView alloc] initWithFrame:(Rect){30,60,300,300}];
    button = [UIButton buttonWithType:1];
    [button setFrame:(Rect){20,30,100,60}];
    [button setTitle:@"Tap" forState:0];
    UILabel *label = [[UILabel alloc] initWithFrame:(Rect){0,0,100,60}];
    [label setText:@"Tap"];
    [button addSubview:label];
    [button addTarget:actions action:@selector(zero) forControlEvents:64];
    [button addTarget:actions action:@selector(one:) forControlEvents:64];
    [button addTarget:actions action:@selector(two:event:) forControlEvents:64];
    [button addTarget:actions action:@selector(cancel:) forControlEvents:256];
    [button addTarget:actions action:@selector(outside:) forControlEvents:128];
    [holder addSubview:button];
    [[page view] addSubview:holder];
    nav = [[UINavigationController alloc] initWithRootViewController:page];
    open_item = [[UIBarButtonItem alloc] initWithTitle:@"Open" style:0 target:actions action:@selector(open:)];
    [[page navigationItem] setRightBarButtonItem:open_item];
    UIViewController *other = [UIViewController new];
    [[other view] setBackgroundColor:[UIColor colorWithRed:0 green:0 blue:1 alpha:1]];
    tabs = [UITabBarController new];
    [tabs setViewControllers:@[nav, other]];
    window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    [window setRootViewController:tabs];
    [window makeKeyAndVisible];
    return 0;
}

int input_check(int stage)
{
    if (bad_sender) return 201;
    switch (stage) {
    case 0:
        return [window hitTest:(Point){70,150} withEvent:(id)0] == button ? 0 : 202;
    case 1: return [tabs selectedIndex] == 1 && !zero ? 0 : 203;
    case 2: return [tabs selectedIndex] == 0 ? 0 : 204;
    case 3: return zero == 1 && one == 1 && two == 1 && ![button isHighlighted] ? 0 : 205;
    case 4: return zero == 1 && one == 2 && two == 2 ? 0 : 206;
    case 5: return one == 2 && cancel_count == 1 && ![button isHighlighted] ? 0 : 207;
    case 6: return one == 2 && cancel_count == 2 && ![button isHighlighted] ? 0 : 208;
    case 7: [button setEnabled:0]; return 0;
    case 8: [button setEnabled:1]; return one == 2 ? 0 : 209;
    case 9:
        cover = [[UIView alloc] initWithFrame:(Rect){45,125,120,80}];
        [[page view] addSubview:cover];
        return 0;
    case 10: [cover setHidden:1]; return one == 2 ? 0 : 210;
    case 11: return one == 3 ? 0 : 211;
    case 12:
        return modal_open == 1 && [page presentedViewController] && [modal presentingViewController] == page ? 0 : 212;
    case 13: return [tabs selectedIndex] == 0 && one == 3 ? 0 : 213;
    case 14: return modal_close == 1 && ![page presentedViewController] && ![modal presentingViewController] ? 0 : 214;
    case 15:
        pushed = [UIViewController new];
        [nav pushViewController:pushed animated:0];
        return [nav topViewController] == pushed ? 0 : 215;
    case 16: return [nav topViewController] == page ? 0 : 216;
    case 17: return one == 3 && !outside_count ? 0 : 217;
    case 18: [open_item setEnabled:0]; return 0;
    case 19: [open_item setEnabled:1]; return modal_open == 1 ? 0 : 218;
    case 20:
        [button removeTarget:(id)0 action:(SEL)0 forControlEvents:~0UL];
        [button addTarget:actions action:@selector(crash) forControlEvents:64];
        return 0;
    default: return 219;
    }
}
