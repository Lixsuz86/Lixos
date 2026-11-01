# LixOS v2 — ROADMAP 2 (Faz 2: Dosya Sistemi (ext2) → Userspace → Shell → GUI → Network)

> Bu roadmap, `ROADMAP.md`'deki Faz 1'in (Milestone 0-6: bootloader,
> kernel, bellek yönetimi, temel driver'lar) tamamlanmasının ardından
> devreye girer. Aynı kurallara tabidir — bkz. `AI_RULES.md`.
>
> Faz 2, önceki fazdan farklı olarak **birbirine sıkı bağımlı** milestone'lar
> içerir: her milestone bir öncekinin üzerine inşa edilir. Sıra değiştirilemez
> (örn. userspace'ten önce dosya sistemi olmadan yüklenecek program yok).

## Faz 2 Genel Bakış

| Milestone | Konu | Bağımlılık |
|---|---|---|
| 7 | Disk I/O + VFS + ext2 | Faz 1 (PMM/VMM/heap) |
| 8 | Veri Şifreleme | Milestone 7 (VFS) |
| 9 | Userspace + Scheduler | Milestone 7 (ELF için dosya sistemi) |
| 10 | Shell | Milestone 9 (userspace) |
| 11 | GUI | Milestone 9 (userspace), Milestone 10 (shell'den bağımsız ama pratikte sonra) |
| 12 | Network Stack | Milestone 9 (userspace, driver modeli) |

---

## Milestone 7 — Disk I/O, VFS ve ext2

### 7.1 — Disk Sürücüsü (AHCI/IDE)
- [x] PCI bus tarama (disk denetleyicisini bulmak için temel PCI enumeration)
- [x] AHCI sürücüsü (öncelik AHCI — modern donanımda standart; IDE sadece QEMU/eski donanım fallback'i olarak düşünülebilir)
- [x] Sektör okuma/yazma API'si: `disk_read_sectors()` / `disk_write_sectors()`
- [x] DMA tabanlı transfer (polling yerine — kaynak verimliliği prensibine uygun, bkz. `AI_RULES.md` §7)

**Çıktı:** Kernel, disk'ten ham sektörleri okuyup içeriğini seri porta/ekrana basabiliyor.

### 7.2 — VFS (Virtual File System) Soyutlaması
- [x] Genel dosya sistemi arayüzü — Linux'un `file_operations`/`inode_operations` mantığına yakın bir `vfs_node` yapısı: `open`, `read`, `write`, `close`, `readdir`
- [x] Mount noktası mekanizması (`vfs_mount()`, `vfs_unmount()`)
- [x] Path çözümleme (`/`, `/boot`, vb. gibi path'lerin ilgili dosya sistemine yönlendirilmesi)
- [x] Dosya tanımlayıcı (file descriptor) tablosu — ileride syscall'larla kullanılacak temel yapı
- [x] **Gelecek genişlemeye uygun tasarım:** VFS arayüzü, ext2'ye özel varsayımlar (örn. sabit blok boyutu, belirli bir inode yapısı) içermeyecek şekilde genel tutulacak. İlerleyen aşamalarda **ext3/ext4** (journaling) ve **btrfs** (copy-on-write, checksumming) desteği eklenmesi planlanıyor — bu driver'lar Milestone 7'nin kapsamında değil, ama VFS'in bunları sonradan sorunsuz kabul edebilmesi bu aşamada gözetilecek

**Çıktı:** VFS katmanı, henüz gerçek bir dosya sistemi bağlı değilken bile mock/ramfs bir node ile test edilebiliyor.

### 7.3 — ext2 Sürücüsü
- [x] Superblock parsing (blok boyutu, inode sayısı, blok grubu bilgisi)
- [x] Blok grubu tanımlayıcıları (Block Group Descriptor Table) okuma
- [x] Inode yapısı parsing (izinler, sahiplik, boyut, blok işaretçileri — direkt/indirect/double-indirect)
- [x] Dizin girdisi parsing (ext2 dizin formatı: inode numarası + isim)
- [x] Dosya okuma: inode'un blok işaretçilerini takip ederek tam dosya içeriğini VFS'e sunma
- [x] Dosya yazma ve yeni dosya/inode oluşturma (bitmap tabanlı blok/inode ayırma — PMM'deki bitmap deneyimiyle kavramsal olarak örtüşüyor)
- [x] Temel Unix izin modeli (rwx, sahiplik) — ileride kullanıcı ayrımı için altyapı
- [x] VFS katmanına ext2 driver'ının bağlanması (`ext2_mount()`)

**Çıktı:** Kernel, disk üzerindeki gerçek bir ext2 bölümünü mount edip bir dosyayı VFS API'si üzerinden okuyup içeriğini ekrana/seri porta basabiliyor. Yeni bir dosya oluşturup içine yazıp tekrar okuyarak, ve izin/sahiplik bilgisinin doğru okunduğu doğrulanacak.

**Not:** Test bölümü hazırlamak için host tarafında (Linux/WSL) `mke2fs` ile küçük bir ext2 imajı oluşturup QEMU'ya disk olarak bağlamak pratik bir başlangıç noktası olur — sürücü kendi yazma desteğini kazanana kadar.

### 7.4 — Sanity Check
- [x] Aynı dosyayı defalarca aç/oku/kapat döngüsü ile file descriptor sızıntısı kontrolü
- [x] Büyük bir dosyayı (birden fazla cluster'a yayılan) okuyup bütünlüğünü (örn. checksum ile) doğrulama

---

## Milestone 8 — Veri Şifreleme

> Bkz. `AI_RULES.md` §8 — kendi kripton'unu yazma, standart algoritma kullan.

### 8.1 — AES Implementasyonu
- [x] AES-256 blok şifreleme (bilinen, denetlenmiş algoritma — S-box, key expansion, round fonksiyonları standart AES spesifikasyonuna birebir uyacak)
- [x] XTS modu (disk şifreleme için endüstri standardı — sektör bazlı, tweakable)
- [x] Kendi test vektörleriyle doğrulama (NIST test vektörleri ile AES implementasyonunun doğruluğunu teyit etmek — bu, "kendi kripton'unu yazma" kuralına aykırı değil, çünkü algoritma standart AES'tir, sadece implementasyon sınanıyor)

**Çıktı:** Bilinen bir test vektörü (plaintext + key) verildiğinde, implementasyon NIST'in beklediği ciphertext'i üretiyor.

### 8.2 — Anahtar Türetme (KDF)
- [x] Argon2 veya PBKDF2 implementasyonu (parola → şifreleme anahtarı)
- [x] Salt üretimi ve saklanması (disk üzerinde ayrı bir metadata bölgesi)

**Çıktı:** Aynı parola + salt her seferinde aynı anahtarı üretiyor; farklı parola farklı anahtar üretiyor.

### 8.3 — VFS Entegrasyonu
- [x] Şifreli bölüm/dosya erişimi için VFS katmanına şeffaf bir şifreleme katmanı eklenmesi (okuma sırasında deşifre, yazma sırasında şifreleme)
- [x] Boot sırasında parola isteme mekanizması (basit bir metin girişi ekranı — henüz tam GUI yok)
- [x] Yanlış parola durumunda güvenli hata yönetimi (sistem bozulmadan reddetme)

**Çıktı:** Şifreli bir dosya, doğru parola ile açılıp okunabiliyor; yanlış parola ile anlamlı veri üretmiyor (ve sistem çökmeden hatayı bildiriyor).

---

## Milestone 9 — Userspace ve Scheduler

### 9.1 — Ring 3 Geçişi
- [x] TSS (Task State Segment) kurulumu — ring geçişlerinde stack switch için gerekli
- [x] GDT'ye user code/data segment'lerinin eklenmesi (Milestone 1'de zaten placeholder olarak vardı — 0x18/0x20)
- [x] `iretq` ile ring 0'dan ring 3'e ilk güvenli geçiş testi

**Çıktı:** Basit bir user-mode fonksiyon (örn. sonsuz döngüde `nop`) ring 3'te çalıştırılabiliyor ve kernel bunu kesme ile geri alabiliyor.

### 9.2 — Syscall Arayüzü
- [x] `syscall`/`sysret` mekanizması (MSR üzerinden `STAR`, `LSTAR`, `SFMASK` yapılandırması)
- [x] Syscall tablosu ve dispatch mekanizması
- [x] İlk syscall'lar: `write`, `read`, `open`, `close`, `exit` (VFS'e bağlı)
- [x] Tanımsız syscall savunması (-ENOSYS döndürme) ve kullanıcı çıkış yönetimi

**Çıktı:** User-mode bir program `write` syscall'ı ile ekrana/seri porta veri yazdırabiliyor, tanımsız syscall çağrısında güvenle -ENOSYS hatası dönüyor ve `exit` syscall'ı ile temiz sonlanıyor.

### 9.3 — Process Modeli
- [x] Process Control Block (PCB) yapısı: PID, sayfa tablosu (PML4), register durumu, dosya tanımlayıcıları
- [x] Process oluşturma (`process_create()`) — her process kendi adres alanına (VMM ile) sahip
- [x] Process sonlandırma ve kaynak temizliği (bellek, açık dosyalar)

**Çıktı:** İki farklı process oluşturulup her birinin bağımsız bellek alanına sahip olduğu doğrulanıyor (biri diğerinin belleğini göremiyor).

### 9.4 — Scheduler
- [x] Basit round-robin scheduler (timer interrupt üzerinden context switch tetikleme)
- [x] Context switch (register/stack kaydetme-yükleme)
- [x] Çalışma kuyruğu (ready queue) yönetimi

**Çıktı:** İki veya daha fazla process aynı anda "çalışıyormuş" gibi davranıyor — her biri periyodik olarak CPU zamanı alıyor (örn. iki process de kendi tick sayacını sırayla artırıyor, ekranda karışık bir çıktı gösteriyor).

### 9.5 — ELF Loader
- [x] ELF64 header parsing (program header'lar, segment bilgileri)
- [x] Disk'ten (VFS üzerinden) bir ELF dosyasının belleğe yüklenmesi
- [x] Segment'lerin doğru izinlerle (R/W/X) map edilmesi

**Çıktı:** Disk üzerindeki basit bir "hello world" ELF programı yüklenip çalıştırılabiliyor, çıktısı syscall üzerinden görülüyor.

---

## Milestone 10 — Shell

### 10.1 — Temel Kabuk Programı
- [x] Klavye girdisini syscall üzerinden okuma
- [x] Komut satırı parse etme (basit tokenization)
- [x] Dahili komutlar: `ls`, `cd`, `cat`, `echo`, `pwd`

### 10.2 — Program Çalıştırma
- [x] `exec`/`fork` benzeri syscall'lar (ya da basitleştirilmiş bir `spawn` syscall'ı)
- [x] PATH benzeri bir mekanizma ile disk üzerindeki programları bulup çalıştırma

**Çıktı:** Kullanıcı, boot sonrası shell'e düşüp `ls` ile dosya sistemini gezebiliyor, `cat` ile dosya okuyabiliyor, disk üzerindeki başka bir ELF programını çalıştırabiliyor.

---

## Milestone 11 — GUI

> Bkz. `AI_RULES.md` §7 — kaynak verimliliği mimariye gömülü olacak.

### 11.0 — Ekran Çözünürlüğü
- [x] Milestone 0-4'te kullanılan varsayılan 1280x800 çözünürlüğünden, gerçek kullanım senaryosuna uygun daha yüksek bir çözünürlüğe (örn. 1920x1080) geçiş
- [x] Not: Çözünürlük zaten koda sabit kodlanmamış — Limine'in `framebuffer_request` yanıtından dinamik olarak okunuyor. Bu adım kod değişikliği değil, test ortamının (QEMU parametreleri) ve gerçek donanımda Limine'in sunduğu modun güncellenmesini gerektirir.

### 11.1 — Compositor Temeli
- [x] Damage-tracking (dirty-rect) render mimarisi — sadece değişen bölgeler yeniden çizilecek
- [x] Framebuffer üzerinde çift tamponlama (double buffering) — flicker önleme
- [x] Pencere yüzeyi (surface) soyutlaması
- [x] Wayland'in client-server/protokol ayrımından ilham alınacak (uygulamalar doğrudan ekrana yazmaz, her şey compositor üzerinden akar) — ancak Wayland'in kendisi hedeflenmeyecek; protokol basit ve tek-compositor'a özel tutulacak, ayrı compositor implementasyonları desteklenmeyecek

### 11.2 — Pencere Yöneticisi
- [x] Pencere oluşturma/taşıma/yeniden boyutlandırma
- [x] Z-order yönetimi (üst üste binen pencereler)
- [x] Fare (PS/2 mouse) sürücüsü ve imleç render'ı
- [x] **Varsayılan görünüm:** Klasik Windows 10 tarzı bir masaüstü deneyimi (alt görev çubuğu, başlat menüsü, sistem tepsisi, sağ üstte pencere kontrolleri) — kullanıcı hiçbir öğrenme eğrisi olmadan ilk açılışta nasıl kullanacağını zaten biliyor. Bu, 11.4'teki "hazır çalışan varsayılan config" ilkesinin görsel/deneyimsel karşılığı; kişiselleştirme sistemi bu varsayılanı değiştirmeye izin verir ama başlangıç noktası bu olur
- [x] **İkinci hazır görünüm seçeneği:** macOS'a benzeyen bir GNOME tarzı düzen (üstte menü çubuğu, ortada/alt merkezde dock) — kullanıcı kurulum sırasında veya ayarlar uygulamasından iki hazır görünüm (Windows 10 tarzı / macOS-esque GNOME tarzı) arasında seçim yapabilir. İkisi de tam çalışan, test edilmiş hazır paketler olacak; ileride topluluk temaları (11.5) bu ikisinin yanına eklenebilir

### 11.3 — Girdi Yönlendirme
- [x] Klavye/fare olaylarının odaklanmış pencereye yönlendirilmesi
- [x] Event queue mekanizması (userspace programlara olay iletimi)

### 11.4 — Kişiselleştirme Sistemi
- [x] Tema/config dosyalarının **veri** olarak ele alınması (her özelleştirme seçeneği için ayrı kod yolu değil — bkz. `AI_RULES.md` §7)
- [x] **Hazır ve çalışan varsayılan config** — kullanıcı sistemi ilk açtığında boş/kırık bir durumdan değil, test edilmiş, tam çalışan bir varsayılan temadan başlar (Hyprland deneyiminden ders: kullanıcıyı sıfırdan bir config yazmaya zorlamak büyük bir sürtünme kaynağı)
- [x] **Görsel bir ayarlar uygulaması** — kullanıcılar ayarları **yalnızca bu arayüz üzerinden** değiştirir, config dosyasını doğrudan elle düzenlemezler. Config dosyası tamamen dahili bir uygulama detayı olarak kalır (kullanıcıya hiç gösterilmesi/düzenlenmesi gerekmez). Hedef: Wayland'in mimari fikirlerinden (compositor merkezli, protokol tabanlı) ilham almak ama kullanıcı tarafında Wayland'in gerektirdiği geliştirici seviyesi karmaşıklığı olmadan
- [x] **Anlık doğrulama:** Arayüz zaten yalnızca geçerli seçenekleri sunduğu için (örn. bir renk seçici asla geçersiz bir değer üretemez), geçersiz bir config durumu mimari olarak oluşamaz — ayrı bir "config dosyasını doğrula" adımına gerek kalmaz
- [x] **Güvenli geri dönüş:** Yine de her zaman "varsayılana dön" ve "son çalışan ayara geri al" seçeneği bulunur — örn. bir dış kaynaktan (paylaşılan tema paketi, bkz. 11.5) içe aktarılan bir config bozuk/uyumsuz çıkarsa
- [x] Temel widget kütüphanesi (buton, metin kutusu, panel)

### 11.5 — Paylaşılabilir Tema Paketleri
- [x] Kullanıcının özelleştirdiği tema/ayar setini tek bir arşiv dosyasına (`.lixtheme` gibi) paketleyebilmesi ("Dışa Aktar" işlevi)
- [x] Başka bir kullanıcının bu dosyayı "İçe Aktar" ile sisteme yükleyip anında uygulayabilmesi
- [x] **Bağımlılık yönetimi:** Paket, kullandığı tüm varlıkları (özel font, ikon seti vb.) **kendi içinde taşır** — dışarıdan, kurulumda olmayabilecek bir kaynağa referans vermez. Eğer bir bağımlılık gerçekten pakete gömülemiyorsa (örn. büyük bir font ailesi), paket bunu açıkça listeler ve içe aktarma sırasında eksik olan bağımlılığı kullanıcıya bildirir — sessizce bozuk görünmez. Amaç, diğer ekosistemlerde (örn. Linux tema paylaşımlarında) sık görülen "bende çalışıyor, sende bozuk" sorununu baştan engellemek
- [x] **Güvenlik kısıtı (kritik):** Tema paket formatı yalnızca pasif veri içerecek — renk değerleri, boyutlar, resim/ikon dosyaları, fontlar. Hiçbir çalıştırılabilir kod, script veya makro içermeyecek. Bu, kötü amaçlı bir "tema"nın sistem üzerinde kod çalıştırmasını mimari olarak imkansız kılmak için zorunlu (bkz. `AI_RULES.md` §8 — güvenlik prensipleri)
- [x] Tema paketleri, Faz 3'teki genel paket/mağaza altyapısıyla aynı temel paketleme mekanizmasını kullanabilir (ayrı bir sistem gerekmeyebilir — bu, Faz 3 detaylandırılırken netleştirilecek)

**Çıktı:** Birden fazla pencere aynı anda ekranda gösterilebiliyor, taşınabiliyor, ve CPU kullanımı boşta (idle) düşük kalıyor (yalnızca değişen alanlar çiziliyor). Kullanıcı görsel ayarlar uygulaması üzerinden temayı özelleştirip dışa aktarabiliyor, bu dosya başka bir LixOS kurulumuna aktarılıp aynı görünümü üretebiliyor.

**Hedef (referans, kesin taahhüt değil):** Boşta sistem RAM kullanımı ~50-150 MB, CPU kullanımı ~%0-1 civarında kalması hedeflenir (modern hafif Linux masaüstü ortamlarıyla karşılaştırılabilir veya daha iyi).



---

## Milestone 12 — Network Stack

> Bkz. `AI_RULES.md` §8 — mahremiyet varsayılan olarak açık, telemetri yok.

### 12.1 — NIC Sürücüsü
- [x] PCI üzerinden ağ kartı tespiti (öncelik: QEMU'nun desteklediği yaygın bir kart, örn. e1000 veya virtio-net)
- [x] Paket gönderme/alma (TX/RX ring buffer yönetimi)

### 12.2 — Temel Protokol Katmanları
- [x] Ethernet frame parsing
- [x] ARP (adres çözümleme)
- [x] IPv4 (temel routing, checksum)
- [x] ICMP (ping desteği — test/doğrulama için pratik)
- [x] UDP
- [x] TCP (üç yönlü el sıkışma, temel pencere yönetimi — tam RFC uyumu hedeflenmez, çalışan bir temel yeterli)

### 12.3 — Soket Arayüzü
- [x] `socket`, `bind`, `connect`, `send`, `recv` syscall'ları
- [x] Userspace programların ağ erişimi

**Çıktı:** LixOS, QEMU'nun sanal ağı üzerinden host makineye ping atabiliyor; basit bir userspace programı ile TCP bağlantısı kurup veri gönderip alabiliyor. Hiçbir arka plan servisi kullanıcı onayı olmadan dışarıya veri göndermiyor.

---

## Faz 2 Sonunda Elde Edilecek Sistem

Faz 2 tamamlandığında LixOS:
- Disk üzerinde kalıcı, şifrelenebilir dosya sistemine sahip
- Birden fazla process'i güvenli şekilde izole edip zamanlayabilen
- Kullanıcının shell üzerinden etkileşime girebildiği
- Hafif, damage-tracking tabanlı bir GUI'si olan
- Temel ağ bağlantısı kurabilen

bir sistem haline gelmiş olacak — yani gerçek anlamda "kullanılabilir" bir hobi işletim sistemi. Bu noktadan sonrası (paket yöneticisi, ek dosya sistemi desteği — ext3/ext4, btrfs —, daha gelişmiş driver desteği, SMP, gerçek zamanlı dosya sistemi geliştirmeleri vb.) muhtemelen bir **Faz 3** gerektirecek.

## Kullanım Notu (AI Ajanı İçin)

- Bu roadmap, `AI_RULES.md` ile birlikte kullanılır; tüm kurallar (tek
  milestone/checkbox odaklı ilerleme, build+QEMU doğrulama, eski koda
  bakmama, kaynak verimliliği, güvenlik/mahremiyet prensipleri) burada da
  geçerlidir.
- Milestone'lar arası sıkı bağımlılık nedeniyle, bir milestone'un "Çıktı"
  kriteri karşılanmadan bir sonrakine **kesinlikle** geçilmeyecek — Faz 1'in
  aksine burada bir hata (örn. VFS'te bir bug), üzerine inşa edilen her şeyi
  (userspace, shell, GUI) etkileyebilir.
- Milestone 8 (şifreleme) ile Milestone 9 (userspace) sırası esnektir —
  ikisi de Milestone 7'ye bağımlı ama birbirine bağımlı değil; kullanıcı
  tercih ederse sırası değiştirilebilir.
