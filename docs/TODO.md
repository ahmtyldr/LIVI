# Yapılacaklar

Durum: 5 Eylül 2026. Pi 3 B+ üzerinde LIVI 9.0.0 nightly çalışıyor.

## Kendi güncelleme kanalımız

- [x] Kiosk servisine `UPDATE_REPO=ahmtyldr/LIVI` ekle (`livi-kiosk.service.d/pi3.conf`), cihaz güncellemeleri bu depodan arasın.
- [x] `pi3-8.3.0` yayınını "ön sürüm" olarak işaretle, "latest" olarak sunulmasın.
- [x] Cihazdaki nightly AppImage'ı `v9.0.0-livilite.1` gibi bir yayın olarak koy; cihaz kendini güncel görsün.
- [x] GitHub Actions `build` iş akışını fork'ta bir kez çalıştırıp (linux_arm64) kendi AppImage'ımızı üret, Pi'de dene. (run 33965991047, 9 dk; Pi'de doğrulandı)
- [x] İlk kendi sürümümüzü yayınla (`v9.0.0-livilite.2`; bugün latest `v9.0.1`).
- [x] Pi'de Settings → System → Software Update ile uçtan uca doğrulandı: 9.0.0 → `v9.0.1` (5 Eylül 2026). Kurallar: etiket düz `vX.Y.Z`, cihazda Nightly anahtarı kapalı.
- [ ] Süreç notu: f-io'dan değişiklik alma → kendi düzeltmeleri → derle → Pi'de test → yayınla.

## Kaynağa taşınacak düzeltmeler (shim ve elle kurulumların yerine)

- [x] `native/livi-gst-video/rust/player/src/lib.rs`: çözücünün kabul listesinde olmayan her colorimetry değeri yakalanıyor (commit 30044c12; Pi 3'te shim olmadan doğrulandı).
- [x] Kurulum scriptleri `livi-wifi-ap.service` dosyasını yazıyor (commit b4160096; Pi'de doğrulandı). Sudoers gerekmedi: helperd root çalışıyor.
- Karar (5 Eylül 2026): f-io'ya PR gönderilmeyecek; bu depo bağımsız bir proje olarak ilerler. Upstream'den değişiklik alma isteğe bağlı ve elle yapılır.

## Açılış süresi (kaldığı yer: adım 3 tamam, arayüz 55 sn / 9.0'da 67 sn)

- [ ] Adım 4: açılmış AppImage'dan çalıştırma denemesi (`--appimage-extract`, ExecStart değişikliği), ölç.
- [ ] Adım 5: Electron'a `--disable-gpu` vererek üç başarısız GPU süreci denemesini atla, ölç.
- [ ] Adım 6: `wifiDedicatedInterface: true` ile erişim noktasını açılışta kaldır, telefon bağlanma süresini ölç.
- [ ] 9.0'ın arayüze 12 sn geç gelmesinin nedenini bul (ana süreç fazları).

## Hedef donanım: Raspberry Pi Zero 2 W

- [x] 2,4 GHz erişim noktası + Bluetooth görüşme testi Pi 3 B+ üzerinde yapıldı, sorunsuz (README).
- [ ] Zero 2 W edin; 9.0.1 imajını olduğu gibi dene (Electron'un 512 MB'a sığmaması bekleniyor; compositor, helperd, AP ve video hattı doğrulanır).
- [ ] Geliştirme erişimi: tek Wi-Fi AP olduğu için Mac'i LIVI'nin AP'sine bağlamak ya da OTG'den USB ağ (`g_ether`) kurmak.
- [ ] LVGL planında bellek bütçesi: toplam < 350 MB, her aşamada Zero 2 W'de ölçülür.

## LVGL (bkz. `LVGL_PLAN.md`)

- [x] Aşama 0.1: 9.0.0'ı Pi 3'te doğrula (nightly ile yapıldı, Android Auto donanım çözmeyle çalışıyor).
- [x] Aşama 0.2: derleme ortamı = GitHub Actions (arm64 runner), doğrulandı.
- [x] Oturum 1: sözleşme çıkarımı (`contracts/`, `pnpm contracts:gen`, CI kontrolü) — 70 çağrı, 11 olay, 116 config anahtarı, 155 ayar alanı, 268 çeviri anahtarı.
- [x] Oturum 2: UiBridge (JSON-RPC soket) + `tools/ui-cli` — Pi'de Electron çalışırken `settings.get`, `getDevices`, olay akışı doğrulandı (6 Eylül 2026). Not: Pi'deki AppImage köprülü test derlemesi (Actions run 33993733418), yayın etiketi henüz yok; bir sonraki yayın `v9.0.2` köprüyü içerecek.
- [x] Oturum 3 (1/2): `src/main/host/` — `UiHost` (diyalog, bağlantı açma, yayın, çıkış) ve yol/sürüm sorguları; 26 dosya Electron'a doğrudan dokunmuyor, `LIVI_UI=lvgl` soket host'u seçiyor. CI: 186 main + 149 renderer test dosyası yeşil (commit ca162c90).
- [x] Oturum 4 (2/2): `RendererTarget` + köprü renderer'ı, `app/bootstrap.ts` ortak başlangıç, `headless.ts` + `vite.headless.config.mts` + `electron-shim`, `scripts/pi3/livi-headless.sh`. Pi'de Electron'suz doğrulandı: AA oturumu, H.264 donanım çözme, video düzlemi; RAM 371 MB, CPU %4,7 (commit fc6b6fee ve sonrası).
- [x] Headless ana süreç gerçek Node ikilisine alındı (v9.1.3): AppImage `resources/node/node` (Node 22), `livi-headless.sh` node ile çalıştırıyor, `headless.js` `process.resourcesPath`'i `LIVI_RESOURCES`'ten kuruyor. Ana süreç RSS: boşta ~68 MB, AA aktifken ~101 MB (Electron-as-Node ~149 MB idi). Codec/helper/livi-ui/AA doğrulandı.
- [ ] Kozmetik: BT supervisor helper'ı önce `resources/app.asar/out/main/driver/livi-helperd`'de arayıp bulamıyor (log gürültüsü), sonra `resources/driver`/staged kopyaya düşüp çalışıyor. Node kipinde ilk yolu düzelt.
- [ ] Kalan Electron-only parçalar (Electron kipinde kalır, headless'ta shim): `FirmwareUpdateService` `net.request`, `appProtocol`, `ipc/utils` `screen`, `cluster:repaint-nudge`.
- [x] Oturum 5: `native/livi-ui` iskeleti (LVGL 9.5, Wayland/shm, FreeType Roboto, JSON-RPC istemcisi, nav rail + durum sayfası, dokunma iletimi); headless ana süreç ikiliyi `uiProcess.ts` ile başlatıyor; paket `resources/ui/`.
- [x] livi-ui: nav ikonları MUI outlined SVG'lerden PNG (`native/livi-ui/assets/icons`, `rsvg-convert` Pi'de); ARGB (saydam) yüzey yapıldı: video düzlemi arayüzün ALTINDA çizilir, `cmake/patch_lvgl.cmake` LVGL 9.5 Wayland sürücüsünü yamalar (XRGB varsayılanı + `WL_SHM_FORMAT_ARGB8888 == 0` hatası); LVGL'ye upstream düzeltme önerilebilir.
- [ ] Compositor `main UI toplevel gone -> shutting down` kuralı: livi-ui çökünce/yeniden başlayınca compositor da kapanıyor (Electron kiosk semantiği). LVGL kipinde UI toplevel'ın gidip gelmesine tolerans ver (uiProcess yeniden başlatması işe yarasın).
- [x] Oturum 6: parite aracı `tools/parity/` (Electron referansları `reference/electron-*.png`, `capture.sh`, `compare.mjs`, `run.sh`; `grim` cage'in `wayland-0` soketinde). Nav rail MUI ikonlarıyla (PNG, lodepng) ve Electron geometrisiyle; Camera boş sayfası kare farkı < %1.
- [ ] Parite notu: FreeType tam sayı ilerleme (advance) yuvarlaması yüzünden metinler Chrome'a göre birkaç px dar; saat için +1 px harf aralığıyla telafi edildi, genel çözüm (kesirli konumlama) bekliyor.
- [ ] livi-ui çoklu dokunma: LVGL Wayland sürücüsü tek parmak (`lv_wl_touch.c` ilk temas); pinch/zoom için wl_touch olaylarını ayrıca dinleyip `projection.ipc.sendMultiTouch` ile ilet (dokunmatik ekranlı Zero 2 W kurulumunda gerekli).
- [ ] Parite: Electron `startPage` yalnızca üst düzey rotaları kabul ediyor (`/settings/devices` → ana sayfa); Cihazlar sayfası referansı için Electron'da fare ile gezinip yakalamak gerekiyor (henüz yok).
- [x] Oturum 7: Projection (StatusOverlay nefes animasyonu, letterbox dokunma haritalama, ViewAreaMask) + Ayarlar çerçevesi (başlık/geri/kart) + Cihazlar listesi (pil, sinyal, durum, unut) + oturum değiştirme rozeti. Ayarlar kök sayfası parite: kare %1,8.
- [x] Oturum 8: Settings şemadan üretilen gruplar (`contracts/settings-schema.json` ağacı → `out/ui/settings-schema.json`). Rota gezinme yığını, onay kutusu (switch+kaydet), seçim (alt sayfa radio), kaydırıcı (% 0-100), sayı (adımlayıcı min/max/step); renk (renk örneği, salt-okunur), metin/keybinding (salt-okunur). ~90 MUI ikonu PNG.
- [ ] livi-ui Settings kalanı: string düzenleme (ekran klavyesi gerek), renk seçici, 35 tuş bağlama editörü, 11 custom bileşen (About, SoftwareUpdate, Restart, PowerOff, Camera kalibrasyon vb.), dinamik select seçenekleri (`loadOptions`: audio cihazları, wifi kanalı — köprüden yükle). disabled `$expr` değerlendirilmiyor.
- [x] Oturum 10: Media (çalınıyor) — kapak (base64 JPEG/PNG, TJPGD/lodepng), başlık/sanatçı/albüm, ilerleme çubuğu (yerel saat ile ilerler), prev/play-pause/next (`sendCommand` 'prev'/'play'/'pause'/'next'). Türkçe/genişletilmiş latin için FreeType latin-ext geri-dönüşü. Canlı podcast'le doğrulandı.
- [x] Oturum 11: Media FFT görselleştirici — `fft.c` (radix-2, 4096, 24 çubuk, Hamming, dB -80; fft.ts portu), kapak↔spektrum geçişi (kapağa dokun / `LIVI_UI_CTL fft`), PCM halka tamponu (mono int16→float), `setVisualizerEnabled`. UI doğrulandı (geçiş + 24 çubuk, çökme yok).
- [ ] livi-ui FFT uçtan uca doğrulanamadı: **kablosuz AA sesi gst-host'tan geçmiyor** (gst-host'ta yalnızca video feed 0x7a000001; AA sesi ProjectionAudio→sistem sink). gst-host görselleştirici tap'i (frame 15) yalnızca kendi ses akışlarını yakalar → AA'da chunk üretmiyor. CarPlay dongle / Node-beslemeli akışta çalışması beklenir. Electron'da da aynı sınırlama olabilir; kontrol edilecek.
- [ ] Oturum 9/15 ertelendi: Camera (Pi'de kamera yok; USB kamera takılınca yap).
- [x] Telemetry sekmesi kullanıcı isteğiyle KALDIRILDI (Oturum 12–14 iptal).
- [ ] Oturum 15 (Camera): Pi'de kamera yok, ertelendi.
- [ ] Oturum 16 (Dash/Aux): ikinci HDMI ekran gerekiyor.
- [x] Oturum 17: kiosk paketi. `v9.1.0` yayınlandı (AppImage'da `resources/ui/livi-ui` + font/ikon/locale/şema). Pi'de paketten kuruldu: üretim drop-in `scripts/pi3/headless-prod.conf` (dev override YOK), livi-ui `resources/ui`'den çalışıyor (RSS 13 MB), güncelleyici v9.1.0'ı görüp 'Up to date' gösteriyor.
- [ ] Oturum 18 (Zero 2 W): hedef donanımda ölçüm. (sıra ve kabul ölçütleri `LVGL_PLAN.md` §10).
- [ ] Oturum 17–18: kiosk paketi `v9.1.0`, Zero 2 W ölçümleri.

## Yedekler

- [x] 8.3.0 AppImage: Release `pi3-8.3.0` ve Pi'de `~/LIVI/LIVI-8.3.0.AppImage`.
- [x] SD kart imajı (8.3.0 hali): `~/Downloads/livi-pi3-8.3.0.img.gz`, harici diske kopyalanacak.
- [ ] 9.0 haliyle yeni bir SD imajı al (AP birimi ve cihaz kaydı dahil).
