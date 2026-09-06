#include "ui/pages.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "theme.h"

static lv_obj_t *g_pages[PAGE_COUNT];
static lv_obj_t *g_status;       /* settings page: live status label */
static lv_obj_t *g_placeholder;  /* home page: phone outline (StatusOverlay) */
static cJSON *g_config;          /* last settings object (owned) */
static char g_proj_state[48] = "idle";
static lv_timer_t *g_status_timer;
static int32_t g_last_x = -1, g_last_y = -1;

/* ---- home: touch → projection.ipc.sendTouch(x, y, action), 0..1 coords ---- */

static void send_touch(lv_obj_t *page, lv_point_t p, int action) {
  lv_area_t a;
  lv_obj_get_coords(page, &a);
  int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  if (w <= 0 || h <= 0) return;
  double x = (double)(p.x - a.x1) / w, y = (double)(p.y - a.y1) / h;
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  if (y < 0) y = 0;
  if (y > 1) y = 1;
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateNumber(x));
  cJSON_AddItemToArray(params, cJSON_CreateNumber(y));
  cJSON_AddItemToArray(params, cJSON_CreateNumber(action));
  bridge_call("projection.ipc.sendTouch", params, NULL, NULL);
}

static void home_touch(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
      code != LV_EVENT_PRESS_LOST)
    return;
  lv_obj_t *page = lv_event_get_current_target(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  switch (code) {
    case LV_EVENT_PRESSED:
      g_last_x = p.x;
      g_last_y = p.y;
      send_touch(page, p, 1); /* MultiTouchAction.Down */
      break;
    case LV_EVENT_PRESSING:
      if (p.x == g_last_x && p.y == g_last_y) return;
      g_last_x = p.x;
      g_last_y = p.y;
      send_touch(page, p, 2); /* Move */
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      send_touch(page, p, 0); /* Up */
      break;
    default:
      break;
  }
}

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

static const char *cfg_str(const char *key, const char *dflt) {
  cJSON *v = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, key) : NULL;
  return cJSON_IsString(v) ? v->valuestring : dflt;
}

static void refresh_status(lv_timer_t *t) {
  (void)t;
  if (!g_status) return;
  cJSON *dark = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, "darkMode") : NULL;
  cJSON *zoom = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, "uiZoomPercent") : NULL;
  cJSON *wifi = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, "wifiChannel") : NULL;
  lv_label_set_text_fmt(
      g_status,
      "merhaba\n\n"
      "bridge: %s\n"
      "main: LIVI %s\n"
      "carName: %s\n"
      "language: %s   darkMode: %s   zoom: %d%%\n"
      "wifiChannel: %d   startPage: %s\n"
      "projection: %s\n"
      "events: %u (last: %s)",
      bridge_connected() ? "connected" : "connecting…", app_main_version(), cfg_str("carName", "?"),
      cfg_str("language", "?"), cJSON_IsBool(dark) ? (cJSON_IsTrue(dark) ? "on" : "off") : "?",
      cJSON_IsNumber(zoom) ? (int)zoom->valuedouble : 100,
      cJSON_IsNumber(wifi) ? (int)wifi->valuedouble : 0, cfg_str("startPage", "/"), g_proj_state,
      bridge_events_received(), bridge_last_event());
}

/* ---- API ---- */

void pages_create(lv_obj_t *parent) {
  /* Home: the projection surface. Transparent, the video plane sits over it. */
  lv_obj_t *home = page_base(parent);
  lv_obj_add_flag(home, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(home, home_touch, LV_EVENT_ALL, NULL);
  /* StatusOverlay: CropPortraitOutlined at 84 px, text.primary, 55 % opacity
   * until a phone session is active. The 24-unit icon is a 14x20 rounded
   * rectangle with a 2-unit stroke. */
  g_placeholder = shell_icon(home, "home", 84);
  lv_obj_set_style_image_recolor(g_placeholder, theme.text, 0);
  lv_obj_set_style_image_recolor_opa(g_placeholder, LV_OPA_COVER, 0);
  lv_obj_set_style_opa(g_placeholder, LV_OPA_50 + 13, 0); /* 0.55 */
  lv_obj_center(g_placeholder);
  g_pages[PAGE_HOME] = home;

  g_pages[PAGE_TELEMETRY] = placeholder(parent, "settings.telemetry");
  g_pages[PAGE_MEDIA] = placeholder(parent, "settings.media");
  /* Camera.tsx without a configured camera: one centred status line. */
  lv_obj_t *cam = page_base(parent);
  lv_obj_t *cam_msg = lv_label_create(cam);
  lv_label_set_text(cam_msg, "No camera configured.");
  lv_obj_set_style_text_color(cam_msg, theme.text, 0);
  lv_obj_align(cam_msg, LV_ALIGN_TOP_MID, 0, theme_px(64));
  g_pages[PAGE_CAMERA] = cam;

  /* Settings: for now the bridge status page (session 5 acceptance). */
  lv_obj_t *s = page_base(parent);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s, theme_px(24), 0);
  lv_obj_set_style_pad_row(s, theme_px(12), 0);
  lv_obj_add_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  title(s, i18n("settings.title"));
  g_status = lv_label_create(s);
  lv_obj_set_width(g_status, lv_pct(100));
  lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_status, theme.text, 0);
  g_pages[PAGE_SETTINGS] = s;

  refresh_status(NULL);
  if (!g_status_timer) g_status_timer = lv_timer_create(refresh_status, 1000, NULL);
}

void pages_destroy(void) {
  /* Objects are deleted with the shell root; just drop our pointers. */
  memset(g_pages, 0, sizeof g_pages);
  g_status = g_placeholder = NULL;
}

lv_obj_t *pages_get(page_id_t id) { return id < PAGE_COUNT ? g_pages[id] : NULL; }

void pages_on_settings(cJSON *config) {
  if (g_config) cJSON_Delete(g_config);
  g_config = config ? cJSON_Duplicate(config, 1) : NULL;
  refresh_status(NULL);
}

void pages_set_streaming(bool streaming) {
  if (!g_placeholder) return;
  if (streaming) lv_obj_add_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
}

void pages_on_event(const char *channel, cJSON *args) {
  if (strcmp(channel, "projection-event") != 0) return;
  cJSON *ev = cJSON_GetArrayItem(args, 0);
  cJSON *type = cJSON_IsObject(ev) ? cJSON_GetObjectItemCaseSensitive(ev, "type") : NULL;
  if (!cJSON_IsString(type)) return;
  const char *t = type->valuestring;
  snprintf(g_proj_state, sizeof g_proj_state, "%s", t);
  /* Projection.tsx: 'projection' {shown} drives isStreaming, 'failure' clears it. */
  if (strcmp(t, "projection") == 0) {
    cJSON *shown = cJSON_GetObjectItemCaseSensitive(ev, "shown");
    shell_set_streaming(cJSON_IsTrue(shown));
  } else if (strcmp(t, "failure") == 0 || strcmp(t, "unplugged") == 0) {
    shell_set_streaming(false);
  }
  refresh_status(NULL);
}

void pages_on_bridge(bool connected) {
  (void)connected;
  refresh_status(NULL);
}
