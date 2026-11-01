#ifndef LIXOS_CRYPT_H
#define LIXOS_CRYPT_H

#include <stdint.h>
#include <stddef.h>
#include "aes.h"
#include "xts.h"
#include "pbkdf2.h"

constexpr uint32_t LIXOS_CRYPT_MAGIC      = 0x58494C43; // 'CLIX'
constexpr uint32_t LIXOS_CRYPT_VERSION    = 1;
constexpr size_t   LIXOS_CRYPT_SALT_LEN   = 32;
constexpr size_t   LIXOS_CRYPT_TAG_LEN    = 32;
constexpr uint32_t LIXOS_CRYPT_ITERATIONS = 5000;

// Disk/file header for encrypted volumes/files (exact 512 bytes = 1 sector)
struct __attribute__((packed)) lixos_crypt_header {
    uint32_t magic;                              // 0x58494C43
    uint32_t version;                            // 1
    uint32_t kdf_type;                           // 1 = PBKDF2-HMAC-SHA256
    uint32_t iterations;                         // Iteration count (e.g. 5000)
    uint8_t  salt[LIXOS_CRYPT_SALT_LEN];         // 32-byte salt
    uint8_t  key_check_tag[LIXOS_CRYPT_TAG_LEN]; // HMAC-SHA256(derived_key, "LIXOS_KEY_VALIDATION_TAG")
    uint64_t data_payload_bytes;                 // Unencrypted payload length
    uint8_t  reserved[424];                      // Pad to exactly 512 bytes (88 + 424 = 512)
};

static_assert(sizeof(lixos_crypt_header) == 512, "lixos_crypt_header must be exactly 512 bytes");

// Cryptographic context for unlocking/encrypting files or volumes
struct lixos_crypt_ctx {
    xts_aes256_ctx xts;
    bool           unlocked;
    uint8_t        master_key[64]; // 512-bit XTS key (256-bit Key1 + 256-bit Key2)
};

// Initialize an encrypted header with a new random/pseudo salt and password
void crypt_init_header(lixos_crypt_header *hdr, const char *password, size_t data_length);

// Attempt to unlock a crypt header using a password. Returns true on success, false on wrong password/bad magic
bool crypt_unlock(const lixos_crypt_header *hdr, const char *password, lixos_crypt_ctx *out_ctx);

#endif // LIXOS_CRYPT_H
