#include "ui/devices.h"
#include <math.h>
#include <string.h>
#include "app.h"
#include "bridge.h"
#include "theme.h"
#include "ui/settings.h"
#include "ui/shell.h"

static cJSON *g_list;     /* DeviceView[] (owned) */
static lv_obj_t *g_card;  /* where the rows live while the view is shown */

static const char *str(cJSON *o, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool has_num(cJSON *o, const char *k) { return cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(o, k)); }
static double num(cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k)->valuedouble; }

/* ---- widgets from SettingsDeviceRow.tsx ---- */

static lv_obj_t *slot(lv_obj_t *row, int w) {
  lv_obj_t *s = lv_obj_create(row);
  lv_obj_remove_style_all(s);
  lv_obj_set_size(s, theme_px(w), theme_px(24));
  return s;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t opa) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, theme_px(x), theme_px(y));
  lv_obj_set_size(o, theme_px(w), theme_px(h));
  lv_obj_set_style_radius(o, theme_px(r), 0);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  return o;
}

static void battery(lv_obj_t *row, double level, bool charging) {
  lv_obj_t *s = slot(row, 52);
  int pct = (int)fmax(0, fmin(100, round(level)));
  lv_color_t fill = pct < 10 ? lv_color_hex(0xff3b30) : pct < 20 ? lv_color_hex(0xffcc00) : lv_color_hex(0x34c759);
  /* outline: rect x1 y3.5 w45 h17 r4, stroke 1.8 text.secondary */
  lv_obj_t *o = rect(s, 1, 4, 45, 17, 4, theme.text2, LV_OPA_TRANSP);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, theme.text2, 0);
  lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
  rect(s, 47, 9, 3, 7, 1, theme.text2, LV_OPA_COVER);                    /* nub */
  rect(s, 3, 6, (int)fmax(3, 42.0 * pct / 100), 13, 2, fill, LV_OPA_90); /* charge */
  lv_obj_t *l = lv_label_create(s);
  lv_label_set_text_fmt(l, "%d", pct);
  lv_obj_set_style_text_font(l, theme_font_bold(theme_px(10)), 0);
  lv_obj_set_style_text_color(l, theme.text, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, theme_px(39) - lv_obj_get_width(l), 0);
  lv_obj_update_layout(l);
  lv_obj_set_x(l, theme_px(39) - lv_obj_get_width(l));
  if (charging) {
    lv_obj_t *b = shell_icon(s, "bolt", 12);
    lv_obj_set_style_image_recolor(b, theme.text, 0);
    lv_obj_set_style_image_recolor_opa(b, LV_OPA_COVER, 0);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, theme_px(2), 0);
  }
}

static void signal_bars(lv_obj_t *row, double level) {
  static const double heights[5] = {4, 6.5, 9, 11.5, 14};
  lv_obj_t *s = slot(row, 26);
  int n = (int)fmax(0, fmin(5, round(level)));
  int total_w = 5 * 3 + 4 * 2;
  int x0 = (26 - total_w) / 2;
  for (int i = 0; i < 5; i++) {
    bool on = i < n;
    int h = (int)round(heights[i]);
    lv_obj_t *b = rect(s, x0 + i * 5, 5 + (14 - h), 3, h, 1, on ? theme.text : theme.disabled,
                       on ? LV_OPA_COVER : LV_OPA_40 - 13);
    (void)b;
  }
}

static void status_dot(lv_obj_t *row, const char *status) {
  lv_obj_t *s = slot(row, 20);
  bool active = status && strcmp(status, "active") == 0;
  bool offline = status && strcmp(status, "offline") == 0;
  lv_obj_t *d = rect(s, 4, 6, 12, 12, 6, active ? theme.secondary : theme.text, offline ? LV_OPA_TRANSP : LV_OPA_COVER);
  if (offline) {
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, theme.disabled, 0);
    lv_obj_set_style_border_opa(d, LV_OPA_COVER, 0);
  }
}

/* ---- actions ---- */

static void select_done(cJSON *result, cJSON *error, void *user) {
  (void)user; (void)error;
  cJSON *ok = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "ok") : NULL;
  if (cJSON_IsTrue(ok)) shell_show_page(PAGE_HOME);
}

static void row_clicked(lv_event_t *e) {
  cJSON *d = lv_event_get_user_data(e);
  const char *id = str(d, "id");
  if (!id) return;
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateString(id));
  bridge_call("projection.ipc.selectDevice", params, select_done, NULL);
}

static void forget_clicked(lv_event_t *e) {
  cJSON *d = lv_event_get_user_data(e);
  const char *id = str(d, "id");
  if (!id) return;
  const char *source = str(d, "source");
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateString(id));
  bridge_call(source && strcmp(source, "dongle") == 0 ? "projection.ipc.forgetBluetoothPairedDevice"
                                                       : "projection.ipc.forgetDevice",
              params, NULL, NULL);
}

/* ---- rows ---- */

static void build_rows(void) {
  if (!g_card) return;
  lv_obj_clean(g_card);
  int n = cJSON_IsArray(g_list) ? cJSON_GetArraySize(g_list) : 0;
  if (n == 0) {
    lv_obj_t *l = lv_label_create(g_card);
    lv_label_set_text(l, "No paired devices");
    lv_obj_set_style_text_color(l, theme.text2, 0);
    lv_obj_set_style_text_font(l, theme_font(theme_px(16)), 0);
    lv_obj_set_style_pad_all(l, theme_px(SET_ROW_PAD), 0);
    return;
  }
  for (int i = 0; i < n; i++) {
    cJSON *d = cJSON_GetArrayItem(g_list, i);
    const char *status = str(d, "status");
    const char *protocol = str(d, "protocol");
    const char *source = str(d, "source");
    const char *transport = str(d, "lastTransport");
    bool active = status && strcmp(status, "active") == 0;
    bool offline = status && strcmp(status, "offline") == 0;
    bool selectable = has_num(d, "session") && !offline;

    lv_obj_t *row = settings_row(g_card, i == n - 1);
    if (offline) lv_obj_set_style_opa(row, LV_OPA_50, 0);
    if (selectable) lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, d);

    /* protocol marker, accent when active */
    lv_obj_t *s = lv_obj_create(row);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, theme_px(12 + 24), theme_px(SET_ROW_H));
    const char *picon = protocol && strcmp(protocol, "carplay") == 0 ? "phone-iphone"
                        : protocol && strcmp(protocol, "androidauto") == 0 ? "android" : "directions-car";
    lv_obj_t *pi = shell_icon(s, picon, 24);
    lv_obj_set_style_image_recolor(pi, active ? theme.secondary : theme.text2, 0);
    lv_obj_set_style_image_recolor_opa(pi, LV_OPA_COVER, 0);
    lv_obj_align(pi, LV_ALIGN_RIGHT_MID, 0, 0);

    const char *name = str(d, "name");
    if (!name) name = str(d, "model");
    if (!name) name = str(d, "id");
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, name ? name : "?");
    lv_obj_set_style_text_font(l, theme_font(theme_px(16)), 0);
    lv_obj_set_style_text_color(l, theme.text2, 0);
    lv_obj_set_style_pad_left(l, theme_px(12), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(l, 1);

    /* value side: fixed slots so missing info never shifts the layout */
    lv_obj_t *badge = slot(row, 26);
    const char *bicon = source && strcmp(source, "dongle") == 0 ? "device-hub"
                        : transport && strcmp(transport, "usb") == 0 ? "cable"
                        : transport && strcmp(transport, "wifi") == 0 ? "wifi" : NULL;
    if (bicon) {
      lv_obj_t *b = shell_icon(badge, bicon, 18);
      lv_obj_set_style_image_recolor(b, theme.text2, 0);
      lv_obj_set_style_image_recolor_opa(b, LV_OPA_COVER, 0);
      lv_obj_center(b);
    }
    if (has_num(d, "batteryLevel")) battery(row, num(d, "batteryLevel"), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "batteryCharging")));
    else slot(row, 52);
    if (has_num(d, "signalStrength")) signal_bars(row, num(d, "signalStrength"));
    else slot(row, 26);
    status_dot(row, status);

    lv_obj_t *btn = lv_obj_create(row);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, theme_px(40), theme_px(40));
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, forget_clicked, LV_EVENT_CLICKED, d);
    lv_obj_t *x = shell_icon(btn, "close", 22);
    lv_obj_set_style_image_recolor(x, theme.text2, 0);
    lv_obj_set_style_image_recolor_opa(x, LV_OPA_COVER, 0);
    lv_obj_center(x);
  }
}

/* ---- API ---- */

void devices_build(lv_obj_t *card) {
  g_card = card;
  build_rows();
}

void devices_destroy(void) { g_card = NULL; }

static void got_devices(cJSON *result, cJSON *error, void *user) {
  (void)error; (void)user;
  if (!cJSON_IsArray(result)) return;
  if (g_list) cJSON_Delete(g_list);
  g_list = cJSON_Duplicate(result, 1);
  build_rows();
}

void devices_refresh(void) { bridge_call("projection.ipc.getDevices", NULL, got_devices, NULL); }

void devices_on_event(const char *channel, cJSON *args) {
  if (strcmp(channel, "projection-event") != 0) return;
  cJSON *ev = cJSON_GetArrayItem(args, 0);
  cJSON *type = cJSON_IsObject(ev) ? cJSON_GetObjectItemCaseSensitive(ev, "type") : NULL;
  if (!cJSON_IsString(type) || strcmp(type->valuestring, "devices") != 0) return;
  cJSON *payload = cJSON_GetObjectItemCaseSensitive(ev, "payload");
  if (!cJSON_IsArray(payload)) return;
  if (g_list) cJSON_Delete(g_list);
  g_list = cJSON_Duplicate(payload, 1);
  build_rows();
}
