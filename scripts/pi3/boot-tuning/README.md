# Açılış hızlandırma (Pi 3 B+, LIVI 8.3.0 kiosk)

Her adım Pi üzerinde çalıştırılır, kendi yedeğini `~/pi3-backup/stepN/` altına alır ve
`stepN-restore.sh` ile geri alınır. Her adımdan sonra `sudo reboot`, ardından
`./measure-boot.sh` ile ölçüm.

| Adım | Script | Ne yapar | Kazanç |
|---|---|---|---|
| 1 | `step1-disable-firstboot-services.sh` | cloud-init ve e2scrub_reap'i kapatır (ilk açılışa özel işler) | ~5 sn |
| 2 | `step2-netplan-to-keyfile.sh` | Ağ profillerini netplan'dan NM keyfile'a taşır; NM'nin profil başına `netplan generate` + reload döngüsünü keser | ~10 sn |
| 3 | `step3-quiet-boot.sh` | Çekirdek konsolunu tty3'e alır, `quiet` ve splash kapatma; ekran siyahtan doğrudan LIVI'ye geçer | görsel |

Ölçümler (kernel başlangıcından itibaren, saniye):

| | Taban | Adım 1 | Adım 2 | Adım 3 |
|---|---|---|---|---|
| NetworkManager hazır | 33.5 | 29.0 | 19.7 | 19.0 |
| kiosk başladı | 33.7 | 29.1 | 19.9 | 19.1 |
| LIVI arayüzü | 69.7 | 64.9 | 56.4 | 55.4 |
| Android Auto videosu | 82.6 | 77.0 | 68.4 | 73.7* |

\* telefon tarafı; erişim noktası 58.6'da hazırdı.

Notlar:
- `rpi-resize-swap-file.service` maskelenmemeli: zram+dosya swap'ı bu servis kurar, maskelenince swap 0 olur.
- Adım 2 sonrası `/etc/NetworkManager/system-connections/` altında `netplan-` önekli dosya kalmamalı; LIVI'nin yardımcısı daha önce bir tane kopyalamıştı, o da bir reload döngüsü tetikler.
- Kalan 34 sn (kiosk → arayüz) tamamen LIVI/Electron açılışı: AppImage bağlama, Electron yüklemesi, üç başarısız GPU süreci denemesi, arayüz çizimi. SD kart okuma hızı 22 MB/s.
