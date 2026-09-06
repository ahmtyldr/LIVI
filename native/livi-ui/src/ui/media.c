#include "ui/media.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "fft.h"
#include "theme.h"

/* mediaScaleOps for 1280x720: title 48, artist 24, album 20, pad 14,
 * colGap 28, sectionGap 22, ctrlSize 68, ctrlGap 32, progressH 9. */
#define M_PAD 14
#define M_COLGAP 28
#define M_TITLE 30 /* clamped for the two-line block; Chrome uses up to 48 */
#define M_ARTIST 22
#define M_ALBUM 20
#define M_CTRL 68
#define M_CTRLGAP 32
#define M_PROGH 9

static lv_obj_t *g_page, *g_art, *g_art_ph, *g_title, *g_artist, *g_album;
static lv_obj_t *g_bar, *g_bar_fill, *g_elapsed, *g_total, *g_play_icon;
static lv_timer_t *g_tick;
static bool g_active;

/* latest media snapshot */
static char g_song[256], g_artist_s[256], g_album_s[256], g_app[128];
static long g_duration_ms, g_play_ms;
static int g_playing;               /* MediaPlayStatus == 1 */
static uint32_t g_play_base_tick;   /* local clock anchor while playing */
static uint8_t *g_art_bytes;        /* decoded base64 (owned) */
static lv_image_dsc_t g_art_dsc;

/* ---- FFT visualiser (tap the artwork to toggle) ---- */
#define RING_MAX (FFT_SIZE * 4)
static bool g_fft_on;
static fft_t *g_fft;
static float g_ring[RING_MAX];
static int g_ring_len;
static int g_sample_rate = 48000;
static float g_bars[FFT_POINTS];       /* smoothed 0..1 */
static float g_target[FFT_POINTS];
static lv_obj_t *g_fft_box, *g_bar_obj[FFT_POINTS];
static lv_timer_t *g_fft_timer;

static void show_artwork(void);
static void set_visualizer(bool on) {
  cJSON *p = cJSON_CreateArray();
  cJSON_AddItemToArray(p, cJSON_CreateBool(on));
  bridge_call("projection.ipc.setVisualizerEnabled", p, NULL, NULL);
}

/* ---- base64 ---- */
static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
static uint8_t *b64decode(const char *in, size_t *out_len) {
  size_t n = strlen(in);
  uint8_t *out = malloc(n / 4 * 3 + 3);
  if (!out) return NULL;
  size_t o = 0;
  int quad[4], qi = 0;
  for (size_t i = 0; i < n; i++) {
    if (in[i] == '=') break;
    int v = b64val(in[i]);
    if (v < 0) continue;
    quad[qi++] = v;
    if (qi == 4) {
      out[o++] = (quad[0] << 2) | (quad[1] >> 4);
      out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
      out[o++] = ((quad[2] & 3) << 6) | quad[3];
      qi = 0;
    }
  }
  if (qi >= 2) {
    out[o++] = (quad[0] << 2) | (quad[1] >> 4);
    if (qi == 3) out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
  }
  *out_len = o;
  return out;
}

/* ---- helpers ---- */
static void fmt_time(long ms, char *buf, size_t n) {
  if (ms < 0) ms = 0;
  long s = ms / 1000;
  snprintf(buf, n, "%ld:%02ld", s / 60, s % 60);
}

static long elapsed_now(void) {
  long e = g_play_ms;
  if (g_playing) e += (long)lv_tick_elaps(g_play_base_tick);
  if (g_duration_ms > 0 && e > g_duration_ms) e = g_duration_ms;
  return e;
}

static void refresh_progress(void) {
  if (!g_bar) return;
  long e = elapsed_now();
  char buf[16];
  fmt_time(e, buf, sizeof buf);
  lv_label_set_text(g_elapsed, buf);
  fmt_time(g_duration_ms, buf, sizeof buf);
  lv_label_set_text(g_total, buf);
  int pct = g_duration_ms > 0 ? (int)(e * 1000 / g_duration_ms) : 0;
  lv_obj_set_width(g_bar_fill, lv_pct(pct / 10)); /* 0..100 % of the track */
  lv_obj_set_style_bg_opa(g_bar_fill, pct > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void refresh_meta(void) {
  if (!g_page) return;
  lv_label_set_text(g_title, g_song[0] ? g_song : "—");
  lv_label_set_text(g_artist, g_artist_s[0] ? g_artist_s : (g_app[0] ? g_app : ""));
  lv_label_set_text(g_album, g_album_s);
  refresh_progress();
}

static void set_play_icon(void) {
  if (!g_play_icon) return;
  char rel[64];
  snprintf(rel, sizeof rel, "A:%s", app_resource(g_playing ? "icons/pause-40.png" : "icons/play-arrow-40.png"));
  lv_image_set_src(g_play_icon, rel);
}

static void fft_apply_visibility(void) {
  if (!g_art || !g_fft_box) return;
  if (g_fft_on) {
    lv_obj_add_flag(g_art, LV_OBJ_FLAG_HIDDEN);
    if (g_art_ph) lv_obj_add_flag(g_art_ph, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_fft_box, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_fft_box, LV_OBJ_FLAG_HIDDEN);
  }
}

static void toggle_fft(lv_event_t *e) { (void)e; media_toggle_fft(); }

void media_toggle_fft(void) {
  g_fft_on = !g_fft_on;
  g_ring_len = 0;
  for (int i = 0; i < FFT_POINTS; i++) g_bars[i] = g_target[i] = 0;
  set_visualizer(g_fft_on);
  if (!g_fft_on) show_artwork();
  fft_apply_visibility();
}

static void show_artwork(void) {
  if (!g_art) return;
  if (g_art_bytes && g_art_dsc.data_size > 0) {
    lv_image_set_src(g_art, &g_art_dsc);
    lv_obj_remove_flag(g_art, LV_OBJ_FLAG_HIDDEN);
    if (g_art_ph) lv_obj_add_flag(g_art_ph, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_art, LV_OBJ_FLAG_HIDDEN);
    if (g_art_ph) lv_obj_remove_flag(g_art_ph, LV_OBJ_FLAG_HIDDEN);
  }
  if (g_fft_on) fft_apply_visibility();
}

/* ---- controls ---- */
static void send_cmd(const char *key) {
  cJSON *p = cJSON_CreateArray();
  cJSON_AddItemToArray(p, cJSON_CreateString(key));
  bridge_call("projection.ipc.sendCommand", p, NULL, NULL);
}
static void on_prev(lv_event_t *e) { (void)e; send_cmd("prev"); }
static void on_next(lv_event_t *e) { (void)e; send_cmd("next"); }
static void on_playpause(lv_event_t *e) {
  (void)e;
  /* optimistic toggle so the icon flips at once */
  g_playing = !g_playing;
  if (g_playing) g_play_base_tick = lv_tick_get();
  else g_play_ms = elapsed_now();
  set_play_icon();
  send_cmd(g_playing ? "play" : "pause");
}

static lv_obj_t *ctrl_button(lv_obj_t *row, const char *icon, int px, lv_event_cb_t cb) {
  lv_obj_t *b = lv_obj_create(row);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, theme_px(M_CTRL), theme_px(M_CTRL));
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  char rel[64];
  snprintf(rel, sizeof rel, "A:%s", app_resource(icon));
  lv_obj_t *img = lv_image_create(b);
  lv_image_set_src(img, rel);
  lv_obj_set_style_image_recolor(img, theme.text, 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_center(img);
  (void)px;
  return img;
}

static void fft_render(lv_timer_t *t) {
  (void)t;
  if (!g_fft_on || !g_fft_box) return;
  int step = FFT_SIZE / 4;
  bool got = false;
  while (g_ring_len >= FFT_SIZE) {
    fft_forward(g_fft, g_ring);
    fft_bins(g_fft, g_target, g_sample_rate);
    got = true;
    memmove(g_ring, g_ring + step, (g_ring_len - step) * sizeof(float));
    g_ring_len -= step;
  }
  if (!got) { /* decay to zero when audio stops */
    for (int i = 0; i < FFT_POINTS; i++) g_target[i] *= 0.85f;
  }
  int32_t h = lv_obj_get_height(g_fft_box);
  for (int i = 0; i < FFT_POINTS; i++) {
    float a = g_target[i];
    g_bars[i] += (a - g_bars[i]) * (a > g_bars[i] ? 0.5f : 0.2f); /* fast attack, slow release */
    int bh = (int)(g_bars[i] * (h - 2)) + 1;
    if (g_bar_obj[i]) lv_obj_set_height(g_bar_obj[i], bh);
  }
}

static void tick_cb(lv_timer_t *t) {
  (void)t;
  if (g_active && g_playing) refresh_progress();
}

/* ---- build ---- */
lv_obj_t *media_create(lv_obj_t *parent) {
  g_page = lv_obj_create(parent);
  lv_obj_remove_style_all(g_page);
  lv_obj_set_size(g_page, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(g_page, theme_px(M_PAD), 0);
  lv_obj_set_style_pad_row(g_page, theme_px(M_PAD), 0);

  /* top: artwork + info */
  lv_obj_t *top = lv_obj_create(g_page);
  lv_obj_remove_style_all(top);
  lv_obj_set_width(top, lv_pct(100));
  lv_obj_set_flex_grow(top, 1);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, theme_px(M_COLGAP), 0);

  int side = 380; /* square artwork within the top row on a 720-tall panel */
  lv_obj_t *artwrap = lv_obj_create(top);
  lv_obj_remove_style_all(artwrap);
  lv_obj_set_size(artwrap, theme_px(side), theme_px(side));
  lv_obj_set_style_radius(artwrap, theme_px(12), 0);
  lv_obj_set_style_clip_corner(artwrap, true, 0);
  lv_obj_set_style_bg_color(artwrap, theme.paper, 0);
  lv_obj_set_style_bg_opa(artwrap, LV_OPA_COVER, 0);
  g_art = lv_image_create(artwrap);
  lv_image_set_inner_align(g_art, LV_IMAGE_ALIGN_CONTAIN);
  lv_obj_set_size(g_art, lv_pct(100), lv_pct(100));
  lv_obj_add_flag(g_art, LV_OBJ_FLAG_HIDDEN);
  g_art_ph = lv_image_create(artwrap);
  {
    char rel[64];
    snprintf(rel, sizeof rel, "A:%s", app_resource("icons/music-note-84.png"));
    lv_image_set_src(g_art_ph, rel);
  }
  lv_obj_set_style_image_recolor(g_art_ph, theme.text2, 0);
  lv_obj_set_style_image_recolor_opa(g_art_ph, LV_OPA_50, 0);
  lv_obj_center(g_art_ph);

  /* FFT spectrum overlay in the same square; 24 bars bottom-anchored */
  g_fft_box = lv_obj_create(artwrap);
  lv_obj_remove_style_all(g_fft_box);
  lv_obj_set_size(g_fft_box, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_all(g_fft_box, theme_px(10), 0);
  lv_obj_set_flex_flow(g_fft_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_fft_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_add_flag(g_fft_box, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < FFT_POINTS; i++) {
    lv_obj_t *b = lv_obj_create(g_fft_box);
    lv_obj_remove_style_all(b);
    lv_obj_set_width(b, lv_pct(100 / FFT_POINTS - 1));
    lv_obj_set_height(b, 1);
    lv_obj_set_style_radius(b, theme_px(2), 0);
    lv_obj_set_style_bg_color(b, theme.highlight, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    g_bar_obj[i] = b;
  }
  /* tap the artwork to switch artwork <-> spectrum */
  lv_obj_add_flag(artwrap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(artwrap, toggle_fft, LV_EVENT_CLICKED, NULL);
  if (!g_fft) g_fft = fft_create();

  lv_obj_t *info = lv_obj_create(top);
  lv_obj_remove_style_all(info);
  lv_obj_set_flex_grow(info, 1);
  lv_obj_set_height(info, lv_pct(100));
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(info, theme_px(8), 0);
  g_title = lv_label_create(info);
  lv_obj_set_style_text_font(g_title, theme_font_bold(theme_px(M_TITLE)), 0);
  lv_obj_set_style_text_color(g_title, theme.text, 0);
  lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_title, lv_pct(100));
  g_artist = lv_label_create(info);
  lv_obj_set_style_text_font(g_artist, theme_font(theme_px(M_ARTIST)), 0);
  lv_obj_set_style_text_color(g_artist, theme.text2, 0);
  lv_label_set_long_mode(g_artist, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_artist, lv_pct(100));
  g_album = lv_label_create(info);
  lv_obj_set_style_text_font(g_album, theme_font(theme_px(M_ALBUM)), 0);
  lv_obj_set_style_text_color(g_album, theme.text2, 0);
  lv_obj_set_style_text_opa(g_album, LV_OPA_70, 0);
  lv_label_set_long_mode(g_album, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_album, lv_pct(100));

  /* dock: controls + progress */
  lv_obj_t *dock = lv_obj_create(g_page);
  lv_obj_remove_style_all(dock);
  lv_obj_set_width(dock, lv_pct(100));
  lv_obj_set_height(dock, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(dock, theme_px(10), 0);

  lv_obj_t *ctrls = lv_obj_create(dock);
  lv_obj_remove_style_all(ctrls);
  lv_obj_set_size(ctrls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ctrls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctrls, theme_px(M_CTRLGAP), 0);
  ctrl_button(ctrls, "icons/skip-prev-40.png", 40, on_prev);
  g_play_icon = ctrl_button(ctrls, "icons/play-arrow-40.png", 40, on_playpause);
  ctrl_button(ctrls, "icons/skip-next-40.png", 40, on_next);

  lv_obj_t *prow = lv_obj_create(dock);
  lv_obj_remove_style_all(prow);
  lv_obj_set_width(prow, lv_pct(100));
  lv_obj_set_height(prow, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(prow, theme_px(10), 0);
  g_elapsed = lv_label_create(prow);
  lv_obj_set_style_text_font(g_elapsed, theme_font(theme_px(14)), 0);
  lv_obj_set_style_text_color(g_elapsed, theme.text2, 0);
  lv_label_set_text(g_elapsed, "0:00");
  g_bar = lv_obj_create(prow);
  lv_obj_remove_style_all(g_bar);
  lv_obj_set_flex_grow(g_bar, 1);
  lv_obj_set_height(g_bar, theme_px(M_PROGH));
  lv_obj_set_style_radius(g_bar, theme_px(M_PROGH / 2), 0);
  lv_obj_set_style_bg_color(g_bar, theme.divider, 0);
  lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
  g_bar_fill = lv_obj_create(g_bar);
  lv_obj_remove_style_all(g_bar_fill);
  lv_obj_set_height(g_bar_fill, lv_pct(100));
  lv_obj_set_width(g_bar_fill, lv_pct(0));
  lv_obj_set_style_radius(g_bar_fill, theme_px(M_PROGH / 2), 0);
  lv_obj_set_style_bg_color(g_bar_fill, theme.primary, 0);
  lv_obj_set_style_bg_opa(g_bar_fill, LV_OPA_COVER, 0);
  g_total = lv_label_create(prow);
  lv_obj_set_style_text_font(g_total, theme_font(theme_px(14)), 0);
  lv_obj_set_style_text_color(g_total, theme.text2, 0);
  lv_label_set_text(g_total, "0:00");

  set_play_icon();
  refresh_meta();
  show_artwork();
  if (!g_tick) g_tick = lv_timer_create(tick_cb, 500, NULL);
  if (!g_fft_timer) g_fft_timer = lv_timer_create(fft_render, 33, NULL);
  return g_page;
}

void media_destroy(void) {
  if (g_fft_on) { set_visualizer(false); g_fft_on = false; }
  g_page = g_art = g_art_ph = g_title = g_artist = g_album = NULL;
  g_bar = g_bar_fill = g_elapsed = g_total = g_play_icon = NULL;
  g_fft_box = NULL;
  for (int i = 0; i < FFT_POINTS; i++) g_bar_obj[i] = NULL;
}

void media_set_active(bool active) {
  g_active = active;
  if (!active && g_fft_on) {
    g_fft_on = false;
    set_visualizer(false);
    show_artwork();
    fft_apply_visibility();
  }
}

/* ---- data ---- */
static const char *bag_str(cJSON *media, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(media, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static void cpy(char *dst, size_t n, const char *s) { snprintf(dst, n, "%s", s ? s : ""); }

static void feed_pcm(cJSON *ev) {
  cJSON *sr = cJSON_GetObjectItemCaseSensitive(ev, "sampleRate");
  if (cJSON_IsNumber(sr) && sr->valuedouble > 0) g_sample_rate = (int)sr->valuedouble;
  cJSON *chunk = cJSON_GetObjectItemCaseSensitive(ev, "chunk");
  cJSON *b = cJSON_IsObject(chunk) ? cJSON_GetObjectItemCaseSensitive(chunk, "$bytes") : NULL;
  if (!cJSON_IsString(b)) return;
  size_t len = 0;
  uint8_t *bytes = b64decode(b->valuestring, &len);
  if (!bytes) return;
  const int16_t *pcm = (const int16_t *)bytes;
  size_t n = len / 2;
  for (size_t i = 0; i < n; i++) {
    if (g_ring_len >= RING_MAX) {
      memmove(g_ring, g_ring + FFT_SIZE, (g_ring_len - FFT_SIZE) * sizeof(float));
      g_ring_len -= FFT_SIZE;
    }
    g_ring[g_ring_len++] = pcm[i] / 32768.0f;
  }
  free(bytes);
}

void media_on_event(const char *channel, cJSON *args) {
  if (strcmp(channel, "projection-audio-chunk") == 0) {
    if (g_fft_on) feed_pcm(cJSON_GetArrayItem(args, 0));
    return;
  }
  if (strcmp(channel, "projection-event") != 0) return;
  cJSON *ev = cJSON_GetArrayItem(args, 0);
  cJSON *type = cJSON_IsObject(ev) ? cJSON_GetObjectItemCaseSensitive(ev, "type") : NULL;
  if (!cJSON_IsString(type)) return;
  if (strcmp(type->valuestring, "media-reset") == 0) {
    g_song[0] = g_artist_s[0] = g_album_s[0] = g_app[0] = 0;
    g_duration_ms = g_play_ms = 0;
    g_playing = 0;
    free(g_art_bytes);
    g_art_bytes = NULL;
    g_art_dsc.data_size = 0;
    refresh_meta();
    show_artwork();
    return;
  }
  if (strcmp(type->valuestring, "media") != 0) return;
  /* { type:'media', payload:{ payload:{ media:{...}, base64Image } } } */
  cJSON *p1 = cJSON_GetObjectItemCaseSensitive(ev, "payload");
  cJSON *pl = cJSON_IsObject(p1) ? cJSON_GetObjectItemCaseSensitive(p1, "payload") : NULL;
  if (!cJSON_IsObject(pl)) return;
  cJSON *media = cJSON_GetObjectItemCaseSensitive(pl, "media");
  if (cJSON_IsObject(media)) {
    const char *s;
    if ((s = bag_str(media, "MediaSongName"))) cpy(g_song, sizeof g_song, s);
    if ((s = bag_str(media, "MediaArtistName"))) cpy(g_artist_s, sizeof g_artist_s, s);
    if ((s = bag_str(media, "MediaAlbumName"))) cpy(g_album_s, sizeof g_album_s, s);
    if ((s = bag_str(media, "MediaAPPName"))) cpy(g_app, sizeof g_app, s);
    cJSON *dur = cJSON_GetObjectItemCaseSensitive(media, "MediaSongDuration");
    if (cJSON_IsNumber(dur)) g_duration_ms = (long)dur->valuedouble;
    cJSON *ply = cJSON_GetObjectItemCaseSensitive(media, "MediaSongPlayTime");
    if (cJSON_IsNumber(ply)) { g_play_ms = (long)ply->valuedouble; g_play_base_tick = lv_tick_get(); }
    cJSON *st = cJSON_GetObjectItemCaseSensitive(media, "MediaPlayStatus");
    if (cJSON_IsNumber(st)) {
      int np = st->valuedouble == 1 ? 1 : 0;
      if (np && !g_playing) g_play_base_tick = lv_tick_get();
      if (!np && g_playing) g_play_ms = elapsed_now();
      g_playing = np;
    }
  }
  cJSON *img = cJSON_GetObjectItemCaseSensitive(pl, "base64Image");
  if (cJSON_IsString(img) && img->valuestring[0]) {
    size_t len = 0;
    uint8_t *bytes = b64decode(img->valuestring, &len);
    if (bytes && len > 0) {
      free(g_art_bytes);
      g_art_bytes = bytes;
      memset(&g_art_dsc, 0, sizeof g_art_dsc);
      g_art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      g_art_dsc.header.cf = LV_COLOR_FORMAT_RAW;
      g_art_dsc.data = g_art_bytes;
      g_art_dsc.data_size = (uint32_t)len;
    } else {
      free(bytes);
    }
  }
  set_play_icon();
  refresh_meta();
  show_artwork();
}
