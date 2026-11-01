#ifndef LIXOS_PROCESS_H
#define LIXOS_PROCESS_H

#include <stdint.h>
#include <stddef.h>

constexpr size_t MAX_PROCESSES = 64;
constexpr size_t MAX_PROCESS_FDS = 32;

enum class ProcessState {
    UNUSED = 0,
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
};

struct ProcessContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct Process {
    uint32_t pid;
    char name[32];
    ProcessState state;
    uint64_t pml4_phys;             // Physical address of PML4 (CR3)
    uint64_t kstack_phys;           // Physical base of allocated kernel stack
    uint64_t kstack_top;            // Virtual top of kernel stack (HHDM, for TSS RSP0)
    uint64_t ustack_top;            // Virtual top of user stack
    uint64_t saved_rsp;             // Saved stack pointer during interrupt / context switch
    uint32_t time_slice;            // Remaining time quantum in ticks
    uint32_t total_ticks;           // Lifetime CPU tick count
    int fd_table[MAX_PROCESS_FDS];  // Mapping from process fd -> global VFS fd
    int exit_code;                  // Exit status code
    ProcessContext context;         // CPU register state
};

// Initialize process management subsystem and register Kernel PID 0
void process_init(void);

// Set up forged interrupt frame on kernel stack for first iretq entry
void process_setup_initial_stack(Process *proc, void (*entry_point)(), uint64_t user_stack_top, bool is_kernel_thread = false);

// Create a new process with isolated address space and stack
Process* process_create(const char *name, void (*entry_point)(), uint64_t flags = 0);

// Terminate and reclaim process resources (free address space, stack, PCB)
void process_destroy(Process *proc);

// Retrieve process pointer by PID
Process* process_get_by_pid(uint32_t pid);

// Get currently running process
Process* process_get_current(void);

// Set currently running process
void process_set_current(Process *proc);

// Find the next READY process in round-robin order
Process* process_get_next_ready(Process *current);

// Switch hardware CR3 and TSS RSP0 to the given process
void process_switch_address_space(Process *proc);

#endif // LIXOS_PROCESS_H
