#!/usr/bin/env bash
# Runs LIVI's main process without Electron (LIVI_UI=lvgl) from the installed
# AppImage, using the AppImage's own Node. cage hosts the nested compositor
# the main process spawns, so the projection video plane still reaches the
# screen; the UI comes from native/livi-ui over the JSON-RPC bridge.
#
#   cage -s -- /usr/local/lib/livi/livi-headless.sh
#
# Kiosk drop-in (/etc/systemd/system/livi-kiosk.service.d/headless.conf):
#   [Service]
#   ExecStart=
#   ExecStart=/usr/bin/cage -s -- /usr/local/lib/livi/livi-headless.sh
#
# The AppImage is mounted once here and stays mounted for the whole session:
# the outer launcher exits right after spawning the compositor, and the
# compositor keeps loading libraries from the mount while it runs.
set -uo pipefail
APPIMAGE_FILE="${LIVI_APPIMAGE:-$HOME/LIVI/LIVI.AppImage}"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}"
PIDFILE="$RUNTIME_DIR/livi-compositor.pid"
LOG_DIR="$HOME/.config/LIVI/log"
mkdir -p "$LOG_DIR"
exec >> "$LOG_DIR/launcher.log" 2>&1
echo "--- $(date +%T) livi-headless start"
rm -f "$PIDFILE"

MOUNT_OUT=$(mktemp)
"$APPIMAGE_FILE" --appimage-mount > "$MOUNT_OUT" 2>&1 &
MOUNT_PID=$!
MOUNT=""
for _ in $(seq 1 100); do
  MOUNT=$(head -1 "$MOUNT_OUT")
  [ -n "$MOUNT" ] && [ -x "$MOUNT/livi" ] && break
  sleep 0.1
done
if [ -z "$MOUNT" ] || [ ! -x "$MOUNT/livi" ]; then
  echo "cannot mount $APPIMAGE_FILE"; kill "$MOUNT_PID" 2>/dev/null; exit 1
fi
cleanup() { kill "$MOUNT_PID" 2>/dev/null; rm -f "$MOUNT_OUT"; }
trap cleanup EXIT

# What AppRun would set for the Electron binary.
export APPDIR="$MOUNT"
export LD_LIBRARY_PATH="$MOUNT/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIVI_UI="${LIVI_UI:-lvgl}"
export LIVI_KIOSK="${LIVI_KIOSK:-1}"
unset APPIMAGE  # relaunch via process.execPath, i.e. this same mount

NODE_BIN="$MOUNT/resources/node/node"
UNPACKED="$MOUNT/resources/app.asar.unpacked"
GST_LIB="$(dirname "$(find "$MOUNT/resources/gstreamer" -name 'libgstapp-1.0.so.0' 2>/dev/null | head -1)" 2>/dev/null)"

if [ -z "${LIVI_HEADLESS_JS:-}" ] && [ -x "$NODE_BIN" ] && [ -f "$UNPACKED/out/main/headless.js" ]; then
  # Standalone Node: far smaller RSS than electron-as-node. host/paths.ts
  # resolves everything from these env vars (no process.resourcesPath), and
  # headlessInnerCommand relaunches this same node for the nested compositor.
  export LIVI_NODE_BIN="$NODE_BIN"
  export LIVI_RESOURCES="$MOUNT/resources"
  export LIVI_APP_ROOT="$UNPACKED"
  export LIVI_PACKAGED=1
  [ -n "$GST_LIB" ] && export LD_LIBRARY_PATH="$GST_LIB:$LD_LIBRARY_PATH"     && export GST_PLUGIN_SYSTEM_PATH="$GST_LIB/gstreamer-1.0"
  echo "--- $(date +%T) launching via standalone node: $NODE_BIN"
  "$NODE_BIN" "$UNPACKED/out/main/headless.js"
  rc=$?
else
  # Electron-as-Node fallback (dev bundle, or no bundled node).
  export ELECTRON_RUN_AS_NODE=1
  if [ -n "${LIVI_HEADLESS_JS:-}" ]; then
    export LIVI_RESOURCES="$MOUNT/resources"
    SCRIPT="process.env.NODE_PATH=process.resourcesPath+'/app.asar/node_modules';require('module').Module._initPaths();require('$LIVI_HEADLESS_JS')"
  else
    SCRIPT="require(process.resourcesPath + '/app.asar/out/main/headless.js')"
  fi
  "$MOUNT/livi" -e "$SCRIPT"
  rc=$?
fi
if [ -f "$PIDFILE" ]; then
  pid=$(cat "$PIDFILE")
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "compositor pid $pid, waiting"
    tail --pid="$pid" -f /dev/null
  fi
  echo "--- $(date +%T) compositor gone"
  exit 0
fi
echo "--- $(date +%T) outer exited rc=$rc without a compositor"
exit "$rc"
