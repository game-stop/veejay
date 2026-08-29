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
#include "director-model.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#define DIRECTOR_ERROR director_error_quark()

typedef enum {
    DIRECTOR_ERROR_INVALID = 1,
    DIRECTOR_ERROR_DUPLICATE,
    DIRECTOR_ERROR_PARSE
} DirectorError;

static GQuark director_error_quark(void)
{
    return g_quark_from_static_string("veejay-director-error");
}

static gint clamp_int(gint value, gint minimum, gint maximum)
{
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

DirectorInputRoute *director_input_route_new(DirectorInputRouteType type,
                                              const gchar *source_instance_id,
                                              const gchar *host,
                                              gint port,
                                              const gchar *ndi_source_name)
{
    DirectorInputRoute *route = g_new0(DirectorInputRoute, 1);
    route->type = type;
    route->source_instance_id = g_strdup(source_instance_id ? source_instance_id : "");
    route->host = g_strdup(host ? host : "");
    route->port = port;
    route->ndi_source_name = g_strdup(ndi_source_name ? ndi_source_name : "");
    return route;
}

DirectorInputRoute *director_input_route_copy(const DirectorInputRoute *route)
{
    if(!route)
        return NULL;
    DirectorInputRoute *copy = director_input_route_new(route->type,
        route->source_instance_id, route->host, route->port, route->ndi_source_name);
    copy->shm_key = route->shm_key;
    copy->applied_connection = route->applied_connection;
    copy->live_stream_id = route->live_stream_id;
    copy->live_active = route->live_active;
    copy->live_current = route->live_current;
    return copy;
}

void director_input_route_free(DirectorInputRoute *route)
{
    if(!route)
        return;
    g_free(route->source_instance_id);
    g_free(route->host);
    g_free(route->ndi_source_name);
    g_free(route);
}

static gboolean director_input_route_matches(const DirectorInputRoute *route,
                                              DirectorInputRouteType type,
                                              const gchar *source_instance_id,
                                              const gchar *host,
                                              gint port,
                                              const gchar *ndi_source_name)
{
    if(!route || route->type != type)
        return FALSE;
    if(type == DIRECTOR_INPUT_ROUTE_NDI)
        return g_strcmp0(route->ndi_source_name, ndi_source_name ? ndi_source_name : "") == 0;
    if(source_instance_id && *source_instance_id &&
       route->source_instance_id && *route->source_instance_id)
        return g_strcmp0(route->source_instance_id, source_instance_id) == 0;
    return route->port == port &&
           g_ascii_strcasecmp(route->host ? route->host : "", host ? host : "") == 0;
}

DirectorInputRoute *director_instance_find_input_route(const DirectorInstance *instance,
                                                        DirectorInputRouteType type,
                                                        const gchar *source_instance_id,
                                                        const gchar *host,
                                                        gint port,
                                                        const gchar *ndi_source_name)
{
    if(!instance || !instance->input_routes)
        return NULL;
    for(guint i = 0; i < instance->input_routes->len; i++) {
        DirectorInputRoute *route = g_ptr_array_index(instance->input_routes, i);
        if(director_input_route_matches(route, type, source_instance_id, host, port, ndi_source_name))
            return route;
    }
    return NULL;
}

gboolean director_instance_add_input_route(DirectorInstance *instance,
                                            DirectorInputRouteType type,
                                            const gchar *source_instance_id,
                                            const gchar *host,
                                            gint port,
                                            const gchar *ndi_source_name)
{
    if(!instance)
        return FALSE;
    if(!instance->input_routes)
        instance->input_routes = g_ptr_array_new_with_free_func((GDestroyNotify)director_input_route_free);
    DirectorInputRoute *route = director_instance_find_input_route(instance, type,
        source_instance_id, host, port, ndi_source_name);
    if(route) {
        g_free(route->host);
        route->host = g_strdup(host ? host : "");
        route->port = port;
        return FALSE;
    }
    g_ptr_array_add(instance->input_routes,
        director_input_route_new(type, source_instance_id, host, port, ndi_source_name));
    return TRUE;
}

gboolean director_instance_remove_input_route(DirectorInstance *instance,
                                               DirectorInputRouteType type,
                                               const gchar *source_instance_id,
                                               const gchar *host,
                                               gint port,
                                               const gchar *ndi_source_name)
{
    if(!instance || !instance->input_routes)
        return FALSE;
    for(guint i = 0; i < instance->input_routes->len; i++) {
        DirectorInputRoute *route = g_ptr_array_index(instance->input_routes, i);
        if(director_input_route_matches(route, type, source_instance_id, host, port, ndi_source_name)) {
            g_ptr_array_remove_index(instance->input_routes, i);
            return TRUE;
        }
    }
    return FALSE;
}

void director_instance_clear_input_routes(DirectorInstance *instance)
{
    if(instance && instance->input_routes)
        g_ptr_array_set_size(instance->input_routes, 0);
}

const gchar *director_role_name(DirectorRole role)
{
    switch(role) {
        case DIRECTOR_ROLE_PROGRAM: return "program";
        case DIRECTOR_ROLE_OUTPUT: return "output";
        default: return "standalone";
    }
}

const gchar *director_startup_mode_name(DirectorStartupMode mode)
{
    return mode == DIRECTOR_STARTUP_MEDIA ? "media" : "blank";
}

DirectorStartupMode director_startup_mode_from_string(const gchar *name)
{
    return g_strcmp0(name, "media") == 0 ?
           DIRECTOR_STARTUP_MEDIA : DIRECTOR_STARTUP_BLANK;
}

const gchar *director_control_mode_name(DirectorControlMode mode)
{
    switch(mode) {
        case DIRECTOR_CONTROL_MASTER: return "master";
        case DIRECTOR_CONTROL_PREVIEW: return "preview";
        default: return "independent";
    }
}

DirectorControlMode director_control_mode_from_string(const gchar *name)
{
    if(g_strcmp0(name, "master") == 0)
        return DIRECTOR_CONTROL_MASTER;
    if(g_strcmp0(name, "preview") == 0)
        return DIRECTOR_CONTROL_PREVIEW;
    return DIRECTOR_CONTROL_INDEPENDENT;
}

gboolean director_instance_id_valid(const gchar *id)
{
    if(!id || !*id || strlen(id) > 63)
        return FALSE;
    for(const unsigned char *p = (const unsigned char*)id; *p; p++) {
        if(!(g_ascii_isalnum(*p) || *p == '-' || *p == '_' || *p == '.'))
            return FALSE;
    }
    return TRUE;
}

DirectorRole director_role_from_string(const gchar *name)
{
    if(g_strcmp0(name, "program") == 0)
        return DIRECTOR_ROLE_PROGRAM;
    if(g_strcmp0(name, "output") == 0)
        return DIRECTOR_ROLE_OUTPUT;
    return DIRECTOR_ROLE_STANDALONE;
}

void director_slice_set_identity(DirectorSlice *slice, gint width, gint height)
{
    if(!slice)
        return;
    memset(slice, 0, sizeof(*slice));
    slice->enabled = TRUE;
    slice->source_width = 10000;
    slice->source_height = 10000;
    slice->dest_width = MAX(1, width);
    slice->dest_height = MAX(1, height);
    slice->blend_gamma = 100;
}

DirectorInstance *director_instance_new(const gchar *id, DirectorRole role)
{
    DirectorInstance *instance = g_new0(DirectorInstance, 1);
    instance->id = g_strdup(id && *id ? id : "veejay");
    instance->role = role;
    instance->host = g_strdup("127.0.0.1");
    instance->port = role == DIRECTOR_ROLE_OUTPUT ? 4490 : 3490;
    instance->output_width = 1280;
    instance->output_height = 720;
    instance->input_width = 1280;
    instance->input_height = 720;
    instance->startup_mode = DIRECTOR_STARTUP_BLANK;
    instance->media_files = g_ptr_array_new_with_free_func(g_free);
    instance->fps = 25.0;
    instance->norm = -1;
    instance->output_driver = 0;
    instance->output_file = g_strdup("");
    instance->yuv_mode = 0;
    instance->sync_correction = TRUE;
    instance->audio_enabled = role != DIRECTOR_ROLE_OUTPUT;
    instance->audio_muted = FALSE;
    instance->audio_sync_thread = role != DIRECTOR_ROLE_OUTPUT;
    instance->audio_beat_thread = role != DIRECTOR_ROLE_OUTPUT;
    instance->auto_loop = FALSE;
    instance->clip_as_sample = TRUE;
    instance->deinterlace = FALSE;
    instance->legacy_viewport = role != DIRECTOR_ROLE_PROGRAM;
    instance->borderless = FALSE;
    instance->fullscreen = FALSE;
    instance->no_keyboard = role == DIRECTOR_ROLE_OUTPUT;
    instance->no_mouse = role == DIRECTOR_ROLE_OUTPUT;
    instance->show_cursor = FALSE;
    instance->verbose = FALSE;
    instance->no_color = FALSE;
    instance->window_width = 0;
    instance->window_height = 0;
    instance->window_x = -1;
    instance->window_y = -1;
    instance->memory_percent = -1;
    instance->max_cache = 0;
    instance->timer_mode = 1;
    instance->pace_correction_ms = 0;
    instance->audio_rate = 48000;
    instance->audio_channels = 2;
    instance->audio_bits = 16;
    instance->ndi_input_enabled = FALSE;
    instance->ndi_source_name = g_strdup("");
    instance->ndi_output_enabled = FALSE;
    instance->ndi_output_name = g_strdup_printf("VeeJay %s", instance->id);
    instance->ndi_tally_enabled = TRUE;
    instance->ndi_follow_clock = FALSE;
    instance->scene_detection = -1;
    instance->capture_device = -1;
    instance->generator_stream = -1;
    instance->swap_range = FALSE;
    instance->dynamic_fx_chain = FALSE;
    instance->fx_custom_defaults = FALSE;
    instance->preserve_pathnames = FALSE;
    instance->bezerk = FALSE;
    instance->split_screen_file = g_strdup("");
    instance->split_master_instance_id = g_strdup("");
    instance->split_row = -1;
    instance->split_column = -1;
    instance->multicast_osc = g_strdup("");
    instance->multicast_vims = g_strdup("");
    instance->sample_file = g_strdup("");
    instance->action_file = g_strdup("");
    instance->working_directory = g_build_filename(g_get_home_dir(),
                                                  ".veejay", "director",
                                                  instance->id, NULL);
    instance->source_host = g_strdup("");
    instance->source_port = 3490;
    instance->input_routes = g_ptr_array_new_with_free_func((GDestroyNotify)director_input_route_free);
    instance->live_input_routes = g_ptr_array_new_with_free_func((GDestroyNotify)director_input_route_free);
    instance->live_route_sdl_display_index = -1;
    instance->stream_advertise_host = g_strdup("");
    instance->control_mode = DIRECTOR_CONTROL_INDEPENDENT;
    instance->master_host = g_strdup("127.0.0.1");
    instance->master_port = 3490;
    instance->preview_forward_vims = FALSE;
    instance->preview_sync_samplelist = FALSE;
    instance->preview_headless = FALSE;
    instance->display_id = g_strdup("");
    instance->display_name = g_strdup("");
    instance->display_connector = g_strdup("");
    instance->display_index = -1;
    instance->display_x = 0;
    instance->display_y = 0;
    instance->display_width = 0;
    instance->display_height = 0;
    instance->stage_x = 0;
    instance->stage_y = 0;
    instance->stage_position_explicit = FALSE;
    instance->wiring_x = 0;
    instance->wiring_y = 0;
    instance->wiring_position_explicit = FALSE;
    instance->executable = g_strdup("veejay");
    instance->extra_args = g_strdup("");
    instance->eidolon_enabled = FALSE;
    instance->eidolon_executable = g_strdup("eidolon");
    instance->eidolon_extra_args = g_strdup("");
    instance->eidolon_pty_fd = -1;
    instance->eidolon_transcript = g_string_new(NULL);
    instance->eidolon_history = g_ptr_array_new_with_free_func(g_free);
    instance->eidolon_history_cursor = -1;
    instance->managed = TRUE;
    instance->autostart = TRUE;
    instance->apply_on_connect = TRUE;
    instance->discovered_transient = FALSE;
    instance->calibration_camera = FALSE;
    instance->recovery_restart_engine = TRUE;
    instance->recovery_reconnect_route = TRUE;
    instance->recovery_restore_projection = TRUE;
    instance->recovery_restore_mapping = TRUE;
    instance->recovery_restore_control = TRUE;
    instance->recovery_retry_limit = 3;
    instance->live_role = -1;
    instance->live_pattern = -1;
    instance->live_v4l_stream_id = 0;
    for(gint i = 0; i < DIRECTOR_V4L_CONTROL_COUNT; i++)
        instance->live_v4l_controls[i] = -1;
    instance->pattern = 0;
    instance->stages = g_ptr_array_new_with_free_func(g_free);
    director_slice_set_identity(&instance->slices[0],
                                instance->output_width,
                                instance->output_height);
    for(gint i = 1; i < DIRECTOR_MAX_SLICES; i++) {
        director_slice_set_identity(&instance->slices[i],
                                    instance->output_width,
                                    instance->output_height);
        instance->slices[i].enabled = FALSE;
    }
    return instance;
}

DirectorInstance *director_instance_clone_configuration(const DirectorInstance *source,
                                                        const gchar *id)
{
    if(!source)
        return NULL;
    DirectorInstance *copy = director_instance_new(id, source->role);
#define COPY_STR(field) do { g_free(copy->field); copy->field = g_strdup(source->field ? source->field : ""); } while(0)
    COPY_STR(host); copy->port = source->port;
    copy->output_width = source->output_width; copy->output_height = source->output_height;
    copy->input_width = source->input_width; copy->input_height = source->input_height;
    copy->startup_mode = source->startup_mode;
    g_ptr_array_set_size(copy->media_files, 0);
    for(guint i = 0; source->media_files && i < source->media_files->len; i++)
        g_ptr_array_add(copy->media_files, g_strdup(g_ptr_array_index(source->media_files, i)));
    copy->fps = source->fps; copy->norm = source->norm; copy->output_driver = source->output_driver;
    COPY_STR(output_file); copy->yuv_mode = source->yuv_mode; copy->sync_correction = source->sync_correction;
    copy->audio_enabled = source->audio_enabled; copy->audio_muted = source->audio_muted;
    copy->audio_sync_thread = source->audio_sync_thread; copy->audio_beat_thread = source->audio_beat_thread;
    copy->auto_loop = source->auto_loop; copy->clip_as_sample = source->clip_as_sample;
    copy->deinterlace = source->deinterlace; copy->legacy_viewport = source->legacy_viewport;
    copy->borderless = source->borderless; copy->fullscreen = source->fullscreen;
    copy->no_keyboard = source->no_keyboard; copy->no_mouse = source->no_mouse;
    copy->show_cursor = source->show_cursor; copy->verbose = source->verbose; copy->no_color = source->no_color;
    copy->window_width = source->window_width; copy->window_height = source->window_height;
    copy->window_x = source->window_x; copy->window_y = source->window_y;
    copy->memory_percent = source->memory_percent; copy->max_cache = source->max_cache;
    copy->timer_mode = source->timer_mode; copy->pace_correction_ms = source->pace_correction_ms;
    copy->audio_rate = source->audio_rate; copy->audio_channels = source->audio_channels; copy->audio_bits = source->audio_bits;
    copy->ndi_input_enabled = source->ndi_input_enabled; COPY_STR(ndi_source_name);
    copy->ndi_output_enabled = source->ndi_output_enabled; COPY_STR(ndi_output_name);
    copy->ndi_tally_enabled = source->ndi_tally_enabled; copy->ndi_follow_clock = source->ndi_follow_clock;
    copy->scene_detection = source->scene_detection; copy->capture_device = source->capture_device;
    copy->generator_stream = source->generator_stream; copy->swap_range = source->swap_range;
    copy->dynamic_fx_chain = source->dynamic_fx_chain; copy->fx_custom_defaults = source->fx_custom_defaults;
    copy->preserve_pathnames = source->preserve_pathnames; copy->bezerk = source->bezerk;
    COPY_STR(split_screen_file); COPY_STR(split_master_instance_id);
    copy->split_row = source->split_row; copy->split_column = source->split_column;
    COPY_STR(multicast_osc); COPY_STR(multicast_vims); COPY_STR(sample_file); COPY_STR(action_file);
    COPY_STR(source_instance_id); COPY_STR(source_host); copy->source_port = source->source_port;
    g_ptr_array_set_size(copy->input_routes, 0);
    for(guint route_i = 0; source->input_routes && route_i < source->input_routes->len; route_i++)
        g_ptr_array_add(copy->input_routes, director_input_route_copy(g_ptr_array_index(source->input_routes, route_i)));
    COPY_STR(stream_advertise_host); copy->control_mode = source->control_mode;
    COPY_STR(master_instance_id); COPY_STR(master_host); copy->master_port = source->master_port;
    copy->preview_forward_vims = source->preview_forward_vims;
    copy->preview_sync_samplelist = source->preview_sync_samplelist; copy->preview_headless = source->preview_headless;
    COPY_STR(display_id); COPY_STR(display_name); COPY_STR(display_connector); copy->display_index = source->display_index;
    copy->display_x = source->display_x; copy->display_y = source->display_y;
    copy->display_width = source->display_width; copy->display_height = source->display_height;
    copy->stage_x = source->stage_x; copy->stage_y = source->stage_y;
    copy->stage_position_explicit = source->stage_position_explicit;
    copy->wiring_x = source->wiring_x + 36; copy->wiring_y = source->wiring_y + 36;
    copy->wiring_position_explicit = source->wiring_position_explicit;
    COPY_STR(executable); COPY_STR(extra_args);
    copy->eidolon_enabled = source->eidolon_enabled; COPY_STR(eidolon_executable); COPY_STR(eidolon_extra_args);
    copy->managed = source->managed; copy->autostart = source->autostart; copy->apply_on_connect = source->apply_on_connect;
    copy->calibration_camera = FALSE;
    copy->pattern = source->pattern;
    copy->recovery_restart_engine = source->recovery_restart_engine;
    copy->recovery_reconnect_route = source->recovery_reconnect_route;
    copy->recovery_restore_projection = source->recovery_restore_projection;
    copy->recovery_restore_mapping = source->recovery_restore_mapping;
    copy->recovery_restore_control = source->recovery_restore_control;
    copy->recovery_retry_limit = source->recovery_retry_limit;
    memcpy(copy->slices, source->slices, sizeof(copy->slices));
    copy->configured_projection = source->configured_projection;
#undef COPY_STR
    return copy;
}

void director_instance_free(DirectorInstance *instance)
{
    if(!instance)
        return;
    g_free(instance->id);
    g_free(instance->host);
    if(instance->media_files)
        g_ptr_array_free(instance->media_files, TRUE);
    g_free(instance->split_screen_file);
    g_free(instance->split_master_instance_id);
    g_free(instance->multicast_osc);
    g_free(instance->multicast_vims);
    g_free(instance->sample_file);
    g_free(instance->action_file);
    g_free(instance->working_directory);
    g_free(instance->output_file);
    g_free(instance->ndi_source_name);
    g_free(instance->ndi_output_name);
    g_free(instance->source_instance_id);
    g_free(instance->source_host);
    if(instance->input_routes)
        g_ptr_array_free(instance->input_routes, TRUE);
    if(instance->live_input_routes)
        g_ptr_array_free(instance->live_input_routes, TRUE);
    g_free(instance->stream_advertise_host);
    g_free(instance->master_instance_id);
    g_free(instance->master_host);
    g_free(instance->display_id);
    g_free(instance->display_name);
    g_free(instance->display_connector);
    g_free(instance->executable);
    g_free(instance->extra_args);
    g_free(instance->eidolon_executable);
    g_free(instance->eidolon_extra_args);
    g_free(instance->live_id);
    g_free(instance->live_source);
    g_free(instance->live_ndi_runtime);
    g_free(instance->live_ndi_source);
    g_free(instance->live_ndi_tx_name);
    g_free(instance->live_ndi_tx_source);
    g_free(instance->live_ndi_tx_url);
    g_free(instance->live_ndi_tx_instance_id);
    g_free(instance->live_ndi_tx_role);
    g_free(instance->live_route_ndi_output_name);
    g_free(instance->ndi_input_pending_name);
    g_free(instance->ndi_input_pending_native_source_id);
    g_free(instance->ndi_input_pending_native_host);
    g_free(instance->ndi_output_pending_name);
    g_free(instance->last_error);
    g_free(instance->last_instance_status);
    g_free(instance->last_output_status);
    g_free(instance->last_projection_status);
    g_free(instance->last_perf_status);
    g_free(instance->last_ndi_status);
    g_free(instance->last_routing_status);
    g_free(instance->recovery_editlist_path);
    g_free(instance->recovery_samplelist_path);
    if(instance->stages)
        g_ptr_array_free(instance->stages, TRUE);
    if(instance->force_stop_timer)
        g_source_remove(instance->force_stop_timer);
    if(instance->eidolon_force_stop_timer)
        g_source_remove(instance->eidolon_force_stop_timer);
    if(instance->recovery_timer)
        g_source_remove(instance->recovery_timer);
    if(instance->restart_timer)
        g_source_remove(instance->restart_timer);
    if(instance->eidolon_io_watch)
        g_source_remove(instance->eidolon_io_watch);
    if(instance->eidolon_pty_channel) {
        g_io_channel_shutdown(instance->eidolon_pty_channel, FALSE, NULL);
        g_io_channel_unref(instance->eidolon_pty_channel);
    }
    if(instance->eidolon_process)
        g_object_unref(instance->eidolon_process);
    if(instance->reloaded_force_stop_timer)
        g_source_remove(instance->reloaded_force_stop_timer);
    if(instance->reloaded_process)
        g_object_unref(instance->reloaded_process);
    if(instance->eidolon_transcript)
        g_string_free(instance->eidolon_transcript, TRUE);
    if(instance->eidolon_history)
        g_ptr_array_free(instance->eidolon_history, TRUE);
    if(instance->process)
        g_object_unref(instance->process);
    g_free(instance);
}

static void director_venue_output_free(DirectorVenueOutput *output)
{
    if(!output)
        return;
    g_free(output->instance_id);
    g_free(output->display_id);
    g_free(output->display_name);
    g_free(output->display_connector);
    g_free(output->split_master_instance_id);
    g_free(output);
}

static void director_projector_camera_map_free(DirectorProjectorCameraMap *map)
{
    if(!map)
        return;
    g_free(map->instance_id);
    if(map->camera_to_projector)
        g_bytes_unref(map->camera_to_projector);
    g_free(map);
}

static void director_venue_profile_free(DirectorVenueProfile *profile)
{
    if(!profile)
        return;
    g_free(profile->name);
    g_free(profile->calibration_camera_id);
    g_free(profile->calibration_camera_path);
    g_free(profile->calibration_camera_name);
    if(profile->outputs)
        g_ptr_array_free(profile->outputs, TRUE);
    if(profile->camera_maps)
        g_ptr_array_free(profile->camera_maps, TRUE);
    g_free(profile);
}

static void director_snapshot_instance_free(DirectorSnapshotInstance *entry)
{
    if(!entry)
        return;
    g_free(entry->instance_id);
    g_free(entry->source_instance_id);
    g_free(entry->source_host);
    if(entry->input_routes)
        g_ptr_array_free(entry->input_routes, TRUE);
    g_free(entry->master_instance_id);
    g_free(entry->master_host);
    g_free(entry->ndi_source_name);
    g_free(entry->ndi_output_name);
    g_free(entry->physical.display_id);
    g_free(entry->physical.display_name);
    g_free(entry->physical.display_connector);
    g_free(entry->physical.split_master_instance_id);
    g_free(entry);
}

typedef struct {
    gchar *key;
    gint x;
    gint y;
} DirectorNdiPatchPosition;

static void director_ndi_patch_position_free(DirectorNdiPatchPosition *position)
{
    if(!position)
        return;
    g_free(position->key);
    g_free(position);
}

static void director_show_snapshot_free(DirectorShowSnapshot *snapshot)
{
    if(!snapshot)
        return;
    g_free(snapshot->name);
    if(snapshot->instances)
        g_ptr_array_free(snapshot->instances, TRUE);
    g_free(snapshot);
}

DirectorShow *director_show_new(const gchar *name)
{
    DirectorShow *show = g_new0(DirectorShow, 1);
    show->name = g_strdup(name && *name ? name : "Untitled show");
    show->launch_reloaded = FALSE;
    show->reloaded_executable = g_strdup("reloaded");
    show->reloaded_args = g_strdup("-a");
    show->active_venue = g_strdup("");
    show->venue_profiles = g_ptr_array_new_with_free_func((GDestroyNotify)director_venue_profile_free);
    show->snapshots = g_ptr_array_new_with_free_func((GDestroyNotify)director_show_snapshot_free);
    show->ndi_patch_positions = g_ptr_array_new_with_free_func(
        (GDestroyNotify)director_ndi_patch_position_free);
    show->instances = g_ptr_array_new_with_free_func((GDestroyNotify)director_instance_free);
    return show;
}

void director_show_free(DirectorShow *show)
{
    if(!show)
        return;
    g_free(show->name);
    g_free(show->path);
    g_free(show->reloaded_executable);
    g_free(show->reloaded_args);
    g_free(show->active_venue);
    if(show->venue_profiles)
        g_ptr_array_free(show->venue_profiles, TRUE);
    if(show->snapshots)
        g_ptr_array_free(show->snapshots, TRUE);
    if(show->ndi_patch_positions)
        g_ptr_array_free(show->ndi_patch_positions, TRUE);
    if(show->instances)
        g_ptr_array_free(show->instances, TRUE);
    g_free(show);
}

gboolean director_show_get_ndi_patch_position(const DirectorShow *show,
                                               const gchar *key,
                                               gint *x, gint *y)
{
    if(!show || !key || !*key)
        return FALSE;
    for(guint i = 0; show->ndi_patch_positions &&
                    i < show->ndi_patch_positions->len; i++) {
        const DirectorNdiPatchPosition *position =
            g_ptr_array_index(show->ndi_patch_positions, i);
        if(position && g_strcmp0(position->key, key) == 0) {
            if(x) *x = position->x;
            if(y) *y = position->y;
            return TRUE;
        }
    }
    return FALSE;
}

void director_show_set_ndi_patch_position(DirectorShow *show,
                                          const gchar *key,
                                          gint x, gint y)
{
    if(!show || !key || !*key)
        return;
    for(guint i = 0; show->ndi_patch_positions &&
                    i < show->ndi_patch_positions->len; i++) {
        DirectorNdiPatchPosition *position =
            g_ptr_array_index(show->ndi_patch_positions, i);
        if(position && g_strcmp0(position->key, key) == 0) {
            if(position->x != x || position->y != y) {
                position->x = x;
                position->y = y;
                show->dirty = TRUE;
            }
            return;
        }
    }
    DirectorNdiPatchPosition *position = g_new0(DirectorNdiPatchPosition, 1);
    position->key = g_strdup(key);
    position->x = x;
    position->y = y;
    g_ptr_array_add(show->ndi_patch_positions, position);
    show->dirty = TRUE;
}

void director_show_clear_ndi_patch_positions(DirectorShow *show)
{
    if(!show || !show->ndi_patch_positions || show->ndi_patch_positions->len == 0)
        return;
    g_ptr_array_set_size(show->ndi_patch_positions, 0);
    show->dirty = TRUE;
}

DirectorInstance *director_show_find_instance(DirectorShow *show, const gchar *id)
{
    if(!show || !id)
        return NULL;
    for(guint i = 0; i < show->instances->len; i++) {
        DirectorInstance *instance = g_ptr_array_index(show->instances, i);
        if(g_strcmp0(instance->id, id) == 0)
            return instance;
    }
    return NULL;
}

DirectorInstance *director_show_find_source_for_instance(DirectorShow *show,
                                                         const DirectorInstance *target)
{
    if(!show || !target || !target->source_instance_id || !*target->source_instance_id)
        return NULL;
    DirectorInstance *source = director_show_find_instance(show, target->source_instance_id);
    return source == target ? NULL : source;
}

gboolean director_show_add_instance(DirectorShow *show,
                                    DirectorInstance *instance,
                                    GError **error)
{
    if(!show || !instance || !director_instance_id_valid(instance->id)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance ID must be 1–63 characters using letters, digits, '.', '_' or '-'");
        return FALSE;
    }
    if(director_show_find_instance(show, instance->id)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_DUPLICATE,
                    "An instance named '%s' already exists", instance->id);
        return FALSE;
    }
    g_ptr_array_add(show->instances, instance);
    show->dirty = TRUE;
    return TRUE;
}

void director_show_remove_instance(DirectorShow *show, DirectorInstance *instance)
{
    if(!show || !instance)
        return;
    if(g_ptr_array_remove(show->instances, instance))
        show->dirty = TRUE;
}

static gchar *slice_group_name(guint instance_index, gint slice_index)
{
    return g_strdup_printf("Instance %u Slice %d", instance_index, slice_index);
}

static void key_file_set_slice(GKeyFile *file, const gchar *group,
                               const DirectorSlice *slice)
{
    gint values[14] = {
        slice->enabled ? 1 : 0,
        slice->source_x, slice->source_y,
        slice->source_width, slice->source_height,
        slice->dest_x, slice->dest_y,
        slice->dest_width, slice->dest_height,
        slice->blend_left, slice->blend_right,
        slice->blend_top, slice->blend_bottom,
        slice->blend_gamma
    };
    g_key_file_set_integer_list(file, group, "values", values, G_N_ELEMENTS(values));
}

static gboolean key_file_get_slice(GKeyFile *file, const gchar *group,
                                   DirectorSlice *slice)
{
    gsize length = 0;
    GError *error = NULL;
    gint *values = g_key_file_get_integer_list(file, group, "values", &length, &error);
    if(error) {
        g_error_free(error);
        return FALSE;
    }
    if(!values || length != 14) {
        g_free(values);
        return FALSE;
    }
    slice->enabled = values[0] != 0;
    slice->source_x = values[1];
    slice->source_y = values[2];
    slice->source_width = values[3];
    slice->source_height = values[4];
    slice->dest_x = values[5];
    slice->dest_y = values[6];
    slice->dest_width = values[7];
    slice->dest_height = values[8];
    slice->blend_left = values[9];
    slice->blend_right = values[10];
    slice->blend_top = values[11];
    slice->blend_bottom = values[12];
    slice->blend_gamma = values[13];
    g_free(values);
    return TRUE;
}


static gboolean director_slice_values_valid(const DirectorSlice *slice,
                                            gint output_width,
                                            gint output_height)
{
    return slice && output_width > 0 && output_height > 0 &&
        slice->source_x >= 0 && slice->source_y >= 0 &&
        slice->source_width > 0 && slice->source_height > 0 &&
        (gint64)slice->source_x + slice->source_width <= 10000 &&
        (gint64)slice->source_y + slice->source_height <= 10000 &&
        slice->dest_x >= 0 && slice->dest_y >= 0 &&
        slice->dest_width > 0 && slice->dest_height > 0 &&
        (gint64)slice->dest_x + slice->dest_width <= output_width &&
        (gint64)slice->dest_y + slice->dest_height <= output_height &&
        slice->blend_left >= 0 && slice->blend_right >= 0 &&
        slice->blend_top >= 0 && slice->blend_bottom >= 0 &&
        (gint64)slice->blend_left + slice->blend_right <= slice->dest_width &&
        (gint64)slice->blend_top + slice->blend_bottom <= slice->dest_height &&
        slice->blend_gamma >= 10 && slice->blend_gamma <= 1000;
}

void director_projection_config_from_live(DirectorProjectionConfig *config,
                                         const DirectorInstance *instance)
{
    if(!config)
        return;
    memset(config, 0, sizeof(*config));
    if(!instance || !instance->live_projection_valid)
        return;
    config->valid = TRUE;
    config->enabled = instance->live_projection_enabled;
    config->startup_enabled = instance->live_projection_startup_enabled;
    config->mode = instance->live_projection_mode;
    config->columns = instance->live_projection_columns;
    config->rows = instance->live_projection_rows;
    config->selected_point = instance->live_projection_selected_point;
    config->point_count = instance->live_projection_point_count;
    config->output_width = instance->live_projection_output_width;
    config->output_height = instance->live_projection_output_height;
    config->source_x = instance->live_projection_source_x;
    config->source_y = instance->live_projection_source_y;
    config->source_width = instance->live_projection_source_width;
    config->source_height = instance->live_projection_source_height;
    config->scale = instance->live_projection_scale;
    if(config->point_count > 0 && config->point_count <= DIRECTOR_MAX_PROJECTION_POINTS)
        memcpy(config->points, instance->live_projection_points,
               sizeof(gdouble) * (gsize)config->point_count * 2u);
}

void director_projection_config_to_instance(DirectorInstance *instance,
                                           const DirectorProjectionConfig *config)
{
    if(!instance || !config)
        return;
    instance->configured_projection = *config;
}

static void director_venue_output_capture(DirectorVenueOutput *output,
                                          const DirectorInstance *instance)
{
    output->instance_id = g_strdup(instance->id);
    output->output_width = instance->output_width;
    output->output_height = instance->output_height;
    output->display_id = g_strdup(instance->display_id ? instance->display_id : "");
    output->display_name = g_strdup(instance->display_name ? instance->display_name : "");
    output->display_connector = g_strdup(instance->display_connector ? instance->display_connector : "");
    output->display_index = instance->display_index;
    output->display_x = instance->display_x;
    output->display_y = instance->display_y;
    output->display_width = instance->display_width;
    output->display_height = instance->display_height;
    output->fullscreen = instance->fullscreen;
    output->borderless = instance->borderless;
    output->window_width = instance->window_width;
    output->window_height = instance->window_height;
    output->window_x = instance->window_x;
    output->window_y = instance->window_y;
    output->split_master_instance_id = g_strdup(instance->split_master_instance_id ?
                                                 instance->split_master_instance_id : "");
    output->split_row = instance->split_row;
    output->split_column = instance->split_column;
    output->graph_width = instance->output_width;
    output->graph_height = instance->output_height;
    memcpy(output->slices, instance->slices, sizeof(output->slices));
    if(instance->live_projection_valid)
        director_projection_config_from_live(&output->projection, instance);
    else
        output->projection = instance->configured_projection;
}

static void director_venue_output_apply(DirectorInstance *instance,
                                        const DirectorVenueOutput *output)
{
    instance->output_width = MAX(16, output->output_width);
    instance->output_height = MAX(16, output->output_height);
    g_free(instance->display_id);
    instance->display_id = g_strdup(output->display_id ? output->display_id : "");
    g_free(instance->display_name);
    instance->display_name = g_strdup(output->display_name ? output->display_name : "");
    g_free(instance->display_connector);
    instance->display_connector = g_strdup(output->display_connector ? output->display_connector : "");
    instance->display_index = output->display_index;
    instance->display_x = output->display_x;
    instance->display_y = output->display_y;
    instance->display_width = output->display_width;
    instance->display_height = output->display_height;
    instance->fullscreen = output->fullscreen;
    instance->borderless = output->borderless;
    instance->window_width = output->window_width;
    instance->window_height = output->window_height;
    instance->window_x = output->window_x;
    instance->window_y = output->window_y;
    g_free(instance->split_master_instance_id);
    instance->split_master_instance_id = g_strdup(output->split_master_instance_id ?
                                                   output->split_master_instance_id : "");
    instance->split_row = output->split_row;
    instance->split_column = output->split_column;
    memcpy(instance->slices, output->slices, sizeof(instance->slices));
    for(gint i = 0; i < DIRECTOR_MAX_SLICES; i++) {
        if(!director_slice_values_valid(&instance->slices[i],
                                        instance->output_width,
                                        instance->output_height)) {
            director_slice_set_identity(&instance->slices[i],
                                        instance->output_width,
                                        instance->output_height);
            instance->slices[i].enabled = i == 0;
        }
    }
    director_projection_config_to_instance(instance, &output->projection);
    instance->graph_applied_connection = FALSE;
}

DirectorVenueProfile *director_show_find_venue(DirectorShow *show, const gchar *name)
{
    if(!show || !name)
        return NULL;
    for(guint i = 0; i < show->venue_profiles->len; i++) {
        DirectorVenueProfile *profile = g_ptr_array_index(show->venue_profiles, i);
        if(g_strcmp0(profile->name, name) == 0)
            return profile;
    }
    return NULL;
}

DirectorProjectorCameraMap *director_venue_find_camera_map(DirectorVenueProfile *profile,
                                                                  const gchar *instance_id)
{
    if(!profile || !profile->camera_maps || !instance_id || !*instance_id)
        return NULL;
    for(guint i = 0; i < profile->camera_maps->len; i++) {
        DirectorProjectorCameraMap *map = g_ptr_array_index(profile->camera_maps, i);
        if(g_strcmp0(map->instance_id, instance_id) == 0)
            return map;
    }
    return NULL;
}

gboolean director_venue_store_camera_map(DirectorVenueProfile *profile,
                                         const gchar *instance_id,
                                         gint camera_width, gint camera_height,
                                         gint projector_width, gint projector_height,
                                         const guint32 *camera_to_projector,
                                         gsize pixel_count,
                                         guint illuminated_pixels,
                                         guint valid_pixels,
                                         gdouble mean_confidence)
{
    if(!profile || !instance_id || !*instance_id ||
       camera_width <= 0 || camera_height <= 0 ||
       projector_width <= 0 || projector_height <= 0 ||
       camera_width > 8192 || camera_height > 8192 ||
       projector_width > 65534 || projector_height > 65534 ||
       pixel_count != (gsize)camera_width * (gsize)camera_height ||
       pixel_count > (64u * 1024u * 1024u) / sizeof(guint32) ||
       !camera_to_projector || illuminated_pixels == 0 ||
       illuminated_pixels > pixel_count || valid_pixels > illuminated_pixels)
        return FALSE;

    if(!profile->camera_maps)
        profile->camera_maps = g_ptr_array_new_with_free_func((GDestroyNotify)director_projector_camera_map_free);
    DirectorProjectorCameraMap *map = director_venue_find_camera_map(profile, instance_id);
    if(!map) {
        map = g_new0(DirectorProjectorCameraMap, 1);
        map->instance_id = g_strdup(instance_id);
        g_ptr_array_add(profile->camera_maps, map);
    }
    if(map->camera_to_projector)
        g_bytes_unref(map->camera_to_projector);

    guint32 *packed = g_new(guint32, pixel_count);
    for(gsize i = 0; i < pixel_count; i++)
        packed[i] = GUINT32_TO_LE(camera_to_projector[i]);
    map->camera_to_projector = g_bytes_new_take(packed, pixel_count * sizeof(guint32));
    map->camera_width = camera_width;
    map->camera_height = camera_height;
    map->projector_width = projector_width;
    map->projector_height = projector_height;
    map->illuminated_pixels = illuminated_pixels;
    map->valid_pixels = valid_pixels;
    map->valid_fraction = pixel_count ? (gdouble)valid_pixels / (gdouble)pixel_count : 0.0;
    map->decoded_fraction = illuminated_pixels ?
        (gdouble)valid_pixels / (gdouble)illuminated_pixels : 0.0;
    if(!(mean_confidence >= 0.0 && mean_confidence <= 1.0))
        mean_confidence = 0.0;
    map->mean_confidence = mean_confidence;
    map->updated_real_us = g_get_real_time();
    return TRUE;
}

gboolean director_venue_remove_camera_map(DirectorVenueProfile *profile,
                                          const gchar *instance_id)
{
    DirectorProjectorCameraMap *map = director_venue_find_camera_map(profile, instance_id);
    return map && profile->camera_maps ? g_ptr_array_remove(profile->camera_maps, map) : FALSE;
}

gboolean director_projector_camera_map_lookup(const DirectorProjectorCameraMap *map,
                                              gint camera_x, gint camera_y,
                                              gint *projector_x, gint *projector_y)
{
    if(projector_x)
        *projector_x = -1;
    if(projector_y)
        *projector_y = -1;
    if(!map || !map->camera_to_projector ||
       camera_x < 0 || camera_y < 0 ||
       camera_x >= map->camera_width || camera_y >= map->camera_height)
        return FALSE;
    gsize size = 0;
    const guint32 *values = g_bytes_get_data(map->camera_to_projector, &size);
    const gsize count = (gsize)map->camera_width * (gsize)map->camera_height;
    if(!values || size != count * sizeof(guint32))
        return FALSE;
    const guint32 value = GUINT32_FROM_LE(values[(gsize)camera_y * map->camera_width + camera_x]);
    if(value == DIRECTOR_CAMERA_MAP_INVALID)
        return FALSE;
    const gint x = (gint)(value & 0xffffu);
    const gint y = (gint)(value >> 16);
    if(x >= map->projector_width || y >= map->projector_height)
        return FALSE;
    if(projector_x)
        *projector_x = x;
    if(projector_y)
        *projector_y = y;
    return TRUE;
}

static gboolean director_instance_has_venue_output(const DirectorInstance *instance)
{
    if(!instance || instance->discovered_transient || instance->calibration_camera)
        return FALSE;
    if(instance->display_index >= 0 ||
       (instance->split_master_instance_id && *instance->split_master_instance_id) ||
       (instance->split_screen_file && *instance->split_screen_file))
        return TRUE;
    if(instance->output_driver != 0 ||
       (instance->control_mode == DIRECTOR_CONTROL_PREVIEW && instance->preview_headless))
        return FALSE;
    return instance->role != DIRECTOR_ROLE_PROGRAM;
}

static gboolean director_projection_geometry_equal(const DirectorProjectionConfig *a,
                                                   const DirectorProjectionConfig *b)
{
    if(!a || !b || a->valid != b->valid)
        return FALSE;
    if(!a->valid)
        return TRUE;
    if(a->enabled != b->enabled ||
       a->startup_enabled != b->startup_enabled ||
       a->mode != b->mode ||
       a->columns != b->columns || a->rows != b->rows ||
       a->point_count != b->point_count ||
       a->output_width != b->output_width || a->output_height != b->output_height ||
       a->source_x != b->source_x || a->source_y != b->source_y ||
       a->source_width != b->source_width || a->source_height != b->source_height ||
       a->scale != b->scale)
        return FALSE;
    const gint values = CLAMP(a->point_count, 0, DIRECTOR_MAX_PROJECTION_POINTS) * 2;
    for(gint i = 0; i < values; i++) {
        if(fabs(a->points[i] - b->points[i]) > 0.000001)
            return FALSE;
    }
    return TRUE;
}

static gboolean director_venue_output_geometry_equal(const DirectorVenueOutput *a,
                                                      const DirectorVenueOutput *b)
{
    return a && b &&
           g_strcmp0(a->instance_id, b->instance_id) == 0 &&
           a->output_width == b->output_width && a->output_height == b->output_height &&
           g_strcmp0(a->display_id, b->display_id) == 0 &&
           g_strcmp0(a->display_connector, b->display_connector) == 0 &&
           a->display_index == b->display_index &&
           a->display_x == b->display_x && a->display_y == b->display_y &&
           a->display_width == b->display_width && a->display_height == b->display_height &&
           a->fullscreen == b->fullscreen && a->borderless == b->borderless &&
           a->window_width == b->window_width && a->window_height == b->window_height &&
           a->window_x == b->window_x && a->window_y == b->window_y &&
           a->graph_width == b->graph_width && a->graph_height == b->graph_height &&
           director_projection_geometry_equal(&a->projection, &b->projection);
}

static DirectorVenueOutput *director_venue_find_output_in_array(GPtrArray *outputs,
                                                                 const gchar *instance_id)
{
    if(!outputs || !instance_id)
        return NULL;
    for(guint i = 0; i < outputs->len; i++) {
        DirectorVenueOutput *output = g_ptr_array_index(outputs, i);
        if(g_strcmp0(output->instance_id, instance_id) == 0)
            return output;
    }
    return NULL;
}

DirectorVenueProfile *director_show_capture_venue(DirectorShow *show, const gchar *name)
{
    if(!show || !name || !*name)
        return NULL;
    DirectorVenueProfile *profile = director_show_find_venue(show, name);
    if(!profile) {
        profile = g_new0(DirectorVenueProfile, 1);
        profile->name = g_strdup(name);
        profile->camera_maps = g_ptr_array_new_with_free_func((GDestroyNotify)director_projector_camera_map_free);
        g_ptr_array_add(show->venue_profiles, profile);
    }

    GPtrArray *previous_outputs = profile->outputs;
    GPtrArray *new_outputs = g_ptr_array_new_with_free_func((GDestroyNotify)director_venue_output_free);
    for(guint i = 0; i < show->instances->len; i++) {
        DirectorInstance *instance = g_ptr_array_index(show->instances, i);
        if(!director_instance_has_venue_output(instance))
            continue;
        DirectorVenueOutput *previous = director_venue_find_output_in_array(previous_outputs, instance->id);
        DirectorVenueOutput *output = g_new0(DirectorVenueOutput, 1);
        director_venue_output_capture(output, instance);
        if(!instance->live_projection_valid && previous)
            output->projection = previous->projection;
        g_ptr_array_add(new_outputs, output);
    }
    profile->outputs = new_outputs;

    for(gint m = profile->camera_maps ? (gint)profile->camera_maps->len - 1 : -1;
        m >= 0; m--) {
        DirectorProjectorCameraMap *map = g_ptr_array_index(profile->camera_maps, (guint)m);
        DirectorVenueOutput *current = director_venue_find_output_in_array(new_outputs, map->instance_id);
        DirectorVenueOutput *previous = director_venue_find_output_in_array(previous_outputs, map->instance_id);
        const gboolean keep = current && previous &&
            current->output_width == map->projector_width &&
            current->output_height == map->projector_height &&
            director_venue_output_geometry_equal(previous, current);
        if(!keep)
            g_ptr_array_remove_index(profile->camera_maps, (guint)m);
    }
    if(previous_outputs)
        g_ptr_array_free(previous_outputs, TRUE);

    g_free(show->active_venue);
    show->active_venue = g_strdup(name);
    show->dirty = TRUE;
    return profile;
}

guint director_show_apply_venue(DirectorShow *show, const DirectorVenueProfile *profile)
{
    if(!show || !profile)
        return 0;
    guint applied = 0;
    for(guint i = 0; i < profile->outputs->len; i++) {
        DirectorVenueOutput *output = g_ptr_array_index(profile->outputs, i);
        DirectorInstance *instance = director_show_find_instance(show, output->instance_id);
        if(!instance)
            continue;
        director_venue_output_apply(instance, output);
        applied++;
    }
    g_free(show->active_venue);
    show->active_venue = g_strdup(profile->name ? profile->name : "");
    show->dirty = TRUE;
    return applied;
}

gboolean director_show_remove_venue(DirectorShow *show, const gchar *name)
{
    DirectorVenueProfile *profile = director_show_find_venue(show, name);
    if(!profile)
        return FALSE;
    if(g_strcmp0(show->active_venue, name) == 0) {
        g_free(show->active_venue);
        show->active_venue = g_strdup("");
    }
    gboolean removed = g_ptr_array_remove(show->venue_profiles, profile);
    if(removed)
        show->dirty = TRUE;
    return removed;
}

DirectorShowSnapshot *director_show_find_snapshot(DirectorShow *show, const gchar *name)
{
    if(!show || !name)
        return NULL;
    for(guint i = 0; i < show->snapshots->len; i++) {
        DirectorShowSnapshot *snapshot = g_ptr_array_index(show->snapshots, i);
        if(g_strcmp0(snapshot->name, name) == 0)
            return snapshot;
    }
    return NULL;
}

DirectorShowSnapshot *director_show_capture_snapshot(DirectorShow *show, const gchar *name)
{
    if(!show || !name || !*name)
        return NULL;
    DirectorShowSnapshot *snapshot = director_show_find_snapshot(show, name);
    if(!snapshot) {
        snapshot = g_new0(DirectorShowSnapshot, 1);
        snapshot->name = g_strdup(name);
        snapshot->instances = g_ptr_array_new_with_free_func((GDestroyNotify)director_snapshot_instance_free);
        g_ptr_array_add(show->snapshots, snapshot);
    }
    else {
        g_ptr_array_set_size(snapshot->instances, 0);
    }
    for(guint i = 0; i < show->instances->len; i++) {
        DirectorInstance *instance = g_ptr_array_index(show->instances, i);
        if(instance->discovered_transient || instance->calibration_camera)
            continue;
        DirectorSnapshotInstance *entry = g_new0(DirectorSnapshotInstance, 1);
        entry->instance_id = g_strdup(instance->id);
        entry->should_run = instance->managed && (instance->process_running || instance->connected);
        entry->source_instance_id = g_strdup(instance->source_instance_id ? instance->source_instance_id : "");
        entry->source_host = g_strdup(instance->source_host ? instance->source_host : "");
        entry->source_port = instance->source_port;
        entry->input_routes = g_ptr_array_new_with_free_func((GDestroyNotify)director_input_route_free);
        for(guint route_i = 0; instance->input_routes && route_i < instance->input_routes->len; route_i++)
            g_ptr_array_add(entry->input_routes, director_input_route_copy(g_ptr_array_index(instance->input_routes, route_i)));
        entry->control_mode = instance->control_mode;
        entry->master_instance_id = g_strdup(instance->master_instance_id ? instance->master_instance_id : "");
        entry->master_host = g_strdup(instance->master_host ? instance->master_host : "");
        entry->master_port = instance->master_port;
        entry->preview_forward_vims = instance->preview_forward_vims;
        entry->preview_sync_samplelist = instance->preview_sync_samplelist;
        entry->preview_headless = instance->preview_headless;
        entry->pattern = instance->live_pattern >= 0 ? instance->live_pattern : instance->pattern;
        entry->audio_enabled = instance->audio_enabled;
        entry->audio_muted = instance->audio_muted;
        entry->ndi_input_enabled = instance->ndi_input_enabled;
        entry->ndi_source_name = g_strdup(instance->ndi_source_name ? instance->ndi_source_name : "");
        entry->ndi_output_enabled = instance->ndi_output_enabled;
        entry->ndi_output_name = g_strdup(instance->ndi_output_name ? instance->ndi_output_name : "VeeJay Program");
        entry->ndi_tally_enabled = instance->ndi_tally_enabled;
        entry->ndi_follow_clock = instance->ndi_follow_clock;
        director_venue_output_capture(&entry->physical, instance);
        g_free(entry->physical.instance_id);
        entry->physical.instance_id = NULL;
        g_ptr_array_add(snapshot->instances, entry);
    }
    show->dirty = TRUE;
    return snapshot;
}

guint director_show_apply_snapshot_model(DirectorShow *show, const DirectorShowSnapshot *snapshot)
{
    if(!show || !snapshot)
        return 0;
    guint applied = 0;
    for(guint i = 0; i < snapshot->instances->len; i++) {
        DirectorSnapshotInstance *entry = g_ptr_array_index(snapshot->instances, i);
        DirectorInstance *instance = director_show_find_instance(show, entry->instance_id);
        if(!instance)
            continue;
        g_free(instance->source_instance_id);
        instance->source_instance_id = g_strdup(entry->source_instance_id ? entry->source_instance_id : "");
        g_free(instance->source_host);
        instance->source_host = g_strdup(entry->source_host ? entry->source_host : "");
        instance->source_port = entry->source_port;
        director_instance_clear_input_routes(instance);
        for(guint route_i = 0; entry->input_routes && route_i < entry->input_routes->len; route_i++) {
            DirectorInputRoute *copy_route = director_input_route_copy(g_ptr_array_index(entry->input_routes, route_i));
            copy_route->applied_connection = FALSE;
            copy_route->shm_key = 0;
            g_ptr_array_add(instance->input_routes, copy_route);
        }
        instance->wire_applied_connection = FALSE;
        instance->control_mode = entry->control_mode;
        g_free(instance->master_instance_id);
        instance->master_instance_id = g_strdup(entry->master_instance_id ? entry->master_instance_id : "");
        g_free(instance->master_host);
        instance->master_host = g_strdup(entry->master_host ? entry->master_host : "");
        instance->master_port = entry->master_port;
        instance->preview_forward_vims = entry->preview_forward_vims;
        instance->preview_sync_samplelist = entry->preview_sync_samplelist;
        instance->preview_headless = entry->preview_headless;
        instance->pattern = CLAMP(entry->pattern, 0, 8);
        instance->audio_enabled = entry->audio_enabled;
        instance->audio_muted = entry->audio_muted;
        instance->ndi_input_enabled = entry->ndi_input_enabled;
        g_free(instance->ndi_source_name);
        instance->ndi_source_name = g_strdup(entry->ndi_source_name ? entry->ndi_source_name : "");
        instance->ndi_output_enabled = entry->ndi_output_enabled;
        g_free(instance->ndi_output_name);
        instance->ndi_output_name = g_strdup(entry->ndi_output_name ? entry->ndi_output_name : "VeeJay Program");
        instance->ndi_tally_enabled = entry->ndi_tally_enabled;
        instance->ndi_follow_clock = entry->ndi_follow_clock;
        DirectorVenueOutput physical = entry->physical;
        physical.instance_id = instance->id;
        director_venue_output_apply(instance, &physical);
        applied++;
    }
    show->dirty = TRUE;
    return applied;
}

gboolean director_show_remove_snapshot(DirectorShow *show, const gchar *name)
{
    DirectorShowSnapshot *snapshot = director_show_find_snapshot(show, name);
    if(!snapshot)
        return FALSE;
    gboolean removed = g_ptr_array_remove(show->snapshots, snapshot);
    if(removed)
        show->dirty = TRUE;
    return removed;
}

static gchar *key_file_get_string_default(GKeyFile *file, const gchar *group,
                                          const gchar *key, const gchar *fallback);
static gint key_file_get_integer_default(GKeyFile *file, const gchar *group,
                                         const gchar *key, gint fallback);
static gboolean key_file_get_boolean_default(GKeyFile *file, const gchar *group,
                                             const gchar *key, gboolean fallback);

static void key_file_set_projection_config(GKeyFile *file, const gchar *group,
                                           const DirectorProjectionConfig *config)
{
    g_key_file_set_boolean(file, group, "projection-valid", config->valid);
    g_key_file_set_boolean(file, group, "projection-enabled", config->enabled);
    g_key_file_set_boolean(file, group, "projection-startup-enabled", config->startup_enabled);
    g_key_file_set_integer(file, group, "projection-mode", config->mode);
    g_key_file_set_integer(file, group, "projection-columns", config->columns);
    g_key_file_set_integer(file, group, "projection-rows", config->rows);
    g_key_file_set_integer(file, group, "projection-selected", config->selected_point);
    g_key_file_set_integer(file, group, "projection-point-count", config->point_count);
    g_key_file_set_integer(file, group, "projection-output-width", config->output_width);
    g_key_file_set_integer(file, group, "projection-output-height", config->output_height);
    g_key_file_set_integer(file, group, "projection-source-x", config->source_x);
    g_key_file_set_integer(file, group, "projection-source-y", config->source_y);
    g_key_file_set_integer(file, group, "projection-source-width", config->source_width);
    g_key_file_set_integer(file, group, "projection-source-height", config->source_height);
    g_key_file_set_integer(file, group, "projection-scale", config->scale);
    if(config->valid && config->point_count > 0 && config->point_count <= DIRECTOR_MAX_PROJECTION_POINTS) {
        const gsize value_count = (gsize)config->point_count * 2u;
        gdouble points[DIRECTOR_MAX_PROJECTION_POINTS * 2];
        memcpy(points, config->points, value_count * sizeof(*points));
        g_key_file_set_double_list(file, group, "projection-points", points, value_count);
    }
}

static void key_file_get_projection_config(GKeyFile *file, const gchar *group,
                                           DirectorProjectionConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->valid = key_file_get_boolean_default(file, group, "projection-valid", FALSE);
    if(!config->valid)
        return;
    config->enabled = key_file_get_boolean_default(file, group, "projection-enabled", FALSE);
    config->startup_enabled = key_file_get_boolean_default(file, group, "projection-startup-enabled", FALSE);
    config->mode = clamp_int(key_file_get_integer_default(file, group, "projection-mode", 0), 0, 4);
    config->columns = clamp_int(key_file_get_integer_default(file, group, "projection-columns", 2), 2, 17);
    config->rows = clamp_int(key_file_get_integer_default(file, group, "projection-rows", 2), 2, 17);
    config->point_count = key_file_get_integer_default(file, group, "projection-point-count",
                                                        config->columns * config->rows);
    if(config->point_count != config->columns * config->rows || config->point_count <= 0 ||
       config->point_count > DIRECTOR_MAX_PROJECTION_POINTS) {
        memset(config, 0, sizeof(*config));
        return;
    }
    config->selected_point = clamp_int(key_file_get_integer_default(file, group,
                                                                     "projection-selected", 0),
                                       0, config->point_count);
    config->output_width = clamp_int(key_file_get_integer_default(file, group,
                                                                   "projection-output-width", 0), 0, 32768);
    config->output_height = clamp_int(key_file_get_integer_default(file, group,
                                                                    "projection-output-height", 0), 0, 32768);
    config->source_x = key_file_get_integer_default(file, group, "projection-source-x", 0);
    config->source_y = key_file_get_integer_default(file, group, "projection-source-y", 0);
    config->source_width = key_file_get_integer_default(file, group, "projection-source-width", 0);
    config->source_height = key_file_get_integer_default(file, group, "projection-source-height", 0);
    config->scale = MAX(1, key_file_get_integer_default(file, group, "projection-scale", 1000));
    gsize length = 0;
    GError *local_error = NULL;
    gdouble *points = g_key_file_get_double_list(file, group, "projection-points", &length, &local_error);
    if(local_error || !points || length != (gsize)config->point_count * 2u) {
        g_clear_error(&local_error);
        g_free(points);
        memset(config, 0, sizeof(*config));
        return;
    }
    memcpy(config->points, points, sizeof(gdouble) * length);
    g_free(points);
}

static void key_file_set_venue_output(GKeyFile *file, const gchar *group,
                                      const DirectorVenueOutput *output)
{
    g_key_file_set_string(file, group, "instance-id", output->instance_id ? output->instance_id : "");
    g_key_file_set_integer(file, group, "output-width", output->output_width);
    g_key_file_set_integer(file, group, "output-height", output->output_height);
    g_key_file_set_string(file, group, "display-id", output->display_id ? output->display_id : "");
    g_key_file_set_string(file, group, "display-name", output->display_name ? output->display_name : "");
    g_key_file_set_string(file, group, "display-connector", output->display_connector ? output->display_connector : "");
    g_key_file_set_integer(file, group, "display-index", output->display_index);
    g_key_file_set_integer(file, group, "display-x", output->display_x);
    g_key_file_set_integer(file, group, "display-y", output->display_y);
    g_key_file_set_integer(file, group, "display-width", output->display_width);
    g_key_file_set_integer(file, group, "display-height", output->display_height);
    g_key_file_set_boolean(file, group, "fullscreen", output->fullscreen);
    g_key_file_set_boolean(file, group, "borderless", output->borderless);
    g_key_file_set_integer(file, group, "window-width", output->window_width);
    g_key_file_set_integer(file, group, "window-height", output->window_height);
    g_key_file_set_integer(file, group, "window-x", output->window_x);
    g_key_file_set_integer(file, group, "window-y", output->window_y);
    g_key_file_set_string(file, group, "split-master-instance",
                          output->split_master_instance_id ? output->split_master_instance_id : "");
    g_key_file_set_integer(file, group, "split-row", output->split_row);
    g_key_file_set_integer(file, group, "split-column", output->split_column);
    g_key_file_set_integer(file, group, "graph-width", output->graph_width);
    g_key_file_set_integer(file, group, "graph-height", output->graph_height);
    key_file_set_projection_config(file, group, &output->projection);
    for(gint i = 0; i < DIRECTOR_MAX_SLICES; i++) {
        gchar *key = g_strdup_printf("slice-%d", i);
        gint values[14] = {
            output->slices[i].enabled ? 1 : 0,
            output->slices[i].source_x, output->slices[i].source_y,
            output->slices[i].source_width, output->slices[i].source_height,
            output->slices[i].dest_x, output->slices[i].dest_y,
            output->slices[i].dest_width, output->slices[i].dest_height,
            output->slices[i].blend_left, output->slices[i].blend_right,
            output->slices[i].blend_top, output->slices[i].blend_bottom,
            output->slices[i].blend_gamma
        };
        g_key_file_set_integer_list(file, group, key, values, G_N_ELEMENTS(values));
        g_free(key);
    }
}

static DirectorVenueOutput *key_file_get_venue_output(GKeyFile *file, const gchar *group)
{
    DirectorVenueOutput *output = g_new0(DirectorVenueOutput, 1);
    output->instance_id = key_file_get_string_default(file, group, "instance-id", "");
    output->output_width = clamp_int(key_file_get_integer_default(file, group, "output-width", 1280), 16, 32768);
    output->output_height = clamp_int(key_file_get_integer_default(file, group, "output-height", 720), 16, 32768);
    output->display_id = key_file_get_string_default(file, group, "display-id", "");
    output->display_name = key_file_get_string_default(file, group, "display-name", "");
    output->display_connector = key_file_get_string_default(file, group, "display-connector", "");
    output->display_index = clamp_int(key_file_get_integer_default(file, group, "display-index", -1), -1, 63);
    output->display_x = key_file_get_integer_default(file, group, "display-x", 0);
    output->display_y = key_file_get_integer_default(file, group, "display-y", 0);
    output->display_width = clamp_int(key_file_get_integer_default(file, group, "display-width", 0), 0, 32768);
    output->display_height = clamp_int(key_file_get_integer_default(file, group, "display-height", 0), 0, 32768);
    output->fullscreen = key_file_get_boolean_default(file, group, "fullscreen", FALSE);
    output->borderless = key_file_get_boolean_default(file, group, "borderless", FALSE);
    output->window_width = clamp_int(key_file_get_integer_default(file, group, "window-width", 0), 0, 32768);
    output->window_height = clamp_int(key_file_get_integer_default(file, group, "window-height", 0), 0, 32768);
    output->window_x = clamp_int(key_file_get_integer_default(file, group, "window-x", -1), -1, 32768);
    output->window_y = clamp_int(key_file_get_integer_default(file, group, "window-y", -1), -1, 32768);
    output->split_master_instance_id = key_file_get_string_default(file, group, "split-master-instance", "");
    output->split_row = clamp_int(key_file_get_integer_default(file, group, "split-row", -1), -1, 63);
    output->split_column = clamp_int(key_file_get_integer_default(file, group, "split-column", -1), -1, 63);
    output->graph_width = clamp_int(key_file_get_integer_default(file, group, "graph-width", output->output_width), 16, 32768);
    output->graph_height = clamp_int(key_file_get_integer_default(file, group, "graph-height", output->output_height), 16, 32768);
    key_file_get_projection_config(file, group, &output->projection);
    for(gint i = 0; i < DIRECTOR_MAX_SLICES; i++) {
        gchar *key = g_strdup_printf("slice-%d", i);
        gsize length = 0;
        GError *local_error = NULL;
        gint *values = g_key_file_get_integer_list(file, group, key, &length, &local_error);
        if(!local_error && values && length == 14) {
            DirectorSlice *slice = &output->slices[i];
            slice->enabled = values[0] != 0;
            slice->source_x = values[1]; slice->source_y = values[2];
            slice->source_width = values[3]; slice->source_height = values[4];
            slice->dest_x = values[5]; slice->dest_y = values[6];
            slice->dest_width = values[7]; slice->dest_height = values[8];
            slice->blend_left = values[9]; slice->blend_right = values[10];
            slice->blend_top = values[11]; slice->blend_bottom = values[12];
            slice->blend_gamma = values[13];
        }
        else {
            director_slice_set_identity(&output->slices[i], output->output_width, output->output_height);
            output->slices[i].enabled = i == 0;
        }
        g_clear_error(&local_error);
        g_free(values);
        g_free(key);
    }
    return output;
}

static void key_file_set_input_routes(GKeyFile *file,
                                      const gchar *group,
                                      const GPtrArray *routes)
{
    const gint count = routes ? (gint)routes->len : 0;
    g_key_file_set_integer(file, group, "input-route-count", count);
    for(gint i = 0; i < count; i++) {
        const DirectorInputRoute *route = g_ptr_array_index((GPtrArray*)routes, (guint)i);
        gchar *key = g_strdup_printf("input-route-%d-type", i);
        g_key_file_set_integer(file, group, key, route ? route->type : 0);
        g_free(key);
        key = g_strdup_printf("input-route-%d-source-instance", i);
        g_key_file_set_string(file, group, key, route && route->source_instance_id ? route->source_instance_id : "");
        g_free(key);
        key = g_strdup_printf("input-route-%d-host", i);
        g_key_file_set_string(file, group, key, route && route->host ? route->host : "");
        g_free(key);
        key = g_strdup_printf("input-route-%d-port", i);
        g_key_file_set_integer(file, group, key, route ? route->port : 0);
        g_free(key);
        key = g_strdup_printf("input-route-%d-ndi-source", i);
        g_key_file_set_string(file, group, key, route && route->ndi_source_name ? route->ndi_source_name : "");
        g_free(key);
    }
}

static GPtrArray *key_file_get_input_routes(GKeyFile *file,
                                             const gchar *group,
                                             gboolean *present)
{
    const gboolean has_routes = g_key_file_has_key(file, group, "input-route-count", NULL);
    if(present)
        *present = has_routes;
    GPtrArray *routes = g_ptr_array_new_with_free_func((GDestroyNotify)director_input_route_free);
    if(!has_routes)
        return routes;

    const gint count = clamp_int(key_file_get_integer_default(file, group, "input-route-count", 0), 0, 256);
    for(gint i = 0; i < count; i++) {
        gchar *key = g_strdup_printf("input-route-%d-type", i);
        const gint type = key_file_get_integer_default(file, group, key, 0);
        g_free(key);
        if(type < DIRECTOR_INPUT_ROUTE_SHM || type > DIRECTOR_INPUT_ROUTE_NDI)
            continue;
        key = g_strdup_printf("input-route-%d-source-instance", i);
        gchar *source_instance = key_file_get_string_default(file, group, key, "");
        g_free(key);
        key = g_strdup_printf("input-route-%d-host", i);
        gchar *host = key_file_get_string_default(file, group, key, "");
        g_free(key);
        key = g_strdup_printf("input-route-%d-port", i);
        const gint port = clamp_int(key_file_get_integer_default(file, group, key, 0), 0, 65530);
        g_free(key);
        key = g_strdup_printf("input-route-%d-ndi-source", i);
        gchar *ndi_source = key_file_get_string_default(file, group, key, "");
        g_free(key);
        DirectorInputRoute *route = director_input_route_new((DirectorInputRouteType)type,
            source_instance, host, port, ndi_source);
        g_ptr_array_add(routes, route);
        g_free(source_instance);
        g_free(host);
        g_free(ndi_source);
    }
    return routes;
}

gboolean director_show_save(DirectorShow *show, const gchar *path, GError **error)
{
    if(!show || !path || !*path) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "No show path was supplied");
        return FALSE;
    }

    GKeyFile *file = g_key_file_new();
    g_key_file_set_string(file, "Show", "format", "VEEJAY-DIRECTOR-2");
    g_key_file_set_string(file, "Show", "name", show->name ? show->name : "Untitled show");
    g_key_file_set_boolean(file, "Show", "launch-reloaded", show->launch_reloaded);
    g_key_file_set_string(file, "Show", "reloaded-executable",
                          show->reloaded_executable ? show->reloaded_executable : "reloaded");
    g_key_file_set_string(file, "Show", "reloaded-args",
                          show->reloaded_args ? show->reloaded_args : "-a");
    g_key_file_set_string(file, "Show", "active-venue",
                          show->active_venue ? show->active_venue : "");
    g_key_file_set_integer(file, "Show", "venue-profiles",
                           show->venue_profiles ? (gint)show->venue_profiles->len : 0);
    g_key_file_set_integer(file, "Show", "snapshots",
                           show->snapshots ? (gint)show->snapshots->len : 0);
    g_key_file_set_integer(file, "Show", "ndi-patch-positions",
                           show->ndi_patch_positions ?
                               (gint)show->ndi_patch_positions->len : 0);
    guint persistent_count = 0;
    for(guint i = 0; i < show->instances->len; i++) {
        DirectorInstance *instance = g_ptr_array_index(show->instances, i);
        if(!instance->discovered_transient)
            persistent_count++;
    }
    g_key_file_set_integer(file, "Show", "instances", (gint)persistent_count);

    for(guint i = 0; show->ndi_patch_positions &&
                    i < show->ndi_patch_positions->len; i++) {
        const DirectorNdiPatchPosition *position =
            g_ptr_array_index(show->ndi_patch_positions, i);
        if(!position || !position->key || !*position->key)
            continue;
        gchar *group = g_strdup_printf("NDI Patch Position %u", i);
        g_key_file_set_string(file, group, "key", position->key);
        g_key_file_set_integer(file, group, "x", position->x);
        g_key_file_set_integer(file, group, "y", position->y);
        g_free(group);
    }

    guint persistent_index = 0;
    for(guint i = 0; i < show->instances->len; i++) {
        DirectorInstance *instance = g_ptr_array_index(show->instances, i);
        if(instance->discovered_transient)
            continue;
        gchar *group = g_strdup_printf("Instance %u", persistent_index);
        g_key_file_set_string(file, group, "id", instance->id);
        g_key_file_set_string(file, group, "role", director_role_name(instance->role));
        g_key_file_set_string(file, group, "host", instance->host);
        g_key_file_set_integer(file, group, "port", instance->port);
        g_key_file_set_integer(file, group, "output-width", instance->output_width);
        g_key_file_set_integer(file, group, "output-height", instance->output_height);
        g_key_file_set_integer(file, group, "input-width", instance->input_width);
        g_key_file_set_integer(file, group, "input-height", instance->input_height);
        g_key_file_set_string(file, group, "startup-source",
                              director_startup_mode_name(instance->startup_mode));
        if(instance->media_files && instance->media_files->len > 0)
            g_key_file_set_string_list(file, group, "media-files",
                                       (const gchar * const*)instance->media_files->pdata,
                                       instance->media_files->len);
        g_key_file_set_double(file, group, "fps", instance->fps);
        g_key_file_set_integer(file, group, "norm", instance->norm);
        g_key_file_set_integer(file, group, "output-driver", instance->output_driver);
        g_key_file_set_string(file, group, "output-file",
                              instance->output_file ? instance->output_file : "");
        g_key_file_set_integer(file, group, "yuv-mode", instance->yuv_mode);
        g_key_file_set_boolean(file, group, "sync-correction", instance->sync_correction);
        g_key_file_set_boolean(file, group, "audio-enabled", instance->audio_enabled);
        g_key_file_set_boolean(file, group, "audio-muted", instance->audio_muted);
        g_key_file_set_boolean(file, group, "audio-sync-thread", instance->audio_sync_thread);
        g_key_file_set_boolean(file, group, "audio-beat-thread", instance->audio_beat_thread);
        g_key_file_set_boolean(file, group, "auto-loop", instance->auto_loop);
        g_key_file_set_boolean(file, group, "clip-as-sample", instance->clip_as_sample);
        g_key_file_set_boolean(file, group, "deinterlace", instance->deinterlace);
        g_key_file_set_boolean(file, group, "legacy-viewport", instance->legacy_viewport);
        g_key_file_set_boolean(file, group, "borderless", instance->borderless);
        g_key_file_set_boolean(file, group, "fullscreen", instance->fullscreen);
        g_key_file_set_boolean(file, group, "no-keyboard", instance->no_keyboard);
        g_key_file_set_boolean(file, group, "no-mouse", instance->no_mouse);
        g_key_file_set_boolean(file, group, "show-cursor", instance->show_cursor);
        g_key_file_set_boolean(file, group, "verbose", instance->verbose);
        g_key_file_set_boolean(file, group, "no-color", instance->no_color);
        g_key_file_set_integer(file, group, "window-width", instance->window_width);
        g_key_file_set_integer(file, group, "window-height", instance->window_height);
        g_key_file_set_integer(file, group, "window-x", instance->window_x);
        g_key_file_set_integer(file, group, "window-y", instance->window_y);
        g_key_file_set_integer(file, group, "memory-percent", instance->memory_percent);
        g_key_file_set_integer(file, group, "max-cache", instance->max_cache);
        g_key_file_set_integer(file, group, "timer-mode", instance->timer_mode);
        g_key_file_set_integer(file, group, "pace-correction-ms", instance->pace_correction_ms);
        g_key_file_set_integer(file, group, "audio-rate", instance->audio_rate);
        g_key_file_set_integer(file, group, "audio-channels", instance->audio_channels);
        g_key_file_set_integer(file, group, "audio-bits", instance->audio_bits);
        g_key_file_set_boolean(file, group, "ndi-input-enabled", instance->ndi_input_enabled);
        g_key_file_set_string(file, group, "ndi-source-name", instance->ndi_source_name ? instance->ndi_source_name : "");
        g_key_file_set_boolean(file, group, "ndi-output-enabled", instance->ndi_output_enabled);
        g_key_file_set_string(file, group, "ndi-output-name", instance->ndi_output_name ? instance->ndi_output_name : "VeeJay Program");
        g_key_file_set_boolean(file, group, "ndi-tally-enabled", instance->ndi_tally_enabled);
        g_key_file_set_boolean(file, group, "ndi-follow-clock", instance->ndi_follow_clock);
        g_key_file_set_integer(file, group, "scene-detection", instance->scene_detection);
        g_key_file_set_integer(file, group, "capture-device", instance->calibration_camera ? -1 : instance->capture_device);
        g_key_file_set_integer(file, group, "generator-stream", instance->generator_stream);
        g_key_file_set_boolean(file, group, "swap-range", instance->swap_range);
        g_key_file_set_boolean(file, group, "dynamic-fx-chain", instance->dynamic_fx_chain);
        g_key_file_set_boolean(file, group, "fx-custom-defaults", instance->fx_custom_defaults);
        g_key_file_set_boolean(file, group, "preserve-pathnames", instance->preserve_pathnames);
        g_key_file_set_boolean(file, group, "bezerk", instance->bezerk);
        g_key_file_set_string(file, group, "split-screen-file", instance->split_screen_file ? instance->split_screen_file : "");
        g_key_file_set_string(file, group, "split-master-instance",
                              instance->split_master_instance_id ? instance->split_master_instance_id : "");
        g_key_file_set_integer(file, group, "split-row", instance->split_row);
        g_key_file_set_integer(file, group, "split-column", instance->split_column);
        g_key_file_set_string(file, group, "multicast-osc", instance->multicast_osc ? instance->multicast_osc : "");
        g_key_file_set_string(file, group, "multicast-vims", instance->multicast_vims ? instance->multicast_vims : "");
        g_key_file_set_string(file, group, "sample-file", instance->sample_file ? instance->sample_file : "");
        g_key_file_set_string(file, group, "action-file", instance->action_file ? instance->action_file : "");
        g_key_file_set_string(file, group, "working-directory",
                              instance->working_directory ? instance->working_directory : "");
        g_key_file_set_string(file, group, "source-instance",
                              instance->source_instance_id ? instance->source_instance_id : "");
        g_key_file_set_string(file, group, "source-host",
                              instance->source_host ? instance->source_host : "");
        g_key_file_set_integer(file, group, "source-port", instance->source_port);
        key_file_set_input_routes(file, group, instance->input_routes);
        g_key_file_set_string(file, group, "stream-advertise-host",
                              instance->stream_advertise_host ? instance->stream_advertise_host : "");
        g_key_file_set_string(file, group, "control-mode",
                              director_control_mode_name(instance->control_mode));
        g_key_file_set_string(file, group, "master-instance",
                              instance->master_instance_id ? instance->master_instance_id : "");
        g_key_file_set_string(file, group, "master-host",
                              instance->master_host ? instance->master_host : "127.0.0.1");
        g_key_file_set_integer(file, group, "master-port", instance->master_port);
        g_key_file_set_boolean(file, group, "preview-forward-vims",
                               instance->preview_forward_vims);
        g_key_file_set_boolean(file, group, "preview-sync-samplelist",
                               instance->preview_sync_samplelist);
        g_key_file_set_boolean(file, group, "preview-headless",
                               instance->preview_headless);
        g_key_file_set_string(file, group, "display-id",
                              instance->display_id ? instance->display_id : "");
        g_key_file_set_string(file, group, "display-name",
                              instance->display_name ? instance->display_name : "");
        g_key_file_set_string(file, group, "display-connector",
                              instance->display_connector ? instance->display_connector : "");
        g_key_file_set_integer(file, group, "display-index", instance->display_index);
        g_key_file_set_integer(file, group, "display-x", instance->display_x);
        g_key_file_set_integer(file, group, "display-y", instance->display_y);
        g_key_file_set_integer(file, group, "display-width", instance->display_width);
        g_key_file_set_integer(file, group, "display-height", instance->display_height);
        g_key_file_set_integer(file, group, "stage-x", instance->stage_x);
        g_key_file_set_integer(file, group, "stage-y", instance->stage_y);
        g_key_file_set_boolean(file, group, "stage-position-explicit", instance->stage_position_explicit);
        g_key_file_set_integer(file, group, "wiring-x", instance->wiring_x);
        g_key_file_set_integer(file, group, "wiring-y", instance->wiring_y);
        g_key_file_set_boolean(file, group, "wiring-position-explicit", instance->wiring_position_explicit);
        g_key_file_set_string(file, group, "executable",
                              instance->executable ? instance->executable : "veejay");
        g_key_file_set_string(file, group, "extra-args",
                              instance->extra_args ? instance->extra_args : "");
        g_key_file_set_boolean(file, group, "eidolon-enabled", instance->eidolon_enabled);
        g_key_file_set_string(file, group, "eidolon-executable",
                              instance->eidolon_executable ? instance->eidolon_executable : "eidolon");
        g_key_file_set_string(file, group, "eidolon-extra-args",
                              instance->eidolon_extra_args ? instance->eidolon_extra_args : "");
        g_key_file_set_boolean(file, group, "managed", instance->managed);
        g_key_file_set_boolean(file, group, "autostart", instance->autostart);
        g_key_file_set_boolean(file, group, "apply-on-connect", instance->apply_on_connect);
        g_key_file_set_boolean(file, group, "calibration-camera", instance->calibration_camera);
        g_key_file_set_boolean(file, group, "recovery-restart-engine", instance->recovery_restart_engine);
        g_key_file_set_boolean(file, group, "recovery-reconnect-route", instance->recovery_reconnect_route);
        g_key_file_set_boolean(file, group, "recovery-restore-projection", instance->recovery_restore_projection);
        g_key_file_set_boolean(file, group, "recovery-restore-mapping", instance->recovery_restore_mapping);
        g_key_file_set_boolean(file, group, "recovery-restore-control", instance->recovery_restore_control);
        g_key_file_set_integer(file, group, "recovery-retry-limit", instance->recovery_retry_limit);
        g_key_file_set_integer(file, group, "pattern", instance->pattern);
        key_file_set_projection_config(file, group, &instance->configured_projection);
        g_free(group);

        for(gint s = 0; s < DIRECTOR_MAX_SLICES; s++) {
            group = slice_group_name(persistent_index, s);
            key_file_set_slice(file, group, &instance->slices[s]);
            g_free(group);
        }
        persistent_index++;
    }

    for(guint v = 0; show->venue_profiles && v < show->venue_profiles->len; v++) {
        DirectorVenueProfile *profile = g_ptr_array_index(show->venue_profiles, v);
        gchar *group = g_strdup_printf("Venue %u", v);
        g_key_file_set_string(file, group, "name", profile->name ? profile->name : "");
        g_key_file_set_integer(file, group, "outputs", profile->outputs ? (gint)profile->outputs->len : 0);
        g_key_file_set_string(file, group, "calibration-camera-id",
                              profile->calibration_camera_id ? profile->calibration_camera_id : "");
        g_key_file_set_string(file, group, "calibration-camera-path",
                              profile->calibration_camera_path ? profile->calibration_camera_path : "");
        g_key_file_set_string(file, group, "calibration-camera-name",
                              profile->calibration_camera_name ? profile->calibration_camera_name : "");
        g_key_file_set_boolean(file, group, "calibration-controls-valid",
                               profile->calibration_controls_valid);
        if(profile->calibration_controls_valid)
            g_key_file_set_integer_list(file, group, "calibration-controls",
                                        profile->calibration_controls,
                                        DIRECTOR_V4L_CONTROL_COUNT);
        g_key_file_set_int64(file, group, "calibration-updated-us",
                             profile->calibration_updated_real_us);
        g_key_file_set_integer(file, group, "camera-maps",
                               profile->camera_maps ? (gint)profile->camera_maps->len : 0);
        g_free(group);
        for(guint o = 0; profile->outputs && o < profile->outputs->len; o++) {
            group = g_strdup_printf("Venue %u Output %u", v, o);
            key_file_set_venue_output(file, group, g_ptr_array_index(profile->outputs, o));
            g_free(group);
        }
    }

    for(guint v = 0; show->venue_profiles && v < show->venue_profiles->len; v++) {
        DirectorVenueProfile *profile = g_ptr_array_index(show->venue_profiles, v);
        for(guint m = 0; profile->camera_maps && m < profile->camera_maps->len; m++) {
            DirectorProjectorCameraMap *map = g_ptr_array_index(profile->camera_maps, m);
            gsize raw_len = 0;
            const guchar *raw = map->camera_to_projector ? g_bytes_get_data(map->camera_to_projector, &raw_len) : NULL;
            if(!raw || raw_len != (gsize)map->camera_width * (gsize)map->camera_height * sizeof(guint32))
                continue;
            gchar *encoded = g_base64_encode(raw, raw_len);
            gchar *group = g_strdup_printf("Venue %u CameraMap %u", v, m);
            g_key_file_set_string(file, group, "instance-id", map->instance_id ? map->instance_id : "");
            g_key_file_set_integer(file, group, "camera-width", map->camera_width);
            g_key_file_set_integer(file, group, "camera-height", map->camera_height);
            g_key_file_set_integer(file, group, "projector-width", map->projector_width);
            g_key_file_set_integer(file, group, "projector-height", map->projector_height);
            g_key_file_set_integer(file, group, "illuminated-pixels",
                                   (gint)MIN(map->illuminated_pixels, (guint)G_MAXINT));
            g_key_file_set_integer(file, group, "valid-pixels", (gint)MIN(map->valid_pixels, (guint)G_MAXINT));
            g_key_file_set_double(file, group, "mean-confidence", map->mean_confidence);
            g_key_file_set_int64(file, group, "updated-us", map->updated_real_us);
            g_key_file_set_string(file, group, "format", "u32le-camera-to-projector-v1");
            g_key_file_set_string(file, group, "data-base64", encoded);
            g_free(encoded);
            g_free(group);
        }
    }

    for(guint n = 0; show->snapshots && n < show->snapshots->len; n++) {
        DirectorShowSnapshot *snapshot = g_ptr_array_index(show->snapshots, n);
        gchar *group = g_strdup_printf("Snapshot %u", n);
        g_key_file_set_string(file, group, "name", snapshot->name ? snapshot->name : "");
        g_key_file_set_integer(file, group, "instances", snapshot->instances ? (gint)snapshot->instances->len : 0);
        g_free(group);
        for(guint e = 0; snapshot->instances && e < snapshot->instances->len; e++) {
            DirectorSnapshotInstance *entry = g_ptr_array_index(snapshot->instances, e);
            group = g_strdup_printf("Snapshot %u Instance %u", n, e);
            g_key_file_set_string(file, group, "instance-id", entry->instance_id ? entry->instance_id : "");
            g_key_file_set_boolean(file, group, "should-run", entry->should_run);
            g_key_file_set_string(file, group, "source-instance", entry->source_instance_id ? entry->source_instance_id : "");
            g_key_file_set_string(file, group, "source-host", entry->source_host ? entry->source_host : "");
            g_key_file_set_integer(file, group, "source-port", entry->source_port);
            key_file_set_input_routes(file, group, entry->input_routes);
            g_key_file_set_string(file, group, "control-mode", director_control_mode_name(entry->control_mode));
            g_key_file_set_string(file, group, "master-instance", entry->master_instance_id ? entry->master_instance_id : "");
            g_key_file_set_string(file, group, "master-host", entry->master_host ? entry->master_host : "");
            g_key_file_set_integer(file, group, "master-port", entry->master_port);
            g_key_file_set_boolean(file, group, "preview-forward-vims", entry->preview_forward_vims);
            g_key_file_set_boolean(file, group, "preview-sync-samplelist", entry->preview_sync_samplelist);
            g_key_file_set_boolean(file, group, "preview-headless", entry->preview_headless);
            g_key_file_set_integer(file, group, "pattern", entry->pattern);
            g_key_file_set_boolean(file, group, "audio-enabled", entry->audio_enabled);
            g_key_file_set_boolean(file, group, "audio-muted", entry->audio_muted);
            g_key_file_set_boolean(file, group, "ndi-input-enabled", entry->ndi_input_enabled);
            g_key_file_set_string(file, group, "ndi-source-name", entry->ndi_source_name ? entry->ndi_source_name : "");
            g_key_file_set_boolean(file, group, "ndi-output-enabled", entry->ndi_output_enabled);
            g_key_file_set_string(file, group, "ndi-output-name", entry->ndi_output_name ? entry->ndi_output_name : "VeeJay Program");
            g_key_file_set_boolean(file, group, "ndi-tally-enabled", entry->ndi_tally_enabled);
            g_key_file_set_boolean(file, group, "ndi-follow-clock", entry->ndi_follow_clock);
            DirectorVenueOutput physical = entry->physical;
            physical.instance_id = entry->instance_id;
            key_file_set_venue_output(file, group, &physical);
            g_free(group);
        }
    }

    gsize length = 0;
    gchar *data = g_key_file_to_data(file, &length, error);
    g_key_file_free(file);
    if(!data)
        return FALSE;

    gchar *directory = g_path_get_dirname(path);
    if(g_mkdir_with_parents(directory, 0700) != 0 && errno != EEXIST) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create '%s': %s", directory, g_strerror(errno));
        g_free(directory);
        g_free(data);
        return FALSE;
    }
    g_free(directory);

    gchar *temporary = g_strdup_printf("%s.tmp.%08x", path, g_random_int());
    gboolean ok = g_file_set_contents(temporary, data, (gssize)length, error);
    if(ok && g_rename(temporary, path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot replace '%s': %s", path, g_strerror(errno));
        ok = FALSE;
    }
    if(!ok)
        g_unlink(temporary);
    g_free(temporary);
    g_free(data);
    if(ok) {
        g_free(show->path);
        show->path = g_strdup(path);
        show->dirty = FALSE;
    }
    return ok;
}

static gchar *key_file_get_string_default(GKeyFile *file, const gchar *group,
                                          const gchar *key, const gchar *fallback)
{
    GError *error = NULL;
    gchar *value = g_key_file_get_string(file, group, key, &error);
    if(error) {
        g_error_free(error);
        return g_strdup(fallback);
    }
    return value;
}

static gint key_file_get_integer_default(GKeyFile *file, const gchar *group,
                                         const gchar *key, gint fallback)
{
    GError *error = NULL;
    gint value = g_key_file_get_integer(file, group, key, &error);
    if(error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

static gdouble key_file_get_double_default(GKeyFile *file, const gchar *group,
                                           const gchar *key, gdouble fallback)
{
    GError *error = NULL;
    gdouble value = g_key_file_get_double(file, group, key, &error);
    if(error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

static gboolean key_file_get_boolean_default(GKeyFile *file, const gchar *group,
                                             const gchar *key, gboolean fallback)
{
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(file, group, key, &error);
    if(error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

DirectorShow *director_show_load(const gchar *path, GError **error)
{
    GKeyFile *file = g_key_file_new();
    if(!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, error)) {
        g_key_file_free(file);
        return NULL;
    }
    gchar *format = g_key_file_get_string(file, "Show", "format", error);
    if(!format) {
        g_key_file_free(file);
        return NULL;
    }
    if(g_strcmp0(format, "VEEJAY-DIRECTOR-2") != 0) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Unsupported Director show format '%s'", format);
        g_free(format);
        g_key_file_free(file);
        return NULL;
    }
    g_free(format);

    gchar *name = key_file_get_string_default(file, "Show", "name", "Untitled show");
    DirectorShow *show = director_show_new(name);
    g_free(name);
    show->launch_reloaded = key_file_get_boolean_default(file, "Show",
                                                          "launch-reloaded", FALSE);
    g_free(show->reloaded_executable);
    show->reloaded_executable = key_file_get_string_default(file, "Show",
                                                             "reloaded-executable", "reloaded");
    g_free(show->reloaded_args);
    show->reloaded_args = key_file_get_string_default(file, "Show",
                                                       "reloaded-args", "-a");
    g_free(show->active_venue);
    show->active_venue = key_file_get_string_default(file, "Show", "active-venue", "");
    gint venue_count = clamp_int(key_file_get_integer_default(file, "Show", "venue-profiles", 0), 0, 64);
    gint snapshot_count = clamp_int(key_file_get_integer_default(file, "Show", "snapshots", 0), 0, 64);
    gint ndi_position_count = clamp_int(
        key_file_get_integer_default(file, "Show", "ndi-patch-positions", 0),
        0, 512);
    gint count = key_file_get_integer_default(file, "Show", "instances", 0);
    count = clamp_int(count, 0, 128);

    for(gint i = 0; i < ndi_position_count; i++) {
        gchar *group = g_strdup_printf("NDI Patch Position %d", i);
        gchar *key = key_file_get_string_default(file, group, "key", "");
        gint x = clamp_int(key_file_get_integer_default(file, group, "x", 32),
                           0, 1000000);
        gint y = clamp_int(key_file_get_integer_default(file, group, "y", 82),
                           0, 1000000);
        if(*key) {
            director_show_set_ndi_patch_position(show, key, x, y);
            show->dirty = FALSE;
        }
        g_free(key);
        g_free(group);
    }

    for(gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Instance %d", i);
        gchar *id = key_file_get_string_default(file, group, "id", "veejay");
        gchar *role_name = key_file_get_string_default(file, group, "role", "standalone");
        if(g_strcmp0(role_name, "standalone") != 0 &&
           g_strcmp0(role_name, "program") != 0 &&
           g_strcmp0(role_name, "output") != 0) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                        "Invalid role '%s' in %s", role_name, group);
            g_free(id);
            g_free(role_name);
            g_free(group);
            director_show_free(show);
            g_key_file_free(file);
            return NULL;
        }
        if(!director_instance_id_valid(id)) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                        "Invalid instance ID '%s' in %s", id, group);
            g_free(id);
            g_free(role_name);
            g_free(group);
            director_show_free(show);
            g_key_file_free(file);
            return NULL;
        }
        if(director_show_find_instance(show, id)) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                        "Duplicate instance ID '%s' in show", id);
            g_free(id);
            g_free(role_name);
            g_free(group);
            director_show_free(show);
            g_key_file_free(file);
            return NULL;
        }
        DirectorInstance *instance = director_instance_new(id,
                                                          director_role_from_string(role_name));
        g_free(id);
        g_free(role_name);

        g_free(instance->host);
        instance->host = key_file_get_string_default(file, group, "host", "127.0.0.1");
        instance->port = clamp_int(key_file_get_integer_default(file, group, "port", 3490), 1, 65530);
        instance->output_width = clamp_int(key_file_get_integer_default(file, group, "output-width", 1280), 16, 32768);
        instance->output_height = clamp_int(key_file_get_integer_default(file, group, "output-height", 720), 16, 32768);
        instance->input_width = clamp_int(key_file_get_integer_default(file, group, "input-width", instance->output_width), 16, 32768);
        instance->input_height = clamp_int(key_file_get_integer_default(file, group, "input-height", instance->output_height), 16, 32768);
        gchar *startup_source = key_file_get_string_default(file, group, "startup-source", "blank");
        instance->startup_mode = director_startup_mode_from_string(startup_source);
        g_free(startup_source);
        gsize media_count = 0;
        GError *media_error = NULL;
        gchar **media_files = g_key_file_get_string_list(file, group, "media-files",
                                                         &media_count, &media_error);
        if(media_error)
            g_error_free(media_error);
        else if(media_files) {
            for(gsize media_index = 0; media_index < media_count; media_index++) {
                if(media_files[media_index] && *media_files[media_index])
                    g_ptr_array_add(instance->media_files, g_strdup(media_files[media_index]));
            }
            g_strfreev(media_files);
        }
        instance->fps = key_file_get_double_default(file, group, "fps", 25.0);
        if(instance->fps < 0.0 ||
           (instance->fps > 0.0 && instance->fps < 1.0) ||
           instance->fps > 240.0)
            instance->fps = 25.0;
        instance->norm = clamp_int(key_file_get_integer_default(file, group, "norm", -1), -1, 2);
        instance->output_driver = key_file_get_integer_default(file, group, "output-driver", 0);
        if(instance->output_driver != 0 && instance->output_driver != 3 &&
           instance->output_driver != 4 && instance->output_driver != 6)
            instance->output_driver = 0;
        g_free(instance->output_file);
        instance->output_file = key_file_get_string_default(file, group, "output-file", "");
        instance->yuv_mode = clamp_int(key_file_get_integer_default(file, group, "yuv-mode", 0), 0, 2);
        instance->sync_correction = key_file_get_boolean_default(file, group, "sync-correction", TRUE);
        instance->audio_enabled = key_file_get_boolean_default(file, group, "audio-enabled",
                                                                instance->role != DIRECTOR_ROLE_OUTPUT);
        instance->audio_muted = key_file_get_boolean_default(file, group, "audio-muted", FALSE);
        instance->audio_sync_thread = key_file_get_boolean_default(file, group, "audio-sync-thread",
                                                                    instance->role != DIRECTOR_ROLE_OUTPUT);
        instance->audio_beat_thread = key_file_get_boolean_default(file, group, "audio-beat-thread",
                                                                    instance->role != DIRECTOR_ROLE_OUTPUT);
        instance->auto_loop = key_file_get_boolean_default(file, group, "auto-loop", FALSE);
        instance->clip_as_sample = key_file_get_boolean_default(file, group, "clip-as-sample", TRUE);
        instance->deinterlace = key_file_get_boolean_default(file, group, "deinterlace", FALSE);
        instance->legacy_viewport = key_file_get_boolean_default(file, group, "legacy-viewport", TRUE);
        instance->borderless = key_file_get_boolean_default(file, group, "borderless", FALSE);
        instance->fullscreen = key_file_get_boolean_default(file, group, "fullscreen", FALSE);
        instance->no_keyboard = key_file_get_boolean_default(file, group, "no-keyboard",
                                                              instance->role == DIRECTOR_ROLE_OUTPUT);
        instance->no_mouse = key_file_get_boolean_default(file, group, "no-mouse",
                                                           instance->role == DIRECTOR_ROLE_OUTPUT);
        instance->show_cursor = key_file_get_boolean_default(file, group, "show-cursor", FALSE);
        instance->verbose = key_file_get_boolean_default(file, group, "verbose", FALSE);
        instance->no_color = key_file_get_boolean_default(file, group, "no-color", FALSE);
        instance->window_width = clamp_int(key_file_get_integer_default(file, group, "window-width", 0), 0, 32768);
        instance->window_height = clamp_int(key_file_get_integer_default(file, group, "window-height", 0), 0, 32768);
        instance->window_x = clamp_int(key_file_get_integer_default(file, group, "window-x", -1), -1, 32768);
        instance->window_y = clamp_int(key_file_get_integer_default(file, group, "window-y", -1), -1, 32768);
        instance->memory_percent = clamp_int(key_file_get_integer_default(file, group, "memory-percent", -1), -1, 100);
        instance->max_cache = clamp_int(key_file_get_integer_default(file, group, "max-cache", 0), 0, 65535);
        instance->timer_mode = clamp_int(key_file_get_integer_default(file, group, "timer-mode", 1), 0, 1);
        instance->pace_correction_ms = clamp_int(key_file_get_integer_default(file, group, "pace-correction-ms", 0), 0, 60000);
        instance->audio_rate = clamp_int(key_file_get_integer_default(file, group, "audio-rate", 48000), 8000, 384000);
        instance->audio_channels = clamp_int(key_file_get_integer_default(file, group, "audio-channels", 2), 1, 32);
        instance->audio_bits = clamp_int(key_file_get_integer_default(file, group, "audio-bits", 16), 8, 64);
        instance->ndi_input_enabled = key_file_get_boolean_default(file, group, "ndi-input-enabled", FALSE);
        g_free(instance->ndi_source_name);
        instance->ndi_source_name = key_file_get_string_default(file, group, "ndi-source-name", "");
        instance->ndi_output_enabled = key_file_get_boolean_default(file, group, "ndi-output-enabled", FALSE);
        g_free(instance->ndi_output_name);
        instance->ndi_output_name = key_file_get_string_default(file, group, "ndi-output-name", "VeeJay Program");
        instance->ndi_tally_enabled = key_file_get_boolean_default(file, group, "ndi-tally-enabled", TRUE);
        instance->ndi_follow_clock = key_file_get_boolean_default(file, group, "ndi-follow-clock", FALSE);
        instance->scene_detection = clamp_int(key_file_get_integer_default(file, group, "scene-detection", -1), -1, 1000000);
        instance->capture_device = clamp_int(key_file_get_integer_default(file, group, "capture-device", -1), -1, 255);
        instance->generator_stream = clamp_int(key_file_get_integer_default(file, group, "generator-stream", -1), -1, 255);
        instance->swap_range = key_file_get_boolean_default(file, group, "swap-range", FALSE);
        instance->dynamic_fx_chain = key_file_get_boolean_default(file, group, "dynamic-fx-chain", FALSE);
        instance->fx_custom_defaults = key_file_get_boolean_default(file, group, "fx-custom-defaults", FALSE);
        instance->preserve_pathnames = key_file_get_boolean_default(file, group, "preserve-pathnames", FALSE);
        instance->bezerk = key_file_get_boolean_default(file, group, "bezerk", FALSE);
        g_free(instance->split_screen_file);
        instance->split_screen_file = key_file_get_string_default(file, group, "split-screen-file", "");
        g_free(instance->split_master_instance_id);
        instance->split_master_instance_id = key_file_get_string_default(file, group,
                                                                          "split-master-instance", "");
        instance->split_row = clamp_int(key_file_get_integer_default(file, group,
                                                                      "split-row", -1), -1, 63);
        instance->split_column = clamp_int(key_file_get_integer_default(file, group,
                                                                         "split-column", -1), -1, 63);
        g_free(instance->multicast_osc);
        instance->multicast_osc = key_file_get_string_default(file, group, "multicast-osc", "");
        g_free(instance->multicast_vims);
        instance->multicast_vims = key_file_get_string_default(file, group, "multicast-vims", "");
        g_free(instance->sample_file);
        instance->sample_file = key_file_get_string_default(file, group, "sample-file", "");
        g_free(instance->action_file);
        instance->action_file = key_file_get_string_default(file, group, "action-file", "");
        g_free(instance->working_directory);
        instance->working_directory = key_file_get_string_default(file, group, "working-directory", "");
        g_free(instance->source_instance_id);
        instance->source_instance_id = key_file_get_string_default(file, group, "source-instance", "");
        g_free(instance->source_host);
        instance->source_host = key_file_get_string_default(file, group, "source-host", "");
        instance->source_port = clamp_int(key_file_get_integer_default(file, group, "source-port", 3490), 1, 65530);
        gboolean routes_present = FALSE;
        GPtrArray *loaded_routes = key_file_get_input_routes(file, group, &routes_present);
        if(routes_present) {
            g_ptr_array_free(instance->input_routes, TRUE);
            instance->input_routes = loaded_routes;
        } else {
            g_ptr_array_free(loaded_routes, TRUE);
            director_instance_clear_input_routes(instance);
            if(instance->source_instance_id && *instance->source_instance_id) {
                const DirectorInputRouteType type =
                    (instance->source_host &&
                     (g_strcmp0(instance->source_host, "127.0.0.1") == 0 ||
                      g_ascii_strcasecmp(instance->source_host, "localhost") == 0)) ?
                    DIRECTOR_INPUT_ROUTE_SHM : DIRECTOR_INPUT_ROUTE_TCP;
                director_instance_add_input_route(instance, type,
                    instance->source_instance_id, instance->source_host,
                    instance->source_port, NULL);
            }
            if(instance->ndi_input_enabled && instance->ndi_source_name && *instance->ndi_source_name)
                director_instance_add_input_route(instance, DIRECTOR_INPUT_ROUTE_NDI,
                    NULL, NULL, 0, instance->ndi_source_name);
        }
        g_free(instance->stream_advertise_host);
        instance->stream_advertise_host = key_file_get_string_default(file, group,
                                                                       "stream-advertise-host", "");
        gchar *control_mode = key_file_get_string_default(file, group, "control-mode", "independent");
        instance->control_mode = director_control_mode_from_string(control_mode);
        g_free(control_mode);
        g_free(instance->master_instance_id);
        instance->master_instance_id = key_file_get_string_default(file, group, "master-instance", "");
        g_free(instance->master_host);
        instance->master_host = key_file_get_string_default(file, group, "master-host", "127.0.0.1");
        instance->master_port = clamp_int(key_file_get_integer_default(file, group, "master-port", 3490), 1, 65530);
        instance->preview_forward_vims = key_file_get_boolean_default(file, group,
                                                                       "preview-forward-vims", FALSE);
        instance->preview_sync_samplelist = key_file_get_boolean_default(file, group,
                                                                          "preview-sync-samplelist", FALSE);
        instance->preview_headless = key_file_get_boolean_default(file, group,
                                                                   "preview-headless", FALSE);
        g_free(instance->display_id);
        instance->display_id = key_file_get_string_default(file, group, "display-id", "");
        g_free(instance->display_name);
        instance->display_name = key_file_get_string_default(file, group, "display-name", "");
        g_free(instance->display_connector);
        instance->display_connector = key_file_get_string_default(file, group, "display-connector", "");
        instance->display_index = clamp_int(key_file_get_integer_default(file, group,
                                                                          "display-index", -1), -1, 63);
        instance->display_x = clamp_int(key_file_get_integer_default(file, group,
                                                                      "display-x", 0), -32768, 32768);
        instance->display_y = clamp_int(key_file_get_integer_default(file, group,
                                                                      "display-y", 0), -32768, 32768);
        instance->display_width = clamp_int(key_file_get_integer_default(file, group,
                                                                          "display-width", 0), 0, 32768);
        instance->display_height = clamp_int(key_file_get_integer_default(file, group,
                                                                           "display-height", 0), 0, 32768);
        instance->stage_x = key_file_get_integer_default(file, group,
                                                         "stage-x", instance->display_x);
        instance->stage_y = key_file_get_integer_default(file, group,
                                                         "stage-y", instance->display_y);
        instance->stage_position_explicit = key_file_get_boolean_default(
            file, group, "stage-position-explicit", FALSE);
        instance->wiring_x = clamp_int(key_file_get_integer_default(file, group,
                                                                     "wiring-x", 0), -32768, 32768);
        instance->wiring_y = clamp_int(key_file_get_integer_default(file, group,
                                                                     "wiring-y", 0), -32768, 32768);
        instance->wiring_position_explicit = key_file_get_boolean_default(
            file, group, "wiring-position-explicit", FALSE);
        if(instance->role == DIRECTOR_ROLE_OUTPUT) {
            instance->control_mode = DIRECTOR_CONTROL_INDEPENDENT;
            instance->preview_headless = FALSE;
            instance->preview_forward_vims = FALSE;
            instance->preview_sync_samplelist = FALSE;
        }
        g_free(instance->executable);
        instance->executable = key_file_get_string_default(file, group, "executable", "veejay");
        g_free(instance->extra_args);
        instance->extra_args = key_file_get_string_default(file, group, "extra-args", "");
        instance->eidolon_enabled = key_file_get_boolean_default(file, group,
                                                                  "eidolon-enabled", FALSE);
        g_free(instance->eidolon_executable);
        instance->eidolon_executable = key_file_get_string_default(file, group,
                                                                    "eidolon-executable", "eidolon");
        g_free(instance->eidolon_extra_args);
        instance->eidolon_extra_args = key_file_get_string_default(file, group,
                                                                    "eidolon-extra-args", "");
        instance->managed = key_file_get_boolean_default(file, group, "managed", TRUE);
        if(!director_instance_is_local(instance))
            instance->managed = FALSE;
        instance->autostart = key_file_get_boolean_default(file, group, "autostart", TRUE);
        instance->apply_on_connect = key_file_get_boolean_default(file, group, "apply-on-connect", TRUE);
        instance->calibration_camera = key_file_get_boolean_default(file, group, "calibration-camera", FALSE);
        if(instance->calibration_camera)
            instance->capture_device = -1;
        instance->recovery_restart_engine = key_file_get_boolean_default(file, group, "recovery-restart-engine", TRUE);
        instance->recovery_reconnect_route = key_file_get_boolean_default(file, group, "recovery-reconnect-route", TRUE);
        instance->recovery_restore_projection = key_file_get_boolean_default(file, group, "recovery-restore-projection", TRUE);
        instance->recovery_restore_mapping = key_file_get_boolean_default(file, group, "recovery-restore-mapping", TRUE);
        instance->recovery_restore_control = key_file_get_boolean_default(file, group, "recovery-restore-control", TRUE);
        instance->recovery_retry_limit = clamp_int(key_file_get_integer_default(file, group, "recovery-retry-limit", 3), 1, 20);
        instance->pattern = clamp_int(key_file_get_integer_default(file, group, "pattern", 0), 0, 8);
        key_file_get_projection_config(file, group, &instance->configured_projection);
        g_free(group);

        for(gint s = 0; s < DIRECTOR_MAX_SLICES; s++) {
            group = slice_group_name((guint)i, s);
            if(!key_file_get_slice(file, group, &instance->slices[s]) ||
               !director_slice_values_valid(&instance->slices[s],
                                            instance->output_width,
                                            instance->output_height)) {
                director_slice_set_identity(&instance->slices[s],
                                            instance->output_width,
                                            instance->output_height);
                instance->slices[s].enabled = s == 0;
            }
            g_free(group);
        }
        g_ptr_array_add(show->instances, instance);
    }
    for(gint v = 0; v < venue_count; v++) {
        gchar *group = g_strdup_printf("Venue %d", v);
        gchar *venue_name = key_file_get_string_default(file, group, "name", "");
        gint outputs = clamp_int(key_file_get_integer_default(file, group, "outputs", 0), 0, 128);
        if(!*venue_name) {
            g_free(group);
            g_free(venue_name);
            continue;
        }
        DirectorVenueProfile *profile = g_new0(DirectorVenueProfile, 1);
        profile->name = venue_name;
        profile->outputs = g_ptr_array_new_with_free_func((GDestroyNotify)director_venue_output_free);
        profile->camera_maps = g_ptr_array_new_with_free_func((GDestroyNotify)director_projector_camera_map_free);
        gint camera_maps = clamp_int(key_file_get_integer_default(file, group, "camera-maps", 0), 0, 128);
        profile->calibration_camera_id = key_file_get_string_default(file, group, "calibration-camera-id", "");
        profile->calibration_camera_path = key_file_get_string_default(file, group, "calibration-camera-path", "");
        profile->calibration_camera_name = key_file_get_string_default(file, group, "calibration-camera-name", "");
        profile->calibration_controls_valid = key_file_get_boolean_default(file, group,
                                                                            "calibration-controls-valid", FALSE);
        profile->calibration_updated_real_us = g_key_file_get_int64(file, group,
                                                                     "calibration-updated-us", NULL);
        if(profile->calibration_controls_valid) {
            gsize n_controls = 0;
            gint *controls = g_key_file_get_integer_list(file, group, "calibration-controls",
                                                         &n_controls, NULL);
            if(controls && n_controls == DIRECTOR_V4L_CONTROL_COUNT) {
                gboolean controls_valid = TRUE;
                for(gint c = 0; c < DIRECTOR_V4L_CONTROL_COUNT; c++) {
                    if(controls[c] < -1 || controls[c] > 65535) {
                        controls_valid = FALSE;
                        break;
                    }
                }
                if(controls_valid)
                    memcpy(profile->calibration_controls, controls, sizeof(profile->calibration_controls));
                else
                    profile->calibration_controls_valid = FALSE;
            }
            else
                profile->calibration_controls_valid = FALSE;
            g_free(controls);
        }
        g_free(group);
        for(gint o = 0; o < outputs; o++) {
            group = g_strdup_printf("Venue %d Output %d", v, o);
            DirectorVenueOutput *output = key_file_get_venue_output(file, group);
            g_free(group);
            if(output->instance_id && *output->instance_id)
                g_ptr_array_add(profile->outputs, output);
            else
                director_venue_output_free(output);
        }
        for(gint m = 0; m < camera_maps; m++) {
            group = g_strdup_printf("Venue %d CameraMap %d", v, m);
            gchar *instance_id = key_file_get_string_default(file, group, "instance-id", "");
            gchar *format = key_file_get_string_default(file, group, "format", "");
            gchar *encoded = key_file_get_string_default(file, group, "data-base64", "");
            gint cw = key_file_get_integer_default(file, group, "camera-width", 0);
            gint ch = key_file_get_integer_default(file, group, "camera-height", 0);
            gint pw = key_file_get_integer_default(file, group, "projector-width", 0);
            gint ph = key_file_get_integer_default(file, group, "projector-height", 0);
            gint illuminated = key_file_get_integer_default(file, group, "illuminated-pixels", 0);
            gint valid = key_file_get_integer_default(file, group, "valid-pixels", 0);
            gdouble confidence = g_key_file_get_double(file, group, "mean-confidence", NULL);
            gint64 updated = g_key_file_get_int64(file, group, "updated-us", NULL);
            if(*instance_id && g_strcmp0(format, "u32le-camera-to-projector-v1") == 0 &&
               cw > 0 && ch > 0 && cw <= 8192 && ch <= 8192 &&
               pw > 0 && ph > 0 && pw <= 65534 && ph <= 65534 &&
               (gsize)cw * (gsize)ch <= (64u * 1024u * 1024u) / sizeof(guint32) &&
               illuminated > 0 && valid >= 0 && valid <= illuminated &&
               (gsize)illuminated <= (gsize)cw * (gsize)ch && *encoded) {
                const gsize expected = (gsize)cw * (gsize)ch * sizeof(guint32);
                const gsize encoded_len = strlen(encoded);
                if(encoded_len > ((expected + 2u) / 3u) * 4u + 8u) {
                    g_free(instance_id);
                    g_free(format);
                    g_free(encoded);
                    g_free(group);
                    continue;
                }
                gsize raw_len = 0;
                guchar *raw = g_base64_decode(encoded, &raw_len);
                if(raw && raw_len == expected) {
                    guint32 *host = g_new(guint32, (gsize)cw * (gsize)ch);
                    const guint32 *le = (const guint32*)raw;
                    gboolean map_valid = TRUE;
                    guint actual_valid = 0;
                    for(gsize p = 0; p < (gsize)cw * (gsize)ch; p++) {
                        guint32 value = GUINT32_FROM_LE(le[p]);
                        if(value != DIRECTOR_CAMERA_MAP_INVALID) {
                            guint x = value & 0xffffu;
                            guint y = value >> 16;
                            if(x >= (guint)pw || y >= (guint)ph) {
                                map_valid = FALSE;
                                break;
                            }
                            actual_valid++;
                        }
                        host[p] = value;
                    }
                    if(map_valid && actual_valid > 0 &&
                       director_venue_store_camera_map(profile, instance_id, cw, ch, pw, ph,
                                                       host, (gsize)cw * (gsize)ch,
                                                       (guint)illuminated,
                                                       actual_valid, confidence)) {
                        DirectorProjectorCameraMap *map = director_venue_find_camera_map(profile, instance_id);
                        if(map)
                            map->updated_real_us = updated;
                    }
                    g_free(host);
                }
                g_free(raw);
            }
            g_free(instance_id);
            g_free(format);
            g_free(encoded);
            g_free(group);
        }
        g_ptr_array_add(show->venue_profiles, profile);
    }
    if(show->active_venue && *show->active_venue &&
       !director_show_find_venue(show, show->active_venue)) {
        g_free(show->active_venue);
        show->active_venue = g_strdup("");
    }

    for(gint n = 0; n < snapshot_count; n++) {
        gchar *group = g_strdup_printf("Snapshot %d", n);
        gchar *snapshot_name = key_file_get_string_default(file, group, "name", "");
        gint entries = clamp_int(key_file_get_integer_default(file, group, "instances", 0), 0, 128);
        g_free(group);
        if(!*snapshot_name) {
            g_free(snapshot_name);
            continue;
        }
        DirectorShowSnapshot *snapshot = g_new0(DirectorShowSnapshot, 1);
        snapshot->name = snapshot_name;
        snapshot->instances = g_ptr_array_new_with_free_func((GDestroyNotify)director_snapshot_instance_free);
        for(gint e = 0; e < entries; e++) {
            group = g_strdup_printf("Snapshot %d Instance %d", n, e);
            DirectorVenueOutput *physical = key_file_get_venue_output(file, group);
            DirectorSnapshotInstance *entry = g_new0(DirectorSnapshotInstance, 1);
            entry->instance_id = key_file_get_string_default(file, group, "instance-id", "");
            entry->should_run = key_file_get_boolean_default(file, group, "should-run", FALSE);
            entry->source_instance_id = key_file_get_string_default(file, group, "source-instance", "");
            entry->source_host = key_file_get_string_default(file, group, "source-host", "");
            entry->source_port = clamp_int(key_file_get_integer_default(file, group, "source-port", 3490), 1, 65530);
            gboolean snapshot_routes_present = FALSE;
            entry->input_routes = key_file_get_input_routes(file, group, &snapshot_routes_present);
            gchar *mode = key_file_get_string_default(file, group, "control-mode", "independent");
            entry->control_mode = director_control_mode_from_string(mode);
            g_free(mode);
            entry->master_instance_id = key_file_get_string_default(file, group, "master-instance", "");
            entry->master_host = key_file_get_string_default(file, group, "master-host", "127.0.0.1");
            entry->master_port = clamp_int(key_file_get_integer_default(file, group, "master-port", 3490), 1, 65530);
            entry->preview_forward_vims = key_file_get_boolean_default(file, group, "preview-forward-vims", FALSE);
            entry->preview_sync_samplelist = key_file_get_boolean_default(file, group, "preview-sync-samplelist", FALSE);
            entry->preview_headless = key_file_get_boolean_default(file, group, "preview-headless", FALSE);
            entry->pattern = clamp_int(key_file_get_integer_default(file, group, "pattern", 0), 0, 8);
            entry->audio_enabled = key_file_get_boolean_default(file, group, "audio-enabled", TRUE);
            entry->audio_muted = key_file_get_boolean_default(file, group, "audio-muted", FALSE);
            entry->ndi_input_enabled = key_file_get_boolean_default(file, group, "ndi-input-enabled", FALSE);
            entry->ndi_source_name = key_file_get_string_default(file, group, "ndi-source-name", "");
            entry->ndi_output_enabled = key_file_get_boolean_default(file, group, "ndi-output-enabled", FALSE);
            entry->ndi_output_name = key_file_get_string_default(file, group, "ndi-output-name", "VeeJay Program");
            entry->ndi_tally_enabled = key_file_get_boolean_default(file, group, "ndi-tally-enabled", TRUE);
            entry->ndi_follow_clock = key_file_get_boolean_default(file, group, "ndi-follow-clock", FALSE);
            if(!snapshot_routes_present) {
                if(entry->source_instance_id && *entry->source_instance_id) {
                    const DirectorInputRouteType route_type =
                        (entry->source_host &&
                         (g_strcmp0(entry->source_host, "127.0.0.1") == 0 ||
                          g_ascii_strcasecmp(entry->source_host, "localhost") == 0)) ?
                        DIRECTOR_INPUT_ROUTE_SHM : DIRECTOR_INPUT_ROUTE_TCP;
                    g_ptr_array_add(entry->input_routes, director_input_route_new(route_type,
                        entry->source_instance_id, entry->source_host, entry->source_port, NULL));
                }
                if(entry->ndi_input_enabled && entry->ndi_source_name && *entry->ndi_source_name)
                    g_ptr_array_add(entry->input_routes, director_input_route_new(DIRECTOR_INPUT_ROUTE_NDI,
                        NULL, NULL, 0, entry->ndi_source_name));
            }
            entry->physical = *physical;
            g_free(entry->physical.instance_id);
            entry->physical.instance_id = NULL;
            g_free(physical);
            g_free(group);
            if(entry->instance_id && *entry->instance_id)
                g_ptr_array_add(snapshot->instances, entry);
            else
                director_snapshot_instance_free(entry);
        }
        g_ptr_array_add(show->snapshots, snapshot);
    }

    g_key_file_free(file);
    show->path = g_strdup(path);
    show->dirty = FALSE;
    return show;
}


static gboolean director_short_option_owned(const gchar *arg, gchar option)
{
    return arg && arg[0] == '-' && arg[1] == option && arg[2] != '-';
}

static gboolean director_extra_arg_reserved(const gchar *arg, DirectorRole role)
{
    (void)role;
    if(!arg || !*arg)
        return FALSE;

    static const gchar *long_options[] = {
        "--port", "--instance-role", "--instance-id", "--master", "--connect",
        "--output-source", "--output-source-pid", "--output-source-shm",
        "--blank", "--dummy", "--source-width", "--source-height",
        "--input-width", "--input-height", "--project-width", "--project-height",
        "--output-width", "--output-height", "--fps", "--norm",
        "--output", "--graphics-driver", "--output-file", "--yuv",
        "--audio", "--audio-muted", "--audio-sync-thread", "--no-audio-sync-thread",
        "--audio-beat-thread", "--no-audio-beat-thread", "--synchronization",
        "--auto-loop", "--clip-as-sample", "--deinterlace", "--composite",
        "--no-viewport",
        "--window-size", "--size", "--window-x", "--window-y",
        "--geometry-x", "--geometry-y", "--borderless", "--fullscreen",
        "--windowed", "--no-keyboard",
        "--no-mouse", "--show-cursor", "--memory", "--max_cache", "--max-cache",
        "--verbose", "--no-color", "--timer", "--pace-correction",
        "--audiorate", "--audio-channels", "--audio-bits", "--scene-detection",
        "--swap-range", "--dynamic-fx-chain", "--split-screen",
        "--fx-custom-default-values", "--preserve-pathnames", "--bezerk",
        "--multicast-osc", "--multicast-vims",
        "--sample-file", "--action-file", "--capture-device", "--load-generators",
        "--ndi-receive", "--ndi-list", "--ndi-send", "--ndi-name",
        "--ndi-no-tally", "--ndi-follow-clock", NULL
    };

    for(gint i = 0; long_options[i]; i++) {
        if(g_strcmp0(arg, long_options[i]) == 0)
            return TRUE;
        gchar *prefix = g_strconcat(long_options[i], "=", NULL);
        gboolean match = g_str_has_prefix(arg, prefix);
        g_free(prefix);
        if(match)
            return TRUE;
    }

    static const gchar short_options[] = {
        'p','d','W','H','w','h','f','N','O','G','o','Y','a','c','L','g','I','D','K','C',
        's','x','y','m','j','v','n','t','r','S','e','X','P','q','b','M','T','l','F','A','Z','\0'
    };
    for(gint i = 0; short_options[i]; i++) {
        if(director_short_option_owned(arg, short_options[i]))
            return TRUE;
    }
    return FALSE;
}

static void argv_add(GPtrArray *argv, const gchar *value)
{
    g_ptr_array_add(argv, g_strdup(value));
}

static void argv_add_int(GPtrArray *argv, gint value)
{
    g_ptr_array_add(argv, g_strdup_printf("%d", value));
}

static void argv_add_double(GPtrArray *argv, gdouble value)
{
    gchar buffer[64];
    g_ascii_dtostr(buffer, sizeof(buffer), value);
    argv_add(argv, buffer);
}

static gchar **argv_finish(GPtrArray *argv)
{
    g_ptr_array_add(argv, NULL);
    gchar **result = g_new0(gchar*, argv->len);
    for(guint i = 0; i < argv->len; i++) {
        result[i] = g_ptr_array_index(argv, i);
        argv->pdata[i] = NULL;
    }
    g_ptr_array_free(argv, TRUE);
    return result;
}

static gboolean director_output_driver_needs_file(gint driver)
{
    return driver == 4 || driver == 5 || driver == 6 ||
           driver == 7 || driver == 8;
}

static gboolean director_source_host_is_local(const gchar *host)
{
    if(!host || !*host)
        return FALSE;

    return g_ascii_strcasecmp(host, "localhost") == 0 ||
           g_strcmp0(host, "127.0.0.1") == 0 ||
           g_strcmp0(host, "::1") == 0;
}

static gboolean director_control_hosts_equivalent(const gchar *a, const gchar *b)
{
    if(!a || !b)
        return FALSE;
    if(g_ascii_strcasecmp(a, b) == 0)
        return TRUE;
    return director_source_host_is_local(a) && director_source_host_is_local(b);
}

static gboolean director_model_route_reaches(const DirectorShow *show,
                                               const DirectorInstance *cursor,
                                               const DirectorInstance *target,
                                               GHashTable *visited)
{
    if(!show || !cursor)
        return FALSE;
    if(cursor == target)
        return TRUE;
    if(g_hash_table_contains(visited, cursor))
        return FALSE;
    g_hash_table_add(visited, (gpointer)cursor);

    gboolean have_native_routes = FALSE;
    if(cursor->input_routes) {
        for(guint i = 0; i < cursor->input_routes->len; i++) {
            const DirectorInputRoute *route = g_ptr_array_index(cursor->input_routes, i);
            if(!route || route->type == DIRECTOR_INPUT_ROUTE_NDI ||
               !route->source_instance_id || !*route->source_instance_id)
                continue;
            have_native_routes = TRUE;
            DirectorInstance *next = director_show_find_instance((DirectorShow*)show,
                                                                  route->source_instance_id);
            if(director_model_route_reaches(show, next, target, visited))
                return TRUE;
        }
    }
    if(!have_native_routes && cursor->source_instance_id && *cursor->source_instance_id) {
        DirectorInstance *next = director_show_find_instance((DirectorShow*)show,
                                                              cursor->source_instance_id);
        if(director_model_route_reaches(show, next, target, visited))
            return TRUE;
    }
    return FALSE;
}

static gboolean director_instance_routes_valid(const DirectorShow *show,
                                                const DirectorInstance *instance,
                                                GError **error)
{
    if(!instance || !instance->input_routes)
        return TRUE;
    if(instance->role == DIRECTOR_ROLE_OUTPUT && instance->input_routes->len > 1) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Output instance '%s' can have only one primary input route",
                    instance->id);
        return FALSE;
    }

    for(guint i = 0; i < instance->input_routes->len; i++) {
        const DirectorInputRoute *route = g_ptr_array_index(instance->input_routes, i);
        if(!route || route->type < DIRECTOR_INPUT_ROUTE_SHM ||
           route->type > DIRECTOR_INPUT_ROUTE_NDI) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' contains an invalid input route", instance->id);
            return FALSE;
        }
        if(route->type == DIRECTOR_INPUT_ROUTE_NDI) {
            if(instance->role == DIRECTOR_ROLE_OUTPUT ||
               !route->ndi_source_name || !*route->ndi_source_name ||
               strlen(route->ndi_source_name) > DIRECTOR_NDI_SOURCE_NAME_MAX) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Instance '%s' contains an invalid NDI input route", instance->id);
                return FALSE;
            }
            continue;
        }

        if(!route->source_instance_id || !*route->source_instance_id ||
           route->port < 1 || route->port > 65530 ||
           (route->type == DIRECTOR_INPUT_ROUTE_TCP &&
            (!route->host || !*route->host))) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' contains an incomplete native input route", instance->id);
            return FALSE;
        }
        DirectorInstance *source = director_show_find_instance((DirectorShow*)show,
                                                                route->source_instance_id);
        if(!source || source == instance) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' selects a missing or invalid video source '%s'",
                        instance->id, route->source_instance_id);
            return FALSE;
        }
        GHashTable *visited = g_hash_table_new(g_direct_hash, g_direct_equal);
        const gboolean cycle = director_model_route_reaches(show, source, instance, visited);
        g_hash_table_destroy(visited);
        if(cycle) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Video routing for instance '%s' contains a feedback cycle",
                        instance->id);
            return FALSE;
        }
    }
    return TRUE;
}

gchar **director_instance_build_argv(const DirectorShow *show,
                                     const DirectorInstance *instance,
                                     GError **error)
{
    if(!instance || !instance->executable || !*instance->executable) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance executable is empty");
        return NULL;
    }
    if(!director_instance_id_valid(instance->id)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance ID is invalid");
        return NULL;
    }
    if(instance->role < DIRECTOR_ROLE_STANDALONE ||
       instance->role > DIRECTOR_ROLE_OUTPUT) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid role", instance->id);
        return NULL;
    }
    if(!instance->host || !*instance->host ||
       instance->port < 1 || instance->port > 65535) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid control endpoint", instance->id);
        return NULL;
    }
    if(instance->display_index < -1 || instance->display_index > 63 ||
       instance->display_width < 0 || instance->display_height < 0 ||
       (instance->display_index >= 0 &&
        (instance->display_width <= 0 || instance->display_height <= 0))) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid display target", instance->id);
        return NULL;
    }
    if(instance->recovery_retry_limit < 1 || instance->recovery_retry_limit > 20) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' recovery retry limit must be between 1 and 20", instance->id);
        return NULL;
    }
    if(instance->output_width <= 0 || instance->output_height <= 0 ||
       instance->output_width > 32768 || instance->output_height > 32768) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid project canvas", instance->id);
        return NULL;
    }
    if((instance->window_width > 0) != (instance->window_height > 0)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "SDL window width and height must both be set, or both left automatic");
        return NULL;
    }
    if(instance->fps < 0.0 ||
       (instance->fps > 0.0 && instance->fps < 1.0) ||
       instance->fps > 240.0) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "FPS must be 0 (automatic) or between 1 and 240");
        return NULL;
    }
    if(instance->norm < -1 || instance->norm > 2 ||
       instance->yuv_mode < 0 || instance->yuv_mode > 2 ||
       (instance->output_driver != 0 && instance->output_driver != 3 &&
        instance->output_driver != 4 && instance->output_driver != 6)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an unsupported video-output setting", instance->id);
        return NULL;
    }
    if(instance->role != DIRECTOR_ROLE_OUTPUT &&
       instance->startup_mode == DIRECTOR_STARTUP_BLANK &&
       (instance->input_width <= 0 || instance->input_height <= 0 ||
        instance->input_width > 32768 || instance->input_height > 32768)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid blank-source size", instance->id);
        return NULL;
    }
    if(instance->control_mode < DIRECTOR_CONTROL_INDEPENDENT ||
       instance->control_mode > DIRECTOR_CONTROL_PREVIEW) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an invalid master/preview mode", instance->id);
        return NULL;
    }
    if(instance->role == DIRECTOR_ROLE_OUTPUT &&
       instance->control_mode != DIRECTOR_CONTROL_INDEPENDENT) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Output instance '%s' cannot be a master or work-ahead preview", instance->id);
        return NULL;
    }
    if(instance->control_mode != DIRECTOR_CONTROL_PREVIEW &&
       (instance->preview_forward_vims || instance->preview_sync_samplelist ||
        instance->preview_headless)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' uses Preview-only options outside Preview mode", instance->id);
        return NULL;
    }

    if(instance->ndi_input_enabled) {
        if(instance->role == DIRECTOR_ROLE_OUTPUT) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Output instance '%s' cannot use an NDI source directly", instance->id);
            return NULL;
        }
        if(!instance->ndi_source_name || !*instance->ndi_source_name) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' has NDI input enabled but no source selected", instance->id);
            return NULL;
        }
        if(strlen(instance->ndi_source_name) > DIRECTOR_NDI_SOURCE_NAME_MAX) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' has an NDI source name longer than %d characters",
                        instance->id, DIRECTOR_NDI_SOURCE_NAME_MAX);
            return NULL;
        }
        if(instance->startup_mode == DIRECTOR_STARTUP_MEDIA ||
           instance->capture_device >= 0 || instance->generator_stream >= 0) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "NDI input cannot be combined with startup media, V4L2 capture or generators");
            return NULL;
        }
    }
    if(instance->ndi_output_enabled &&
       (!instance->ndi_output_name || !*instance->ndi_output_name)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has NDI output enabled but no sender name", instance->id);
        return NULL;
    }
    if(instance->ndi_output_enabled &&
       strlen(instance->ndi_output_name) > DIRECTOR_NDI_SOURCE_NAME_MAX) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Instance '%s' has an NDI sender name longer than %d characters",
                    instance->id, DIRECTOR_NDI_SOURCE_NAME_MAX);
        return NULL;
    }

    if(!director_instance_routes_valid(show, instance, error))
        return NULL;

    const gint effective_output_driver =
        instance->control_mode == DIRECTOR_CONTROL_PREVIEW && instance->preview_headless ?
        3 : instance->output_driver;
    if(director_output_driver_needs_file(effective_output_driver) &&
       (!instance->output_file || !*instance->output_file)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Output driver %d requires an output file or device", effective_output_driver);
        return NULL;
    }

    DirectorInstance *master = NULL;
    const gchar *master_host = instance->master_host;
    gint master_port = instance->master_port;
    if(instance->control_mode == DIRECTOR_CONTROL_PREVIEW) {
        if(instance->master_instance_id && *instance->master_instance_id) {
            master = director_show_find_instance((DirectorShow*)show,
                                                 instance->master_instance_id);
            if(!master || master == instance || master->role == DIRECTOR_ROLE_OUTPUT ||
               master->control_mode != DIRECTOR_CONTROL_MASTER) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Preview instance '%s' selects an instance that is not configured as Master: '%s'",
                            instance->id, instance->master_instance_id);
                return NULL;
            }
            master_host = master->host;
            master_port = master->port;
        }
        if(!master_host || !*master_host || master_port < 1 || master_port > 65535) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Preview instance '%s' has no valid master endpoint", instance->id);
            return NULL;
        }
        if(director_control_hosts_equivalent(instance->host, master_host) &&
           instance->port == master_port) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Preview instance '%s' cannot connect to its own control endpoint", instance->id);
            return NULL;
        }
    }

    if(instance->split_master_instance_id && *instance->split_master_instance_id) {
        DirectorInstance *split_master = director_show_find_instance((DirectorShow*)show,
                                                                     instance->split_master_instance_id);
        if(!split_master || split_master == instance ||
           !split_master->split_screen_file || !*split_master->split_screen_file ||
           instance->split_row < 0 || instance->split_column < 0) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' has an invalid native video-wall assignment",
                        instance->id);
            return NULL;
        }
    }

    DirectorInstance *video_source = NULL;
    if(instance->source_instance_id && *instance->source_instance_id) {
        video_source = director_show_find_source_for_instance((DirectorShow*)show, instance);
        if(!video_source) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' selects a missing or invalid video source '%s'",
                        instance->id, instance->source_instance_id);
            return NULL;
        }
        DirectorInstance *cursor = video_source;
        for(guint guard = 0; cursor && guard <= show->instances->len; guard++) {
            if(cursor == instance) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Video routing for instance '%s' contains a feedback cycle",
                            instance->id);
                return NULL;
            }
            cursor = director_show_find_source_for_instance((DirectorShow*)show, cursor);
            if(guard == show->instances->len && cursor) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Video routing contains a feedback cycle");
                return NULL;
            }
        }
    }

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    argv_add(argv, instance->executable);
    argv_add(argv, "--instance-role");
    argv_add(argv, director_role_name(instance->role));
    argv_add(argv, "--instance-id");
    argv_add(argv, instance->id);
    argv_add(argv, "--port");
    argv_add_int(argv, instance->port);
    if(instance->control_mode == DIRECTOR_CONTROL_MASTER)
        argv_add(argv, "--master");
    else if(instance->control_mode == DIRECTOR_CONTROL_PREVIEW) {
        argv_add(argv, "--connect");
        g_ptr_array_add(argv, g_strdup_printf("%s:%d", master_host, master_port));
    }
    argv_add(argv, "--project-width");
    argv_add_int(argv, instance->output_width);
    argv_add(argv, "--project-height");
    argv_add_int(argv, instance->output_height);

    if(instance->role == DIRECTOR_ROLE_OUTPUT) {
        const gchar *host = instance->source_host;
        gint port = instance->source_port;
        if(video_source) {
            if(director_control_hosts_equivalent(instance->host, video_source->host))
                host = "127.0.0.1";
            else if(video_source->stream_advertise_host && *video_source->stream_advertise_host)
                host = video_source->stream_advertise_host;
            else if(director_source_host_is_local(video_source->host))
                host = NULL;
            else
                host = video_source->host;
            port = video_source->port;
        }
        argv_add(argv, "--blank");
        argv_add(argv, "--source-width");
        argv_add_int(argv, instance->output_width);
        argv_add(argv, "--source-height");
        argv_add_int(argv, instance->output_height);
        if(video_source &&
           director_control_hosts_equivalent(instance->host, video_source->host) &&
           video_source->shm_key > 0) {
            argv_add(argv, "--output-source-shm");
            argv_add_int(argv, video_source->shm_key);
        }
        else if(host && *host && port > 0 && port <= 65535) {
            argv_add(argv, "--output-source");
            g_ptr_array_add(argv, g_strdup_printf("%s:%d", host, port));
        }
    }
    else if(instance->ndi_input_enabled) {
        const gint input_width = instance->input_width > 0 ?
                                 instance->input_width : instance->output_width;
        const gint input_height = instance->input_height > 0 ?
                                  instance->input_height : instance->output_height;
        argv_add(argv, "--ndi-receive");
        argv_add(argv, instance->ndi_source_name);
        argv_add(argv, "--source-width");
        argv_add_int(argv, input_width);
        argv_add(argv, "--source-height");
        argv_add_int(argv, input_height);
        if(!instance->ndi_tally_enabled)
            argv_add(argv, "--ndi-no-tally");
        if(instance->ndi_follow_clock)
            argv_add(argv, "--ndi-follow-clock");
    }
    else if(instance->startup_mode == DIRECTOR_STARTUP_MEDIA) {
        if(!instance->media_files || instance->media_files->len == 0) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                        "Instance '%s' is configured for startup media but its Media Bank is empty",
                        instance->id);
            g_ptr_array_free(argv, TRUE);
            return NULL;
        }
        for(guint i = 0; i < instance->media_files->len; i++) {
            const gchar *path = g_ptr_array_index(instance->media_files, i);
            if(!path || !*path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Startup media file is missing: %s", path ? path : "(empty)");
                g_ptr_array_free(argv, TRUE);
                return NULL;
            }
        }
    }
    else {
        const gint input_width = instance->input_width > 0 ?
                                 instance->input_width : instance->output_width;
        const gint input_height = instance->input_height > 0 ?
                                  instance->input_height : instance->output_height;
        argv_add(argv, "--blank");
        argv_add(argv, "--source-width");
        argv_add_int(argv, input_width);
        argv_add(argv, "--source-height");
        argv_add_int(argv, input_height);
    }

    if(instance->fps > 0.0) {
        argv_add(argv, "--fps");
        argv_add_double(argv, instance->fps);
    }
    if(instance->norm >= 0) {
        argv_add(argv, "--norm");
        argv_add_int(argv, instance->norm);
    }

    argv_add(argv, "--output");
    argv_add_int(argv, effective_output_driver);
    if(instance->output_file && *instance->output_file) {
        argv_add(argv, "--output-file");
        argv_add(argv, instance->output_file);
    }
    if(instance->yuv_mode > 0) {
        argv_add(argv, "--yuv");
        argv_add_int(argv, instance->yuv_mode);
    }
    if(instance->ndi_output_enabled) {
        argv_add(argv, "--ndi-send");
        argv_add(argv, "--ndi-name");
        argv_add(argv, instance->ndi_output_name);
    }

    argv_add(argv, "--synchronization");
    argv_add_int(argv, instance->sync_correction ? 1 : 0);

    const gboolean output_role = instance->role == DIRECTOR_ROLE_OUTPUT;
    argv_add(argv, "--audio");
    argv_add_int(argv, (!output_role && instance->audio_enabled) ? 1 : 0);
    if(!output_role && instance->audio_muted)
        argv_add(argv, "--audio-muted");
    if(output_role || !instance->audio_sync_thread)
        argv_add(argv, "--no-audio-sync-thread");
    if(output_role || !instance->audio_beat_thread)
        argv_add(argv, "--no-audio-beat-thread");
    if(!output_role && instance->auto_loop)
        argv_add(argv, "--auto-loop");
    if(!output_role && instance->clip_as_sample &&
       instance->startup_mode == DIRECTOR_STARTUP_MEDIA)
        argv_add(argv, "--clip-as-sample");
    if(instance->deinterlace)
        argv_add(argv, "--deinterlace");
    if(!output_role && !instance->legacy_viewport)
        argv_add(argv, "--no-viewport");
    argv_add(argv, instance->fullscreen ? "--fullscreen" : "--windowed");
    if(instance->borderless)
        argv_add(argv, "--borderless");
    if(instance->no_keyboard)
        argv_add(argv, "--no-keyboard");
    if(instance->no_mouse)
        argv_add(argv, "--no-mouse");
    if(instance->show_cursor)
        argv_add(argv, "--show-cursor");
    if(instance->window_width > 0 && instance->window_height > 0) {
        argv_add(argv, "--window-size");
        g_ptr_array_add(argv, g_strdup_printf("%dx%d",
                                              instance->window_width,
                                              instance->window_height));
    }
    const gboolean use_display_target = instance->display_index >= 0 &&
                                           instance->display_width > 0 &&
                                           instance->display_height > 0;
    const gint launch_x = use_display_target ?
                          instance->display_x + 1 : instance->window_x;
    const gint launch_y = use_display_target ?
                          instance->display_y + 1 : instance->window_y;
    if(launch_x >= 0 || use_display_target) {
        argv_add(argv, "--window-x");
        argv_add_int(argv, launch_x);
    }
    if(launch_y >= 0 || use_display_target) {
        argv_add(argv, "--window-y");
        argv_add_int(argv, launch_y);
    }
    if(instance->verbose)
        argv_add(argv, "--verbose");
    if(instance->no_color)
        argv_add(argv, "--no-color");

    if(instance->memory_percent >= 0) {
        argv_add(argv, "--memory");
        argv_add_int(argv, instance->memory_percent);
    }
    if(instance->max_cache > 0) {
        argv_add(argv, "--max-cache");
        argv_add_int(argv, instance->max_cache);
    }

    argv_add(argv, "--timer");
    argv_add_int(argv, instance->timer_mode);
    if(instance->pace_correction_ms > 0) {
        argv_add(argv, "--pace-correction");
        argv_add_int(argv, instance->pace_correction_ms);
    }
    if(!output_role) {
        argv_add(argv, "--audiorate");
        argv_add_int(argv, instance->audio_rate);
        argv_add(argv, "--audio-channels");
        argv_add_int(argv, instance->audio_channels);
        argv_add(argv, "--audio-bits");
        argv_add_int(argv, instance->audio_bits);
    }
    if(instance->scene_detection >= 0) {
        argv_add(argv, "--scene-detection");
        argv_add_int(argv, instance->scene_detection);
    }
    if(!output_role && instance->capture_device >= 0 && instance->generator_stream >= 0) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Capture device and generator startup overrides are mutually exclusive");
        g_ptr_array_free(argv, TRUE);
        return NULL;
    }
    if(!output_role && instance->capture_device >= 0) { argv_add(argv, "--capture-device"); argv_add_int(argv, instance->capture_device); }
    if(!output_role && instance->generator_stream >= 0) { argv_add(argv, "--load-generators"); argv_add_int(argv, instance->generator_stream); }
    if(instance->swap_range) argv_add(argv, "--swap-range");
    if(instance->dynamic_fx_chain) argv_add(argv, "--dynamic-fx-chain");
    if(instance->fx_custom_defaults) argv_add(argv, "--fx-custom-default-values");
    if(instance->preserve_pathnames) argv_add(argv, "--preserve-pathnames");
    if(instance->bezerk) argv_add(argv, "--bezerk");
    if(instance->split_screen_file && *instance->split_screen_file) { argv_add(argv, "--split-screen"); argv_add(argv, instance->split_screen_file); }
    if(instance->multicast_osc && *instance->multicast_osc) { argv_add(argv, "--multicast-osc"); argv_add(argv, instance->multicast_osc); }
    if(instance->multicast_vims && *instance->multicast_vims) { argv_add(argv, "--multicast-vims"); argv_add(argv, instance->multicast_vims); }
    if(instance->sample_file && *instance->sample_file) { argv_add(argv, "--sample-file"); argv_add(argv, instance->sample_file); }
    if(instance->action_file && *instance->action_file) { argv_add(argv, "--action-file"); argv_add(argv, instance->action_file); }

    if(instance->extra_args && *instance->extra_args) {
        gint extra_argc = 0;
        gchar **extra_argv = NULL;
        if(!g_shell_parse_argv(instance->extra_args, &extra_argc, &extra_argv, error)) {
            g_ptr_array_free(argv, TRUE);
            return NULL;
        }
        for(gint i = 0; i < extra_argc; i++) {
            if(director_extra_arg_reserved(extra_argv[i], instance->role)) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                            "Custom arguments may not override Director-owned option '%s'",
                            extra_argv[i]);
                g_strfreev(extra_argv);
                g_ptr_array_free(argv, TRUE);
                return NULL;
            }
            argv_add(argv, extra_argv[i]);
        }
        g_strfreev(extra_argv);
    }

    if(!output_role && !instance->ndi_input_enabled &&
       instance->startup_mode == DIRECTOR_STARTUP_MEDIA) {
        for(guint i = 0; i < instance->media_files->len; i++)
            argv_add(argv, g_ptr_array_index(instance->media_files, i));
    }

    return argv_finish(argv);
}

gboolean director_instance_is_local(const DirectorInstance *instance)
{
    if(!instance || !instance->host)
        return FALSE;
    return g_ascii_strcasecmp(instance->host, "localhost") == 0 ||
           g_strcmp0(instance->host, "127.0.0.1") == 0 ||
           g_strcmp0(instance->host, "::1") == 0;
}

void director_instance_clear_live_metrics(DirectorInstance *instance)
{
    if(!instance)
        return;
    memset(instance->perf_history, 0, sizeof(instance->perf_history));
    instance->perf_history_head = 0;
    instance->perf_history_count = 0;
    instance->budget_us = 0;
    instance->dropped = 0;
    instance->replaced = 0;
    instance->stalls = 0;
    instance->delivery_counters_seen = FALSE;
    instance->delivery_last_issue_us = 0;
    g_ptr_array_set_size(instance->stages, 0);
}

static gboolean parse_signed(const gchar *value, gint *result)
{
    gchar *end = NULL;
    gint64 parsed;
    if(!value || !*value)
        return FALSE;
    parsed = g_ascii_strtoll(value, &end, 10);
    if(end == value || *end != '\0' || parsed < G_MININT || parsed > G_MAXINT)
        return FALSE;
    *result = (gint)parsed;
    return TRUE;
}

static gboolean parse_unsigned64(const gchar *value, guint64 *result)
{
    gchar *end = NULL;
    guint64 parsed;
    if(!value || !*value || *value == '-')
        return FALSE;
    parsed = g_ascii_strtoull(value, &end, 10);
    if(end == value || *end != '\0')
        return FALSE;
    *result = parsed;
    return TRUE;
}

static gboolean parse_double_ascii(const gchar *value, gdouble *result)
{
    gchar *end = NULL;
    gdouble parsed;
    if(!value || !*value)
        return FALSE;
    parsed = g_ascii_strtod(value, &end);
    if(end == value || *end != '\0' || !isfinite(parsed))
        return FALSE;
    *result = parsed;
    return TRUE;
}

static GHashTable *parse_line_key_values(const gchar *text,
                                         const gchar *header,
                                         GError **error)
{
    if(!text || !header) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Missing line-oriented status response");
        return NULL;
    }

    gchar **lines = g_strsplit(text, "\n", -1);
    if(!lines[0] || g_strcmp0(g_strstrip(lines[0]), header) != 0) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Expected response header '%s'", header);
        g_strfreev(lines);
        return NULL;
    }

    GHashTable *values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for(gint i = 1; lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if(!*line)
            continue;
        gchar *equals = strchr(line, '=');
        if(!equals || equals == line)
            continue;
        *equals = '\0';
        gchar *key = g_strstrip(line);
        gchar *value = g_strstrip(equals + 1);
        g_hash_table_replace(values, g_strdup(key), g_strdup(value));
    }
    g_strfreev(lines);
    return values;
}

static GHashTable *parse_key_values(const gchar *text, const gchar *prefix,
                                    GError **error)
{
    if(!text || !g_str_has_prefix(text, prefix)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Expected response prefix '%s'", prefix);
        return NULL;
    }
    gchar **tokens = g_strsplit(text, " ", -1);
    GHashTable *values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for(gint i = 2; tokens[i]; i++) {
        gchar *equals = strchr(tokens[i], '=');
        if(!equals)
            continue;
        *equals = '\0';
        g_hash_table_replace(values, g_strdup(tokens[i]), g_strdup(equals + 1));
    }
    g_strfreev(tokens);
    return values;
}

static void replace_string(gchar **target, const gchar *value)
{
    g_free(*target);
    *target = g_strdup(value ? value : "");
}

gboolean director_instance_parse_instance_status(DirectorInstance *instance,
                                                 const gchar *text,
                                                 GError **error)
{
    GHashTable *values = parse_key_values(text, "VJINSTANCE 1", error);
    if(!values)
        return FALSE;
    const gchar *role = g_hash_table_lookup(values, "role");
    const gchar *id = g_hash_table_lookup(values, "id");
    const gchar *port = g_hash_table_lookup(values, "port");
    if(!role || !id || !port ||
       (g_strcmp0(role, "standalone") != 0 &&
        g_strcmp0(role, "program") != 0 &&
        g_strcmp0(role, "output") != 0) ||
       !director_instance_id_valid(id)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Incomplete or invalid VJINSTANCE response");
        g_hash_table_destroy(values);
        return FALSE;
    }
    if(!parse_signed(port, &instance->live_port)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Incomplete or invalid VJINSTANCE response");
        g_hash_table_destroy(values);
        return FALSE;
    }
    instance->live_role = director_role_from_string(role);
    replace_string(&instance->live_id, id);
    instance->shm_key = 0;
    instance->shm_enabled = FALSE;
    instance->source_sequence = 0;

    const gchar *shm_key = g_hash_table_lookup(values, "shm_key");
    const gchar *shm_enabled = g_hash_table_lookup(values, "shm_enabled");
    if(shm_key)
        parse_signed(shm_key, &instance->shm_key);
    gint enabled = 0;
    if(shm_enabled && parse_signed(shm_enabled, &enabled))
        instance->shm_enabled = enabled != 0;

    const gchar *sequence = g_hash_table_lookup(values, "source_sequence");
    if(sequence)
        parse_unsigned64(sequence, &instance->source_sequence);
    const gchar *source = g_hash_table_lookup(values, "source");
    if(!source)
        source = g_hash_table_lookup(values, "source_pid");
    if(!source)
        source = g_hash_table_lookup(values, "source_shm");
    replace_string(&instance->live_source, source);

    instance->backend_ready = instance->live_role == DIRECTOR_ROLE_OUTPUT ?
                              instance->source_sequence > 0 :
                              (instance->live_role == DIRECTOR_ROLE_PROGRAM ?
                               instance->shm_enabled : TRUE);
    replace_string(&instance->last_instance_status, text);
    g_hash_table_destroy(values);
    return TRUE;
}

static gboolean parse_slice_values(const gchar *value,
                                   gint output_width,
                                   gint output_height,
                                   DirectorSlice *slice)
{
    gchar **parts = g_strsplit(value, ",", -1);
    gint values[13];
    gint count = 0;
    for(; parts[count] && count < 13; count++) {
        if(!parse_signed(parts[count], &values[count])) {
            g_strfreev(parts);
            return FALSE;
        }
    }

    const gboolean valid = count == 13 && parts[count] == NULL &&
        values[0] >= 0 && values[1] >= 0 &&
        values[2] > 0 && values[3] > 0 &&
        (gint64)values[0] + values[2] <= 10000 &&
        (gint64)values[1] + values[3] <= 10000 &&
        values[4] >= 0 && values[5] >= 0 &&
        values[6] > 0 && values[7] > 0 &&
        (gint64)values[4] + values[6] <= output_width &&
        (gint64)values[5] + values[7] <= output_height &&
        values[8] >= 0 && values[9] >= 0 &&
        values[10] >= 0 && values[11] >= 0 &&
        (gint64)values[8] + values[9] <= values[6] &&
        (gint64)values[10] + values[11] <= values[7] &&
        values[12] >= 10 && values[12] <= 1000;

    if(valid) {
        memset(slice, 0, sizeof(*slice));
        slice->enabled = TRUE;
        slice->source_x = values[0];
        slice->source_y = values[1];
        slice->source_width = values[2];
        slice->source_height = values[3];
        slice->dest_x = values[4];
        slice->dest_y = values[5];
        slice->dest_width = values[6];
        slice->dest_height = values[7];
        slice->blend_left = values[8];
        slice->blend_right = values[9];
        slice->blend_top = values[10];
        slice->blend_bottom = values[11];
        slice->blend_gamma = values[12];
    }
    g_strfreev(parts);
    return valid;
}

gboolean director_instance_parse_output_status(DirectorInstance *instance,
                                               const gchar *text,
                                               GError **error)
{
    GHashTable *values = parse_key_values(text, "VJOUTPUT 1", error);
    if(!values)
        return FALSE;

    gint width = 0;
    gint height = 0;
    gint pattern = -1;
    gint reported_slices = -1;
    const gboolean header_valid =
        parse_signed(g_hash_table_lookup(values, "width"), &width) &&
        parse_signed(g_hash_table_lookup(values, "height"), &height) &&
        parse_signed(g_hash_table_lookup(values, "pattern"), &pattern) &&
        parse_signed(g_hash_table_lookup(values, "slices"), &reported_slices) &&
        width > 0 && width <= 32768 &&
        height > 0 && height <= 32768 &&
        pattern >= 0 && pattern <= 9 &&
        reported_slices >= 0 && reported_slices <= DIRECTOR_MAX_SLICES;
    if(!header_valid) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Incomplete or invalid VJOUTPUT response header");
        g_hash_table_destroy(values);
        return FALSE;
    }

    DirectorSlice slices[DIRECTOR_MAX_SLICES];
    gboolean enabled[DIRECTOR_MAX_SLICES];
    memset(slices, 0, sizeof(slices));
    memset(enabled, 0, sizeof(enabled));
    gint parsed_slices = 0;

    for(gint i = 0; i < DIRECTOR_MAX_SLICES; i++) {
        gchar *key = g_strdup_printf("slice%d", i);
        const gchar *value = g_hash_table_lookup(values, key);
        if(value) {
            if(!parse_slice_values(value, width, height, &slices[i])) {
                g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                            "Invalid VJOUTPUT area description '%s'", key);
                g_free(key);
                g_hash_table_destroy(values);
                return FALSE;
            }
            enabled[i] = TRUE;
            parsed_slices++;
        }
        g_free(key);
    }

    if(parsed_slices != reported_slices) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "VJOUTPUT reports %d areas but describes %d",
                    reported_slices, parsed_slices);
        g_hash_table_destroy(values);
        return FALSE;
    }

    instance->live_graph_width = width;
    instance->live_graph_height = height;
    instance->live_pattern = pattern;
    memcpy(instance->live_slices, slices, sizeof(slices));
    memcpy(instance->live_slice_enabled, enabled, sizeof(enabled));
    replace_string(&instance->last_output_status, text);
    g_hash_table_destroy(values);
    return TRUE;
}

DirectorStageMetric *director_instance_find_stage(DirectorInstance *instance,
                                                  const gchar *name)
{
    if(!instance || !name)
        return NULL;
    for(guint i = 0; i < instance->stages->len; i++) {
        DirectorStageMetric *metric = g_ptr_array_index(instance->stages, i);
        if(g_strcmp0(metric->name, name) == 0)
            return metric;
    }
    return NULL;
}

static gboolean parse_stage(const gchar *name, const gchar *value,
                            DirectorStageMetric *metric)
{
    gchar **parts = g_strsplit(value, ",", -1);
    guint64 values[6] = { 0, 0, 0, 0, 0, 0 };
    gint count = 0;
    for(; parts[count] && count < 6; count++) {
        if(!parse_unsigned64(parts[count], &values[count])) {
            g_strfreev(parts);
            return FALSE;
        }
    }
    gboolean valid = (count == 5 || count == 6) && parts[count] == NULL;
    if(valid) {
        g_strlcpy(metric->name, name, sizeof(metric->name));
        metric->count = values[0];
        metric->avg_us = values[1];
        metric->p95_us = values[2];
        metric->max_us = values[3];
        metric->over_budget = values[4];
        metric->recent_us = count == 6 ? values[5] : values[1];
    }
    g_strfreev(parts);
    return valid;
}

static void director_instance_append_perf_history(DirectorInstance *instance)
{
    DirectorPerfHistorySample sample = { 0.0, 0.0, 0.0 };
    DirectorStageMetric *producer = director_instance_find_stage(instance, "producer_total");
    DirectorStageMetric *renderer = director_instance_find_stage(instance, "renderer_total");
    DirectorStageMetric *queue_wait = director_instance_find_stage(instance, "queue_wait");
    DirectorStageMetric *sync_wait = director_instance_find_stage(instance, "sync_wait");
    DirectorStageMetric *output_graph = director_instance_find_stage(instance, "output_graph");

    if(producer)
        sample.producer_ms = producer->recent_us / 1000.0;
    if(renderer) {
        const guint64 wait_us = (queue_wait ? queue_wait->recent_us : 0) +
                                (sync_wait ? sync_wait->recent_us : 0);
        sample.renderer_work_ms = renderer->recent_us > wait_us ?
                                  (renderer->recent_us - wait_us) / 1000.0 : 0.0;
    }
    if(output_graph)
        sample.output_graph_ms = output_graph->recent_us / 1000.0;

    instance->perf_history[instance->perf_history_head] = sample;
    instance->perf_history_head = (instance->perf_history_head + 1u) % DIRECTOR_PERF_HISTORY;
    if(instance->perf_history_count < DIRECTOR_PERF_HISTORY)
        instance->perf_history_count++;
}

gboolean director_instance_parse_projection_status(DirectorInstance *instance,
                                                   const gchar *text,
                                                   GError **error)
{
    if(!instance || !text) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_INVALID,
                    "Missing viewport projection status");
        return FALSE;
    }

    gint ui_active = 0, startup_active = 0, composite_mode = 0;
    gint columns = 0, rows = 0, selected = 0, point_count = 0;
    gint output_width = 0, output_height = 0;
    gint source_x = 0, source_y = 0, source_width = 0, source_height = 0;
    gint scale = 0, consumed = 0;
    if(sscanf(text,
              "VPM1 %d %d %d %d %d %d %d %d %d %d %d %d %d %d%n",
              &ui_active, &startup_active, &composite_mode,
              &columns, &rows, &selected, &point_count,
              &output_width, &output_height,
              &source_x, &source_y, &source_width, &source_height,
              &scale, &consumed) != 14)
    {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Invalid VPM1 projection status header");
        return FALSE;
    }

    if(columns < 2 || columns > 17 || rows < 2 || rows > 17 ||
       point_count != columns * rows || point_count <= 0 ||
       point_count > DIRECTOR_MAX_PROJECTION_POINTS ||
       selected < 0 || selected > point_count || scale <= 0 ||
       output_width <= 0 || output_height <= 0)
    {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Invalid VPM1 projection dimensions/state");
        return FALSE;
    }

    gdouble points[DIRECTOR_MAX_PROJECTION_POINTS * 2];
    memset(points, 0, sizeof(points));
    const gchar *cursor = text + consumed;
    for(gint i = 0; i < point_count * 2; i++) {
        while(g_ascii_isspace(*cursor))
            cursor++;
        if(!*cursor) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                        "Projection status ended before all mesh points");
            return FALSE;
        }
        gchar *end = NULL;
        errno = 0;
        const gint64 value = g_ascii_strtoll(cursor, &end, 10);
        if(errno != 0 || end == cursor || value < G_MININT || value > G_MAXINT) {
            g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                        "Invalid projection mesh coordinate");
            return FALSE;
        }
        points[i] = (gdouble)value / ((gdouble)scale * 100.0);
        cursor = end;
    }
    while(g_ascii_isspace(*cursor))
        cursor++;
    if(*cursor) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Unexpected trailing projection status data");
        return FALSE;
    }

    instance->live_projection_valid = TRUE;
    instance->live_projection_ui_active = ui_active != 0;
    instance->live_projection_startup_enabled = startup_active != 0;
    instance->live_projection_enabled = composite_mode != 0;
    instance->live_projection_mode = composite_mode;
    instance->live_projection_columns = columns;
    instance->live_projection_rows = rows;
    instance->live_projection_selected_point = selected;
    instance->live_projection_point_count = point_count;
    instance->live_projection_output_width = output_width;
    instance->live_projection_output_height = output_height;
    instance->live_projection_source_x = source_x;
    instance->live_projection_source_y = source_y;
    instance->live_projection_source_width = source_width;
    instance->live_projection_source_height = source_height;
    instance->live_projection_scale = scale;
    memcpy(instance->live_projection_points, points,
           sizeof(gdouble) * (gsize)point_count * 2u);
    director_projection_config_from_live(&instance->configured_projection, instance);
    g_free(instance->last_projection_status);
    instance->last_projection_status = g_strdup(text);
    return TRUE;
}

gboolean director_instance_parse_perf_status(DirectorInstance *instance,
                                             const gchar *text,
                                             GError **error)
{
    GHashTable *values = parse_key_values(text, "VJPERF 1", error);
    if(!values)
        return FALSE;
    parse_unsigned64(g_hash_table_lookup(values, "budget_us"), &instance->budget_us);
    guint64 dropped = instance->dropped;
    guint64 replaced = instance->replaced;
    guint64 stalls = instance->stalls;
    const gboolean have_delivery =
        parse_unsigned64(g_hash_table_lookup(values, "dropped"), &dropped) &&
        parse_unsigned64(g_hash_table_lookup(values, "replaced"), &replaced) &&
        parse_unsigned64(g_hash_table_lookup(values, "stalls"), &stalls);
    if(have_delivery) {
        if(instance->delivery_counters_seen) {
            if(dropped > instance->dropped || replaced > instance->replaced ||
               stalls > instance->stalls)
                instance->delivery_last_issue_us = g_get_monotonic_time();
            else if(dropped < instance->dropped || replaced < instance->replaced ||
                    stalls < instance->stalls)
                instance->delivery_last_issue_us = 0;
        }
        instance->delivery_counters_seen = TRUE;
        instance->dropped = dropped;
        instance->replaced = replaced;
        instance->stalls = stalls;
    }
    g_ptr_array_set_size(instance->stages, 0);

    const gchar *reserved[] = {
        "id", "role", "port", "budget_us", "dropped", "replaced", "stalls", NULL
    };
    GHashTableIter iter;
    gpointer key_ptr;
    gpointer value_ptr;
    g_hash_table_iter_init(&iter, values);
    while(g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
        const gchar *key = key_ptr;
        gboolean skip = FALSE;
        for(gint i = 0; reserved[i]; i++)
            skip |= g_strcmp0(key, reserved[i]) == 0;
        if(skip)
            continue;
        DirectorStageMetric *metric = director_instance_find_stage(instance, key);
        if(!metric) {
            metric = g_new0(DirectorStageMetric, 1);
            g_ptr_array_add(instance->stages, metric);
        }
        if(!parse_stage(key, value_ptr, metric))
            g_ptr_array_remove(instance->stages, metric);
    }
    director_instance_append_perf_history(instance);
    replace_string(&instance->last_perf_status, text);
    g_hash_table_destroy(values);
    return TRUE;
}

gboolean director_instance_parse_routing_status(DirectorInstance *instance,
                                                const gchar *text,
                                                GError **error)
{
    if(!instance) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Missing instance for VJROUTES response");
        return FALSE;
    }
    GHashTable *values = parse_line_key_values(text, "VJROUTES 1", error);
    if(!values)
        return FALSE;

    gint count = 0;
    if(!parse_signed(g_hash_table_lookup(values, "count"), &count) || count < 0 || count > 512) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Incomplete or invalid VJROUTES response");
        g_hash_table_destroy(values);
        return FALSE;
    }

    g_ptr_array_set_size(instance->live_input_routes, 0);
    for(gint i = 0; i < count; i++) {
        gchar *key = g_strdup_printf("route.%d.transport", i);
        const gchar *transport = g_hash_table_lookup(values, key);
        g_free(key);
        if(!transport)
            continue;

        DirectorInputRouteType type = 0;
        if(g_strcmp0(transport, "shm") == 0)
            type = DIRECTOR_INPUT_ROUTE_SHM;
        else if(g_strcmp0(transport, "tcp") == 0)
            type = DIRECTOR_INPUT_ROUTE_TCP;
        else if(g_strcmp0(transport, "ndi") == 0)
            type = DIRECTOR_INPUT_ROUTE_NDI;
        else
            continue;

        gchar *host_key = g_strdup_printf("route.%d.host", i);
        gchar *port_key = g_strdup_printf("route.%d.port", i);
        gchar *source_key = g_strdup_printf("route.%d.source", i);
        const gchar *host = g_hash_table_lookup(values, host_key);
        const gchar *source = g_hash_table_lookup(values, source_key);
        gint port = 0;
        parse_signed(g_hash_table_lookup(values, port_key), &port);
        DirectorInputRoute *route = director_input_route_new(type, NULL,
            host ? host : "", port, source ? source : "");
        g_free(host_key);
        g_free(port_key);
        g_free(source_key);

        gchar *id_key = g_strdup_printf("route.%d.id", i);
        gchar *active_key = g_strdup_printf("route.%d.active", i);
        gchar *current_key = g_strdup_printf("route.%d.current", i);
        gchar *shm_key = g_strdup_printf("route.%d.key", i);
        gint value = 0;
        if(parse_signed(g_hash_table_lookup(values, id_key), &value))
            route->live_stream_id = value;
        value = 0;
        route->live_active = parse_signed(g_hash_table_lookup(values, active_key), &value) && value != 0;
        value = 0;
        route->live_current = parse_signed(g_hash_table_lookup(values, current_key), &value) && value != 0;
        value = 0;
        if(parse_signed(g_hash_table_lookup(values, shm_key), &value))
            route->shm_key = value;
        route->applied_connection = route->live_stream_id > 0 || instance->role == DIRECTOR_ROLE_OUTPUT;
        g_free(id_key);
        g_free(active_key);
        g_free(current_key);
        g_free(shm_key);
        g_ptr_array_add(instance->live_input_routes, route);
    }

#define PARSE_ROUTE_BOOL(name, field) do { gint _v = 0; instance->field = parse_signed(g_hash_table_lookup(values, name), &_v) && _v != 0; } while(0)
#define PARSE_ROUTE_INT(name, field) do { gint _v = 0; if(parse_signed(g_hash_table_lookup(values, name), &_v)) instance->field = _v; else instance->field = 0; } while(0)
    PARSE_ROUTE_BOOL("out.shm.enabled", live_route_shm_output_enabled);
    PARSE_ROUTE_INT("out.shm.key", live_route_shm_output_key);
    PARSE_ROUTE_BOOL("out.tcp.enabled", live_route_tcp_output_enabled);
    PARSE_ROUTE_INT("out.tcp.port", live_route_tcp_output_port);
    PARSE_ROUTE_BOOL("out.ndi.enabled", live_route_ndi_output_enabled);
    replace_string(&instance->live_route_ndi_output_name,
                   g_hash_table_lookup(values, "out.ndi.name"));
    PARSE_ROUTE_BOOL("out.sdl.enabled", live_route_sdl_output_enabled);
    PARSE_ROUTE_BOOL("out.sdl.initialized", live_route_sdl_output_initialized);
    PARSE_ROUTE_BOOL("out.sdl.fullscreen", live_route_sdl_output_fullscreen);
    PARSE_ROUTE_INT("out.sdl.x", live_route_sdl_output_x);
    PARSE_ROUTE_INT("out.sdl.y", live_route_sdl_output_y);
    PARSE_ROUTE_INT("out.sdl.width", live_route_sdl_output_width);
    PARSE_ROUTE_INT("out.sdl.height", live_route_sdl_output_height);
    PARSE_ROUTE_INT("out.sdl.display", live_route_sdl_display_index);
    if(!g_hash_table_contains(values, "out.sdl.display"))
        instance->live_route_sdl_display_index = -1;
#undef PARSE_ROUTE_BOOL
#undef PARSE_ROUTE_INT

    replace_string(&instance->last_routing_status, text);
    g_hash_table_destroy(values);
    return TRUE;
}

gboolean director_instance_parse_ndi_status(DirectorInstance *instance,
                                            const gchar *text,
                                            GError **error)
{
    if(!instance) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Missing instance for VJNDI response");
        return FALSE;
    }

    GHashTable *values = parse_line_key_values(text, "VJNDI 1", error);
    if(!values)
        return FALSE;

    const gchar *runtime = g_hash_table_lookup(values, "runtime");
    const gchar *rx_enabled = g_hash_table_lookup(values, "rx.enabled");
    const gchar *tx_enabled = g_hash_table_lookup(values, "tx.enabled");
    gint rx_flag = 0;
    gint tx_flag = 0;
    if(!runtime || !parse_signed(rx_enabled, &rx_flag) ||
       !parse_signed(tx_enabled, &tx_flag)) {
        g_set_error(error, DIRECTOR_ERROR, DIRECTOR_ERROR_PARSE,
                    "Incomplete VJNDI response");
        g_hash_table_destroy(values);
        return FALSE;
    }

    gint signed_value = 0;
    guint64 counter = 0;
    gdouble drift = 0.0;
    replace_string(&instance->live_ndi_runtime, runtime);
    replace_string(&instance->live_ndi_source,
                   g_hash_table_lookup(values, "rx.source"));
    replace_string(&instance->live_ndi_tx_name,
                   g_hash_table_lookup(values, "tx.name"));
    replace_string(&instance->live_ndi_tx_source,
                   g_hash_table_lookup(values, "tx.source"));
    replace_string(&instance->live_ndi_tx_url,
                   g_hash_table_lookup(values, "tx.url"));
    replace_string(&instance->live_ndi_tx_instance_id,
                   g_hash_table_lookup(values, "tx.instance_id"));
    replace_string(&instance->live_ndi_tx_role,
                   g_hash_table_lookup(values, "tx.role"));
    instance->live_ndi_rx_enabled = rx_flag != 0;
    instance->live_ndi_tx_enabled = tx_flag != 0;
    signed_value = 0;
    instance->live_ndi_tx_owned =
        parse_signed(g_hash_table_lookup(values, "tx.self"), &signed_value) ?
        signed_value != 0 : instance->live_ndi_tx_enabled;

    instance->live_ndi_rx_connected =
        parse_signed(g_hash_table_lookup(values, "rx.connected"), &signed_value) &&
        signed_value > 0;
    signed_value = 0;
    instance->live_ndi_tx_connections =
        parse_signed(g_hash_table_lookup(values, "tx.connections"), &signed_value) ?
        MAX(0, signed_value) : 0;

    instance->live_ndi_rx_video_frames =
        parse_unsigned64(g_hash_table_lookup(values, "rx.video"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_rx_audio_frames =
        parse_unsigned64(g_hash_table_lookup(values, "rx.audio"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_rx_dropped_video_frames =
        parse_unsigned64(g_hash_table_lookup(values, "rx.drop_video"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_rx_dropped_audio_frames =
        parse_unsigned64(g_hash_table_lookup(values, "rx.drop_audio"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_rx_audio_underruns =
        parse_unsigned64(g_hash_table_lookup(values, "rx.audio_underruns"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_tx_video_frames =
        parse_unsigned64(g_hash_table_lookup(values, "tx.video"), &counter) ? counter : 0;
    counter = 0;
    instance->live_ndi_tx_audio_frames =
        parse_unsigned64(g_hash_table_lookup(values, "tx.audio"), &counter) ? counter : 0;

    signed_value = 0;
    instance->live_ndi_clock_available =
        parse_signed(g_hash_table_lookup(values, "rx.clock_available"), &signed_value) &&
        signed_value != 0;
    signed_value = 0;
    instance->live_ndi_clock_age_ms =
        parse_signed(g_hash_table_lookup(values, "rx.clock_age_ms"), &signed_value) ?
        MAX(0, signed_value) : 0;
    instance->live_ndi_clock_drift_ms =
        parse_double_ascii(g_hash_table_lookup(values, "rx.clock_drift_ms"), &drift) ?
        drift : 0.0;

    replace_string(&instance->last_ndi_status, text);
    g_hash_table_destroy(values);
    return TRUE;
}

gchar *director_slice_to_vims(gint index, const DirectorSlice *slice)
{
    if(!slice || index < 0 || index >= DIRECTOR_MAX_SLICES)
        return NULL;
    return g_strdup_printf("286:%d %d %d %d %d %d %d %d %d %d %d %d %d %d;",
                           index,
                           slice->source_x, slice->source_y,
                           slice->source_width, slice->source_height,
                           slice->dest_x, slice->dest_y,
                           slice->dest_width, slice->dest_height,
                           slice->blend_left, slice->blend_right,
                           slice->blend_top, slice->blend_bottom,
                           slice->blend_gamma);
}
