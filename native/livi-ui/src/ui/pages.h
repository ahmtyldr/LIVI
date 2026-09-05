// Page objects living inside the shell's content area. Session 5 ships the
// frame and a status page; the real screens follow in sessions 7–16.
#pragma once
#include "cJSON.h"
#include "lvgl.h"
#include "ui/shell.h"

void pages_create(lv_obj_t *parent);
void pages_destroy(void);
lv_obj_t *pages_get(page_id_t id);
/** New settings object from projection.settings.get / the `settings` event. */
void pages_on_settings(cJSON *config);
/** A mirrored renderer event. */
void pages_on_event(const char *channel, cJSON *args);
/** Bridge connection state changed. */
void pages_on_bridge(bool connected);
