# Yapılacaklar

Durum: 5 Eylül 2026. Pi 3 B+ üzerinde LIVI 9.0.0 nightly çalışıyor.

## Kendi güncelleme kanalımız

- [x] Kiosk servisine `UPDATE_REPO=ahmtyldr/LIVI` ekle (`livi-kiosk.service.d/pi3.conf`), cihaz güncellemeleri bu depodan arasın.
- [x] `pi3-8.3.0` yayınını "ön sürüm" olarak işaretle, "latest" olarak sunulmasın.
- [x] Cihazdaki nightly AppImage'ı `v9.0.0-livilite.1` gibi bir yayın olarak koy; cihaz kendini güncel görsün.
- [ ] GitHub Actions `build` iş akışını fork'ta bir kez çalıştırıp (linux_arm64) kendi AppImage'ımızı üret, Pi'de dene.
- [ ] `release` iş akışıyla ilk kendi sürümümüzü yayınla; Software Update'ten alındığını doğrula.
- [ ] Süreç notu: f-io'dan değişiklik alma → kendi düzeltmeleri → derle → Pi'de test → yayınla.

## Kaynağa taşınacak düzeltmeler (shim ve elle kurulumların yerine)

- [ ] `native/livi-gst-video/rust/player/src/lib.rs`: `BAD_COLORIMETRY` tek değer yerine, çözücünün kabul listesinde olmayan her değeri yakalasın (Pi 3'te `2:4:16:3`).
- [ ] Headless kurulum scripti `livi-wifi-ap.service` ve `99-LIVI-wifi-ap` sudoers dosyasını da yazsın (uygulama pkexec ile kuramıyor).
- [ ] İkisini f-io'ya PR olarak sun.

## Açılış süresi (kaldığı yer: adım 3 tamam, arayüz 55 sn / 9.0'da 67 sn)

- [ ] Adım 4: açılmış AppImage'dan çalıştırma denemesi (`--appimage-extract`, ExecStart değişikliği), ölç.
- [ ] Adım 5: Electron'a `--disable-gpu` vererek üç başarısız GPU süreci denemesini atla, ölç.
- [ ] Adım 6: `wifiDedicatedInterface: true` ile erişim noktasını açılışta kaldır, telefon bağlanma süresini ölç.
- [ ] 9.0'ın arayüze 12 sn geç gelmesinin nedenini bul (ana süreç fazları).

## LVGL (bkz. `LVGL_PLAN.md`)

- [x] Aşama 0.1: 9.0.0'ı Pi 3'te doğrula (nightly ile yapıldı, Android Auto donanım çözmeyle çalışıyor).
- [ ] Aşama 0.2: derleme ortamı (GitHub Actions yeterli olabilir; Lima yedek).
- [ ] Aşama 0.3: sözleşme çıkarımı (`contracts/`).
- [ ] Aşama 1: ana süreci Electron'dan ayır, JSON-RPC köprüsü.
- [ ] Aşama 2: `native/livi-ui` iskeleti.

## Yedekler

- [x] 8.3.0 AppImage: Release `pi3-8.3.0` ve Pi'de `~/LIVI/LIVI-8.3.0.AppImage`.
- [x] SD kart imajı (8.3.0 hali): `~/Downloads/livi-pi3-8.3.0.img.gz`, harici diske kopyalanacak.
- [ ] 9.0 haliyle yeni bir SD imajı al (AP birimi ve cihaz kaydı dahil).
