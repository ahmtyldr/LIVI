// The projection (home) page: the transparent surface the video plane shows
// through, Projection.tsx's StatusOverlay while nothing is streaming, the
// ViewAreaMask bars, and touch forwarding with the Android Auto letterbox
// transform (useProjectionTouch.ts).
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "lvgl.h"

lv_obj_t *projection_create(lv_obj_t *parent);
void projection_destroy(void);
void projection_set_streaming(bool streaming);
void projection_on_settings(cJSON *config);
void projection_on_event(const char *channel, cJSON *args);
