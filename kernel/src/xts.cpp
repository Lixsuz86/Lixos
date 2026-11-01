#include "xts.h"
#include "string.h"

namespace {
    void gf_mul_alpha(uint8_t t[16]) {
        uint8_t carry_in = 0;
        for (size_t i = 0; i < 16; ++i) {
            uint8_t carry_out = (t[i] >> 7) & 1;
            t[i] = (t[i] << 1) | carry_in;
            carry_in = carry_out;
        }
        if (carry_in) {
            t[0] ^= 0x87;
        }
    }
}

void xts_aes256_init(xts_aes256_ctx *ctx, const uint8_t key[XTS_AES256_KEY_BYTES]) {
    aes256_init(&ctx->key1, key);
    aes256_init(&ctx->key2, key + 32);
}

void xts_aes256_encrypt(const xts_aes256_ctx *ctx, const uint8_t tweak[AES_BLOCK_SIZE],
                        const uint8_t *plain, uint8_t *cipher, size_t length) {
    if (!ctx || !tweak || !plain || !cipher || length < AES_BLOCK_SIZE) {
        return;
    }

    uint8_t T[AES_BLOCK_SIZE];
    aes256_encrypt_block(&ctx->key2, tweak, T);

    size_t blocks = length / AES_BLOCK_SIZE;
    size_t rem = length % AES_BLOCK_SIZE;

    // Encrypt all full blocks
    for (size_t b = 0; b < (rem ? (blocks - 1) : blocks); ++b) {
        uint8_t PP[AES_BLOCK_SIZE];
        uint8_t CC[AES_BLOCK_SIZE];

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            PP[i] = plain[b * AES_BLOCK_SIZE + i] ^ T[i];
        }

        aes256_encrypt_block(&ctx->key1, PP, CC);

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            cipher[b * AES_BLOCK_SIZE + i] = CC[i] ^ T[i];
        }

        gf_mul_alpha(T);
    }

    // Ciphertext Stealing for partial final block if needed
    if (rem > 0) {
        size_t b = blocks - 1;
        uint8_t PP[AES_BLOCK_SIZE];
        uint8_t CC[AES_BLOCK_SIZE];
        uint8_t T_prev[AES_BLOCK_SIZE];

        memcpy(T_prev, T, AES_BLOCK_SIZE);

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            PP[i] = plain[b * AES_BLOCK_SIZE + i] ^ T[i];
        }

        aes256_encrypt_block(&ctx->key1, PP, CC);
        gf_mul_alpha(T);

        // Copy stolen ciphertext
        for (size_t i = 0; i < rem; ++i) {
            cipher[(b + 1) * AES_BLOCK_SIZE + i] = CC[i] ^ T_prev[i];
        }

        uint8_t P_last[AES_BLOCK_SIZE];
        for (size_t i = 0; i < rem; ++i) {
            P_last[i] = plain[(b + 1) * AES_BLOCK_SIZE + i];
        }
        for (size_t i = rem; i < AES_BLOCK_SIZE; ++i) {
            P_last[i] = CC[i] ^ T_prev[i];
        }

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            PP[i] = P_last[i] ^ T[i];
        }

        aes256_encrypt_block(&ctx->key1, PP, CC);

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            cipher[b * AES_BLOCK_SIZE + i] = CC[i] ^ T[i];
        }
    }
}

void xts_aes256_decrypt(const xts_aes256_ctx *ctx, const uint8_t tweak[AES_BLOCK_SIZE],
                        const uint8_t *cipher, uint8_t *plain, size_t length) {
    if (!ctx || !tweak || !cipher || !plain || length < AES_BLOCK_SIZE) {
        return;
    }

    uint8_t T[AES_BLOCK_SIZE];
    aes256_encrypt_block(&ctx->key2, tweak, T);

    size_t blocks = length / AES_BLOCK_SIZE;
    size_t rem = length % AES_BLOCK_SIZE;

    // Decrypt all full blocks
    for (size_t b = 0; b < (rem ? (blocks - 1) : blocks); ++b) {
        uint8_t CC[AES_BLOCK_SIZE];
        uint8_t PP[AES_BLOCK_SIZE];

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            CC[i] = cipher[b * AES_BLOCK_SIZE + i] ^ T[i];
        }

        aes256_decrypt_block(&ctx->key1, CC, PP);

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            plain[b * AES_BLOCK_SIZE + i] = PP[i] ^ T[i];
        }

        gf_mul_alpha(T);
    }

    if (rem > 0) {
        size_t b = blocks - 1;
        uint8_t T_next[AES_BLOCK_SIZE];
        memcpy(T_next, T, AES_BLOCK_SIZE);
        gf_mul_alpha(T_next);

        uint8_t CC[AES_BLOCK_SIZE];
        uint8_t PP[AES_BLOCK_SIZE];

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            CC[i] = cipher[b * AES_BLOCK_SIZE + i] ^ T_next[i];
        }

        aes256_decrypt_block(&ctx->key1, CC, PP);

        uint8_t C_last[AES_BLOCK_SIZE];
        for (size_t i = 0; i < rem; ++i) {
            plain[(b + 1) * AES_BLOCK_SIZE + i] = PP[i] ^ T_next[i];
            C_last[i] = cipher[(b + 1) * AES_BLOCK_SIZE + i];
        }
        for (size_t i = rem; i < AES_BLOCK_SIZE; ++i) {
            C_last[i] = PP[i] ^ T_next[i];
        }

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            CC[i] = C_last[i] ^ T[i];
        }

        aes256_decrypt_block(&ctx->key1, CC, PP);

        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            plain[b * AES_BLOCK_SIZE + i] = PP[i] ^ T[i];
        }
    }
}

void xts_aes256_encrypt_sector(const xts_aes256_ctx *ctx, uint64_t sector_num,
                              const uint8_t *plain, uint8_t *cipher, size_t sector_size) {
    uint8_t tweak[AES_BLOCK_SIZE] = {0};
    for (size_t i = 0; i < 8; ++i) {
        tweak[i] = static_cast<uint8_t>((sector_num >> (i * 8)) & 0xFF);
    }
    xts_aes256_encrypt(ctx, tweak, plain, cipher, sector_size);
}

void xts_aes256_decrypt_sector(const xts_aes256_ctx *ctx, uint64_t sector_num,
                              const uint8_t *cipher, uint8_t *plain, size_t sector_size) {
    uint8_t tweak[AES_BLOCK_SIZE] = {0};
    for (size_t i = 0; i < 8; ++i) {
        tweak[i] = static_cast<uint8_t>((sector_num >> (i * 8)) & 0xFF);
    }
    xts_aes256_decrypt(ctx, tweak, cipher, plain, sector_size);
}
