#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "tss.h"
#include "kprint.h"
#include "string.h"
#include "irq_lock.h"

extern "C" uint64_t g_kernel_syscall_stack_top;

namespace {
    Process g_process_table[MAX_PROCESSES];
    Process *g_current_process = nullptr;
    uint32_t g_next_pid = 1;
}

void process_setup_initial_stack(Process *proc, void (*entry_point)(), uint64_t user_stack_top, bool is_kernel_thread) {
    if (!proc || proc->kstack_top == 0 || entry_point == nullptr) {
        return;
    }

    uint64_t ss_sel   = is_kernel_thread ? 0x10 : 0x1B;
    uint64_t cs_sel   = is_kernel_thread ? 0x08 : 0x23;
    uint64_t initial_rsp = is_kernel_thread ? (proc->kstack_top - 512) : user_stack_top;

    // Start building frame below initial_rsp, ensuring 16-byte alignment
    uint64_t *sp = reinterpret_cast<uint64_t *>((proc->kstack_top - 512) & ~0xFULL);

    // 1. Hardware iretq frame (pushed automatically on interrupt in reverse order):
    //    SS, RSP, RFLAGS, CS, RIP
    *(--sp) = ss_sel;
    *(--sp) = initial_rsp;
    *(--sp) = 0x202; // RFLAGS: Interrupt Flag (IF) enabled, bit 1 reserved
    *(--sp) = cs_sel;
    *(--sp) = reinterpret_cast<uint64_t>(entry_point);

    // 2. Interrupt Vector & Error Code (Simulating Timer Vector 32)
    *(--sp) = 0;   // err_code
    *(--sp) = 32;  // int_no (APIC_TIMER_VECTOR)

    // 3. General purpose registers pushed by isr_common:
    //    rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15
    for (int i = 0; i < 15; ++i) {
        *(--sp) = 0;
    }

    proc->saved_rsp = reinterpret_cast<uint64_t>(sp);
    proc->ustack_top = user_stack_top;
}

void process_init(void) {
    memset(g_process_table, 0, sizeof(g_process_table));

    // Register PID 0: The Kernel Process
    Process *kernel_proc = &g_process_table[0];
    kernel_proc->pid = 0;
    strncpy(kernel_proc->name, "kernel", sizeof(kernel_proc->name) - 1);
    kernel_proc->state = ProcessState::RUNNING;
    kernel_proc->pml4_phys = vmm_get_kernel_pml4();
    kernel_proc->kstack_phys = 0;

    tss_entry *tss = tss_get_current();
    kernel_proc->kstack_top = tss ? tss->rsp0 : 0;
    kernel_proc->ustack_top = 0;
    kernel_proc->exit_code = 0;

    for (size_t f = 0; f < MAX_PROCESS_FDS; ++f) {
        kernel_proc->fd_table[f] = (f < 3) ? static_cast<int>(f) : -1;
    }

    g_current_process = kernel_proc;

    kprintf("[ OK ] Process Subsystem Initialized: Kernel PID 0 Registered (PML4: 0x%p)\n",
            reinterpret_cast<void*>(kernel_proc->pml4_phys));
}

Process* process_create(const char *name, void (*entry_point)(), uint64_t flags) {
    InterruptLock lock;
    (void)flags;

    // Find free PCB entry (index 1 .. MAX_PROCESSES-1)
    Process *proc = nullptr;
    for (size_t i = 1; i < MAX_PROCESSES; ++i) {
        if (g_process_table[i].state == ProcessState::UNUSED) {
            proc = &g_process_table[i];
            break;
        }
    }

    if (!proc) {
        kprintf("[PROCESS] Error: Process table full!\n");
        return nullptr;
    }

    // 1. Create an isolated address space for this process
    uint64_t pml4_phys = vmm_create_address_space();
    if (!pml4_phys) {
        kprintf("[PROCESS] Error: Failed to allocate address space (PML4)!\n");
        return nullptr;
    }

    // 2. Allocate dedicated 16KB kernel stack (4 physical pages) for this process
    uint64_t kstack_phys = pmm_alloc_pages(4);
    if (!kstack_phys) {
        vmm_destroy_address_space(pml4_phys);
        kprintf("[PROCESS] Error: Failed to allocate kernel stack pages!\n");
        return nullptr;
    }

    uint64_t hhdm = vmm_get_hhdm_offset();
    uint64_t kstack_top = kstack_phys + hhdm + (4 * PAGE_SIZE);

    // 3. Initialize PCB fields
    proc->pid = g_next_pid++;
    const char *pname = (name != nullptr && name[0] != '\0') ? name : "proc";
    strncpy(proc->name, pname, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    proc->state = ProcessState::READY;
    proc->pml4_phys = pml4_phys;
    proc->kstack_phys = kstack_phys;
    proc->kstack_top = kstack_top;
    proc->ustack_top = 0;
    proc->saved_rsp = 0;
    proc->time_slice = 1;
    proc->total_ticks = 0;
    proc->exit_code = 0;

    // Initialize file descriptor table: inherit standard streams (0, 1, 2)
    for (size_t f = 0; f < MAX_PROCESS_FDS; ++f) {
        proc->fd_table[f] = (f < 3) ? static_cast<int>(f) : -1;
    }

    // 4. Initialize CPU context
    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.rip = reinterpret_cast<uint64_t>(entry_point);
    proc->context.cs = 0x23;       // Ring 3 User Code Segment
    proc->context.ss = 0x1B;       // Ring 3 User Data Segment
    proc->context.rflags = 0x202;  // Interrupt Flag enabled

    // If an entry point was given, setup initial stack frame for scheduler iretq
    if (entry_point != nullptr) {
        process_setup_initial_stack(proc, entry_point, 0, false);
    }

    kprintf("[ OK ] Process Created: PID %u ('%s'), PML4: 0x%p, KStack: 0x%p\n",
            proc->pid, proc->name,
            reinterpret_cast<void*>(proc->pml4_phys),
            reinterpret_cast<void*>(proc->kstack_top));

    return proc;
}

void process_destroy(Process *proc) {
    InterruptLock lock;
    if (!proc || proc->pid == 0) {
        return; // Never destroy kernel process
    }

    kprintf("[PROCESS] Destroying Process: PID %u ('%s')...\n", proc->pid, proc->name);
    proc->state = ProcessState::TERMINATED;

    // Reclaim process address space (user-mode page tables and mapped pages)
    if (proc->pml4_phys != 0) {
        vmm_destroy_address_space(proc->pml4_phys);
        proc->pml4_phys = 0;
    }

    // Reclaim dedicated kernel stack
    if (proc->kstack_phys != 0) {
        pmm_free_pages(proc->kstack_phys, 4);
        proc->kstack_phys = 0;
    }

    // Clear file descriptors
    for (size_t f = 0; f < MAX_PROCESS_FDS; ++f) {
        proc->fd_table[f] = -1;
    }

    // Reset PCB slot to UNUSED
    proc->pid = 0;
    proc->name[0] = '\0';
    proc->state = ProcessState::UNUSED;

    // If current process was destroyed, switch back to kernel
    if (g_current_process == proc) {
        g_current_process = &g_process_table[0];
        vmm_switch_pml4(g_process_table[0].pml4_phys);
        if (g_process_table[0].kstack_top != 0) {
            tss_set_rsp0(g_process_table[0].kstack_top);
            g_kernel_syscall_stack_top = g_process_table[0].kstack_top;
        }
    }
}

Process* process_get_by_pid(uint32_t pid) {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (g_process_table[i].state != ProcessState::UNUSED && g_process_table[i].pid == pid) {
            return &g_process_table[i];
        }
    }
    return nullptr;
}

Process* process_get_current(void) {
    return g_current_process;
}

void process_set_current(Process *proc) {
    if (proc != nullptr) {
        g_current_process = proc;
    }
}

void process_switch_address_space(Process *proc) {
    InterruptLock lock;
    if (!proc || proc->pml4_phys == 0) {
        return;
    }

    // Switch hardware CR3
    vmm_switch_pml4(proc->pml4_phys);

    // Update TSS Ring 0 stack pointer and syscall entry stack
    if (proc->kstack_top != 0) {
        tss_set_rsp0(proc->kstack_top);
        g_kernel_syscall_stack_top = proc->kstack_top;
    }

    g_current_process = proc;
}

Process* process_get_next_ready(Process *current) {
    InterruptLock lock;
    size_t start_idx = 0;
    if (current != nullptr) {
        for (size_t i = 0; i < MAX_PROCESSES; ++i) {
            if (&g_process_table[i] == current) {
                start_idx = (i + 1) % MAX_PROCESSES;
                break;
            }
        }
    }

    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        size_t idx = (start_idx + i) % MAX_PROCESSES;
        if (g_process_table[idx].state == ProcessState::READY ||
            (g_process_table[idx].state == ProcessState::RUNNING && &g_process_table[idx] == current)) {
            return &g_process_table[idx];
        }
    }

    return nullptr;
}

