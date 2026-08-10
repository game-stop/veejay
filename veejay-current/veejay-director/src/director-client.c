/* director - Linux VeeJay - GVeejay GTK+-3/Glade User Interface 
 * (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include "director-client.h"
#include "director-compat.h"
#include "director-wire.h"

#include <stdio.h>
#include <string.h>

#ifndef VJ_CMD_PORT
#define VJ_CMD_PORT 0
#endif

#define DIRECTOR_QUERY_INTERVAL_US 1000000
#define DIRECTOR_RECONNECT_INITIAL_US 1000000LL
#define DIRECTOR_RECONNECT_MAX_US 30000000LL
#define DIRECTOR_FAILURE_EXPIRE_US 300000000LL
#define DIRECTOR_CONNECT_TIMEOUT_MS 1000
#define DIRECTOR_SEND_TIMEOUT_MS 600
#define DIRECTOR_RESPONSE_MAX 65536
#define DIRECTOR_RESPONSE_IDLE_MS 60
#define DIRECTOR_RESPONSE_TIMEOUT_MS 600

typedef enum {
    DIRECTOR_REQUEST_COMMAND = 0,
    DIRECTOR_REQUEST_REFRESH,
    DIRECTOR_REQUEST_QUERY_FRAMED,
    DIRECTOR_REQUEST_STOP
} DirectorRequestType;

typedef struct {
    DirectorRequestType type;
    gchar *command;
    DirectorClientEvent event;
    gsize header_digits;
} DirectorRequest;

typedef struct {
    DirectorClient *client;
    DirectorClientEvent event;
    gchar *payload;
} DirectorDispatch;

struct _DirectorClient {
    DirectorInstance *instance;
    gchar *host;
    gint port;
    GAsyncQueue *requests;
    GThread *thread;
    GMutex mutex;
    gboolean stop;
    gboolean started;
    gboolean connected;
    gboolean refresh_pending;
    gboolean identity_error_reported;
    DirectorClientEventFunc callback;
    gpointer user_data;
    gint ref_count;
};

static gint64 director_now_us(void)
{
    return g_get_monotonic_time();
}

static gint64 director_reconnect_delay_us(guint failure_count)
{
    gint64 delay = DIRECTOR_RECONNECT_INITIAL_US;
    for(guint i = 1; i < failure_count && delay < DIRECTOR_RECONNECT_MAX_US; i++)
        delay = MIN(delay * 2, DIRECTOR_RECONNECT_MAX_US);
    return delay;
}

static void director_vims_stderr(DirectorClient *client,
                                 const gchar *command,
                                 const gchar *reason)
{
    if(!client)
        return;
    fprintf(stderr,
            "veejay-director: VIMS request failed: instance=%s endpoint=%s:%d command=%s reason=%s\n",
            client->instance && client->instance->id ? client->instance->id : "unknown",
            client->host ? client->host : "unknown",
            client->port,
            command && *command ? command : "unknown",
            reason && *reason ? reason : "request or response failed");
    fflush(stderr);
}

static void director_control_connect_stderr(DirectorClient *client,
                                            const gchar *reason)
{
    if(!client)
        return;
    fprintf(stderr,
            "veejay-director: control connection failed: instance=%s endpoint=%s:%d reason=%s\n",
            client->instance && client->instance->id ? client->instance->id : "unknown",
            client->host ? client->host : "unknown",
            client->port,
            reason && *reason ? reason : "connection failed");
    fflush(stderr);
}

static void director_register_control_failure(gint64 *failure_since,
                                              guint *failure_count,
                                              gint64 *reconnect_at)
{
    const gint64 now = director_now_us();
    if(*failure_since == 0)
        *failure_since = now;
    if(*failure_count < G_MAXUINT)
        (*failure_count)++;
    *reconnect_at = now + director_reconnect_delay_us(*failure_count);
}

static void director_reset_control_failures(gint64 *failure_since,
                                            guint *failure_count,
                                            gint64 *reconnect_at)
{
    *failure_since = 0;
    *failure_count = 0;
    *reconnect_at = 0;
}

static void director_request_free(DirectorRequest *request)
{
    if(!request)
        return;
    g_free(request->command);
    g_free(request);
}

static DirectorClient *director_client_ref(DirectorClient *client)
{
    g_atomic_int_inc(&client->ref_count);
    return client;
}

static void director_client_unref(DirectorClient *client)
{
    if(!g_atomic_int_dec_and_test(&client->ref_count))
        return;
    g_async_queue_unref(client->requests);
    g_mutex_clear(&client->mutex);
    g_free(client->host);
    g_free(client);
}

static gboolean director_dispatch_main(gpointer data)
{
    DirectorDispatch *dispatch = data;
    DirectorClientEventFunc callback;
    gpointer user_data;
    g_mutex_lock(&dispatch->client->mutex);
    callback = dispatch->client->callback;
    user_data = dispatch->client->user_data;
    g_mutex_unlock(&dispatch->client->mutex);
    if(callback)
        callback(dispatch->client, dispatch->event, dispatch->payload, user_data);
    g_free(dispatch->payload);
    director_client_unref(dispatch->client);
    g_free(dispatch);
    return G_SOURCE_REMOVE;
}

static void director_dispatch(DirectorClient *client,
                              DirectorClientEvent event,
                              const gchar *payload)
{
    DirectorDispatch *dispatch = g_new0(DirectorDispatch, 1);
    dispatch->client = director_client_ref(client);
    dispatch->event = event;
    dispatch->payload = director_utf8_make_valid_compat(payload ? payload : "", -1);
    g_main_context_invoke(NULL, director_dispatch_main, dispatch);
}

static gboolean director_should_stop(DirectorClient *client)
{
    gboolean stop;
    g_mutex_lock(&client->mutex);
    stop = client->stop;
    g_mutex_unlock(&client->mutex);
    return stop;
}

static void director_set_connected(DirectorClient *client, gboolean connected)
{
    gboolean changed;
    g_mutex_lock(&client->mutex);
    changed = client->connected != connected;
    client->connected = connected;
    g_mutex_unlock(&client->mutex);
    if(changed)
        director_dispatch(client,
                          connected ? DIRECTOR_CLIENT_CONNECTED :
                                      DIRECTOR_CLIENT_DISCONNECTED,
                          connected ? "Connected" : "Disconnected");
}

static void director_clear_refresh_pending(DirectorClient *client)
{
    g_mutex_lock(&client->mutex);
    client->refresh_pending = FALSE;
    g_mutex_unlock(&client->mutex);
}

static gboolean director_identity_response_matches(DirectorClient *client,
                                                   const gchar *response,
                                                   gchar **message)
{
    if(message)
        *message = NULL;
    if(!client || !client->instance || !response)
        return FALSE;

    gchar *copy = g_strdup(response);
    g_strstrip(copy);
    gchar **tokens = g_strsplit_set(copy, " \t\r\n", -1);
    const gchar *role = NULL;
    const gchar *id = NULL;
    gint port = -1;
    for(gint i = 0; tokens && tokens[i]; i++) {
        if(g_str_has_prefix(tokens[i], "role="))
            role = tokens[i] + 5;
        else if(g_str_has_prefix(tokens[i], "id="))
            id = tokens[i] + 3;
        else if(g_str_has_prefix(tokens[i], "port=")) {
            gchar *end = NULL;
            const gint64 parsed = g_ascii_strtoll(tokens[i] + 5, &end, 10);
            if(end && *end == '\0' && parsed > 0 && parsed <= 65535)
                port = (gint)parsed;
        }
    }

    const gchar *expected_role = director_role_name(client->instance->role);
    const gboolean match = role && id && port > 0 &&
        g_strcmp0(role, expected_role) == 0 &&
        g_strcmp0(id, client->instance->id) == 0 &&
        port == client->port;
    if(!match && message)
        *message = g_strdup_printf(
            "Control endpoint %s:%d belongs to %s/%s on port %d; expected %s/%s on port %d",
            client->host, client->port,
            role && *role ? role : "unknown",
            id && *id ? id : "unknown", port,
            expected_role, client->instance->id, client->port);

    g_strfreev(tokens);
    g_free(copy);
    return match;
}

static gboolean director_connect(DirectorClient *client, DirectorWire *wire)
{
    director_wire_close(wire);
    if(!director_wire_connect(wire,
                              client->host,
                              client->port + VJ_CMD_PORT,
                              DIRECTOR_CONNECT_TIMEOUT_MS)) {
        director_control_connect_stderr(client, "connect failed or timed out");
        return FALSE;
    }

    gchar response[DIRECTOR_RESPONSE_MAX];
    if(!director_wire_query_timed(wire, "488:;", response, sizeof(response),
                                  DIRECTOR_RESPONSE_TIMEOUT_MS,
                                  DIRECTOR_RESPONSE_IDLE_MS, NULL)) {
        director_vims_stderr(client, "488:;", "identity query failed or timed out");
        director_wire_close(wire);
        return FALSE;
    }

    gchar *identity_error = NULL;
    if(!director_identity_response_matches(client, response, &identity_error)) {
        director_vims_stderr(client, "488:;",
                             identity_error ? identity_error :
                             "control endpoint identity did not match");
        if(!client->identity_error_reported) {
            director_dispatch(client, DIRECTOR_CLIENT_ERROR,
                              identity_error ? identity_error :
                              "Control endpoint identity did not match the configured VeeJay instance");
            client->identity_error_reported = TRUE;
        }
        g_free(identity_error);
        director_wire_close(wire);
        return FALSE;
    }

    client->identity_error_reported = FALSE;
    director_set_connected(client, TRUE);
    g_strstrip(response);
    director_dispatch(client, DIRECTOR_CLIENT_INSTANCE_STATUS, response);
    return TRUE;
}

static gboolean director_send_raw(DirectorClient *client,
                                  DirectorWire *wire,
                                  const gchar *command)
{
    if(director_wire_send(wire, command, DIRECTOR_SEND_TIMEOUT_MS) != 0)
        return TRUE;
    director_vims_stderr(client, command, "send failed or timed out");
    return FALSE;
}

static gboolean director_query_timeout(DirectorClient *client,
                                       DirectorWire *wire,
                                       const gchar *command,
                                       DirectorClientEvent event,
                                       gdouble *first_response_ms,
                                       gint timeout_ms)
{
    gchar response[DIRECTOR_RESPONSE_MAX];
    if(!director_wire_query_timed(wire,
                                  command,
                                  response,
                                  sizeof(response),
                                  timeout_ms,
                                  DIRECTOR_RESPONSE_IDLE_MS,
                                  first_response_ms)) {
        director_vims_stderr(client, command, "query failed or response timed out");
        return FALSE;
    }
    g_strstrip(response);
    director_dispatch(client, event, response);
    return TRUE;
}

static gboolean director_query(DirectorClient *client,
                               DirectorWire *wire,
                               const gchar *command,
                               DirectorClientEvent event,
                               gdouble *first_response_ms)
{
    return director_query_timeout(client, wire, command, event, first_response_ms,
                                  DIRECTOR_RESPONSE_TIMEOUT_MS);
}

static gboolean director_query_framed(DirectorClient *client,
                                      DirectorWire *wire,
                                      const gchar *command,
                                      DirectorClientEvent event,
                                      gsize header_digits)
{
    gchar response[DIRECTOR_RESPONSE_MAX];
    if(!director_wire_query_framed(wire, command, response, sizeof(response),
                                   header_digits, DIRECTOR_RESPONSE_TIMEOUT_MS)) {
        director_vims_stderr(client, command, "framed query failed or response timed out");
        return FALSE;
    }
    g_strstrip(response);
    director_dispatch(client, event, response);
    return TRUE;
}

static gboolean director_refresh_all(DirectorClient *client, DirectorWire *wire)
{
    gdouble instance_ms = 0.0, routing_ms = 0.0, output_ms = 0.0, perf_ms = 0.0, ndi_ms = 0.0;
    if(!director_query(client, wire, "488:;", DIRECTOR_CLIENT_INSTANCE_STATUS,
                       &instance_ms))
        return FALSE;
    if(!director_query(client, wire, "494:;", DIRECTOR_CLIENT_ROUTING_STATUS,
                       &routing_ms))
        return FALSE;
    if(!director_query(client, wire, "484:;", DIRECTOR_CLIENT_OUTPUT_STATUS,
                       &output_ms))
        return FALSE;
    if((client->instance->role == DIRECTOR_ROLE_OUTPUT ||
        client->instance->legacy_viewport ||
        !client->instance->managed) &&
       !director_query_framed(client, wire, "007:;",
                              DIRECTOR_CLIENT_PROJECTION_STATUS, 8))
        return FALSE;
    if(!director_query(client, wire, "482:;", DIRECTOR_CLIENT_PERF_STATUS,
                       &perf_ms))
        return FALSE;
    if(!director_query(client, wire, "490:;", DIRECTOR_CLIENT_NDI_STATUS,
                       &ndi_ms))
        return FALSE;
    client->instance->live_latency_ms =
        MAX(instance_ms, MAX(routing_ms, MAX(output_ms, MAX(perf_ms, ndi_ms))));
    return TRUE;
}

static gboolean director_process_request(DirectorClient *client,
                                         DirectorWire *wire,
                                         DirectorRequest *request)
{
    gboolean ok = TRUE;
    switch(request->type) {
        case DIRECTOR_REQUEST_STOP:
            return FALSE;
        case DIRECTOR_REQUEST_REFRESH:
            ok = director_refresh_all(client, wire);
            break;
        case DIRECTOR_REQUEST_COMMAND:
            ok = director_send_raw(client, wire, request->command);
            break;
        case DIRECTOR_REQUEST_QUERY_FRAMED:
            ok = director_query_framed(client, wire, request->command,
                                       request->event, request->header_digits);
            break;
    }
    if(!ok)
        director_dispatch(client, DIRECTOR_CLIENT_ERROR,
                          "VIMS command or response failed");
    return ok;
}

static gpointer director_client_worker(gpointer data)
{
    DirectorClient *client = data;
    DirectorWire wire;
    GQueue pending = G_QUEUE_INIT;
    gint64 reconnect_at = 0;
    gint64 refresh_at = 0;
    gint64 failure_since = 0;
    guint failure_count = 0;
    director_wire_init(&wire);

    while(!director_should_stop(client)) {
        gint64 now = director_now_us();
        if(failure_since > 0 && now - failure_since >= DIRECTOR_FAILURE_EXPIRE_US) {
            fprintf(stderr,
                    "veejay-director: control endpoint retired: instance=%s endpoint=%s:%d continuous_failure_seconds=300\n",
                    client->instance && client->instance->id ? client->instance->id : "unknown",
                    client->host ? client->host : "unknown", client->port);
            fflush(stderr);
            director_dispatch(client, DIRECTOR_CLIENT_EXPIRED,
                              "Control endpoint failed continuously for 5 minutes; removing instance");
            break;
        }
        if(wire.fd < 0) {
            if(now >= reconnect_at) {
                if(director_connect(client, &wire))
                    refresh_at = 0;
                else
                    director_register_control_failure(&failure_since,
                                                      &failure_count,
                                                      &reconnect_at);
            }
            if(wire.fd < 0) {
                now = director_now_us();
                gint64 wake_at = reconnect_at;
                if(failure_since > 0)
                    wake_at = MIN(wake_at, failure_since + DIRECTOR_FAILURE_EXPIRE_US);
                gint64 wait_us = MAX((gint64)1000, wake_at - now);
                DirectorRequest *request = g_async_queue_timeout_pop(client->requests, wait_us);
                if(!request)
                    continue;
                if(request->type == DIRECTOR_REQUEST_STOP) {
                    director_request_free(request);
                    break;
                }
                g_queue_push_tail(&pending, request);
                continue;
            }
        }

        now = director_now_us();
        gint64 wait_us = refresh_at > now ? refresh_at - now : 0;
        DirectorRequest *request = !g_queue_is_empty(&pending) ?
            g_queue_pop_head(&pending) :
            (wait_us > 0 ? g_async_queue_timeout_pop(client->requests, wait_us) :
                           g_async_queue_try_pop(client->requests));
        if(request) {
            if(request->type == DIRECTOR_REQUEST_STOP) {
                director_request_free(request);
                break;
            }
            const gboolean manual_refresh = request->type == DIRECTOR_REQUEST_REFRESH;
            if(!director_process_request(client, &wire, request)) {
                if(manual_refresh)
                    director_clear_refresh_pending(client);
                director_request_free(request);
                director_wire_close(&wire);
                director_set_connected(client, FALSE);
                director_register_control_failure(&failure_since,
                                                  &failure_count,
                                                  &reconnect_at);
                continue;
            }
            if(manual_refresh) {
                director_clear_refresh_pending(client);
                director_reset_control_failures(&failure_since,
                                                &failure_count,
                                                &reconnect_at);
                refresh_at = director_now_us() + DIRECTOR_QUERY_INTERVAL_US;
            }
            director_request_free(request);
        }

        if(director_should_stop(client))
            break;

        now = director_now_us();
        if(now >= refresh_at) {
            if(!director_refresh_all(client, &wire)) {
                director_wire_close(&wire);
                director_set_connected(client, FALSE);
                director_register_control_failure(&failure_since,
                                                  &failure_count,
                                                  &reconnect_at);
                continue;
            }
            director_reset_control_failures(&failure_since,
                                            &failure_count,
                                            &reconnect_at);
            refresh_at = director_now_us() + DIRECTOR_QUERY_INTERVAL_US;
        }
    }

    while(!g_queue_is_empty(&pending))
        director_request_free(g_queue_pop_head(&pending));
    director_wire_close(&wire);
    director_set_connected(client, FALSE);
    return NULL;
}

DirectorClient *director_client_new(DirectorInstance *instance,
                                    DirectorClientEventFunc callback,
                                    gpointer user_data)
{
    if(!instance)
        return NULL;
    DirectorClient *client = g_new0(DirectorClient, 1);
    client->instance = instance;
    client->host = g_strdup(instance->host);
    client->port = instance->port;
    client->requests = g_async_queue_new_full((GDestroyNotify)director_request_free);
    g_mutex_init(&client->mutex);
    client->callback = callback;
    client->user_data = user_data;
    client->ref_count = 1;
    return client;
}

void director_client_start(DirectorClient *client)
{
    if(!client)
        return;
    g_mutex_lock(&client->mutex);
    if(client->started) {
        g_mutex_unlock(&client->mutex);
        return;
    }
    client->stop = FALSE;
    client->started = TRUE;
    g_mutex_unlock(&client->mutex);
    client->thread = g_thread_new("veejay-director-vims",
                                  director_client_worker,
                                  client);
}

void director_client_stop(DirectorClient *client)
{
    if(!client)
        return;
    g_mutex_lock(&client->mutex);
    gboolean started = client->started;
    client->stop = TRUE;
    g_mutex_unlock(&client->mutex);
    if(!started)
        return;

    DirectorRequest *request = g_new0(DirectorRequest, 1);
    request->type = DIRECTOR_REQUEST_STOP;
    g_async_queue_push(client->requests, request);
    if(client->thread) {
        g_thread_join(client->thread);
        client->thread = NULL;
    }
    g_mutex_lock(&client->mutex);
    client->started = FALSE;
    client->connected = FALSE;
    client->refresh_pending = FALSE;
    g_mutex_unlock(&client->mutex);
}

void director_client_free(DirectorClient *client)
{
    if(!client)
        return;
    director_client_stop(client);
    g_mutex_lock(&client->mutex);
    client->callback = NULL;
    client->user_data = NULL;
    g_mutex_unlock(&client->mutex);
    director_client_unref(client);
}

DirectorInstance *director_client_get_instance(DirectorClient *client)
{
    return client ? client->instance : NULL;
}

static gboolean director_client_queue_command(DirectorClient *client,
                                                const gchar *command)
{
    if(!client || !command || !*command)
        return FALSE;

    g_mutex_lock(&client->mutex);
    const gboolean ready = client->started && client->connected && !client->stop;
    g_mutex_unlock(&client->mutex);
    if(!ready) {
        director_vims_stderr(client, command,
                             "not sent: control connection is unavailable");
        return FALSE;
    }

    DirectorRequest *request = g_new0(DirectorRequest, 1);
    request->type = DIRECTOR_REQUEST_COMMAND;
    request->command = g_strdup(command);
    g_async_queue_push(client->requests, request);
    return TRUE;
}

void director_client_send(DirectorClient *client, const gchar *command)
{
    (void)director_client_queue_command(client, command);
}

gboolean director_client_set_shm_output(DirectorClient *client, gboolean enabled)
{
    return director_client_queue_command(client, enabled ? "025:1;" : "025:0;");
}

gboolean director_client_select_input_route(DirectorClient *client, gint stream_id)
{
    if(!client || stream_id <= 0)
        return FALSE;
    gchar *command = g_strdup_printf("297:%d;", stream_id);
    const gboolean queued = director_client_queue_command(client, command);
    g_free(command);
    return queued;
}

static gboolean director_client_ndi_argument_valid(const gchar *value)
{
    return value && strpbrk(value, ";\n\r") == NULL;
}

gboolean director_client_set_ndi_input(DirectorClient *client, const gchar *source_name)
{
    const gchar *value = source_name && *source_name ? source_name : "-";
    if(!director_client_ndi_argument_valid(value))
        return FALSE;
    gchar *command = g_strdup_printf("291:%s;", value);
    const gboolean queued = director_client_queue_command(client, command);
    g_free(command);
    return queued;
}

gboolean director_client_set_ndi_output(DirectorClient *client, gboolean enabled,
                                         const gchar *sender_name)
{
    const gchar *value = sender_name && *sender_name ? sender_name : "-";
    if(!director_client_ndi_argument_valid(value))
        return FALSE;
    gchar *command = g_strdup_printf("292:%d %s;", enabled ? 1 : 0, value);
    const gboolean queued = director_client_queue_command(client, command);
    g_free(command);
    return queued;
}

gboolean director_client_remove_input_route_id(DirectorClient *client, gint stream_id)
{
    if(!client || stream_id <= 0)
        return FALSE;
    gchar *command = g_strdup_printf("293:0 %d -;", stream_id);
    const gboolean queued = director_client_queue_command(client, command);
    g_free(command);
    return queued;
}

gboolean director_client_remove_input_route(DirectorClient *client,
                                             gint transport,
                                             gint key_or_port,
                                             const gchar *endpoint)
{
    const gchar *value = endpoint && *endpoint ? endpoint : "-";
    if(transport < DIRECTOR_INPUT_ROUTE_SHM || transport > DIRECTOR_INPUT_ROUTE_NDI ||
       !director_client_ndi_argument_valid(value))
        return FALSE;
    gchar *command = g_strdup_printf("293:%d %d %s;", transport, key_or_port, value);
    const gboolean queued = director_client_queue_command(client, command);
    g_free(command);
    return queued;
}

void director_client_refresh(DirectorClient *client)
{
    if(!client)
        return;
    g_mutex_lock(&client->mutex);
    const gboolean ready = client->started && client->connected && !client->stop;
    if(!ready || client->refresh_pending) {
        g_mutex_unlock(&client->mutex);
        return;
    }
    client->refresh_pending = TRUE;
    g_mutex_unlock(&client->mutex);

    DirectorRequest *request = g_new0(DirectorRequest, 1);
    request->type = DIRECTOR_REQUEST_REFRESH;
    g_async_queue_push(client->requests, request);
}

static void director_client_query_framed_async(DirectorClient *client,
                                               const gchar *command,
                                               DirectorClientEvent event,
                                               gsize header_digits)
{
    if(!client || !command || !*command || header_digits == 0)
        return;
    g_mutex_lock(&client->mutex);
    const gboolean ready = client->started && client->connected && !client->stop;
    g_mutex_unlock(&client->mutex);
    if(!ready) {
        director_vims_stderr(client, command,
                             "not sent: control connection is unavailable");
        return;
    }
    DirectorRequest *request = g_new0(DirectorRequest, 1);
    request->type = DIRECTOR_REQUEST_QUERY_FRAMED;
    request->command = g_strdup(command);
    request->event = event;
    request->header_digits = header_digits;
    g_async_queue_push(client->requests, request);
}

void director_client_query_devices(DirectorClient *client)
{
    director_client_query_framed_async(client, "415:;",
                                       DIRECTOR_CLIENT_DEVICE_LIST, 6);
}

void director_client_query_v4l(DirectorClient *client, gint stream_id)
{
    gchar *command = g_strdup_printf("409:%d;", stream_id);
    director_client_query_framed_async(client, command,
                                       DIRECTOR_CLIENT_V4L_STATUS, 3);
    g_free(command);
}

void director_client_apply_output_graph(DirectorClient *client,
                                        const DirectorInstance *instance)
{
    if(!client || !instance)
        return;
    gchar *command = NULL;
    for(gint i = 0; i < DIRECTOR_MAX_SLICES; i++) {
        command = director_slice_to_vims(i, &instance->slices[i]);
        director_client_send(client, command);
        g_free(command);
        command = g_strdup_printf("287:%d %d;", i,
                                  instance->slices[i].enabled ? 1 : 0);
        director_client_send(client, command);
        g_free(command);
    }

    command = g_strdup_printf("285:%d;", instance->pattern);
    director_client_send(client, command);
    g_free(command);
    director_client_refresh(client);
}
