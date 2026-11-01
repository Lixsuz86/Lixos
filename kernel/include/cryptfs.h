#ifndef LIXOS_CRYPTFS_H
#define LIXOS_CRYPTFS_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "crypt.h"

constexpr int ERR_AUTH_FAILED = -13; // EACCES

// CryptFS File Handle structure
struct crypt_file_handle {
    int                 underlying_fd;
    lixos_crypt_header  header;
    lixos_crypt_ctx     ctx;
    uint64_t            offset;
    bool                in_use;
};

// Initialize CryptFS subsystem
void cryptfs_init(void);

// Create a new encrypted file with a given password on the filesystem
int cryptfs_create(const char *path, const char *password, uint16_t mode = 0644);

// Open an encrypted file with a password. Returns a crypt descriptor >= 0, or ERR_AUTH_FAILED (-13) if wrong password
int cryptfs_open(const char *path, const char *password, uint32_t flags = O_RDWR);

// Read plaintext bytes from an opened encrypted file
int64_t cryptfs_read(int cfd, void *buffer, size_t count);

// Write plaintext bytes into an opened encrypted file (automatically encrypted with XTS-AES-256 on disk)
int64_t cryptfs_write(int cfd, const void *buffer, size_t count);

// Seek within an encrypted file
int64_t cryptfs_lseek(int cfd, int64_t offset, int whence);

// Close an encrypted file handle (wipes active key from memory)
int cryptfs_close(int cfd);

#endif // LIXOS_CRYPTFS_H
