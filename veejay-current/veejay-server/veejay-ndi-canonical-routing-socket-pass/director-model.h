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
#ifndef VEEJAY_DIRECTOR_MODEL_H
#define VEEJAY_DIRECTOR_MODEL_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define DIRECTOR_MAX_SLICES 8
#define DIRECTOR_STAGE_NAME_MAX 48
#define DIRECTOR_PERF_HISTORY 240
#define DIRECTOR_MAX_PROJECTION_POINTS (17 * 17)
#define DIRECTOR_V4L_CONTROL_COUNT 21
#define DIRECTOR_CAMERA_MAP_INVALID 0xffffffffu
#define DIRECTOR_NDI_SOURCE_NAME_MAX 253

typedef enum {
    DIRECTOR_ROLE_STANDALONE = 0,
    DIRECTOR_ROLE_PROGRAM = 1,
    DIRECTOR_ROLE_OUTPUT = 2
} DirectorRole;

typedef enum {
    DIRECTOR_STARTUP_BLANK = 0,
    DIRECTOR_STARTUP_MEDIA = 1
} DirectorStartupMode;

typedef enum {
    DIRECTOR_CONTROL_INDEPENDENT = 0,
    DIRECTOR_CONTROL_MASTER = 1,
    DIRECTOR_CONTROL_PREVIEW = 2
} DirectorControlMode;

typedef struct {
    gboolean enabled;
    gint source_x;
    gint source_y;
    gint source_width;
    gint source_height;
    gint dest_x;
    gint dest_y;
    gint dest_width;
    gint dest_height;
    gint blend_left;
    gint blend_right;
    gint blend_top;
    gint blend_bottom;
    gint blend_gamma;
} DirectorSlice;

typedef struct {
    gchar name[DIRECTOR_STAGE_NAME_MAX];
    guint64 count;
    guint64 avg_us;
    guint64 p95_us;
    guint64 max_us;
    guint64 over_budget;
    guint64 recent_us;
} DirectorStageMetric;

typedef struct {
    gdouble producer_ms;
    gdouble renderer_work_ms;
    gdouble output_graph_ms;
} DirectorPerfHistorySample;

typedef struct {
    gboolean valid;
    gboolean enabled;
    gboolean startup_enabled;
    gint mode;
    gint columns;
    gint rows;
    gint selected_point;
    gint point_count;
    gint output_width;
    gint output_height;
    gint source_x;
    gint source_y;
    gint source_width;
    gint source_height;
    gint scale;
    gdouble points[DIRECTOR_MAX_PROJECTION_POINTS * 2];
} DirectorProjectionConfig;

typedef struct {
    gchar *instance_id;
    gint output_width;
    gint output_height;
    gchar *display_id;
    gchar *display_name;
    gchar *display_connector;
    gint display_index;
    gint display_x;
    gint display_y;
    gint display_width;
    gint display_height;
    gboolean fullscreen;
    gboolean borderless;
    gint window_width;
    gint window_height;
    gint window_x;
    gint window_y;
    gchar *split_master_instance_id;
    gint split_row;
    gint split_column;
    gint graph_width;
    gint graph_height;
    DirectorSlice slices[DIRECTOR_MAX_SLICES];
    DirectorProjectionConfig projection;
} DirectorVenueOutput;

typedef struct {
    gchar *instance_id;
    gint camera_width;
    gint camera_height;
    gint projector_width;
    gint projector_height;
    GBytes *camera_to_projector;
    guint illuminated_pixels;
    guint valid_pixels;
    gdouble valid_fraction;
    gdouble decoded_fraction;
    gdouble mean_confidence;
    gint64 updated_real_us;
} DirectorProjectorCameraMap;

typedef enum {
    DIRECTOR_INPUT_ROUTE_SHM = 1,
    DIRECTOR_INPUT_ROUTE_TCP = 2,
    DIRECTOR_INPUT_ROUTE_NDI = 3
} DirectorInputRouteType;

typedef struct {
    DirectorInputRouteType type;
    gchar *source_instance_id;
    gchar *host;
    gint port;
    gint shm_key;
    gchar *ndi_source_name;
    gboolean applied_connection;
    gint live_stream_id;
    gboolean live_active;
    gboolean live_current;
} DirectorInputRoute;

typedef struct {
    gchar *name;
    GPtrArray *outputs;
    gchar *calibration_camera_id;
    gchar *calibration_camera_path;
    gchar *calibration_camera_name;
    gboolean calibration_controls_valid;
    gint calibration_controls[DIRECTOR_V4L_CONTROL_COUNT];
    gint64 calibration_updated_real_us;
    GPtrArray *camera_maps;
} DirectorVenueProfile;

typedef struct {
    gchar *instance_id;
    gboolean should_run;
    gchar *source_instance_id;
    gchar *source_host;
    gint source_port;
    GPtrArray *input_routes;
    DirectorControlMode control_mode;
    gchar *master_instance_id;
    gchar *master_host;
    gint master_port;
    gboolean preview_forward_vims;
    gboolean preview_sync_samplelist;
    gboolean preview_headless;
    gint pattern;
    gboolean audio_enabled;
    gboolean audio_muted;
    gboolean ndi_input_enabled;
    gchar *ndi_source_name;
    gboolean ndi_output_enabled;
    gchar *ndi_output_name;
    gboolean ndi_tally_enabled;
    gboolean ndi_follow_clock;
    DirectorVenueOutput physical;
} DirectorSnapshotInstance;

typedef struct {
    gchar *name;
    GPtrArray *instances;
} DirectorShowSnapshot;

typedef struct _DirectorClient DirectorClient;

typedef struct {
    gchar *id;
    DirectorRole role;
    gchar *host;
    gint port;
    gint output_width;
    gint output_height;
    gint input_width;
    gint input_height;
    DirectorStartupMode startup_mode;
    GPtrArray *media_files;
    gdouble fps;
    gint norm;
    gint output_driver;
    gchar *output_file;
    gint yuv_mode;
    gboolean sync_correction;
    gboolean audio_enabled;
    gboolean audio_muted;
    gboolean audio_sync_thread;
    gboolean audio_beat_thread;
    gboolean auto_loop;
    gboolean clip_as_sample;
    gboolean deinterlace;
    gboolean legacy_viewport;
    gboolean borderless;
    gboolean fullscreen;
    gboolean no_keyboard;
    gboolean no_mouse;
    gboolean show_cursor;
    gboolean verbose;
    gboolean no_color;
    gint window_width;
    gint window_height;
    gint window_x;
    gint window_y;
    gint memory_percent;
    gint max_cache;
    gint timer_mode;
    gint pace_correction_ms;
    gint audio_rate;
    gint audio_channels;
    gint audio_bits;
    gboolean ndi_input_enabled;
    gchar *ndi_source_name;
    gboolean ndi_output_enabled;
    gchar *ndi_output_name;
    gboolean ndi_tally_enabled;
    gboolean ndi_follow_clock;
    gint scene_detection;
    gint capture_device;
    gint generator_stream;
    gboolean swap_range;
    gboolean dynamic_fx_chain;
    gboolean fx_custom_defaults;
    gboolean preserve_pathnames;
    gboolean bezerk;
    gchar *split_screen_file;
    gchar *split_master_instance_id;
    gint split_row;
    gint split_column;
    gchar *multicast_osc;
    gchar *multicast_vims;
    gchar *sample_file;
    gchar *action_file;
    gchar *working_directory;
    gchar *source_instance_id;
    gchar *source_host;
    gint source_port;
    GPtrArray *input_routes;
    GPtrArray *live_input_routes;
    gchar *stream_advertise_host;
    DirectorControlMode control_mode;
    gchar *master_instance_id;
    gchar *master_host;
    gint master_port;
    gboolean preview_forward_vims;
    gboolean preview_sync_samplelist;
    gboolean preview_headless;
    gchar *display_id;
    gchar *display_name;
    gchar *display_connector;
    gint display_index;
    gint display_x;
    gint display_y;
    gint display_width;
    gint display_height;
    gint stage_x;
    gint stage_y;
    gboolean stage_position_explicit;
    gint wiring_x;
    gint wiring_y;
    gboolean wiring_position_explicit;
    gchar *executable;
    gchar *extra_args;
    gboolean eidolon_enabled;
    gchar *eidolon_executable;
    gchar *eidolon_extra_args;
    gboolean managed;
    gboolean autostart;
    gboolean apply_on_connect;
    gboolean discovered_transient;
    gboolean calibration_camera;
    gboolean recovery_restart_engine;
    gboolean recovery_reconnect_route;
    gboolean recovery_restore_projection;
    gboolean recovery_restore_mapping;
    gboolean recovery_restore_control;
    gint recovery_retry_limit;

    gboolean connected;
    gboolean backend_ready;
    gboolean graph_applied_connection;
    gboolean control_applied_connection;
    gboolean samplelist_synced_connection;
    gboolean wire_applied_connection;
    gboolean process_running;
    gboolean eidolon_running;
    gboolean eidolon_start_failed;
    gboolean eidolon_autostart_blocked;
    gint live_role;
    gint live_port;
    gchar *live_id;
    gchar *live_source;
    gint shm_key;
    gboolean shm_enabled;
    gint wire_source_shm_key;
    guint64 source_sequence;

    gchar *live_ndi_runtime;
    gchar *live_ndi_source;
    gchar *live_ndi_tx_name;
    gchar *live_ndi_tx_source;
    gchar *live_ndi_tx_url;
    gchar *live_ndi_tx_instance_id;
    gchar *live_ndi_tx_role;
    gboolean live_ndi_rx_enabled;
    gboolean live_ndi_rx_connected;
    gboolean live_ndi_tx_enabled;
    gboolean live_ndi_tx_owned;
    gboolean ndi_input_change_pending;
    gboolean ndi_input_pending_enabled;
    gchar *ndi_input_pending_name;
    gchar *ndi_input_pending_native_source_id;
    gchar *ndi_input_pending_native_host;
    gint ndi_input_pending_native_port;
    gboolean ndi_output_change_pending;
    gboolean ndi_output_pending_enabled;
    gchar *ndi_output_pending_name;
    gint live_ndi_tx_connections;
    guint64 live_ndi_rx_video_frames;
    guint64 live_ndi_rx_audio_frames;
    guint64 live_ndi_rx_dropped_video_frames;
    guint64 live_ndi_rx_dropped_audio_frames;
    guint64 live_ndi_rx_audio_underruns;
    guint64 live_ndi_tx_video_frames;
    guint64 live_ndi_tx_audio_frames;
    gboolean live_ndi_clock_available;
    gint live_ndi_clock_age_ms;
    gdouble live_ndi_clock_drift_ms;

    gboolean live_route_shm_output_enabled;
    gint live_route_shm_output_key;
    gboolean live_route_tcp_output_enabled;
    gint live_route_tcp_output_port;
    gboolean live_route_ndi_output_enabled;
    gchar *live_route_ndi_output_name;
    gboolean live_route_sdl_output_enabled;
    gboolean live_route_sdl_output_initialized;
    gboolean live_route_sdl_output_fullscreen;
    gint live_route_sdl_output_x;
    gint live_route_sdl_output_y;
    gint live_route_sdl_output_width;
    gint live_route_sdl_output_height;
    gint live_route_sdl_display_index;

    gint graph_width;
    gint graph_height;
    gint pattern;
    DirectorSlice slices[DIRECTOR_MAX_SLICES];

    gint live_graph_width;
    gint live_graph_height;
    gint live_pattern;
    DirectorSlice live_slices[DIRECTOR_MAX_SLICES];
    gboolean live_slice_enabled[DIRECTOR_MAX_SLICES];

    gboolean live_projection_valid;
    gboolean live_projection_ui_active;
    gboolean live_projection_enabled;
    gboolean live_projection_startup_enabled;
    gint live_projection_mode;
    gint live_projection_columns;
    gint live_projection_rows;
    gint live_projection_selected_point;
    gint live_projection_point_count;
    gint live_projection_output_width;
    gint live_projection_output_height;
    gint live_projection_source_x;
    gint live_projection_source_y;
    gint live_projection_source_width;
    gint live_projection_source_height;
    gint live_projection_scale;
    gdouble live_projection_points[DIRECTOR_MAX_PROJECTION_POINTS * 2];
    DirectorProjectionConfig configured_projection;

    guint64 budget_us;
    guint64 dropped;
    guint64 replaced;
    guint64 stalls;
    gboolean delivery_counters_seen;
    gint64 delivery_last_issue_us;
    GPtrArray *stages;
    DirectorPerfHistorySample perf_history[DIRECTOR_PERF_HISTORY];
    guint perf_history_head;
    guint perf_history_count;

    gchar *last_error;
    gchar *last_instance_status;
    gchar *last_output_status;
    gchar *last_projection_status;
    gchar *last_perf_status;
    gchar *last_ndi_status;
    gchar *last_routing_status;

    GSubprocess *process;
    guint force_stop_timer;
    GSubprocess *eidolon_process;
    GSubprocess *reloaded_process;
    gboolean reloaded_running;
    guint reloaded_force_stop_timer;
    gint eidolon_pty_fd;
    GIOChannel *eidolon_pty_channel;
    guint eidolon_io_watch;
    guint eidolon_force_stop_timer;
    guint recovery_timer;
    guint restart_timer;
    gint recovery_attempts;
    gint64 recovery_started_us;
    gboolean recovery_active;
    gboolean recovery_stop_requested;
    gint process_pid;
    gint64 process_started_real_us;
    gint recovery_source_pid;
    gchar *recovery_editlist_path;
    gchar *recovery_samplelist_path;
    gboolean restart_pending;
    gboolean physical_restore_pending;
    gdouble live_latency_ms;
    gboolean live_v4l_valid;
    gint live_v4l_stream_id;
    gint live_v4l_controls[DIRECTOR_V4L_CONTROL_COUNT];
    gboolean calibration_device_query_pending;
    gboolean calibration_controls_restored_connection;
    GString *eidolon_transcript;
    GPtrArray *eidolon_history;
    gint eidolon_history_cursor;
    DirectorClient *client;
} DirectorInstance;

typedef struct {
    gchar *name;
    gchar *path;
    gboolean dirty;
    gboolean launch_reloaded;
    gchar *reloaded_executable;
    gchar *reloaded_args;
    gchar *active_venue;
    GPtrArray *venue_profiles;
    GPtrArray *snapshots;
    GPtrArray *ndi_patch_positions;
    GPtrArray *instances;
} DirectorShow;

const gchar *director_role_name(DirectorRole role);
const gchar *director_startup_mode_name(DirectorStartupMode mode);
DirectorStartupMode director_startup_mode_from_string(const gchar *name);
const gchar *director_control_mode_name(DirectorControlMode mode);
DirectorControlMode director_control_mode_from_string(const gchar *name);
gboolean director_instance_id_valid(const gchar *id);
DirectorRole director_role_from_string(const gchar *name);

void director_slice_set_identity(DirectorSlice *slice, gint width, gint height);
DirectorInstance *director_instance_new(const gchar *id, DirectorRole role);
DirectorInstance *director_instance_clone_configuration(const DirectorInstance *source,
                                                        const gchar *id);
void director_instance_free(DirectorInstance *instance);
DirectorShow *director_show_new(const gchar *name);
void director_show_free(DirectorShow *show);
DirectorInstance *director_show_find_instance(DirectorShow *show, const gchar *id);
DirectorInstance *director_show_find_source_for_instance(DirectorShow *show,
                                                         const DirectorInstance *target);
gboolean director_show_add_instance(DirectorShow *show,
                                    DirectorInstance *instance,
                                    GError **error);
void director_show_remove_instance(DirectorShow *show, DirectorInstance *instance);

void director_projection_config_from_live(DirectorProjectionConfig *config,
                                         const DirectorInstance *instance);
void director_projection_config_to_instance(DirectorInstance *instance,
                                           const DirectorProjectionConfig *config);
DirectorVenueProfile *director_show_find_venue(DirectorShow *show, const gchar *name);
DirectorProjectorCameraMap *director_venue_find_camera_map(DirectorVenueProfile *profile,
                                                                  const gchar *instance_id);
gboolean director_venue_store_camera_map(DirectorVenueProfile *profile,
                                         const gchar *instance_id,
                                         gint camera_width, gint camera_height,
                                         gint projector_width, gint projector_height,
                                         const guint32 *camera_to_projector,
                                         gsize pixel_count,
                                         guint illuminated_pixels,
                                         guint valid_pixels,
                                         gdouble mean_confidence);
gboolean director_venue_remove_camera_map(DirectorVenueProfile *profile,
                                          const gchar *instance_id);
gboolean director_projector_camera_map_lookup(const DirectorProjectorCameraMap *map,
                                              gint camera_x, gint camera_y,
                                              gint *projector_x, gint *projector_y);
DirectorVenueProfile *director_show_capture_venue(DirectorShow *show, const gchar *name);
guint director_show_apply_venue(DirectorShow *show, const DirectorVenueProfile *profile);
gboolean director_show_remove_venue(DirectorShow *show, const gchar *name);
gboolean director_show_get_ndi_patch_position(const DirectorShow *show,
                                               const gchar *key,
                                               gint *x, gint *y);
void director_show_set_ndi_patch_position(DirectorShow *show,
                                          const gchar *key,
                                          gint x, gint y);
void director_show_clear_ndi_patch_positions(DirectorShow *show);
DirectorShowSnapshot *director_show_find_snapshot(DirectorShow *show, const gchar *name);
DirectorShowSnapshot *director_show_capture_snapshot(DirectorShow *show, const gchar *name);
guint director_show_apply_snapshot_model(DirectorShow *show, const DirectorShowSnapshot *snapshot);
gboolean director_show_remove_snapshot(DirectorShow *show, const gchar *name);

gboolean director_show_save(DirectorShow *show, const gchar *path, GError **error);
DirectorShow *director_show_load(const gchar *path, GError **error);

gchar **director_instance_build_argv(const DirectorShow *show,
                                     const DirectorInstance *instance,
                                     GError **error);
gboolean director_instance_is_local(const DirectorInstance *instance);

void director_instance_clear_live_metrics(DirectorInstance *instance);
gboolean director_instance_parse_instance_status(DirectorInstance *instance,
                                                 const gchar *text,
                                                 GError **error);
gboolean director_instance_parse_output_status(DirectorInstance *instance,
                                               const gchar *text,
                                               GError **error);
gboolean director_instance_parse_projection_status(DirectorInstance *instance,
                                                   const gchar *text,
                                                   GError **error);
gboolean director_instance_parse_perf_status(DirectorInstance *instance,
                                             const gchar *text,
                                             GError **error);
gboolean director_instance_parse_ndi_status(DirectorInstance *instance,
                                            const gchar *text,
                                            GError **error);
gboolean director_instance_parse_routing_status(DirectorInstance *instance,
                                                const gchar *text,
                                                GError **error);
DirectorStageMetric *director_instance_find_stage(DirectorInstance *instance,
                                                  const gchar *name);

gchar *director_slice_to_vims(gint index, const DirectorSlice *slice);


DirectorInputRoute *director_input_route_new(DirectorInputRouteType type,
                                              const gchar *source_instance_id,
                                              const gchar *host,
                                              gint port,
                                              const gchar *ndi_source_name);
DirectorInputRoute *director_input_route_copy(const DirectorInputRoute *route);
void director_input_route_free(DirectorInputRoute *route);
DirectorInputRoute *director_instance_find_input_route(const DirectorInstance *instance,
                                                        DirectorInputRouteType type,
                                                        const gchar *source_instance_id,
                                                        const gchar *host,
                                                        gint port,
                                                        const gchar *ndi_source_name);
gboolean director_instance_add_input_route(DirectorInstance *instance,
                                            DirectorInputRouteType type,
                                            const gchar *source_instance_id,
                                            const gchar *host,
                                            gint port,
                                            const gchar *ndi_source_name);
gboolean director_instance_remove_input_route(DirectorInstance *instance,
                                               DirectorInputRouteType type,
                                               const gchar *source_instance_id,
                                               const gchar *host,
                                               gint port,
                                               const gchar *ndi_source_name);
void director_instance_clear_input_routes(DirectorInstance *instance);

G_END_DECLS

#endif
