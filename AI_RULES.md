# AI_RULES.md — LixOS Geliştirme Ajanı Kuralları

Bu dosya, bu projede çalışan her AI ajanının (Antigravity/agy, Claude, vb.) her
sohbet/oturum başında okuması ve uyması gereken kuralları tanımlar. Amaç:
oturumlar arası bağlamı korumak, projeyi sıfırdan taramayı önlemek ve ilerlemeyi
tutarlı, denetlenebilir şekilde kayıt altına almak.

---

## 1. Oturum Başlangıcında Yapılacaklar

Her yeni sohbet/oturum başında, kod tabanını taramadan veya kullanıcıya proje
hakkında soru sormadan ÖNCE:

1. `ROADMAP.md` dosyasını oku — genel plan ve hangi milestone'ların
   tamamlandığını (✅/⏳ işaretleriyle) gösterir.
2. `süreç.md` dosyasını oku — tamamlanan milestone'ların **nasıl**
   tamamlandığına dair teknik detayları, alınan mimari kararları ve
   doğrulama kanıtlarını içerir.
3. Bu iki dosyadan edinilen bağlamla devam et. Sadece süreç.md'de
   bulunmayan, kod detayına inen bir soru geldiğinde ilgili kaynak dosyalarını
   aç.

Bu iki dosya birlikte "nerede kaldık" sorusunu tam olarak cevaplamalı;
kullanıcıya "hangi milestone'dayız" gibi sorular sorulmamalı.

---

## 2. ROADMAP.md — Plan ve Durum Takibi

- Her milestone/alt-milestone bir satır/madde olarak listelenir.
- Durum işaretleri: `✅ TAMAMLANDI`, `⏳ Planlandı`, `⏳ BEKLEMEDE (Onay Sonrası)`,
  `🔧 Devam Ediyor`.
- ROADMAP.md **sadece durumu** gösterir, açıklama içermez — detay süreç.md'ye
  yazılır. Bu ayrım korunmalı, ROADMAP.md şişirilmemeli.
- Bir milestone tamamlandığında ajan bu dosyadaki ilgili satırı günceller
  (⏳ → ✅) VE aynı anda süreç.md'ye ilgili bölümü ekler (bkz. Bölüm 3).

---

## 3. süreç.md — Ayrıntılı İlerleme Günlüğü

Bir milestone veya alt-görev tamamlandığında, ajan süreç.md'ye aşağıdaki
formatta yeni bir bölüm ekler:

```
### Milestone X.Y: <Başlık>
- **<Alt başlık / bileşen adı>:**
  - Ne yapıldı, hangi teknik yaklaşım/algoritma kullanıldı.
  - Hangi mimari kararlar alındı ve neden.
  - Hangi standart/spesifikasyona uyuldu (varsa: FIPS, RFC, IEEE vb.).
- **Doğrulama:** Nasıl test edildi (örn. NIST test vektörleri, QEMU + seri
  port, WSL debugfs/hexdump ile host tarafı kontrol, belirli syscall
  senaryoları).
```

Kurallar:
- Her giriş **kısa ve teknik** olmalı — düz nesir değil, madde işaretli.
- Kod satırlarını veya tam implementasyonu buraya kopyalama; sadece
  "ne yapıldı ve nasıl doğrulandı" seviyesinde özet.
- Doğrulama adımı asla atlanmamalı — "nasıl test edildi" her zaman belirtilir.
- **QEMU üzerinde çalışan her test için doğrulama iki parçalıdır: hem
  serial log (`build/serial.log`) hem de ekran görüntüsü
  (`run_test.ps1` script'inin ürettiği `build/screen.ppm` ve otomatik olarak PNG'ye dönüştürülen `build/screen.png`). Ajan ikisini
  de kontrol etmeden ve süreç.md girişinde ikisine de değinmeden
  milestone'ı tamamlanmış sayamaz.** Sadece log kontrolü yeterli değildir
  — ekran çıktısının (framebuffer/konsol) beklenen görsel durumda olduğu
  ayrıca teyit edilmelidir.
- Dosyanın sonunda güncel bir **"📊 Mevcut Durum Tablosu"** ve bir
  **"🎯 Sıradaki Adım"** bölümü tutulur; her güncellemede bu iki bölüm de
  yenilenir.
- süreç.md büyüdükçe (birkaç fazı aştığında) eski fazlar kısa bir özet
  paragrafına indirilip yalnızca aktif fazın detayları tam olarak tutulur
  (örn. Faz 1 tek bir özet paragrafına indirilirken, aktif olan Faz 2'nin
  milestone'ları tam ayrıntılı tutulur).

---

## 4. Ne Zaman Güncelleme Yapılır

- Bir milestone/alt-milestone tamamen bittiğinde (kod çalışıyor + test edildi).
- Önemli bir mimari karar alındığında (örneğin bir veri yapısı, protokol veya
  yaklaşım değişikliği), milestone henüz bitmemiş olsa bile kısa bir not
  süreç.md'nin ilgili milestone bölümüne eklenir.
- Kullanıcı açıkça "bunu kaydet" demese bile, ajan bu güncellemeleri
  **kendiliğinden** yapar — onay veya hatırlatma beklemez.

## 5. Ne Zaman Güncelleme YAPILMAZ

- Denenip başarısız olan ara adımlar, geri alınan denemeler.
- Küçük stil/isimlendirme değişiklikleri, refactor'lar (milestone'ın özünü
  değiştirmeyen).
- Henüz doğrulanmamış (test edilmemiş) iş — önce doğrulama, sonra kayıt.

---

## 6. Dosya Konumları

- `ROADMAP.md` — proje kök dizininde.
- `süreç.md` — proje kök dizininde, ROADMAP.md ile aynı seviyede.

Bu iki dosya git'e commit edilir; geçmiş, versiyon kontrolü üzerinden
izlenebilir olmalıdır.
