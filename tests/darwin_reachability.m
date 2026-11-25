typedef unsigned int Flags;
typedef unsigned char Boolean;
typedef struct {
    long version;
    void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    void *(*description)(const void *);
} Context;
extern void *SCNetworkReachabilityCreateWithAddress(void *, const void *);
extern void *SCNetworkReachabilityCreateWithName(void *, const char *);
extern Boolean SCNetworkReachabilityGetFlags(void *, Flags *);
extern Boolean SCNetworkReachabilitySetCallback(void *, void (*)(void *, Flags, void *), const Context *);
extern Boolean SCNetworkReachabilityScheduleWithRunLoop(void *, void *, void *);
extern Boolean SCNetworkReachabilityUnscheduleFromRunLoop(void *, void *, void *);
extern void *CFRunLoopGetMain(void);
extern void *CFRetain(void *);
extern void CFRelease(void *);
extern void *kCFRunLoopCommonModes;
extern int puts(const char *);

static void *active, *muted, *named, *route;
static int retained, released, calls, muted_calls, name_calls, name_local, errors, started, finished;
static int original_info, retained_info, route_calls;
static Flags route_flags;
static const Flags local_flags = 2u | (1u << 16) | (1u << 17);

static const void *retain_info(const void *info)
{
    if (info != &original_info) errors++;
    retained++;
    return &retained_info;
}

static void release_info(const void *info)
{
    if (info != &retained_info) errors++;
    released++;
}

static void muted_callback(void *target, Flags flags, void *info)
{
    (void)target; (void)flags; (void)info;
    muted_calls++;
}

static void active_callback(void *target, Flags flags, void *info)
{
    calls++;
    if (target != active || info != &retained_info || (flags & local_flags) != local_flags || released != 1) errors++;
    if (!SCNetworkReachabilitySetCallback(target, 0, 0) || released != 1) errors++;
    if (!SCNetworkReachabilityUnscheduleFromRunLoop(target, CFRunLoopGetMain(), kCFRunLoopCommonModes)) errors++;
    CFRelease(target);
    active = 0;
}

static void name_callback(void *target, Flags flags, void *info)
{
    (void)info;
    name_calls++;
    if (target != named) errors++;
    if (flags & (1u << 16)) {
        if (!(flags & 2)) errors++;
        name_local++;
        if (!SCNetworkReachabilityUnscheduleFromRunLoop(target, CFRunLoopGetMain(), kCFRunLoopCommonModes)) errors++;
    }
}

static void route_callback(void *target, Flags flags, void *info)
{
    (void)info;
    if (target != route) errors++;
    route_flags = flags;
    route_calls++;
}

int reachability_check(int stage)
{
    if (stage == 5) {
        Flags flags = 0;
        return SCNetworkReachabilityGetFlags(route, &flags) ? (int)flags : -1;
    }
    if (stage == 1) {
        route = SCNetworkReachabilityCreateWithName(0, "127.0.0.1");
        if (!route || !SCNetworkReachabilitySetCallback(route, route_callback, 0) ||
            !SCNetworkReachabilityScheduleWithRunLoop(route, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return -30;
        return 1;
    }
    if (stage >= 2 && stage <= 4) {
        int expected = stage - 1;
        if (errors || route_calls > expected) return -31;
        if (route_calls < expected) return 0;
        if (!!(route_flags & 2) != (stage != 3)) return -32;
        if (stage == 4) {
            SCNetworkReachabilityUnscheduleFromRunLoop(route, CFRunLoopGetMain(), kCFRunLoopCommonModes);
            CFRelease(route);
            route = 0;
        }
        return 1;
    }
    if (finished) return errors || muted_calls || calls != 1 || retained != 3 || released != 2 ? -25 : 1;
    if (errors || muted_calls || calls > 1 || name_local > 1 || name_calls > 2) return -20;
    if (calls != 1 || name_local != 1) return 0;
    if (retained != 2 || released != 2 || active) return -21;
    Flags flags = 0;
    if (!SCNetworkReachabilityGetFlags(named, &flags) || !(flags & 2)) return -22;
    CFRelease(muted);
    CFRelease(named);
    muted = named = 0;
    void *pending = SCNetworkReachabilityCreateWithName(0, "localhost");
    Context context = {0, &original_info, retain_info, release_info, 0};
    if (!pending || !SCNetworkReachabilitySetCallback(pending, muted_callback, &context) ||
        !SCNetworkReachabilityScheduleWithRunLoop(pending, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return -23;
    if (!SCNetworkReachabilityUnscheduleFromRunLoop(pending, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return -24;
    finished = 1;
    puts("reachability: callbacks, DNS, cancellation and reentrant ownership passed");
    return 1;
}

int main(void)
{
    if (started) {
        int result = reachability_check(0);
        return result == 1 ? 0 : result == 0 ? 199 : 300 - result;
    }
    unsigned char v4[16] = {16, 2, 0, 0, 127, 0, 0, 1};
    unsigned char v6[28] = {28, 30};
    v6[23] = 1;
    unsigned char invalid[16] = {16, 1};
    Flags flags = 0;
    if (SCNetworkReachabilityCreateWithAddress(0, invalid) || SCNetworkReachabilityCreateWithAddress(0, 0) ||
        SCNetworkReachabilityCreateWithName(0, 0) || SCNetworkReachabilityCreateWithName(0, "")) return 1;
    void *object = SCNetworkReachabilityCreateWithAddress(0, v4);
    if (!object || !SCNetworkReachabilityGetFlags(object, &flags) || (flags & local_flags) != local_flags) return 2;
    if (CFRetain(object) != object) return 3;
    CFRelease(object);
    if (!SCNetworkReachabilityGetFlags(object, &flags)) return 4;
    CFRelease(object);
    object = SCNetworkReachabilityCreateWithAddress(0, v6);
    if (!object || !SCNetworkReachabilityGetFlags(object, &flags)) return 5;
    if ((flags & 2) && (flags & local_flags) != local_flags) return 6;
    CFRelease(object);
    object = SCNetworkReachabilityCreateWithName(0, "127.0.0.1");
    if (!object || !SCNetworkReachabilityGetFlags(object, &flags) || (flags & local_flags) != local_flags) return 7;
    CFRelease(object);
    unsigned char zero[28] = {28, 30};
    object = SCNetworkReachabilityCreateWithAddress(0, zero);
    if (!object || !SCNetworkReachabilityGetFlags(object, &flags)) return 8;
    if (flags & ((1u << 16) | (1u << 17))) return 9;
    CFRelease(object);
    flags = 0x5a5a5a5a;
    if (SCNetworkReachabilityGetFlags(0, &flags) || flags != 0x5a5a5a5a) return 10;
    active = SCNetworkReachabilityCreateWithAddress(0, v4);
    if (SCNetworkReachabilityGetFlags(active, 0)) return 11;
    Context context = {0, &original_info, retain_info, release_info, 0};
    Context invalid_context = {1, &original_info, retain_info, release_info, 0};
    if (SCNetworkReachabilitySetCallback(active, active_callback, &invalid_context) || retained) return 12;
    if (!SCNetworkReachabilitySetCallback(active, muted_callback, &context) || retained != 1 || released) return 13;
    if (!SCNetworkReachabilitySetCallback(active, active_callback, &context) || retained != 2 || released != 1) return 14;
    if (SCNetworkReachabilityScheduleWithRunLoop(active, 0, kCFRunLoopCommonModes)) return 15;
    if (!SCNetworkReachabilityScheduleWithRunLoop(active, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return 16;
    muted = SCNetworkReachabilityCreateWithName(0, "127.0.0.1");
    if (!muted || !SCNetworkReachabilitySetCallback(muted, muted_callback, 0) ||
        !SCNetworkReachabilityScheduleWithRunLoop(muted, CFRunLoopGetMain(), kCFRunLoopCommonModes) ||
        !SCNetworkReachabilityUnscheduleFromRunLoop(muted, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return 17;
    named = SCNetworkReachabilityCreateWithName(0, "localhost");
    if (!named || !SCNetworkReachabilitySetCallback(named, name_callback, 0) ||
        !SCNetworkReachabilityScheduleWithRunLoop(named, CFRunLoopGetMain(), kCFRunLoopCommonModes)) return 18;
    if (calls || muted_calls || name_calls || errors) return 19;
    started = 1;
    return 0;
}
