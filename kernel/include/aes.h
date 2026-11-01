#ifndef LIXOS_AES_H
#define LIXOS_AES_H

#include <stdint.h>
#include <stddef.h>

constexpr size_t AES256_KEY_BYTES = 32;
constexpr size_t AES_BLOCK_SIZE   = 16;
constexpr size_t AES256_ROUNDS    = 14;

struct aes256_ctx {
    uint32_t enc_round_keys[60]; // 15 rounds * 4 words
    uint32_t dec_round_keys[60];
};

// Initialize AES-256 context with 256-bit key
void aes256_init(aes256_ctx *ctx, const uint8_t key[AES256_KEY_BYTES]);

// Encrypt a single 16-byte block
void aes256_encrypt_block(const aes256_ctx *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE]);

// Decrypt a single 16-byte block
void aes256_decrypt_block(const aes256_ctx *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE]);

#endif // LIXOS_AES_H
