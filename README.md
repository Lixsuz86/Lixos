# LixOS v2 (Beta Release)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE-MIT)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE-APACHE)
[![Platform: x86_64](https://img.shields.io/badge/Platform-x86__64%20Freestanding-blueviolet.svg)](#)
[![Status: Beta](https://img.shields.io/badge/Status-v2.0.0--beta-success.svg)](#)

**LixOS v2** is a modern, 64-bit freestanding operating system designed and implemented from scratch for the `x86_64` architecture, booted via the modern [Limine](https://limine-bootloader.org/) boot protocol.

---

## 🌟 Key Features

### 🖥️ 1. Modern GUI Desktop & Window Manager (1920x1080 Full HD)
- **Double-Buffered Compositor**: Full HD 1920x1080 32-bit ARGB software rendering with dirty-rect damage tracking for stutter-free display updates.
- **Dual Desktop Layouts**:
  - **Windows 10 Classic Style**: Taskbar with Start menu button, active running window tasklist, and system clock tray.
  - **macOS / GNOME Dock Style**: Top system menu bar with a centered floating dock.
- **Theme Package Engine (`.lixtheme`)**: Self-contained shareable themes (with bundled bitmap fonts) and strict security guardrails blocking executable payload injections.

### 💾 2. Storage & Cryptography Subsystem
- **AHCI SATA Driver**: Native PCI AHCI HBA controller discovery and 32-slot DMA command table execution.
- **VFS (Virtual File System)**: Unified file system abstraction supporting file descriptors, nodes, directories, and sockets.
- **ext2 Filesystem**: Full read/write ext2 filesystem driver supporting directory trees, inodes, and file operations.
- **CryptFS (Transparent Disk Encryption)**: High-performance AES-256 in XTS mode with PBKDF2-HMAC-SHA256 key derivation.

### 🛡️ 3. Process Model, Syscalls & Userspace Shell
- **Hardware Isolation**: Ring 0 kernel / Ring 3 userspace separation via TSS RSP0 and MSRs (`STAR`, `LSTAR`, `SFMASK`, `EFER.SCE`).
- **Preemptive Round-Robin Multitasking**: Local APIC periodic timer-driven context switcher with per-process isolated PML4 address spaces.
- **ELF64 Dynamic Loader**: Validates and maps 64-bit ELF executables from ext2 disks into isolated user virtual memory.
- **Userspace Shell (`/sh.elf`)**: Interactive command-line shell with built-in commands (`pwd`, `cd`, `ls`, `cat`, `echo`, `help`) and external program execution (`sys_spawn`, `sys_waitpid`).

### 🌐 4. Network Stack & Socket API
- **Intel e1000 Gigabit NIC Driver**: PCI discovery (82540EM), 128 KB MMIO mapping, 32-descriptor RX/TX circular DMA rings.
- **Protocol Suite**:
  - IEEE 802.3 Ethernet framing (14-byte header, EtherType demux)
  - RFC 826 ARP (32-entry cache, automatic reply responder)
  - RFC 791 IPv4 (Routing, 16-bit Internet checksum, loopback routing)
  - RFC 792 ICMP (Ping Echo Request / Echo Reply responder)
  - RFC 768 UDP (Datagrams & port listener dispatch)
  - RFC 793 TCP (Active/passive open, three-way handshake `SYN` -> `SYN-ACK` -> `ACK`, data transfer, and clean `FIN` teardown)
- **POSIX Socket Syscall Interface**: `sys_socket`, `sys_bind`, `sys_connect`, `sys_send`, `sys_recv` mapped seamlessly to VFS file descriptors.
- **Userspace Network Client (`/netclient.elf`)**: Independent Ring 3 program executing TCP/UDP socket network transactions over the virtual network.

---

## 🏗️ Building and Running

### Prerequisites
- `clang++` (LLVM 15+ with `--target=x86_64-elf`)
- `ld.lld` (LLVM Linker)
- `xorriso` (for ISO creation)
- `qemu-system-x86_64` (for testing/emulation)
- WSL2 (for generating ext2 disk images via `mke2fs` and `debugfs`)

### Build Commands
```bash
# 1. Compile kernel and build bootable ISO:
make iso

# 2. Run under QEMU:
make run

# 3. Automated QEMU validation suite with 1920x1080 screenshot capture:
powershell -ExecutionPolicy Bypass -File ./run_test.ps1
```

---

## 📜 Dual License

LixOS is licensed under either of:

- **Apache License, Version 2.0** ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
- **MIT License** ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.
