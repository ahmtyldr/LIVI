// SettingsLayout.tsx + SettingsPage.tsx: a schema-driven settings tree
// (contracts/settings-schema.json). A header (back, bold title) over a card
// of 52 px rows; route rows push a sub-view, control rows edit config and
// save via projection.settings.save.
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "lvgl.h"

lv_obj_t *settings_create(lv_obj_t *parent);
void settings_destroy(void);
/** Enters the tree at a /settings/... route (e.g. "devices", "audio"). */
void settings_show_route(const char *tail);
/** True while a view deeper than the root is shown (shell keeps Settings tab). */
bool settings_at_root(void);
/** New config from settings.get / the 'settings' event: refreshes values. */
void settings_on_config(cJSON *config);
void settings_on_event(const char *channel, cJSON *args);

/* Layout constants for 1280x720 (SettingsLayout.tsx clamp() values). */
#define SET_PAD_L 19
#define SET_PAD_T 16
#define SET_HEADER_H 40
#define SET_HEADER_MB 12
#define SET_ROW_H 52
#define SET_ROW_PAD 14
#define SET_ROW_GAP 19
#define SET_ROW_ICON 26
#define SET_ROW_INSET (SET_ROW_PAD + SET_ROW_ICON + SET_ROW_GAP + SET_ROW_PAD)
#define SET_SLOT_L 56
#define SET_ICON_PX 23
#define SET_TITLE_PX 26
#define SET_CARD_PAD 8
#define SET_CARD_RADIUS 12

/** A settings row shell: 52 px, inset 2 px divider unless `last`. */
lv_obj_t *settings_row(lv_obj_t *card, bool last);
/** Standard label-side content: icon + text; returns the row. */
lv_obj_t *settings_row_label(lv_obj_t *row, const char *icon, const char *text);
void settings_row_chevron(lv_obj_t *row);
/** Saves one config key over the bridge (projection.settings.save). */
void settings_save(const char *path, cJSON *value);
