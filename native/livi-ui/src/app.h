// Process-wide bits shared by the livi-ui sources.
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include "lvgl.h"

/** Directory holding fonts/ and locales/ (LIVI_UI_RESOURCES or the binary's dir). */
const char *app_resource_dir(void);
/** Path of a resource, e.g. app_resource("fonts/roboto-latin-400-normal.woff"). */
const char *app_resource(const char *rel);
/** app_resource with three concatenated parts: dir+name+suffix. */
const char *app_resource_join(const char *a, const char *b, const char *c);
/** Asks the main loop to stop. */
void app_request_exit(void);
/** Version string of the main process ("" until app.getVersion answered). */
const char *app_main_version(void);

#define LOG(...)                        \
  do {                                  \
    fprintf(stderr, "[livi-ui] ");      \
    fprintf(stderr, __VA_ARGS__);       \
    fputc('\n', stderr);                \
  } while (0)
