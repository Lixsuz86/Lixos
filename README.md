# LIXOS — minimal example bootloader + C++ kernel

This repository contains a tiny BIOS bootloader and a multiboot-compatible C++ kernel suitable for testing under QEMU/GRUB.

Files added:

- /lixos/boot/boot.asm — 512-byte BIOS boot sector (prints message and halts)
- /lixos/kernel/multiboot_header.S — multiboot header + start label
- /lixos/kernel/kernel.cpp — minimal C++ kernel writing to VGA text buffer
- /lixos/kernel/linker.ld — linker script (loads at 1MiB)
- /lixos/iso/boot/grub/grub.cfg — GRUB menu to boot the kernel
- /lixos/Makefile — build targets for kernel, bootloader, and iso

Build & run (recommended on Linux/macOS with required tools installed):

1. Install toolchain: `i686-elf-gcc/g++`, `nasm`, `grub-mkrescue`, `qemu` (or use your distro's cross-toolchain)
2. From repository root run:

```
cd lixos
make all
```

3. If `grub-mkrescue` succeeded you'll have `lixos.iso` — run in QEMU:

```
qemu-system-i386 -cdrom lixos.iso
```

Bootloader only test (BIOS floppy image):

```
cd lixos
````markdown
# LIXOS — minimal örnek bootloader + C++ çekirdek (kernel)

Bu depo, QEMU/GRUB altında test etmek için uygun, küçük bir BIOS bootloader ve multiboot-uyumlu bir C++ çekirdek içerir.

Dosyalar:

- `/lixos/boot/boot.asm` — 512 baytlık BIOS boot sektörü (bir mesaj yazdırır ve durur)
- `/lixos/kernel/multiboot_header.S` — multiboot başlığı ve başlangıç etiketi
- `/lixos/kernel/kernel.cpp` — VGA metin ekranına yazan minimal C++ çekirdek
- `/lixos/kernel/linker.ld` — linker betiği (1MiB'e yüklenir)
- `/lixos/iso/boot/grub/grub.cfg` — çekirdeği önyüklemek için GRUB menüsü
- `/lixos/Makefile` — kernel, bootloader ve iso için derleme hedefleri

Derleme ve çalıştırma (önerilen: Linux/macOS ve gerekli araçlar yüklü):

1. Gerekli araçları yükleyin: `i686-elf-gcc/g++`, `nasm`, `grub-mkrescue`, `qemu` (ya da dağıtımınızın sağladığı çapraz araç zincirini kullanın)
2. Depo kökünden çalıştırın:

```
cd lixos
make all
```

3. Eğer `grub-mkrescue` başarılı olduysa `lixos.iso` oluşacaktır — QEMU ile çalıştırmak için:

```
qemu-system-i386 -cdrom lixos.iso
```

Sadece bootloader testi (BIOS floppy imajı):

```
cd lixos
make bootloader
qemu-system-i386 -fda boot/boot.img
```

Notlar:
- Çekirdek, VGA metin ekranına yazarak çalışan ve ardından duran basit bir gösterimdir.
- Çapraz araç zincirine (`i686-elf-*`) sahip değilseniz, Makefile'ı yerel 32-bit araç zinciri (örn. `-m32`) kullanacak şekilde uyarlayabilirsiniz; fakat hobi işletim sistemi geliştirme için `i686-elf-*` kurmanız önerilir.

````
