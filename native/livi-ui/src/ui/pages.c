#include "ui/pages.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "i18n.h"
#include "theme.h"
#include "ui/projection.h"
#include "ui/settings.h"

static lv_obj_t *g_pages[PAGE_COUNT];

/* ---- helpers ---- */

static lv_obj_t *page_base(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, lv_pct(100), lv_pct(100));
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

static lv_obj_t *title(lv_obj_t *page, const char *text) {
  lv_obj_t *l = lv_label_create(page);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, theme_font(theme_px(24)), 0);
  lv_obj_set_style_text_color(l, theme.text, 0);
  return l;
}

static lv_obj_t *placeholder(lv_obj_t *parent, const char *title_key) {
  lv_obj_t *p = page_base(parent);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(p, theme_px(24), 0);
  lv_obj_set_style_pad_row(p, theme_px(12), 0);
  title(p, i18n(title_key));
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, "livi-ui: this screen arrives in a later session");
  lv_obj_set_style_text_color(l, theme.text2, 0);
  return p;
}

/* ---- API ---- */

void pages_create(lv_obj_t *parent) {
  g_pages[PAGE_HOME] = projection_create(parent);
  lv_obj_add_flag(g_pages[PAGE_HOME], LV_OBJ_FLAG_HIDDEN);
  g_pages[PAGE_TELEMETRY] = placeholder(parent, "settings.telemetry");
  g_pages[PAGE_MEDIA] = placeholder(parent, "settings.media");

  /* Camera.tsx without a configured camera: one centred status line. */
  lv_obj_t *cam = page_base(parent);
  lv_obj_t *cam_msg = lv_label_create(cam);
  lv_label_set_text(cam_msg, "No camera configured.");
  lv_obj_set_style_text_color(cam_msg, theme.text, 0);
  lv_obj_align(cam_msg, LV_ALIGN_TOP_MID, 0, theme_px(64));
  g_pages[PAGE_CAMERA] = cam;

  g_pages[PAGE_SETTINGS] = settings_create(parent);
  lv_obj_add_flag(g_pages[PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN);
}

void pages_destroy(void) {
  /* Objects are deleted with the shell root; just drop our pointers. */
  memset(g_pages, 0, sizeof g_pages);
  projection_destroy();
  settings_destroy();
}

lv_obj_t *pages_get(page_id_t id) { return id < PAGE_COUNT ? g_pages[id] : NULL; }

void pages_on_settings(cJSON *config) {
  projection_on_settings(config);
  settings_on_config(config);
}

void pages_set_streaming(bool streaming) { projection_set_streaming(streaming); }

void pages_on_event(const char *channel, cJSON *args) {
  projection_on_event(channel, args);
  settings_on_event(channel, args);
  if (strcmp(channel, "projection-event") != 0) return;
  cJSON *ev = cJSON_GetArrayItem(args, 0);
  cJSON *type = cJSON_IsObject(ev) ? cJSON_GetObjectItemCaseSensitive(ev, "type") : NULL;
  if (!cJSON_IsString(type)) return;
  const char *t = type->valuestring;
  /* AA "My Car" button (CommandMapping.requestHostUI=3): Projection.tsx leaves
   * the projection surface for /media so the nav rail returns. */
  if (strcmp(t, "command") == 0) {
    cJSON *msg = cJSON_GetObjectItemCaseSensitive(ev, "message");
    cJSON *val = cJSON_IsObject(msg) ? cJSON_GetObjectItemCaseSensitive(msg, "value") : NULL;
    if (cJSON_IsNumber(val) && (int)val->valuedouble == 3) shell_show_page(PAGE_MEDIA);
  }
  if (strcmp(t, "session") == 0) {
    cJSON *pos = cJSON_GetObjectItemCaseSensitive(ev, "position");
    cJSON *tot = cJSON_GetObjectItemCaseSensitive(ev, "total");
    shell_session_flash(cJSON_IsNumber(pos) ? (int)pos->valuedouble : 0,
                        cJSON_IsNumber(tot) ? (int)tot->valuedouble : 0);
  }
  /* Projection.tsx: 'projection' {shown} drives isStreaming, 'failure' clears it. */
  if (strcmp(t, "projection") == 0) {
    cJSON *shown = cJSON_GetObjectItemCaseSensitive(ev, "shown");
    shell_set_streaming(cJSON_IsTrue(shown));
  } else if (strcmp(t, "failure") == 0 || strcmp(t, "unplugged") == 0) {
    shell_set_streaming(false);
  }
}

void pages_on_bridge(bool connected) { (void)connected; }
