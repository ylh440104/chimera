typedef unsigned long NSUInteger;
typedef signed char BOOL;
typedef struct { double x, y; } Point;
typedef struct { Point origin; struct { double width, height; } size; } Rect;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
- (id)init;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)text;
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
@end
@interface NSLayoutConstraint : NSObject
+ (void)activateConstraints:(NSArray *)constraints;
+ (void)deactivateConstraints:(NSArray *)constraints;
- (BOOL)isActive;
@end
@interface NSLayoutAnchor : NSObject
- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutAnchor *)other;
- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutAnchor *)other constant:(double)value;
- (NSLayoutConstraint *)constraintEqualToConstant:(double)value;
@end
@interface UILayoutGuide : NSObject
- (NSLayoutAnchor *)bottomAnchor;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
- (Rect)frame;
- (void)setFrame:(Rect)frame;
- (void)addSubview:(UIView *)child;
- (id)superview;
- (void)setTranslatesAutoresizingMaskIntoConstraints:(BOOL)value;
- (void)layoutIfNeeded;
- (void)setHidden:(BOOL)value;
- (NSLayoutAnchor *)topAnchor;
- (NSLayoutAnchor *)bottomAnchor;
- (NSLayoutAnchor *)leadingAnchor;
- (NSLayoutAnchor *)trailingAnchor;
- (NSLayoutAnchor *)heightAnchor;
- (UILayoutGuide *)safeAreaLayoutGuide;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UIFont : NSObject
+ (id)preferredFontForTextStyle:(NSString *)style;
- (double)pointSize;
@end
@interface UIColor : NSObject
+ (id)secondaryLabelColor;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
- (NSString *)text;
- (void)setFont:(UIFont *)font;
- (void)setTextColor:(UIColor *)color;
- (void)setTextAlignment:(long)alignment;
- (void)setNumberOfLines:(long)lines;
@end
@interface UITableView : UIView
- (id)initWithFrame:(Rect)frame style:(long)style;
- (void)setBackgroundView:(UIView *)view;
- (UIView *)backgroundView;
- (void)setDataSource:(id)source;
- (void)setDelegate:(id)delegate;
- (id)delegate;
- (void)reloadData;
@end
@interface UIProgressView : UIView
- (id)initWithProgressViewStyle:(long)style;
@end
extern const Rect CGRectZero;
extern double CGRectGetMaxX(Rect rect);
extern double CGRectGetMaxY(Rect rect);
extern BOOL CGRectContainsPoint(Rect rect, Point point);
extern NSString *UIFontTextStyleFootnote;
static int called;
@interface EmptySource : NSObject
- (long)tableView:(id)table numberOfRowsInSection:(long)section;
@end
@implementation EmptySource
- (long)tableView:(id)table numberOfRowsInSection:(long)section
{
    if (table && !section) called++;
    return 0;
}
@end

static int rect_is(Rect r, double x, double y, double w, double h)
{
    return r.origin.x == x && r.origin.y == y && r.size.width == w && r.size.height == h;
}

int main(void)
{
    if (!rect_is(CGRectZero, 0, 0, 0, 0)) return 111;
    Rect probe = {{7,9},{30,40}};
    if (CGRectGetMaxX(probe) != 37 || CGRectGetMaxY(probe) != 49 || !CGRectContainsPoint(probe, (Point){8,10})) return 112;
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){{0,0},{720,1280}}];
    UIView *root = [[UIView alloc] initWithFrame:(Rect){{15,44},{600,1100}}];
    [window addSubview:root];
    UITableView *table = [[UITableView alloc] initWithFrame:CGRectZero style:1];
    [table setTranslatesAutoresizingMaskIntoConstraints:0];
    [root addSubview:table];
    UILabel *hint = [[UILabel alloc] initWithFrame:CGRectZero];
    [hint setText:[NSString stringWithUTF8String:"No version installed\n未安装版本 😀"]];
    [hint setNumberOfLines:0];
    [hint setTextAlignment:1];
    UIFont *font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    if ([font pointSize] != 13) return 113;
    [hint setFont:font];
    [hint setTextColor:[UIColor secondaryLabelColor]];
    [table setBackgroundView:hint];
    EmptySource *source = [EmptySource new];
    [table setDataSource:source];
    [table setDelegate:source];
    [table reloadData];
    if ([table delegate] != source || called != 1 || [hint superview] != table) return 114;
    UIProgressView *progress = [[UIProgressView alloc] initWithProgressViewStyle:0];
    [progress setHidden:1];
    [progress setTranslatesAutoresizingMaskIntoConstraints:0];
    [root addSubview:progress];
    UILabel *status = [[UILabel alloc] initWithFrame:CGRectZero];
    [status setText:@"Ready"];
    [status setTranslatesAutoresizingMaskIntoConstraints:0];
    [root addSubview:status];
    NSArray *constraints = @[
        [[status bottomAnchor] constraintEqualToAnchor:[progress topAnchor] constant:-4],
        [[status trailingAnchor] constraintEqualToAnchor:[root trailingAnchor] constant:-20],
        [[status leadingAnchor] constraintEqualToAnchor:[root leadingAnchor] constant:20],
        [[progress bottomAnchor] constraintEqualToAnchor:[[root safeAreaLayoutGuide] bottomAnchor] constant:-8],
        [[progress leadingAnchor] constraintEqualToAnchor:[root leadingAnchor] constant:20],
        [[progress trailingAnchor] constraintEqualToAnchor:[root trailingAnchor] constant:-20],
        [[table bottomAnchor] constraintEqualToAnchor:[root bottomAnchor]],
        [[table trailingAnchor] constraintEqualToAnchor:[root trailingAnchor]],
        [[table topAnchor] constraintEqualToAnchor:[root topAnchor]],
        [[table leadingAnchor] constraintEqualToAnchor:[root leadingAnchor]]
    ];
    [NSLayoutConstraint activateConstraints:constraints];
    [root layoutIfNeeded];
    if (!rect_is([table frame], 0,0,600,1100) || !rect_is([hint frame], 0,0,600,1100)) return 115;
    if (!rect_is([progress frame], 20,1090,560,2) || !rect_is([status frame], 20,1064,560,22)) return 116;
    [root setFrame:(Rect){{15,44},{640,1000}}];
    [root layoutIfNeeded];
    if (!rect_is([table frame], 0,0,640,1000) || !rect_is([hint frame], 0,0,640,1000)) return 117;
    if (!rect_is([progress frame], 20,990,600,2) || !rect_is([status frame], 20,964,600,22)) return 118;
    NSLayoutConstraint *height = [[status heightAnchor] constraintEqualToConstant:30];
    [NSLayoutConstraint activateConstraints:@[height]];
    [root layoutIfNeeded];
    if (![height isActive] || !rect_is([status frame], 20,956,600,30)) return 119;
    [NSLayoutConstraint deactivateConstraints:@[height]];
    if ([height isActive]) return 120;
    [table setBackgroundView:(id)0];
    if ([hint superview] || [table backgroundView]) return 121;
    [table setBackgroundView:hint];
    [window makeKeyAndVisible];
    return 0;
}
