#include "pbkdf2.h"
#include "sha256.h"
#include "string.h"

void pbkdf2_hmac_sha256(const uint8_t *password, size_t pass_len,
                        const uint8_t *salt, size_t salt_len,
                        uint32_t iterations,
                        uint8_t *out_key, size_t out_len) {
    if (!password || !salt || iterations == 0 || !out_key || out_len == 0) {
        return;
    }

    size_t blocks = (out_len + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE;
    uint8_t salt_buf[128]; // Stack buffer for salt + 4-byte big-endian counter
    uint8_t *salt_work = salt_buf;

    bool dyn_alloc = false;
    if (salt_len + 4 > sizeof(salt_buf)) {
        // Fallback or limit if salt is unusually huge
        salt_work = salt_buf;
        salt_len = sizeof(salt_buf) - 4;
    }

    memcpy(salt_work, salt, salt_len);

    for (uint32_t block_idx = 1; block_idx <= blocks; ++block_idx) {
        // Append 4-byte big endian integer (1, 2, 3, ...)
        salt_work[salt_len + 0] = static_cast<uint8_t>((block_idx >> 24) & 0xFF);
        salt_work[salt_len + 1] = static_cast<uint8_t>((block_idx >> 16) & 0xFF);
        salt_work[salt_len + 2] = static_cast<uint8_t>((block_idx >> 8) & 0xFF);
        salt_work[salt_len + 3] = static_cast<uint8_t>((block_idx >> 0) & 0xFF);

        uint8_t u[SHA256_DIGEST_SIZE];
        uint8_t f[SHA256_DIGEST_SIZE];

        // U_1 = PRF(P, Salt || INT_32_BE(i))
        hmac_sha256(password, pass_len, salt_work, salt_len + 4, u);
        memcpy(f, u, SHA256_DIGEST_SIZE);

        // U_2 .. U_c
        for (uint32_t iter = 1; iter < iterations; ++iter) {
            hmac_sha256(password, pass_len, u, SHA256_DIGEST_SIZE, u);
            for (size_t k = 0; k < SHA256_DIGEST_SIZE; ++k) {
                f[k] ^= u[k];
            }
        }

        size_t offset = (block_idx - 1) * SHA256_DIGEST_SIZE;
        size_t copy_bytes = (out_len - offset < SHA256_DIGEST_SIZE) ? (out_len - offset) : SHA256_DIGEST_SIZE;
        memcpy(out_key + offset, f, copy_bytes);
    }

    (void)dyn_alloc;
}
