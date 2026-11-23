typedef unsigned char Byte;
typedef unsigned long Size;
typedef struct {
    const Byte *next_in;
    unsigned avail_in;
    Size total_in;
    Byte *next_out;
    unsigned avail_out;
    Size total_out;
    const char *msg;
    void *state;
    void *(*zalloc)(void *, unsigned, unsigned);
    void (*zfree)(void *, void *);
    void *opaque;
    int data_type;
    Size adler, reserved;
} Stream;
_Static_assert(sizeof(Stream) == 112, "Darwin arm64 z_stream size");
extern int deflateInit2_(Stream *, int, int, int, int, int, const char *, int);
extern int deflate(Stream *, int);
extern int deflateEnd(Stream *);
extern int inflateInit2_(Stream *, int, const char *, int);
extern int inflate(Stream *, int);
extern int inflateEnd(Stream *);
extern Size crc32(Size, const Byte *, unsigned);
extern const unsigned *get_crc_table(void);
extern int memcmp(const void *, const void *, Size);
extern void *calloc(Size, Size);
extern void free(void *);

static int active_blocks;
static void *allocate(void *opaque, unsigned count, unsigned size)
{
    if (opaque != &active_blocks) return (void *)0;
    void *p = calloc(count, size);
    if (p) active_blocks++;
    return p;
}
static void release(void *opaque, void *block)
{
    if (opaque == &active_blocks && block) active_blocks--;
    free(block);
}

static int decode(const Byte *bytes, unsigned length)
{
    Byte output[16] = {0};
    Stream s = {.next_in = bytes, .avail_in = length, .next_out = output, .avail_out = sizeof(output)};
    if (inflateInit2_(&s, -15, "1.2.11", sizeof(s)) != 0) return 170;
    int result = inflate(&s, 4);
    if (inflateEnd(&s) != 0 || result != 1 || s.total_in != length || s.total_out != 9 ||
        memcmp(output, "123456789", 9)) return 171;
    return 0;
}

int main(void)
{
    static const Byte reference[] = {0x33,0x34,0x32,0x36,0x31,0x35,0x33,0xb7,0xb0,0x04,0x00};
    const Byte *input = (const Byte *)"123456789";
    if (crc32(crc32(0, input, 4), input + 4, 5) != 0xcbf43926UL) return 172;
    const unsigned *table = get_crc_table();
    if (!table || table[1] != 0x77073096U) return 173;
    int result = decode(reference, sizeof(reference));
    if (result) return result;
    Byte compressed[64];
    Stream s = {.next_in = input, .avail_in = 4, .next_out = compressed, .avail_out = sizeof(compressed),
                .zalloc = allocate, .zfree = release, .opaque = &active_blocks};
    if (deflateInit2_(&s, 6, 8, -15, 8, 0, "1.2.11", sizeof(s)) || active_blocks < 1) return 174;
    if (deflate(&s, 0) || s.avail_in) return 175;
    s.next_in = input + 4; s.avail_in = 5;
    if (deflate(&s, 4) != 1 || s.total_in != 9 || !s.total_out || s.total_out > sizeof(compressed)) return 176;
    unsigned used = (unsigned)s.total_out;
    if (deflateEnd(&s) || active_blocks) return 177;
    return decode(compressed, used);
}
