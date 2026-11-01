#ifndef LIXOS_ELF_H
#define LIXOS_ELF_H

#include <stdint.h>
#include <stddef.h>
#include "process.h"

// ELF Identification indices
constexpr size_t EI_MAG0       = 0;
constexpr size_t EI_MAG1       = 1;
constexpr size_t EI_MAG2       = 2;
constexpr size_t EI_MAG3       = 3;
constexpr size_t EI_CLASS      = 4;
constexpr size_t EI_DATA       = 5;
constexpr size_t EI_VERSION    = 6;
constexpr size_t EI_OSABI      = 7;
constexpr size_t EI_ABIVERSION = 8;
constexpr size_t EI_NIDENT     = 16;

// ELF Magic Numbers
constexpr uint8_t ELFMAG0 = 0x7F;
constexpr uint8_t ELFMAG1 = 'E';
constexpr uint8_t ELFMAG2 = 'L';
constexpr uint8_t ELFMAG3 = 'F';

// ELF Architecture Classes
constexpr uint8_t ELFCLASSNONE = 0;
constexpr uint8_t ELFCLASS32   = 1;
constexpr uint8_t ELFCLASS64   = 2;

// ELF Data Encodings
constexpr uint8_t ELFDATANONE = 0;
constexpr uint8_t ELFDATA2LSB = 1; // Little-endian
constexpr uint8_t ELFDATA2MSB = 2; // Big-endian

// ELF File Types
constexpr uint16_t ET_NONE = 0;
constexpr uint16_t ET_REL  = 1;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN  = 3;
constexpr uint16_t ET_CORE = 4;

// Target Machine Architecture
constexpr uint16_t EM_X86_64 = 62; // 0x3E (Advanced Micro Devices X86-64)

// Segment Types (p_type)
constexpr uint32_t PT_NULL    = 0;
constexpr uint32_t PT_LOAD    = 1;
constexpr uint32_t PT_DYNAMIC = 2;
constexpr uint32_t PT_INTERP  = 3;
constexpr uint32_t PT_NOTE    = 4;
constexpr uint32_t PT_SHLIB   = 5;
constexpr uint32_t PT_PHDR    = 6;
constexpr uint32_t PT_TLS     = 7;
constexpr uint32_t PT_GNU_STACK = 0x6474E551;

// Segment Flags (p_flags)
constexpr uint32_t PF_X = 1; // Execute
constexpr uint32_t PF_W = 2; // Write
constexpr uint32_t PF_R = 4; // Read

// 64-bit ELF Header
struct __attribute__((packed)) Elf64_Ehdr {
    uint8_t  e_ident[EI_NIDENT]; // Magic number and other info
    uint16_t e_type;             // Object file type
    uint16_t e_machine;          // Architecture
    uint32_t e_version;          // Object file version
    uint64_t e_entry;            // Entry point virtual address
    uint64_t e_phoff;            // Program header table file offset
    uint64_t e_shoff;            // Section header table file offset
    uint32_t e_flags;            // Processor-specific flags
    uint16_t e_ehsize;           // ELF header size in bytes
    uint16_t e_phentsize;        // Program header table entry size
    uint16_t e_phnum;            // Program header table entry count
    uint16_t e_shentsize;        // Section header table entry size
    uint16_t e_shnum;            // Section header table entry count
    uint16_t e_shstrndx;         // Section header string table index
};

// 64-bit ELF Program Header
struct __attribute__((packed)) Elf64_Phdr {
    uint32_t p_type;   // Segment type
    uint32_t p_flags;  // Segment flags
    uint64_t p_offset; // Segment file offset
    uint64_t p_vaddr;  // Segment virtual address
    uint64_t p_paddr;  // Segment physical address
    uint64_t p_filesz; // Segment size in file
    uint64_t p_memsz;  // Segment size in memory
    uint64_t p_align;  // Segment alignment
};

// User space memory layout constants
constexpr uint64_t USER_STACK_TOP    = 0x00007FFFFFFFE000ULL;
constexpr uint64_t USER_STACK_PAGES  = 4; // 16 KB user stack
constexpr uint64_t USER_STACK_BOTTOM = USER_STACK_TOP - (USER_STACK_PAGES * 4096);

// Load an ELF64 executable from VFS path into the specified process
int elf_load(const char *path, Process *proc);

#endif // LIXOS_ELF_H
