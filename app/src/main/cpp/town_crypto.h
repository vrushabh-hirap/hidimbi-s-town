#ifndef TOWN_CRYPTO_H
#define TOWN_CRYPTO_H

// Shared pure-C crypto/encoding helpers for Hidimbi's Town native engine.
// Kept free of any Android/JNI dependency so the exact same code can be
// compiled into libtown.so (NDK/ARM64) and into the host harness used by
// solve_scripts/solve_hidimbi.py for QA verification.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} TOWN_SHA256_CTX;

inline const uint32_t* town_k256() {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    return K;
}

#define TOWN_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define TOWN_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define TOWN_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define TOWN_SIG0(x) (TOWN_ROR(x, 2) ^ TOWN_ROR(x, 13) ^ TOWN_ROR(x, 22))
#define TOWN_SIG1(x) (TOWN_ROR(x, 6) ^ TOWN_ROR(x, 11) ^ TOWN_ROR(x, 25))
#define TOWN_GAM0(x) (TOWN_ROR(x, 7) ^ TOWN_ROR(x, 18) ^ ((x) >> 3))
#define TOWN_GAM1(x) (TOWN_ROR(x, 17) ^ TOWN_ROR(x, 19) ^ ((x) >> 10))

inline void town_sha256_transform(TOWN_SHA256_CTX* ctx, const uint8_t data[]) {
    const uint32_t* K256 = town_k256();
    uint32_t a, b, c, d, e, f, g, h, m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (int i = 16; i < 64; ++i)
        m[i] = TOWN_GAM1(m[i - 2]) + m[i - 7] + TOWN_GAM0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + TOWN_SIG1(e) + TOWN_CH(e, f, g) + K256[i] + m[i];
        uint32_t t2 = TOWN_SIG0(a) + TOWN_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

inline void town_sha256_init(TOWN_SHA256_CTX* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

inline void town_sha256_update(TOWN_SHA256_CTX* ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0)
            town_sha256_transform(ctx, ctx->buffer);
    }
}

inline void town_sha256_final(TOWN_SHA256_CTX* ctx, uint8_t hash[]) {
    size_t i = ctx->count % 64;
    ctx->buffer[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->buffer[i++] = 0x00;
        town_sha256_transform(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    } else {
        while (i < 56) ctx->buffer[i++] = 0x00;
    }
    uint64_t total_bits = ctx->count * 8;
    for (int j = 7; j >= 0; --j)
        ctx->buffer[56 + (7 - j)] = (total_bits >> (j * 8)) & 0xFF;
    town_sha256_transform(ctx, ctx->buffer);
    for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 8; ++k)
            hash[k * 4 + j] = (ctx->state[k] >> (24 - j * 8)) & 0x000000ff;
    }
}

inline void town_sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    TOWN_SHA256_CTX ctx;
    town_sha256_init(&ctx);
    town_sha256_update(&ctx, data, len);
    town_sha256_final(&ctx, out);
}

inline void town_hmac_sha256(const uint8_t* key, size_t key_len,
                             const uint8_t* data, size_t data_len, uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        town_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }

    uint8_t k_ipad[64], k_opad[64];
    for (int i = 0; i < 64; ++i) {
        k_ipad[i] = k[i] ^ 0x36;
        k_opad[i] = k[i] ^ 0x5c;
    }

    uint8_t inner_hash[32];
    TOWN_SHA256_CTX ctx;
    town_sha256_init(&ctx);
    town_sha256_update(&ctx, k_ipad, 64);
    town_sha256_update(&ctx, data, data_len);
    town_sha256_final(&ctx, inner_hash);

    town_sha256_init(&ctx);
    town_sha256_update(&ctx, k_opad, 64);
    town_sha256_update(&ctx, inner_hash, 32);
    town_sha256_final(&ctx, out);
}

// ---- hex helpers -----------------------------------------------------------

inline int town_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes a hex string into out. Returns number of bytes written, or -1.
inline int town_hex_decode(const char* hex, uint8_t* out, size_t out_cap) {
    size_t n = strlen(hex);
    if (n % 2 != 0) return -1;
    if (n / 2 > out_cap) return -1;
    for (size_t i = 0; i < n; i += 2) {
        int hi = town_hex_nibble(hex[i]);
        int lo = town_hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return (int)(n / 2);
}

// Encodes len bytes into a freshly malloc'd NUL-terminated lowercase hex string.
inline char* town_hex_encode(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    char* out = (char*)malloc(len * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; ++i) {
        out[i * 2]     = digits[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = digits[data[i] & 0xF];
    }
    out[len * 2] = 0;
    return out;
}

inline int town_ct_equals(const void* a, const void* b, size_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= (uint8_t)(x[i] ^ y[i]);
    return acc == 0;
}

#endif // TOWN_CRYPTO_H
