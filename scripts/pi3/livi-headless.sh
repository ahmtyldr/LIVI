#!/usr/bin/env bash
# Runs LIVI's main process without Electron (LIVI_UI=lvgl) from the installed
# AppImage, using the AppImage's own Node. cage hosts the nested compositor
# the main process spawns, so the projection video plane still reaches the
# screen; the UI comes from native/livi-ui over the JSON-RPC bridge.
#
#   cage -s -- /usr/local/lib/livi/livi-headless.sh
#
# Switch the kiosk with a drop-in, e.g. /etc/systemd/system/livi-kiosk.service.d/headless.conf:
#   [Service]
#   ExecStart=
#   ExecStart=/usr/bin/cage -s -- /usr/local/lib/livi/livi-headless.sh
set -euo pipefail
APPIMAGE="${LIVI_APPIMAGE:-$HOME/LIVI/LIVI.AppImage}"
export ELECTRON_RUN_AS_NODE=1
export LIVI_UI="${LIVI_UI:-lvgl}"
export LIVI_KIOSK="${LIVI_KIOSK:-1}"
exec "$APPIMAGE" -e "require(process.resourcesPath + '/app.asar/out/main/headless.js')"
