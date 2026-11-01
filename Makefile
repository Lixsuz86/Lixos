CXX := clang++
LD := ld.lld
XORRISO := C:/msys64/usr/bin/xorriso.exe
QEMU := C:/msys64/mingw64/bin/qemu-system-x86_64.exe
LIMINE_BIN := boot/limine.exe

CXXFLAGS := --target=x86_64-elf \
            -std=c++20 \
            -ffreestanding \
            -fno-rtti \
            -fno-exceptions \
            -fno-stack-protector \
            -fno-pic \
            -fpie \
            -mcmodel=kernel \
            -mno-red-zone \
            -mno-mmx \
            -mno-sse \
            -mno-sse2 \
            -Wall \
            -Wextra \
            -O2 \
            -Ikernel/include

LDFLAGS := -T linker.ld \
           -nostdlib \
           -static \
           -z max-page-size=0x1000

BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso_root
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/lixos.iso

SRCS_CPP := $(wildcard kernel/src/*.cpp)
SRCS_ASM := $(wildcard kernel/src/*.S)

OBJS := $(patsubst kernel/src/%.cpp, $(BUILD_DIR)/%.o, $(SRCS_CPP)) \
        $(patsubst kernel/src/%.S, $(BUILD_DIR)/%_asm.o, $(SRCS_ASM))

# OS-specific commands
ifeq ($(OS),Windows_NT)
    SHELL := cmd.exe
    MKDIR = if not exist $(subst /,\,$1) mkdir $(subst /,\,$1)
    RM = if exist $(subst /,\,$1) rmdir /s /q $(subst /,\,$1)
    CP = copy /Y $(subst /,\,$1) $(subst /,\,$2)
else
    MKDIR = mkdir -p $1
    RM = rm -rf $1
    CP = cp $1 $2
    XORRISO := xorriso
    QEMU := qemu-system-x86_64
    LIMINE_BIN := ./boot/limine
endif

.PHONY: all clean iso run

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS) linker.ld
	@$(call MKDIR,$(BUILD_DIR))
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL_ELF)

$(BUILD_DIR)/%.o: kernel/src/%.cpp
	@$(call MKDIR,$(BUILD_DIR))
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%_asm.o: kernel/src/%.S
	@$(call MKDIR,$(BUILD_DIR))
	$(CXX) $(CXXFLAGS) -c $< -o $@

iso: $(ISO)

$(ISO): $(KERNEL_ELF) limine.conf
	@$(call RM,$(ISO_DIR))
	@$(call MKDIR,$(ISO_DIR)/boot/limine)
	@$(call MKDIR,$(ISO_DIR)/EFI/BOOT)
	@$(call CP,$(KERNEL_ELF),$(ISO_DIR)/boot/kernel.elf)
	@$(call CP,limine.conf,$(ISO_DIR)/boot/limine/limine.conf)
	@$(call CP,limine.conf,$(ISO_DIR)/boot/limine.conf)
	@$(call CP,limine.conf,$(ISO_DIR)/limine.conf)
	@$(call CP,boot/limine-bios.sys,$(ISO_DIR)/boot/limine/limine-bios.sys)
	@$(call CP,boot/limine-bios.sys,$(ISO_DIR)/boot/limine-bios.sys)
	@$(call CP,boot/limine-bios-cd.bin,$(ISO_DIR)/boot/limine/limine-bios-cd.bin)
	@$(call CP,boot/limine-uefi-cd.bin,$(ISO_DIR)/boot/limine/limine-uefi-cd.bin)
	@$(call CP,boot/BOOTX64.EFI,$(ISO_DIR)/EFI/BOOT/BOOTX64.EFI)
	$(XORRISO) -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO)
	$(LIMINE_BIN) bios-install $(ISO)

run: $(ISO)
	$(QEMU) -cpu max -cdrom $(ISO) -serial stdio -m 256M

clean:
	@$(call RM,$(BUILD_DIR))
