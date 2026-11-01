#include "syscall.h"
#include "msr.h"
#include "tss.h"
#include "vfs.h"
#include "kprint.h"
#include "keyboard.h"
#include "string.h"
#include "process.h"
#include "scheduler.h"
#include "irq_lock.h"
#include "elf.h"
#include "socket.h"

extern "C" void syscall_entry(void);
extern "C" uint64_t g_kernel_syscall_stack_top;
extern "C" void kernel_resume_after_syscall_test(int exit_code);

// Syscall statistics / state tracking
volatile uint64_t g_syscall_count = 0;
volatile int      g_last_exit_code = 0;
volatile bool     g_user_program_exited = false;

void syscall_init(void) {
    // 1. Set Kernel Stack pointer for syscall entry to current TSS RSP0
    tss_entry *current_tss = tss_get_current();
    if (current_tss != nullptr && current_tss->rsp0 != 0) {
        g_kernel_syscall_stack_top = current_tss->rsp0;
    }

    // 2. Enable Syscall Extension (SCE) in EFER MSR
    uint64_t efer = rdmsr(IA32_EFER_MSR);
    wrmsr(IA32_EFER_MSR, efer | EFER_SCE);

    // 3. Configure STAR MSR:
    //    Bits 47:32 -> Kernel CS (0x08). Kernel SS will automatically be CS + 8 = 0x10.
    //    Bits 63:48 -> User Base Selector (0x10).
    //                  On 64-bit sysretq:
    //                  Target User SS = Base + 8  | 3 = 0x18 | 3 = 0x1B (User Data)
    //                  Target User CS = Base + 16 | 3 = 0x20 | 3 = 0x23 (User Code)
    uint64_t star = (0x10ULL << 48) | (0x08ULL << 32);
    wrmsr(IA32_STAR_MSR, star);

    // 4. Configure LSTAR MSR: Target 64-bit Syscall Entry point
    wrmsr(IA32_LSTAR_MSR, reinterpret_cast<uint64_t>(syscall_entry));

    // 5. Configure SFMASK MSR: Mask IF (Interrupts disabled on entry) and other sensitive flags
    //    Bit 9 = IF (0x200), Bit 8 = TF (0x100), Bit 10 = DF (0x400), Bit 14 = NT (0x4000)
    uint64_t sfmask = 0x4700; // Mask IF, TF, DF, NT, IOPL
    wrmsr(IA32_SFMASK_MSR, sfmask);

    kprintf("[ OK ] Fast Syscall MSRs Configured: STAR (0x%p), LSTAR (0x%p), SFMASK (0x%x), EFER.SCE=1\n",
            reinterpret_cast<void*>(star),
            reinterpret_cast<void*>(syscall_entry),
            static_cast<unsigned int>(sfmask));
}

// -----------------------------------------------------------------------------
// Syscall Handlers
// -----------------------------------------------------------------------------

int64_t sys_exit(int status) {
    g_syscall_count = g_syscall_count + 1;
    g_last_exit_code = status;
    g_user_program_exited = true;

    Process *cur = process_get_current();
    if (cur != nullptr && cur->pid != 0) {
        cur->state = ProcessState::TERMINATED;
        cur->exit_code = status;
        kprintf("[SYSCALL sys_exit] Process %u ('%s') terminated cleanly with exit status: %d\n",
                cur->pid, cur->name, status);

        // Re-enable interrupts so timer interrupt / scheduler can switch to next ready process
        asm volatile("sti");
        for (;;) { asm volatile("hlt"); }
    }

    // Direct Milestone 9.2 test runner (runs in PID 0 without scheduler active)
    kprintf("[SYSCALL sys_exit] Process 0 terminated cleanly with exit status: %d\n", status);
    kernel_resume_after_syscall_test(status);

    // Never reached
    for (;;) { asm volatile("hlt"); }
    return 0;
}

int64_t sys_read(int fd, void *buf, size_t count) {
    g_syscall_count = g_syscall_count + 1;
    if (buf == nullptr && count > 0) {
        return -EINVAL;
    }
    if (count == 0) {
        return 0;
    }

    if (fd == 0) {
        // Standard Input (stdin) -> Read from Keyboard Driver
        size_t read_bytes = 0;
        char *cbuf = static_cast<char *>(buf);

        // If at least 1 character is requested, wait until key arrives (if scheduler running)
        while (count > 0 && !keyboard_has_key()) {
            if (scheduler_is_enabled()) {
                asm volatile("sti");
                scheduler_yield();
            } else {
                break;
            }
        }

        while (read_bytes < count && keyboard_has_key()) {
            if (keyboard_read(&cbuf[read_bytes], 1)) {
                read_bytes++;
            }
        }
        return static_cast<int64_t>(read_bytes);
    }

    // Regular file descriptor via VFS
    return vfs_read(fd, buf, count);
}

int64_t sys_write(int fd, const void *buf, size_t count) {
    g_syscall_count = g_syscall_count + 1;
    if (buf == nullptr && count > 0) {
        return -EINVAL;
    }
    if (count == 0) {
        return 0;
    }

    if (fd == 1 || fd == 2) {
        // Standard Output / Standard Error -> Terminal & Serial Output
        const char *s = static_cast<const char *>(buf);
        for (size_t i = 0; i < count; ++i) {
            kprintf("%c", s[i]);
        }
        return static_cast<int64_t>(count);
    }

    // Regular file descriptor via VFS
    return vfs_write(fd, buf, count);
}

int64_t sys_open(const char *path, uint32_t flags, uint16_t mode) {
    g_syscall_count = g_syscall_count + 1;
    if (path == nullptr) {
        return -EINVAL;
    }
    int fd = vfs_open(path, flags, mode);
    if (fd < 0) {
        return -EBADF;
    }
    return static_cast<int64_t>(fd);
}

int64_t sys_close(int fd) {
    g_syscall_count = g_syscall_count + 1;
    if (fd >= 0 && fd <= 2) {
        // Stdio streams close succeeds
        return 0;
    }
    int res = vfs_close(fd);
    if (res < 0) {
        return -EBADF;
    }
    return 0;
}

int64_t sys_lseek(int fd, int64_t offset, int whence) {
    g_syscall_count = g_syscall_count + 1;
    return vfs_lseek(fd, offset, whence);
}

int64_t sys_getpid(void) {
    g_syscall_count = g_syscall_count + 1;
    Process *cur = process_get_current();
    if (cur != nullptr) {
        return static_cast<int64_t>(cur->pid);
    }
    return 0;
}

int64_t sys_yield(void) {
    g_syscall_count = g_syscall_count + 1;
    scheduler_yield();
    return 0;
}

int64_t sys_readdir(int fd, uint32_t index, vfs_dirent *out) {
    g_syscall_count = g_syscall_count + 1;
    if (out == nullptr) {
        return -EINVAL;
    }
    int res = vfs_readdir(fd, index, out);
    if (res < 0) {
        return -EBADF;
    }
    return res;
}

int64_t sys_spawn(const char *path) {
    g_syscall_count = g_syscall_count + 1;
    if (path == nullptr || path[0] == '\0') {
        return -EINVAL;
    }

    // Extract process name from path
    const char *pname = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' && *(p + 1) != '\0') {
            pname = p + 1;
        }
    }

    Process *proc = process_create(pname, nullptr);
    if (!proc) {
        return -ENOMEM;
    }

    int elf_res = elf_load(path, proc);
    if (elf_res != 0) {
        process_destroy(proc);
        return -ENOENT;
    }

    proc->state = ProcessState::READY;
    return static_cast<int64_t>(proc->pid);
}

int64_t sys_waitpid(uint32_t pid, int *status) {
    g_syscall_count = g_syscall_count + 1;
    Process *child = process_get_by_pid(pid);
    if (!child) {
        return -ESRCH;
    }

    // Wait until child terminates
    while (child->state != ProcessState::TERMINATED) {
        if (scheduler_is_enabled()) {
            asm volatile("sti");
            scheduler_yield();
        } else {
            asm volatile("pause");
        }
    }

    if (status != nullptr) {
        *status = child->exit_code;
    }

    // Clean up child process resources
    process_destroy(child);
    return 0;
}

int64_t sys_socket(int domain, int type, int protocol) {
    g_syscall_count = g_syscall_count + 1;
    int sock_id = socket_create(domain, type, protocol);
    if (sock_id < 0) {
        return sock_id;
    }
    kernel_socket *sock = socket_get(sock_id);
    int fd = vfs_allocate_fd(&sock->vnode, O_RDWR);
    if (fd < 0) {
        socket_close(sock_id);
        return -ENOMEM;
    }
    return static_cast<int64_t>(fd);
}

int64_t sys_bind(int fd, const sockaddr_in *addr, uint32_t addrlen) {
    g_syscall_count = g_syscall_count + 1;
    if (!addr || addrlen < sizeof(sockaddr_in)) {
        return -EINVAL;
    }
    vfs_node *node = vfs_get_node(fd);
    if (!node || node->type != vfs_node_type::SOCKET) {
        return -EBADF;
    }
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    return socket_bind(sock->id, addr);
}

int64_t sys_connect(int fd, const sockaddr_in *addr, uint32_t addrlen) {
    g_syscall_count = g_syscall_count + 1;
    if (!addr || addrlen < sizeof(sockaddr_in)) {
        return -EINVAL;
    }
    vfs_node *node = vfs_get_node(fd);
    if (!node || node->type != vfs_node_type::SOCKET) {
        return -EBADF;
    }
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    return socket_connect(sock->id, addr);
}

int64_t sys_send(int fd, const void *buf, size_t len, int flags) {
    g_syscall_count = g_syscall_count + 1;
    vfs_node *node = vfs_get_node(fd);
    if (!node || node->type != vfs_node_type::SOCKET) {
        return -EBADF;
    }
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    return socket_send(sock->id, buf, len, flags);
}

int64_t sys_recv(int fd, void *buf, size_t len, int flags) {
    g_syscall_count = g_syscall_count + 1;
    vfs_node *node = vfs_get_node(fd);
    if (!node || node->type != vfs_node_type::SOCKET) {
        return -EBADF;
    }
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    return socket_recv(sock->id, buf, len, flags);
}

// -----------------------------------------------------------------------------
// Central Syscall Dispatcher (Called from syscall_entry.S)
// -----------------------------------------------------------------------------
extern "C" int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a5;

    switch (num) {
        case SYS_EXIT:
            return sys_exit(static_cast<int>(a1));

        case SYS_READ:
            return sys_read(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3));

        case SYS_WRITE:
            return sys_write(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3));

        case SYS_OPEN:
            return sys_open(reinterpret_cast<const char*>(a1), static_cast<uint32_t>(a2), static_cast<uint16_t>(a3));

        case SYS_CLOSE:
            return sys_close(static_cast<int>(a1));

        case SYS_LSEEK:
            return sys_lseek(static_cast<int>(a1), static_cast<int64_t>(a2), static_cast<int>(a3));

        case SYS_GETPID:
            return sys_getpid();

        case SYS_YIELD:
            return sys_yield();

        case SYS_READDIR:
            return sys_readdir(static_cast<int>(a1), static_cast<uint32_t>(a2), reinterpret_cast<vfs_dirent*>(a3));

        case SYS_SPAWN:
            return sys_spawn(reinterpret_cast<const char*>(a1));

        case SYS_WAITPID:
            return sys_waitpid(static_cast<uint32_t>(a1), reinterpret_cast<int*>(a2));

        case SYS_SOCKET:
            return sys_socket(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3));

        case SYS_BIND:
            return sys_bind(static_cast<int>(a1), reinterpret_cast<const sockaddr_in*>(a2), static_cast<uint32_t>(a3));

        case SYS_CONNECT:
            return sys_connect(static_cast<int>(a1), reinterpret_cast<const sockaddr_in*>(a2), static_cast<uint32_t>(a3));

        case SYS_SEND:
            return sys_send(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4));

        case SYS_RECV:
            return sys_recv(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4));

        default:
            // Unregistered / invalid syscall number: return -ENOSYS cleanly without crash
            return -ENOSYS;
    }
}
