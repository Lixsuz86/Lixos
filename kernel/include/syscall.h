#ifndef LIXOS_SYSCALL_H
#define LIXOS_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

// Standard Syscall Numbers
constexpr uint64_t SYS_EXIT  = 0;
constexpr uint64_t SYS_READ  = 1;
constexpr uint64_t SYS_WRITE = 2;
constexpr uint64_t SYS_OPEN  = 3;
constexpr uint64_t SYS_CLOSE = 4;
constexpr uint64_t SYS_LSEEK  = 5;
constexpr uint64_t SYS_GETPID  = 6;
constexpr uint64_t SYS_YIELD   = 7;
constexpr uint64_t SYS_READDIR = 8;
constexpr uint64_t SYS_SPAWN   = 9;
constexpr uint64_t SYS_WAITPID = 10;
constexpr uint64_t SYS_SOCKET  = 11;
constexpr uint64_t SYS_BIND    = 12;
constexpr uint64_t SYS_CONNECT = 13;
constexpr uint64_t SYS_SEND    = 14;
constexpr uint64_t SYS_RECV    = 15;

// POSIX-compatible Error Numbers (returned as negative values)
constexpr int64_t ENOSYS       = 38;  // Function not implemented
constexpr int64_t EBADF        = 9;   // Bad file descriptor
constexpr int64_t EINVAL       = 22;  // Invalid argument
constexpr int64_t EACCES       = 13;  // Permission denied
constexpr int64_t ENOENT       = 2;   // No such file or directory
constexpr int64_t ESRCH        = 3;   // No such process
constexpr int64_t ENOMEM       = 12;  // Out of memory
constexpr int64_t ECONNREFUSED = 111; // Connection refused
constexpr int64_t ENOTCONN     = 107; // Transport endpoint is not connected
constexpr int64_t ETIMEDOUT    = 110; // Connection timed out

struct vfs_dirent;
struct sockaddr_in;

// Initialize MSRs (STAR, LSTAR, SFMASK, EFER.SCE) for fast syscall/sysret
void syscall_init(void);

// Central C++ Dispatcher called from assembly entry stub
extern "C" int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

// Individual Syscall Handlers
int64_t sys_exit(int status);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *path, uint32_t flags, uint16_t mode);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, int64_t offset, int whence);
int64_t sys_getpid(void);
int64_t sys_yield(void);
int64_t sys_readdir(int fd, uint32_t index, vfs_dirent *out);
int64_t sys_spawn(const char *path);
int64_t sys_waitpid(uint32_t pid, int *status);
int64_t sys_socket(int domain, int type, int protocol);
int64_t sys_bind(int fd, const sockaddr_in *addr, uint32_t addrlen);
int64_t sys_connect(int fd, const sockaddr_in *addr, uint32_t addrlen);
int64_t sys_send(int fd, const void *buf, size_t len, int flags);
int64_t sys_recv(int fd, void *buf, size_t len, int flags);

// Userspace library wrapper macro for invoking syscall from user mode
inline int64_t lixos_syscall(uint64_t num, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0, uint64_t a5 = 0) {
    int64_t ret;
    register uint64_t r10 asm("r10") = a4;
    register uint64_t r8  asm("r8")  = a5;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#endif // LIXOS_SYSCALL_H
