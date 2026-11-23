typedef struct { double width, height; } Size;
typedef struct { double x, y, width, height; } Rect;
typedef struct objc_method *Method;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (Class)class;
+ (id)alloc;
+ (id)new;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
@end
@interface UIImage : NSObject
+ (UIImage *)systemImageNamed:(NSString *)name;
- (UIImage *)_imageWithSize:(Size)size;
- (Size)size;
- (double)scale;
- (void)drawInRect:(Rect)rect;
- (UIImage *)imageWithRenderingMode:(long)mode;
- (long)renderingMode;
@end
@interface UIGraphicsImageRendererFormat : NSObject
+ (id)defaultFormat;
- (double)scale;
- (void)setScale:(double)scale;
@end
@interface UIGraphicsImageRenderer : NSObject
- (id)initWithSize:(Size)size format:(UIGraphicsImageRendererFormat *)format;
- (UIImage *)imageWithActions:(void (^)(id))actions;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
- (void)setFrame:(Rect)frame;
- (void)addSubview:(UIView *)view;
@end
@interface UIImageView : UIView
- (id)initWithImage:(UIImage *)image;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UIDevice : NSObject
+ (UIDevice *)currentDevice;
- (long)userInterfaceIdiom;
@end
extern Method class_getInstanceMethod(Class, SEL);
extern Method class_getClassMethod(Class, SEL);
extern void (*method_getImplementation(Method))(void);
extern SEL method_getName(Method);
extern const char *method_getTypeEncoding(Method);
extern void method_exchangeImplementations(Method, Method);
extern SEL NSSelectorFromString(NSString *);
extern id objc_getAssociatedObject(id, const void *);
extern void objc_setAssociatedObject(id, const void *, id, unsigned long);

static int scaled, named;
static char token;

@interface UIImage (MethodFixture)
- (UIImage *)fixtureSize:(Size)size;
+ (UIImage *)fixtureName:(NSString *)name;
@end
@implementation UIImage (MethodFixture)
- (UIImage *)fixtureSize:(Size)size
{
    scaled++;
    Size target = {size.width + 1, size.height + 2};
    UIImage *original = [self fixtureSize:target];
    UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
    [format setScale:[self scale]];
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:target format:format];
    UIImage *result = [renderer imageWithActions:^(id context) {
        if (context) [original drawInRect:(Rect){0, 0, target.width, target.height}];
    }];
    return [result imageWithRenderingMode:2];
}
+ (UIImage *)fixtureName:(NSString *)name
{
    (void)name;
    named++;
    return [self fixtureName:@"plus"];
}
@end

@interface UIDevice (MethodFixture)
- (long)fixtureIdiom;
@end
@implementation UIDevice (MethodFixture)
- (long)fixtureIdiom { return 17 + [self userInterfaceIdiom]; }
@end

int main(void)
{
    Class imageClass = [UIImage class];
    SEL resize = NSSelectorFromString(@"_imageWithSize:");
    Method base = class_getInstanceMethod(imageClass, resize);
    Method alternate = class_getInstanceMethod(imageClass, @selector(fixtureSize:));
    Method factory = class_getClassMethod(imageClass, @selector(systemImageNamed:));
    Method categoryFactory = class_getClassMethod(imageClass, @selector(fixtureName:));
    if (!base || !alternate || !factory || !categoryFactory || !method_getImplementation(base)) return 151;
    if (method_getName(base) != resize || !method_getTypeEncoding(base) || !*method_getTypeEncoding(base)) return 152;
    if (class_getInstanceMethod(imageClass, @selector(fixtureName:)) || class_getClassMethod(imageClass, resize)) return 153;
    UIDevice *device = [UIDevice currentDevice];
    if (!device || !class_getInstanceMethod([UIDevice class], @selector(fixtureIdiom)) ||
        [device fixtureIdiom] != 17 + [device userInterfaceIdiom]) return 154;
    UIImage *plus = [UIImage systemImageNamed:@"plus"];
    if (!plus || [UIImage systemImageNamed:@"fixture-unknown"]) return 155;
    method_exchangeImplementations(factory, categoryFactory);
    if ([UIImage systemImageNamed:@"fixture-unknown"] != plus || named != 1) return 156;
    method_exchangeImplementations(factory, categoryFactory);
    if ([UIImage systemImageNamed:@"fixture-unknown"] || named != 1) return 157;
    method_exchangeImplementations(base, alternate);
    UIImage *sized = [plus _imageWithSize:(Size){18, 20}];
    Size size = [sized size];
    if (!sized || scaled != 1 || size.width != 19 || size.height != 22) return 158;
    method_exchangeImplementations(base, alternate);
    size = [[plus _imageWithSize:(Size){18,20}] size];
    if (scaled != 1 || size.width != 18 || size.height != 20) return 159;
    objc_setAssociatedObject(plus, &token, sized, 1);
    if (objc_getAssociatedObject(plus, &token) != sized || objc_getAssociatedObject(sized, &token)) return 160;
    objc_setAssociatedObject(plus, &token, (id)0, 1);
    if (objc_getAssociatedObject(plus, &token)) return 161;
    if ([sized renderingMode] != 2 || [plus scale] <= 0 || [sized scale] != [plus scale]) return 162;
    UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
    [format setScale:3];
    UIGraphicsImageRenderer *outer = [[UIGraphicsImageRenderer alloc] initWithSize:(Size){32,32} format:format];
    UIGraphicsImageRenderer *inner = [[UIGraphicsImageRenderer alloc] initWithSize:(Size){16,8} format:format];
    UIImage *minus = [UIImage systemImageNamed:@"minus"];
    UIImage *composed = [outer imageWithActions:^(id outerContext) {
        [sized drawInRect:(Rect){-4,0,16,16}];
        UIImage *second = [inner imageWithActions:^(id innerContext) {
            if (outerContext && innerContext != outerContext) [minus drawInRect:(Rect){0,0,16,8}];
        }];
        [second drawInRect:(Rect){8,16,16,8}];
    }];
    if (!composed || [composed scale] != 3 || [composed size].width != 32) return 163;
    UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0,0,720,1280}];
    UIImageView *view = [[UIImageView alloc] initWithImage:composed];
    [view setFrame:(Rect){12,20,64,64}];
    [window addSubview:view];
    [window makeKeyAndVisible];
    return 0;
}
