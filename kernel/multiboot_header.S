/* Multiboot 1 Header */
.section .multiboot
.align 4
.long 0x1BADB002        /* Magic number */
.long 0x00000003        /* Flags: 1=Hizala, 2=Bellek Bilgisi */
.long -(0x1BADB002 + 0x00000003) /* Checksum */

/* Stack Alanı (16 KB) */
.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global start
start:
    /* Stack Pointer ayarla */
    mov $stack_top, %esp
    
    /* C++ Kernel'a giriş yap */
    call kernel_main

    /* Kernel dönerse işlemciyi durdur */
    cli
1:  hlt
    jmp 1b