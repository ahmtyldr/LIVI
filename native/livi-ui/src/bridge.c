#include "bridge.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "app.h"

#define MAX_PENDING 64
#define MAX_LINE (8u << 20)

typedef struct {
  int id;
  bridge_result_cb cb;
  void *user;
} pending_t;

static char g_path[108]; /* sizeof sockaddr_un.sun_path */
static int g_fd = -1;
static int g_next_id = 1;
static pending_t g_pending[MAX_PENDING];
static char *g_rx;
static size_t g_rx_len, g_rx_cap;
static time_t g_last_attempt;
static bridge_event_cb g_event_cb;
static void *g_event_user;
static bridge_state_cb g_state_cb;
static void *g_state_user;
static unsigned g_events;
static char g_last_event[64];

void bridge_init(const char *socket_path) {
  snprintf(g_path, sizeof g_path, "%s", socket_path);
  for (int i = 0; i < MAX_PENDING; i++) g_pending[i].id = 0;
}

void bridge_set_event_handler(bridge_event_cb cb, void *user) {
  g_event_cb = cb;
  g_event_user = user;
}

void bridge_set_state_handler(bridge_state_cb cb, void *user) {
  g_state_cb = cb;
  g_state_user = user;
}

int bridge_fd(void) { return g_fd; }
bool bridge_connected(void) { return g_fd >= 0; }
unsigned bridge_events_received(void) { return g_events; }
const char *bridge_last_event(void) { return g_last_event; }

static void fail_pending(void) {
  for (int i = 0; i < MAX_PENDING; i++) {
    if (!g_pending[i].id) continue;
    pending_t p = g_pending[i];
    g_pending[i].id = 0;
    if (p.cb) {
      cJSON *err = cJSON_CreateObject();
      cJSON_AddNumberToObject(err, "code", -32000);
      cJSON_AddStringToObject(err, "message", "bridge disconnected");
      p.cb(NULL, err, p.user);
      cJSON_Delete(err);
    }
  }
}

void bridge_close(void) {
  if (g_fd < 0) return;
  close(g_fd);
  g_fd = -1;
  g_rx_len = 0;
  fail_pending();
  LOG("bridge disconnected");
  if (g_state_cb) g_state_cb(false, g_state_user);
}

static void try_connect(void) {
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof addr.sun_path, "%s", g_path);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(fd);
    return;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  g_fd = fd;
  g_rx_len = 0;
  LOG("bridge connected: %s", g_path);
  if (g_state_cb) g_state_cb(true, g_state_user);
}

void bridge_tick(void) {
  if (g_fd >= 0) return;
  time_t now = time(NULL);
  if (now == g_last_attempt) return;
  g_last_attempt = now;
  try_connect();
}

static bool write_all(const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(g_fd, buf, len);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct pollfd p = {.fd = g_fd, .events = POLLOUT};
        if (poll(&p, 1, 2000) <= 0) return false;
        continue;
      }
      return false;
    }
    buf += n;
    len -= (size_t)n;
  }
  return true;
}

int bridge_call(const char *method, cJSON *params, bridge_result_cb cb, void *user) {
  if (g_fd < 0) {
    cJSON_Delete(params);
    return -1;
  }
  int slot = -1;
  if (cb) {
    for (int i = 0; i < MAX_PENDING; i++)
      if (!g_pending[i].id) {
        slot = i;
        break;
      }
    if (slot < 0) {
      LOG("too many pending calls, dropping %s", method);
      cJSON_Delete(params);
      return -1;
    }
  }
  int id = g_next_id++;
  cJSON *req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(req, "id", id);
  cJSON_AddStringToObject(req, "method", method);
  cJSON_AddItemToObject(req, "params", params ? params : cJSON_CreateArray());
  char *text = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);
  if (!text) return -1;
  size_t len = strlen(text);
  text[len] = '\n'; /* cJSON leaves room: it allocates len+1 for the NUL */
  bool ok = write_all(text, len + 1);
  free(text);
  if (!ok) {
    bridge_close();
    return -1;
  }
  if (slot >= 0) {
    g_pending[slot].id = id;
    g_pending[slot].cb = cb;
    g_pending[slot].user = user;
  }
  return id;
}

static void dispatch(const char *line, size_t len) {
  cJSON *msg = cJSON_ParseWithLength(line, len);
  if (!msg) {
    LOG("bad frame (%zu bytes)", len);
    return;
  }
  cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
  cJSON *method = cJSON_GetObjectItemCaseSensitive(msg, "method");
  if (cJSON_IsNumber(id)) {
    int want = (int)id->valuedouble;
    for (int i = 0; i < MAX_PENDING; i++) {
      if (g_pending[i].id != want) continue;
      pending_t p = g_pending[i];
      g_pending[i].id = 0;
      cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
      cJSON *error = cJSON_GetObjectItemCaseSensitive(msg, "error");
      if (error) {
        cJSON *m = cJSON_GetObjectItemCaseSensitive(error, "message");
        LOG("rpc %d failed: %s", want, cJSON_IsString(m) ? m->valuestring : "?");
      }
      if (p.cb) p.cb(result, error, p.user);
      break;
    }
  } else if (cJSON_IsString(method) && strcmp(method->valuestring, "event") == 0) {
    cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "params");
    cJSON *channel = cJSON_GetObjectItemCaseSensitive(params, "channel");
    cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "args");
    if (cJSON_IsString(channel)) {
      g_events++;
      snprintf(g_last_event, sizeof g_last_event, "%s", channel->valuestring);
      if (g_event_cb) g_event_cb(channel->valuestring, args, g_event_user);
    }
  }
  cJSON_Delete(msg);
}

void bridge_read(void) {
  if (g_fd < 0) return;
  for (;;) {
    if (g_rx_cap - g_rx_len < 65536) {
      size_t cap = g_rx_cap ? g_rx_cap * 2 : 262144;
      if (cap > MAX_LINE) {
        LOG("frame over %u bytes, dropping connection", MAX_LINE);
        bridge_close();
        return;
      }
      char *nb = realloc(g_rx, cap);
      if (!nb) {
        bridge_close();
        return;
      }
      g_rx = nb;
      g_rx_cap = cap;
    }
    ssize_t n = read(g_fd, g_rx + g_rx_len, g_rx_cap - g_rx_len);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      bridge_close();
      return;
    }
    if (n == 0) {
      bridge_close();
      return;
    }
    g_rx_len += (size_t)n;
    /* dispatch complete lines */
    size_t start = 0;
    for (size_t i = 0; i < g_rx_len; i++) {
      if (g_rx[i] != '\n') continue;
      if (i > start) dispatch(g_rx + start, i - start);
      start = i + 1;
      if (g_fd < 0) return; /* a handler closed us */
    }
    if (start > 0) {
      memmove(g_rx, g_rx + start, g_rx_len - start);
      g_rx_len -= start;
    }
  }
}
