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
- [ ] Oturum 4 (2/2): kalan Electron kullanımları — pencere yönetimi (`window/*`, `app/*`, `index.ts`), `protocol/appProtocol.ts`, `FirmwareUpdateService` `net.request`, `ProjectionService`/`cluster.ts`/`GstVideo.ts` pencere kimliği (`webContents.fromId`, `BrowserWindow.fromWebContents`), `liviDashAdapter`, `ipc/utils.ts` `screen`; ardından `LIVI_UI=lvgl` headless paket (düz Node 24) ve Pi'de Electron'suz çalıştırma.
- [ ] Oturum 5–6: `native/livi-ui` iskeleti + parite aracı.
- [ ] Oturum 7–16: ekranlar (sıra ve kabul ölçütleri `LVGL_PLAN.md` §10).
- [ ] Oturum 17–18: kiosk paketi `v9.1.0`, Zero 2 W ölçümleri.

## Yedekler

- [x] 8.3.0 AppImage: Release `pi3-8.3.0` ve Pi'de `~/LIVI/LIVI-8.3.0.AppImage`.
- [x] SD kart imajı (8.3.0 hali): `~/Downloads/livi-pi3-8.3.0.img.gz`, harici diske kopyalanacak.
- [ ] 9.0 haliyle yeni bir SD imajı al (AP birimi ve cihaz kaydı dahil).
