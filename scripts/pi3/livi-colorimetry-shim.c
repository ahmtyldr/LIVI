/* LIVI Pi 3 shim: h264parse reports the phone's colorimetry as "2:4:16:3",
 * which the bcm2835 v4l2 decoder's probed caps do not list, so the pipeline
 * fails with not-negotiated. This LD_PRELOAD wrapper rewrites that one value
 * to "bt709", which the decoder accepts. Loaded into livi-gst-host only via
 * LIVI_GST_PRELOAD. Override the mapping with LIVI_COLORIMETRY_MAP="from=to". */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char *(*to_string_fn)(const void *);
typedef char *(*g_strdup_fn)(const char *);
typedef void (*g_free_fn)(void *);

char *gst_video_colorimetry_to_string(const void *cinfo)
{
    static to_string_fn real = NULL;
    static g_strdup_fn gdup = NULL;
    static g_free_fn gfree = NULL;
    static const char *from = "2:4:16:3", *to = "bt709";
    static int init = 0;
    if (!init) {
        init = 1;
        real = (to_string_fn)dlsym(RTLD_NEXT, "gst_video_colorimetry_to_string");
        gdup = (g_strdup_fn)dlsym(RTLD_DEFAULT, "g_strdup");
        gfree = (g_free_fn)dlsym(RTLD_DEFAULT, "g_free");
        const char *m = getenv("LIVI_COLORIMETRY_MAP");
        if (m && strchr(m, '=')) {
            char *dup = strdup(m); char *eq = strchr(dup, '=');
            *eq = 0; from = dup; to = eq + 1;
        }
        fprintf(stderr, "[colorimetry-shim] active: %s -> %s\n", from, to);
    }
    char *s = real ? real(cinfo) : NULL;
    if (s && gdup && gfree && strcmp(s, from) == 0) {
        fprintf(stderr, "[colorimetry-shim] rewrote colorimetry %s -> %s\n", s, to);
        gfree(s);
        return gdup(to);
    }
    return s;
}
