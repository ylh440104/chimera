static uint32_t sha1_rotate(uint32_t n, unsigned bits)
{
    return (n << bits) | (n >> (32 - bits));
}

static void sha1_block(uint32_t h[5], const uint8_t p[64])
{
    uint32_t w[80];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 |
               (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    }
    for (unsigned i = 16; i < 80; i++) w[i] = sha1_rotate(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
    for (unsigned i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = sha1_rotate(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rotate(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static unsigned char *CC_SHA1_impl(const void *bytes, unsigned int length, unsigned char *digest)
{
    if ((!bytes && length) || !digest) return NULL;
    const uint8_t *p = bytes;
    uint32_t h[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    unsigned left = length;
    while (left >= 64) { sha1_block(h, p); p += 64; left -= 64; }
    uint8_t tail[128] = {0};
    if (left) memcpy(tail, p, left);
    tail[left] = 0x80;
    unsigned end = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)length * 8;
    for (unsigned i = 0; i < 8; i++) tail[end - 1 - i] = (uint8_t)(bits >> (i * 8));
    sha1_block(h, tail);
    if (end == 128) sha1_block(h, tail + 64);
    for (unsigned i = 0; i < 20; i++) digest[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
    return digest;
}