// The application shell: vertical nav rail (clock + tabs) on the left, the
// active page on the right — the LVGL counterpart of components/navigation.
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "lvgl.h"

typedef enum {
  PAGE_HOME = 0,  /* "/"        projection */
  PAGE_TELEMETRY, /* /telemetry */
  PAGE_MEDIA,     /* /media */
  PAGE_CAMERA,    /* /camera */
  PAGE_SETTINGS,  /* /settings */
  PAGE_COUNT
} page_id_t;

void shell_create(void);
/** Tears the screen down and rebuilds it with the current theme/locale. */
void shell_rebuild(void);
void shell_show_page(page_id_t id);
page_id_t shell_current_page(void);
/** Route string as the React app names it, for `startPage` matching. */
page_id_t shell_page_from_route(const char *route);
/** Nav.tsx: while the projection video is shown the rail leaves the home page. */
void shell_set_streaming(bool streaming);
bool shell_streaming(void);
