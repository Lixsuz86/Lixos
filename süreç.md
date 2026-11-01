# LixOS v2 — Geliştirme Süreci ve İlerleme Özeti

Bu belge, **LixOS v2** 64-bit freestanding işletim sistemi çekirdeğinin (C++20, Limine Bootloader, x86_64) sıfırdan bugüne kadar tamamlanan tüm geliştirme aşamalarını, teknik mimari kararlarını ve doğrulama kanıtlarını özetler.

---

## 📌 Faz 1: Çekirdek Temelleri ve Donanım Katmanı (Milestone 0 - 6)

### Milestone 0: Altyapı ve Derleme Ortamı
- **Araç Zinciri:** Clang++ (`--target=x86_64-elf`, `-std=c++20`, `-ffreestanding`, `-mcmodel=kernel`, `-mno-red-zone`, `-mno-sse`), LLVM LLD (`ld.lld`), MSYS2/MinGW64, QEMU `qemu-system-x86_64`.
- **Bootloader:** Limine Bootloader (Protocol Base Revision 3, UEFI + BIOS uyumlu ISO imajı üretimi).
- **Otomasyon:** `Makefile` ile tam otomatik derleme, ISO üretimi (`xorriso`, `limine bios-install`) ve çalıştırma hedefleri.

### Milestone 1: Minimal Boot, Segmentasyon ve Konsol
- **GDT (Global Descriptor Table):** Flat 64-bit bellek modeli yapılandırıldı:
  - `0x00`: Null Descriptor
  - `0x08`: Kernel Code Segment (64-bit Ring 0, DPL 0)
  - `0x10`: Kernel Data Segment (Ring 0, DPL 0)
  - `0x18`: User Data Segment (Ring 3, DPL 3)
  - `0x20`: User Code Segment (64-bit Ring 3, DPL 3)
- **Konsol Çıktısı:**
  - `COM1` (0x3F8) Seri Port sürücüsü (hata ayıklama ve loglama için).
  - Limine Linear Framebuffer Console (`fb.cpp`, 8x16 font renderlama, ANSI renk kodları ve kaydırma desteği).
  - Formatlı çekirdek yazdırma motoru (`kprintf`).

### Milestone 2: Kesmeler ve İstisna Yönetimi (IDT)
- **IDT (Interrupt Descriptor Table):** 256 vektörün tamamı için 64-bit Interrupt Gate girişleri oluşturuldu.
- **Assembly Stub'ları (`isr_stubs.S`):** Hata kodlu ve hata kodsuz istisnalar için ortak assembly çerçevesi, tüm register'ların (`pushaq`/`popaq`) korunması.
- **Kernel Panic & Debugger:** CPU Exception'larında (Örn: `#GP`, `#PF`, `#DE`, `#UD`) register dump, instruction pointer (`RIP`), hata kodu ve yığın izini ekrana basan panic handler.
- **PIC:** Eski 8259 PIC yongaları maskelenerek devre dışı bırakıldı.

### Milestone 3: APIC ve Donanım Zamanlayıcısı
- **Local APIC (LAPIC):** MMIO tabanlı erişim (`IA32_APIC_BASE_MSR`), Spurious Interrupt vektörü (255) ayarlandı.
- **LAPIC Timer:** Periodik modda zamanlayıcı kesmesi (`Vector 0x20`) yapılandırıldı, çekirdek zaman sayacı (tick counter) kuruldu.

### Milestone 4: Bellek Yönetimi (PMM, VMM, Heap)
- **PMM (Physical Memory Manager):** Limine bellek haritasını tarayan, 4096 baytlık sayfaları bitmap üzerinden yöneten fiziksel sayfa ayırıcı (`pmm_alloc_page`, `pmm_free_page`).
- **HHDM Entegrasyonu:** Higher-Half Direct Map (`0xffff800000000000`) ile tüm fiziksel belleğe çekirdek alanından doğrudan erişim.
- **VMM (Virtual Memory Manager):** 4 seviyeli x86_64 sayfalama (PML4, PDPT, PD, PT):
  - 2 MB ve 4 KB sayfa eşlemeleri, sayfa öznitelikleri (`PRESENT`, `WRITABLE`, `USER`, `NX`).
  - Çekirdek ve kullanıcı adres alanlarını izole edebilme yeteneği.
- **Kernel Heap:** Dinamik bellek tahsisi için first-fit freelist ayırıcı (`kmalloc`, `kfree`, `kcalloc`, `krealloc`).

### Milestone 5 & 6: Donanım Sürücüleri ve Güç Yönetimi
- **I/O APIC:** PCI ve anakart kesmelerinin LAPIC vektörlerine yönlendirilmesi.
- **PS/2 Klavye:** Scancode Set 1 ayrıştırıcı, Shift/Capslock durum takibi ve canlı ASCII çevrimi.
- **CMOS RTC:** BCD/Binary format dönüşümlü gerçek zaman saati (tarih, saat, dakika, saniye).
- **ACPI:** RSDP/RSDT/XSDT checksum doğrulaması, FADT ve MADT tabloları tespiti, ACPI tabanlı donanımsal sistem kapatma (`acpi_shutdown`).

---

## 📌 Faz 2: Dosya Sistemi, Kriptografi, Userspace & Syscall (Milestone 7 - 9)

### Milestone 7: Disk I/O, VFS ve ext2 Sürücüsü
- **7.1 PCI & AHCI SATA Sürücüsü:** PCI veriyolu taraması ile AHCI HBA denetleyicisinin bulunması, FIS/PRDT tabanlı DMA sektör transferleri (`disk_read_sectors`, `disk_write_sectors`).
- **7.2 VFS (Virtual File System) Katmanı:** Dosya sistemi agnostik `vfs_node`, `vfs_operations` (`open`, `read`, `write`, `close`, `readdir`, `create`, `mkdir`), bağlama mekanizması (`vfs_mount`), path çözümleme (`/`, `/vault.enc`) ve File Descriptor (FD) yönetim tablosu.
- **7.3 ext2 Sürücüsü:**
  - Superblock, Block Group Descriptor Table (BGDT), Inode ayrıştırma (doğrudan ve dolaylı blok işaretçileri).
  - Dizin formatı, dosya oluşturma/yazma (blok/inode bitmap yönetimi).
  - WSL/Linux host tarafında `debugfs` ile oluşturulan ve yazılan dosyaların disk kalıcılığı ve veri bütünlüğü doğrulandı.
- **7.4 Sanity Checks:** FD sızıntı testleri ve çok bloklu büyük dosya okuma/yazma doğrulandı.

### Milestone 8: Güvenlik ve Kriptografik Depolama
- **8.1 AES-256 & XTS-AES-256:**
  - FIPS 197 standardında 256-bit AES blok şifreleyici (S-Box, Key Expansion, SubBytes, ShiftRows, MixColumns).
  - IEEE 1619-2007 standardında XTS disk şifreleme modu (512-bit master key).
  - **NIST Resmi Test Vektörleri** (FIPS 197 C.3, IEEE 1619 Annex D.2 Vector 1, Vector 2 ve 512-byte sektör simetrisi) ile 100% bayt uyumluluğu kanıtlandı.
- **8.2 KDF & Parola Güvenliği:**
  - FIPS 180-4 SHA-256 ve RFC 2104 HMAC-SHA256 implementasyonu.
  - RFC 2898 / RFC 8018 PBKDF2-HMAC-SHA256 (RFC 6070 test vektörleri ile tam eşleşme).
  - 512 baytlık disk başlığı (`CLIX` magic, 32-bayt salt, key verification tag).
  - Yanlış parola girişinde sistemin çökmeden, bellek sızdırmadan güvenle reddetmesi (`ERR_AUTH_FAILED` / `-EACCES`).
- **8.3 CryptFS (Şeffaf VFS Şifreleme Entegrasyonu):**
  - VFS üzerinde transparent okuma/yazma şifreleme katmanı (`/vault.enc`).
  - Doğru parola ile açılıp deşifre edilen verinin doğruluğu; WSL host ortamında hexdump ile incelenen ham ext2 diskinde sıfır düz metin (tamamen anlamsız ciphertext) olduğu kanıtlandı.

### Milestone 9.1: TSS ve Ring 3 Kullanıcı Alanı İzolasyonu
- **TSS (Task State Segment):** 64-bit TSS yapısı oluşturuldu, GDT'ye 16-baytlık TSS tanımlayıcısı (Seçici `0x28`) eklendi, `RSP0` çekirdek yığını yüklendi ve `ltr` komutu çalıştırıldı.
- **Ring 3 Geçişi:** `iretq` komutu ile çekirdekten (Ring 0) kullanıcı alanına (`CS: 0x23`, `SS: 0x1B`, Ring 3) güvenli geçiş yapıldı.
- **Donanımsal İzolasyon Kanıtı:**
  - Kullanıcı modunda çalışan kodun `CPL = 3` olduğu doğrulandı.
  - Ring 3 içerisinden ayrıcalıklı `cli` (kesmeleri kapatma) komutu çalıştırıldığında CPU'nun bunu donanımsal olarak engelleyip `#GP` (Genel Koruma Hatası, Vektör 13) ürettiği ve TSS'in `RSP0` yığınını kullanarak çekirdeğe güvenle döndüğü kanıtlandı.

### Milestone 9.2: Hızlı Syscall Arayüzü (Fast Syscall Interface)
- **MSR Yapılandırması:**
  - `IA32_EFER.SCE` (Bit 0): 64-bit `syscall`/`sysret` komutları aktif edildi.
  - `IA32_STAR` (0xC0000081): Kernel CS (`0x08`), Kernel SS (`0x10`), User Data (`0x1B`), User Code (`0x23`) segmentleri bağlandı.
  - `IA32_LSTAR` (0xC0000082): Assembly giriş noktası (`syscall_entry`) yüklendi.
  - `IA32_SFMASK` (0xC0000084): Syscall girişinde IF (kesmeler) ve bayraklar otomatik maskelendi.
- **Düşük Seviyeli Assembly Stub (`syscall_entry.S`):**
  - Kullanıcı yığınından çekirdek yığınına güvenli geçiş.
  - Register durumunun korunması ve System V AMD64 ABI argüman yönlendirmesi.
  - `sysretq` ile Ring 3'e yüksek performanslı dönüş.
- **Syscall Tablosu ve VFS Entegrasyonu (`syscall.cpp`):**
  - `SYS_WRITE (2)`: Ekrana/seri porta çıktı veya VFS dosyasına yazma.
  - `SYS_READ (1)`: Klavye stdin veya VFS dosyasından okuma.
  - `SYS_OPEN (3)`, `SYS_CLOSE (4)`, `SYS_LSEEK (5)`: VFS dosya işlemleri.
  - `SYS_EXIT (0)`: Sürecin temiz sonlanması ve durum kodunun çekirdeğe iletilmesi.
  - **Hata Savunması:** Tanımsız/geçersiz syscall numaralarında çökme olmadan `-ENOSYS` (-38) dönülmesi sağlandı.
- **Canlı Kullanıcı Modu Doğrulaması:** Ring 3 kullanıcı programından gerçek `SYS_WRITE`, geçersiz `syscall(999)` (-ENOSYS kontrolü) ve `SYS_EXIT(42)` çağrıları yapılarak tüm mekanizma başarıyla test edildi.

### Milestone 9.3: Process Modeli ve Adres Alanı İzolasyonu
- **PCB (Process Control Block) Yapısı (`process.h`):**
  - Süreç kimliği (`pid`), isim, durum (`ProcessState: UNUSED, READY, RUNNING, BLOCKED, TERMINATED`).
  - Donanımsal sayfa dizini kökü (`pml4_phys` / CR3), register context (`ProcessContext`), çekirdek yığını (`kstack_phys`, `kstack_top`), kullanıcı yığını (`ustack_top`) ve süreç bazlı dosya tanımlayıcı tablosu (`fd_table[32]`).
- **Adres Alanı İzolasyonu (VMM Genişletmesi):**
  - `vmm_create_address_space()`: Her yeni süreç için üst yarısı (giriş 256..511) paylaşımlı çekirdek eşlemelerini içeren, alt yarısı (giriş 0..255) sıfırlanmış bağımsız PML4 sayfa dizini üretimi.
  - `vmm_destroy_address_space()`: Süreç sonlandığında alt yarıdaki kullanıcı sayfası ve sayfa tablolarının (PDPT, PD, PT) PMM'e iade edilmesi; üst yarıdaki çekirdek eşlemelerine dokunulmaması.
- **Süreç Yaşam Döngüsü (`process.cpp`):**
  - `process_init()`: PID 0 (çekirdek süreci, "kernel") kaydı.
  - `process_create()`: Bağımsız adres alanı, 16 KB sürece özel çekirdek yığını ve varsayılan FD eşlemeleri ile süreç tahsisi.
  - `process_destroy()`: Adres alanı, yığın ve PCB slotunun temizlenip serbest bırakılması.
  - `process_switch_address_space()`: Donanımsal `%cr3`, TSS `rsp0` ve syscall çekirdek yığınının anlık olarak sürece yönlendirilmesi.
  - `SYS_GETPID (6)`: Süreçlerin kendi PID değerlerini sorgulayabilmesi sağlandı.
- **Canlı İzolasyon Doğrulaması:**
  - `Process 1 (PID 1)` ve `Process 2 (PID 2)` oluşturuldu; tam olarak aynı sanal adrese (`0x00500000`) farklı fiziksel sayfalar eşlendi.
  - Process 1 kendi alanına özel veri yazdıktan sonra Process 2'ye geçildi; Process 2'nin Process 1'in verisini görmediği (sıfır bellek sızıntısı) kanıtlandı.
  - Process 2 kendi verisini yazdıktan sonra Process 1'e geri dönüldü ve Process 1'in verisinin bozulmadan korunduğu doğrulandı.
  - Process 2 yok edildikten (`process_destroy`) sonra sayfalarının geri alındığı ve Process 1'in sağlam kaldığı kanıtlandı.
  - **İkili Doğrulama (Serial Log + Ekran Çıktısı):** QEMU testinde hem `build/serial.log` üzerinden tüm izolasyon adımları ve başarı bayrakları teyit edildi, hem de `run_test.ps1` tarafından üretilen `build/screen.ppm` ekran görüntüsü kontrol edilerek framebuffer konsol çıktısının beklenen görsel durumda olduğu doğrulandı.

### Milestone 9.4: Preemptive Scheduler & Context Switching
- **Kesme Tabanlı Context Switching Altyapısı (`isr_stubs.S`, `idt.cpp`):**
  - Donanımsal zamanlayıcı kesmesinde (LAPIC Timer Vector 0x20) CPU ve `isr_common` tarafından yığına yerleştirilen 15 genel amaçlı register (`rax`..`r15`), kesme no, hata kodu ve donanımsal dönüş çerçevesi (`rip`, `cs`, `rflags`, `rsp`, `ss`) `interrupt_frame` yapısında korundu.
  - `isr_dispatch` dönüş tipi `uint64_t` yapılarak `isr_common` içine `movq %rax, %rsp` eklendi; böylece zamanlayıcı kesmesinden dönerken yığın anında yeni sürecin yığın çerçevesine yönlendirildi ve `iretq` ile yeni süreç canlandırıldı.
- **Preemptive Round-Robin Zamanlayıcı (`scheduler.h`, `scheduler.cpp`):**
  - Hazır kuyruk (`READY` süreçler) arasında adil zaman dilimi (time slice / quantum) tahsisi sağlandı.
  - Süreç zaman dilimi bittiğinde `ProcessState::READY` durumuna alınıp dairesel sırada bir sonraki hazır sürece geçildi.
  - Context switch anında donanımsal CR3 sayfa dizini (`vmm_switch_pml4`) ve TSS `rsp0` (`tss_set_rsp0`) yeni sürece yönlendirildi.
- **Kritik Bölüm ve Kesme Güvenliği (`irq_lock.h`):**
  - `process_create()`, `process_destroy()`, `process_switch_address_space()` ve `sys_exit()` gibi kritik fonksiyonlar RAII tabanlı `InterruptLock` (`pushfq; popq; cli` ve geri yükleme) ile kesme yarış durumlarına (race condition) karşı tam koruma altına alındı.
  - Syscall girişinde (`syscall_entry.S`) MSR `SFMASK (0x4700)` ile donanımsal IF maskelemesi teyit edildi.
- **Gönüllü Bırakma (Cooperative Yield):**
  - `scheduler_yield()` ve `SYS_YIELD (7)` sistem çağrısı eklenerek süreçlerin istedikleri zaman CPU zaman dilimini bırakabilmesi sağlandı.
- **Canlı Eş Zamanlı Çalışma ve İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - `Worker Alpha (PID 3)` ve `Worker Beta (PID 4)` isimli iki bağımsız preemptive süreç oluşturuldu.
  - LAPIC Timer kesmeleri altında her iki sürecin CPU zamanını paylaştığı ve çıktıların seri konsola/ekrana tam sırayla birbirinin arasına girerek (`interleaved: Alpha 1 -> Beta 1 -> Alpha 2 -> Beta 2 -> Alpha 3 -> Beta 3 -> Alpha 4 -> Beta 4`) yazıldığı kanıtlandı.
  - **İkili Doğrulama:** Hem `build/serial.log` üzerinden tüm döngülerin tamamlandığı (4/4 Alpha, 4/4 Beta) ve `[SUCCESS] Milestone 9.4 Complete` mesajı doğrulandı, hem de `run_test.ps1` tarafından üretilen `build/screen.ppm` (1280x800 P6) ve PNG'ye dönüştürülen `build/screen.png` ekran görüntüsü kontrol edilerek framebuffer konsolunun beklenen görsel durumda olduğu teyit edildi.

### Milestone 9.5: ELF64 Program Yükleyicisi (ELF Loader)
- **ELF64 Başlık ve Program Başlığı Ayrıştırma (`elf.h`, `elf.cpp`):**
  - Standart 64-bit ELF başlıkları (`Elf64_Ehdr`, `Elf64_Phdr`) ve segment sabitleri tanımlandı.
  - Sihirli bayt (`0x7F 'E' 'L' 'F'`), mimari (`ELFCLASS64 = 2`, `ELFDATA2LSB = 1`, `EM_X86_64 = 62`) ve çalıştırılabilir dosya türü (`ET_EXEC` / `ET_DYN`) doğrulamaları uygulandı.
- **VFS Üzerinden Segment Yükleme & Bellek Eşleme:**
  - ext2 dosya sistemi üzerindeki `/hello.elf` ikili dosyası VFS API'si (`vfs_open`, `vfs_read`, `vfs_lseek`) ile açıldı.
  - `PT_LOAD` segmentleri (`0x400000` R-- ve `0x4011f0` R-X) tespit edildi; sayfa sınırlarına (4 KB) hizalanarak PMM'den fiziksel sayfalar tahsis edildi.
  - Hedef sürecin izole PML4 adres alanına (`proc->pml4_phys`) uygun izinlerle (`vmm_flags::USER`, `WRITABLE`, `NO_EXECUTE`) eşlendi.
  - Segment verileri doğrudan dosya ofsetinden HHDM üzerinden fiziksel sayfalara kopyalandı ve BSS alanı sıfırlandı.
- **Kullanıcı Modu Yığını & Başlatma:**
  - Sürece özel izole 16 KB kullanıcı yığını (`USER_STACK_BOTTOM = 0x7fffffffa000` - `USER_STACK_TOP = 0x7fffffffe000`) `vmm_flags::USER | vmm_flags::WRITABLE | vmm_flags::NO_EXECUTE` ile tahsis edilip eşlendi.
  - Giriş noktası `ehdr.e_entry` (`0x4011f0`) olarak belirlendi; sürece ait `kstack` üzerinde Ring 3 (`CS = 0x23`, `SS = 0x1B`, `CPL = 3`) `iretq` çerçevesi hazırlandı.
- **Bağımsız Kullanıcı ELF Test Programı (`user/hello.S`):**
  - Saf assembly ile yazılan freestanding x86_64 ikili dosyası `clang` ve `ld.lld` ile derlendi; `create_ext2.sh` (WSL / `debugfs`) aracılığıyla `disk.img` içine `/hello.elf` olarak yazıldı.
  - Program `SYS_WRITE` (Syscall 2) ile çıktı yazıp ardından `SYS_EXIT(0)` (Syscall 0) ile temiz sonlandı.
- **İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - QEMU testi başarıyla tamamlandı:
    - `build/serial.log` dosyasında `[USER ELF] Hello from Userspace ELF binary loaded via VFS & ext2!`, `[USER ELF] Running in Ring 3 (CPL = 3) on isolated page tables!`, `[SYSCALL sys_exit] Process 5 ('hello_elf') terminated cleanly with exit status: 0` ve `[SUCCESS] Milestone 9.5 Complete` kayıtları doğrulandı.
    - `run_test.ps1` tarafından üretilen `build/screen.ppm` ve otomatik olarak PNG'ye dönüştürülen `build/screen.png` ekran görüntüsü incelenerek framebuffer konsol ekranında bağımsız ELF çıktısının ve başarı banner'ının kusursuz şekilde görüntülendiği kanıtlandı.

### Milestone 10: Shell (Komut Satırı Arayüzü & Program Çalıştırma)
- **Sistem Çağrısı Genişletmeleri (`syscall.h`, `syscall.cpp`):**
  - `SYS_READDIR (8)`: VFS `vfs_readdir` köprüsü ile dizin girdilerini (`vfs_dirent`) kullanıcı moduna aktarma.
  - `SYS_SPAWN (9)`: Disk (ext2) üzerindeki hedef ELF ikili dosyasını `process_create` ve `elf_load` ile bağımsız bir süreç olarak oluşturup `ProcessState::READY` kuyruğuna alma.
  - `SYS_WAITPID (10)`: Hedef sürecin sonlanmasını (`ProcessState::TERMINATED`) `scheduler_yield()` ile bekleyip çıkış durumunu alma ve `process_destroy` ile alt süreç kaynaklarını temizleme (POSIX zombie/reaping modeli).
  - `SYS_READ (1)` İyileştirmesi: Stdin (`fd == 0`) okumasında klavye kuyruğu boşken `scheduler_yield()` ile diğer süreçlere CPU zamanı bırakarak verimli bekleme sağlandı.
- **Kullanıcı Modu Kabuk Programı (`user/sh.cpp` -> `/sh.elf`):**
  - Tamamen Ring 3 (CPL = 3) ortamında syscall'lar üzerinden çalışan bağımsız C++ kabuk ikili dosyası `clang++` ve `ld.lld` ile derlendi; ext2 disk imajına `/sh.elf` olarak yazıldı.
  - **Dahili Komutlar (10.1):**
    - `pwd`: Çalışma dizini (`/`) takibi ve gösterimi.
    - `cd <dir>`: Mutlak (`/`), göreceli ve üst dizin (`..`) desteği; hedef dizinin varlığı `O_DIRECTORY` ile denetlenir.
    - `ls [dir]`: `SYS_READDIR` sistem çağrısı ile dizin içerisindeki dosyaları ve dizinleri listeleme (`[DIR ] lost+found`, `[FILE] hello.txt`, `[FILE] system.conf`, `[FILE] hello.elf`, `[FILE] sh.elf`, `[FILE] vault.enc`).
    - `cat <file>`: `SYS_OPEN`, `SYS_READ`, `SYS_CLOSE` ile ext2 üzerindeki dosyaların içeriğini ekrana basma (`hello.txt` içeriği başarıyla okundu).
    - `echo <text>`: Kullanıcı metnini stdout'a yazdırma.
    - `help`: Dahili komutların ve program çalıştırma rehberinin listelenmesi.
  - **Harici Program Çalıştırma & PATH (10.2):**
    - Komut satırından `/hello.elf` (veya `hello.elf`) girildiğinde kabuk `SYS_SPAWN` ile yeni süreç (PID 7) başlattı.
    - `SYS_WAITPID` ile çocuk sürecin sonlanmasını bekledi; çocuk süreç Ring 3'te bağımsız olarak çalışıp `SYS_WRITE` ve `SYS_EXIT(0)` çağrılarını tamamladıktan sonra kabuk kontrolü geri alarak durum kodunu (0) raporladı.
  - **İnteraktif REPL Hazırlığı:**
    - Otomatik test modunun ardından kabuk canlı kullanıcı girişine geçti (`lixos:/# `) ve klavye girdilerini satır düzenleme (`\b`) desteğiyle okumaya hazır hale geldi.
- **İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - **Serial Log (`build/serial.log`):**
    - `[TEST 1] Testing 'pwd': /`
    - `[TEST 2] Testing 'echo': LixOS Userspace Shell Environment Active!`
    - `[TEST 3] Testing 'ls /': [DIR ] lost+found, [FILE] hello.txt, [FILE] system.conf, [FILE] hello.elf, [FILE] sh.elf, [FILE] vault.enc`
    - `[TEST 4] Testing 'cat /hello.txt': Hello from Real ext2 Filesystem! Milestone 7.3 Verified.`
    - `[TEST 5] Testing external ELF execution '/hello.elf' via SYS_SPAWN: Child PID 7 spawned, executed in Ring 3, exited cleanly with status 0.`
    - `[SUCCESS] Milestone 10 Complete: Userspace Shell & Command Execution Verified.` kayıtları doğrulandı.
  - **Ekran Görüntüsü (`build/screen.png`):** `run_test.ps1` tarafından üretilen PNG incelendi; ekranda kabuk testlerinin, dizin listesinin, dosya okumasının, harici program çıktısının ve interaktif `lixos:/# ` kabuk prompt'unun kusursuz görüntülendiği teyit edildi.

- **Milestone 11 (Modern Grafiksel Kullanıcı Arayüzü, Compositor & Pencere Yöneticisi):**
  - **Compositor Temeli (11.1):**
    - Çift tamponlama (double buffering) mimarisi kurularak Limine donanım framebuffer'ı ile arasına 32-bit ARGB arka tampon (backbuffer) yerleştirildi (flicker/titreme tamamen önlendi).
    - Hasar takibi (damage-tracking / dirty-rect bounding box) mimarisi uygulandı: Yalnızca içeriği değişen ekran koordinatları (`damage_min_x/y`, `damage_max_x/y`) donanım framebuffer'ına kopyalanarak boşta %0 CPU ek yükü (zero idle overhead) sağlandı.
    - Bağımsız pencere yüzeyi (`Surface`) soyutlaması, alfa harmanlama (`color::blend`), kırpma (clipping) destekli yüzey aktarımı ve 8x16 bitmap yazı tipi render motoru geliştirildi.
  - **PS/2 Fare Sürücüsü & İmleç (11.2):**
    - PS/2 ikincil portu (0xA8) ve konfigürasyon baytları (0x20/0x60) üzerinden IRQ 12 etkinleştirildi; I/O APIC IRQ12 kesmesi IDT Vector `0x2C`'ye eşlendi.
    - 3 baytlık standart PS/2 paket akışı (0xF4 enable streaming, delta X/Y ve buton durumları) çözümlendi ve ekran sınırlarına kenetlendi (clamped).
    - 12x18 piksellik siyah gölgeli şeffaf beyaz ok imleci donanım seviyesinde render edildi.
  - **Pencere Yöneticisi & Z-Order (11.2):**
    - Çoklu pencere tahsisi, bağımsız piksel yüzeyleri, derinlik sırası (Z-order) dizisi ve fare tıklamasıyla pencere odaklama/taşıma (drag-and-drop) mantığı kuruldu.
    - İki bağımsız sistem penceresi oluşturuldu: "LixOS Terminal (Ring 3)" (aktif odak, terminal çıktıları) ve "LixOS System Settings" (arka planda ayar ve donanım kartı).
  - **Çift Masaüstü Düzeni (Dual Layouts - 11.2):**
    - **1) Windows 10 Klasik Düzeni:** Ekranın altında 40 piksellik koyu görev çubuğu, sol başta 4 kutulu ikonik mavi Windows başlat butonu, ortada açık pencerelerin aktif/pasif butonları, sağ başta dijital saat ve sistem tepsisi; pencerelerde Windows stili başlık çubuğu kontrolleri ([_], [□], [X]).
    - **2) macOS / GNOME Düzeni:** Ekranın üstünde 28 piksellik menü çubuğu, alt merkezde yüzen yuvarlatılmış dock (renkli uygulama ikonları ve aktif çalışma göstergeleri); pencerelerde sol tarafta yer alan renkli trafik ışığı butonları (Kırmızı/Sarı/Yeşil).
  - **Kişiselleştirme, Tema Motoru & Paylaşılabilir Tema Paketleri (11.4 & 11.5):**
    - Temalar saf veri yapısı (`ThemeConfig`) olarak modellendi; `WINDOWS_10_DARK`, `MACOS_DARK` ve `CYBERPUNK_GOLD` hazır temaları tanımlandı.
    - **`.lixtheme` Paket Formatı ve VFS Entegrasyonu (11.5):** Tek bir arşiv dosyası (`LixThemeHeader` + `LixThemeAssetEntry` dizisi) formatı tanımlandı. `theme_export_package()` ve `theme_import_package()` API'leri ile kullanıcı temaları VFS üzerinden dışa ve içe aktarılabilir hale getirildi.
    - **Bağımlılık Yönetimi (Self-Contained Dependency):** Tema paketleri gereksinim duyduğu varlıkları (örn. 4096 baytlık 8x16 bitmap fontu) kendi bünyesinde taşır; dışarıda eksik font veya dosya kalması ("bende çalışıyor, sende bozuk" problemi) engellendi. `/cyberpunk.lixtheme` paketi 4264 bayt olarak font varlığıyla birlikte başarıyla dışa aktarıldı ve okundu.
    - **Kritik Güvenlik Kısıtı (Pasif Veri & Anti-Malware Koruması):** `theme_validate_security()` motoru geliştirilerek tema paketinin **yalnızca pasif veri** içermesi zorunlu kılındı. Paket gövdesinde çalıştırılabilir ikili (ELF sihirli baytı `\x7fELF`), betik yorumlayıcısı (`#!/bin/sh` shebang) veya PE imzası (`MZ`) bulunması durumunda paket derhal reddedilir (`[SECURITY ALERT]`). Test senaryosunda oluşturulan `/malicious.lixtheme` saldırı paketi başarıyla yakalanıp engellendi; sistem çökmeksizin otomatik olarak güvenli `Windows 10 Dark` temasına geri döndü.
    - **Görsel Ayarlar Arayüzü:** "LixOS System Settings" penceresine `[Export .lixtheme]` ve `[Import .lixtheme]` interaktif butonları ile pasif veri güvenlik durumu eklenerek kullanıcıya grafiksel yönetim sağlandı.
- **İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - **Serial Log (`build/serial.log`):**
    - `[ OK ] Compositor Initialized: 1920x1080 (Pitch 7680 bytes, Double Buffering Active)`
    - `[ OK ] PS/2 Mouse Driver Initialized: IRQ 12 -> Vector 0x2c, Pos (960, 540)`
    - `[ OK ] Window Manager Initialized (Layout: Windows 10 Classic, Theme: 'Windows 10 Dark')`
    - `[ OK ] Window Created: ID 1 ('LixOS Terminal (Ring 3)') at (60, 80), size 480x300`
    - `[ OK ] Window Created: ID 2 ('LixOS System Settings') at (420, 160), size 460x320`
    - `[ OK ] Desktop Environment Rendered: Windows 10 Style (Taskbar, Start, System Tray, Z-Order Windows)`
    - `[ OK ] PS/2 Mouse Cursor Rendered at (520, 240) with Damage-Tracking Presentation.`
    - `[WM] Switching Desktop Layout to macOS-esque GNOME Dock Style...`
    - `[ OK ] Desktop Layout Switched: macOS Top Bar + Floating Dock Rendered Cleanly.`
    - `[THEME-1] Testing Theme Export to '/cyberpunk.lixtheme' (with Bundled Font Asset)...`
    - `[THEME] Exported theme 'Cyberpunk Gold' -> '/cyberpunk.lixtheme' (4264 bytes, Assets: 1)`
    - `[ OK ] Theme Package Successfully Exported to '/cyberpunk.lixtheme'.`
    - `[THEME-2] Testing Theme Import & Dependency Validation from '/cyberpunk.lixtheme'...`
    - `[THEME] Package '/cyberpunk.lixtheme' successfully imported: Name: 'Cyberpunk Gold' | Author: 'LixOS Community'`
    - `[ OK ] Package Integrity & Self-Contained Font Asset Verified (0 External Missing Dependencies).`
    - `[THEME-3] Testing Security Constraint: Rejecting Malicious / Executable Packages...`
    - `[SECURITY ALERT] Malicious or invalid theme package in '/malicious.lixtheme'! Violation: SECURITY ALERT: Executable ELF binary signature detected in package payload!`
    - `[SECURITY] Enforcing safe fallback -> Reverting to default 'Windows 10 Dark' theme.`
    - `[ OK ] Security Guardrail Verified: Malicious executable/script payload successfully BLOCKED!`
    - `[ OK ] Safe Fallback Triggered: Active theme remained 'Windows 10 Dark'.`
    - `[THEME-4] Applying Valid Imported Theme Package to Window Manager...`
    - `[THEME] Theme 'Cyberpunk Gold' applied to Window Manager successfully.`
    - `[ OK ] Window Manager Re-rendered with Imported Theme 'Cyberpunk Gold'.`
    - `[THEME-5] Restoring Default Windows 10 Theme as Base Workspace...`
    - `[ OK ] Restored Default Theme: Windows 10 Dark (Safe Fallback Verified).`
    - `[SUCCESS] Milestone 11 Complete: Modern GUI Compositor, WM, Dual Layout & Theme Packages Verified.`
  - **Ekran Görüntüsü (`build/screen.png`):** `run_test.ps1` tarafından kaydedilen PNG formatlı ekran görüntüsü incelendi; ekranda Windows 10 klasik görev çubuğu ve başlat menüsü butonu, Z-order ile üst üste yerleşen Terminal ve Ayarlar pencereleri, Ayarlar penceresi içindeki `[Export .lixtheme]` ve `[Import .lixtheme]` butonları, aktif `.lixtheme` paket bilgisi ve güvenlik doğrulama rozeti ile gerçek zamanlı fare imlecinin kusursuz görüntülendiği teyit edildi.

- **Milestone 12.1: NIC Sürücüsü (Intel 82540EM / e1000 Gigabit Ethernet & Ring Buffers):**
  - **PCI Algılama ve Donanım Tespiti (`pci.cpp`, `e1000.cpp`):**
    - PCI veri yolu taranarak Intel 82540EM Gigabit Ethernet denetleyicisi (Vendor ID `0x8086`, Device ID `0x100E`) Bus 0, Slot 4, Func 0 üzerinde tespit edildi.
    - Ağ sürücüsü soyutlaması için `enum class driver_type::NETWORK` eklendi.
  - **BAR0 MMIO Bellek Eşleme (Kritik Mimari Karar):**
    - Limine HHDM yalnızca BIOS bellek haritasındaki standart RAM'i önceden eşler; yüksek PCI MMIO adresleri (örn. `0xFEBC0000`, 128 KB) eşlenmemiş durumdadır.
    - Herhangi bir `#PF` (Page Fault) istisnasına izin vermemek adına BAR0 fiziksel alanı 32 sayfa (128 KB) boyunca çekirdek master PML4 tablosuna `vmm_map_page()` ile `NO_CACHE` bayrağıyla birebir sanal HHDM adresine eşlendi.
  - **MAC Adresi Okuma & Donanım Başlatma:**
    - RAL0 (`0x5400`) ve RAH0 (`0x5404`) donanım register'ları okunarak kartın fabrika çıkışlı MAC adresi (`52:54:00:12:34:56`) başarıyla elde edildi.
    - Multicast Table Array (MTA, 128 kayıt) sıfırlandı.
    - `CTRL_SLU` (Set Link Up, Bit 6) register komutu verilerek donanımsal Gigabit Full Duplex bağlantı aktif edildi.
  - **DMA Ring Buffer Yönetimi (RX / TX Rings):**
    - PMM üzerinden fiziksel olarak kesintisiz sayfalarda 32 adet 16-baytlık RX tanımlayıcısı (`e1000_rx_desc`) ve 32 adet 2048-baytlık DMA alım tamponu tahsis edildi.
    - 32 adet 16-baytlık TX tanımlayıcısı (`e1000_tx_desc`) ve 32 adet 2048-baytlık iletim tamponu kuruldu.
    - `RDBAL`/`RDBAH`, `RDLEN` (512B), `RDH`/`RDT` ve `RCTL` (Broadcast accept, 2048B buffer size, Loopback mode disabled) yapılandırılarak alıcı etkinleştirildi.
    - `TDBAL`/`TDBAH`, `TDLEN` (512B), `TDH`/`TDT`, `TIPG` (IPG 10/8/6 standardı) ve `TCTL` (Enable, Pad Short Packets, Collision params) yapılandırılarak verici etkinleştirildi.
  - **Canlı Ethernet Çerçeve İletimi (Frame Transmission):**
    - 64 baytlık standart IEEE 802.3 broadcast çerçevesi (Hedef: `FF:FF:FF:FF:FF:FF`, Kaynak: `52:54:00:12:34:56`, EtherType: `0x88B5`) oluşturuldu.
    - Tanımlayıcıya `TXD_CMD_EOP` (End of Packet) ve `TXD_CMD_IFCS` (Insert FCS/CRC) bayrakları verilerek iletim kuyruğuna yazıldı (`TDT` artırıldı).
    - e1000 donanımının çerçeveyi başarıyla kabloya aktarıp `TXD_STAT_DD` (Descriptor Done) bitini set ettiği doğrulandı.
  - **Görsel Masaüstü Entegrasyonu:**
    - 1920x1080 Full HD masaüstü tuvalinde "LixOS System Settings" penceresine `Network: Intel e1000 (52:54:00:12:34:56) | Link: UP (Gigabit)` donanım durumu entegre edildi.
- **İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - **Serial Log (`build/serial.log`):**
    - `[NIC-1] Detecting and Initializing Intel e1000 Gigabit Ethernet Controller...`
    - `[ OK ] Intel e1000 NIC Detected: PCI Bus 0, Slot 4, Func 0 (Dev ID: 0x100e)`
    - `[ OK ] e1000 MMIO Mapped: BAR0 Phys = 0xFEBC0000 -> Virt = 0xffff8000febc0000 (128 KB)`
    - `[ OK ] e1000 MAC Address Read: 52:54:00:12:34:56`
    - `[ OK ] e1000 RX/TX Ring Buffers Initialized (RX: 32 Descs, TX: 32 Descs)`
    - `[ OK ] e1000 Hardware Link Status: UP (Gigabit Full Duplex Active)`
    - `[ OK ] NIC Hardware MAC Verified: 52:54:00:12:34:56`
    - `[NIC-2] Constructing and Transmitting Standard IEEE 802.3 Ethernet Frame...`
    - `[ OK ] Ethernet Broadcast Frame Transmitted: 64 Bytes (FCS/CRC Inserted by Hardware).`
    - `[ OK ] Hardware Transmit Descriptor Status: DD (Descriptor Done) Acknowledged.`
    - `[SUCCESS] Milestone 12.1 Complete: Intel e1000 NIC Driver, Rings & Frame TX Verified.` kayıtları doğrulandı.
  - **Ekran Görüntüsü (`build/screen.png`):** `run_test.ps1` tarafından üretilen 1920x1080 Full HD PNG incelendi; ekranda ağ arayüzü durumunun "Network: Intel e1000 (52:54:00:12:34:56) | Link: UP" olarak yeşil göstergeyle görüntülendiği teyit edildi.

- **Milestone 12.2: Çekirdek Ağ Yığını ve Temel Protokol Katmanları (Ethernet, ARP, IPv4, ICMP, UDP, TCP):**
  - **Ağ Yardımcıları ve Endianness (`net.h`):**
    - RFC 1700 Network Byte Order (Big-Endian) <-> x86_64 Little-Endian dönüştürücüleri: `htons`, `ntohs`, `htonl`, `ntohl`.
    - RFC 1071 / RFC 791 16-bit One's Complement Internet Checksum algoritması uygulandı (`net_checksum`).
    - Statik ağ yapılandırması: LixOS IP: `10.0.2.15`, Gateway: `10.0.2.2`, Alt Ağ Maskesi: `255.255.255.0`, DNS: `10.0.2.3`.
  - **Ethernet Çerçeve Ayrıştırma ve Gönderme (`net.cpp`):**
    - 14 baytlık standart IEEE 802.3 Ethernet başlığı (`dest_mac`, `src_mac`, `ethertype`) üzerinden filtreleme ve yönlendirme.
    - Gelen paketlerde EtherType kontrolü: `0x0806` -> `arp_handle_packet()`, `0x0800` -> `ipv4_handle_packet()`.
    - `net_poll()` fonksiyonu: e1000 RX tanımlayıcı halkasını periyodik ve olay bazlı boşaltarak paketleri üst katmanlara aktarır.
  - **ARP (Adres Çözümleme Protokolü - `arp.cpp`):**
    - RFC 826 standardı 28 baytlık ARP paketi (`hw_type = 1`, `proto_type = 0x0800`, `opcode = 1/2`).
    - 32 girdili dinamik **ARP Cache Tablosu** (IP <-> MAC eşlemesi, LRU eskime mantığı).
    - Ağ geçidi (`10.0.2.2`) IP-MAC eşlemesi çözümlendi (`52:54:00:12:34:02`).
    - Ağdan gelen ARP isteklerine LixOS MAC adresiyle anında otomatik `ARP Reply` dönülmesi doğrulandı; gelen sorguların gönderen IP/MAC bilgileri otomatik olarak ARP cache tablosuna işlendi.
  - **IPv4 Katmanı & Routing (`ipv4.cpp`):**
    - RFC 791 standardında 20 baytlık IPv4 başlığı (`0x45`, TTL 64, Don't Fragment `0x4000`, Checksum, IP adresleri).
    - Subnet yönlendirmesi: Hedef IP yerel alt ağdaysa doğrudan hedefin MAC'ine, harici ağdaysa Gateway (`10.0.2.2`) MAC'ine yönlendirme.
    - RFC 791 internet checksum doğrulaması: Bozuk/hasarlı paketler elenir, geçerli paketler protokollerine göre ICMP (`1`), TCP (`6`), UDP (`17`) modüllerine yönlendirilir.
  - **ICMP Ping Yankı Protokolü (`icmp.cpp`):**
    - RFC 792 ICMP Echo Request (`Type 8`) ve Echo Reply (`Type 0`) mimarisi kuruldu.
    - **Canlı Host Ping Doğrulaması:** LixOS, QEMU Gateway (`10.0.2.2`) IP'sine canlı Echo Request iletti ve QEMU SLIRP sanal yönlendiricisinden canlı **Echo Reply** paketini başarıyla teslim aldı (`[ICMP] Ping Success: Received Echo Reply from 10.0.2.2 (ID: 51966, Seq: 1, Len: 31)`).
    - Otomatik Echo Reply Motoru: Dışarıdan LixOS'a gelen ICMP Echo Request paketlerine aynı ID ve Sequence numarasıyla canlı yanıt üretildi.
  - **UDP Protokolü (`udp.cpp`):**
    - RFC 768 8-baytlık UDP başlığı ve port bağlama (binding) altyapısı (`udp_bind_port`).
    - Gateway DNS portuna (`10.0.2.2:53`) giden datagram iletimi ve 7777 numaralı porta gelen UDP verisinin dinleyici geri çağırım fonksiyonuna (callback) yönlendirilmesi doğrulandı.
  - **TCP Protokolü ve Üç Yönlü El Sıkışma (`tcp.cpp`):**
    - RFC 793 TCP başlığı, kontrol bayrakları (SYN, ACK, FIN, PSH, RST) ve 12-baytlık IPv4 Pseudo-Header checksum motoru.
    - TCP Durum Makinesi: `CLOSED`, `LISTEN`, `SYN_SENT`, `SYN_RECEIVED`, `ESTABLISHED`, `FIN_WAIT_1`, `CLOSE_WAIT`.
    - **Pasif Açılış (Server - Port 80):** `LISTEN` -> İstemci SYN -> `SYN_RECEIVED` (SYN-ACK yanıtı) -> İstemci ACK -> `ESTABLISHED` durumu. HTTP GET yükü tamponlandı ve FIN ile bağlantı temiz şekilde kapatıldı.
    - **Aktif Açılış (Client - Port 443):** `SYN_SENT` (SYN iletimi) -> Sunucu SYN-ACK alımı -> ACK iletimi -> `ESTABLISHED` durumu. Faz 3 TLS ön hazırlık yükü iletildi ve soket düzgün kapatıldı.
  - **64-bit Freestanding Bellek Optimizasyonu (`string.cpp`, `fb.cpp`):**
    - 1920x1080 çözünürlükte (satır başına 7680 bayt, 7.8 MB ekran kaydırma) performansı maksimize etmek amacıyla `memcpy`, `memset`, `memmove` ve `fb_scroll` fonksiyonları 64-bit qword (8-bayt) register transferleriyle optimize edildi.
- **İkili Doğrulama (Serial Log + Ekran Görüntüsü):**
  - **Serial Log (`build/serial.log`):**
    - `[NET-1] Verifying ARP (Address Resolution Protocol - RFC 826)...`
    - `[ OK ] ARP Cache Lookup: 10.0.2.2 -> 52:54:00:12:34:02 (Valid: YES)`
    - `[ OK ] ARP Automatic Reply Generated & Sender Cached in ARP Table (Entries: 2).`
    - `[NET-2] Verifying IPv4 Protocol (RFC 791 Checksum & Subnet Routing)...`
    - `[ OK ] RFC 791 IPv4 Checksum Algorithm Verified (Header Checksum: 0x1091, Folded Sum: 0).`
    - `[NET-3] Verifying ICMP (RFC 792 Ping Echo Request / Echo Reply)...`
    - `[ OK ] ICMP Echo Request Transmitted to Gateway (10.0.2.2, ID: 0xCAFE, Seq: 1).`
    - `[ OK ] ICMP Echo Request from 10.0.2.2 Handled -> Echo Reply Generated and Transmitted.`
    - `[NET-4] Verifying UDP (RFC 768 Datagram Transmission & Port Listener)...`
    - `[ OK ] UDP Datagram Transmitted to 10.0.2.2:53 (Src Port: 49152, 17 bytes).`
    - `[ OK ] Inbound UDP Datagram Dispatched to Bound Port 7777 Handler (Data Verified).`
    - `[NET-5] Verifying TCP (RFC 793 Three-Way Handshake & State Machine)...`
    - `[ OK ] Server Socket Created: Listening on Port 80 (State: LISTEN).`
    - `[ OK ] TCP Handshake Step 1 (SYN) -> Server Responded with SYN-ACK (State: SYN_RECEIVED).`
    - `[ OK ] TCP Handshake Step 2 (ACK) -> Connection Established (State: ESTABLISHED)!`
    - `[ OK ] TCP Data Payload Received & Buffered in Socket (31 bytes, Seq/Ack updated).`
    - `[ OK ] TCP Connection Graceful Teardown Handled (Peer FIN Acknowledged).`
    - `[NET-6] Verifying TCP Active Open (Client Connect & Three-Way Handshake)...`
    - `[ OK ] Client Initialized Active Open (State: SYN_SENT, Local Port: 49200).`
    - `[ OK ] TCP Client Completed Three-Way Handshake -> State: ESTABLISHED!`
    - `[ OK ] TCP Client Transmitted Application Payload (29 bytes).`
    - `[ OK ] TCP Client Socket Closed Cleanly.`
    - `[ ICMP] Ping Success: Received Echo Reply from 10.0.2.2 (ID: 51966, Seq: 1, Len: 31)`
    - `[SUCCESS] Milestone 12.2 Complete: Core Network Stack Protocols Verified.` kayıtları doğrulandı.
  - **Ekran Görüntüsü (`build/screen.png`):** `run_test.ps1` tarafından üretilen 1920x1080 Full HD PNG incelendi; ekranda ağ arayüzü ve protokol durumunun "Net: 10.0.2.15/24 (GW: 10.0.2.2) | ARP/IPv4/ICMP/TCP: OK" olarak yeşil göstergeyle görüntülendiği teyit edildi.

- **Milestone 12.3: Soket Arayüzü & Kullanıcı Modu Ağ Erişimi (Socket Syscall'ları & /netclient.elf):**
  - **Soket Sistem Çağrıları (`syscall.h`, `syscall.cpp`, `socket.h`, `socket.cpp`):**
    - `SYS_SOCKET (11)`: `AF_INET`, `SOCK_STREAM` (TCP) ve `SOCK_DGRAM` (UDP) soket nesnelerinin çekirdekte tahsis edilmesi ve VFS dosya tanımlayıcı (FD) tablosuna bağlanması (`vfs_allocate_fd`).
    - `SYS_BIND (12)`: Soketin yerel porta bağlanması ve dinleyici kuyruğunun aktifleştirilmesi (`tcp_listen`, `udp_bind_port`).
    - `SYS_CONNECT (13)`: Uzak IP ve porta aktif TCP el sıkışması başlatılması (`tcp_connect`, SYN-ACK bekleme döngüsü) veya UDP varsayılan hedefinin belirlenmesi.
    - `SYS_SEND (14)`: Soket üzerinden TCP segmenti veya UDP datagramı iletimi (`tcp_send_segment`, `udp_send`).
    - `SYS_RECV (15)`: Soket halka tamponundan (`rx_buffer`) kullanıcı tamponuna veri kopyalama ve NIC RX halkasının polllenmesi (`net_poll`).
  - **VFS Dosya Tanımlayıcısı (FD) Polimorfizmi:**
    - `vfs_node_type::SOCKET (0x07)` tipi tanımlandı.
    - Soket FD'leri üzerinden standart `sys_write()`, `sys_read()` ve `sys_close()` çağrılarının şeffaf olarak soket alt sistemine yönlendirilmesi sağlandı.
  - **Kullanıcı Modu Ağ İstemcisi (`user/netclient.cpp` -> `/netclient.elf`):**
    - Tamamen Ring 3 (CPL = 3) seviyesinde çalışan bağımsız ikili dosya.
    - `SYS_SOCKET` ile TCP soketi açtı (FD 3), `SYS_CONNECT` ile ağ geçidine (10.0.2.2:80) bağlandı.
    - `SYS_SEND` ile HTTP GET iletti, standart `sys_write` ile VFS polimorfizmini doğruladı.
    - `SYS_RECV` ile yanıt aldı, `SYS_CLOSE` ile soketi kapattı.
    - `SYS_SOCKET` ile UDP soketi tahsis etti, 10.0.2.2:53 DNS portuna datagram gönderdi ve kapattı.
    - `sys_exit(0)` ile başarıyla sonlandı.
  - **Çift Doğrulama (`AI_RULES.md`):**
    - `build/serial.log`:
      - `[SOCK-1] Testing Kernel Socket Subsystem & VFS File Descriptor Allocation...`
      - `[ OK ] TCP Stream Socket Created: FD = 1, VFS Node Type = SOCKET (0x07)`
      - `[ OK ] UDP Datagram Socket Created: FD = 2, VFS Node Type = SOCKET (0x07)`
      - `[ OK ] Socket File Descriptors Closed Cleanly via VFS sys_close().`
      - `[SOCK-2] Spawning Userspace Network Client '/netclient.elf' in Ring 3...`
      - `[ OK ] Process Created: PID 7 ('netclient'), PML4: 0x0x23b3000`
      - `[ OK ] /netclient.elf Loaded: Entry 0x0x401840, UStack = 0x0x7fffffffe000`
      - `[NETCLIENT-1] Creating TCP Socket via SYS_SOCKET(AF_INET, SOCK_STREAM)... FD: 3`
      - `[NETCLIENT-2] Transmitting HTTP GET Request via SYS_SEND...`
      - `[NETCLIENT-3] Testing VFS File Descriptor Polymorphism via standard sys_write(fd)...`
      - `[NETCLIENT-4] Receiving Data via SYS_RECV...`
      - `[NETCLIENT-5] Creating UDP Socket via SYS_SOCKET(AF_INET, SOCK_DGRAM)... Transmitted UDP Datagram (32 bytes to 10.0.2.2:53 via SYS_SEND)`
      - `[SUCCESS] Milestone 12.3 Userspace Network Client (netclient.elf) PASSED!`
      - `[SYSCALL sys_exit] Process 7 ('netclient') terminated cleanly with exit status: 0`
      - `[SUCCESS] Milestone 12.3 Complete: Socket Syscall Interface & Userspace Network Access Verified.`
      - `FAZ 2 (MILESTONE 7 - 12) 100% TAMAMLANDI!`
    - **Ekran Görüntüsü (`build/screen.png`):** `run_test.ps1` tarafından üretilen 1920x1080 Full HD PNG incelendi; Sistem Ayarları penceresinde "Net: Socket API & Userspace Net Active | Faz 2 Complete!", Terminal penceresinde `/netclient.elf` çalıştırması ve "[OK] Connected 10.0.2.2:80 | HTTP GET Sent | Exit 0" çıktısının yeşil göstergeyle görüntülendiği teyit edildi.

---

## 📊 Mevcut Durum Tablosu

| Milestone | Kapsam | Durum | Doğrulama Yöntemi |
|---|---|---|---|
| **0 - 6 (Faz 1)** | Boot, GDT, IDT, LAPIC, PMM, VMM, Heap, Drivers, ACPI | ✅ **TAMAMLANDI** | QEMU + Seri Port + Donanım Testleri |
| **7.1 - 7.4** | AHCI SATA DMA, VFS Soyutlaması, ext2 Sürücüsü | ✅ **TAMAMLANDI** | QEMU + ext2 İmajı + WSL `debugfs` |
| **8.1 - 8.3** | AES-256, XTS, PBKDF2, CryptFS Şeffaf Şifreleme | ✅ **TAMAMLANDI** | NIST Vektörleri + WSL Hexdump |
| **9.1** | TSS Kurulumu, Ring 3 Geçişi, #GP Donanım İzolasyonu | ✅ **TAMAMLANDI** | CPL=3 Kontrolü + #GP İstisna Yakalama |
| **9.2** | MSR Yapılandırması, `syscall`/`sysretq`, Syscall Dispatcher | ✅ **TAMAMLANDI** | Ring 3 `write`/`exit`/`-ENOSYS` Testi |
| **9.3** | Process Modeli (PCB, Adres Alanı İzolasyonu) | ✅ **TAMAMLANDI** | Çift Süreç CR3 İzolasyon Testi (`serial.log` + `screen.png`) |
| **9.4** | Preemptive Scheduler & Context Switching | ✅ **TAMAMLANDI** | Donanım Timer ile Eş Zamanlı Süreç Testi (`serial.log` + `screen.png`) |
| **9.5** | ELF64 Program Yükleyicisi | ✅ **TAMAMLANDI** | QEMU + ext2 `/hello.elf` (`serial.log` + `screen.png`) |
| **10** | Shell (Komut Satırı Arayüzü & Program Çalıştırma) | ✅ **TAMAMLANDI** | QEMU + ext2 `/sh.elf` (`serial.log` + `screen.png`) |
| **11** | GUI (Compositor, Pencere Yöneticisi & Çift Masaüstü Düzeni) | ✅ **TAMAMLANDI** | QEMU + Çift Doğrulama (`serial.log` + `screen.png`) |
| **12.1** | NIC Sürücüsü (Intel e1000 Gigabit Ethernet & Ring Buffers) | ✅ **TAMAMLANDI** | QEMU + Çift Doğrulama (`serial.log` + `screen.png`) |
| **12.2** | Temel Protokoller (Ethernet, ARP, IPv4, ICMP Ping, UDP, TCP) | ✅ **TAMAMLANDI** | QEMU + Çift Doğrulama (`serial.log` + `screen.png`) |
| **12.3** | Soket Arayüzü (Syscall'lar & Userspace Ağ Erişimi) | ✅ **TAMAMLANDI** | QEMU + Çift Doğrulama (`serial.log` + `screen.png`) |

---

## 🏆 Faz 2 (Milestone 7 - 12) Başarıyla Tamamlandı!
LixOS v2, Freestanding x86_64 çekirdek mimarisinde depolama (AHCI/ext2/CryptFS), süreç modeli (Ring 3/Preemptive Scheduler/ELF64), modern grafik masaüstü (1920x1080 Compositor/Window Manager/Dual Layout/Theme Packages) ve tam ağ yığını (Intel e1000/ARP/IPv4/ICMP/UDP/TCP/Socket Syscall API/Userspace Net Access) özelliklerini başarıyla kazanmıştır.


