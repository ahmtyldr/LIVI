#!/usr/bin/env bash
# Grabs the Pi's screen (cage output) into a local PNG.
#   tools/parity/capture.sh out.png [route]
# With a route, livi-ui is told to show that page first through its control
# FIFO ($XDG_RUNTIME_DIR/livi-ui.ctl, headless mode only).
set -euo pipefail
PI="${LIVI_PI:-livilite@192.168.3.2}"
OUT="$1"; ROUTE="${2:-}"
if [ -n "$ROUTE" ]; then
  ssh "$PI" "echo 'page $ROUTE' > /run/user/1000/livi-ui.ctl" ; sleep 1.5
fi
ssh "$PI" 'WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 grim /tmp/cap.png'
scp -q "$PI:/tmp/cap.png" "$OUT"
echo "$OUT"
