// Locale strings from contracts/locales/<lang>.json (nested objects, dotted keys).
#pragma once

void i18n_load(const char *lang);
/** Looks `key` up in the active locale, then English; returns `key` when missing.
 *  The returned pointer is valid until the next i18n_load(). */
const char *i18n(const char *key);
const char *i18n_language(void);
