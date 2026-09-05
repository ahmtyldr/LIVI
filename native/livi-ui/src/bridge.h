// JSON-RPC 2.0 client for LIVI's UiBridge: newline-delimited JSON over a
// Unix socket. Requests carry a numeric id; the server answers with result
// or error; renderer events arrive as {"method":"event","params":{channel,args}}.
#pragma once
#include <stdbool.h>
#include "cJSON.h"

typedef void (*bridge_result_cb)(cJSON *result, cJSON *error, void *user);
typedef void (*bridge_event_cb)(const char *channel, cJSON *args, void *user);
typedef void (*bridge_state_cb)(bool connected, void *user);

void bridge_init(const char *socket_path);
void bridge_set_event_handler(bridge_event_cb cb, void *user);
void bridge_set_state_handler(bridge_state_cb cb, void *user);

int bridge_fd(void);        /* -1 while disconnected */
bool bridge_connected(void);
void bridge_tick(void);     /* reconnects when needed; call about once a second */
void bridge_read(void);     /* drain the socket; call when the fd is readable */
void bridge_close(void);

/** Sends a request. `params` is a JSON array (ownership taken) or NULL.
 *  `cb` may be NULL when the answer does not matter. Returns the id, -1 on failure. */
int bridge_call(const char *method, cJSON *params, bridge_result_cb cb, void *user);

/* Counters for the status page. */
unsigned bridge_events_received(void);
const char *bridge_last_event(void);
