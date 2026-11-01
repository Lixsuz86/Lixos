#include "cryptfs.h"
#include "heap.h"
#include "string.h"
#include "kprint.h"

namespace {
    constexpr size_t MAX_CRYPT_FDS = 32;
    constexpr size_t SECTOR_SIZE   = 512;

    crypt_file_handle g_crypt_table[MAX_CRYPT_FDS];

    int alloc_cfd() {
        for (size_t i = 0; i < MAX_CRYPT_FDS; ++i) {
            if (!g_crypt_table[i].in_use) {
                g_crypt_table[i].in_use = true;
                g_crypt_table[i].offset = 0;
                g_crypt_table[i].underlying_fd = -1;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}

void cryptfs_init(void) {
    for (size_t i = 0; i < MAX_CRYPT_FDS; ++i) {
        memset(&g_crypt_table[i], 0, sizeof(crypt_file_handle));
        g_crypt_table[i].underlying_fd = -1;
    }
}

int cryptfs_create(const char *path, const char *password, uint16_t mode) {
    if (!path || !password) {
        return -1;
    }

    int fd = vfs_open(path, O_CREAT | O_RDWR | O_TRUNC, mode);
    if (fd < 0) {
        return fd;
    }

    lixos_crypt_header hdr{};
    crypt_init_header(&hdr, password, 0);

    int64_t written = vfs_write(fd, &hdr, sizeof(lixos_crypt_header));
    vfs_close(fd);

    if (written != static_cast<int64_t>(sizeof(lixos_crypt_header))) {
        return -1;
    }

    return 0;
}

int cryptfs_open(const char *path, const char *password, uint32_t flags) {
    if (!path || !password) {
        return -1;
    }

    int raw_fd = vfs_open(path, flags);
    if (raw_fd < 0) {
        return raw_fd;
    }

    lixos_crypt_header hdr{};
    int64_t bytes_read = vfs_read(raw_fd, &hdr, sizeof(lixos_crypt_header));
    if (bytes_read != static_cast<int64_t>(sizeof(lixos_crypt_header))) {
        vfs_close(raw_fd);
        return -1;
    }

    lixos_crypt_ctx ctx{};
    bool unlocked = crypt_unlock(&hdr, password, &ctx);
    if (!unlocked || !ctx.unlocked) {
        // Wrong password / authentication failed
        vfs_close(raw_fd);
        return ERR_AUTH_FAILED; // -13
    }

    int cfd = alloc_cfd();
    if (cfd < 0) {
        vfs_close(raw_fd);
        return -1;
    }

    g_crypt_table[cfd].underlying_fd = raw_fd;
    g_crypt_table[cfd].header = hdr;
    g_crypt_table[cfd].ctx = ctx;
    g_crypt_table[cfd].offset = 0;

    return cfd;
}

int64_t cryptfs_read(int cfd, void *buffer, size_t count) {
    if (cfd < 0 || cfd >= static_cast<int>(MAX_CRYPT_FDS) || !g_crypt_table[cfd].in_use) {
        return -1;
    }

    crypt_file_handle *h = &g_crypt_table[cfd];
    if (h->offset >= h->header.data_payload_bytes) {
        return 0; // EOF
    }

    size_t avail = static_cast<size_t>(h->header.data_payload_bytes - h->offset);
    size_t to_read = (count < avail) ? count : avail;
    if (to_read == 0) {
        return 0;
    }

    uint8_t *out_ptr = reinterpret_cast<uint8_t *>(buffer);
    size_t bytes_done = 0;

    uint8_t cipher_sector[SECTOR_SIZE];
    uint8_t plain_sector[SECTOR_SIZE];

    while (bytes_done < to_read) {
        uint64_t current_pos = h->offset + bytes_done;
        uint64_t sector_idx = current_pos / SECTOR_SIZE;
        size_t sector_offset = static_cast<size_t>(current_pos % SECTOR_SIZE);
        size_t chunk = SECTOR_SIZE - sector_offset;
        if (chunk > to_read - bytes_done) {
            chunk = to_read - bytes_done;
        }

        // Read underlying encrypted sector (header is sector 0, payload starts at sector 1)
        uint64_t disk_sector_offset = sizeof(lixos_crypt_header) + (sector_idx * SECTOR_SIZE);
        vfs_lseek(h->underlying_fd, static_cast<int64_t>(disk_sector_offset), SEEK_SET);
        int64_t r = vfs_read(h->underlying_fd, cipher_sector, SECTOR_SIZE);
        if (r <= 0) {
            break;
        }

        // Decrypt sector with XTS-AES-256 using sector_idx + 1 as tweak
        xts_aes256_decrypt_sector(&h->ctx.xts, sector_idx + 1, cipher_sector, plain_sector, SECTOR_SIZE);

        memcpy(out_ptr + bytes_done, plain_sector + sector_offset, chunk);
        bytes_done += chunk;
    }

    h->offset += bytes_done;
    return static_cast<int64_t>(bytes_done);
}

int64_t cryptfs_write(int cfd, const void *buffer, size_t count) {
    if (cfd < 0 || cfd >= static_cast<int>(MAX_CRYPT_FDS) || !g_crypt_table[cfd].in_use) {
        return -1;
    }

    crypt_file_handle *h = &g_crypt_table[cfd];
    const uint8_t *in_ptr = reinterpret_cast<const uint8_t *>(buffer);
    size_t bytes_done = 0;

    uint8_t cipher_sector[SECTOR_SIZE];
    uint8_t plain_sector[SECTOR_SIZE];

    while (bytes_done < count) {
        uint64_t current_pos = h->offset + bytes_done;
        uint64_t sector_idx = current_pos / SECTOR_SIZE;
        size_t sector_offset = static_cast<size_t>(current_pos % SECTOR_SIZE);
        size_t chunk = SECTOR_SIZE - sector_offset;
        if (chunk > count - bytes_done) {
            chunk = count - bytes_done;
        }

        uint64_t disk_sector_offset = sizeof(lixos_crypt_header) + (sector_idx * SECTOR_SIZE);

        // If doing a partial sector write, read existing decrypted sector first
        memset(plain_sector, 0, SECTOR_SIZE);
        if (sector_offset > 0 || chunk < SECTOR_SIZE) {
            vfs_lseek(h->underlying_fd, static_cast<int64_t>(disk_sector_offset), SEEK_SET);
            if (vfs_read(h->underlying_fd, cipher_sector, SECTOR_SIZE) == static_cast<int64_t>(SECTOR_SIZE)) {
                xts_aes256_decrypt_sector(&h->ctx.xts, sector_idx + 1, cipher_sector, plain_sector, SECTOR_SIZE);
            }
        }

        // Copy new plaintext into sector
        memcpy(plain_sector + sector_offset, in_ptr + bytes_done, chunk);

        // Encrypt sector with XTS-AES-256
        xts_aes256_encrypt_sector(&h->ctx.xts, sector_idx + 1, plain_sector, cipher_sector, SECTOR_SIZE);

        // Write ciphertext to disk
        vfs_lseek(h->underlying_fd, static_cast<int64_t>(disk_sector_offset), SEEK_SET);
        vfs_write(h->underlying_fd, cipher_sector, SECTOR_SIZE);

        bytes_done += chunk;
    }

    h->offset += bytes_done;
    if (h->offset > h->header.data_payload_bytes) {
        h->header.data_payload_bytes = h->offset;
        // Update header on disk
        vfs_lseek(h->underlying_fd, 0, SEEK_SET);
        vfs_write(h->underlying_fd, &h->header, sizeof(lixos_crypt_header));
    }

    return static_cast<int64_t>(bytes_done);
}

int64_t cryptfs_lseek(int cfd, int64_t offset, int whence) {
    if (cfd < 0 || cfd >= static_cast<int>(MAX_CRYPT_FDS) || !g_crypt_table[cfd].in_use) {
        return -1;
    }

    crypt_file_handle *h = &g_crypt_table[cfd];
    int64_t new_offset = 0;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = static_cast<int64_t>(h->offset) + offset;
            break;
        case SEEK_END:
            new_offset = static_cast<int64_t>(h->header.data_payload_bytes) + offset;
            break;
        default:
            return -1;
    }

    if (new_offset < 0) {
        return -1;
    }

    h->offset = static_cast<uint64_t>(new_offset);
    return new_offset;
}

int cryptfs_close(int cfd) {
    if (cfd < 0 || cfd >= static_cast<int>(MAX_CRYPT_FDS) || !g_crypt_table[cfd].in_use) {
        return -1;
    }

    crypt_file_handle *h = &g_crypt_table[cfd];

    // Wipe cryptographic keys from memory
    memset(&h->ctx, 0, sizeof(lixos_crypt_ctx));
    memset(&h->header, 0, sizeof(lixos_crypt_header));

    int raw_fd = h->underlying_fd;
    h->underlying_fd = -1;
    h->in_use = false;
    h->offset = 0;

    if (raw_fd >= 0) {
        return vfs_close(raw_fd);
    }

    return 0;
}
