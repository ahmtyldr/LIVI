#!/usr/bin/env bash
set -euo pipefail
sudo cp -a ~/pi3-backup/step3/cmdline.txt ~/pi3-backup/step3/config.txt /boot/firmware/
echo "step3 restored (reboot to take effect)"
