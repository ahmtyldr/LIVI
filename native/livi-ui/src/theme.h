// Colors and fonts mirroring src/renderer/src/theme (themeColors.ts + MUI theme).
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "lvgl.h"

typedef struct {
  bool dark;
  int zoom;              /* uiZoomPercent */
  lv_color_t bg;         /* page background (compositor backdrop) */
  lv_color_t text;       /* text.primary */
  lv_color_t text2;      /* text.secondary */
  lv_color_t primary;    /* primary.main: selected tab, buttons */
  lv_color_t highlight;  /* hover/active */
  lv_color_t divider;
} theme_t;

extern theme_t theme;

void theme_init(void);
/** Applies darkMode / *Color* / uiZoomPercent from a settings object. */
void theme_apply(cJSON *config);
/** Scales a CSS pixel value by uiZoomPercent. */
int theme_px(int px);
/** Roboto at `px` (already zoomed by the caller or not; use theme_px). */
const lv_font_t *theme_font(int px);
/** Built-in Montserrat carrying LV_SYMBOL_* glyphs, nearest to `px`. */
const lv_font_t *theme_symbol_font(int px);
