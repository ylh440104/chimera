typedef signed char BOOL;
typedef unsigned long NSUInteger;
typedef struct { double x, y, w, h; } Rect;
typedef struct { double w, h; } Size;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
+ (Class)class;
- (id)init;
- (BOOL)isKindOfClass:(Class)cls;
@end
@interface NSString : NSObject
+ (id)stringWithFormat:(NSString *)format, ...;
- (const char *)UTF8String;
@end
@interface NSArray : NSObject
+ (id)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
- (void)setFrame:(Rect)frame;
- (Rect)frame;
- (void)addSubview:(id)view;
- (NSArray *)subviews;
- (id)superview;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
- (NSString *)text;
@end
@interface UIImage : NSObject
+ (id)systemImageNamed:(NSString *)name;
@end
@interface UISegmentedControl : UIView
- (id)initWithItems:(NSArray *)items;
- (NSUInteger)numberOfSegments;
- (long)selectedSegmentIndex;
- (void)setSelectedSegmentIndex:(long)index;
- (void)setEnabled:(BOOL)value;
- (void)setEnabled:(BOOL)value forSegmentAtIndex:(NSUInteger)index;
- (BOOL)isEnabledForSegmentAtIndex:(NSUInteger)index;
- (void)addTarget:(id)target action:(SEL)selector forControlEvents:(NSUInteger)events;
- (void)insertSegmentWithTitle:(NSString *)title atIndex:(NSUInteger)index animated:(BOOL)animated;
- (void)insertSegmentWithImage:(UIImage *)image atIndex:(NSUInteger)index animated:(BOOL)animated;
- (void)removeSegmentAtIndex:(NSUInteger)index animated:(BOOL)animated;
- (void)removeAllSegments;
- (void)setTitle:(NSString *)title forSegmentAtIndex:(NSUInteger)index;
- (NSString *)titleForSegmentAtIndex:(NSUInteger)index;
- (UIImage *)imageForSegmentAtIndex:(NSUInteger)index;
- (void)setWidth:(double)width forSegmentAtIndex:(NSUInteger)index;
- (double)widthForSegmentAtIndex:(NSUInteger)index;
- (void)setMomentary:(BOOL)value;
- (BOOL)isMomentary;
@end
@interface UIPickerView : UIView
- (void)setDelegate:(id)delegate;
- (void)setDataSource:(id)source;
- (long)numberOfComponents;
- (long)numberOfRowsInComponent:(long)component;
- (long)selectedRowInComponent:(long)component;
- (void)selectRow:(long)row inComponent:(long)component animated:(BOOL)animated;
- (Size)rowSizeForComponent:(long)component;
- (void)reloadAllComponents;
- (void)reloadComponent:(long)component;
- (id)viewForRow:(long)row forComponent:(long)component;
@end
@interface UITableViewHeaderFooterView : UIView
- (id)initWithReuseIdentifier:(NSString *)identifier;
- (NSString *)reuseIdentifier;
- (UILabel *)textLabel;
- (UILabel *)detailTextLabel;
- (UIView *)contentView;
- (void)prepareForReuse;
- (void)setBackgroundView:(UIView *)view;
- (UIView *)backgroundView;
@end
@interface UITableView : UIView
- (id)initWithFrame:(Rect)frame style:(long)style;
- (void)setDelegate:(id)delegate;
- (void)setDataSource:(id)source;
- (void)registerClass:(Class)cls forHeaderFooterViewReuseIdentifier:(NSString *)identifier;
- (id)dequeueReusableHeaderFooterViewWithIdentifier:(NSString *)identifier;
- (void)reloadData;
@end
extern int strcmp(const char *, const char *);
extern int UIApplicationMain(int, char **, id, id);
static UISegmentedControl *segments;
static UIPickerView *picker;
static UITableView *table;
static UITableViewHeaderFooterView *header;
static int changed, picked, bad, prepared, created, empty, narrow, reenter;
static long selected_in_action, picked_row, picked_component;
static int row_queries[2];
static UILabel *status;

@interface ChoiceHeader : UITableViewHeaderFooterView
@end
@implementation ChoiceHeader
- (id)initWithReuseIdentifier:(NSString *)identifier
{
    self = [super initWithReuseIdentifier:identifier];
    if (self) created++;
    return self;
}
- (void)prepareForReuse { [super prepareForReuse]; prepared++; }
@end

@interface ChoiceSource : NSObject
@end
@implementation ChoiceSource
- (void)changed:(UISegmentedControl *)sender
{
    if (sender != segments) bad = 1;
    changed++;
    selected_in_action = [sender selectedSegmentIndex];
    [status setText:[NSString stringWithFormat:@"segment:%ld:%d", selected_in_action, changed]];
}
- (long)numberOfComponentsInPickerView:(id)view { if (view != picker) bad = 2; return empty ? 0 : 2; }
- (long)pickerView:(id)view numberOfRowsInComponent:(long)component
{
    if (view != picker || component < 0 || component > 1) { bad = 3; return 0; }
    row_queries[component]++;
    return component ? 3 : narrow ? 2 : 5;
}
- (double)pickerView:(id)view rowHeightForComponent:(long)component { (void)view; return component ? 30 : 40; }
- (double)pickerView:(id)view widthForComponent:(long)component { (void)view; return component ? 200 : 100; }
- (id)pickerView:(id)view titleForRow:(long)row forComponent:(long)component
{
    (void)view;
    return [NSString stringWithFormat:@"P%ld-%ld", component, row];
}
- (id)pickerView:(UIPickerView *)view viewForRow:(long)row forComponent:(long)component reusingView:(id)reuse
{
    if (reenter) { reenter = 0; [view reloadComponent:component]; }
    if (!component) return (id)0;
    UILabel *label = reuse ? reuse : [UILabel new];
    [label setText:[NSString stringWithFormat:@"Custom-%ld", row]];
    return label;
}
- (void)pickerView:(UIPickerView *)view didSelectRow:(long)row inComponent:(long)component
{
    if (view != picker || row != [view selectedRowInComponent:component]) bad = 4;
    picked++; picked_row = row; picked_component = component;
    [status setText:[NSString stringWithFormat:@"picker:%ld:%ld:%d", component, row, picked]];
}
- (long)numberOfSectionsInTableView:(id)view { (void)view; return empty ? 0 : 1; }
- (long)tableView:(id)view numberOfRowsInSection:(long)section { (void)view; (void)section; return 0; }
- (id)tableView:(UITableView *)view viewForHeaderInSection:(long)section
{
    (void)section;
    header = [view dequeueReusableHeaderFooterViewWithIdentifier:@"choice"];
    if (!header || ![header contentView]) bad = 5;
    [[header textLabel] setText:@"Header title"];
    [[header detailTextLabel] setText:@"Header detail"];
    if (![header backgroundView]) [header setBackgroundView:[UIView new]];
    return header;
}
- (double)tableView:(id)view heightForHeaderInSection:(long)section { (void)view; (void)section; return 50; }
- (NSString *)tableView:(id)view titleForFooterInSection:(long)section { (void)view; (void)section; return @"Footer title"; }
@end

int main(void)
{
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0,0,400,600}];
    ChoiceSource *source = [ChoiceSource new];
    id names[] = {@"One", @"Two", @"Three"};
    if (![@"One" isKindOfClass:[NSString class]] || ![@"One" isKindOfClass:[NSObject class]] || [@"One" isKindOfClass:[UIImage class]]) return 104;
    segments = [[UISegmentedControl alloc] initWithItems:[NSArray arrayWithObjects:names count:3]];
    [segments setFrame:(Rect){20,20,300,40}];
    [segments setSelectedSegmentIndex:0];
    [segments setEnabled:0 forSegmentAtIndex:1];
    [segments addTarget:source action:@selector(changed:) forControlEvents:4096];
    if ([segments numberOfSegments] != 3 || [segments selectedSegmentIndex] != 0 || [segments isEnabledForSegmentAtIndex:1]) return 101;
    picker = [[UIPickerView alloc] initWithFrame:(Rect){20,100,300,160}];
    [picker setDataSource:source]; [picker setDelegate:source];
    [picker selectRow:1 inComponent:0 animated:0];
    [picker selectRow:1 inComponent:1 animated:0];
    if ([picker numberOfComponents] != 2 || [picker numberOfRowsInComponent:0] != 5 || [picker selectedRowInComponent:0] != 1 || picked) return 102;
    Size size = [picker rowSizeForComponent:0];
    if (size.w != 100 || size.h != 40) return 103;
    table = [[UITableView alloc] initWithFrame:(Rect){20,300,300,130} style:1];
    [table setDataSource:source]; [table setDelegate:source];
    [table registerClass:[ChoiceHeader class] forHeaderFooterViewReuseIdentifier:@"choice"];
    status = [[UILabel alloc] initWithFrame:(Rect){20,450,340,40}];
    [status setText:@"ready"];
    [window addSubview:segments]; [window addSubview:picker]; [window addSubview:table]; [window addSubview:status];
    [window makeKeyAndVisible];
    return UIApplicationMain(0, (char **)0, (id)0, (id)0);
}

int choices_check(int stage)
{
    if (bad) return 110 + bad;
    switch (stage) {
    case 0:
        if (created != 1 || prepared || changed || picked || !header || [[table subviews] count] != 2) return 120;
        if (strcmp([[header reuseIdentifier] UTF8String], "choice")) return 121;
        if ([header frame].w != 300 || [header frame].h != 50 || [[header contentView] frame].w != 300) return 122;
        if ([[header backgroundView] frame].h != 50 || [[header textLabel] frame].h != 30) return 123;
        return 0;
    case 1: return !changed && [segments selectedSegmentIndex] == 0 ? 0 : 124;
    case 2: return changed == 1 && selected_in_action == 2 && [segments selectedSegmentIndex] == 2 ? 0 : 125;
    case 3: [segments setMomentary:1]; return [segments isMomentary] ? 0 : 126;
    case 4:
        if (changed != 2 || selected_in_action != 0 || [segments selectedSegmentIndex] != -1) return 127;
        [segments setMomentary:0]; [segments setSelectedSegmentIndex:2];
        [segments insertSegmentWithTitle:@"Zero" atIndex:0 animated:0];
        if ([segments numberOfSegments] != 4 || [segments selectedSegmentIndex] != 3 || [segments isEnabledForSegmentAtIndex:2]) return 128;
        [segments removeSegmentAtIndex:0 animated:0];
        [segments setTitle:@"Last" forSegmentAtIndex:2];
        [segments setWidth:60 forSegmentAtIndex:0];
        if ([segments widthForSegmentAtIndex:0] != 60 || strcmp([[segments titleForSegmentAtIndex:2] UTF8String], "Last")) return 129;
        return [segments selectedSegmentIndex] == 2 && changed == 2 ? 0 : 130;
    case 5: return picked == 1 && picked_component == 0 && picked_row == 2 ? 0 : 131;
    case 6: return picked == 1 && [picker selectedRowInComponent:0] == 4 ? 0 : 132;
    case 7: return picked == 2 && picked_row == 4 && picked_component == 0 ? 0 : 133;
    case 8: return picked == 3 && picked_row == 2 && picked_component == 1 ? 0 : 134;
    case 9: {
        id other = [picker viewForRow:1 forComponent:1];
        int queries = row_queries[1];
        narrow = 1; [picker reloadComponent:0];
        if (row_queries[1] != queries || [picker viewForRow:1 forComponent:1] != other || [picker selectedRowInComponent:0] != 1) return 135;
        [picker selectRow:999 inComponent:0 animated:0];
        if ([picker selectedRowInComponent:0] != 1 || picked != 3) return 136;
        [table reloadData];
        return created == 1 && prepared == 1 ? 0 : 137;
    }
    case 10:
        reenter = 1; [picker reloadAllComponents]; return 0;
    case 11: return !reenter && [picker numberOfComponents] == 2 ? 0 : 138;
    case 12:
        empty = 1; [picker reloadAllComponents]; [table reloadData]; [segments removeAllSegments];
        return [picker numberOfComponents] == 0 && [picker selectedRowInComponent:0] == -1 && [segments selectedSegmentIndex] == -1 ? 0 : 139;
    case 13:
        if ([[picker subviews] count] || [[table subviews] count]) return 140;
        empty = 0; narrow = 0; [picker reloadAllComponents]; [table reloadData];
        [segments insertSegmentWithTitle:@"Restored" atIndex:0 animated:0];
        UIImage *image = [UIImage systemImageNamed:@"cube.box"];
        [segments insertSegmentWithImage:image atIndex:1 animated:0];
        if (!image || [segments imageForSegmentAtIndex:1] != image) return 141;
        return created == 1 && prepared == 2 ? 0 : 142;
    default: return 143;
    }
}
