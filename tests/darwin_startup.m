typedef struct { double x, y, width, height; } Rect;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
@end
@interface UIView : NSObject
- (id)initWithFrame:(Rect)frame;
- (void)setFrame:(Rect)frame;
- (void)addSubview:(id)view;
@end
@interface UIButton : UIView
+ (id)buttonWithType:(long)type;
- (void)setTitle:(id)title forState:(unsigned long)state;
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(unsigned long)events;
@end
@interface UIViewController : NSObject
- (UIView *)view;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(id)root;
- (void)makeKeyAndVisible;
@end
extern int UIApplicationMain(int, char **, id, id);
extern int pipe(int *);
extern int dup2(int, int);
extern void *__stdoutp, *__stderrp;
extern int fileno(void *stream);
extern int fprintf(void *stream, const char *format, ...);
extern int close(int);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int fstat(int, void *);
extern void *dispatch_get_main_queue(void);
extern void *dispatch_get_global_queue(long, unsigned long);
extern void dispatch_async(void *, void (^)(void));
extern int strcmp(const char *, const char *);

static int pipe_ends[2], reads_started, reader_ended, messages, taps, main_jobs, failure, deferred, launch_failure;
static UIButton *button;

@interface StartupDelegate : NSObject
- (signed char)application:(id)application didFinishLaunchingWithOptions:(id)options;
- (void)submit;
@end
@implementation StartupDelegate
- (void)submit
{
    taps++;
    if (deferred ? fprintf(__stderrp, "%s", "x") != 1 : write(1, "x", 1) != 1) failure = 10;
}
- (signed char)application:(id)application didFinishLaunchingWithOptions:(id)options
{
    (void)application;
    (void)options;
    int out = fileno(__stdoutp), err = fileno(__stderrp);
    if (out != 1 || err != 2) { launch_failure = 1; return 0; }
    if (pipe(pipe_ends)) { launch_failure = 2; return 0; }
    if (dup2(pipe_ends[1], out) != out || dup2(pipe_ends[1], err) != err || close(pipe_ends[1])) { launch_failure = 3; return 0; }
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        for (;;) {
            char value = 0;
            reads_started++;
            long count = read(pipe_ends[0], &value, 1);
            if (count <= 0) break;
            if (value != 'x') { failure = 11; break; }
            dispatch_async(dispatch_get_main_queue(), ^{
                messages++;
                [button setTitle:@"Delivered" forState:0];
            });
        }
        reader_ended++;
        close(pipe_ends[0]);
    });
    void (^open_window)(void) = ^{
        UIWindow *window = [[UIWindow alloc] initWithFrame:(Rect){0, 0, 720, 1280}];
        UIViewController *controller = [UIViewController new];
        button = [UIButton buttonWithType:1];
        [button setFrame:(Rect){20, 40, 140, 60}];
        [button setTitle:@"Send" forState:0];
        [button addTarget:self action:@selector(submit) forControlEvents:64];
        [[controller view] addSubview:button];
        [window setRootViewController:controller];
        [window makeKeyAndVisible];
    };
    if (deferred) dispatch_async(dispatch_get_main_queue(), open_window);
    else open_window();
    dispatch_async(dispatch_get_main_queue(), ^{ main_jobs++; });
    return 1;
}
@end

int main(int argc, char **argv)
{
    deferred = argc > 1 && !strcmp(argv[1], "deferred");
    return UIApplicationMain(argc, argv, (id)0, @"StartupDelegate");
}
int startup_state(int stage)
{
    if (launch_failure) return 30 + launch_failure;
    if (failure || reader_ended || taps > 1 || messages > 1 || main_jobs > 1) return -1;
    if (!stage) return main_jobs == 1 && reads_started == 1 && !taps && !messages;
    return main_jobs == 1 && reads_started == 2 && taps == 1 && messages == 1;
}
