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
