#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "app.h"
#include "cJSON.h"

static cJSON *g_active;
static cJSON *g_en;
static char g_lang[16] = "en";

static cJSON *load_file(const char *lang) {
  char rel[PATH_MAX + 2];
  snprintf(rel, sizeof rel, "locales/%s.json", lang);
  FILE *f = fopen(app_resource(rel), "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fclose(f);
    return NULL;
  }
  char *buf = malloc((size_t)size);
  size_t got = buf ? fread(buf, 1, (size_t)size, f) : 0;
  fclose(f);
  cJSON *root = got == (size_t)size ? cJSON_ParseWithLength(buf, got) : NULL;
  free(buf);
  return root;
}

void i18n_load(const char *lang) {
  if (!lang || !*lang) lang = "en";
  snprintf(g_lang, sizeof g_lang, "%s", lang);
  if (!g_en) g_en = load_file("en");
  if (g_active && g_active != g_en) cJSON_Delete(g_active);
  g_active = strcmp(lang, "en") == 0 ? g_en : load_file(lang);
  if (!g_active) {
    LOG("locale %s not found, using en", lang);
    g_active = g_en;
  }
  if (!g_en) LOG("no locales under %s", app_resource_dir());
}

static const char *lookup(cJSON *root, const char *key) {
  if (!root) return NULL;
  char buf[128];
  snprintf(buf, sizeof buf, "%s", key);
  cJSON *node = root;
  char *save = NULL;
  for (char *part = strtok_r(buf, ".", &save); part; part = strtok_r(NULL, ".", &save)) {
    node = cJSON_GetObjectItemCaseSensitive(node, part);
    if (!node) return NULL;
  }
  return cJSON_IsString(node) ? node->valuestring : NULL;
}

const char *i18n(const char *key) {
  const char *s = lookup(g_active, key);
  if (!s) s = lookup(g_en, key);
  return s ? s : key;
}

const char *i18n_language(void) { return g_lang; }
