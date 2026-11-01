# LixOS v2 — Roadmap (Sıfırdan Yeniden Yazım)

> Kapsam: Bootloader → Kernel → Bellek Yönetimi → Temel Driver'lar.
> GUI, network stack, FAT32, paket yöneticisi bu roadmap'in dışında —
> bu aşamalar tamamlandıktan sonra ayrı bir roadmap ile ele alınacak.
>
> Kural: Eski (decompile edilmiş) LixOS kodu **hiç referans alınmıyor**.
> Her satır sıfırdan, güncel osdev pratiklerine göre yazılıyor.

## Teknoloji Kararları

- **Mimari:** x86_64
- **Bootloader:** Limine (Limine boot protocol) — BIOS ve UEFI ikisini de destekler
- **Dil:** C++ (freestanding, no stdlib, no exceptions, no RTTI)
- **Derleyici:** Cross-compiler (`x86_64-elf-gcc` / clang + lld), asla host derleyici kullanılmayacak
- **Build sistemi:** Makefile veya Ninja (basit tutulacak, CMake gerekmiyor bu ölçekte)
- **Test/çalıştırma:** QEMU (`qemu-system-x86_64`), gerektiğinde GDB ile kernel debug
- **Versiyon kontrolü:** Her milestone sonunda ayrı bir git tag/commit; kernel panic'e kadar her adım derlenebilir ve boot edebilir halde kalmalı

## Genel Prensipler

1. **Her milestone bağımsız çalışır durumda bitmeli.** Bir sonraki milestone'a geçmeden önce QEMU'da test edilip doğrulanacak.
2. **Küçük, gözden geçirilebilir adımlar.** Tek seferde "tüm bellek yöneticisini yaz" değil; PMM → VMM → heap gibi alt adımlara bölünecek.
3. **Decompile edilen eski koda bakılmayacak.** Mimari kararlar bu roadmap'e ve güncel kaynaklara (OSDev Wiki, Limine dokümantasyonu, Intel SDM) dayanacak.
4. **Panic/hata durumları en baştan düşünülecek.** "Şimdilik crash etsin, sonra düzeltiriz" yaklaşımı yerine, her modülde en azından temel hata kontrolü olacak.

---

## Milestone 0 — Geliştirme Ortamı

- [x] Cross-compiler toolchain kurulumu (`x86_64-elf-gcc`, `binutils` / clang + lld)
- [x] Limine bootloader'ın projeye entegrasyonu (submodule veya vendored)
- [x] QEMU kurulumu ve temel çalıştırma script'i
- [x] Boş bir ISO/disk image üretip Limine ile boot edebilme (henüz kernel yok, sadece bootloader "hello" seviyesi)
- [x] Proje iskeleti: `/kernel`, `/boot`, `/build`, `Makefile`

**Çıktı:** `make run` komutu QEMU'yu açıp Limine'in kernel'i yüklemeye çalıştığını gösterir.

---

## Milestone 1 — Minimal Kernel Girişi ("Hello Kernel")

- [x] Limine boot protokolü ile kernel entry point (`_start`)
- [x] Higher-half kernel linkleme (linker script)
- [x] Framebuffer üzerinden temel yazı yazdırma (Limine'in verdiği framebuffer bilgisiyle basit bir font renderer)
- [x] Seri port (COM1) üzerinden debug çıktısı (`kprintf` benzeri basit bir fonksiyon)
- [x] GDT kurulumu (flat model, kernel + user segment'leri şimdiden ayrılacak)

**Çıktı:** Kernel boot olur, ekrana ve seri porta "LixOS v2 booting..." yazar.

---

## Milestone 2 — Kesme Altyapısı (Interrupts)

- [x] IDT kurulumu
- [x] ISR'lar (exception handler'lar: divide by zero, page fault, GPF vb.)
- [x] IRQ'lar için PIC ya da APIC seçimi (öneri: doğrudan APIC ile başla, PIC'i devre dışı bırak — modern donanımda daha temiz)
- [x] Timer interrupt (PIT veya APIC timer) ile temel "tick" sayacı
- [x] Panic handler: exception geldiğinde register dump + halt

**Çıktı:** Kasıtlı bir page fault / divide-by-zero tetiklendiğinde kernel düzgün bir panic ekranı basıp duruyor.

---

## Milestone 3 — Fiziksel Bellek Yönetimi (PMM)

- [x] Limine'den memory map alınması (usable/reserved bölgeler)
- [x] Bitmap veya free-list tabanlı Physical Memory Manager
- [x] `pmm_alloc_page()` / `pmm_free_page()` API'si
- [x] Bellek istatistikleri (toplam/kullanılan/boş sayfa sayısı) debug çıktısı olarak

**Çıktı:** Rastgele sayfa alloc/free döngüsü çalışır, sayaçlar tutarlı kalır (leak yok).

**Not:** Eski projede PMM free-page mantığında bir bug tespit edilmişti — bu sefer alloc/free simetrisi baştan unit-test benzeri bir sanity check ile doğrulanacak.

---

## Milestone 4 — Sanal Bellek Yönetimi (VMM)

- [x] 4 seviyeli paging (PML4 → PDPT → PD → PT) kurulumu
- [x] Kernel adres alanının (higher-half) map edilmesi
- [x] `vmm_map()` / `vmm_unmap()` API'si
- [x] Page fault handler'ın VMM ile entegrasyonu (demand paging temelleri, en azından "geçersiz erişim = panic" seviyesinde)
- [x] Kernel heap için ayrı bir sanal bölge ayrılması

**Çıktı:** Yeni bir adres alanına page map edip oradan okuma/yazma yapılabiliyor; map edilmemiş adrese erişim düzgün page fault veriyor.

---

## Milestone 5 — Kernel Heap (kmalloc/kfree)

- [x] Basit bir allocator (örn. bump allocator ile başlayıp sonra free-list/slab'a evrilecek)
- [x] `kmalloc()` / `kfree()` / `kcalloc()` API'si
- [x] Alignment desteği (DMA gibi ileride gerekecek ihtiyaçlar için)
- [x] Basit bir leak/double-free sanity check (debug build'de)

**Çıktı:** Kernel içinde dinamik veri yapıları (liste, buffer vb.) güvenle kullanılabiliyor.

---

## Milestone 6 — Temel Driver Altyapısı

- [x] Driver arayüzü için minimal bir soyutlama (init/read/write/ioctl benzeri basit bir sözleşme — aşırı mühendislik yapılmayacak)
- [x] PS/2 klavye driver'ı (interrupt tabanlı)
- [x] PIT veya RTC üzerinden gerçek zaman/tick bilgisi
- [x] ACPI temel parsing (RSDP/RSDT/XSDT okuma) — en azından shutdown ve temel donanım bilgisi için
- [x] (Opsiyonel, milestone sonunda karar verilecek) Seri port'u tam bir driver olarak yeniden düzenleme

**Çıktı:** Klavyeden basılan tuşlar interrupt ile yakalanıp ekrana/seri porta yansıyor; ACPI üzerinden sistem düzgün shutdown edilebiliyor.

---

## Bu Roadmap'in Dışında Kalanlar (Sonraki Faz)

Bunlar bilinçli olarak bu rewrite'ın kapsamı dışında tutuldu, ileride ayrı roadmap'lerle ele alınacak:

- GUI / Window Manager
- Network stack
- FAT32 dosya sistemi ve VFS katmanı
- Paket yöneticisi
- Kullanıcı alanı (userspace) ve process/scheduler (bu rewrite kernel'i şu an tek bir "kernel-mode" ortam olarak düşünüyor; scheduler ayrı bir milestone seti gerektirir)
- **Veri şifreleme:** Kullanıcı verisini korumak için standart, denetlenmiş
  kriptografi (örn. AES + uygun bir disk şifreleme modu, parola tabanlı
  anahtarlar için Argon2/PBKDF2 gibi bir KDF). Bu, dosya sistemi (FAT32/VFS)
  katmanı kurulduktan sonra ele alınacak; kendi tasarım bir şifreleme
  yöntemi kullanılmayacak — bkz. `AI_RULES.md` §6.

---

## Kullanım Notu (AI Ajanı İçin)

Bu roadmap `AI_RULES.md` ile birlikte kullanılacak şekilde tasarlandı. AI ajanı bir milestone üzerinde çalışırken:
- O milestone'daki checkbox'ları sırayla ele alacak
- Bir sonraki milestone'a geçmeden önce mevcut milestone'un "Çıktı" kriterini karşıladığını doğrulayacak
- Roadmap'i güncel tutmak için tamamlanan maddeleri işaretleyecek
