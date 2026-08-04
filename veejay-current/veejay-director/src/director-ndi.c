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
/*
 * NDI(R) is a registered trademark of Vizrt NDI AB.
 */
#include <config.h>
#include "director-ndi.h"

#ifdef HAVE_NDI
#include <Processing.NDI.Lib.h>
#include <dlfcn.h>
#include <string.h>

#ifndef NDILIB_LIBRARY_NAME
#define NDILIB_LIBRARY_NAME "libndi.so.6"
#endif

typedef const NDIlib_v5 *(*DirectorNdiLoadFunc)(void);

typedef struct {
    GMutex mutex;
    gpointer library;
    const NDIlib_v5 *api;
    guint references;
    gboolean probed;
    gchar version[160];
} DirectorNdiRuntime;

static DirectorNdiRuntime ndi_runtime;
static gsize ndi_runtime_once;

static void director_ndi_runtime_init_once(void)
{
    g_mutex_init(&ndi_runtime.mutex);
    g_strlcpy(ndi_runtime.version, "unavailable", sizeof(ndi_runtime.version));
}

static void director_ndi_runtime_init(void)
{
    if(g_once_init_enter(&ndi_runtime_once)) {
        director_ndi_runtime_init_once();
        g_once_init_leave(&ndi_runtime_once, 1);
    }
}

static gboolean director_ndi_try_library(const gchar *path)
{
    gpointer symbol = NULL;
    DirectorNdiLoadFunc load = NULL;

    ndi_runtime.library = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
    if(!ndi_runtime.library)
        return FALSE;

    symbol = dlsym(ndi_runtime.library, "NDIlib_v5_load");
    if(!symbol) {
        dlclose(ndi_runtime.library);
        ndi_runtime.library = NULL;
        return FALSE;
    }

    memcpy(&load, &symbol, sizeof(load));
    ndi_runtime.api = load();
    if(!ndi_runtime.api || !ndi_runtime.api->initialize ||
       !ndi_runtime.api->initialize()) {
        ndi_runtime.api = NULL;
        dlclose(ndi_runtime.library);
        ndi_runtime.library = NULL;
        return FALSE;
    }

    g_strlcpy(ndi_runtime.version,
              ndi_runtime.api->version ? ndi_runtime.api->version() : "NDI runtime",
              sizeof(ndi_runtime.version));
    return TRUE;
}

static gboolean director_ndi_acquire(void)
{
    director_ndi_runtime_init();
    g_mutex_lock(&ndi_runtime.mutex);
    if(!ndi_runtime.api && !ndi_runtime.probed) {
        ndi_runtime.probed = TRUE;
        const gchar *libraries[] = {
            NDILIB_LIBRARY_NAME,
            "libndi.so.6",
            "libndi.so",
            "libndi.so.5",
            NULL
        };
#ifdef NDILIB_REDIST_FOLDER
        const gchar *folder = g_getenv(NDILIB_REDIST_FOLDER);
        if(folder && *folder) {
            gchar *candidate = g_build_filename(folder, NDILIB_LIBRARY_NAME, NULL);
            director_ndi_try_library(candidate);
            g_free(candidate);
        }
#endif
        for(gint i = 0; !ndi_runtime.api && libraries[i]; i++)
            director_ndi_try_library(libraries[i]);
    }
    if(ndi_runtime.api)
        ndi_runtime.references++;
    const gboolean available = ndi_runtime.api != NULL;
    g_mutex_unlock(&ndi_runtime.mutex);
    return available;
}

static void director_ndi_release(void)
{
    g_mutex_lock(&ndi_runtime.mutex);
    if(ndi_runtime.references > 0)
        ndi_runtime.references--;
    g_mutex_unlock(&ndi_runtime.mutex);
}

static void director_ndi_runtime_allow_reprobe(void)
{
    director_ndi_runtime_init();
    g_mutex_lock(&ndi_runtime.mutex);
    if(ndi_runtime.references == 0) {
        if(ndi_runtime.api && ndi_runtime.api->destroy)
            ndi_runtime.api->destroy();
        if(ndi_runtime.library)
            dlclose(ndi_runtime.library);
        ndi_runtime.library = NULL;
        ndi_runtime.api = NULL;
        ndi_runtime.probed = FALSE;
        g_strlcpy(ndi_runtime.version, "unavailable", sizeof(ndi_runtime.version));
    }
    g_mutex_unlock(&ndi_runtime.mutex);
}

static gboolean director_ndi_has_discovery_api(const NDIlib_v5 *api)
{
    return api && api->find_create_v2 && api->find_destroy &&
           api->find_get_current_sources && api->find_wait_for_sources;
}

gboolean director_ndi_available(void)
{
    if(!director_ndi_acquire())
        return FALSE;
    director_ndi_release();
    return TRUE;
}

const gchar *director_ndi_version(void)
{
    if(!director_ndi_acquire())
        return "unavailable";
    director_ndi_release();
    return ndi_runtime.version;
}

void director_ndi_source_free(DirectorNdiSource *source)
{
    if(!source)
        return;
    g_free(source->name);
    g_free(source->url);
    g_free(source);
}

typedef struct {
    const NDIlib_v5 *api;
    NDIlib_find_instance_t finder;
    gboolean runtime_acquired;
    gchar runtime_version[160];
} DirectorNdiFinder;

static DirectorNdiFinder *director_ndi_finder_create(GError **error)
{
    if(!director_ndi_acquire()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "The NDI runtime is not installed or could not be loaded");
        return NULL;
    }

    if(!director_ndi_has_discovery_api(ndi_runtime.api)) {
        director_ndi_release();
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "The loaded NDI runtime does not provide source discovery");
        return NULL;
    }

    DirectorNdiFinder *finder = g_new0(DirectorNdiFinder, 1);
    finder->api = ndi_runtime.api;
    finder->runtime_acquired = TRUE;
    g_strlcpy(finder->runtime_version, ndi_runtime.version,
              sizeof(finder->runtime_version));

    NDIlib_find_create_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.show_local_sources = true;
    finder->finder = finder->api->find_create_v2(&desc);
    if(!finder->finder) {
        director_ndi_release();
        g_free(finder);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "NDI source discovery could not be started");
        return NULL;
    }

    return finder;
}

static void director_ndi_finder_destroy(DirectorNdiFinder *finder)
{
    if(!finder)
        return;
    if(finder->finder && finder->api && finder->api->find_destroy)
        finder->api->find_destroy(finder->finder);
    if(finder->runtime_acquired)
        director_ndi_release();
    g_free(finder);
}

static gboolean director_ndi_finder_wait(DirectorNdiFinder *finder, guint timeout_ms)
{
    if(!finder || !finder->finder || !finder->api || !finder->api->find_wait_for_sources)
        return FALSE;
    return finder->api->find_wait_for_sources(finder->finder, timeout_ms);
}

static gint director_ndi_source_compare(gconstpointer a, gconstpointer b)
{
    const DirectorNdiSource *sa = *(DirectorNdiSource * const *)a;
    const DirectorNdiSource *sb = *(DirectorNdiSource * const *)b;
    gint result = g_ascii_strcasecmp(sa && sa->name ? sa->name : "",
                                     sb && sb->name ? sb->name : "");
    if(result != 0)
        return result;
    return g_strcmp0(sa && sa->url ? sa->url : "",
                     sb && sb->url ? sb->url : "");
}

static gboolean director_ndi_source_same(const DirectorNdiSource *a,
                                         const DirectorNdiSource *b)
{
    return a && b && g_strcmp0(a->name, b->name) == 0 &&
           g_strcmp0(a->url, b->url) == 0;
}

static guint64 director_ndi_signature_bytes(guint64 hash, const gchar *text)
{
    const guchar *p = (const guchar *)(text ? text : "");
    while(*p) {
        hash ^= (guint64)*p++;
        hash *= G_GUINT64_CONSTANT(1099511628211);
    }
    hash ^= G_GUINT64_CONSTANT(0xff);
    hash *= G_GUINT64_CONSTANT(1099511628211);
    return hash;
}

static guint64 director_ndi_source_signature(const GPtrArray *sources)
{
    guint64 hash = G_GUINT64_CONSTANT(1469598103934665603);
    if(!sources)
        return hash;
    for(guint i = 0; i < sources->len; i++) {
        const DirectorNdiSource *source = g_ptr_array_index((GPtrArray *)sources, i);
        hash = director_ndi_signature_bytes(hash, source ? source->name : "");
        hash = director_ndi_signature_bytes(hash, source ? source->url : "");
    }
    hash ^= sources->len;
    hash *= G_GUINT64_CONSTANT(1099511628211);
    return hash;
}

static GPtrArray *director_ndi_finder_snapshot(DirectorNdiFinder *finder)
{
    GPtrArray *sources = g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free);
    if(!finder || !finder->finder || !finder->api || !finder->api->find_get_current_sources)
        return sources;

    uint32_t count = 0;
    const NDIlib_source_t *found =
        finder->api->find_get_current_sources(finder->finder, &count);
    for(uint32_t i = 0; found && i < count; i++) {
        const gchar *name = found[i].p_ndi_name;
        if(!name || !*name || strlen(name) > 253)
            continue;
        DirectorNdiSource *source = g_new0(DirectorNdiSource, 1);
        source->name = g_strdup(name);
        source->url = g_strdup(found[i].p_url_address ? found[i].p_url_address : "");
        g_ptr_array_add(sources, source);
    }

    g_ptr_array_sort(sources, director_ndi_source_compare);
    for(guint i = 1; i < sources->len;) {
        DirectorNdiSource *previous = g_ptr_array_index(sources, i - 1);
        DirectorNdiSource *current = g_ptr_array_index(sources, i);
        if(director_ndi_source_same(previous, current))
            g_ptr_array_remove_index(sources, i);
        else
            i++;
    }

    return sources;
}

GPtrArray *director_ndi_discover(guint timeout_ms, GError **error)
{
    DirectorNdiFinder *finder = director_ndi_finder_create(error);
    if(!finder)
        return g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free);

    if(timeout_ms > 0)
        director_ndi_finder_wait(finder, timeout_ms);

    GPtrArray *sources = director_ndi_finder_snapshot(finder);
    director_ndi_finder_destroy(finder);
    return sources;
}

typedef struct _DirectorNdiDispatch DirectorNdiDispatch;

struct _DirectorNdiDiscovery {
    GMutex mutex;
    GCond cond;
    GThread *thread;
    gboolean started;
    gboolean joining;
    gboolean stop;
    gboolean refresh_requested;
    gboolean callback_enabled;
    gint ref_count;
    guint64 generation;
    guint64 latest_signature;
    gint64 latest_observed_us;
    DirectorNdiDiscoveryState latest_state;
    gchar *latest_version;
    gchar *latest_error;
    DirectorNdiDispatch *pending_dispatch;
    guint dispatch_source_id;
    DirectorNdiDiscoveryFunc callback;
    gpointer user_data;
};

struct _DirectorNdiDispatch {
    DirectorNdiDiscoveryState state;
    guint64 generation;
    gint64 observed_us;
    GPtrArray *sources;
    gchar *version;
    gchar *error;
    gboolean forced;
};

static void director_ndi_dispatch_free(DirectorNdiDispatch *dispatch)
{
    if(!dispatch)
        return;
    if(dispatch->sources)
        g_ptr_array_free(dispatch->sources, TRUE);
    g_free(dispatch->version);
    g_free(dispatch->error);
    g_free(dispatch);
}

static void director_ndi_discovery_unref(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;

    gboolean destroy = FALSE;
    g_mutex_lock(&discovery->mutex);
    if(discovery->ref_count > 0)
        discovery->ref_count--;
    destroy = discovery->ref_count == 0;
    g_mutex_unlock(&discovery->mutex);

    if(!destroy)
        return;

    g_free(discovery->latest_version);
    g_free(discovery->latest_error);
    director_ndi_dispatch_free(discovery->pending_dispatch);
    g_cond_clear(&discovery->cond);
    g_mutex_clear(&discovery->mutex);
    g_free(discovery);
}

static gboolean director_ndi_dispatch_main(gpointer data)
{
    DirectorNdiDiscovery *discovery = data;
    DirectorNdiDispatch *dispatch = NULL;
    DirectorNdiDiscoveryFunc callback = NULL;
    gpointer user_data = NULL;

    g_mutex_lock(&discovery->mutex);
    dispatch = discovery->pending_dispatch;
    discovery->pending_dispatch = NULL;
    discovery->dispatch_source_id = 0;
    if(discovery->callback_enabled) {
        callback = discovery->callback;
        user_data = discovery->user_data;
    }
    g_mutex_unlock(&discovery->mutex);

    if(dispatch && callback) {
        DirectorNdiDiscoveryUpdate update;
        update.state = dispatch->state;
        update.generation = dispatch->generation;
        update.observed_us = dispatch->observed_us;
        update.runtime_version = dispatch->version;
        update.sources = dispatch->sources;
        update.error = dispatch->error;
        update.forced = dispatch->forced;
        callback(discovery, &update, user_data);
    }

    director_ndi_dispatch_free(dispatch);
    return G_SOURCE_REMOVE;
}

static gboolean director_ndi_discovery_publish(DirectorNdiDiscovery *discovery,
                                               DirectorNdiDiscoveryState state,
                                               GPtrArray *sources,
                                               const gchar *version,
                                               const gchar *error,
                                               gboolean forced)
{
    const guint64 signature = director_ndi_source_signature(sources);
    gboolean changed = FALSE;
    gboolean dispatch_enabled = FALSE;
    guint64 generation = 0;
    gint64 observed_us = 0;

    g_mutex_lock(&discovery->mutex);
    changed = forced || state != discovery->latest_state ||
              signature != discovery->latest_signature ||
              g_strcmp0(version ? version : "",
                        discovery->latest_version ? discovery->latest_version : "") != 0 ||
              g_strcmp0(error ? error : "",
                        discovery->latest_error ? discovery->latest_error : "") != 0;
    if(changed) {
        discovery->latest_state = state;
        discovery->latest_signature = signature;
        discovery->latest_observed_us = g_get_monotonic_time();
        discovery->generation++;
        g_free(discovery->latest_version);
        discovery->latest_version = g_strdup(version ? version : "");
        g_free(discovery->latest_error);
        discovery->latest_error = g_strdup(error ? error : "");
        generation = discovery->generation;
        observed_us = discovery->latest_observed_us;
        dispatch_enabled = discovery->callback_enabled && discovery->callback != NULL;
    }
    g_mutex_unlock(&discovery->mutex);

    if(!changed || !dispatch_enabled) {
        if(sources)
            g_ptr_array_free(sources, TRUE);
        return changed;
    }

    DirectorNdiDispatch *dispatch = g_new0(DirectorNdiDispatch, 1);
    dispatch->state = state;
    dispatch->generation = generation;
    dispatch->observed_us = observed_us;
    dispatch->sources = sources;
    dispatch->version = g_strdup(version ? version : "");
    dispatch->error = g_strdup(error ? error : "");
    dispatch->forced = forced;

    DirectorNdiDispatch *replaced = NULL;
    gboolean scheduled = TRUE;

    g_mutex_lock(&discovery->mutex);
    if(!discovery->callback_enabled) {
        scheduled = FALSE;
    }
    else {
        replaced = discovery->pending_dispatch;
        discovery->pending_dispatch = dispatch;
        if(discovery->dispatch_source_id == 0) {
            discovery->ref_count++;
            discovery->dispatch_source_id =
                g_idle_add_full(G_PRIORITY_DEFAULT,
                                director_ndi_dispatch_main,
                                discovery,
                                (GDestroyNotify)director_ndi_discovery_unref);
            if(discovery->dispatch_source_id == 0) {
                discovery->pending_dispatch = NULL;
                discovery->ref_count--;
                scheduled = FALSE;
            }
        }
    }
    g_mutex_unlock(&discovery->mutex);

    director_ndi_dispatch_free(replaced);
    if(!scheduled)
        director_ndi_dispatch_free(dispatch);
    return TRUE;
}

static gboolean director_ndi_discovery_take_flags(DirectorNdiDiscovery *discovery,
                                                   gboolean *forced)
{
    gboolean stop;
    g_mutex_lock(&discovery->mutex);
    stop = discovery->stop;
    if(forced) {
        *forced = discovery->refresh_requested;
        discovery->refresh_requested = FALSE;
    }
    g_mutex_unlock(&discovery->mutex);
    return !stop;
}

static gboolean director_ndi_discovery_wait_retry(DirectorNdiDiscovery *discovery,
                                                  guint delay_ms,
                                                  gboolean *forced)
{
    const gint64 deadline = g_get_monotonic_time() + (gint64)delay_ms * 1000;
    gboolean stop;

    g_mutex_lock(&discovery->mutex);
    while(!discovery->stop && !discovery->refresh_requested) {
        if(!g_cond_wait_until(&discovery->cond, &discovery->mutex, deadline))
            break;
    }
    stop = discovery->stop;
    if(forced) {
        *forced = discovery->refresh_requested;
        discovery->refresh_requested = FALSE;
    }
    g_mutex_unlock(&discovery->mutex);
    return !stop;
}

static gpointer director_ndi_discovery_worker(gpointer data)
{
    static const guint retry_delays_ms[] = { 1000, 2000, 4000, 8000, 15000, 30000 };
    DirectorNdiDiscovery *discovery = data;
    guint retry_index = 0;
    gboolean first_attempt = TRUE;
    gboolean forced = FALSE;
    gboolean pending_forced = FALSE;

    for(;;) {
        if(pending_forced) {
            if(!director_ndi_discovery_take_flags(discovery, NULL))
                break;
            forced = TRUE;
        }
        else if(!director_ndi_discovery_take_flags(discovery, &forced)) {
            break;
        }
        if(first_attempt || forced) {
            director_ndi_discovery_publish(
                discovery, DIRECTOR_NDI_DISCOVERY_STARTING,
                g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free),
                "loading", NULL, TRUE);
        }

        GError *error = NULL;
        DirectorNdiFinder *finder = director_ndi_finder_create(&error);
        if(!finder) {
            const gboolean unavailable =
                error && error->code == G_IO_ERROR_NOT_SUPPORTED;
            director_ndi_discovery_publish(
                discovery,
                unavailable ? DIRECTOR_NDI_DISCOVERY_UNAVAILABLE :
                              DIRECTOR_NDI_DISCOVERY_ERROR,
                g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free),
                "unavailable",
                error ? error->message : "NDI discovery failed",
                first_attempt || forced);
            g_clear_error(&error);

            const guint delay_ms = retry_delays_ms[retry_index];
            gboolean refresh_during_wait = FALSE;
            if(!director_ndi_discovery_wait_retry(discovery, delay_ms,
                                                  &refresh_during_wait))
                break;

            if(refresh_during_wait)
                retry_index = 0;
            else if(retry_index + 1 < G_N_ELEMENTS(retry_delays_ms))
                retry_index++;

            director_ndi_runtime_allow_reprobe();
            pending_forced = refresh_during_wait;
            first_attempt = FALSE;
            continue;
        }

        director_ndi_discovery_publish(discovery,
                                       DIRECTOR_NDI_DISCOVERY_WATCHING,
                                       director_ndi_finder_snapshot(finder),
                                       finder->runtime_version,
                                       NULL,
                                       first_attempt || forced);

        for(;;) {
            const gboolean changed = director_ndi_finder_wait(finder, 250);
            if(!director_ndi_discovery_take_flags(discovery, &forced))
                break;
            if(changed || forced) {
                director_ndi_discovery_publish(discovery,
                                               DIRECTOR_NDI_DISCOVERY_WATCHING,
                                               director_ndi_finder_snapshot(finder),
                                               finder->runtime_version,
                                               NULL,
                                               forced);
            }
        }

        director_ndi_finder_destroy(finder);
        break;
    }

    g_mutex_lock(&discovery->mutex);
    discovery->started = FALSE;
    g_mutex_unlock(&discovery->mutex);
    director_ndi_discovery_unref(discovery);
    return NULL;
}

DirectorNdiDiscovery *director_ndi_discovery_new(DirectorNdiDiscoveryFunc callback,
                                                  gpointer user_data)
{
    DirectorNdiDiscovery *discovery = g_new0(DirectorNdiDiscovery, 1);
    g_mutex_init(&discovery->mutex);
    g_cond_init(&discovery->cond);
    discovery->ref_count = 1;
    discovery->latest_state = DIRECTOR_NDI_DISCOVERY_STOPPED;
    discovery->callback = callback;
    discovery->user_data = user_data;
    return discovery;
}

void director_ndi_discovery_start(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;

    g_mutex_lock(&discovery->mutex);
    if(discovery->thread || discovery->started || discovery->joining) {
        g_mutex_unlock(&discovery->mutex);
        return;
    }
    discovery->stop = FALSE;
    discovery->refresh_requested = TRUE;
    discovery->callback_enabled = TRUE;
    discovery->started = TRUE;
    discovery->ref_count++;
    discovery->thread = g_thread_new("director-ndi-discovery",
                                     director_ndi_discovery_worker, discovery);
    g_mutex_unlock(&discovery->mutex);
}

void director_ndi_discovery_request_refresh(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;
    g_mutex_lock(&discovery->mutex);
    if(discovery->started && !discovery->stop) {
        discovery->refresh_requested = TRUE;
        g_cond_signal(&discovery->cond);
    }
    g_mutex_unlock(&discovery->mutex);
}

void director_ndi_discovery_stop(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;

    GThread *thread = NULL;
    DirectorNdiDispatch *pending_dispatch = NULL;
    guint dispatch_source_id = 0;

    g_mutex_lock(&discovery->mutex);
    discovery->callback_enabled = FALSE;
    discovery->stop = TRUE;
    pending_dispatch = discovery->pending_dispatch;
    discovery->pending_dispatch = NULL;
    dispatch_source_id = discovery->dispatch_source_id;
    discovery->dispatch_source_id = 0;
    g_cond_broadcast(&discovery->cond);

    while(discovery->joining)
        g_cond_wait(&discovery->cond, &discovery->mutex);

    if(discovery->thread) {
        thread = discovery->thread;
        discovery->thread = NULL;
        discovery->joining = TRUE;
    }
    g_mutex_unlock(&discovery->mutex);

    if(dispatch_source_id != 0)
        g_source_remove(dispatch_source_id);
    director_ndi_dispatch_free(pending_dispatch);

    if(thread)
        g_thread_join(thread);

    g_mutex_lock(&discovery->mutex);
    if(thread)
        discovery->joining = FALSE;
    discovery->started = FALSE;
    g_cond_broadcast(&discovery->cond);
    g_mutex_unlock(&discovery->mutex);
}

void director_ndi_discovery_free(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;
    director_ndi_discovery_stop(discovery);
    director_ndi_discovery_unref(discovery);
}

void director_ndi_shutdown(void)
{
    director_ndi_runtime_init();
    g_mutex_lock(&ndi_runtime.mutex);
    if(ndi_runtime.references == 0) {
        if(ndi_runtime.api && ndi_runtime.api->destroy)
            ndi_runtime.api->destroy();
        if(ndi_runtime.library)
            dlclose(ndi_runtime.library);
        ndi_runtime.library = NULL;
        ndi_runtime.api = NULL;
        ndi_runtime.probed = FALSE;
        g_strlcpy(ndi_runtime.version, "unavailable", sizeof(ndi_runtime.version));
    }
    g_mutex_unlock(&ndi_runtime.mutex);
}

#else

gboolean director_ndi_available(void) { return FALSE; }
const gchar *director_ndi_version(void) { return "not built"; }
void director_ndi_source_free(DirectorNdiSource *source)
{
    if(!source)
        return;
    g_free(source->name);
    g_free(source->url);
    g_free(source);
}
GPtrArray *director_ndi_discover(guint timeout_ms, GError **error)
{
    (void)timeout_ms;
    GPtrArray *sources = g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                        "Director was built without NDI support");
    return sources;
}

struct _DirectorNdiDiscovery {
    DirectorNdiDiscoveryFunc callback;
    gpointer user_data;
    guint64 generation;
    gboolean callback_enabled;
};

static void director_ndi_stub_publish(DirectorNdiDiscovery *discovery, gboolean forced)
{
    if(!discovery || !discovery->callback_enabled || !discovery->callback)
        return;
    GPtrArray *sources = g_ptr_array_new_with_free_func((GDestroyNotify)director_ndi_source_free);
    DirectorNdiDiscoveryUpdate update;
    update.state = DIRECTOR_NDI_DISCOVERY_UNAVAILABLE;
    update.generation = ++discovery->generation;
    update.observed_us = g_get_monotonic_time();
    update.runtime_version = "not built";
    update.sources = sources;
    update.error = "Director was built without NDI support";
    update.forced = forced;
    discovery->callback(discovery, &update, discovery->user_data);
    g_ptr_array_free(sources, TRUE);
}

DirectorNdiDiscovery *director_ndi_discovery_new(DirectorNdiDiscoveryFunc callback,
                                                  gpointer user_data)
{
    DirectorNdiDiscovery *discovery = g_new0(DirectorNdiDiscovery, 1);
    discovery->callback = callback;
    discovery->user_data = user_data;
    return discovery;
}
void director_ndi_discovery_start(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;
    discovery->callback_enabled = TRUE;
    director_ndi_stub_publish(discovery, TRUE);
}
void director_ndi_discovery_request_refresh(DirectorNdiDiscovery *discovery)
{
    director_ndi_stub_publish(discovery, TRUE);
}
void director_ndi_discovery_stop(DirectorNdiDiscovery *discovery)
{
    if(discovery)
        discovery->callback_enabled = FALSE;
}
void director_ndi_discovery_free(DirectorNdiDiscovery *discovery)
{
    if(!discovery)
        return;
    director_ndi_discovery_stop(discovery);
    g_free(discovery);
}
void director_ndi_shutdown(void) { }

#endif
