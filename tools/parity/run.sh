#!/usr/bin/env bash
# Captures livi-ui on the Pi for each reference page and reports the diff.
#   tools/parity/run.sh [page...]        (default: camera media telemetry settings)
# Writes /tmp/parity/lvgl-<page>.png and diff-<page>.png.
set -uo pipefail
cd "$(dirname "$0")/../.."
pages=("$@"); [ ${#pages[@]} -eq 0 ] && pages=(camera media telemetry settings)
mkdir -p /tmp/parity
route_of() { case "$1" in home) echo /;; *) echo "/$1";; esac; }
rc=0
for p in "${pages[@]}"; do
  tools/parity/capture.sh "/tmp/parity/lvgl-$p.png" "$(route_of "$p")" >/dev/null
  echo "== $p"
  node tools/parity/compare.mjs "tools/parity/reference/electron-$p.png" "/tmp/parity/lvgl-$p.png" \
    --out "/tmp/parity/diff-$p.png" --region rail=0,0,75,720 --region content=75,0,1205,720 || rc=1
done
exit $rc
