typedef unsigned long Size;
typedef signed char BOOL;
__attribute__((objc_root_class))
@interface NSObject { Class isa; }
+ (id)alloc;
+ (id)new;
- (id)init;
@end
@interface NSString : NSObject
- (const char *)UTF8String;
- (id)dataUsingEncoding:(Size)encoding;
@end
@interface NSNumber : NSObject
+ (id)numberWithInt:(int)value;
- (long)integerValue;
@end
@interface NSData : NSObject
+ (id)dataWithBytes:(const void *)bytes length:(Size)length;
+ (id)dataWithContentsOfFile:(id)path;
- (const void *)bytes;
- (Size)length;
@end
@interface NSDictionary : NSObject
- (id)objectForKeyedSubscript:(id)key;
- (Size)count;
@end
@interface NSURL : NSObject
+ (id)fileURLWithPath:(id)path;
@end
@interface NSError : NSObject
- (long)code;
- (NSString *)domain;
@end
@interface NSRunLoop : NSObject
+ (id)mainRunLoop;
@end
@interface NSStream : NSObject
- (void)open;
- (void)close;
- (Size)streamStatus;
- (NSError *)streamError;
- (void)setDelegate:(id)delegate;
- (id)delegate;
- (void)scheduleInRunLoop:(id)loop forMode:(id)mode;
- (void)removeFromRunLoop:(id)loop forMode:(id)mode;
- (id)propertyForKey:(id)key;
- (BOOL)setProperty:(id)value forKey:(id)key;
@end
@interface NSInputStream : NSStream
+ (id)inputStreamWithData:(id)data;
+ (id)inputStreamWithFileAtPath:(id)path;
+ (id)inputStreamWithURL:(id)url;
- (id)initWithData:(id)data;
- (long)read:(void *)buffer maxLength:(Size)length;
- (BOOL)getBuffer:(void **)buffer length:(Size *)length;
- (BOOL)hasBytesAvailable;
@end
@interface NSOutputStream : NSStream
+ (id)outputStreamToMemory;
+ (id)outputStreamToFileAtPath:(id)path append:(BOOL)append;
- (long)write:(const void *)buffer maxLength:(Size)length;
- (BOOL)hasSpaceAvailable;
@end
@interface NSXMLParser : NSObject
- (id)initWithData:(id)data;
- (id)initWithStream:(id)stream;
- (id)initWithContentsOfURL:(id)url;
- (void)setDelegate:(id)delegate;
- (void)setShouldProcessNamespaces:(BOOL)value;
- (void)setShouldReportNamespacePrefixes:(BOOL)value;
- (BOOL)shouldProcessNamespaces;
- (BOOL)parse;
- (void)abortParsing;
- (NSError *)parserError;
- (long)lineNumber;
@end
extern int memcmp(const void *, const void *, Size);
extern int strcmp(const char *, const char *);
extern Size strlen(const char *);
extern void *memcpy(void *, const void *, Size);

static int equal(id string, const char *expected)
{
    return string && !strcmp([string UTF8String], expected);
}

static int inherited_reads;
@interface Multipart : NSInputStream
@end
@implementation Multipart
- (long)read:(void *)buffer maxLength:(Size)length
{
    inherited_reads++;
    return [super read:buffer maxLength:length > 2 ? 2 : length];
}
@end

static int events, opened, ended, stream_failures, event_error, muted_events;
static char received[16];
static Size received_length;
static NSInputStream *active, *muted, *missing;
@interface StreamObserver : NSObject
@end
@implementation StreamObserver
- (void)stream:(NSInputStream *)stream handleEvent:(Size)event
{
    if (stream == muted) { muted_events++; return; }
    if (stream == missing) { if (event == 8 && [stream streamError]) stream_failures++; else event_error++; return; }
    if (stream != active) { event_error++; return; }
    events |= (int)event;
    if (event == 1) opened++;
    else if (event == 2) {
        if (opened != 1 || ended) event_error++;
        char bytes[2];
        long n = [stream read:bytes maxLength:sizeof(bytes)];
        if (n <= 0 || received_length + (Size)n > sizeof(received)) { event_error++; return; }
        memcpy(received + received_length, bytes, (Size)n);
        received_length += (Size)n;
    } else if (event == 16) ended++;
    else event_error++;
}
@end

static int starts, ends, documents, failures, mappings, unmappings, instructions, xml_error, abort_now;
static char text[128];
static Size text_length;
@interface XMLObserver : NSObject
@end
@implementation XMLObserver
- (void)parserDidStartDocument:(NSXMLParser *)parser { (void)parser; documents++; }
- (void)parserDidEndDocument:(NSXMLParser *)parser { (void)parser; documents++; }
- (void)parser:(NSXMLParser *)parser didStartElement:(id)name namespaceURI:(id)uri qualifiedName:(id)qualified attributes:(NSDictionary *)attributes
{
    starts++;
    if (abort_now) { [parser abortParsing]; return; }
    if (equal(name, "root")) {
        if (!equal(uri, "urn:root") || !equal(qualified, "root") || [attributes count] != 1 || !equal(attributes[@"a"], "A&B")) xml_error++;
    } else if (equal(name, "child")) {
        if (!equal(uri, "urn:item") || !equal(qualified, "p:child") || !equal(attributes[@"id"], "7")) xml_error++;
    } else xml_error++;
}
- (void)parser:(NSXMLParser *)parser didEndElement:(id)name namespaceURI:(id)uri qualifiedName:(id)qualified
{
    (void)parser; (void)name; (void)uri; (void)qualified; ends++;
}
- (void)parser:(NSXMLParser *)parser foundCharacters:(id)value
{
    (void)parser;
    const char *bytes = [value UTF8String];
    Size length = strlen(bytes);
    if (text_length + length >= sizeof(text)) { xml_error++; return; }
    memcpy(text + text_length, bytes, length);
    text_length += length;
    text[text_length] = 0;
}
- (void)parser:(NSXMLParser *)parser didStartMappingPrefix:(id)prefix toURI:(id)uri
{
    (void)parser;
    if ((!equal(prefix, "") || !equal(uri, "urn:root")) && (!equal(prefix, "p") || !equal(uri, "urn:item"))) xml_error++;
    mappings++;
}
- (void)parser:(NSXMLParser *)parser didEndMappingPrefix:(id)prefix { (void)parser; (void)prefix; unmappings++; }
- (void)parser:(NSXMLParser *)parser foundProcessingInstructionWithTarget:(id)target data:(id)data
{
    (void)parser;
    if (!equal(target, "step") || !equal(data, "go")) xml_error++;
    instructions++;
}
- (void)parser:(NSXMLParser *)parser parseErrorOccurred:(id)error { (void)parser; if (error) failures++; }
@end

int main(void)
{
    const unsigned char bytes[] = {0, 255, 1, 2, 0, 3};
    NSData *data = [NSData dataWithBytes:bytes length:sizeof(bytes)];
    NSInputStream *input = [NSInputStream inputStreamWithData:data];
    unsigned char buffer[16];
    if (!input || [input streamStatus] || [input read:buffer maxLength:1] != -1) return 1;
    [input open];
    void *direct = 0; Size length = 0;
    if ([input streamStatus] != 2 || ![input hasBytesAvailable] || ![input getBuffer:&direct length:&length] || length != 6 || memcmp(direct, bytes, 6)) return 2;
    if ([input read:buffer maxLength:2] != 2 || memcmp(buffer, bytes, 2)) return 3;
    if ([input read:buffer maxLength:16] != 4 || memcmp(buffer, bytes + 2, 4) || [input streamStatus] != 5 || [input hasBytesAvailable]) return 4;
    if ([input read:buffer maxLength:1] || ![input setProperty:@1 forKey:@"NSStreamFileCurrentOffsetKey"] || [input read:buffer maxLength:1] != 1 || buffer[0] != 255) return 5;
    [input close];
    [input open];
    if ([input streamStatus] != 6 || [input read:buffer maxLength:1] != -1) return 6;
    NSOutputStream *memory = [NSOutputStream outputStreamToMemory];
    [memory open];
    if (![memory hasSpaceAvailable] || [memory write:bytes maxLength:6] != 6) return 7;
    NSData *snapshot = [memory propertyForKey:@"NSStreamDataWrittenToMemoryStreamKey"];
    if ([memory write:"x" maxLength:1] != 1 || [snapshot length] != 6 || memcmp([snapshot bytes], bytes, 6)) return 8;
    [memory close];
    if ([[memory propertyForKey:@"NSStreamDataWrittenToMemoryStreamKey"] length] != 7) return 9;
    NSOutputStream *file = [NSOutputStream outputStreamToFileAtPath:@"/var/mobile/stream.bin" append:0];
    [file open];
    if ([file write:bytes maxLength:6] != 6) return 10;
    [file close];
    file = [NSOutputStream outputStreamToFileAtPath:@"/var/mobile/stream.bin" append:1];
    [file open];
    if ([file write:"Z" maxLength:1] != 1) return 11;
    [file close];
    input = [NSInputStream inputStreamWithURL:[NSURL fileURLWithPath:@"/var/mobile/stream.bin"]];
    [input open];
    if ([input read:buffer maxLength:16] != 7 || memcmp(buffer, bytes, 6) || buffer[6] != 'Z') return 12;
    if ([[input propertyForKey:@"NSStreamFileCurrentOffsetKey"] integerValue] != 7 || [input read:buffer maxLength:1] || [input streamStatus] != 5) return 13;
    [input close];
    input = [[Multipart alloc] initWithData:data];
    [input open];
    if ([input read:buffer maxLength:16] != 2 || inherited_reads != 1 || memcmp(buffer, bytes, 2)) return 14;
    [input close];
    XMLObserver *xml_observer = [XMLObserver new];
    NSData *xml = [@"<?xml version=\"1.0\"?><root xmlns=\"urn:root\" xmlns:p=\"urn:item\" a=\"A&amp;B\">中文<p:child id=\"7\">&#x1F600;&lt;</p:child><?step go?></root>" dataUsingEncoding:4];
    NSXMLParser *parser = [[NSXMLParser alloc] initWithStream:[NSInputStream inputStreamWithData:xml]];
    [parser setDelegate:xml_observer];
    [parser setShouldProcessNamespaces:1];
    [parser setShouldReportNamespacePrefixes:1];
    if (![parser shouldProcessNamespaces] || ![parser parse] || [parser parserError]) return 20;
    if (xml_error || starts != 2 || ends != 2 || documents != 2 || failures || mappings != 2 || unmappings != 2 || instructions != 1 || strcmp(text, "中文😀<")) return 21;
    parser = [[NSXMLParser alloc] initWithData:[@"<root>\n<bad></root>" dataUsingEncoding:4]];
    [parser setDelegate:xml_observer];
    if ([parser parse] || ![parser parserError] || !equal([[parser parserError] domain], "NSXMLParserErrorDomain") || failures != 1 || [parser lineNumber] != 2) return 22;
    parser = [[NSXMLParser alloc] initWithData:[@"<root a=\"1\" a=\"2\"/>" dataUsingEncoding:4]];
    if ([parser parse] || ![parser parserError]) return 23;
    parser = [[NSXMLParser alloc] initWithData:[@"<root>&missing;</root>" dataUsingEncoding:4]];
    if ([parser parse] || ![parser parserError]) return 24;
    abort_now = 1;
    parser = [[NSXMLParser alloc] initWithData:xml];
    [parser setDelegate:xml_observer];
    if ([parser parse] || [[parser parserError] code] != 512 || failures != 2) return 25;
    abort_now = 0;
    parser = [[NSXMLParser alloc] initWithData:[@"<empty/>" dataUsingEncoding:4]];
    if (![parser parse]) return 26;
    StreamObserver *observer = [StreamObserver new];
    active = [NSInputStream inputStreamWithData:[@"abcdefg" dataUsingEncoding:4]];
    [active setDelegate:observer];
    [active scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:@"NSDefaultRunLoopMode"];
    [active open];
    muted = [NSInputStream inputStreamWithData:data];
    [muted setDelegate:observer];
    [muted scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:@"NSDefaultRunLoopMode"];
    [muted open];
    [muted removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:@"NSDefaultRunLoopMode"];
    missing = [NSInputStream inputStreamWithFileAtPath:@"/var/mobile/absent-stream.bin"];
    [missing setDelegate:observer];
    [missing scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:@"NSDefaultRunLoopMode"];
    [missing open];
    if ([missing streamStatus] != 7 || ![missing streamError] || events || stream_failures || muted_events) return 30;
    return 0;
}

int streams_check(int stage)
{
    if (!stage) return !event_error && !muted_events && events == 19 && opened == 1 && ended == 1 && stream_failures == 1 && received_length == 7 && !memcmp(received, "abcdefg", 7);
    NSInputStream *pending = [NSInputStream inputStreamWithFileAtPath:@"/var/mobile/stream.bin"];
    [pending scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:@"NSDefaultRunLoopMode"];
    [pending open];
    return [pending streamStatus] == 2;
}