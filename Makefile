AS = nasm
CROSS_PREFIX := i686-elf-

# Cross-compiler kontrolü
ifeq ($(shell which $(CROSS_PREFIX)g++ >/dev/null 2>&1 && echo yes),yes)
    CC = $(CROSS_PREFIX)g++
    LD = $(CROSS_PREFIX)ld
else
    CC = g++
    LD = ld
endif

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
LDFLAGS = -m elf_i386 -nostdlib

all: lixos.iso

kernel.elf: kernel/multiboot_header.o kernel/kernel.o
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o kernel.elf kernel/multiboot_header.o kernel/kernel.o

kernel/multiboot_header.o: kernel/multiboot_header.S
	$(CC) $(CFLAGS) -x assembler-with-cpp -c kernel/multiboot_header.S -o kernel/multiboot_header.o

kernel/kernel.o: kernel/kernel.cpp
	$(CC) $(CFLAGS) -c kernel/kernel.cpp -o kernel/kernel.o

lixos.iso: kernel.elf
	@# Eski kalıntıları temizle
	rm -rf iso
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	@# grub.cfg dosyasını oluştur
	@echo 'set timeout=0' > iso/boot/grub/grub.cfg
	@echo 'set default=0' >> iso/boot/grub/grub.cfg
	@echo 'menuentry "LIXOS" { multiboot /boot/kernel.elf; boot }' >> iso/boot/grub/grub.cfg
	@# ISO dosyasını oluştur
	grub-mkrescue -o lixos.iso iso

clean:
	rm -rf kernel/*.o *.elf iso lixos.iso "" "2]"

.PHONY: all clean