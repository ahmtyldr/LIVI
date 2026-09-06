// The Media (now playing) page: artwork, title/artist/album, a progress bar
// and prev/play-pause/next controls, fed by the projection 'media' event
// (Media.tsx + MediaInfoChannel). FFT visualiser follows in a later session.
#pragma once
#include "cJSON.h"
#include "lvgl.h"

lv_obj_t *media_create(lv_obj_t *parent);
void media_destroy(void);
void media_on_event(const char *channel, cJSON *args);
/** Called when the Media page becomes visible/hidden (drives the clock). */
void media_set_active(bool active);
/** Tooling: toggle the artwork<->spectrum view (LIVI_UI_CTL "fft"). */
void media_toggle_fft(void);
