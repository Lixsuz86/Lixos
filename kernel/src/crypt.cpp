#include "crypt.h"
#include "sha256.h"
#include "string.h"
#include "timer.h"
#include "rtc.h"

namespace {
    const char *KEY_VALIDATION_STRING = "LIXOS_ENCRYPTED_VOLUME_AUTH_V1";

    // Simple pseudo-random salt generator using RTC time and APIC timer ticks
    void generate_salt(uint8_t salt[LIXOS_CRYPT_SALT_LEN]) {
        rtc_time t = rtc_read_time();
        uint64_t ticks = timer_get_ticks();

        uint8_t seed[32];
        for (size_t i = 0; i < 8; ++i) {
            seed[i] = static_cast<uint8_t>((ticks >> (i * 8)) & 0xFF);
        }
        seed[8]  = t.second;
        seed[9]  = t.minute;
        seed[10] = t.hour;
        seed[11] = t.day;
        seed[12] = t.month;
        seed[13] = static_cast<uint8_t>(t.year & 0xFF);
        seed[14] = static_cast<uint8_t>((t.year >> 8) & 0xFF);
        for (size_t i = 15; i < 32; ++i) {
            seed[i] = static_cast<uint8_t>((seed[i - 1] * 33 + i) ^ 0xA5);
        }

        sha256_hash(seed, 32, salt);
    }
}

void crypt_init_header(lixos_crypt_header *hdr, const char *password, size_t data_length) {
    if (!hdr || !password) return;

    memset(hdr, 0, sizeof(lixos_crypt_header));
    hdr->magic = LIXOS_CRYPT_MAGIC;
    hdr->version = LIXOS_CRYPT_VERSION;
    hdr->kdf_type = 1;
    hdr->iterations = LIXOS_CRYPT_ITERATIONS;
    hdr->data_payload_bytes = data_length;

    generate_salt(hdr->salt);

    // Derive 512-bit master key
    uint8_t master_key[64];
    pbkdf2_hmac_sha256(reinterpret_cast<const uint8_t *>(password), strlen(password),
                       hdr->salt, LIXOS_CRYPT_SALT_LEN,
                       hdr->iterations,
                       master_key, 64);

    // Generate validation tag: HMAC-SHA256(master_key, KEY_VALIDATION_STRING)
    hmac_sha256(master_key, 64,
                KEY_VALIDATION_STRING, strlen(KEY_VALIDATION_STRING),
                hdr->key_check_tag);

    // Wipe sensitive stack memory
    memset(master_key, 0, sizeof(master_key));
}

bool crypt_unlock(const lixos_crypt_header *hdr, const char *password, lixos_crypt_ctx *out_ctx) {
    if (!hdr || !password || !out_ctx) return false;

    if (hdr->magic != LIXOS_CRYPT_MAGIC || hdr->version != LIXOS_CRYPT_VERSION) {
        return false;
    }

    uint8_t derived_key[64];
    pbkdf2_hmac_sha256(reinterpret_cast<const uint8_t *>(password), strlen(password),
                       hdr->salt, LIXOS_CRYPT_SALT_LEN,
                       hdr->iterations,
                       derived_key, 64);

    uint8_t calculated_tag[SHA256_DIGEST_SIZE];
    hmac_sha256(derived_key, 64,
                KEY_VALIDATION_STRING, strlen(KEY_VALIDATION_STRING),
                calculated_tag);

    // Constant-time compare validation tag
    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        diff |= (calculated_tag[i] ^ hdr->key_check_tag[i]);
    }

    if (diff != 0) {
        // Wrong password: wipe derived key and return false
        memset(derived_key, 0, sizeof(derived_key));
        out_ctx->unlocked = false;
        return false;
    }

    // Password verified! Initialize XTS cipher
    memcpy(out_ctx->master_key, derived_key, 64);
    xts_aes256_init(&out_ctx->xts, out_ctx->master_key);
    out_ctx->unlocked = true;

    return true;
}
