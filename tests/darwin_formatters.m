typedef unsigned long Size;
typedef signed char BOOL;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
@end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)value;
- (const char *)UTF8String;
- (id)dataUsingEncoding:(Size)encoding;
@end
@interface NSData : NSObject
- (BOOL)writeToFile:(id)path atomically:(BOOL)atomic;
@end
@interface NSDate : NSObject
+ (id)date;
+ (id)dateWithTimeIntervalSince1970:(double)seconds;
+ (id)dateWithTimeIntervalSinceReferenceDate:(double)seconds;
- (double)timeIntervalSince1970;
@end
@interface NSLocale : NSObject
+ (id)currentLocale;
+ (id)localeWithLocaleIdentifier:(id)name;
- (id)localeIdentifier;
@end
@interface NSTimeZone : NSObject
+ (id)localTimeZone;
+ (id)timeZoneWithName:(id)name;
+ (id)timeZoneForSecondsFromGMT:(long)seconds;
- (id)name;
- (long)secondsFromGMTForDate:(id)date;
@end
@interface NSDateFormatter : NSObject
- (void)setDateFormat:(id)format;
- (id)dateFormat;
- (void)setLocale:(id)locale;
- (void)setTimeZone:(id)zone;
- (void)setDateStyle:(long)style;
- (void)setTimeStyle:(long)style;
- (id)stringFromDate:(id)date;
- (NSDate *)dateFromString:(id)string;
@end
@interface NSByteCountFormatter : NSObject
+ (id)stringFromByteCount:(long long)count countStyle:(long)style;
- (id)stringFromByteCount:(long long)count;
- (void)setCountStyle:(long)style;
- (void)setAllowedUnits:(Size)units;
- (void)setIncludesActualByteCount:(BOOL)value;
- (void)setIncludesCount:(BOOL)value;
- (void)setIncludesUnit:(BOOL)value;
- (void)setZeroPadsFractionDigits:(BOOL)value;
- (void)setAdaptive:(BOOL)value;
- (void)setAllowsNonnumericFormatting:(BOOL)value;
- (void)setLocale:(id)locale;
@end
@interface NSBundle : NSObject
+ (id)bundleWithPath:(id)path;
@end
extern unsigned int CFBundleGetVersionNumber(id bundle);
extern int strcmp(const char *, const char *);
extern int snprintf(char *, Size, const char *, ...);
extern long time(long *);

static int same(id value, const char *expected)
{
    const char *actual = value ? [value UTF8String] : "<nil>";
    if (!strcmp(actual, expected)) return 1;
    extern int puts(const char *);
    puts(actual);
    puts(expected);
    return 0;
}

static int version(const char *text, unsigned int expected)
{
    char xml[512];
    snprintf(xml, sizeof(xml), "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleVersion</key><string>%s</string></dict></plist>", text);
    NSData *data = [[NSString stringWithUTF8String:xml] dataUsingEncoding:4];
    if (![data writeToFile:@"/var/mobile/version/Info.plist" atomically:1]) return 0;
    NSBundle *bundle = [NSBundle bundleWithPath:@"/var/mobile/version"];
    return bundle && CFBundleGetVersionNumber(bundle) == expected;
}

int main(void)
{
    if (!same([[NSLocale currentLocale] localeIdentifier], "en_US")) return 1;
    if (!same([[NSTimeZone localTimeZone] name], "Asia/Shanghai")) return 2;
    NSDateFormatter *format = [NSDateFormatter new];
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:1719765000];
    [format setDateFormat:@"MM-dd"];
    if (!same([format stringFromDate:date], "07-01")) return 3;
    NSTimeZone *gmt = [NSTimeZone timeZoneForSecondsFromGMT:0];
    if (!gmt) return 4;
    [format setTimeZone:gmt];
    if (!same([format stringFromDate:date], "06-30")) return 5;
    [format setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
    [format setDateFormat:@"yyyy-MM-dd'T'HH:mm:ss.SSSXXX"];
    date = [NSDate dateWithTimeIntervalSince1970:1719765000.125];
    if (!same([format stringFromDate:date], "2024-06-30T16:30:00.125Z")) return 6;
    NSDate *parsed = [format dateFromString:@"2024-06-30T16:30:00.125Z"];
    if (!parsed || [parsed timeIntervalSince1970] != 1719765000.125) return 7;
    if ([format dateFromString:@"2024-02-30T12:00:00.000Z"] || [format dateFromString:@"2024-02-29T12:00:00.000Zgarbage"]) return 8;
    NSTimeZone *ny = [NSTimeZone timeZoneWithName:@"America/New_York"];
    NSDate *before = [NSDate dateWithTimeIntervalSince1970:1710053999];
    NSDate *after = [NSDate dateWithTimeIntervalSince1970:1710054000];
    if (!ny || [ny secondsFromGMTForDate:before] != -18000 || [ny secondsFromGMTForDate:after] != -14400) return 9;
    [format setTimeZone:ny];
    [format setDateFormat:@"yyyy-MM-dd HH:mm:ss XXX"];
    if (!same([format stringFromDate:before], "2024-03-10 01:59:59 -05:00") || !same([format stringFromDate:after], "2024-03-10 03:00:00 -04:00")) return 10;
    [format setTimeZone:gmt];
    [format setLocale:[NSLocale localeWithLocaleIdentifier:@"fr_FR"]];
    [format setDateFormat:@"MMMM"];
    if (!same([format stringFromDate:date], "juin")) return 11;
    [format setLocale:[NSLocale localeWithLocaleIdentifier:@"zh_CN"]];
    [format setTimeZone:[NSTimeZone localTimeZone]];
    [format setDateFormat:@"yyyy年MM月dd日 '😀'"];
    if (!same([format stringFromDate:date], "2024年07月01日 😀")) return 12;
    [format setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
    [format setDateFormat:@"yyyy-MM-dd"];
    if (![format dateFromString:@"2024-02-29"] || [format dateFromString:@"2023-02-29"]) return 13;
    if ([NSTimeZone timeZoneWithName:@"Invalid/Zone"] || [NSTimeZone timeZoneForSecondsFromGMT:64801]) return 14;
    double now = [[NSDate date] timeIntervalSince1970];
    long clock = time(0);
    if (now < (double)clock - 2 || now > (double)clock + 2) return 15;
    if ([[NSDate dateWithTimeIntervalSinceReferenceDate:0] timeIntervalSince1970] != 978307200) return 16;
    if (!same([NSByteCountFormatter stringFromByteCount:1048576 countStyle:1], "1 MB")) return 20;
    if (!same([NSByteCountFormatter stringFromByteCount:1000000 countStyle:0], "1 MB")) return 21;
    if (!same([NSByteCountFormatter stringFromByteCount:1000000 countStyle:1], "977 KB")) return 22;
    if (!same([NSByteCountFormatter stringFromByteCount:0 countStyle:1], "Zero KB")) return 23;
    if (!same([NSByteCountFormatter stringFromByteCount:-1 countStyle:0], "-1 byte")) return 24;
    if (!same([NSByteCountFormatter stringFromByteCount:-9223372036854775807LL - 1 countStyle:3], "-8 EB")) return 25;
    NSByteCountFormatter *bytes = [NSByteCountFormatter new];
    [bytes setAllowedUnits:4];
    [bytes setZeroPadsFractionDigits:1];
    if (!same([bytes stringFromByteCount:1000000], "1.0 MB")) return 26;
    [bytes setIncludesActualByteCount:1];
    if (!same([bytes stringFromByteCount:1000000], "1.0 MB (1,000,000 bytes)")) return 27;
    [bytes setIncludesActualByteCount:0];
    [bytes setLocale:[NSLocale localeWithLocaleIdentifier:@"de_DE"]];
    if (!same([bytes stringFromByteCount:1500000], "1,5 MB")) return 28;
    [bytes setIncludesCount:0];
    if (!same([bytes stringFromByteCount:1500000], "MB")) return 29;
    [bytes setIncludesUnit:0];
    if (!same([bytes stringFromByteCount:1500000], "")) return 30;
    [bytes setIncludesCount:1];
    [bytes setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
    [bytes setAllowedUnits:1];
    if (!same([bytes stringFromByteCount:9223372036854775807LL], "9,223,372,036,854,775,807")) return 31;
    if (!version("12.3.4b5", 0x12346005) || !version("1.2.3", 0x01238000) || !version(".5", 0x00508000)) return 40;
    if (!version("99.9.9f255", 0x999980ff) || !version("1.2.3d4", 0x01232004) || !version("1.2.3a4", 0x01234004)) return 41;
    if (!version("100.1", 0) || !version("1.2.3b256", 0) || !version("invalid", 0) || CFBundleGetVersionNumber(0)) return 42;
    return 0;
}