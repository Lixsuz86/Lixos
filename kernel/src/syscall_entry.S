/* LixOS v2 Fast Syscall Entry Stub (x86_64 Long Mode) */

.extern syscall_dispatch

.global g_user_rsp_scratch
.global g_kernel_syscall_stack_top
.global syscall_entry

.data
.align 16
g_user_rsp_scratch:
    .quad 0
g_kernel_syscall_stack_top:
    .quad 0

.text
.align 16
.type syscall_entry, @function
syscall_entry:
    /* 1. Switch to Kernel Stack safely */
    movq %rsp, g_user_rsp_scratch(%rip)
    movq g_kernel_syscall_stack_top(%rip), %rsp

    /* 2. Save User Context (User RSP, User RFLAGS from R11, User RIP from RCX) */
    pushq g_user_rsp_scratch(%rip)
    pushq %r11
    pushq %rcx

    /* 3. Save Callee-Saved Registers */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 4. Prepare Arguments for C++ syscall_dispatch(num, a1, a2, a3, a4, a5)
          User input registers:
            RAX = Syscall Number
            RDI = a1
            RSI = a2
            RDX = a3
            R10 = a4 (ABI uses R10 because RCX is clobbered by syscall hardware)
            R8  = a5
          Target System V calling convention:
            RDI = num (from RAX)
            RSI = a1  (from RDI)
            RDX = a2  (from RSI)
            RCX = a3  (from RDX)
            R8  = a4  (from R10)
            R9  = a5  (from R8)
    */
    movq %r8,  %r9   /* Arg 6: a5 */
    movq %r10, %r8   /* Arg 5: a4 */
    movq %rdx, %rcx  /* Arg 4: a3 */
    movq %rsi, %rdx  /* Arg 3: a2 */
    movq %rdi, %rsi  /* Arg 2: a1 */
    movq %rax, %rdi  /* Arg 1: Syscall Number */

    /* 5. Dispatch Syscall in C++ */
    call syscall_dispatch
    /* Return value is in %rax */

    /* 6. Restore Callee-Saved Registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    /* 7. Restore User RIP into RCX and User RFLAGS into R11 for sysretq */
    popq %rcx
    popq %r11
    popq %rsp

    /* 8. Return to User Mode (Ring 3) */
    sysretq
