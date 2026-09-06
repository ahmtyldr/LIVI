#include "ui/shell.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "theme.h"
#include "ui/pages.h"
#include "ui/settings.h"

/* AppLayout.tsx nav column: content 74 px + 1 px #444 right border; top block
 * paddingTop 1rem + clock Typography 1.5rem (line-height 1.5 → 36 px);
 * NavRail.tsx: MUI vertical Tabs, each flex 1 1 0, icon 32 px (24 under
 * UI.XS_ICON_MAX_HEIGHT), padding 10px 0, opacity 0.7 unless selected. */
#define RAIL_W 74
#define RAIL_W_XS 56
#define XS_HEIGHT 480
#define CLOCK_PAD_TOP 16
#define CLOCK_LINE 36

static lv_obj_t *g_root, *g_rail, *g_clock, *g_content;
static lv_obj_t *g_tabs[PAGE_COUNT];
static page_id_t g_current = PAGE_HOME;
static lv_timer_t *g_clock_timer;
static bool g_streaming;
static bool g_tab_visible[PAGE_COUNT] = {true, false, true, false, true};
static lv_obj_t *g_session;      /* SessionSwitchOverlay chip */
static lv_timer_t *g_session_timer;
static uint32_t g_session_t0;
static cJSON *g_config;

/* useTabsConfig.tsx: MUI outlined icons, rasterised into assets/icons. */
static const char *const tab_icons[PAGE_COUNT] = {"home", "telemetry", "media", "camera", "settings"};
static const char *const routes[PAGE_COUNT] = {"/", "/telemetry", "/media", "/camera", "/settings"};

page_id_t shell_page_from_route(const char *route) {
  if (route && strncmp(route, "/settings", 9) == 0) return PAGE_SETTINGS;
  for (int i = 0; i < PAGE_COUNT; i++)
    if (route && strcmp(route, routes[i]) == 0) return (page_id_t)i;
  return PAGE_HOME;
}

void shell_show_route(const char *route) {
  page_id_t id = shell_page_from_route(route);
  if (id == PAGE_SETTINGS) {
    const char *tail = route && strlen(route) > 10 ? route + 10 : "";
    settings_show_route(tail);
  }
  shell_show_page(id);
}

page_id_t shell_current_page(void) { return g_current; }

static bool cfg_role_flag(const char *group, bool dflt) {
  cJSON *g = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, group) : NULL;
  cJSON *v = cJSON_IsObject(g) ? cJSON_GetObjectItemCaseSensitive(g, "main") : NULL;
  return cJSON_IsBool(v) ? cJSON_IsTrue(v) : dflt;
}

/* useTabsConfig.tsx for role 'main': Telemetry when a dashboard slot targets
 * main, Media unless media.main is false, Camera when camera.main (default
 * true) and a camera is configured (cameraId), Custom is out of scope. */
bool shell_set_config(cJSON *config) {
  if (g_config) cJSON_Delete(g_config);
  g_config = config ? cJSON_Duplicate(config, 1) : NULL;
  /* Telemetry tab removed at the user's request; it is never shown. */
  bool next[PAGE_COUNT] = {true, false, true, false, true};
  next[PAGE_MEDIA] = cfg_role_flag("media", true);
  cJSON *cam = g_config ? cJSON_GetObjectItemCaseSensitive(g_config, "cameraId") : NULL;
  next[PAGE_CAMERA] = cfg_role_flag("camera", true) && cJSON_IsString(cam) && cam->valuestring[0];
  bool changed = memcmp(next, g_tab_visible, sizeof next) != 0;
  memcpy(g_tab_visible, next, sizeof next);
  return changed;
}

static void paint_tabs(void) {
  /* Nav.tsx activeKey: a route without a tab highlights the first tab (Home). */
  page_id_t active = g_tab_visible[g_current] ? g_current : PAGE_HOME;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (!g_tabs[i]) continue;
    bool sel = i == (int)active;
    lv_obj_t *icon = lv_obj_get_child(g_tabs[i], 0);
    if (icon) {
      lv_obj_set_style_image_recolor(icon, sel ? theme.primary : theme.text, 0);
      lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
      lv_obj_set_style_text_color(icon, sel ? theme.primary : theme.text, 0);
    }
    /* unselected tabs measure at 60 % of text.primary in the Electron capture */
    lv_obj_set_style_opa(g_tabs[i], sel ? LV_OPA_COVER : LV_OPA_60, 0);
  }
}

/** Icon image ("A:" = POSIX root) or a Montserrat symbol when the PNG is missing. */
lv_obj_t *shell_icon(lv_obj_t *parent, const char *name, int px) {
  char rel[PATH_MAX + 2], src[PATH_MAX + 2];
  snprintf(rel, sizeof rel, "icons/%s-%d.png", name, px);
  snprintf(src, sizeof src, "A:%s", app_resource(rel));
  if (access(src + 2, R_OK) == 0) {
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_size(img, px, px);
    return img;
  }
  static bool warned;
  if (!warned) LOG("no icon %s, using symbols", src + 2);
  warned = true;
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, LV_SYMBOL_DUMMY "?");
  lv_obj_set_style_text_font(l, theme_symbol_font(px), 0);
  return l;
}

static void update_clock(lv_timer_t *t) {
  (void)t;
  if (!g_clock) return;
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  char buf[8];
  strftime(buf, sizeof buf, "%H:%M", &tm);
  lv_label_set_text(g_clock, buf);
}

static void set_rail_hidden(bool hidden) {
  if (!g_rail) return;
  if (hidden) lv_obj_add_flag(g_rail, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g_rail, LV_OBJ_FLAG_HIDDEN);
}

/* Projection.tsx toggles the 'show-video' class: while the video plane is
 * shown on the home page the window turns transparent so the plane (which
 * the compositor keeps under the UI surface) becomes visible. */
static void apply_rail_visibility(void) {
  bool video = g_streaming && g_current == PAGE_HOME;
  set_rail_hidden(video);
  lv_obj_set_style_bg_opa(lv_screen_active(), video ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

void shell_set_streaming(bool streaming) {
  g_streaming = streaming;
  apply_rail_visibility();
  pages_set_streaming(streaming);
}

bool shell_streaming(void) { return g_streaming; }

static void set_visible_cb(cJSON *r, cJSON *e, void *u) { (void)r; (void)e; (void)u; }

void shell_show_page(page_id_t id) {
  if (id >= PAGE_COUNT) id = PAGE_HOME;
  page_id_t prev = g_current;
  g_current = id;
  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *p = pages_get((page_id_t)i);
    if (!p) continue;
    if (i == (int)id) lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  }
  paint_tabs();
  apply_rail_visibility();
  pages_set_media_active(id == PAGE_MEDIA);
  /* Projection.tsx: tell main when the video surface is (not) on screen. */
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateBool(id == PAGE_HOME));
  bridge_call("projection.ipc.setVisible", params, set_visible_cb, NULL);
  if (id == PAGE_HOME && prev != PAGE_HOME) bridge_call("projection.ipc.sendFrame", NULL, NULL, NULL);
}

static void tab_clicked(lv_event_t *e) {
  page_id_t id = (page_id_t)(uintptr_t)lv_event_get_user_data(e);
  shell_show_page(id);
}

static void build(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, theme.bg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, theme.text, 0);
  lv_obj_set_style_text_font(scr, theme_font(theme_px(16)), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  int32_t h = lv_display_get_vertical_resolution(NULL);
  bool xs = h <= XS_HEIGHT;
  int icon = theme_px(xs ? 24 : 32);
  int pad = theme_px(xs ? 5 : 10);

  g_root = lv_obj_create(scr);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_size(g_root, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  /* rail */
  g_rail = lv_obj_create(g_root);
  lv_obj_remove_style_all(g_rail);
  lv_obj_set_size(g_rail, theme_px(xs ? RAIL_W_XS : RAIL_W) + 1, lv_pct(100));
  lv_obj_set_flex_flow(g_rail, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_side(g_rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(g_rail, 1, 0);
  lv_obj_set_style_border_color(g_rail, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_opa(g_rail, LV_OPA_COVER, 0);

  /* top block: 1rem padding + one 1.5rem line (line-height 1.5) */
  lv_obj_t *top = lv_obj_create(g_rail);
  lv_obj_remove_style_all(top);
  lv_obj_set_size(top, lv_pct(100), theme_px(CLOCK_PAD_TOP + CLOCK_LINE));
  g_clock = lv_label_create(top);
  lv_obj_set_style_text_font(g_clock, theme_font(theme_px(24)), 0);
  /* Chrome positions glyphs on fractional advances; FreeType rounds them
   * down. +1 px letter spacing and -1 px y put the digits where MUI has them
   * (measured with tools/parity). */
  lv_obj_set_style_text_letter_space(g_clock, 1, 0);
  lv_obj_align(g_clock, LV_ALIGN_TOP_MID, 0, theme_px(CLOCK_PAD_TOP + (CLOCK_LINE - 24) / 2) - 1);
  update_clock(NULL);

  /* tabs fill the remaining height, flex 1 1 0 each */
  for (int i = 0; i < PAGE_COUNT; i++) {
    g_tabs[i] = NULL;
    if (!g_tab_visible[i]) continue;
    lv_obj_t *b = lv_obj_create(g_rail);
    lv_obj_remove_style_all(b);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_min_height(b, icon + 2 * pad, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    lv_obj_t *l = shell_icon(b, tab_icons[i], icon == 24 ? 24 : 32);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 1);
    g_tabs[i] = b;
  }

  /* content */
  g_content = lv_obj_create(g_root);
  lv_obj_remove_style_all(g_content);
  lv_obj_set_height(g_content, lv_pct(100));
  lv_obj_set_flex_grow(g_content, 1);
  pages_create(g_content);

  if (!g_clock_timer) g_clock_timer = lv_timer_create(update_clock, 1000, NULL);
  shell_show_page(g_current);
}

/* @keyframes liviSessionSwitch: 0 → 12 % fade in, hold to 78 %, fade out at 100 %, 1500 ms */
static void session_tick(lv_timer_t *t) {
  (void)t;
  if (!g_session) return;
  uint32_t el = lv_tick_elaps(g_session_t0);
  double p = el / 1500.0, o;
  if (p >= 1) {
    lv_obj_add_flag(g_session, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(g_session_timer);
    return;
  }
  if (p < 0.12) o = p / 0.12;
  else if (p < 0.78) o = 1;
  else o = 1 - (p - 0.78) / 0.22;
  lv_obj_set_style_opa(g_session, (lv_opa_t)(o * 255), 0);
}

void shell_session_flash(int position, int total) {
  if (position < 1) return;
  if (!g_session) {
    g_session = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_session);
    lv_obj_set_style_bg_color(g_session, lv_color_make(18, 18, 20), 0);
    lv_obj_set_style_bg_opa(g_session, (lv_opa_t)(0.72 * 255), 0);
    lv_obj_set_style_radius(g_session, theme_px(14), 0);
    lv_obj_set_style_pad_hor(g_session, theme_px(20), 0);
    lv_obj_set_style_pad_ver(g_session, theme_px(10), 0);
    lv_obj_set_style_shadow_width(g_session, theme_px(22), 0);
    lv_obj_set_style_shadow_offset_y(g_session, theme_px(6), 0);
    lv_obj_set_style_shadow_color(g_session, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(g_session, LV_OPA_40, 0);
    lv_obj_set_size(g_session, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_session, LV_FLEX_FLOW_ROW);
    lv_obj_align(g_session, LV_ALIGN_TOP_RIGHT, -theme_px(16), theme_px(16));
    for (int i = 0; i < 2; i++) {
      lv_obj_t *l = lv_label_create(g_session);
      lv_obj_set_style_text_font(l, theme_font_medium(theme_px(28)), 0);
      lv_obj_set_style_text_letter_space(l, 1, 0);
    }
    g_session_timer = lv_timer_create(session_tick, 33, NULL);
  }
  lv_obj_t *pos = lv_obj_get_child(g_session, 0), *tot = lv_obj_get_child(g_session, 1);
  lv_label_set_text_fmt(pos, "%d", position);
  lv_obj_set_style_text_color(pos, theme.primary, 0);
  lv_label_set_text_fmt(tot, "/%d", total);
  lv_obj_set_style_text_color(tot, lv_color_white(), 0);
  lv_obj_set_style_text_opa(tot, (lv_opa_t)(0.92 * 255), 0);
  lv_obj_remove_flag(g_session, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(g_session, LV_OPA_TRANSP, 0);
  g_session_t0 = lv_tick_get();
  lv_timer_resume(g_session_timer);
}

void shell_create(void) { build(); }

void shell_rebuild(void) {
  pages_destroy();
  if (g_root) lv_obj_delete(g_root);
  g_root = g_rail = g_clock = g_content = NULL;
  memset(g_tabs, 0, sizeof g_tabs);
  build();
}
