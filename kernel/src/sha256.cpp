#include "sha256.h"
#include "string.h"

namespace {
    constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    inline uint32_t ror(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }

    inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    inline uint32_t big_sigma0(uint32_t x) {
        return ror(x, 2) ^ ror(x, 13) ^ ror(x, 22);
    }

    inline uint32_t big_sigma1(uint32_t x) {
        return ror(x, 6) ^ ror(x, 11) ^ ror(x, 25);
    }

    inline uint32_t small_sigma0(uint32_t x) {
        return ror(x, 7) ^ ror(x, 18) ^ (x >> 3);
    }

    inline uint32_t small_sigma1(uint32_t x) {
        return ror(x, 17) ^ ror(x, 19) ^ (x >> 10);
    }

    void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
        uint32_t w[64];

        for (size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }

        for (size_t i = 16; i < 64; ++i) {
            w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];

        for (size_t i = 0; i < 64; ++i) {
            uint32_t t1 = h + big_sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = big_sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
}

void sha256_init(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void sha256_update(sha256_ctx *ctx, const void *data, size_t len) {
    const uint8_t *input = reinterpret_cast<const uint8_t *>(data);
    size_t buffer_idx = static_cast<size_t>((ctx->count / 8) % SHA256_BLOCK_SIZE);
    ctx->count += static_cast<uint64_t>(len) * 8;

    size_t space = SHA256_BLOCK_SIZE - buffer_idx;
    size_t input_idx = 0;

    if (len >= space) {
        memcpy(ctx->buffer + buffer_idx, input, space);
        sha256_transform(ctx->state, ctx->buffer);
        input_idx = space;

        while (input_idx + SHA256_BLOCK_SIZE <= len) {
            sha256_transform(ctx->state, input + input_idx);
            input_idx += SHA256_BLOCK_SIZE;
        }

        buffer_idx = 0;
    }

    memcpy(ctx->buffer + buffer_idx, input + input_idx, len - input_idx);
}

void sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint8_t count_bytes[8];
    for (size_t i = 0; i < 8; ++i) {
        count_bytes[i] = static_cast<uint8_t>((ctx->count >> ((7 - i) * 8)) & 0xFF);
    }

    size_t buffer_idx = static_cast<size_t>((ctx->count / 8) % SHA256_BLOCK_SIZE);
    ctx->buffer[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        memset(ctx->buffer + buffer_idx, 0, SHA256_BLOCK_SIZE - buffer_idx);
        sha256_transform(ctx->state, ctx->buffer);
        buffer_idx = 0;
    }

    memset(ctx->buffer + buffer_idx, 0, 56 - buffer_idx);
    memcpy(ctx->buffer + 56, count_bytes, 8);
    sha256_transform(ctx->state, ctx->buffer);

    for (size_t i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>((ctx->state[i]) & 0xFF);
    }
}

void sha256_hash(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t k_temp[SHA256_DIGEST_SIZE];

    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_hash(key, key_len, k_temp);
        memcpy(k_pad, k_temp, SHA256_DIGEST_SIZE);
        memset(k_pad + SHA256_DIGEST_SIZE, 0, SHA256_BLOCK_SIZE - SHA256_DIGEST_SIZE);
    } else {
        memcpy(k_pad, key, key_len);
        memset(k_pad + key_len, 0, SHA256_BLOCK_SIZE - key_len);
    }

    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];

    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5c;
    }

    // Inner hash: H(ipad || data)
    sha256_ctx ctx;
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    // Outer hash: H(opad || inner_hash)
    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}
