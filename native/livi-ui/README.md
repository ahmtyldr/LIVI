# livi-ui

LIVI's LVGL front end: a Wayland client for the nested `livi-compositor`
that replaces the Electron renderer in headless mode (`LIVI_UI=lvgl`). It
talks to the main process over the JSON-RPC bridge
(`$XDG_RUNTIME_DIR/livi-ui.sock`, see `src/main/ui-bridge/`) and is spawned
by the headless entry (`src/main/app/uiProcess.ts`).

Layout

- `CMakeLists.txt` – LVGL 9.5 and cJSON via FetchContent, xdg-shell from
  wayland-scanner, `cmake/lv_conf.cmake` derives `lv_conf.h` from LVGL's template.
- `src/main.c` – window (title/app id `dev.f-io.livi`), poll loop.
- `src/bridge.c` – NDJSON JSON-RPC client with reconnect.
- `src/theme.c` – colours from `themeColors.ts` + settings, Roboto via FreeType.
- `src/i18n.c` – `contracts/locales/*.json`.
- `src/ui/shell.c` – nav rail (clock + tabs) and content area.
- `src/ui/pages.c` – pages; session 5 ships the home surface (touch
  forwarding) and a status page under Settings.

Build (Debian / Raspberry Pi OS)

```
sudo apt install cmake git build-essential pkg-config libwayland-dev \
  wayland-protocols libxkbcommon-dev libfreetype-dev
cmake -S native/livi-ui -B native/livi-ui/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/livi-ui/build -j
node scripts/ui-resources.mjs          # out/ui/fonts + out/ui/locales
```

Run by hand under a running headless LIVI:

```
LIVI_UI_RESOURCES=out/ui XDG_RUNTIME_DIR=/run/user/1000 \
  WAYLAND_DISPLAY=wayland-1 native/livi-ui/build/livi-ui
```

The packaged app carries it as `resources/ui/livi-ui` next to `fonts/` and
`locales/` (`scripts/build-native.mjs`, `electron-builder.yml`).
`LIVI_UI_BIN=/path/to/livi-ui` makes the headless entry use another binary.
