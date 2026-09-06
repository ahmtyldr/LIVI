#include "theme.h"
#include <stdio.h>
#include <string.h>
#include "app.h"

theme_t theme;

#define FONT_SLOTS 16
static struct {
  int px;
  int weight;
  lv_font_t *font;
} g_fonts[FONT_SLOTS];

static lv_color_t hex(const char *s, lv_color_t fallback) {
  if (!s || s[0] != '#' || strlen(s) != 7) return fallback;
  unsigned v = 0;
  if (sscanf(s + 1, "%6x", &v) != 1) return fallback;
  return lv_color_hex(v);
}

static const char *str(cJSON *o, const char *key) {
  cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
  return cJSON_IsString(v) && v->valuestring[0] ? v->valuestring : NULL;
}

void theme_init(void) {
  /* lv_init() already ran lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT). */
  theme.dark = true;
  theme.zoom = 100;
  theme_apply(NULL);
}

void theme_apply(cJSON *config) {
  cJSON *dark = config ? cJSON_GetObjectItemCaseSensitive(config, "darkMode") : NULL;
  if (cJSON_IsBool(dark)) theme.dark = cJSON_IsTrue(dark);
  cJSON *zoom = config ? cJSON_GetObjectItemCaseSensitive(config, "uiZoomPercent") : NULL;
  if (cJSON_IsNumber(zoom) && zoom->valuedouble >= 50 && zoom->valuedouble <= 200)
    theme.zoom = (int)zoom->valuedouble;

  if (theme.dark) {
    theme.bg = hex(str(config, "backgroundColorDark"), lv_color_hex(0x000000));
    theme.text = lv_color_hex(0xffffff);
    theme.text2 = lv_color_hex(0xbbbbbb);
    theme.primary = hex(str(config, "primaryColorDark"), lv_color_hex(0x00adad));
    theme.highlight = hex(str(config, "highlightColorDark"), lv_color_hex(0x009494));
    theme.divider = lv_color_hex(0x444444);
    theme.paper = lv_color_hex(0x0d0d0d);
    theme.secondary = lv_color_hex(0x30fb37);
    theme.disabled = lv_color_hex(0x666666);
  } else {
    theme.bg = hex(str(config, "backgroundColorLight"), lv_color_hex(0xd4d4d4));
    theme.text = lv_color_hex(0x000000);
    theme.text2 = lv_color_hex(0x333333);
    theme.primary = hex(str(config, "primaryColorLight"), lv_color_hex(0x008585));
    theme.highlight = hex(str(config, "highlightColorLight"), lv_color_hex(0x007575));
    theme.divider = lv_color_hex(0xcccccc);
    theme.paper = lv_color_hex(0xf2f2f2);
    theme.secondary = lv_color_hex(0x30fb37);
    theme.disabled = lv_color_hex(0x999999);
  }
}

int theme_px(int px) { return (px * theme.zoom + 50) / 100; }

const lv_font_t *theme_symbol_font(int px) {
  if (px <= 15) return &lv_font_montserrat_14;
  if (px <= 18) return &lv_font_montserrat_16;
  if (px <= 22) return &lv_font_montserrat_20;
  if (px <= 28) return &lv_font_montserrat_24;
  return &lv_font_montserrat_32;
}

static const lv_font_t *font_get(int px, int weight) {
  if (px < 8) px = 8;
  if (px > 96) px = 96;
  for (int i = 0; i < FONT_SLOTS; i++)
    if (g_fonts[i].font && g_fonts[i].px == px && g_fonts[i].weight == weight) return g_fonts[i].font;
  int slot = -1;
  for (int i = 0; i < FONT_SLOTS; i++)
    if (!g_fonts[i].font) {
      slot = i;
      break;
    }
  if (slot < 0) return theme_symbol_font(px);
  const char *file = weight >= 700 ? "fonts/roboto-latin-700-normal.woff"
                     : weight >= 500 ? "fonts/roboto-latin-500-normal.woff"
                                     : "fonts/roboto-latin-400-normal.woff";
  lv_font_t *f = lv_freetype_font_create(app_resource(file), LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                         (uint32_t)px, LV_FREETYPE_FONT_STYLE_NORMAL);
  if (!f) {
    static bool warned;
    if (!warned) LOG("Roboto not found under %s, using Montserrat", app_resource_dir());
    warned = true;
    return theme_symbol_font(px);
  }
  g_fonts[slot].px = px;
  g_fonts[slot].weight = weight;
  g_fonts[slot].font = f;
  return f;
}

const lv_font_t *theme_font(int px) { return font_get(px, 400); }
const lv_font_t *theme_font_bold(int px) { return font_get(px, 700); }
const lv_font_t *theme_font_medium(int px) { return font_get(px, 500); }
