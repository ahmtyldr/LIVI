#include "ui/projection.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "bridge.h"
#include "theme.h"
#include "ui/shell.h"

static lv_obj_t *g_page, *g_overlay, *g_mask[4];
static lv_timer_t *g_breathe;
static bool g_streaming;
static bool g_phone_phase;      /* useProjectionActive(): a session protocol is set */
static int g_stream_w, g_stream_h;   /* negotiated ('resolution' event) */
static int g_user_w, g_user_h;       /* projectionWidth/Height */
static int g_inset[4];               /* view area top, bottom, left, right */
static int32_t g_last_x = -1, g_last_y = -1;
static bool g_down;

/* ---- StatusOverlay ---- */

/* theme.ts initUiBreatheClock: 1600 ms, 0.18..1, ramp 35 %, hold 15 %, fall 35 %, off 15 %, 24 fps */
static void breathe_cb(lv_timer_t *t) {
  (void)t;
  if (!g_overlay || !g_phone_phase) return;
  uint32_t ms = lv_tick_get() % 1600;
  double p = ms / 1600.0, wave;
  if (p < 0.35) wave = p / 0.35;
  else if (p < 0.5) wave = 1;
  else if (p < 0.85) wave = 1 - (p - 0.5) / 0.35;
  else wave = 0;
  lv_obj_set_style_opa(g_overlay, (lv_opa_t)((0.18 + 0.82 * wave) * 255), 0);
}

static void overlay_refresh(void) {
  if (!g_overlay) return;
  if (g_streaming) lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  if (!g_phone_phase) lv_obj_set_style_opa(g_overlay, LV_OPA_50 + 13, 0); /* 0.55 */
}

/* ---- ViewAreaMask: theme background over the insets while video shows ---- */

static void mask_refresh(void) {
  int32_t w = lv_obj_get_width(g_page), h = lv_obj_get_height(g_page);
  for (int i = 0; i < 4; i++) {
    if (!g_mask[i]) continue;
    int inset = g_inset[i];
    bool vertical = i >= 2; /* left/right bars */
    int denom = vertical ? (g_user_w > 0 ? g_user_w : w) : (g_user_h > 0 ? g_user_h : h);
    int size = inset > 0 && denom > 0 ? (int)lround((double)inset * (vertical ? w : h) / denom) : 0;
    if (!g_streaming || size <= 0) {
      lv_obj_add_flag(g_mask[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(g_mask[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_mask[i], theme.bg, 0);
    if (i == 0) { lv_obj_set_size(g_mask[i], w, size); lv_obj_align(g_mask[i], LV_ALIGN_TOP_LEFT, 0, 0); }
    if (i == 1) { lv_obj_set_size(g_mask[i], w, size); lv_obj_align(g_mask[i], LV_ALIGN_BOTTOM_LEFT, 0, 0); }
    if (i == 2) { lv_obj_set_size(g_mask[i], size, h); lv_obj_align(g_mask[i], LV_ALIGN_TOP_LEFT, 0, 0); }
    if (i == 3) { lv_obj_set_size(g_mask[i], size, h); lv_obj_align(g_mask[i], LV_ALIGN_TOP_RIGHT, 0, 0); }
  }
}

/* ---- touch: useProjectionTouch.ts norm() ---- */

static bool norm_point(lv_point_t p, double *nx, double *ny) {
  lv_area_t a;
  lv_obj_get_coords(g_page, &a);
  double rw = lv_area_get_width(&a), rh = lv_area_get_height(&a);
  if (rw <= 0 || rh <= 0) return false;
  double lx = p.x - a.x1, ly = p.y - a.y1;
  if (g_stream_w <= 0 || g_stream_h <= 0) {
    if (lx < 0 || lx > rw || ly < 0 || ly > rh) return false;
    *nx = lx / rw;
    *ny = ly / rh;
  } else {
    /* the phone renders the user aspect ratio inside the transport tier */
    double vis_w = g_stream_w, vis_h = g_stream_h;
    if (g_user_w > 0 && g_user_h > 0) {
      double user_ar = (double)g_user_w / g_user_h, frame_ar = (double)g_stream_w / g_stream_h;
      if (user_ar <= frame_ar) vis_w = fmax(2, floor(g_stream_h * user_ar));
      else vis_h = fmax(2, floor(g_stream_w / user_ar));
    }
    double crop_l = fmax(0, (g_stream_w - vis_w) / 2), crop_t = fmax(0, (g_stream_h - vis_h) / 2);
    double content_ar = vis_w / vis_h, dw = rw, dh = rh, ox = 0, oy = 0;
    if (rw / rh > content_ar) { dw = rh * content_ar; ox = (rw - dw) / 2; }
    else { dh = rw / content_ar; oy = (rh - dh) / 2; }
    lx -= ox;
    ly -= oy;
    if (lx < 0 || lx > dw || ly < 0 || ly > dh) return false;
    *nx = (crop_l + lx / dw * vis_w) / g_stream_w;
    *ny = (crop_t + ly / dh * vis_h) / g_stream_h;
  }
  if (*nx < 0) *nx = 0;
  if (*nx > 1) *nx = 1;
  if (*ny < 0) *ny = 0;
  if (*ny > 1) *ny = 1;
  return true;
}

/* projection.ipc.sendTouch takes TouchAction codes (Down 14, Move 15, Up 16),
 * NOT MultiTouchAction; mapTouchAction() in AaSession silently turns anything
 * else into a Move, so a wrong code registers no tap on the phone. */
static void send_touch(double x, double y, int action) {
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateNumber(x));
  cJSON_AddItemToArray(params, cJSON_CreateNumber(y));
  cJSON_AddItemToArray(params, cJSON_CreateNumber(action));
  bridge_call("projection.ipc.sendTouch", params, NULL, NULL);
}

static bool g_debug;

static void touch_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
      code != LV_EVENT_PRESS_LOST)
    return;
  if (g_debug && code != LV_EVENT_PRESSING)
    LOG("touch: code=%d streaming=%d stream=%dx%d user=%dx%d", code, g_streaming, g_stream_w,
        g_stream_h, g_user_w, g_user_h);
  if (!g_streaming) return; /* Projection.tsx: pointerEvents only while streaming */
  lv_indev_t *indev = lv_event_get_indev(e);
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  double nx, ny;
  bool inside = norm_point(p, &nx, &ny);
  if (g_debug && code != LV_EVENT_PRESSING)
    LOG("touch: p=%d,%d inside=%d n=%.3f,%.3f", p.x, p.y, inside, nx, ny);
  switch (code) {
    case LV_EVENT_PRESSED:
      if (!inside) return;
      g_down = true;
      g_last_x = p.x;
      g_last_y = p.y;
      send_touch(nx, ny, 14); /* TouchAction.Down */
      break;
    case LV_EVENT_PRESSING:
      if (!g_down || !inside || (p.x == g_last_x && p.y == g_last_y)) return;
      g_last_x = p.x;
      g_last_y = p.y;
      send_touch(nx, ny, 15); /* TouchAction.Move */
      break;
    default:
      if (!g_down) return;
      g_down = false;
      if (!inside) { nx = 0.5; ny = 0.5; } /* lift outside: still release */
      send_touch(nx, ny, 16); /* TouchAction.Up */
      break;
  }
}

/* ---- API ---- */

lv_obj_t *projection_create(lv_obj_t *parent) {
  g_debug = getenv("LIVI_UI_DEBUG") != NULL;
  g_page = lv_obj_create(parent);
  lv_obj_remove_style_all(g_page);
  lv_obj_set_size(g_page, lv_pct(100), lv_pct(100));
  lv_obj_add_flag(g_page, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_page, touch_cb, LV_EVENT_ALL, NULL);

  for (int i = 0; i < 4; i++) {
    g_mask[i] = lv_obj_create(g_page);
    lv_obj_remove_style_all(g_mask[i]);
    lv_obj_set_style_bg_opa(g_mask[i], LV_OPA_COVER, 0);
    lv_obj_add_flag(g_mask[i], LV_OBJ_FLAG_HIDDEN);
  }

  /* StatusOverlay: CropPortraitOutlined 84 px, text.primary, centred in the content area */
  g_overlay = shell_icon(g_page, "home", 84);
  lv_obj_set_style_image_recolor(g_overlay, theme.text, 0);
  lv_obj_set_style_image_recolor_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_center(g_overlay);
  overlay_refresh();
  mask_refresh();
  if (!g_breathe) g_breathe = lv_timer_create(breathe_cb, 42, NULL);
  return g_page;
}

void projection_destroy(void) {
  g_page = g_overlay = NULL;
  memset(g_mask, 0, sizeof g_mask);
}

void projection_set_streaming(bool streaming) {
  g_streaming = streaming;
  if (!streaming) g_down = false;
  overlay_refresh();
  if (g_page) mask_refresh();
}

static int cfg_int(cJSON *c, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  return cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
}

void projection_on_settings(cJSON *config) {
  if (!cJSON_IsObject(config)) return;
  g_user_w = cfg_int(config, "projectionWidth");
  g_user_h = cfg_int(config, "projectionHeight");
  g_inset[0] = cfg_int(config, "projectionViewAreaTop");
  g_inset[1] = cfg_int(config, "projectionViewAreaBottom");
  g_inset[2] = cfg_int(config, "projectionViewAreaLeft");
  g_inset[3] = cfg_int(config, "projectionViewAreaRight");
  if (g_page) mask_refresh();
}

void projection_on_event(const char *channel, cJSON *args) {
  if (strcmp(channel, "projection-event") != 0) return;
  cJSON *ev = cJSON_GetArrayItem(args, 0);
  cJSON *type = cJSON_IsObject(ev) ? cJSON_GetObjectItemCaseSensitive(ev, "type") : NULL;
  if (!cJSON_IsString(type)) return;
  const char *t = type->valuestring;
  if (strcmp(t, "resolution") == 0) {
    cJSON *pl = cJSON_GetObjectItemCaseSensitive(ev, "payload");
    if (cJSON_IsObject(pl)) {
      g_stream_w = cfg_int(pl, "width");
      g_stream_h = cfg_int(pl, "height");
    }
  } else if (strcmp(t, "session") == 0) {
    cJSON *proto = cJSON_GetObjectItemCaseSensitive(ev, "protocol");
    g_phone_phase = cJSON_IsString(proto);
    overlay_refresh();
  } else if (strcmp(t, "failure") == 0 || strcmp(t, "unplugged") == 0) {
    g_phone_phase = false;
    g_stream_w = g_stream_h = 0;
    overlay_refresh();
  }
}
