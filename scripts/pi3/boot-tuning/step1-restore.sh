#!/usr/bin/env bash
set -euo pipefail
sudo rm -f /etc/cloud/cloud-init.disabled
sudo systemctl unmask e2scrub_reap.service
sudo systemctl enable rpi-resize-swap-file.service e2scrub_reap.service 2>/dev/null || true
sudo systemctl enable cloud-init-main.service cloud-init-local.service cloud-init-network.service cloud-config.service cloud-final.service 2>/dev/null || true
echo "step1 restored"
