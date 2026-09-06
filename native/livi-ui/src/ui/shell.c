#include "ui/shell.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "theme.h"
#include "ui/pages.h"

/* NavRail.tsx: MUI vertical Tabs, icon 32 px (24 when the window is under
 * UI.XS_ICON_MAX_HEIGHT), padding 10px 0 (5px), clock above the tabs. */
#define RAIL_W 72
#define XS_HEIGHT 480

static lv_obj_t *g_root, *g_rail, *g_clock, *g_content;
static lv_obj_t *g_tabs[PAGE_COUNT];
static page_id_t g_current = PAGE_HOME;
static lv_timer_t *g_clock_timer;
static bool g_streaming;

static const char *const tab_symbols[PAGE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_GPS, LV_SYMBOL_PLAY, LV_SYMBOL_IMAGE, LV_SYMBOL_SETTINGS};
static const char *const routes[PAGE_COUNT] = {"/", "/telemetry", "/media", "/camera", "/settings"};

page_id_t shell_page_from_route(const char *route) {
  for (int i = 0; i < PAGE_COUNT; i++)
    if (route && strcmp(route, routes[i]) == 0) return (page_id_t)i;
  return PAGE_HOME;
}

page_id_t shell_current_page(void) { return g_current; }

static void paint_tabs(void) {
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (!g_tabs[i]) continue;
    bool sel = i == (int)g_current;
    lv_obj_set_style_text_color(g_tabs[i], sel ? theme.primary : theme.text, 0);
    lv_obj_set_style_opa(g_tabs[i], sel ? LV_OPA_COVER : LV_OPA_70, 0);
  }
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
  lv_obj_set_size(g_rail, theme_px(RAIL_W), lv_pct(100));
  lv_obj_set_flex_flow(g_rail, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(g_rail, theme_px(8), 0);
  lv_obj_set_style_border_side(g_rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(g_rail, 1, 0);
  lv_obj_set_style_border_color(g_rail, theme.divider, 0);
  lv_obj_set_style_border_opa(g_rail, LV_OPA_COVER, 0);

  g_clock = lv_label_create(g_rail);
  lv_obj_set_style_text_font(g_clock, theme_font(theme_px(24)), 0);
  lv_obj_set_style_pad_bottom(g_clock, theme_px(12), 0);
  update_clock(NULL);

  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *b = lv_obj_create(g_rail);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), icon + 2 * pad);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tab_symbols[i]);
    lv_obj_set_style_text_font(l, theme_symbol_font(icon), 0);
    lv_obj_center(l);
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

void shell_create(void) { build(); }

void shell_rebuild(void) {
  pages_destroy();
  if (g_root) lv_obj_delete(g_root);
  g_root = g_rail = g_clock = g_content = NULL;
  memset(g_tabs, 0, sizeof g_tabs);
  build();
}
