// SettingsLayout.tsx + SettingsPage.tsx: header (back, bold title, apply
// slot) over a card of 52 px rows. Session 7 ships the root menu and the
// Devices view; the schema-driven groups follow in session 8.
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "lvgl.h"

typedef enum { SETTINGS_VIEW_ROOT = 0, SETTINGS_VIEW_DEVICES } settings_view_t;

lv_obj_t *settings_create(lv_obj_t *parent);
void settings_destroy(void);
void settings_show_view(settings_view_t view);
settings_view_t settings_current_view(void);
/** Maps the tail of a /settings/... route to a view. */
settings_view_t settings_view_from_route(const char *tail);
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
#define SET_SLOT_R 100
#define SET_ICON_PX 23
#define SET_TITLE_PX 26
#define SET_CARD_PAD 8    /* Stack padding 0.5rem */
#define SET_CARD_RADIUS 12

/** A settings row shell: 52 px, inset 2 px divider unless `last`. The caller
 *  adds children; the row is a horizontal flex container. */
lv_obj_t *settings_row(lv_obj_t *card, bool last);
/** Standard label-side content: icon + text; returns the label. */
lv_obj_t *settings_row_label(lv_obj_t *row, const char *icon, const char *text);
/** Right-side chevron. */
void settings_row_chevron(lv_obj_t *row);
