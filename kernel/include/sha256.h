#ifndef LIXOS_SHA256_H
#define LIXOS_SHA256_H

#include <stdint.h>
#include <stddef.h>

constexpr size_t SHA256_DIGEST_SIZE = 32;
constexpr size_t SHA256_BLOCK_SIZE  = 64;

struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;               // Total bits processed
    uint8_t  buffer[SHA256_BLOCK_SIZE]; // 64-byte input chunk buffer
};

// Initialize SHA-256 hashing context
void sha256_init(sha256_ctx *ctx);

// Update SHA-256 context with input data
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);

// Finalize SHA-256 hash and produce 32-byte digest
void sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

// Convenience single-shot SHA-256 hash
void sha256_hash(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

// HMAC-SHA256 (RFC 2104 / FIPS 198-1)
void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST_SIZE]);

#endif // LIXOS_SHA256_H
