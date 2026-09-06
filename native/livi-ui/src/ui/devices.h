// Settings → Devices (Devices.tsx + SettingsDeviceRow.tsx): the unified
// device picker fed by projection.ipc.getDevices and the 'devices' event.
#pragma once
#include "cJSON.h"
#include "lvgl.h"

/** Builds the rows into `card` from the cached list. */
void devices_build(lv_obj_t *card);
void devices_destroy(void);
/** Re-fetches the list over the bridge (on entering the view). */
void devices_refresh(void);
void devices_on_event(const char *channel, cJSON *args);
