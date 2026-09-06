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
- [ ] Headless ana süreç RSS 151 MB (hedef <100): gerçek Node ikilisi paketle ya da Electron-as-Node tabanını ölç/azalt.
- [ ] Kalan Electron-only parçalar (Electron kipinde kalır, headless'ta shim): `FirmwareUpdateService` `net.request`, `appProtocol`, `ipc/utils` `screen`, `cluster:repaint-nudge`.
- [x] Oturum 5: `native/livi-ui` iskeleti (LVGL 9.5, Wayland/shm, FreeType Roboto, JSON-RPC istemcisi, nav rail + durum sayfası, dokunma iletimi); headless ana süreç ikiliyi `uiProcess.ts` ile başlatıyor; paket `resources/ui/`.
- [x] livi-ui: nav ikonları MUI outlined SVG'lerden PNG (`native/livi-ui/assets/icons`, `rsvg-convert` Pi'de); ARGB (saydam) yüzey yapıldı: video düzlemi arayüzün ALTINDA çizilir, `cmake/patch_lvgl.cmake` LVGL 9.5 Wayland sürücüsünü yamalar (XRGB varsayılanı + `WL_SHM_FORMAT_ARGB8888 == 0` hatası); LVGL'ye upstream düzeltme önerilebilir.
- [ ] Compositor `main UI toplevel gone -> shutting down` kuralı: livi-ui çökünce/yeniden başlayınca compositor da kapanıyor (Electron kiosk semantiği). LVGL kipinde UI toplevel'ın gidip gelmesine tolerans ver (uiProcess yeniden başlatması işe yarasın).
- [x] Oturum 6: parite aracı `tools/parity/` (Electron referansları `reference/electron-*.png`, `capture.sh`, `compare.mjs`, `run.sh`; `grim` cage'in `wayland-0` soketinde). Nav rail MUI ikonlarıyla (PNG, lodepng) ve Electron geometrisiyle; Camera boş sayfası kare farkı < %1.
- [ ] Parite notu: FreeType tam sayı ilerleme (advance) yuvarlaması yüzünden metinler Chrome'a göre birkaç px dar; saat için +1 px harf aralığıyla telafi edildi, genel çözüm (kesirli konumlama) bekliyor.
- [ ] livi-ui çoklu dokunma: LVGL Wayland sürücüsü tek parmak (`lv_wl_touch.c` ilk temas); pinch/zoom için wl_touch olaylarını ayrıca dinleyip `projection.ipc.sendMultiTouch` ile ilet (dokunmatik ekranlı Zero 2 W kurulumunda gerekli).
- [ ] Parite: Electron `startPage` yalnızca üst düzey rotaları kabul ediyor (`/settings/devices` → ana sayfa); Cihazlar sayfası referansı için Electron'da fare ile gezinip yakalamak gerekiyor (henüz yok).
- [x] Oturum 7: Projection (StatusOverlay nefes animasyonu, letterbox dokunma haritalama, ViewAreaMask) + Ayarlar çerçevesi (başlık/geri/kart) + Cihazlar listesi (pil, sinyal, durum, unut) + oturum değiştirme rozeti. Ayarlar kök sayfası parite: kare %1,8.
- [ ] Oturum 8–16: ekranlar (sıra ve kabul ölçütleri `LVGL_PLAN.md` §10).
- [ ] Oturum 17–18: kiosk paketi `v9.1.0`, Zero 2 W ölçümleri.

## Yedekler

- [x] 8.3.0 AppImage: Release `pi3-8.3.0` ve Pi'de `~/LIVI/LIVI-8.3.0.AppImage`.
- [x] SD kart imajı (8.3.0 hali): `~/Downloads/livi-pi3-8.3.0.img.gz`, harici diske kopyalanacak.
- [ ] 9.0 haliyle yeni bir SD imajı al (AP birimi ve cihaz kaydı dahil).
