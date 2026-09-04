#!/usr/bin/env bash
# Step 3: silent boot. Kernel console moves to tty3, systemd status and the
# firmware rainbow splash are turned off, so tty1 stays black until cage starts.
set -euo pipefail
B=~/pi3-backup/step3; mkdir -p "$B"
sudo cp -a /boot/firmware/cmdline.txt /boot/firmware/config.txt "$B/"
C=$(cat /boot/firmware/cmdline.txt)
C=${C//console=tty1/console=tty3}
for f in quiet loglevel=3 vt.global_cursor_default=0 logo.nologo systemd.show_status=false; do
  case " $C " in *" $f "*) ;; *) C="$C $f";; esac
done
printf '%s\n' "$C" | sudo tee /boot/firmware/cmdline.txt >/dev/null
grep -q '^disable_splash=1' /boot/firmware/config.txt || printf '\n[all]\ndisable_splash=1\n' | sudo tee -a /boot/firmware/config.txt >/dev/null
echo "cmdline: $(cat /boot/firmware/cmdline.txt)"
echo "step3 applied; backup in $B"
