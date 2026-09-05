#!/usr/bin/env bash
# LIVI 9.0 (nightly) on a headless kiosk: the app tries to install its early-boot
# Wi-Fi AP unit through pkexec, which does not exist on Pi OS Lite, so wireless
# projection never gets an access point. Install the same unit and sudoers rule
# by hand. Run once on the Pi as the kiosk user (uses sudo).
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
sed "s#/home/livilite#$HOME#g; s#SUDO_USER=livilite#SUDO_USER=$USER#" "$D/livi-wifi-ap.service" | sudo tee /etc/systemd/system/livi-wifi-ap.service >/dev/null
sed "s#/home/livilite#$HOME#g; s#^livilite #$USER #" "$D/99-LIVI-wifi-ap.sudoers" | sudo tee /etc/sudoers.d/99-LIVI-wifi-ap >/dev/null
sudo chmod 440 /etc/sudoers.d/99-LIVI-wifi-ap
sudo visudo -c -f /etc/sudoers.d/99-LIVI-wifi-ap
sudo systemctl daemon-reload
sudo systemctl enable --now livi-wifi-ap.service
systemctl is-active livi-wifi-ap.service
