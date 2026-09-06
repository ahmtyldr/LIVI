// livi-ui entry: one fullscreen Wayland window titled dev.f-io.livi (the
// compositor's main-screen app id), LVGL drawing into shm buffers, and the
// JSON-RPC bridge on a second poll fd.
//
// Environment:
//   XDG_RUNTIME_DIR      bridge socket lives at $XDG_RUNTIME_DIR/livi-ui.sock
//   LIVI_UI_SOCKET       explicit socket path (overrides the above)
//   LIVI_UI_RESOURCES    fonts/ and locales/ (default: next to the binary)
//   LIVI_UI_SIZE         WxH of the initial window (default 1280x720; fullscreen follows)
//   LIVI_UI_TITLE        window title/app id (default dev.f-io.livi)
//   LIVI_UI_CTL          control FIFO for tooling: "page /media\n" switches pages
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "bridge.h"
#include "i18n.h"
#include "lvgl.h"
#include "theme.h"
#include "ui/pages.h"
#include "ui/shell.h"

static char g_res_dir[PATH_MAX];
static char g_res_path[PATH_MAX];
static char g_version[32] = "";
static bool g_running = true;
static lv_display_t *g_disp;
static int g_ctl_fd = -1;

/* Development control FIFO (tools/parity/capture.sh). Opened O_RDWR so the
 * read end never sees EOF between writers. */
static void ctl_open(void) {
  const char *path = getenv("LIVI_UI_CTL");
  if (!path || !*path) return;
  if (mkfifo(path, 0600) != 0 && errno != EEXIST) {
    LOG("mkfifo %s: %s", path, strerror(errno));
    return;
  }
  g_ctl_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (g_ctl_fd < 0) LOG("open %s: %s", path, strerror(errno));
}

static void ctl_read(void) {
  char buf[256];
  ssize_t n = read(g_ctl_fd, buf, sizeof buf - 1);
  if (n <= 0) return;
  buf[n] = 0;
  char *save = NULL;
  for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    if (strncmp(line, "page ", 5) == 0) {
      shell_show_route(line + 5);
      LOG("ctl: page %s", line + 5);
    } else if (strcmp(line, "fft") == 0) {
      pages_toggle_fft();
    } else if (strcmp(line, "quit") == 0) {
      g_running = false;
    } else {
      LOG("ctl: unknown command '%s'", line);
    }
  }
}

const char *app_resource_dir(void) { return g_res_dir; }
const char *app_resource(const char *rel) {
  snprintf(g_res_path, sizeof g_res_path, "%s/%s", g_res_dir, rel);
  return g_res_path;
}
const char *app_resource_join(const char *a, const char *b, const char *c) {
  snprintf(g_res_path, sizeof g_res_path, "%s/%s%s%s", g_res_dir, a, b, c);
  return g_res_path;
}
void app_request_exit(void) { g_running = false; }
const char *app_main_version(void) { return g_version; }

static uint32_t tick_cb(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static bool on_close(lv_display_t *disp) {
  (void)disp;
  LOG("window closed by the compositor");
  g_running = false;
  return true;
}

static void resolve_resource_dir(void) {
  const char *env = getenv("LIVI_UI_RESOURCES");
  if (env && *env) {
    snprintf(g_res_dir, sizeof g_res_dir, "%s", env);
    return;
  }
  char exe[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n > 0) {
    exe[n] = 0;
    snprintf(g_res_dir, sizeof g_res_dir, "%s", dirname(exe));
  } else {
    snprintf(g_res_dir, sizeof g_res_dir, ".");
  }
}

/* ---- bridge callbacks ---- */

static void on_version(cJSON *result, cJSON *error, void *user) {
  (void)error; (void)user;
  if (cJSON_IsString(result)) snprintf(g_version, sizeof g_version, "%s", result->valuestring);
}

static void apply_settings(cJSON *config) {
  if (!cJSON_IsObject(config)) return;
  cJSON *lang = cJSON_GetObjectItemCaseSensitive(config, "language");
  const char *want = cJSON_IsString(lang) ? lang->valuestring : "en";
  bool relayout = strcmp(want, i18n_language()) != 0;
  int zoom = theme.zoom;
  bool dark = theme.dark;
  theme_apply(config);
  relayout = relayout || zoom != theme.zoom || dark != theme.dark;
  relayout = shell_set_config(config) || relayout;
  if (relayout) i18n_load(want);
  pages_on_settings(config);
  if (relayout) shell_rebuild();
}

static void on_settings(cJSON *result, cJSON *error, void *user) {
  (void)error; (void)user;
  if (!cJSON_IsObject(result)) return;
  apply_settings(result);
  static bool first = true;
  if (first) {
    first = false;
    cJSON *start = cJSON_GetObjectItemCaseSensitive(result, "startPage");
    shell_show_route(cJSON_IsString(start) ? start->valuestring : "/");
  }
}

static void on_event(const char *channel, cJSON *args, void *user) {
  (void)user;
  if (strcmp(channel, "settings") == 0) {
    apply_settings(cJSON_GetArrayItem(args, 0));
    return;
  }
  pages_on_event(channel, args);
}

static void on_bridge_state(bool connected, void *user) {
  (void)user;
  pages_on_bridge(connected);
  if (!connected) return;
  bridge_call("app.getVersion", NULL, on_version, NULL);
  bridge_call("projection.settings.get", NULL, on_settings, NULL);
  cJSON *params = cJSON_CreateArray();
  cJSON_AddItemToArray(params, cJSON_CreateBool(shell_current_page() == PAGE_HOME));
  bridge_call("projection.ipc.setVisible", params, NULL, NULL);
}

int main(void) {
  resolve_resource_dir();
  char sock[PATH_MAX];
  const char *explicit = getenv("LIVI_UI_SOCKET");
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (explicit && *explicit) snprintf(sock, sizeof sock, "%s", explicit);
  else snprintf(sock, sizeof sock, "%s/livi-ui.sock", runtime && *runtime ? runtime : "/tmp");
  const char *title = getenv("LIVI_UI_TITLE");
  if (!title || !*title) title = "dev.f-io.livi";
  unsigned w = 1280, h = 720;
  const char *size = getenv("LIVI_UI_SIZE");
  if (size && sscanf(size, "%ux%u", &w, &h) != 2) { w = 1280; h = 720; }

  LOG("starting: resources=%s socket=%s title=%s", g_res_dir, sock, title);
  lv_init();
  lv_tick_set_cb(tick_cb);
  /* the driver connects to $WAYLAND_DISPLAY itself inside window_create */
  char title_buf[64];
  snprintf(title_buf, sizeof title_buf, "%s", title);
  g_disp = lv_wayland_window_create(w, h, title_buf, on_close);
  if (!g_disp) {
    LOG("cannot create window");
    return 1;
  }
  /* Ack the initial configure before anything else can queue a second one
   * (the driver acks out of order otherwise, which Smithay rejects). */
  lv_wayland_timer_handler();
  /* The compositor sizes UI toplevels itself (layout::apply_ui_layout);
   * LIVI_UI_FULLSCREEN=1 additionally requests xdg fullscreen. */
  const char *fs = getenv("LIVI_UI_FULLSCREEN");
  if (fs && *fs == '1') lv_wayland_window_set_fullscreen(g_disp, true);

  theme_init();
  i18n_load("en");
  shell_create();

  bridge_init(sock);
  bridge_set_event_handler(on_event, NULL);
  bridge_set_state_handler(on_bridge_state, NULL);
  bridge_tick();
  ctl_open();

  uint32_t last_tick = tick_cb();
  while (g_running && lv_wayland_window_is_open(g_disp)) {
    uint32_t wait = lv_wayland_timer_handler();
    if (wait > 100) wait = 100;
    struct pollfd fds[3];
    int n = 0;
    int bridge_idx = -1, ctl_idx = -1;
    fds[n].fd = lv_wayland_get_fd();
    fds[n].events = POLLIN;
    n++;
    int bfd = bridge_fd();
    if (bfd >= 0) {
      bridge_idx = n;
      fds[n].fd = bfd;
      fds[n].events = POLLIN;
      n++;
    }
    if (g_ctl_fd >= 0) {
      ctl_idx = n;
      fds[n].fd = g_ctl_fd;
      fds[n].events = POLLIN;
      n++;
    }
    int r = poll(fds, (nfds_t)n, (int)wait);
    if (r > 0 && (fds[0].revents & (POLLHUP | POLLERR))) {
      LOG("wayland connection lost");
      return 1;
    }
    if (r > 0 && bridge_idx >= 0 && (fds[bridge_idx].revents & (POLLIN | POLLHUP | POLLERR))) bridge_read();
    if (r > 0 && ctl_idx >= 0 && (fds[ctl_idx].revents & POLLIN)) ctl_read();
    uint32_t now = tick_cb();
    if (now - last_tick >= 1000) {
      last_tick = now;
      bridge_tick();
    }
  }
  LOG("exiting");
  bridge_close();
  return 0;
}
