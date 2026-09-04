#!/usr/bin/env bash
# Step 1: disable first-boot-only services that cost ~9 s on every boot.
# cloud-init (Imager customisation, already applied), rpi-resize-swap-file
# (swap already sized), e2scrub_reap (stale ext4 snapshot cleanup).
set -euo pipefail
B=~/pi3-backup/step1; mkdir -p "$B"
systemctl is-enabled cloud-init-main.service cloud-init-local.service cloud-init-network.service cloud-config.service cloud-final.service rpi-resize-swap-file.service e2scrub_reap.service > "$B/enabled-state.txt" 2>&1 || true
sudo touch /etc/cloud/cloud-init.disabled
sudo systemctl mask --now e2scrub_reap.service
sudo systemctl disable cloud-init-main.service cloud-init-local.service cloud-init-network.service cloud-config.service cloud-final.service 2>/dev/null || true
echo "step1 applied; backup in $B"
