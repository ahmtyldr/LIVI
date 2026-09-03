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
