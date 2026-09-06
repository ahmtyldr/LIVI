# Parity tool (LVGL plan, session 6)

Compares livi-ui against the Electron renderer pixel by pixel on the same
device and screen (1280x720 on the Pi 3 B+ test bench).

- `reference/electron-<page>.png` – Electron captures, one per route
  (`startPage` set over the bridge, `app.restartApp`, then `grim` on cage's
  `wayland-0`).
- `capture.sh out.png [route]` – grabs the current screen; with a route it
  first steers livi-ui through its control FIFO (`page /media`).
- `compare.mjs ref.png test.png --out diff.png [--region rail=0,0,72,720]` –
  differing-pixel fraction, per region, plus a diff image. Exit 1 above
  `--max` percent (default 1).
- `png.mjs` – tiny PNG codec (no native deps).
- `icons-src/*.svg` – the MUI outlined nav icons, rasterised with
  `rsvg-convert` into `native/livi-ui/assets/icons`.

Typical loop:

```
tools/parity/capture.sh /tmp/lvgl-media.png /media
node tools/parity/compare.mjs tools/parity/reference/electron-media.png /tmp/lvgl-media.png \
  --out /tmp/diff-media.png --region rail=0,0,72,720
```
