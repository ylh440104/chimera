typedef unsigned long Size;
typedef signed char Bool;
typedef struct { double x, y, w, h; } Rect;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (id)init;
- (Bool)isKindOfClass:(Class)cls;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(Size)count;
- (Size)count;
- (id)objectAtIndex:(Size)index;
@end
@interface UIColor : NSObject
+ (id)colorWithRed:(double)r green:(double)g blue:(double)b alpha:(double)a;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)rect;
- (Rect)frame;
- (id)superview;
- (NSArray *)subviews;
- (void)setBackgroundColor:(id)color;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
@end
@interface UIViewController : NSObject
- (UIView *)view;
- (void)viewDidLoad;
- (void)viewDidAppear:(Bool)animated;
- (void)viewDidDisappear:(Bool)animated;
- (id)parentViewController;
- (id)splitViewController;
@end
@interface UITableView : UIView
- (id)delegate;
- (id)dataSource;
- (void)deselectRowAtIndexPath:(id)index animated:(Bool)animated;
@end
@interface UITableViewCell : UIView
- (id)initWithStyle:(long)style reuseIdentifier:(NSString *)name;
- (UILabel *)textLabel;
@end
@interface UITableViewController : UIViewController
- (id)initWithStyle:(long)style;
- (UITableView *)tableView;
- (void)setTableView:(UITableView *)table;
@end
@interface UINavigationController : UIViewController
- (id)initWithRootViewController:(UIViewController *)root;
- (NSArray *)viewControllers;
- (void)pushViewController:(UIViewController *)child animated:(Bool)animated;
@end
@interface UISplitViewController : UIViewController
- (void)setViewControllers:(NSArray *)children;
- (NSArray *)viewControllers;
- (void)setDelegate:(id)delegate;
- (id)delegate;
- (void)setPreferredDisplayMode:(long)mode;
- (long)preferredDisplayMode;
- (long)displayMode;
- (void)setPreferredSplitBehavior:(long)behavior;
- (long)preferredSplitBehavior;
- (void)setPreferredPrimaryColumnWidthFraction:(double)fraction;
- (void)setMinimumPrimaryColumnWidth:(double)width;
- (void)setMaximumPrimaryColumnWidth:(double)width;
- (double)primaryColumnWidth;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(UIViewController *)controller;
- (id)rootViewController;
- (void)makeKeyAndVisible;
@end

static int loads[2], appears[2], disappears[2], modes, picked, failure;
static UINavigationController *primary, *secondary;
static UIWindow *window;

@interface PrimaryPage : UITableViewController
@end
@implementation PrimaryPage
- (void)viewDidLoad { [super viewDidLoad]; loads[0]++; }
- (void)viewDidAppear:(Bool)animated { (void)animated; appears[0]++; }
- (void)viewDidDisappear:(Bool)animated { (void)animated; disappears[0]++; }
- (long)tableView:(id)table numberOfRowsInSection:(long)section { (void)table; (void)section; return 1; }
- (id)tableView:(id)table cellForRowAtIndexPath:(id)index
{
    (void)table; (void)index;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:0 reuseIdentifier:@"row"];
    [[cell textLabel] setText:@"Primary"];
    return cell;
}
- (void)tableView:(UITableView *)table didSelectRowAtIndexPath:(id)index
{
    picked++;
    [table deselectRowAtIndexPath:index animated:0];
}
@end

@interface SecondaryPage : UIViewController
@end
@implementation SecondaryPage
- (void)viewDidLoad
{
    [super viewDidLoad];
    loads[1]++;
    [[self view] setBackgroundColor:[UIColor colorWithRed:1 green:0 blue:0 alpha:1]];
}
- (void)viewDidAppear:(Bool)animated { (void)animated; appears[1]++; }
- (void)viewDidDisappear:(Bool)animated { (void)animated; disappears[1]++; }
@end

static PrimaryPage *menu;
static SecondaryPage *detail;
@interface SplitFixture : UISplitViewController { unsigned long marker; }
- (Bool)intact;
@end
@implementation SplitFixture
- (id)init { marker = 0x7766554433221100UL; return [super init]; }
- (Bool)intact { return marker == 0x7766554433221100UL; }
- (void)viewDidLoad
{
    [super viewDidLoad];
    if (![self intact]) failure = 1;
    [self setDelegate:self];
    menu = [[PrimaryPage alloc] initWithStyle:1];
    detail = [SecondaryPage new];
    primary = [[UINavigationController alloc] initWithRootViewController:menu];
    secondary = [[UINavigationController alloc] initWithRootViewController:detail];
    [self setViewControllers:@[primary, secondary]];
    [self setPreferredPrimaryColumnWidthFraction:0.25];
    [self setMaximumPrimaryColumnWidth:600];
    [self setPreferredDisplayMode:2];
}
- (void)splitViewController:(id)sender willChangeToDisplayMode:(long)mode
{
    if (sender != self || mode < 1 || mode > 3) failure = 2;
    modes++;
}
@end
static SplitFixture *split;

int main(void)
{
    split = [SplitFixture new];
    if (!split || ![split isKindOfClass:[UIViewController class]]) return 1;
    window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    [window setRootViewController:split];
    [window makeKeyAndVisible];
    if ([window rootViewController] != split || ![split intact] || failure) return 2;
    if (loads[0] != 1 || loads[1] != 1 || appears[0] != 1 || appears[1] != 1) return 3;
    return 0;
}

int split_check(int stage)
{
    if (failure || ![split intact]) return 10;
    Rect a = [[primary view] frame], b = [[secondary view] frame];
    switch (stage) {
    case 0:
        if ([menu splitViewController] != split || [detail splitViewController] != split || [split delegate] != split) return 11;
        if ([primary parentViewController] != split || [secondary parentViewController] != split) return 12;
        if (a.x || a.w != 180 || b.x != 180 || b.w != 540 || a.h != 1280 || b.h != 1280) return 13;
        if ([menu tableView] != (id)[menu view] || [[menu tableView] delegate] != menu || [[menu tableView] dataSource] != menu) return 14;
        if ([split primaryColumnWidth] != 180 || modes != 1 || [[split viewControllers] count] != 2) return 15;
        [menu setTableView:(id)[UIView new]];
        return [[menu tableView] isKindOfClass:[UITableView class]] ? 0 : 16;
    case 1:
        [split setPreferredDisplayMode:1];
        b = [[secondary view] frame];
        if ([[primary view] superview] || b.x || b.w != 720 || disappears[0] != 1 || appears[1] != 1) return 17;
        return [split displayMode] == 1 ? 0 : 18;
    case 2:
        [split setPreferredDisplayMode:3];
        a = [[primary view] frame]; b = [[secondary view] frame];
        if (a.x || a.w != 180 || b.x || b.w != 720 || [[primary view] superview] != [split view]) return 19;
        return appears[0] == 2 && loads[0] == 1 && loads[1] == 1 ? 0 : 20;
    case 3:
        [split setPreferredDisplayMode:2];
        [split setMinimumPrimaryColumnWidth:200];
        [split setMaximumPrimaryColumnWidth:250];
        [split setPreferredPrimaryColumnWidthFraction:0.5];
        return [split primaryColumnWidth] == 250 ? 0 : 21;
    case 4:
        if (a.w != 250 || b.x != 250 || b.w != 1350 || b.h != 720) return 22;
        return [[detail view] frame].w == 1350 ? 0 : 23;
    case 5:
        [split setViewControllers:@[]];
        if ([[primary view] superview] || [[secondary view] superview] || [primary parentViewController] || [secondary parentViewController]) return 24;
        return [[split view] subviews].count == 0 && disappears[0] == 2 && disappears[1] == 1 ? 0 : 25;
    case 6:
        [split setViewControllers:@[secondary]];
        b = [[secondary view] frame];
        if (b.x || b.w != 1600 || b.h != 720 || appears[1] != 2) return 26;
        [split setViewControllers:@[secondary, secondary]];
        [secondary pushViewController:split animated:0];
        if ([[split viewControllers] count] != 1 || [[secondary viewControllers] count] != 1) return 27;
        [split setPreferredDisplayMode:99];
        return [split preferredDisplayMode] == 2 ? 0 : 28;
    case 7: return picked == 1 ? 0 : 29;
    default: return 30;
    }
}
