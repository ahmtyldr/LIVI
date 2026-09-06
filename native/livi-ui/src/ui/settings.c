#include "ui/settings.h"
#include <string.h>
#include "app.h"
#include "i18n.h"
#include "theme.h"
#include "ui/devices.h"
#include "ui/shell.h"

static lv_obj_t *g_page, *g_header, *g_back, *g_title, *g_body, *g_card;
static settings_view_t g_view = SETTINGS_VIEW_ROOT;

/* ---- rows (StackItem.tsx) ---- */

lv_obj_t *settings_row(lv_obj_t *card, bool last) {
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), theme_px(SET_ROW_H));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_right(row, theme_px(SET_ROW_PAD), 0);
  lv_obj_set_style_pad_column(row, theme_px(SET_ROW_GAP), 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  if (!last) {
    lv_obj_t *div = lv_obj_create(row);
    lv_obj_remove_style_all(div);
    lv_obj_add_flag(div, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(div, theme.divider, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_height(div, 2);
    lv_obj_set_width(div, lv_pct(100)); /* resized in the layout callback below */
    lv_obj_align(div, LV_ALIGN_BOTTOM_LEFT, theme_px(SET_ROW_INSET), 0);
    lv_obj_set_style_margin_right(div, theme_px(SET_ROW_PAD), 0);
  }
  return row;
}

static void fix_divider(lv_event_t *e) {
  lv_obj_t *row = lv_event_get_target(e);
  uint32_t n = lv_obj_get_child_count(row);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(row, (int32_t)i);
    if (!lv_obj_has_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT)) continue;
    int32_t w = lv_obj_get_width(row) - theme_px(SET_ROW_INSET) - theme_px(SET_ROW_PAD);
    if (w > 0) lv_obj_set_width(c, w);
  }
}

lv_obj_t *settings_row_label(lv_obj_t *row, const char *icon, const char *text) {
  lv_obj_add_event_cb(row, fix_divider, LV_EVENT_SIZE_CHANGED, NULL);
  lv_obj_t *slot = lv_obj_create(row);
  lv_obj_remove_style_all(slot);
  lv_obj_set_size(slot, theme_px(SET_ROW_PAD + SET_ROW_ICON), theme_px(SET_ROW_H));
  lv_obj_t *img = shell_icon(slot, icon, SET_ROW_ICON);
  lv_obj_set_style_image_recolor(img, theme.text2, 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_align(img, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, theme_font(theme_px(16)), 0);
  lv_obj_set_style_text_color(l, theme.text2, 0);
  lv_obj_set_style_pad_left(l, theme_px(SET_ROW_PAD), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(l, 1);
  return l;
}

void settings_row_chevron(lv_obj_t *row) {
  lv_obj_t *c = shell_icon(row, "chevron", SET_ICON_PX);
  lv_obj_set_style_image_recolor(c, theme.text2, 0);
  lv_obj_set_style_image_recolor_opa(c, LV_OPA_COVER, 0);
}

/* ---- views ---- */

static void root_row_clicked(lv_event_t *e) {
  settings_view_t v = (settings_view_t)(uintptr_t)lv_event_get_user_data(e);
  if (v == SETTINGS_VIEW_ROOT) return; /* group pages arrive in session 8 */
  settings_show_view(v);
}

static void build_root(void) {
  static const struct { const char *icon, *key; settings_view_t view; } groups[] = {
      {"smartphone", "settings.devices", SETTINGS_VIEW_DEVICES},
      {"tune", "settings.general", SETTINGS_VIEW_ROOT},
      {"volume-up", "settings.audio", SETTINGS_VIEW_ROOT},
      {"monitor", "settings.video", SETTINGS_VIEW_ROOT},
      {"contrast", "settings.appearance", SETTINGS_VIEW_ROOT},
      {"memory", "settings.system", SETTINGS_VIEW_ROOT},
  };
  size_t n = sizeof groups / sizeof groups[0];
  for (size_t i = 0; i < n; i++) {
    lv_obj_t *row = settings_row(g_card, i == n - 1);
    settings_row_label(row, groups[i].icon, i18n(groups[i].key));
    settings_row_chevron(row);
    lv_obj_add_event_cb(row, root_row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)groups[i].view);
  }
}

static void back_clicked(lv_event_t *e) {
  (void)e;
  settings_show_view(SETTINGS_VIEW_ROOT);
}

static void build_view(void) {
  if (g_card) lv_obj_delete(g_card);
  g_card = lv_obj_create(g_body);
  lv_obj_remove_style_all(g_card);
  lv_obj_set_width(g_card, lv_pct(100));
  lv_obj_set_height(g_card, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(g_card, theme.paper, 0);
  lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_card, theme_px(SET_CARD_RADIUS), 0);
  lv_obj_set_style_clip_corner(g_card, true, 0);
  lv_obj_set_style_pad_bottom(g_card, 2, 0);

  const char *title = i18n("settings.settingsTitle");
  if (g_view == SETTINGS_VIEW_DEVICES) {
    title = i18n("settings.devices");
    devices_build(g_card);
  } else {
    build_root();
  }
  lv_label_set_text(g_title, title);
  if (g_view == SETTINGS_VIEW_ROOT) lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g_back, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *settings_create(lv_obj_t *parent) {
  g_page = lv_obj_create(parent);
  lv_obj_remove_style_all(g_page);
  lv_obj_set_size(g_page, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_left(g_page, theme_px(SET_PAD_L), 0);
  lv_obj_set_style_pad_right(g_page, theme_px(SET_PAD_L), 0);
  lv_obj_set_style_pad_top(g_page, theme_px(SET_PAD_T), 0);
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_page, theme_px(SET_HEADER_MB), 0);

  /* header: [back slot 56][title, centred][apply slot 100] */
  g_header = lv_obj_create(g_page);
  lv_obj_remove_style_all(g_header);
  lv_obj_set_size(g_header, lv_pct(100), theme_px(SET_HEADER_H));
  g_back = lv_obj_create(g_header);
  lv_obj_remove_style_all(g_back);
  lv_obj_set_size(g_back, theme_px(SET_SLOT_L), lv_pct(100));
  lv_obj_add_flag(g_back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_back, back_clicked, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_icon = shell_icon(g_back, "back", SET_ICON_PX);
  lv_obj_set_style_image_recolor(back_icon, theme.text, 0);
  lv_obj_set_style_image_recolor_opa(back_icon, LV_OPA_COVER, 0);
  lv_obj_center(back_icon);
  g_title = lv_label_create(g_header);
  lv_obj_set_style_text_font(g_title, theme_font_bold(theme_px(SET_TITLE_PX)), 0);
  lv_obj_set_style_text_color(g_title, theme.text, 0);
  lv_obj_center(g_title);

  /* body: the card sits in a Stack with 0.5rem side padding; scrolls when long */
  g_body = lv_obj_create(g_page);
  lv_obj_remove_style_all(g_body);
  lv_obj_set_width(g_body, lv_pct(100));
  lv_obj_set_flex_grow(g_body, 1);
  lv_obj_set_style_pad_left(g_body, theme_px(SET_CARD_PAD), 0);
  lv_obj_set_style_pad_right(g_body, theme_px(SET_CARD_PAD), 0);
  lv_obj_set_scroll_dir(g_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_body, LV_SCROLLBAR_MODE_OFF);
  g_card = NULL;
  build_view();
  return g_page;
}

void settings_destroy(void) {
  devices_destroy();
  g_page = g_header = g_back = g_title = g_body = g_card = NULL;
}

void settings_show_view(settings_view_t view) {
  g_view = view;
  if (g_page) build_view();
  if (view == SETTINGS_VIEW_DEVICES) devices_refresh();
}

settings_view_t settings_current_view(void) { return g_view; }

settings_view_t settings_view_from_route(const char *tail) {
  if (tail && strncmp(tail, "devices", 7) == 0) return SETTINGS_VIEW_DEVICES;
  return SETTINGS_VIEW_ROOT;
}

void settings_on_event(const char *channel, cJSON *args) { devices_on_event(channel, args); }
