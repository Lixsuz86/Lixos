#ifndef LIXOS_XTS_H
#define LIXOS_XTS_H

#include <stdint.h>
#include <stddef.h>
#include "aes.h"

constexpr size_t XTS_AES256_KEY_BYTES = 64; // 256 bits Key1 + 256 bits Key2

struct xts_aes256_ctx {
    aes256_ctx key1;
    aes256_ctx key2;
};

// Initialize XTS context with 512-bit (64-byte) key
void xts_aes256_init(xts_aes256_ctx *ctx, const uint8_t key[XTS_AES256_KEY_BYTES]);

// Standard XTS-AES-256 Buffer Encryption (with 16-byte initial tweak)
void xts_aes256_encrypt(const xts_aes256_ctx *ctx, const uint8_t tweak[AES_BLOCK_SIZE],
                        const uint8_t *plain, uint8_t *cipher, size_t length);

// Standard XTS-AES-256 Buffer Decryption (with 16-byte initial tweak)
void xts_aes256_decrypt(const xts_aes256_ctx *ctx, const uint8_t tweak[AES_BLOCK_SIZE],
                        const uint8_t *cipher, uint8_t *plain, size_t length);

// Sector-based convenience functions (encodes sector index as 128-bit tweak)
void xts_aes256_encrypt_sector(const xts_aes256_ctx *ctx, uint64_t sector_num,
                              const uint8_t *plain, uint8_t *cipher, size_t sector_size = 512);

void xts_aes256_decrypt_sector(const xts_aes256_ctx *ctx, uint64_t sector_num,
                              const uint8_t *cipher, uint8_t *plain, size_t sector_size = 512);

#endif // LIXOS_XTS_H
