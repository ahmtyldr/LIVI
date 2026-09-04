#!/usr/bin/env bash
set -euo pipefail
B=~/pi3-backup/step2
sudo cp -a "$B"/netplan/90-NM-*.yaml /etc/netplan/ 2>/dev/null || true
sudo rm -f /etc/NetworkManager/system-connections/eth0.nmconnection /etc/NetworkManager/system-connections/wlan0-*.nmconnection
sudo systemctl disable --now pi3-net-safety.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/pi3-net-safety.service /usr/local/lib/livi/pi3-net-safety.sh
sudo systemctl daemon-reload
echo "step2 restored (reboot to take effect)"
