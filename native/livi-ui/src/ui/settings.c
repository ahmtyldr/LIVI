#include "ui/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "theme.h"
#include "ui/devices.h"
#include "ui/shell.h"

/* The schema tree (contracts/settings-schema.json → out/ui/settings-schema.json)
 * and the live config both stay loaded; every view is rebuilt from them. */
static cJSON *g_root;         /* "new-settings" node */
static cJSON *g_schema_doc;   /* owns g_root */
static cJSON *g_config;       /* live config (owned) */
static lv_obj_t *g_page, *g_header, *g_back, *g_title, *g_body, *g_card;

/* Navigation stack of nodes: [0] is the root, the last is the shown view. */
#define NAV_MAX 10
static cJSON *g_stack[NAV_MAX];
static int g_depth;

static void render_view(void);

/* ---- helpers ---- */

static const char *node_type(cJSON *n) {
  cJSON *t = cJSON_GetObjectItemCaseSensitive(n, "type");
  return cJSON_IsString(t) ? t->valuestring : "";
}
static const char *node_str(cJSON *n, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(n, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static const char *node_label(cJSON *n) {
  const char *key = node_str(n, "labelKey");
  if (key) return i18n(key);
  const char *l = node_str(n, "label");
  return l ? l : "";
}
static cJSON *cfg(const char *path) {
  return g_config && path ? cJSON_GetObjectItemCaseSensitive(g_config, path) : NULL;
}

/* ---- rows (StackItem.tsx) ---- */

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

lv_obj_t *settings_row(lv_obj_t *card, bool last) {
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), theme_px(SET_ROW_H));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_right(row, theme_px(SET_ROW_PAD), 0);
  lv_obj_set_style_pad_column(row, theme_px(SET_ROW_GAP), 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  if (!last) {
    lv_obj_t *div = lv_obj_create(row);
    lv_obj_remove_style_all(div);
    lv_obj_add_flag(div, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(div, theme.divider, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_height(div, 2);
    lv_obj_align(div, LV_ALIGN_BOTTOM_LEFT, theme_px(SET_ROW_INSET), 0);
    lv_obj_add_event_cb(row, fix_divider, LV_EVENT_SIZE_CHANGED, NULL);
  }
  return row;
}

lv_obj_t *settings_row_label(lv_obj_t *row, const char *icon, const char *text) {
  lv_obj_t *slot = lv_obj_create(row);
  lv_obj_remove_style_all(slot);
  lv_obj_set_size(slot, theme_px(SET_ROW_PAD + SET_ROW_ICON), theme_px(SET_ROW_H));
  if (icon) {
    lv_obj_t *img = shell_icon(slot, icon, SET_ROW_ICON);
    lv_obj_set_style_image_recolor(img, theme.text2, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_align(img, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, theme_font(theme_px(16)), 0);
  lv_obj_set_style_text_color(l, theme.text2, 0);
  lv_obj_set_style_pad_left(l, theme_px(SET_ROW_PAD), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(l, 1);
  return row;
}

void settings_row_chevron(lv_obj_t *row) {
  lv_obj_t *c = shell_icon(row, "chevron", SET_ICON_PX);
  lv_obj_set_style_image_recolor(c, theme.text2, 0);
  lv_obj_set_style_image_recolor_opa(c, LV_OPA_COVER, 0);
}

/* value label on the right of a row (select current option, number, %) */
static lv_obj_t *row_value(lv_obj_t *row, const char *text) {
  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, text ? text : "");
  lv_obj_set_style_text_font(l, theme_font(theme_px(16)), 0);
  lv_obj_set_style_text_color(l, theme.text2, 0);
  return l;
}

/* ---- navigation ---- */

static void push(cJSON *node) {
  if (g_depth >= NAV_MAX) return;
  g_stack[g_depth++] = node;
  render_view();
}
static void pop(void) {
  if (g_depth > 1) g_depth--;
  render_view();
}

void settings_save(const char *path, cJSON *value) {
  cJSON *obj = cJSON_CreateObject();
  cJSON_AddItemToObject(obj, path, value);
  /* optimistic local update so the row reflects the change at once */
  if (g_config) {
    cJSON_DeleteItemFromObjectCaseSensitive(g_config, path);
    cJSON_AddItemToObject(g_config, path, cJSON_Duplicate(value, 1));
  }
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, obj);
  bridge_call("projection.settings.save", params, NULL, NULL);
}

/* ---- controls ---- */

static void checkbox_changed(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  cJSON *node = lv_event_get_user_data(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  settings_save(node_str(node, "path"), cJSON_CreateBool(on));
}

static void add_checkbox(lv_obj_t *card, cJSON *node, bool last) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_set_size(sw, theme_px(44), theme_px(24));
  lv_obj_set_style_bg_color(sw, theme.divider, 0);
  lv_obj_set_style_bg_color(sw, theme.primary, LV_PART_INDICATOR | LV_STATE_CHECKED);
  cJSON *v = cfg(node_str(node, "path"));
  if (cJSON_IsTrue(v)) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, checkbox_changed, LV_EVENT_VALUE_CHANGED, node);
}

/* select: show current option label; tapping pushes the field node as an
 * option-list editor (render_view special-cases a control node). */
static const char *select_current_label(cJSON *node) {
  cJSON *v = cfg(node_str(node, "path"));
  cJSON *opts = cJSON_GetObjectItemCaseSensitive(node, "options");
  cJSON *o;
  cJSON_ArrayForEach(o, opts) {
    cJSON *ov = cJSON_GetObjectItemCaseSensitive(o, "value");
    bool match = (cJSON_IsString(ov) && cJSON_IsString(v) && strcmp(ov->valuestring, v->valuestring) == 0) ||
                 (cJSON_IsNumber(ov) && cJSON_IsNumber(v) && ov->valuedouble == v->valuedouble) ||
                 (cJSON_IsBool(ov) && cJSON_IsBool(v) && cJSON_IsTrue(ov) == cJSON_IsTrue(v));
    if (match) return node_str(o, "label");
  }
  if (cJSON_IsString(v)) return v->valuestring;
  return NULL;
}

static void open_control(lv_event_t *e) { push(lv_event_get_user_data(e)); }

static void add_select(lv_obj_t *card, cJSON *node, bool last) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  const char *cur = select_current_label(node);
  row_value(row, cur ? cur : "—");
  settings_row_chevron(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, open_control, LV_EVENT_CLICKED, node);
}

/* number/slider: show value; tap pushes the editor page */
static int slider_view(cJSON *node) { /* stored 0..1 shown 0..100 */
  cJSON *v = cfg(node_str(node, "path"));
  double s = cJSON_IsNumber(v) ? v->valuedouble : 1;
  return (int)(s * 100 + 0.5);
}

static void add_slider_row(lv_obj_t *card, cJSON *node, bool last) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  char buf[16];
  snprintf(buf, sizeof buf, "%d %%", slider_view(node));
  row_value(row, buf);
  settings_row_chevron(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, open_control, LV_EVENT_CLICKED, node);
}

static void add_number_row(lv_obj_t *card, cJSON *node, bool last) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  cJSON *v = cfg(node_str(node, "path"));
  char buf[24];
  if (cJSON_IsNumber(v)) snprintf(buf, sizeof buf, "%g", v->valuedouble);
  else snprintf(buf, sizeof buf, "—");
  row_value(row, buf);
  settings_row_chevron(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, open_control, LV_EVENT_CLICKED, node);
}

static void add_color_row(lv_obj_t *card, cJSON *node, bool last) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  cJSON *v = cfg(node_str(node, "path"));
  const char *hex = cJSON_IsString(v) ? v->valuestring : NULL;
  lv_obj_t *sw = lv_obj_create(row);
  lv_obj_remove_style_all(sw);
  lv_obj_set_size(sw, theme_px(24), theme_px(24));
  lv_obj_set_style_radius(sw, theme_px(4), 0);
  lv_obj_set_style_border_width(sw, 1, 0);
  lv_obj_set_style_border_color(sw, theme.divider, 0);
  lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
  unsigned rgb = 0;
  if (hex && hex[0] == '#' && sscanf(hex + 1, "%6x", &rgb) == 1) {
    lv_obj_set_style_bg_color(sw, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, 0);
  }
  row_value(row, hex ? hex : "auto");
}

static void add_readonly_row(lv_obj_t *card, cJSON *node, bool last, const char *fallback) {
  lv_obj_t *row = settings_row(card, last);
  settings_row_label(row, node_str(node, "icon"), node_label(node));
  cJSON *v = cfg(node_str(node, "path"));
  row_value(row, cJSON_IsString(v) ? v->valuestring : fallback);
}

/* ---- editors (a control node on top of the stack) ---- */

static void option_picked(lv_event_t *e) {
  cJSON *opt = lv_event_get_user_data(e);
  cJSON *node = g_stack[g_depth - 1];
  cJSON *val = cJSON_GetObjectItemCaseSensitive(opt, "value");
  settings_save(node_str(node, "path"), cJSON_Duplicate(val, 1));
  pop();
}

static void render_select_editor(cJSON *node) {
  cJSON *v = cfg(node_str(node, "path"));
  cJSON *opts = cJSON_GetObjectItemCaseSensitive(node, "options");
  int n = cJSON_GetArraySize(opts), i = 0;
  if (n == 0) {
    lv_obj_t *l = lv_label_create(g_card);
    lv_label_set_text(l, "No options on this device");
    lv_obj_set_style_text_color(l, theme.text2, 0);
    lv_obj_set_style_pad_all(l, theme_px(SET_ROW_PAD), 0);
    return;
  }
  cJSON *o;
  cJSON_ArrayForEach(o, opts) {
    lv_obj_t *row = settings_row(g_card, ++i == n);
    settings_row_label(row, NULL, node_str(o, "label"));
    cJSON *ov = cJSON_GetObjectItemCaseSensitive(o, "value");
    bool sel = (cJSON_IsString(ov) && cJSON_IsString(v) && strcmp(ov->valuestring, v->valuestring) == 0) ||
               (cJSON_IsNumber(ov) && cJSON_IsNumber(v) && ov->valuedouble == v->valuedouble);
    if (sel) {
      lv_obj_t *chk = shell_icon(row, "check", SET_ICON_PX);
      lv_obj_set_style_image_recolor(chk, theme.primary, 0);
      lv_obj_set_style_image_recolor_opa(chk, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, option_picked, LV_EVENT_CLICKED, o);
  }
}

static void slider_released(lv_event_t *e) {
  lv_obj_t *sl = lv_event_get_target(e);
  cJSON *node = lv_event_get_user_data(e);
  int val = lv_slider_get_value(sl);
  settings_save(node_str(node, "path"), cJSON_CreateNumber(val / 100.0));
  lv_obj_t *lbl = lv_obj_get_child(lv_obj_get_parent(sl), 0);
  if (lbl) lv_label_set_text_fmt(lbl, "%d %%", val);
}

static void render_slider_editor(cJSON *node) {
  lv_obj_t *wrap = lv_obj_create(g_card);
  lv_obj_remove_style_all(wrap);
  lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(wrap, theme_px(24), 0);
  lv_obj_set_style_pad_row(wrap, theme_px(20), 0);
  lv_obj_t *val = lv_label_create(wrap);
  lv_obj_set_style_text_font(val, theme_font(theme_px(28)), 0);
  lv_obj_set_style_text_color(val, theme.text, 0);
  lv_label_set_text_fmt(val, "%d %%", slider_view(node));
  lv_obj_t *sl = lv_slider_create(wrap);
  lv_obj_set_width(sl, lv_pct(80));
  lv_slider_set_range(sl, 0, 100);
  lv_slider_set_value(sl, slider_view(node), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, theme.primary, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, theme.primary, LV_PART_KNOB);
  lv_obj_add_event_cb(sl, slider_released, LV_EVENT_RELEASED, node);
}

static void stepper_changed(lv_obj_t *card, cJSON *node, double delta) {
  cJSON *v = cfg(node_str(node, "path"));
  double cur = cJSON_IsNumber(v) ? v->valuedouble : 0;
  cJSON *mn = cJSON_GetObjectItemCaseSensitive(node, "min");
  cJSON *mx = cJSON_GetObjectItemCaseSensitive(node, "max");
  cJSON *st = cJSON_GetObjectItemCaseSensitive(node, "step");
  double step = cJSON_IsNumber(st) ? st->valuedouble : 1;
  double next = cur + delta * step;
  if (cJSON_IsNumber(mn) && next < mn->valuedouble) next = mn->valuedouble;
  if (cJSON_IsNumber(mx) && next > mx->valuedouble) next = mx->valuedouble;
  settings_save(node_str(node, "path"), cJSON_CreateNumber(next));
  lv_obj_t *lbl = lv_obj_get_child(card, 1); /* wrap → [minus, value, plus] via user_data below */
  (void)lbl;
}

typedef struct { cJSON *node; int dir; lv_obj_t *value; } step_ctx_t;
static step_ctx_t g_step[2];

static void stepper_btn(lv_event_t *e) {
  step_ctx_t *c = lv_event_get_user_data(e);
  stepper_changed(g_card, c->node, c->dir);
  cJSON *v = cfg(node_str(c->node, "path"));
  if (c->value && cJSON_IsNumber(v)) lv_label_set_text_fmt(c->value, "%g", v->valuedouble);
}

static void render_number_editor(cJSON *node) {
  lv_obj_t *wrap = lv_obj_create(g_card);
  lv_obj_remove_style_all(wrap);
  lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(wrap, theme_px(24), 0);
  lv_obj_set_style_pad_column(wrap, theme_px(28), 0);
  cJSON *v = cfg(node_str(node, "path"));
  lv_obj_t *minus = lv_button_create(wrap);
  lv_obj_set_style_bg_color(minus, theme.paper, 0);
  lv_obj_t *ml = lv_label_create(minus);
  lv_label_set_text(ml, "-");
  lv_obj_center(ml);
  lv_obj_t *value = lv_label_create(wrap);
  lv_obj_set_style_text_font(value, theme_font(theme_px(28)), 0);
  lv_obj_set_style_text_color(value, theme.text, 0);
  lv_label_set_text_fmt(value, "%g", cJSON_IsNumber(v) ? v->valuedouble : 0);
  lv_obj_set_style_min_width(value, theme_px(90), 0);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *plus = lv_button_create(wrap);
  lv_obj_set_style_bg_color(plus, theme.paper, 0);
  lv_obj_t *pl = lv_label_create(plus);
  lv_label_set_text(pl, "+");
  lv_obj_center(pl);
  g_step[0] = (step_ctx_t){node, -1, value};
  g_step[1] = (step_ctx_t){node, +1, value};
  lv_obj_add_event_cb(minus, stepper_btn, LV_EVENT_CLICKED, &g_step[0]);
  lv_obj_add_event_cb(plus, stepper_btn, LV_EVENT_CLICKED, &g_step[1]);
}

/* ---- render a route node's children, or a control editor ---- */

static void route_clicked(lv_event_t *e) { push(lv_event_get_user_data(e)); }

static void render_children(cJSON *node) {
  cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "children");
  int n = cJSON_GetArraySize(children), i = 0;
  cJSON *c;
  cJSON_ArrayForEach(c, children) {
    bool last = ++i == n;
    const char *type = node_type(c);
    if (strcmp(type, "btDeviceList") == 0) {
      devices_build(g_card);
      devices_refresh();
    } else if (strcmp(type, "route") == 0 || strcmp(type, "custom") == 0) {
      lv_obj_t *row = settings_row(g_card, last);
      settings_row_label(row, node_str(c, "icon"), node_label(c));
      if (strcmp(type, "route") == 0) {
        settings_row_chevron(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, route_clicked, LV_EVENT_CLICKED, c);
      } else {
        row_value(row, "…"); /* custom component: session 10+ */
      }
    } else if (strcmp(type, "checkbox") == 0) {
      add_checkbox(g_card, c, last);
    } else if (strcmp(type, "select") == 0) {
      add_select(g_card, c, last);
    } else if (strcmp(type, "slider") == 0) {
      add_slider_row(g_card, c, last);
    } else if (strcmp(type, "number") == 0) {
      add_number_row(g_card, c, last);
    } else if (strcmp(type, "color") == 0) {
      add_color_row(g_card, c, last);
    } else if (strcmp(type, "keybinding") == 0) {
      add_readonly_row(g_card, c, last, "—");
    } else {
      add_readonly_row(g_card, c, last, ""); /* string, posList */
    }
  }
}

static void render_view(void) {
  cJSON *node = g_stack[g_depth - 1];
  devices_destroy(); /* its rows live in the old card we are about to free */
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

  const char *type = node_type(node);
  const char *title = node_label(node);
  if (g_depth == 1) title = i18n("settings.settingsTitle");
  lv_label_set_text(g_title, title && *title ? title : i18n("settings.settingsTitle"));
  if (g_depth <= 1) lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g_back, LV_OBJ_FLAG_HIDDEN);

  if (strcmp(type, "select") == 0) render_select_editor(node);
  else if (strcmp(type, "slider") == 0) render_slider_editor(node);
  else if (strcmp(type, "number") == 0) render_number_editor(node);
  else render_children(node);
}

/* ---- frame ---- */

static void back_clicked(lv_event_t *e) { (void)e; pop(); }

static cJSON *load_schema(void) {
  FILE *f = fopen(app_resource("settings-schema.json"), "rb");
  if (!f) { LOG("settings-schema.json not found under %s", app_resource_dir()); return NULL; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = size > 0 ? malloc((size_t)size) : NULL;
  size_t got = buf ? fread(buf, 1, (size_t)size, f) : 0;
  fclose(f);
  cJSON *doc = got == (size_t)size ? cJSON_ParseWithLength(buf, got) : NULL;
  free(buf);
  return doc;
}

lv_obj_t *settings_create(lv_obj_t *parent) {
  if (!g_schema_doc) {
    g_schema_doc = load_schema();
    g_root = g_schema_doc ? cJSON_GetObjectItemCaseSensitive(g_schema_doc, "tree") : NULL;
  }
  g_page = lv_obj_create(parent);
  lv_obj_remove_style_all(g_page);
  lv_obj_set_size(g_page, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_left(g_page, theme_px(SET_PAD_L), 0);
  lv_obj_set_style_pad_right(g_page, theme_px(SET_PAD_L), 0);
  lv_obj_set_style_pad_top(g_page, theme_px(SET_PAD_T), 0);
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_page, theme_px(SET_HEADER_MB), 0);

  g_header = lv_obj_create(g_page);
  lv_obj_remove_style_all(g_header);
  lv_obj_set_size(g_header, lv_pct(100), theme_px(SET_HEADER_H));
  g_back = lv_obj_create(g_header);
  lv_obj_remove_style_all(g_back);
  lv_obj_set_size(g_back, theme_px(SET_SLOT_L), lv_pct(100));
  lv_obj_add_flag(g_back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_back, back_clicked, LV_EVENT_CLICKED, NULL);
  lv_obj_t *bi = shell_icon(g_back, "back", SET_ICON_PX);
  lv_obj_set_style_image_recolor(bi, theme.text, 0);
  lv_obj_set_style_image_recolor_opa(bi, LV_OPA_COVER, 0);
  lv_obj_center(bi);
  g_title = lv_label_create(g_header);
  lv_obj_set_style_text_font(g_title, theme_font_bold(theme_px(SET_TITLE_PX)), 0);
  lv_obj_set_style_text_color(g_title, theme.text, 0);
  lv_obj_center(g_title);

  g_body = lv_obj_create(g_page);
  lv_obj_remove_style_all(g_body);
  lv_obj_set_width(g_body, lv_pct(100));
  lv_obj_set_flex_grow(g_body, 1);
  lv_obj_set_style_pad_left(g_body, theme_px(SET_CARD_PAD), 0);
  lv_obj_set_style_pad_right(g_body, theme_px(SET_CARD_PAD), 0);
  lv_obj_set_scroll_dir(g_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_body, LV_SCROLLBAR_MODE_ACTIVE);

  g_card = NULL;
  g_depth = 0;
  if (g_root) g_stack[g_depth++] = g_root;
  else g_stack[g_depth++] = cJSON_CreateObject();
  render_view();
  return g_page;
}

void settings_destroy(void) {
  devices_destroy();
  g_page = g_header = g_back = g_title = g_body = g_card = NULL;
  g_depth = 0;
}

void settings_show_route(const char *tail) {
  g_depth = 1;
  cJSON *node = g_root;
  if (tail && *tail && g_root) {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", tail);
    char *save = NULL;
    for (char *seg = strtok_r(buf, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
      cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "children"), *c, *next = NULL;
      cJSON_ArrayForEach(c, children) {
        const char *r = node_str(c, "route");
        const char *pth = node_str(c, "path"); /* controls: reach editors by path (tooling) */
        if ((r && strcmp(r, seg) == 0) || (pth && strcmp(pth, seg) == 0)) { next = c; break; }
      }
      if (!next) break;
      node = next;
      if (g_depth < NAV_MAX) g_stack[g_depth++] = node;
    }
  }
  (void)node;
  if (g_page) render_view();
}

bool settings_at_root(void) { return g_depth <= 1; }

void settings_on_config(cJSON *config) {
  if (g_config) cJSON_Delete(g_config);
  g_config = config ? cJSON_Duplicate(config, 1) : NULL;
  if (g_page) render_view();
}

void settings_on_event(const char *channel, cJSON *args) { devices_on_event(channel, args); }
