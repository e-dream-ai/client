/*
 * RFC 1321 MD5 Message-Digest Algorithm — compact reference-style
 * implementation. Placed in the public domain.
 */
#include "md5.h"
#include <string.h>

typedef struct {
    unsigned int count[2];
    unsigned int state[4];
    unsigned char buffer[64];
} md5_ctx_t;

static void md5_init(md5_ctx_t *ctx)
{
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
}

/* clang-format off */
#define F(x,y,z) (((x) & (y)) | ((~x) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & (~z)))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | (~z)))
#define ROTL32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
/* clang-format on */

#define STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = ROTL32((a), (s)); \
    (a) += (b);

static void md5_transform(unsigned int state[4], const unsigned char block[64])
{
    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned int x[16];
    int i;
    for (i = 0; i < 16; i++)
        x[i] = (unsigned int)block[i * 4] | ((unsigned int)block[i * 4 + 1] << 8) |
               ((unsigned int)block[i * 4 + 2] << 16) | ((unsigned int)block[i * 4 + 3] << 24);

    /* Round 1 */
    STEP(F, a, b, c, d, x[0], 0xd76aa478u, 7);
    STEP(F, d, a, b, c, x[1], 0xe8c7b756u, 12);
    STEP(F, c, d, a, b, x[2], 0x242070dbu, 17);
    STEP(F, b, c, d, a, x[3], 0xc1bdceeeu, 22);
    STEP(F, a, b, c, d, x[4], 0xf57c0fafu, 7);
    STEP(F, d, a, b, c, x[5], 0x4787c62au, 12);
    STEP(F, c, d, a, b, x[6], 0xa8304613u, 17);
    STEP(F, b, c, d, a, x[7], 0xfd469501u, 22);
    STEP(F, a, b, c, d, x[8], 0x698098d8u, 7);
    STEP(F, d, a, b, c, x[9], 0x8b44f7afu, 12);
    STEP(F, c, d, a, b, x[10], 0xffff5bb1u, 17);
    STEP(F, b, c, d, a, x[11], 0x895cd7beu, 22);
    STEP(F, a, b, c, d, x[12], 0x6b901122u, 7);
    STEP(F, d, a, b, c, x[13], 0xfd987193u, 12);
    STEP(F, c, d, a, b, x[14], 0xa679438eu, 17);
    STEP(F, b, c, d, a, x[15], 0x49b40821u, 22);
    /* Round 2 */
    STEP(G, a, b, c, d, x[1], 0xf61e2562u, 5);
    STEP(G, d, a, b, c, x[6], 0xc040b340u, 9);
    STEP(G, c, d, a, b, x[11], 0x265e5a51u, 14);
    STEP(G, b, c, d, a, x[0], 0xe9b6c7aau, 20);
    STEP(G, a, b, c, d, x[5], 0xd62f105du, 5);
    STEP(G, d, a, b, c, x[10], 0x02441453u, 9);
    STEP(G, c, d, a, b, x[15], 0xd8a1e681u, 14);
    STEP(G, b, c, d, a, x[4], 0xe7d3fbc8u, 20);
    STEP(G, a, b, c, d, x[9], 0x21e1cde6u, 5);
    STEP(G, d, a, b, c, x[14], 0xc33707d6u, 9);
    STEP(G, c, d, a, b, x[3], 0xf4d50d87u, 14);
    STEP(G, b, c, d, a, x[8], 0x455a14edu, 20);
    STEP(G, a, b, c, d, x[13], 0xa9e3e905u, 5);
    STEP(G, d, a, b, c, x[2], 0xfcefa3f8u, 9);
    STEP(G, c, d, a, b, x[7], 0x676f02d9u, 14);
    STEP(G, b, c, d, a, x[12], 0x8d2a4c8au, 20);
    /* Round 3 */
    STEP(H, a, b, c, d, x[5], 0xfffa3942u, 4);
    STEP(H, d, a, b, c, x[8], 0x8771f681u, 11);
    STEP(H, c, d, a, b, x[11], 0x6d9d6122u, 16);
    STEP(H, b, c, d, a, x[14], 0xfde5380cu, 23);
    STEP(H, a, b, c, d, x[1], 0xa4beea44u, 4);
    STEP(H, d, a, b, c, x[4], 0x4bdecfa9u, 11);
    STEP(H, c, d, a, b, x[7], 0xf6bb4b60u, 16);
    STEP(H, b, c, d, a, x[10], 0xbebfbc70u, 23);
    STEP(H, a, b, c, d, x[13], 0x289b7ec6u, 4);
    STEP(H, d, a, b, c, x[0], 0xeaa127fau, 11);
    STEP(H, c, d, a, b, x[3], 0xd4ef3085u, 16);
    STEP(H, b, c, d, a, x[6], 0x04881d05u, 23);
    STEP(H, a, b, c, d, x[9], 0xd9d4d039u, 4);
    STEP(H, d, a, b, c, x[12], 0xe6db99e5u, 11);
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8u, 16);
    STEP(H, b, c, d, a, x[2], 0xc4ac5665u, 23);
    /* Round 4 */
    STEP(I, a, b, c, d, x[0], 0xf4292244u, 6);
    STEP(I, d, a, b, c, x[7], 0x432aff97u, 10);
    STEP(I, c, d, a, b, x[14], 0xab9423a7u, 15);
    STEP(I, b, c, d, a, x[5], 0xfc93a039u, 21);
    STEP(I, a, b, c, d, x[12], 0x655b59c3u, 6);
    STEP(I, d, a, b, c, x[3], 0x8f0ccc92u, 10);
    STEP(I, c, d, a, b, x[10], 0xffeff47du, 15);
    STEP(I, b, c, d, a, x[1], 0x85845dd1u, 21);
    STEP(I, a, b, c, d, x[8], 0x6fa87e4fu, 6);
    STEP(I, d, a, b, c, x[15], 0xfe2ce6e0u, 10);
    STEP(I, c, d, a, b, x[6], 0xa3014314u, 15);
    STEP(I, b, c, d, a, x[13], 0x4e0811a1u, 21);
    STEP(I, a, b, c, d, x[4], 0xf7537e82u, 6);
    STEP(I, d, a, b, c, x[11], 0xbd3af235u, 10);
    STEP(I, c, d, a, b, x[2], 0x2ad7d2bbu, 15);
    STEP(I, b, c, d, a, x[9], 0xeb86d391u, 21);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_update(md5_ctx_t *ctx, const unsigned char *data, size_t len)
{
    unsigned int i, idx, part;

    idx = (unsigned int)((ctx->count[0] >> 3) & 0x3F);
    if ((ctx->count[0] += (unsigned int)(len << 3)) < (unsigned int)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (unsigned int)(len >> 29);

    part = 64 - idx;
    if (len >= part) {
        memcpy(ctx->buffer + idx, data, part);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(ctx->state, data + i);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(ctx->buffer + idx, data + i, len - i);
}

static void md5_final(unsigned char digest[16], md5_ctx_t *ctx)
{
    unsigned char bits[8];
    unsigned int idx, padn;
    unsigned int i;

    for (i = 0; i < 8; i++)
        bits[i] = (unsigned char)((ctx->count[i >> 2] >> ((i & 3) << 3)) & 0xFF);

    idx = (unsigned int)((ctx->count[0] >> 3) & 0x3f);
    padn = (idx < 56) ? (56 - idx) : (120 - idx);
    {
        static const unsigned char pad[64] = { 0x80 };
        md5_update(ctx, pad, padn);
    }
    md5_update(ctx, bits, 8);

    for (i = 0; i < 16; i++)
        digest[i] = (unsigned char)((ctx->state[i >> 2] >> ((i & 3) << 3)) & 0xFF);
}

void md5_buffer(const char *buffer, size_t len, void *resblock)
{
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)buffer, len);
    md5_final((unsigned char *)resblock, &ctx);
}
