#include "elf.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "kprint.h"
#include "process.h"

int elf_load(const char *path, Process *proc) {
    if (!path || !proc || proc->pml4_phys == 0) {
        kprintf("[ELF] Error: Invalid parameters for elf_load!\n");
        return -1;
    }

    kprintf("[ELF] Loading 64-bit ELF binary '%s' for Process PID %u ('%s')...\n",
            path, proc->pid, proc->name);

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kprintf("[ELF] Error: Failed to open '%s' via VFS (fd: %d)!\n", path, fd);
        return -1;
    }

    // 1. Read and validate ELF Header
    Elf64_Ehdr ehdr;
    int64_t read_bytes = vfs_read(fd, &ehdr, sizeof(Elf64_Ehdr));
    if (read_bytes != static_cast<int64_t>(sizeof(Elf64_Ehdr))) {
        kprintf("[ELF] Error: Failed to read ELF header (read %d of %u bytes)!\n",
                static_cast<int>(read_bytes), static_cast<unsigned int>(sizeof(Elf64_Ehdr)));
        vfs_close(fd);
        return -2;
    }

    // Verify magic bytes: 0x7F, 'E', 'L', 'F'
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        kprintf("[ELF] Error: Invalid ELF magic (0x%02x 0x%02x 0x%02x 0x%02x)!\n",
                ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
        vfs_close(fd);
        return -3;
    }

    // Verify 64-bit architecture
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        kprintf("[ELF] Error: Not a 64-bit ELF executable (class %u)!\n", ehdr.e_ident[EI_CLASS]);
        vfs_close(fd);
        return -3;
    }

    // Verify 2's complement little-endian
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        kprintf("[ELF] Error: Not little-endian ELF (encoding %u)!\n", ehdr.e_ident[EI_DATA]);
        vfs_close(fd);
        return -3;
    }

    // Verify x86_64 machine
    if (ehdr.e_machine != EM_X86_64) {
        kprintf("[ELF] Error: Target machine is not x86_64 (machine %u)!\n", ehdr.e_machine);
        vfs_close(fd);
        return -3;
    }

    // Verify executable type (ET_EXEC or ET_DYN/PIE)
    if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) {
        kprintf("[ELF] Error: Unsupported ELF type %u (expected ET_EXEC or ET_DYN)!\n", ehdr.e_type);
        vfs_close(fd);
        return -3;
    }

    kprintf("[ELF] ELF Header Verified: Entry = 0x%p, PhOff = 0x%x, PhNum = %u\n",
            reinterpret_cast<void*>(ehdr.e_entry),
            static_cast<uint32_t>(ehdr.e_phoff),
            static_cast<uint32_t>(ehdr.e_phnum));

    // 2. Iterate Program Headers and map PT_LOAD segments
    uint64_t hhdm = vmm_get_hhdm_offset();
    size_t loaded_segments = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        uint64_t phdr_offset = ehdr.e_phoff + (i * ehdr.e_phentsize);
        vfs_lseek(fd, phdr_offset, SEEK_SET);

        Elf64_Phdr phdr;
        if (vfs_read(fd, &phdr, sizeof(Elf64_Phdr)) != static_cast<int64_t>(sizeof(Elf64_Phdr))) {
            kprintf("[ELF] Error: Failed to read program header %u!\n", i);
            vfs_close(fd);
            return -4;
        }

        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0) {
            continue;
        }

        // Determine VMM permissions for this segment
        uint64_t vflags = vmm_flags::PRESENT | vmm_flags::USER;
        if (phdr.p_flags & PF_W) {
            vflags |= vmm_flags::WRITABLE;
        }
        if (!(phdr.p_flags & PF_X)) {
            vflags |= vmm_flags::NO_EXECUTE;
        }

        uint64_t seg_start = phdr.p_vaddr;
        uint64_t seg_end   = phdr.p_vaddr + phdr.p_memsz;
        uint64_t page_start = seg_start & ~0xFFFULL;
        uint64_t page_end   = (seg_end + 0xFFFULL) & ~0xFFFULL;

        kprintf("[ELF]  -> PT_LOAD Segment %u: VAddr 0x%p - 0x%p (FileSz: %u, MemSz: %u, Flags: %c%c%c)\n",
                i, reinterpret_cast<void*>(phdr.p_vaddr),
                reinterpret_cast<void*>(phdr.p_vaddr + phdr.p_memsz),
                static_cast<uint32_t>(phdr.p_filesz),
                static_cast<uint32_t>(phdr.p_memsz),
                (phdr.p_flags & PF_R) ? 'R' : '-',
                (phdr.p_flags & PF_W) ? 'W' : '-',
                (phdr.p_flags & PF_X) ? 'X' : '-');

        // Allocate and map physical pages for this segment
        for (uint64_t cur_va = page_start; cur_va < page_end; cur_va += 4096) {
            uint64_t phys_page = pmm_alloc_page();
            if (!phys_page) {
                kprintf("[ELF] Error: Out of physical memory loading segment %u!\n", i);
                vfs_close(fd);
                return -5;
            }

            // Zero out physical page via HHDM
            uint8_t *page_ptr = reinterpret_cast<uint8_t *>(phys_page + hhdm);
            memset(page_ptr, 0, 4096);

            // Map page into the process's address space
            vmm_map_page(proc->pml4_phys, cur_va, phys_page, vflags);

            // If this page overlaps with the file data [phdr.p_vaddr, phdr.p_vaddr + phdr.p_filesz)
            if (phdr.p_filesz > 0) {
                uint64_t file_data_start = phdr.p_vaddr;
                uint64_t file_data_end   = phdr.p_vaddr + phdr.p_filesz;

                if (cur_va + 4096 > file_data_start && cur_va < file_data_end) {
                    uint64_t copy_start = (cur_va > file_data_start) ? cur_va : file_data_start;
                    uint64_t copy_end   = (cur_va + 4096 < file_data_end) ? (cur_va + 4096) : file_data_end;
                    size_t copy_len     = copy_end - copy_start;
                    size_t page_offset  = copy_start - cur_va;
                    uint64_t file_offset = phdr.p_offset + (copy_start - file_data_start);

                    vfs_lseek(fd, file_offset, SEEK_SET);
                    int64_t r = vfs_read(fd, page_ptr + page_offset, copy_len);
                    if (r != static_cast<int64_t>(copy_len)) {
                        kprintf("[ELF] Warning: Short read on segment %u at offset 0x%x (expected %u, got %d)\n",
                                i, static_cast<uint32_t>(file_offset),
                                static_cast<uint32_t>(copy_len), static_cast<int>(r));
                    }
                }
            }
        }
        loaded_segments++;
    }

    vfs_close(fd);

    if (loaded_segments == 0) {
        kprintf("[ELF] Error: No PT_LOAD segments found in ELF!\n");
        return -6;
    }

    // 3. Allocate and map dedicated User Stack
    kprintf("[ELF] Allocating User Stack: 0x%p - 0x%p (%u KB)...\n",
            reinterpret_cast<void*>(USER_STACK_BOTTOM),
            reinterpret_cast<void*>(USER_STACK_TOP),
            static_cast<uint32_t>(USER_STACK_PAGES * 4));

    for (uint64_t s_va = USER_STACK_BOTTOM; s_va < USER_STACK_TOP; s_va += 4096) {
        uint64_t stack_phys = pmm_alloc_page();
        if (!stack_phys) {
            kprintf("[ELF] Error: Failed to allocate user stack page!\n");
            return -7;
        }

        uint8_t *s_ptr = reinterpret_cast<uint8_t *>(stack_phys + hhdm);
        memset(s_ptr, 0, 4096);

        vmm_map_page(proc->pml4_phys, s_va, stack_phys,
                     vmm_flags::PRESENT | vmm_flags::WRITABLE | vmm_flags::USER | vmm_flags::NO_EXECUTE);
    }

    // 4. Configure process context and initial stack frame
    proc->ustack_top = USER_STACK_TOP;
    proc->context.rip = ehdr.e_entry;

    process_setup_initial_stack(proc, reinterpret_cast<void (*)()>(ehdr.e_entry),
                                proc->ustack_top, false /* is_kernel_thread = false -> Ring 3 */);

    kprintf("[ OK ] ELF Binary '%s' Loaded Successfully: Entry = 0x%p, UStack = 0x%p\n",
            path, reinterpret_cast<void*>(ehdr.e_entry),
            reinterpret_cast<void*>(proc->ustack_top));

    return 0;
}
