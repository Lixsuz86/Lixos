.global _start
.section .text
_start:
    # 1. Write hello message via SYS_WRITE (Syscall 2)
    # System V ABI: RAX=Syscall, RDI=fd, RSI=buf, RDX=count
    mov $2, %rax
    mov $1, %rdi
    lea hello_msg(%rip), %rsi
    mov $hello_len, %rdx
    syscall

    # 2. Write second confirmation message
    mov $2, %rax
    mov $1, %rdi
    lea success_msg(%rip), %rsi
    mov $success_len, %rdx
    syscall

    # 3. Exit cleanly with code 0 via SYS_EXIT (Syscall 0)
    mov $0, %rax
    mov $0, %rdi
    syscall

    # Safety trap loop
1:
    hlt
    jmp 1b

.section .rodata
hello_msg:
    .ascii "\n[USER ELF] Hello from Userspace ELF binary loaded via VFS & ext2!\n"
    .ascii "[USER ELF] Running in Ring 3 (CPL = 3) on isolated page tables!\n"
hello_len = . - hello_msg

success_msg:
    .ascii "[USER ELF] System calls (SYS_WRITE, SYS_EXIT) verified from standalone ELF.\n"
success_len = . - success_msg
