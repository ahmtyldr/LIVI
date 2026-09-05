# Raspberry Pi 3 B+ notları (LIVI 8.3.0)

Pi 3'te donanım H.264 çözücü (`v4l2h264dec`, bcm2835-codec) telefonun bildirdiği
`2:4:16:3` renk bilgisini kabul listesinde bulundurmaz; hat `not-negotiated` ile durur
ve Android Auto ekranı siyah kalır.

`livi-colorimetry-shim.c`, `gst_video_colorimetry_to_string` çağrısını yakalayıp bu
değeri `bt709` ile değiştirir. Sadece gst-host sürecine yüklenir.

Pi üzerinde kurulum:

```bash
gcc -O2 -shared -fPIC -o livi-colorimetry-shim.so livi-colorimetry-shim.c -ldl
sudo install -m 0755 livi-colorimetry-shim.so /usr/local/lib/livi/
sudo mkdir -p /etc/systemd/system/livi-kiosk.service.d
printf '[Service]\nEnvironment=LIVI_GST_PRELOAD=/usr/local/lib/livi/livi-colorimetry-shim.so\n' \
  | sudo tee /etc/systemd/system/livi-kiosk.service.d/pi3.conf
sudo systemctl daemon-reload && sudo reboot
```

Notlar:
- 8.3.0'da `systemctl restart livi-kiosk` hostapd'yi düşürür (AP kalkmaz); reboot kullan.
- Kalıcı çözüm için 9.0 `native/livi-gst-video/rust/player/src/lib.rs` içindeki
  `BAD_COLORIMETRY` eşleşmesi tek değere bağlı; genelleştirilmeli.

## Sıfırdan kurulum (SD kart bozulursa)

Gerekenler bu depoda ve sürüm sayfasında saklanır; f-io'nun sürümlerine bağımlı değildir.

1. Raspberry Pi OS Lite (64-bit) yaz, SSH aç, Pi'ye bağlan, `sudo apt update && sudo apt full-upgrade -y && sudo reboot`.
2. AppImage'ı indir (SHA-256: `266b6f0c31e89326c305f4ff678fd7032995eece4666bacef5c137ce881fcb5a`):

   ```bash
   mkdir -p ~/LIVI
   curl -fL -o ~/LIVI/LIVI.AppImage \
     https://github.com/ahmtyldr/LIVI/releases/download/pi3-8.3.0/LIVI-8.3.0-linux-arm64.AppImage
   chmod +x ~/LIVI/LIVI.AppImage
   ```

3. Bu klasördeki 8.3.0 kurulum scriptini yerel AppImage ile çalıştır (main dalındaki script 8.3.0 ile uyumsuzdur):

   ```bash
   git clone https://github.com/ahmtyldr/LIVI.git && cd LIVI/scripts/pi3
   ./installer-8.3.0/headless/install.sh ~/LIVI/LIVI.AppImage
   ```

4. Yukarıdaki shim adımlarını uygula ve `sudo reboot`.
5. İsteğe bağlı: `~/.config/LIVI/config.json` içinde `"dashScreenActive": false` (Dash penceresini kapatır).

## SD kart imajı (tam yedek)

Çalışan kartın blok imajı Mac'te `~/Downloads/livi-pi3-8.3.0.img.gz` olarak alındı
(64 GB kart, sıkıştırılmış ~3 GB). Depoya sığmaz; harici diske veya bulut depoya
kopyalayın. SHA-256 özeti aşağıdaki "Doğrulama" satırında.

Alma (Mac, kart `/dev/diskN`):

```bash
diskutil unmountDisk /dev/diskN
sudo dd if=/dev/rdiskN bs=4m | gzip -1 > ~/Downloads/livi-pi3-8.3.0.img.gz
```

Geri yükleme (en az 64 GB kart, `/dev/diskN` doğru olduğundan emin olun, kart tamamen silinir):

```bash
diskutil unmountDisk /dev/diskN
gunzip -c ~/Downloads/livi-pi3-8.3.0.img.gz | sudo dd of=/dev/rdiskN bs=4m
diskutil eject /dev/diskN
```

Geri yüklenen kart Pi'de doğrudan açılır; Wi-Fi şifresi, eşleşmiş telefon ve
tüm ayarlar imajın alındığı andaki gibidir.

Doğrulama (4 Eylül 2026): açılmış boyut 64 088 965 120 bayt, kartla birebir.
SHA-256 (`livi-pi3-8.3.0.img.gz`): `8f893615b2607ef24bdaa9ad8aa6e96c76f85a5d97a8b5f437c0a82875f28589`

## LIVI 9.0 (nightly) notları

5 Eylül 2026'da Pi 3 B+ üzerine f-io'nun `nightly` sürümü (9.0.0, Rust helperd) kuruldu ve
Android Auto donanım çözmeyle çalıştı. 8.3.0 AppImage'ı Pi'de `~/LIVI/LIVI-8.3.0.AppImage`
olarak yedekte; geri dönmek için onu `LIVI.AppImage` adına kopyalayıp reboot yeterli.

Kurulum: `curl -fL -o install.sh https://raw.githubusercontent.com/f-io/LIVI/main/scripts/install/headless/install.sh && chmod +x install.sh && LIVI_CHANNEL=nightly ./install.sh`

Bilinen farklar:
- **Wi-Fi AP birimi (headless hata).** Uygulama `livi-wifi-ap.service` ve sudoers kuralını
  `pkexec` ile kurmaya çalışır; Pi OS Lite'ta pkexec yoktur (`spawn pkexec ENOENT`) ve erişim
  noktası hiç kalkmaz. `nightly-9.0/install-wifi-ap-unit.sh` aynı dosyaları elle kurar.
  Upstream'e bildirilmeli: headless installer bu adımı da yapmalı.
- **Cihaz kaydı.** 9.0 telefonları `~/.config/LIVI/devices.json` içinde tutar. Boşsa (yeni kurulum
  veya listeden kaldırılmışsa) kablosuz Android Auto kendiliğinden başlamaz; Settings → Devices'ta
  telefona tıklayınca el sıkışma başlar ve kayıt oluşur.
- **Colorimetry shim** 9.0'ın gst-host'una da aynı `LIVI_GST_PRELOAD` ile yüklenir, `pi3.conf` korunur.
- Açılış: arayüz ~67 sn (8.3.0'da 55). Ana süreç 9.0'da daha fazla iş yapıyor (helperd, cihaz tarama).

## Kendi güncelleme kanalı

Cihaz, `pi3.conf` içindeki `UPDATE_REPO=ahmtyldr/LIVI` sayesinde Software Update'i bu deponun
"latest" yayınından alır (`nightly` etiketi `updateNightly` açıkken). f-io'nun yayınları cihaza
kendiliğinden gelmez.

Kurallar:
- "latest" her zaman Pi'de doğrulanmış sürüm olmalı. Yedek yayınlar (`pi3-8.3.0`) ön sürüm işaretli.
- Yayın adı `LIVI-<sürüm>-linux-arm64.AppImage` kalıbında olmalı; güncelleyici arm64 AppImage'ı bu ada göre seçer.
- Yeni sürüm akışı: f-io'dan değişiklik al → kendi düzeltmeleri → Actions `build` (linux_arm64) → Pi'de test → `release`.

Mevcut yayın: `v9.0.0-livilite.1` = f-io nightly (4 Eylül 2026), Pi'de çalışan sürümle aynı,
SHA-256 `c045c4ec…9c71acec`.

### Kendi derleme akışı (doğrulandı, 5 Eylül 2026)

```bash
gh workflow run build.yml --repo ahmtyldr/LIVI --ref main \
  -f linux_x64=false -f linux_arm64=true -f macos_arm64=false -f macos_x64=false -f publish_nightly=false
gh run watch --repo ahmtyldr/LIVI            # ~9 dk, ubuntu-24.04-arm
gh run download <RUN_ID> --repo ahmtyldr/LIVI --dir /tmp/livi-build
# Pi'de dene: ~/LIVI/LIVI.AppImage yerine koy, sudo reboot, Android Auto'yu bağla
gh release create v9.0.0-livilite.N /tmp/livi-build/*/LIVI-9.0.0-linux-arm64.AppImage \
  --repo ahmtyldr/LIVI --target main --latest --title "..." --notes "... SHA-256: ..."
```

GStreamer paketleri depoda (`assets/gstreamer/`), iş akışı yalnızca `GITHUB_TOKEN` kullanır; dış bağımlılık yok.
İlk kendi sürüm: `v9.0.0-livilite.2`, SHA-256 `0bf4e998…56fee513`, Pi'de `~/LIVI/LIVI.AppImage`.
Pi'deki yedekler: `LIVI-8.3.0.AppImage`, `LIVI-9.0.0-nightly.AppImage`.

Not: `livi-wifi-ap.service` içeriği uygulamanın beklediğiyle **birebir** aynı olmalı
(`src/main/services/projection/driver/helper/wifiApUnit.ts` → `unitContent()`), yoksa her açılışta
"LIVI needs a boot service so the projection Wi-Fi AP starts with the device" penceresi çıkar ve
"Install" pkexec olmadığı için sessizce başarısız olur. Depodaki kopya bu depodan derlenen sürümle eşleşir;
f-io nightly (dev dalı) ExecStop/TimeoutStopSec satırları da ekliyor.

## Yamalar kaynağa taşındı (5 Eylül 2026)

`v9.0.0-livilite.3` ve sonrası için bu klasördeki elle adımlar **gerekmez**:
- Colorimetry düzeltmesi `native/livi-gst-video/rust/player/src/lib.rs` içinde (commit 30044c12). Shim ve
  `LIVI_GST_PRELOAD` satırı kaldırıldı; `pi3.conf` yalnızca `UPDATE_REPO` taşıyor.
- Wi-Fi AP servisi kurulum scripti tarafından yazılıyor (commit b4160096).

Sıfırdan kurulum artık tek komut (bu deponun scripti ve yayını):

```bash
curl -fL -o install.sh https://raw.githubusercontent.com/ahmtyldr/LIVI/main/scripts/install/headless/install.sh
chmod +x install.sh && LIVI_REPO=ahmtyldr/LIVI ./install.sh && sudo reboot
```

Sonra `pi3.conf` için: `printf '[Service]\nEnvironment=UPDATE_REPO=ahmtyldr/LIVI\n' | sudo tee /etc/systemd/system/livi-kiosk.service.d/pi3.conf`
ve açılış hızlandırma için `boot-tuning/` adımları. Shim dosyaları eski sürümler için burada kalıyor.

Mevcut yayın: `v9.0.0-livilite.3`, SHA-256 `f7e532a1…bd5d66f6`, Pi bu dosyayı çalıştırıyor (kurulum scriptiyle kuruldu).

**Etiket kuralı:** yayın etiketi düz `vX.Y.Z` olmalı (ör. `v9.0.1`). Güncelleme ekranı `^(\d+)\.(\d+)\.(\d+)$` ile okur; `-livilite.N` gibi ekler sürümü okunamaz yapar ve Update düğmesi pasif kalır. Ayrıca cihazda "Nightly" anahtarı kapalı olmalı, bu depoda nightly etiketi yok.

## 2,4 GHz testi (Zero 2 W hazırlığı, 6 Eylül 2026)

Pi Zero 2 W'nin kablosuz çipi yalnızca 2,4 GHz. Pi 3 B+ üzerinde erişim noktası `wifiType=2.4ghz`,
kanal 6, 20 MHz'e alınıp kablosuz Android Auto + hareketli harita + Bluetooth telefon görüşmesi
birlikte denendi (`wifi-monitor.sh`, 10 sn örnekler):

| Ölçüm | Sonuç |
|---|---|
| Bağlantı hızı | 72 Mbit/s sabit, kısa anlarda 58–65 |
| Video trafiği (harita hareketli) | tepe 3,4 Mbit/s |
| Başarısız çerçeve | 10 sn'de en çok 5 |
| Görüşme sırasında video | takılma yok (kullanıcı gözlemi) |
| Sıcaklık | 49 → 58 °C (yük altında), soğutucusuz |

Sonuç: Bluetooth ile aynı bantta çalışmak bu senaryoda sorun çıkarmadı; Zero 2 W'nin dahili çipi
kablosuz Android Auto için yeterli görünüyor. Şehir içi parazit ayrı bir değişken, araçta gözlenmeli.
5 GHz'e dönüş: `~/pi3-backup/config-5ghz.json` → `~/.config/LIVI/config.json`, reboot.

## Headless kip (LIVI_UI=lvgl, Electron yok) — 6 Eylül 2026

Ana süreç, `out/main/headless.js` paketiyle Electron penceresi olmadan çalışır; arayüz JSON-RPC
köprüsünün öbür ucundadır (livi-ui). Pi'de AppImage'ın kendi Node'u kullanılır:

```bash
sudo install -m 0755 scripts/pi3/livi-headless.sh /usr/local/lib/livi/livi-headless.sh
printf '[Service]\nExecStart=\nExecStart=/usr/bin/cage -s -- /usr/local/lib/livi/livi-headless.sh\n' \
  | sudo tee /etc/systemd/system/livi-kiosk.service.d/headless.conf
sudo systemctl daemon-reload && sudo reboot
# Electron'a dönüş: sudo rm /etc/systemd/system/livi-kiosk.service.d/headless.conf && sudo reboot
```

Başlatıcı AppImage'ı `--appimage-mount` ile bir kez bağlar ve compositor yaşadığı sürece bağlı tutar
(compositor kütüphaneleri bağlantıdan tembel yükler; bağlantı kalkınca EGL başlatırken sessizce ölüyordu).
Geliştirme: `Environment=LIVI_HEADLESS_JS=/home/livilite/headless.js` ile `pnpm build:headless` çıktısı
doğrudan çalıştırılır (native eklentiler mount'tan çözülür).

Ölçüm (Pi 3 B+, kablosuz Android Auto, harita açık):

| | Electron kipi | Headless kip |
|---|---|---|
| Toplam RAM kullanımı | ~570 MB | 371 MB |
| Ana süreç RSS | 217 MB (+ renderer 216 MB) | 151 MB |
| Toplam CPU | ~%11 | %4,7 |
| Ana süreç ayağa kalkış | ~34 sn | 6,6 sn |

Ana süreç RSS'i hedefin (<100 MB) üstünde: Electron ikilisi Node olarak çalışsa da ~120 MB taban taşıyor.
Gerçek bir Node ikilisi paketlenirse ~60 MB'a iner (TODO).
