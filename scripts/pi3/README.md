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
