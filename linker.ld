/* LixOS v2 Linker Script (Higher-Half x86_64 ELF) */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);   /* Read + Execute */
    rodata  PT_LOAD FLAGS(4);   /* Read Only */
    data    PT_LOAD FLAGS(6);   /* Read + Write */
}

SECTIONS
{
    /* Higher-half base address for Limine kernel */
    . = 0xffffffff80000000;
    __kernel_start = .;

    __text_start = .;
    .text : {
        *(.text .text.*)
    } :text
    . = ALIGN(0x1000);
    __text_end = .;

    __rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata
    . = ALIGN(0x1000);
    __rodata_end = .;

    __data_start = .;
    .data : {
        *(.data .data.*)
        *(.requests)
    } :data
    . = ALIGN(0x1000);
    __data_end = .;

    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data
    . = ALIGN(0x1000);
    __bss_end = .;

    __kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
