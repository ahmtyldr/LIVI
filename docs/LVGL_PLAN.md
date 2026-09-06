# LIVI arayüzünü LVGL ile yeniden yazma planı

Hedef: Electron/React arayüzünün yerine LVGL ile yazılmış, görünüşü ve davranışı
birebir aynı bir arayüz. Android Auto, CarPlay, ayarlar, medya, telemetri ve
çoklu ekran korunur. Geçiş boyunca Electron arayüzü çalışır kalır; LVGL bir
anahtarla devreye girer, sorun çıkarsa aynı anahtarla geri dönülür.

Bu plan 4 Eylül 2026'da `main` (9.0.0, commit `fb244e72`) üzerinde çıkarılan
envantere dayanır.

## 1. Neyin değiştiği, neyin kaldığı

LIVI üç katmandan oluşur. Sadece en üstteki değişir.

| Katman | Boyut | Durum |
|---|---|---|
| Native (Rust): `livi-helperd` (iAP2/MFi/AA), `livi-gst-video`, `livi-compositor`, `livi-crypto` | ~20.000 satır | Aynen kalır |
| Ana süreç (Node.js): USB, oturumlar, CarPlay/AA sürücüleri, config, gst-host ve compositor kontrolü | ~35.000 satır, 202 dosya | Kalır; Electron'dan ayrıştırılır |
| Arayüz (React + MUI) | ~19.000 satır, 81 bileşen | LVGL ile yeniden yazılır |

Android ve iOS desteği ana süreç ile native katmanda yaşar; arayüz sadece
dokunma gönderir ve olayları gösterir. Bu yüzden telefon desteğini kaybetme
riski yoktur, yeter ki ana süreç Electron olmadan çalışabilsin.

## 2. Arayüz ile ana süreç arasındaki sözleşme

Arayüzün ana süreçle tek konuşma yolu `src/preload/index.ts`. Bu dosya LVGL
için de sözleşmedir:

- **70 çağrı** (48 `invoke`, 8 `send`, 14 yerel dinleyici; arayüz → ana süreç): `settings.get/save`, `ipc.start/stop/
  restart`, `setVisible`, `sendTouch`, `sendMultiTouch`, `sendCommand`,
  `getDevices/selectDevice/cycleSession/forgetDevice`, Bluetooth eşleşme,
  dongle firmware, ses cihazları, `setVolume`, `requestCluster`, telemetri
  anlık görüntüsü, Wi-Fi/BT adaptör listeleri, güncelleme, `quit/restart`.
- **11 olay** (ana süreç → arayüz): `usb-event`, `projection-event`,
  `projection-audio-chunk` (görselleştirici için PCM), `cluster-video-resolution`,
  `telemetry:update`, `app:media-key`, `settings`, `update:event`, `update:progress`,
  `usb-reset-start`, `usb-reset-done`.

Video arayüzden geçmez. Compositor, `videocfg/videoshow/claim/backdrop/gamma`
komutlarıyla video düzlemini kendisi yerleştirir; arayüz projeksiyon
sayfasında sadece şeffaf alan bırakır ve dokunmayı iletir. LVGL için bu büyük
kolaylık: video, kamera ve renk kalibrasyonu için tek satır çizim kodu gerekmez.

## 3. Ekran envanteri

| Sayfa | İçerik | LVGL karşılığı | Zorluk |
|---|---|---|---|
| Projection | Şeffaf alan, dokunma/çoklu dokunma, oturum değiştirme kaplaması | Şeffaf ekran + `lv_indev` dokunma iletimi | Düşük |
| Devices | Cihaz listesi, pil/sinyal, slot rozetleri | `lv_list` + özel satır | Düşük |
| Settings | 5 grup, şemadan üretilen 155 alan: 35 tuş bağlama, 34 sayı, 39 onay kutusu, 16 seçim, 7 kaydırıcı, 6 renk, 5 metin, 11 özel, 1 cihaz listesi, 1 sıralı liste | Şema yorumlayıcı + 12 satır bileşeni | Orta |
| Media | Metadata, albüm kapağı, kontroller, FFT görselleştirici | `lv_image` + `lv_canvas` | Orta |
| Telemetry | 4 dashboard, 21 widget (hız, devir, yakıt, sıcaklık, telltale, navigasyon) | `lv_arc`, `lv_bar`, `lv_canvas` | Yüksek |
| Camera | Geri görüş kamerası (`getUserMedia`) | GStreamer `v4l2src` → compositor düzlemi (video ile aynı yol) | Orta |
| Custom | Kullanıcının web sayfası (`iframe`) | **Karşılığı yok**, karar gerekir (bkz. §8) | — |
| Dash / Aux pencereleri | Ayrı ekranlarda dashboard, kamera, medya | LVGL çoklu ekran, her biri ayrı Wayland yüzeyi | Orta |

Ortak: nav rail, düzen, tema (`primaryColor*`, `highlightColor*`,
`backgroundColor*`, gece modu), 268 çeviri anahtarı × 4 dil, `uiZoomPercent`.

## 4. Mimari

```
                 ┌──────────────── livi-compositor (Wayland, Rust) ────────────────┐
                 │   video düzlemi   │  kamera düzlemi  │   arayüz yüzeyi (şeffaf) │
                 └───────────────────┴──────────────────┴────────────┬─────────────┘
                                                                     │ Wayland
  gst-host ◄── unix socket ──┐                                ┌──────┴──────┐
  helperd  ◄── unix socket ──┤   Node ana süreci (headless)   │  livi-ui    │
  usb, config, oturumlar     │   UiBridge: JSON-RPC 2.0       │  (LVGL 9, C)│
                             └─────── /run/user/1000/livi-ui.sock ──────────┘
```

- **UiBridge**: ana süreçte, preload'daki 44 çağrı ve 6 olayı bire bir taşıyan
  bir Unix soket sunucusu. Satır sonu ayrılmış JSON-RPC 2.0; PCM parçaları
  ikili çerçeve olarak aynı sokette. Electron modunda bu köprü de açık kalır,
  böylece iki arayüz aynı anda aynı ana sürece bağlanıp karşılaştırılabilir.
- **Ana süreç iki kipte çalışır**: `LIVI_UI=electron` (bugünkü) ve
  `LIVI_UI=lvgl` (BrowserWindow yok, düz Node 24). Native eklentiler N-API
  olduğu için her iki ABI'de de çalışır.
- **livi-ui**: LVGL 9.x, C, tek binary. Wayland istemcisi (LVGL'nin resmi
  Wayland sürücüsü + libxkbcommon), dokunma ve fare Wayland'dan gelir.
  Sadece köprü soketiyle konuşur; USB, Bluetooth, GStreamer bilmez.

## 5. Aşamalar

Her aşamanın çıkışında Electron arayüzü hâlâ çalışır ve testler yeşildir.

### Aşama 0 — Zemin (2 hafta)
1. 9.0.0'ı arm64 için derleyip Pi 3'te Electron ile çalıştır. Bugün Pi'de
   8.3.0 var; kaynak ise 9.0.0. `scripts/pi3/livi-colorimetry-shim.c`'nin
   yaptığı düzeltmeyi `native/livi-gst-video/rust/player/src/lib.rs` içindeki
   `BAD_COLORIMETRY` eşleşmesini genelleştirerek kaynağa taşı.
2. Derleme ortamı: Mac'te Lima ile arm64 Debian 13 VM (Apple Silicon'da
   native hızda). Pi 3'te derleme yapılmaz (1 GB RAM).
3. Sözleşme çıkarımı (6 Eylül 2026'da tamamlandı): preload'dan JSON-RPC şeması,
   `Config.ts`'ten config şeması, `routes/schemas/*.ts`'ten ayar şeması, `locales/*.json`. Bunlar
   `contracts/` altında üretilen dosyalar olur; iki arayüz de buradan beslenir.
   Şema tek kaynak olduğu için Electron'da eklenen bir ayar LVGL'de de
   otomatik çıkar.

### Aşama 1 — Ana süreci Electron'dan ayır (3–4 hafta)
Electron'a dokunan 42 dosya, kullanım sayısına göre:

| API | Dosya | Yerine |
|---|---|---|
| `app` (getPath, on, quit) | 40 | `Paths`/`Lifecycle` servisi |
| `BrowserWindow`, `webContents.send` | 16 | `UiHost` arayüzü: `ElectronUiHost` + `SocketUiHost` |
| `dialog` | 10 | Köprüde `ui:dialog` olayı; LVGL kendi diyaloğunu çizer |
| `shell`, `screen`, `session`, `net`, `protocol` | 15 | Küçük sarmalayıcılar; ekran bilgisi compositor'dan |

Çıkış kriteri: `LIVI_UI=lvgl node out/main/main.js` Pi'de açılır, USB'yi
tarar, erişim noktasını kaldırır, telefonu bağlar ve video düzlemi ekrana
gelir (arayüz henüz yokken bile, compositor sayesinde).

### Aşama 2 — livi-ui iskeleti (3 hafta)
- CMake projesi, LVGL 9 alt modül, Wayland sürücüsü, `lv_conf.h`.
- Köprü istemcisi, yeniden bağlanma, olay dağıtımı.
- Tema: config'deki renkler ve gece modu → LVGL stil seti. Yazı tipi Roboto
  (Electron'da da Roboto; MUI'nin kullandığı 14/16/20/24 px boyutları).
- Nav rail, `Layout`, `SettingsLayout`, sayfa geçişleri, otomatik gizlenen
  nav (`useAutoHideNav`), `uiZoomPercent` ölçekleme.
- Çeviri yükleyici (aynı JSON dosyaları).
- **Parite aracı**: Electron ve LVGL'yi aynı ana sürece bağlayıp aynı
  ekranı iki compositor çıktısında yakalayan script (`grim`), piksel farkını
  raporlar. "Birebir aynı" iddiası her ekranda bununla doğrulanır.

### Aşama 3 — Ekranlar (8–10 hafta)
Sıra, risk ve bağımlılığa göre:

| Hafta | Ekran | Not |
|---|---|---|
| 1 | Projection + Devices | Bunlarla telefon uçtan uca kullanılabilir olur |
| 2–3 | Settings | Şema yorumlayıcı + 12 satır bileşeni; 5 grup, 190 alan |
| 4–5 | Media | Görselleştirici için PCM → FFT (renderer'daki `fft.ts` C'ye taşınır) |
| 6–8 | Telemetry | 21 widget; gauge'ler `lv_arc`/`lv_canvas`; 4 dashboard düzeni |
| 9 | Camera | `v4l2src ! v4l2convert ! waylandsink`, mevcut video hattının kopyası |
| 10 | Tampon | Parite farklarını kapatma |

### Aşama 4 — Dash ve Aux ekranları (1–2 hafta)
`secondaryWindows.ts`'teki rol/atama mantığı köprüye taşınır; livi-ui her
rol için ayrı bir LVGL ekranı ve Wayland yüzeyi açar, compositor'ın `screen`
komutuyla fiziksel ekrana bağlanır.

### Aşama 5 — Kiosk ve dağıtım (1 hafta)
- `livi-kiosk.service`: cage → livi-compositor → node ana süreci + livi-ui.
- AppImage yerine düz dizin: `LIVI/main.js`, `LIVI/livi-ui`, native eklentiler.
- Açılış ölçümü: Pi OS üzerinde hedef, güçten arayüze ≈ 25 sn (bugün 55).
  Buildroot ile ≈ 10 sn; Buildroot bu planın dışında ayrı bir adım.

### Aşama 6 — Geçiş
İki arayüz iki ay boyunca birlikte kalır. Parite aracı temiz, kullanıcı
raporu yoksa Electron renderer'ı ve `vite`/`react` bağımlılıkları kaldırılır.

## 6. Süre ve kaynak

Tek deneyimli geliştirici, tam zamanlı: **yaklaşık 5 ay**. LVGL bilen ikinci
bir kişiyle ekranlar paralel yürür ve süre 3 aya iner. Aşama 0–1 (ana süreç
ayrıştırma) LVGL bilgisi gerektirmez, TypeScript işidir; Aşama 2–4 C/LVGL.

## 7. Kazanç

| | Electron (bugün) | LVGL (hedef) |
|---|---|---|
| Arayüz RAM | ~450 MB (iki süreç) | ~30 MB |
| Boşta CPU | ~%50 | <%5 |
| Güçten arayüze (Pi OS) | 55 sn | ~25 sn |
| Güçten arayüze (Buildroot) | — | ~10 sn |
| GPU gereksinimi | ES 3 (Pi 3'te yazılımsal) | ES 2 / yazılımsal yeterli |

## 8. Kararlar ve riskler

1. **Custom sayfası.** LVGL web sayfası gösteremez. Seçenekler: (a) LVGL
   modunda sayfayı kaldırmak, (b) küçük bir WebKitGTK süreci ile ayrı yüzeyde
   göstermek (RAM +150 MB, açılışta değil istek anında başlar). Öneri: (a) ile
   başla, talep gelirse (b).
2. **"Birebir" tanımı.** Yazı tipi rasterizasyonu ve MUI animasyon eğrileri
   piksel düzeyinde aynı olmaz. Hedef: aynı boyut, aynı renk, aynı yerleşim,
   aynı davranış; parite aracı %1 altı fark eşiğiyle çalışır.
3. **Sürüm tabanı.** Plan 9.0.0 üzerinde. Pi'de çalışan 8.3.0 yalnızca
   bugünkü referanstır; Aşama 0'da 9.0.0'ın Pi 3'te doğrulanması şarttır.
4. **Dokunma.** cage → livi-compositor → Wayland touch → LVGL. Çoklu dokunma
   LVGL 9'da tek `indev` ile sınırlı; `sendMultiTouch` için Wayland ham
   olaylarını doğrudan köprüye iletmek gerekir (küçük özel kod).
5. **Ana süreç bellek.** Node ana süreci Pi 3'te ~150 MB kalır; toplam sistem
   bugünkü 570 MB'tan ~250 MB'a iner.
6. **Upstream takibi.** f-io/LIVI gelişmeye devam ediyor. Köprü ve şema
   çıkarımı upstream'e PR olarak sunulursa çatal bakımı kolaylaşır; aksi halde
   her upstream değişikliğinde ana süreç ayrıştırması yeniden birleştirilir.

## 9. İlk somut adımlar

1. Lima VM'de arm64 derleme ortamı kur, 9.0.0 AppImage üret.
2. `player/src/lib.rs`'te colorimetry düzeltmesini genelleştir, Pi 3'te 9.0.0'ı
   Electron ile doğrula (Android Auto + kablolu CarPlay yolu).
3. `src/main/ui-bridge/` altında JSON-RPC sunucusunu yaz, preload'daki 44
   çağrıyı tek tek bağla, Electron modunda çalıştığını testlerle göster.
4. `native/livi-ui/` iskeleti: LVGL 9 + Wayland, köprüye bağlanıp
   `settings.get` sonucunu ekrana yazan "merhaba" sürümü.

Bu dördü bittiğinde plan somutlaşmış olur; süre tahmini o noktada
güncellenir.

---

## 10. Uygulama sırası (6 Eylül 2026'dan itibaren)

Hedef donanım **Raspberry Pi Zero 2 W** (512 MB RAM, 2,4 GHz Wi-Fi). Kabul ölçütü her adımda
aynı: Pi 3 B+ üzerinde Electron arayüzü çalışmaya devam eder; Zero 2 W bütçesi toplam
**< 350 MB RAM** ve boşta **< %10 CPU**.

Bugün hazır olanlar: fork'un kendi derleme ve yayın hattı (Actions, `vX.Y.Z`), 9.0.1 Pi 3'te
doğrulanmış, Pi 3 düzeltmeleri kaynakta, 2,4 GHz + Bluetooth testi geçti.

| # | Oturum | Yapılacak | Kabul ölçütü | Sizden |
|---|---|---|---|---|
| 1 | Sözleşme çıkarımı | `contracts/` altına üretilen dosyalar: preload'daki 70 çağrı + 11 olay (JSON), `Config.ts` (116 anahtar), ayar şemaları (155 alan), çeviriler. `pnpm contracts:gen` + CI'da güncellik kontrolü. | Üretilen dosyalar depoda, CI yeşil. | Yok |
| 2 | UiBridge | Ana süreçte Unix soket JSON-RPC 2.0 sunucusu; 44 çağrı bire bir, 6 olay yayın, PCM ikili çerçeve. Küçük bir komut satırı istemcisi (`tools/ui-cli`). Electron modunda da açık. | Pi'de Electron çalışırken `ui-cli settings.get` ve `ui-cli ipc.getDevices` doğru cevap verir. | Yok |
| 3 | Ana süreci ayır (1/2) | `UiHost` arayüzü; `app.getPath`, `dialog`, `shell`, `screen` sarmalayıcıları; 42 dosyanın Electron importları arayüz üzerinden. Electron modu değişmez. | Tüm mevcut testler yeşil, Pi'de Electron arayüzü aynen çalışır. | Pi'de kısa bir kullanım kontrolü |
| 4 | Ana süreci ayır (2/2) | `LIVI_UI=lvgl`: BrowserWindow yok, düz Node 24. Headless paket: node binary + `out/main` + N-API eklentiler + helperd/compositor/gst-host; Actions'a ikinci artifact. | Pi 3'te headless ana süreç: USB tarar, AP kaldırır, telefon bağlanır, video düzlemi ekrana gelir; ana süreç RSS < 100 MB. | Telefonu bağlayıp ekrana bakmak |
| 5 ✅ | livi-ui iskeleti | `native/livi-ui`: CMake, LVGL 9.5, Wayland sürücüsü, köprü istemcisi, tema, Roboto, nav rail, düzen, çeviri yükleyici. Actions'ta derleme (`build-native.mjs` → `resources/ui`). | "Merhaba" sürümü compositor altında açılır, `settings.get` sonucunu gösterir, RSS < 40 MB. | Yok |
| 6 ✅ | Parite aracı | Aynı ana sürece bağlı iki arayüzün ekran görüntüsünü alıp farkı raporlayan script (`grim`). | Nav rail ve boş sayfa için fark < %1. | Yok |
| 7 | Projection + Devices | Şeffaf projeksiyon alanı, dokunma/çoklu dokunma iletimi, oturum kaplaması, cihaz listesi. | Zero 2 W bütçesi içinde telefon uçtan uca kullanılabilir. | Telefonla kullanım testi |
| 8–9 | Settings | Şema yorumlayıcı + 12 satır bileşeni (anahtar, sayı, seçim, kaydırıcı, renk, metin, tuş bağlama, cihaz satırı, liste…). | 5 grup, 190 alan parite < %1; ayar değişiklikleri çift yönlü. | Ayar sayfalarını gezip fark bildirmek |
| 10–11 | Media | Metadata, kapak, kontroller, FFT görselleştirici (C'ye taşınan `fft.ts`). | Parite < %1, PCM görselleştirici 30 fps'te < %5 CPU. | Müzik çalıp bakmak |
| 12–14 | Telemetry | 21 widget, 4 dashboard; `telemetry-sim` ile beslenir. | Parite < %1, dashboard geçişleri akıcı. | Dashboard'ları karşılaştırmak |
| 15 | Camera | `v4l2src` → compositor düzlemi, video hattının kopyası. | USB kamera görüntüsü, tam ekran ve pencere içinde. | USB kamera takmak |
| 16 | Dash / Aux | Rol ve atama mantığı köprüde, her rol ayrı LVGL ekranı. | İkinci HDMI ekranda dashboard. | İkinci ekran (varsa) |
| 17 | Kiosk ve paket | `livi-kiosk.service`: cage → compositor → headless main + livi-ui. Yayın: `v9.1.0`. | Pi 3'te güçten arayüze < 30 sn. | Pi'de kullanım |
| 18 | Zero 2 W | Aynı paketi Zero 2 W'ye kur; bellek, sıcaklık, açılış ölçümleri. | Toplam RAM < 350 MB, AA görüşme + harita testi geçer. | Zero 2 W ve micro-HDMI/OTG kabloları |
| — | Geçiş | Electron arayüzü iki ay yedek olarak kalır, sonra kaldırılır. | Kullanıcı raporu yok, parite temiz. | Karar |

Süre: 18 oturum, günde bir oturumla yaklaşık 4 hafta; parite cilası ve bilinmeyenler için +1–2 hafta.

### Şimdi verilmesi gereken kararlar (varsayılanlar parantezde)
1. **Custom sayfası:** LVGL modunda yok (varsayılan). İstenirse WebKitGTK ile ayrı yüzey, +150 MB; Zero 2 W'de yer yok.
2. **Parite eşiği:** ekran başına piksel farkı %1 (varsayılan). Yazı tipi kenar yumuşatması ve animasyon eğrileri bu payın içinde.
3. **Dil:** C ve LVGL 9.x (varsayılan). Rust bağlamaları geride, riske girilmez.
4. **Geliştirme erişimi Zero 2 W'de:** OTG portundan USB ağ (`g_ether`, varsayılan) ya da Mac'i LIVI'nin erişim noktasına bağlamak.

### İlk oturumun (1) somut çıktıları
- `contracts/ui-api.json`: 70 çağrı ve 11 olayın adları, parametre ve dönüş tipleri (preload + `env.d.ts`'ten).
- `contracts/config.schema.json`: `Config.ts`'ten JSON Schema.
- `contracts/settings-schema.json`: `routes/schemas/*.ts`'ten düz veri.
- `contracts/locales/*.json`: mevcut dosyaların kopyası değil, sembolik bağ ya da üretim adımı.
- `scripts/contracts-gen.ts` ve `pnpm contracts:gen`, typecheck iş akışında "üretilen dosyalar güncel mi" adımı.
