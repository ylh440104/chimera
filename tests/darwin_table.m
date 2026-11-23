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
+ (id)stringWithFormat:(NSString *)format, ...;
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
- (NSUInteger)count;
@end
@interface NSIndexPath : NSObject
+ (id)indexPathForRow:(long)row inSection:(long)section;
- (long)row;
- (long)section;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)r;
- (Rect)frame;
- (void)addSubview:(id)view;
- (NSArray *)subviews;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
- (NSString *)text;
@end
@interface UISwitch : UIView
- (void)setOn:(BOOL)on;
- (BOOL)isOn;
- (void)setEnabled:(BOOL)enabled;
- (void)addTarget:(id)target action:(SEL)sel forControlEvents:(NSUInteger)events;
@end
@interface UIImage : NSObject
+ (UIImage *)systemImageNamed:(NSString *)name;
@end
@interface UIImageView : UIView
- (void)setImage:(UIImage *)image;
- (UIImage *)image;
@end
@interface UITableViewCell : UIView
- (UIImageView *)imageView;
- (id)initWithStyle:(long)style reuseIdentifier:(NSString *)reuse;
- (UILabel *)textLabel;
- (UILabel *)detailTextLabel;
- (UIView *)contentView;
- (void)setAccessoryView:(UIView *)view;
- (id)accessoryView;
- (void)setAccessoryType:(long)type;
- (BOOL)isSelected;
@end
@interface UITableView : UIView
- (id)initWithFrame:(Rect)r style:(long)style;
- (void)setDataSource:(id)source;
- (void)setDelegate:(id)delegate;
- (void)reloadData;
- (id)dequeueReusableCellWithIdentifier:(NSString *)reuse;
- (id)cellForRowAtIndexPath:(NSIndexPath *)index;
- (long)numberOfSections;
- (long)numberOfRowsInSection:(long)section;
- (void)deselectRowAtIndexPath:(NSIndexPath *)path animated:(BOOL)animated;
@end
extern int strcmp(const char *, const char *);
static UITableView *table;
static UISwitch *toggle;
static int built, allocated, reused, selected, toggled, bad;
static long picked_section, picked_row;
static int empty;

@interface TableSource : NSObject
- (long)numberOfSectionsInTableView:(id)table;
- (long)tableView:(id)table numberOfRowsInSection:(long)section;
- (id)tableView:(id)table titleForHeaderInSection:(long)section;
- (id)tableView:(UITableView *)table cellForRowAtIndexPath:(NSIndexPath *)index;
- (void)tableView:(UITableView *)table didSelectRowAtIndexPath:(NSIndexPath *)index;
- (void)toggle:(UISwitch *)sender;
@end
@implementation TableSource
- (long)numberOfSectionsInTableView:(id)t { (void)t; return empty ? 0 : 2; }
- (long)tableView:(id)t numberOfRowsInSection:(long)section { (void)t; return section ? 3 : 2; }
- (id)tableView:(id)t titleForHeaderInSection:(long)section { (void)t; return section ? @"Other" : @"Preferences"; }
- (id)tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)p
{
    built++;
    UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"row"];
    if (c) reused++;
    else { allocated++; c = [[UITableViewCell alloc] initWithStyle:1 reuseIdentifier:@"row"]; }
    [[c textLabel] setText:[NSString stringWithFormat:@"Row %ld-%ld", [p section], [p row]]];
    [[c detailTextLabel] setText:@"Detail"];
    UIImage *icon = [p section] == 1 && [p row] == 1 ? [UIImage systemImageNamed:@"cube.box"] : (id)0;
    [[c imageView] setImage:icon];
    if ([[c imageView] image] != icon || ([p section] == 1 && [p row] == 1 && !icon)) bad = 3;
    [c setAccessoryView:(id)0];
    [c setAccessoryType:1];
    if (![p section] && ![p row]) {
        if (!toggle) {
            toggle = [UISwitch new];
            [toggle setOn:1];
            [toggle addTarget:self action:@selector(toggle:) forControlEvents:4096];
        }
        [c setAccessoryView:toggle];
    }
    if (![c textLabel] || ![c detailTextLabel] || ![c contentView]) bad = 1;
    return c;
}
- (void)tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)p
{
    selected++; picked_row = [p row]; picked_section = [p section];
    [t deselectRowAtIndexPath:p animated:0];
}
- (void)toggle:(UISwitch *)sender { if (sender != toggle) bad = 2; toggled++; }
@end

int main(void)
{
    UIWindow *w = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    table = [[UITableView alloc] initWithFrame:(Rect){20,40,320,160} style:1];
    TableSource *source = [TableSource new];
    [table setDataSource:source];
    [table setDelegate:source];
    [w addSubview:table];
    [w makeKeyAndVisible];
    return built ? 131 : 0;
}

int table_check(int stage)
{
    if (bad) return 132;
    NSIndexPath *first = [NSIndexPath indexPathForRow:0 inSection:0];
    UITableViewCell *c = [table cellForRowAtIndexPath:first];
    switch (stage) {
    case 0:
        if (built != 5 || allocated != 5 || reused || [[table subviews] count] != 7) return 133;
        if ([table numberOfSections] != 2 || [table numberOfRowsInSection:1] != 3) return 134;
        if (!c || strcmp([[[c textLabel] text] UTF8String], "Row 0-0")) return 135;
        if ([c accessoryView] != toggle || ![toggle isOn]) return 136;
        return 0;
    case 1: return toggled == 1 && ![toggle isOn] && selected == 0 ? 0 : 137;
    case 2: return selected == 1 && picked_section == 0 && picked_row == 1 ? 0 : 138;
    case 3: return selected == 2 && picked_section == 1 && picked_row == 1 ? 0 : 139;
    case 4: [table reloadData]; return 0;
    case 5:
        if (built != 10 || reused != 5 || allocated != 5 || [[table subviews] count] != 7) return 140;
        return [c accessoryView] == toggle && ![toggle isOn] ? 0 : 141;
    case 6: empty = 1; [table reloadData]; return 0;
    case 7: return [[table subviews] count] == 0 && [table numberOfSections] == 0 ? 0 : 142;
    case 8: empty = 0; [table reloadData]; return 0;
    case 9: return [[table subviews] count] == 7 && allocated == 5 && reused == 10 ? 0 : 143;
    default: return 144;
    }
}
