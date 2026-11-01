#ifndef LIXOS_PBKDF2_H
#define LIXOS_PBKDF2_H

#include <stdint.h>
#include <stddef.h>

// PBKDF2 with HMAC-SHA256 (RFC 2898 / RFC 8018)
// Derives 'out_len' bytes of cryptographic key from 'password' and 'salt'
void pbkdf2_hmac_sha256(const uint8_t *password, size_t pass_len,
                        const uint8_t *salt, size_t salt_len,
                        uint32_t iterations,
                        uint8_t *out_key, size_t out_len);

#endif // LIXOS_PBKDF2_H
