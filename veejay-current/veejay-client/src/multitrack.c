/* Gveejay Reloaded - graphical interface for VeeJay
 * Multi-instance timeline controller
 *      (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
 *      (C) 2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <gtk/gtk.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <src/vj-api.h>
#include <src/common.h>
#include <src/utils.h>
#include "sequence.h"
#include "multitrack.h"
#include "gtkmultitrackedit.h"

#ifndef STREAM_BUFFER_POSITION
#define STREAM_BUFFER_POSITION FRAME_NUM
#endif
#ifndef STREAM_BUFFER_CAPACITY
#define STREAM_BUFFER_CAPACITY TOTAL_FRAMES
#endif

static int seq_stream_buffer_supported_status(const int *status)
{
    return status &&
           status[PLAY_MODE] == MODE_STREAM &&
           status[CURRENT_ID] > 0 &&
           status[STREAM_BUFFER_STATE] != STREAM_BUFFER_STATE_UNSUPPORTED;
}

static int seq_stream_buffer_ready_status(const int *status)
{
    return seq_stream_buffer_supported_status(status) &&
           status[STREAM_BUFFER_ENABLED] > 0 &&
           status[STREAM_BUFFER_FILLED] > 0;
}

static int multitrack_mode_is_sample(int play_mode)
{
    return play_mode == MODE_SAMPLE || play_mode == MODE_PATTERN;
}

static int multitrack_mode_has_source_timeline(int play_mode)
{
    return multitrack_mode_is_sample(play_mode) || play_mode == MODE_STREAM;
}

static const char *multitrack_seek_unavailable_reason(const int *status)
{
    if(!status)
        return "no playback status";

    if(status[SEQ_ACT])
        return NULL;

    if(status[CURRENT_ID] <= 0)
        return "no active playback source";

    if(multitrack_mode_is_sample(status[PLAY_MODE]))
        return NULL;

    if(status[PLAY_MODE] == MODE_STREAM) {
        if(status[STREAM_BUFFER_STATE] == STREAM_BUFFER_STATE_UNSUPPORTED)
            return "live stream has no seek buffer";
        if(status[STREAM_BUFFER_CAPACITY] <= 0)
            return "set a stream buffer length";
        if(status[STREAM_BUFFER_ENABLED] <= 0)
            return "stream buffer is disabled";
        if(status[STREAM_BUFFER_FILLED] <= 0)
            return "stream buffer is still filling";
        return NULL;
    }

    if(status[PLAY_MODE] == MODE_PLAIN)
        return "plain EDL uses the edit timeline";

    return "playback mode is not seekable";
}

static int multitrack_status_seekable(const int *status)
{
    return multitrack_seek_unavailable_reason(status) == NULL;
}

static int multitrack_loop_repeats(int loop_type)
{
    return loop_type == 1 || loop_type == 2 || loop_type == 3;
}

static int MAX_TRACKS = 4;

typedef struct {
    void *preview;
    GtkWidget *main_window;
    GtkWidget *main_box;
    GtkWidget *status_bar;
    GtkWidget *timeline;
    void *data;

    int selected;
    int current_ui_track;
    int pending_ui_track;
    int project_master_track;
    int sensitive;

    float fps;
    float aspect_ratio;
    int width;
    int height;
    int preview_width;
    int preview_height;
    int quality;

    GdkPixbuf *logo;

    int track_status[__MAX_TRACKS];
    int preview_enabled[__MAX_TRACKS];
    int status_lock[__MAX_TRACKS];
    int status_cache[__MAX_TRACKS][VJ_STATUS_ARRAY_SIZE];
    int sync_frame[__MAX_TRACKS];
    int sync_origin[__MAX_TRACKS];
    int sync_total[__MAX_TRACKS];
    int sync_speed[__MAX_TRACKS];
    int sync_play_mode[__MAX_TRACKS];
    int sync_fps_x100[__MAX_TRACKS];
    gint64 sync_status_us[__MAX_TRACKS];

    int timeline_bank;
    unsigned int timeline_revision;
    int track_timeline_bank[__MAX_TRACKS];
    unsigned int track_timeline_revision[__MAX_TRACKS];
    int track_timeline_source_fps_x100[__MAX_TRACKS];
    int track_timeline_master_fps_x100[__MAX_TRACKS];
    int track_timeline_master_total[__MAX_TRACKS];
    int track_timeline_clips_active[__MAX_TRACKS];
    int track_timeline_play_mode[__MAX_TRACKS];
    int track_timeline_source_id[__MAX_TRACKS];
    int track_timeline_source_total[__MAX_TRACKS];
    int track_timeline_speed[__MAX_TRACKS];
    int track_timeline_loop_type[__MAX_TRACKS];
    gvr_sequence_timeline_t track_timeline[__MAX_TRACKS];
    int bus_a_track;
    int bus_b_track;
    int active_bus;
    int transition_pending;
    int transition_pending_duration;
    int transition_pending_method;
    int transition_pending_shape;
    int transition_pending_target_track;
    gint64 transition_pending_started_us;
    gint64 switcher_input_last_bind_us[__MAX_TRACKS];
    gint64 switcher_track_list_request_us;
    int drift_lock_enabled;
    int drift_frames[__MAX_TRACKS];
    int drift_correcting[__MAX_TRACKS];
    int drift_last_source[__MAX_TRACKS];
    int drift_last_mode[__MAX_TRACKS];
    int drift_last_origin[__MAX_TRACKS];
    int drift_last_total[__MAX_TRACKS];
    int drift_previous_frame[__MAX_TRACKS];
    int drift_previous_total[__MAX_TRACKS];
    int drift_previous_speed[__MAX_TRACKS];
    gint64 drift_previous_status_us[__MAX_TRACKS];
    gint64 drift_source_changed_us[__MAX_TRACKS];
    gint64 drift_last_correction_us[__MAX_TRACKS];
    int drift_error_sign[__MAX_TRACKS];
    int drift_error_samples[__MAX_TRACKS];
    int drift_phase_valid[__MAX_TRACKS];
    long double drift_phase_offset_seconds[__MAX_TRACKS];
    long double drift_phase_duration_seconds[__MAX_TRACKS];
    long double drift_last_observed_phase_seconds[__MAX_TRACKS];
    gint64 drift_last_observed_status_us[__MAX_TRACKS];
    int timeline_total_frames;
    multitrack_master_clip_t master_clips[120];
    unsigned int master_clip_count;
} multitracker_t;

static char *mt_new_connection_dialog(multitracker_t *mt,
                                      int *port_num,
                                      int *error);
static void multitrack_refresh_track(multitracker_t *mt, int track);
static void multitrack_refresh_all_tracks(multitracker_t *mt);
static void multitrack_refresh_connection_topology(multitracker_t *mt);
static int multitrack_first_open(multitracker_t *mt, int except);
static void multitrack_set_current_track(multitracker_t *mt,
                                         int track,
                                         int reconnect_main_ui);
static void multitrack_preview_configure(multitracker_t *mt, int track);
static void multitrack_transition_prepare(multitracker_t *mt,
                                          int duration,
                                          int method,
                                          int shape,
                                          int target_track);
static void multitrack_switcher_prepare_inputs(multitracker_t *mt,
                                               int force);
static void multitrack_drift_update_internal(multitracker_t *mt);
static void multitrack_update_track_timeline(multitracker_t *mt,
                                             int track,
                                             const int *status,
                                             int sequence_bank);

static void multitrack_sync_main_preview_toggle(multitracker_t *mt,
                                                int enabled)
{
    if(!mt)
        return;

    vj_gui_sync_preview_toggle(enabled);
}

static void status_print(multitracker_t *mt, const char format[], ...)
{
    char buf[1024];
    va_list args;
    gchar *text;
    gsize nr;
    gsize nw;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    text = g_locale_to_utf8(buf, -1, &nr, &nw, NULL);
    if(!text)
        return;

    gsize len = strlen(text);
    if(len > 0 && text[len - 1] == '\n')
        text[len - 1] = '\0';

    if(mt && mt->status_bar) {
        if(GTK_IS_STATUSBAR(mt->status_bar))
            gtk_statusbar_push(GTK_STATUSBAR(mt->status_bar), 0, text);
        else if(GTK_IS_LABEL(mt->status_bar))
            gtk_label_set_text(GTK_LABEL(mt->status_bar), text);
    }

    g_free(text);
}

static int multitrack_first_other(multitracker_t *mt, int exclude_track)
{
    if(!mt)
        return -1;

    for(int track = 0; track < MAX_TRACKS; track++)
        if(track != exclude_track && gvr_track_test(mt->preview, track))
            return track;
    return -1;
}

static int multitrack_track_for_stream(multitracker_t *mt, int stream_id)
{
    int master;

    if(!mt)
        return -1;

    master = mt->project_master_track;
    if(stream_id <= 0)
        return master;

    for(int track = 0; track < MAX_TRACKS; track++) {
        if(track == master || !gvr_track_test(mt->preview, track))
            continue;
        if(gvr_get_stream_id_for(mt->preview, master, track) == stream_id)
            return track;
    }

    return -1;
}

static void multitrack_switcher_prepare_inputs(multitracker_t *mt,
                                               int force)
{
    gint64 now;
    int master;
    int missing = 0;

    if(!mt)
        return;

    master = mt->project_master_track;
    if(master < 0 || !gvr_track_test(mt->preview, master))
        return;

    now = g_get_monotonic_time();
    for(int track = 0; track < MAX_TRACKS; track++) {
        if(track == master || !gvr_track_test(mt->preview, track))
            continue;

        if(gvr_get_stream_id_for(mt->preview, master, track) > 0) {
            mt->switcher_input_last_bind_us[track] = 0;
            continue;
        }

        missing = 1;
        if(force || mt->switcher_input_last_bind_us[track] == 0 ||
           now - mt->switcher_input_last_bind_us[track] >= 1000000) {
            multitrack_bind_track(mt, master, track);
            mt->switcher_input_last_bind_us[track] = now;
        }
    }

    if(missing &&
       (force || mt->switcher_track_list_request_us == 0 ||
        now - mt->switcher_track_list_request_us >= 200000)) {
        gvr_need_track_list(mt->preview, master);
        mt->switcher_track_list_request_us = now;
    }
}

static void multitrack_switcher_sync_ui(multitracker_t *mt)
{
    int master;

    if(!mt || !mt->timeline)
        return;

    master = mt->project_master_track;
    if(master < 0 || !gvr_track_test(mt->preview, master))
        master = multitrack_first_open(mt, -1);

    mt->bus_a_track = master;
    if(mt->bus_b_track < 0 ||
       mt->bus_b_track == master ||
       !gvr_track_test(mt->preview, mt->bus_b_track))
        mt->bus_b_track = multitrack_first_other(mt, master);

    if(mt->active_bus == 1 &&
       (mt->bus_b_track < 0 ||
        !gvr_track_test(mt->preview, mt->bus_b_track)))
        mt->active_bus = 0;
    else if(mt->active_bus != 1)
        mt->active_bus = 0;

    gvr_multi_track_edit_set_transition_buses(mt->timeline,
                                               mt->bus_a_track,
                                               mt->bus_b_track,
                                               mt->active_bus);
}

static const char *multitrack_transition_method_name(int method)
{
    return method == VJ_MULTITRACK_TRANSITION_SHAPE_WIPE ?
           "Shape Wipe" : "Dissolve";
}

static void multitrack_transition_send(multitracker_t *mt,
                                       int target_track,
                                       int stream_id,
                                       int duration,
                                       int method,
                                       int shape)
{
    int master;

    if(!mt)
        return;

    master = mt->project_master_track;
    if(master < 0 || target_track < 0 ||
       !gvr_track_test(mt->preview, master) ||
       !gvr_track_test(mt->preview, target_track))
        return;

    if(method != VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        method = VJ_MULTITRACK_TRANSITION_DISSOLVE;

    mt->transition_pending = 0;
    mt->transition_pending_target_track = -1;
    vj_gui_vims_observe_external(VIMS_VIDEO_TRANSITION_TAKE,
                                  "%d %d %d %d",
                                  stream_id,
                                  duration,
                                  method,
                                  shape);
    gvr_queue_mmmmvims(mt->preview,
                       master,
                       VIMS_VIDEO_TRANSITION_TAKE,
                       stream_id,
                       duration,
                       method,
                       shape);
    if(duration == 0)
        status_print(mt,
                     target_track == master ?
                         "Cutting back to Video %d / Bus A" :
                         "Cutting to Video %d / Bus B",
                     target_track + 1);
    else if(method == VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        status_print(mt,
                     target_track == master ?
                         "Shape-wiping back to Video %d / Bus A with shape %d over %d rendered frames" :
                         "Shape-wiping Video %d to Bus B with shape %d over %d rendered frames",
                     target_track + 1,
                     shape,
                     duration);
    else
        status_print(mt,
                     target_track == master ?
                         "Taking back to Video %d / Bus A over %d rendered frames" :
                         "Taking Video %d to Bus B over %d rendered frames",
                     target_track + 1,
                     duration);
}

static void multitrack_transition_prepare(multitracker_t *mt,
                                          int duration,
                                          int method,
                                          int shape,
                                          int target_track)
{
    gint64 now;
    int stream_id;
    int master;

    if(!mt)
        return;

    master = mt->project_master_track;
    if(master < 0 || target_track < 0 || target_track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, master) ||
       !gvr_track_test(mt->preview, target_track)) {
        status_print(mt,
                     "A/B switching requires a connected Video 1 master and B source");
        return;
    }

    duration = CLAMP(duration, 0, 36000);
    if(method != VJ_MULTITRACK_TRANSITION_SHAPE_WIPE)
        method = VJ_MULTITRACK_TRANSITION_DISSOLVE;

    if(target_track == master) {
        multitrack_transition_send(mt,
                                   target_track,
                                   0,
                                   duration,
                                   method,
                                   shape);
        return;
    }

    stream_id = gvr_get_stream_id_for(mt->preview, master, target_track);
    if(stream_id > 0) {
        multitrack_transition_send(mt,
                                   target_track,
                                   stream_id,
                                   duration,
                                   method,
                                   shape);
        return;
    }

    now = g_get_monotonic_time();
    mt->transition_pending = 1;
    mt->transition_pending_duration = duration;
    mt->transition_pending_method = method;
    mt->transition_pending_shape = shape;
    mt->transition_pending_target_track = target_track;
    mt->transition_pending_started_us = now;
    multitrack_switcher_prepare_inputs(mt, 1);
    status_print(mt,
                 "Preparing the Video %d unicast input on Video 1 for %s",
                 target_track + 1,
                 multitrack_transition_method_name(method));
}

static void multitrack_transition_pending_tick(multitracker_t *mt)
{
    gint64 now;
    int stream_id;
    int master;
    int target_track;

    if(!mt || !mt->transition_pending)
        return;

    master = mt->project_master_track;
    target_track = mt->transition_pending_target_track;
    if(master < 0 || target_track < 0 || target_track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, master) ||
       !gvr_track_test(mt->preview, target_track)) {
        mt->transition_pending = 0;
        mt->transition_pending_target_track = -1;
        return;
    }

    now = g_get_monotonic_time();
    stream_id = gvr_get_stream_id_for(mt->preview, master, target_track);
    if(stream_id > 0) {
        multitrack_transition_send(mt,
                                   target_track,
                                   stream_id,
                                   mt->transition_pending_duration,
                                   mt->transition_pending_method,
                                   mt->transition_pending_shape);
        return;
    }

    if(now - mt->transition_pending_started_us > 5000000) {
        mt->transition_pending = 0;
        mt->transition_pending_target_track = -1;
        status_print(mt,
                     "Unable to resolve the Video %d unicast input on Video 1 within five seconds",
                     target_track + 1);
        return;
    }

    multitrack_switcher_prepare_inputs(mt, 0);
}

static int multitrack_track_fps_x100(const multitracker_t *mt, int track)
{
    int fps_x100;

    if(!mt || track < 0 || track >= MAX_TRACKS)
        return 2500;

    fps_x100 = mt->sync_fps_x100[track];
    if(fps_x100 <= 0)
        fps_x100 = (int)floor((double)mt->fps * 100.0 + 0.5);
    return fps_x100 > 0 ? fps_x100 : 2500;
}

static long double multitrack_frame_period_seconds(int fps_x100)
{
    return 100.0L / (long double)MAX(1, fps_x100);
}

static int multitrack_rescale_frame_count(long long frames,
                                          int source_fps_x100,
                                          int target_fps_x100)
{
    long double value;

    if(frames <= 0)
        return 0;

    value = (long double)frames *
            (long double)MAX(1, target_fps_x100) /
            (long double)MAX(1, source_fps_x100);
    if(value >= (long double)INT_MAX)
        return INT_MAX;
    return MAX(1, (int)llroundl(value));
}

static long double multitrack_position_seconds(int frame, int fps_x100)
{
    return (long double)MAX(0, frame) *
           multitrack_frame_period_seconds(fps_x100);
}

typedef struct {
    int valid;
    int slot;
    int project_frame;
    int project_total;
    int clip_in;
    int clip_out;
    int local_frame;
    int local_total;
    int fps_x100;
    long double position_seconds;
    long double duration_seconds;
} multitrack_sequence_clock_t;

static int multitrack_master_clip_for_status(const multitracker_t *mt,
                                              int slot,
                                              int source_id,
                                              multitrack_master_clip_t *result)
{
    if(!mt || !result)
        return 0;

    for(unsigned int i = 0; i < mt->master_clip_count; i++) {
        const multitrack_master_clip_t *clip = &mt->master_clips[i];
        if(clip->slot == slot) {
            *result = *clip;
            return 1;
        }
    }

    for(unsigned int i = 0; i < mt->master_clip_count; i++) {
        const multitrack_master_clip_t *clip = &mt->master_clips[i];
        if(clip->sample_id == source_id) {
            *result = *clip;
            return 1;
        }
    }

    return 0;
}

static int multitrack_follower_clip_for_status(const multitracker_t *mt,
                                                int track,
                                                int slot,
                                                int source_id,
                                                gvr_sequence_timeline_clip_t *result)
{
    const gvr_sequence_timeline_t *timeline;

    if(!mt || !result || track < 0 || track >= MAX_TRACKS)
        return 0;

    timeline = &mt->track_timeline[track];
    if(!timeline->valid || timeline->count == 0)
        return 0;

    for(unsigned int i = 0; i < timeline->count; i++) {
        const gvr_sequence_timeline_clip_t *clip = &timeline->clips[i];
        if(clip->slot == slot) {
            *result = *clip;
            return 1;
        }
    }

    for(unsigned int i = 0; i < timeline->count; i++) {
        const gvr_sequence_timeline_clip_t *clip = &timeline->clips[i];
        if(clip->sample_id == source_id) {
            *result = *clip;
            return 1;
        }
    }

    return 0;
}

static int multitrack_build_sequence_clock(const multitracker_t *mt,
                                            int track,
                                            multitrack_sequence_clock_t *clock)
{
    const int *status;
    int clip_in;
    int clip_out;
    int project_total;
    int slot;
    int clip_length;
    int local_frame;
    int local_total;
    long long elapsed;

    if(!mt || !clock || track < 0 || track >= MAX_TRACKS)
        return 0;

    veejay_memset(clock, 0, sizeof(*clock));
    status = mt->status_cache[track];
    if(!status[SEQ_ACT])
        return 0;

    slot = status[SEQ_CUR];
    if(track == mt->project_master_track) {
        multitrack_master_clip_t clip;
        if(!multitrack_master_clip_for_status(mt,
                                               slot,
                                               status[CURRENT_ID],
                                               &clip))
            return 0;
        clip_in = clip.project_in;
        clip_out = clip.project_out;
        project_total = mt->timeline_total_frames;
        if(project_total <= 0) {
            for(unsigned int i = 0; i < mt->master_clip_count; i++)
                project_total = MAX(project_total,
                                    mt->master_clips[i].project_out + 1);
        }
    }
    else {
        gvr_sequence_timeline_clip_t clip;
        const gvr_sequence_timeline_t *timeline = &mt->track_timeline[track];
        if(!multitrack_follower_clip_for_status(mt,
                                                 track,
                                                 slot,
                                                 status[CURRENT_ID],
                                                 &clip))
            return 0;
        clip_in = clip.project_in;
        clip_out = clip.project_out;
        project_total = timeline->total_frames;
        if(project_total <= 0) {
            for(unsigned int i = 0; i < timeline->count; i++)
                project_total = MAX(project_total,
                                    timeline->clips[i].project_out + 1);
        }
    }

    clip_length = clip_out - clip_in + 1;
    local_frame = mt->sync_frame[track];
    local_total = mt->sync_total[track];
    if(clip_length <= 0 || project_total <= 0 || local_total <= 0)
        return 0;

    if(local_total <= 1 || clip_length <= 1) {
        elapsed = 0;
    }
    else {
        elapsed = llroundl((long double)MAX(0, local_frame) *
                           (long double)(clip_length - 1) /
                           (long double)(local_total - 1));
    }
    if(elapsed < 0)
        elapsed = 0;
    else if(elapsed >= clip_length)
        elapsed = clip_length - 1;

    clock->valid = 1;
    clock->slot = slot;
    clock->clip_in = clip_in;
    clock->clip_out = clip_out;
    clock->project_frame = clip_in + (int)elapsed;
    clock->project_total = project_total;
    clock->local_frame = local_frame;
    clock->local_total = local_total;
    clock->fps_x100 = multitrack_track_fps_x100(mt, track);
    clock->position_seconds =
        multitrack_position_seconds(clock->project_frame, clock->fps_x100);
    clock->duration_seconds =
        multitrack_position_seconds(clock->project_total, clock->fps_x100);
    return clock->duration_seconds > 0.0L;
}

static long double multitrack_wrap_seconds(long double position,
                                               long double duration)
{
    if(duration <= 0.0L)
        return position;

    position = fmodl(position, duration);
    if(position < 0.0L)
        position += duration;
    return position;
}

static long double multitrack_project_direct_seconds(int frame,
                                                       int fps_x100,
                                                       long double age_seconds,
                                                       int speed,
                                                       int total,
                                                       int can_wrap)
{
    long double position = multitrack_position_seconds(frame, fps_x100);

    if(speed != 0 && age_seconds > 0.0L)
        position += age_seconds * (long double)speed;

    if(can_wrap && total > 1) {
        const long double duration =
            multitrack_position_seconds(total, fps_x100);
        position = multitrack_wrap_seconds(position, duration);
    }

    return position;
}

static long double multitrack_status_age_seconds(const multitracker_t *mt,
                                                  int track,
                                                  gint64 compare_us)
{
    gint64 age_us;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       mt->sync_status_us[track] <= 0)
        return 0.0L;

    age_us = compare_us - mt->sync_status_us[track];
    if(age_us <= 0)
        return 0.0L;

    return (long double)age_us / 1000000.0L;
}

static long double multitrack_project_sequence_seconds(
    const multitrack_sequence_clock_t *clock,
    long double age_seconds,
    int speed)
{
    long double position;

    if(!clock || !clock->valid)
        return 0.0L;

    position = clock->position_seconds;
    if(speed != 0 && age_seconds > 0.0L)
        position += age_seconds * (long double)speed;

    return multitrack_wrap_seconds(position, clock->duration_seconds);
}

static void multitrack_drift_reset_phase(multitracker_t *mt, int track)
{
    if(!mt || track < 0 || track >= __MAX_TRACKS)
        return;

    mt->drift_phase_valid[track] = 0;
    mt->drift_phase_offset_seconds[track] = 0.0L;
    mt->drift_phase_duration_seconds[track] = 0.0L;
    mt->drift_last_observed_phase_seconds[track] = 0.0L;
    mt->drift_last_observed_status_us[track] = 0;
    mt->drift_previous_frame[track] = 0;
    mt->drift_previous_total[track] = 0;
    mt->drift_previous_speed[track] = 0;
    mt->drift_previous_status_us[track] = 0;
    mt->drift_error_sign[track] = 0;
    mt->drift_error_samples[track] = 0;
}

static void multitrack_drift_reset_all_phases(multitracker_t *mt)
{
    if(!mt)
        return;

    for(int track = 0; track < __MAX_TRACKS; track++)
        multitrack_drift_reset_phase(mt, track);
}


static void multitrack_drift_clear_track(multitracker_t *mt, int track)
{
    if(!mt || track < 0 || track >= __MAX_TRACKS)
        return;
    if(track >= MAX_TRACKS)
        return;

    multitrack_drift_reset_phase(mt, track);
    mt->drift_frames[track] = 0;
    mt->drift_correcting[track] = 0;
    gvr_multi_track_edit_set_track_drift(mt->timeline,
                                          track,
                                          0,
                                          0,
                                          FALSE);
}

static void multitrack_drift_seek_local(multitracker_t *mt,
                                        int track,
                                        int target_local)
{
    const int target = mt->sync_origin[track] + target_local;

    if(mt->sync_play_mode[track] == MODE_STREAM &&
       seq_stream_buffer_ready_status(mt->status_cache[track])) {
        const int stream_id = mt->status_cache[track][CURRENT_ID];
        gvr_queue_mmvims(mt->preview,
                         track,
                         VIMS_STREAM_BUFFER_SET_FRAME,
                         stream_id,
                         target_local);
    }
    else {
        gvr_queue_mvims(mt->preview,
                        track,
                        VIMS_VIDEO_SET_FRAME,
                        target);
    }
}

static int multitrack_drift_consume_direct_wrap(multitracker_t *mt,
                                                 int track,
                                                 int frame,
                                                 int total,
                                                 int speed,
                                                 int fps_x100,
                                                 gint64 status_us)
{
    int wrapped = 0;

    if(mt->drift_previous_status_us[track] > 0 &&
       status_us > mt->drift_previous_status_us[track] &&
       mt->drift_previous_total[track] == total &&
       mt->drift_previous_speed[track] == speed &&
       total > 4) {
        const gint64 elapsed_us =
            status_us - mt->drift_previous_status_us[track];
        const long double expected =
            ((long double)elapsed_us / 1000000.0L) *
            ((long double)fps_x100 / 100.0L) *
            (long double)ABS(speed);
        const int edge = CLAMP((int)ceill(expected) + 4,
                               1,
                               MAX(1, total / 4));
        const int high = total - edge;
        const int previous = mt->drift_previous_frame[track];
        int wrapped_step = 0;

        if(speed > 0 && previous >= high && frame < edge)
            wrapped_step = (total - previous) + frame;
        else if(speed < 0 && previous < edge && frame >= high)
            wrapped_step = previous + (total - frame);

        if(wrapped_step > 0 &&
           (long double)wrapped_step <= expected + (long double)edge)
            wrapped = 1;
    }

    mt->drift_previous_frame[track] = frame;
    mt->drift_previous_total[track] = total;
    mt->drift_previous_speed[track] = speed;
    mt->drift_previous_status_us[track] = status_us;
    return wrapped;
}

static int multitrack_sequence_target_local_frame(
    const multitracker_t *mt,
    int track,
    const multitrack_sequence_clock_t *follower,
    long double target_phase_seconds)
{
    const gvr_sequence_timeline_t *timeline;
    long long target_project_frame;
    const gvr_sequence_timeline_clip_t *target_clip = NULL;
    long long clip_offset;
    long long local;

    if(!mt || !follower || !follower->valid ||
       track < 0 || track >= MAX_TRACKS)
        return -1;

    timeline = &mt->track_timeline[track];
    if(!timeline->valid || timeline->count == 0)
        return -1;

    target_project_frame = llroundl(target_phase_seconds /
                                    multitrack_frame_period_seconds(
                                        follower->fps_x100));
    if(follower->project_total > 0) {
        target_project_frame %= follower->project_total;
        if(target_project_frame < 0)
            target_project_frame += follower->project_total;
    }

    for(unsigned int i = 0; i < timeline->count; i++) {
        const gvr_sequence_timeline_clip_t *clip = &timeline->clips[i];
        if(target_project_frame >= clip->project_in &&
           target_project_frame <= clip->project_out) {
            target_clip = clip;
            break;
        }
    }

    if(!target_clip || target_clip->slot != follower->slot)
        return -1;

    clip_offset = target_project_frame - target_clip->project_in;
    if(target_clip->project_out <= target_clip->project_in ||
       follower->local_total <= 1)
        return 0;

    local = llroundl((long double)clip_offset *
                     (long double)(follower->local_total - 1) /
                     (long double)(target_clip->project_out -
                                   target_clip->project_in));
    if(local < 0)
        local = 0;
    else if(local >= follower->local_total)
        local = follower->local_total - 1;

    return local > INT_MAX ? INT_MAX : (int)local;
}

static void multitrack_drift_update_internal(multitracker_t *mt)
{
    static gint64 drift_debug_last_us[__MAX_TRACKS];
    static gint64 drift_master_unavailable_last_us;
    const gint64 settle_us = 1200000;
    gint64 now;
    int master;
    int *m;
    int master_frame;
    int master_total;
    int master_speed;
    int master_mode;
    int master_fps_x100;
    int transition_active;
    int master_wrapped = 0;
    long double master_period;
    gint64 master_stale_us;
    multitrack_sequence_clock_t master_sequence_clock;
    int master_sequence_clock_valid;
    long double master_age_seconds;
    const char *master_unavailable = NULL;

    if(!mt || !mt->drift_lock_enabled)
        return;

    master = mt->project_master_track;
    if(master < 0 || !gvr_track_test(mt->preview, master))
        return;

    now = g_get_monotonic_time();
    m = mt->status_cache[master];
    master_frame = mt->sync_frame[master];
    master_total = mt->sync_total[master];
    master_speed = mt->sync_speed[master];
    master_mode = mt->sync_play_mode[master];
    master_fps_x100 = multitrack_track_fps_x100(mt, master);
    master_period = multitrack_frame_period_seconds(master_fps_x100);
    master_stale_us = MAX((gint64)750000,
                          (gint64)llroundl(master_period * 3000000.0L));

    if(mt->sync_status_us[master] <= 0)
        master_unavailable = "no-status";
    else if(now - mt->sync_status_us[master] > master_stale_us)
        master_unavailable = "stale-status";
    else if(m[CURRENT_ID] <= 0)
        master_unavailable = "no-source";
    else if(master_total <= 0)
        master_unavailable = "source-initializing";
    else if(master_speed == 0)
        master_unavailable = "paused";
    else if(master_mode == MODE_PLAIN)
        master_unavailable = "plain-edl";

    if(master_unavailable) {
        multitrack_drift_clear_track(mt, master);
        for(int track = 0; track < MAX_TRACKS; track++) {
            if(track != master && gvr_track_test(mt->preview, track))
                multitrack_drift_clear_track(mt, track);
        }
        if(now - drift_master_unavailable_last_us >= 1000000) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "MULTITRACK DRIFT unavailable master=Video%d reason=%s",
                       master + 1,
                       master_unavailable);
            drift_master_unavailable_last_us = now;
        }
        return;
    }

    transition_active = m[VJ_STATUS_MULTITRACK_TRANSITION_ACTIVE];
    master_sequence_clock_valid = multitrack_build_sequence_clock(
        mt,
        master,
        &master_sequence_clock);
    master_age_seconds = multitrack_status_age_seconds(mt, master, now);
    gvr_multi_track_edit_set_track_drift(mt->timeline, master, 0, 0, FALSE);

    if(!m[SEQ_ACT] &&
       multitrack_mode_is_sample(master_mode) &&
       m[SAMPLE_LOOP] == 1 &&
       now - mt->drift_source_changed_us[master] >= settle_us) {
        master_wrapped = multitrack_drift_consume_direct_wrap(
            mt,
            master,
            master_frame,
            master_total,
            master_speed,
            master_fps_x100,
            mt->sync_status_us[master]);
    }
    else {
        mt->drift_previous_status_us[master] = 0;
    }

    for(int track = 0; track < MAX_TRACKS; track++) {
        int *f;
        int follower_frame;
        int follower_total;
        int follower_speed;
        int follower_fps_x100;
        int can_wrap;
        int drift;
        int drift_ms;
        int correcting = 0;
        int display_drift = 1;
        int use_sequence_phase = 0;
        int target_local = -1;
        int follower_compare_local = -1;
        int correction_frame_error = 0;
        int correction_frame_valid = 0;
        int phase_reanchored = 0;
        int soft_adjustment = 0;
        long double follower_period;
        long double drift_seconds;
        gint64 follower_stale_us;
        gint64 soft_cooldown_us;
        gint64 hard_cooldown_us;
        long double phase_jump_seconds = 0.0L;
        long double target_phase_seconds = 0.0L;
        long double master_compare_seconds = 0.0L;
        long double follower_compare_seconds = 0.0L;
        long double follower_age_seconds;
        multitrack_sequence_clock_t follower_sequence_clock;
        const char *suppressed_reason = NULL;
        char action[64];

        if(track == master || !gvr_track_test(mt->preview, track))
            continue;

        f = mt->status_cache[track];
        follower_frame = mt->sync_frame[track];
        follower_total = mt->sync_total[track];
        follower_speed = mt->sync_speed[track];
        follower_fps_x100 = multitrack_track_fps_x100(mt, track);
        follower_period = multitrack_frame_period_seconds(follower_fps_x100);
        follower_stale_us = MAX((gint64)750000,
                                (gint64)llroundl(follower_period * 3000000.0L));

        if(f[CURRENT_ID] <= 0 ||
           follower_total <= 0 ||
           follower_speed == 0 ||
           mt->sync_status_us[track] <= 0 ||
           now - mt->sync_status_us[track] > follower_stale_us) {
            multitrack_drift_clear_track(mt, track);
            continue;
        }

        follower_age_seconds = multitrack_status_age_seconds(mt, track, now);
        soft_cooldown_us = MAX((gint64)250000,
                               (gint64)llroundl(follower_period * 1500000.0L));
        hard_cooldown_us = MAX((gint64)1000000,
                               (gint64)llroundl(follower_period * 2000000.0L));
        can_wrap = multitrack_mode_is_sample(master_mode) &&
                   multitrack_mode_is_sample(mt->sync_play_mode[track]) &&
                   m[SAMPLE_LOOP] == 1 &&
                   f[SAMPLE_LOOP] == 1;
        g_strlcpy(action, "none", sizeof(action));

        {
            const int master_sequence_active = m[SEQ_ACT] != 0;
            const int follower_sequence_active = f[SEQ_ACT] != 0;
            const int follower_sequence_clock_valid =
                multitrack_build_sequence_clock(mt,
                                                track,
                                                &follower_sequence_clock);

            if(master_sequence_active || follower_sequence_active) {
                if(!master_sequence_clock_valid ||
                   !follower_sequence_clock_valid) {
                    multitrack_drift_clear_track(mt, track);
                    continue;
                }

                {
                    const long double follower_duration =
                        follower_sequence_clock.duration_seconds;
                    const long double reanchor_limit =
                        4.5L * follower_period;
                    int need_anchor =
                        !mt->drift_phase_valid[track] ||
                        fabsl(mt->drift_phase_duration_seconds[track] -
                              follower_duration) > 2.0L * follower_period;

                    use_sequence_phase = 1;
                    master_compare_seconds =
                        multitrack_project_sequence_seconds(
                            &master_sequence_clock,
                            master_age_seconds,
                            master_speed);
                    follower_compare_seconds =
                        multitrack_project_sequence_seconds(
                            &follower_sequence_clock,
                            follower_age_seconds,
                            follower_speed);

                    if(!need_anchor &&
                       mt->drift_last_observed_status_us[track] > 0 &&
                       mt->sync_status_us[track] >
                           mt->drift_last_observed_status_us[track]) {
                        const long double elapsed_seconds =
                            (long double)(mt->sync_status_us[track] -
                                          mt->drift_last_observed_status_us[track]) /
                            1000000.0L;
                        const long double observed_step = remainderl(
                            follower_sequence_clock.position_seconds -
                            mt->drift_last_observed_phase_seconds[track],
                            follower_duration);
                        const long double expected_step =
                            elapsed_seconds * (long double)follower_speed;

                        phase_jump_seconds = remainderl(
                            observed_step - expected_step,
                            follower_duration);
                        if(fabsl(phase_jump_seconds) > reanchor_limit &&
                           now - mt->drift_last_correction_us[track] >=
                               soft_cooldown_us)
                            need_anchor = 1;
                    }

                    if(!need_anchor) {
                        target_phase_seconds = multitrack_wrap_seconds(
                            master_compare_seconds +
                            mt->drift_phase_offset_seconds[track],
                            follower_duration);
                        drift_seconds = remainderl(
                            target_phase_seconds - follower_compare_seconds,
                            follower_duration);
                        if(fabsl(drift_seconds) > reanchor_limit)
                            need_anchor = 1;
                    }

                    if(need_anchor) {
                        const long double master_on_follower =
                            multitrack_wrap_seconds(master_compare_seconds,
                                                    follower_duration);

                        mt->drift_phase_valid[track] = 1;
                        mt->drift_phase_offset_seconds[track] = remainderl(
                            follower_compare_seconds - master_on_follower,
                            follower_duration);
                        mt->drift_phase_duration_seconds[track] =
                            follower_duration;
                        target_phase_seconds = follower_compare_seconds;
                        drift_seconds = 0.0L;
                        phase_reanchored = 1;
                        mt->drift_error_sign[track] = 0;
                        mt->drift_error_samples[track] = 0;
                        g_strlcpy(action, "anchor", sizeof(action));

                        veejay_msg(VEEJAY_MSG_DEBUG,
                                   "MULTITRACK DRIFT anchor Video%d offset=%.6Lf jump_ms=%.0Lf",
                                   track + 1,
                                   mt->drift_phase_offset_seconds[track],
                                   phase_jump_seconds * 1000.0L);
                    }

                    mt->drift_phase_duration_seconds[track] =
                        follower_duration;
                    mt->drift_last_observed_phase_seconds[track] =
                        follower_sequence_clock.position_seconds;
                    mt->drift_last_observed_status_us[track] =
                        mt->sync_status_us[track];
                    target_local = multitrack_sequence_target_local_frame(
                        mt,
                        track,
                        &follower_sequence_clock,
                        target_phase_seconds);
                }
            }
            else {
                const long double master_position =
                    multitrack_project_direct_seconds(master_frame,
                                                       master_fps_x100,
                                                       master_age_seconds,
                                                       master_speed,
                                                       master_total,
                                                       can_wrap);
                const long double follower_position =
                    multitrack_project_direct_seconds(follower_frame,
                                                       follower_fps_x100,
                                                       follower_age_seconds,
                                                       follower_speed,
                                                       follower_total,
                                                       can_wrap);

                if(mt->drift_phase_valid[track])
                    multitrack_drift_reset_phase(mt, track);
                drift_seconds = master_position - follower_position;
                if(can_wrap && follower_total > 1) {
                    const long double follower_duration =
                        multitrack_position_seconds(follower_total,
                                                    follower_fps_x100);
                    drift_seconds = remainderl(drift_seconds,
                                               follower_duration);
                }
                master_compare_seconds = master_position;
                follower_compare_seconds = follower_position;
                target_phase_seconds = master_position;
                target_local = (int)llroundl(
                    master_position /
                    multitrack_frame_period_seconds(follower_fps_x100));
                if(follower_total > 0) {
                    if(can_wrap) {
                        target_local %= follower_total;
                        if(target_local < 0)
                            target_local += follower_total;
                    }
                    target_local = CLAMP(target_local, 0, follower_total - 1);
                }
            }
        }

        if(target_local >= 0) {
            long long projected = follower_frame;

            if(follower_age_seconds > 0.0L) {
                projected += llroundl(
                    follower_age_seconds *
                    ((long double)follower_fps_x100 / 100.0L) *
                    (long double)follower_speed);
            }

            if(follower_total > 0) {
                if(can_wrap) {
                    projected %= follower_total;
                    if(projected < 0)
                        projected += follower_total;
                }
                else {
                    if(projected < 0)
                        projected = 0;
                    else if(projected >= follower_total)
                        projected = follower_total - 1;
                }
            }

            follower_compare_local = projected > INT_MAX ? INT_MAX :
                                     (projected < INT_MIN ? INT_MIN :
                                      (int)projected);
            correction_frame_error = target_local - follower_compare_local;

            if(can_wrap && follower_total > 1) {
                const int half = follower_total / 2;
                if(correction_frame_error > half)
                    correction_frame_error -= follower_total;
                else if(correction_frame_error < -half)
                    correction_frame_error += follower_total;
            }
            correction_frame_valid = 1;
        }

        drift = (int)llroundl(drift_seconds / master_period);
        drift_ms = (int)llroundl(drift_seconds * 1000.0L);
        mt->drift_frames[track] = drift;

        if(transition_active)
            suppressed_reason = "transition";
        else if(master_speed != follower_speed)
            suppressed_reason = "speed-mismatch";
        else if(mt->sync_play_mode[track] == MODE_PLAIN)
            suppressed_reason = "plain-edl";
        else if(now - mt->drift_source_changed_us[master] < settle_us ||
                now - mt->drift_source_changed_us[track] < settle_us)
            suppressed_reason = "settling";

        if(suppressed_reason) {
            mt->drift_error_sign[track] = 0;
            mt->drift_error_samples[track] = 0;
            display_drift = 0;
            g_snprintf(action,
                       sizeof(action),
                       "suppressed:%s",
                       suppressed_reason);
        }
        else if(master_wrapped &&
                !use_sequence_phase &&
                can_wrap &&
                correction_frame_valid &&
                ABS(correction_frame_error) > 1 &&
                target_local >= 0) {
            multitrack_drift_seek_local(mt, track, target_local);
            mt->drift_last_correction_us[track] = now;
            mt->drift_error_sign[track] = 0;
            mt->drift_error_samples[track] = 0;
            correcting = 1;
            display_drift = 0;
            g_snprintf(action,
                       sizeof(action),
                       "wrap-hard:%d",
                       target_local);
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "MULTITRACK DRIFT wrap-hard Video%d frame_error=%d target_local=%d delta_ms=%d",
                       track + 1,
                       correction_frame_error,
                       target_local,
                       drift_ms);
        }
        else {
            if(!correction_frame_valid || ABS(correction_frame_error) <= 1) {
                mt->drift_error_sign[track] = 0;
                mt->drift_error_samples[track] = 0;
            }
            else {
                const int correction_sign =
                    correction_frame_error > 0 ? 1 : -1;
                if(mt->drift_error_sign[track] == correction_sign) {
                    if(mt->drift_error_samples[track] < 3)
                        mt->drift_error_samples[track]++;
                }
                else {
                    mt->drift_error_sign[track] = correction_sign;
                    mt->drift_error_samples[track] = 1;
                }
            }

            if(mt->drift_error_samples[track] >= 2 &&
               correction_frame_valid &&
               ABS(correction_frame_error) >= 2 &&
               ABS(correction_frame_error) <= 4 &&
               now - mt->drift_last_correction_us[track] >= soft_cooldown_us) {
                soft_adjustment = correction_frame_error;

                if((soft_adjustment > 0 && follower_speed < 0) ||
                   (soft_adjustment < 0 && follower_speed > 0))
                    soft_adjustment = -follower_speed;

                soft_adjustment = CLAMP(soft_adjustment, -4, 4);
                if(soft_adjustment != 0) {
                    gvr_queue_mvims(mt->preview,
                                    track,
                                    VIMS_VIDEO_SYNC_ADJUST,
                                    soft_adjustment);
                    mt->drift_last_correction_us[track] = now;
                    mt->drift_error_sign[track] = 0;
                    mt->drift_error_samples[track] = 0;
                    correcting = 1;
                    g_snprintf(action,
                               sizeof(action),
                               "soft:%+d",
                               soft_adjustment);
                    veejay_msg(VEEJAY_MSG_DEBUG,
                               "MULTITRACK DRIFT soft Video%d frame_error=%d adjustment=%d delta_ms=%d",
                               track + 1,
                               correction_frame_error,
                               soft_adjustment,
                               drift_ms);
                }
            }
            else if(!use_sequence_phase &&
                    mt->drift_error_samples[track] >= 2 &&
                    correction_frame_valid &&
                    ABS(correction_frame_error) > 4 &&
                    target_local >= 0 &&
                    now - mt->drift_last_correction_us[track] >=
                        hard_cooldown_us) {
                multitrack_drift_seek_local(mt, track, target_local);
                mt->drift_last_correction_us[track] = now;
                mt->drift_error_sign[track] = 0;
                mt->drift_error_samples[track] = 0;
                correcting = 1;
                g_snprintf(action,
                           sizeof(action),
                           "hard:%d",
                           target_local);
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "MULTITRACK DRIFT hard Video%d frame_error=%d target_local=%d delta_ms=%d",
                           track + 1,
                           correction_frame_error,
                           target_local,
                           drift_ms);
            }
        }

        mt->drift_correcting[track] = correcting;
        gvr_multi_track_edit_set_track_drift(mt->timeline,
                                              track,
                                              display_drift ? drift : 0,
                                              display_drift ? drift_ms : 0,
                                              correcting);

        if(now - drift_debug_last_us[track] >= 1000000 ||
           correcting || phase_reanchored) {
            if(use_sequence_phase) {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "MULTITRACK DRIFT clock=sequence-relative action=%s anchor=%s offset=%.6Lf jump_ms=%.0Lf M=Video%d slot=%d id=%d local=%d/%d fps=%.5Lf age=%.1Lfms project_t=%.6Lf/%.6Lf cmp=%.6Lf | F=Video%d slot=%d id=%d local=%d/%d fps=%.5Lf age=%.1Lfms project_t=%.6Lf/%.6Lf cmp=%.6Lf target=%.6Lf | delta_ms=%d delta_master_frames=%d target_local=%d compare_local=%d frame_error=%d speed=%d/%d",
                           action,
                           phase_reanchored ? "new" : "keep",
                           mt->drift_phase_offset_seconds[track],
                           phase_jump_seconds * 1000.0L,
                           master + 1,
                           master_sequence_clock.slot,
                           m[CURRENT_ID],
                           master_frame,
                           master_total,
                           (long double)master_fps_x100 / 100.0L,
                           master_age_seconds * 1000.0L,
                           master_sequence_clock.position_seconds,
                           master_sequence_clock.duration_seconds,
                           master_compare_seconds,
                           track + 1,
                           follower_sequence_clock.slot,
                           f[CURRENT_ID],
                           follower_frame,
                           follower_total,
                           (long double)follower_fps_x100 / 100.0L,
                           follower_age_seconds * 1000.0L,
                           follower_sequence_clock.position_seconds,
                           follower_sequence_clock.duration_seconds,
                           follower_compare_seconds,
                           target_phase_seconds,
                           drift_ms,
                           drift,
                           target_local,
                           follower_compare_local,
                           correction_frame_error,
                           master_speed,
                           follower_speed);
            }
            else {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "MULTITRACK DRIFT clock=sample-local action=%s M=Video%d id=%d raw=%d local=%d/%d fps=%.5Lf age=%.1Lfms cmp=%.6Lf | F=Video%d id=%d raw=%d local=%d/%d fps=%.5Lf age=%.1Lfms cmp=%.6Lf target=%.6Lf | delta_ms=%d delta_master_frames=%d target_local=%d compare_local=%d frame_error=%d speed=%d/%d",
                           action,
                           master + 1,
                           m[CURRENT_ID],
                           m[FRAME_NUM],
                           master_frame,
                           master_total,
                           (long double)master_fps_x100 / 100.0L,
                           master_age_seconds * 1000.0L,
                           master_compare_seconds,
                           track + 1,
                           f[CURRENT_ID],
                           f[FRAME_NUM],
                           follower_frame,
                           follower_total,
                           (long double)follower_fps_x100 / 100.0L,
                           follower_age_seconds * 1000.0L,
                           follower_compare_seconds,
                           target_phase_seconds,
                           drift_ms,
                           drift,
                           target_local,
                           follower_compare_local,
                           correction_frame_error,
                           master_speed,
                           follower_speed);
            }
            drift_debug_last_us[track] = now;
        }
    }
}

void multitrack_drift_update(void *data)
{
    multitrack_drift_update_internal((multitracker_t*)data);
}

static GdkPixbuf *load_logo_image(void)
{
    char path[1024];
    veejay_memset(path, 0, sizeof(path));
    get_gd(path, NULL, "veejay-logo.png");
    return gdk_pixbuf_new_from_file(path, NULL);
}

int mt_set_max_tracks(int tracks)
{
    if(tracks < 1 || tracks > __MAX_TRACKS)
        return 0;
    MAX_TRACKS = tracks;
    return 1;
}

int mt_get_max_tracks(void)
{
    return __MAX_TRACKS;
}

static const char *multitrack_default_host(multitracker_t *mt)
{
    const char *host = NULL;

    if(mt && mt->preview && mt->project_master_track >= 0)
        host = gvr_track_get_hostname(mt->preview,
                                      mt->project_master_track);
    return host && host[0] ? host : "localhost";
}

static int multitrack_next_port_hint(multitracker_t *mt, const char *host)
{
    int base = DEFAULT_PORT_NUM;
    int current = 0;

    if(mt && mt->preview && mt->project_master_track >= 0) {
        current = gvr_track_get_portnum(mt->preview,
                                        mt->project_master_track);
        if(current > 0)
            base = current;
    }

    if(!mt || !mt->preview)
        return base;

    for(int port = current > 0 ? base + 1000 : base;
        port <= 65535;
        port += 1000)
        if(!gvr_track_already_open(mt->preview, host, port))
            return port;

    for(int port = DEFAULT_PORT_NUM; port <= 65535; port += 1000)
        if(!gvr_track_already_open(mt->preview, host, port))
            return port;

    return base;
}

static void calculate_img_dimension(int w,
                                    int h,
                                    int *dst_w,
                                    int *dst_h,
                                    float *result,
                                    int max_w,
                                    int max_h,
                                    int quality)
{
    int tmp_w = w;
    int tmp_h = h;
    float ratio = (float)tmp_w / (float)tmp_h;

    *result = ratio;

    if(quality > 0) {
        while(quality-- > 0) {
            tmp_w /= 2;
            tmp_h /= 2;
        }
    }

    if(tmp_h > max_h) {
        tmp_h = max_h;
        tmp_w = (int)((float)tmp_h * ratio);
    }
    else if(tmp_w > max_w) {
        tmp_w = max_w;
        tmp_h = (int)((float)tmp_w / ratio);
    }

    *dst_w = tmp_w;
    *dst_h = tmp_h;
}

void multitrack_get_preview_dimensions(int w,
                                       int h,
                                       int *dst_w,
                                       int *dst_h)
{
    const int max_w = 192;
    const int max_h = 108;
    double ratio;

    if(w <= 0 || h <= 0) {
        *dst_w = 160;
        *dst_h = 90;
        return;
    }

    ratio = (double)w / (double)h;
    *dst_w = max_w;
    *dst_h = (int)floor(max_w / ratio + 0.5);
    if(*dst_h > max_h) {
        *dst_h = max_h;
        *dst_w = (int)floor(max_h * ratio + 0.5);
    }
    *dst_w = MAX(16, *dst_w & ~1);
    *dst_h = MAX(16, *dst_h & ~1);
}

static void multitrack_preview_configure(multitracker_t *mt, int track)
{
    int width;
    int height;
    int divisor;
    const int is_current = mt && track == mt->current_ui_track;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, track))
        return;

    if(is_current && mt->width > 0 && mt->height > 0) {
        width = mt->width;
        height = mt->height;
    }
    else {
        width = mt->preview_width > 0 ? mt->preview_width : 192;
        height = mt->preview_height > 0 ? mt->preview_height : 108;
    }

    divisor = 1 << CLAMP(mt->quality, 0, 3);
    width = MAX(16, (width / divisor) & ~1);
    height = MAX(16, (height / divisor) & ~1);

    if(!gvr_track_configure(mt->preview, track, width, height))
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Unable to configure %s preview %dx%d for Video %d",
                   is_current ? "main" : "lane",
                   width,
                   height,
                   track + 1);
}

static void multitrack_promote_cached_preview(multitracker_t *mt, int track)
{
    GdkPixbuf *pixbuf;

    if(!mt || track < 0 || track >= MAX_TRACKS)
        return;

    pixbuf = gvr_multi_track_edit_ref_track_preview(mt->timeline, track);
    vj_gui_apply_multitrack_preview(pixbuf);
    if(pixbuf)
        g_object_unref(pixbuf);
}

static int multitrack_first_open(multitracker_t *mt, int except)
{
    if(!mt)
        return -1;

    for(int i = 0; i < MAX_TRACKS; i++)
        if(i != except && gvr_track_test(mt->preview, i))
            return i;
    return -1;
}

static void multitrack_refresh_track(multitracker_t *mt, int track)
{
    int connected;
    char *host;
    int port;

    if(!mt || track < 0 || track >= MAX_TRACKS)
        return;

    connected = gvr_track_test(mt->preview, track);
    host = connected ? gvr_track_get_hostname(mt->preview, track) : NULL;
    port = connected ? gvr_track_get_portnum(mt->preview, track) : 0;

    gvr_multi_track_edit_set_track(mt->timeline,
                                   track,
                                   connected,
                                   host,
                                   port);
    gvr_multi_track_edit_set_current_control(mt->timeline,
                                             mt->current_ui_track);
    gvr_multi_track_edit_set_project_master(mt->timeline,
                                            mt->project_master_track);
    mt->preview_enabled[track] =
        connected && gvr_get_preview_status(mt->preview, track);
    gvr_multi_track_edit_set_track_preview_enabled(
        mt->timeline,
        track,
        mt->preview_enabled[track]);
}

static void multitrack_refresh_connection_topology(multitracker_t *mt)
{
    int forwarding;

    if(!mt || !mt->timeline)
        return;

    forwarding = vj_gui_vims_forwarding_enabled();
    if(mt->current_ui_track >= 0 && mt->current_ui_track < MAX_TRACKS)
        forwarding = mt->status_cache[mt->current_ui_track][MESSAGE_FORWARDING] != 0;

    gvr_multi_track_edit_set_connection_topology(
        mt->timeline,
        vj_gui_is_connected(),
        vj_gui_connected_host(),
        vj_gui_connected_port(),
        vj_gui_connected_to_master(),
        vj_gui_upstream_master_info_known(),
        vj_gui_connected_has_upstream_master(),
        vj_gui_upstream_master_host(),
        vj_gui_upstream_master_port(),
        forwarding);
}

void multitrack_refresh_connection_state(void *data)
{
    multitrack_refresh_connection_topology((multitracker_t*)data);
}

static void multitrack_refresh_all_tracks(multitracker_t *mt)
{
    if(!mt)
        return;
    for(int i = 0; i < MAX_TRACKS; i++)
        multitrack_refresh_track(mt, i);
    multitrack_refresh_connection_topology(mt);
    multitrack_switcher_sync_ui(mt);
}

static void multitrack_set_current_track(multitracker_t *mt,
                                         int track,
                                         int reconnect_main_ui)
{
    char *host_src;
    char *host;
    int port;
    int already_connected;
    const char *connected_host;
    int connected_port;
    int old_track;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, track))
        return;

    host_src = gvr_track_get_hostname(mt->preview, track);
    port = gvr_track_get_portnum(mt->preview, track);
    old_track = mt->current_ui_track;

    if(reconnect_main_ui) {
        if(!host_src || port <= 0)
            return;

        connected_host = vj_gui_connected_host();
        connected_port = vj_gui_connected_port();
        already_connected = connected_host &&
                            connected_port == port &&
                            strcmp(connected_host, host_src) == 0;

        if(!already_connected) {
            host = strdup(host_src);
            if(!host)
                return;

            mt->pending_ui_track = track;
            mt->current_ui_track = track;
            mt->selected = track;
            gvr_set_master(mt->preview, track);
            gvr_multi_track_edit_set_current_control(mt->timeline, track);
            gvr_multi_track_edit_set_selected_track(mt->timeline, track);
            multitrack_sync_main_preview_toggle(
                mt,
                gvr_get_preview_status(mt->preview, track));
            multitrack_promote_cached_preview(mt, track);
            if(old_track >= 0 && old_track != track &&
               gvr_track_test(mt->preview, old_track))
                multitrack_preview_configure(mt, old_track);
            multitrack_preview_configure(mt, track);

            if(vj_gui_switch_cached_track(old_track, track, host, port)) {
                mt->pending_ui_track = -1;
                status_print(mt,
                             "Switched the main Reloaded controls to Video %d (%s:%d) from the hot connection cache.",
                             track + 1,
                             host,
                             port);
                free(host);
                return;
            }

            status_print(mt,
                         "Cached switch unavailable; reconnecting the main Reloaded controls to Video %d (%s:%d)...",
                         track + 1,
                         host,
                         port);

            vj_gui_disable();
            vj_gui_cb(1, host, port);
            free(host);
            return;
        }
    }

    mt->pending_ui_track = -1;
    mt->current_ui_track = track;
    mt->selected = track;
    gvr_set_master(mt->preview, track);

    if(old_track >= 0 && old_track != track &&
       gvr_track_test(mt->preview, old_track))
        multitrack_preview_configure(mt, old_track);
    multitrack_preview_configure(mt, track);
    gvr_multi_track_edit_set_current_control(mt->timeline, track);
    gvr_multi_track_edit_set_selected_track(mt->timeline, track);
    multitrack_sync_main_preview_toggle(
        mt,
        gvr_get_preview_status(mt->preview, track));

    if(old_track != track) {
        status_print(mt,
                     "Switched the main Reloaded controls to Video %d (%s:%d). "
                     "The project-master timeline remains on Video %d.",
                     track + 1,
                     host_src ? host_src : "localhost",
                     port,
                     mt->project_master_track + 1);
    }
}

static void multitrack_track_selected(GtkWidget *widget,
                                      gint track,
                                      gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;
    if(!mt || track < 0 || track >= MAX_TRACKS)
        return;
    mt->selected = track;
}

static void multitrack_switch_requested(GtkWidget *widget,
                                        gint track,
                                        gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;
    multitrack_set_current_track(mt, track, 1);
}

static void multitrack_preview_toggled(GtkWidget *widget,
                                       gint track,
                                       gboolean enabled,
                                       gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, track))
        return;

    if(enabled)
        multitrack_preview_configure(mt, track);

    if(!gvr_track_toggle_preview(mt->preview, track, enabled)) {
        enabled = gvr_get_preview_status(mt->preview, track);
    }

    mt->preview_enabled[track] = enabled ? 1 : 0;
    gvr_multi_track_edit_set_track_preview_enabled(mt->timeline,
                                                    track,
                                                    enabled);
    if(track == mt->current_ui_track)
        multitrack_sync_main_preview_toggle(mt, enabled);
    status_print(mt,
                 "Video %d preview %s",
                 track + 1,
                 enabled ? "enabled" : "disabled");
}

static void multitrack_seek_requested(GtkWidget *widget,
                                      gint frame,
                                      gpointer user_data)
{
    multitracker_t *mt = user_data;
    const int *status;
    int master;
    int play_mode;
    (void)widget;

    if(!mt || mt->project_master_track < 0 ||
       !gvr_track_test(mt->preview, mt->project_master_track))
        return;

    master = mt->project_master_track;
    status = mt->status_cache[master];
    play_mode = status[PLAY_MODE];

    if(status[SEQ_ACT]) {
        const int current_slot = status[SEQ_CUR];

        for(unsigned int i = 0; i < mt->master_clip_count; i++) {
            multitrack_master_clip_t *clip = &mt->master_clips[i];

            if(frame < clip->project_in || frame > clip->project_out)
                continue;

            if(current_slot != clip->slot) {
                status_print(mt,
                             "Project frame %d belongs to sequence slot %d. "
                             "The backend has no direct project-slot seek command yet; "
                             "enter that slot before seeking inside it.",
                             frame,
                             clip->slot + 1);
                return;
            }

            const int source_frame = frame - clip->project_in;
            vj_gui_vims_observe_external(VIMS_VIDEO_SET_FRAME,
                                         "%d",
                                         source_frame);
            gvr_queue_mvims(mt->preview,
                            master,
                            VIMS_VIDEO_SET_FRAME,
                            source_frame);
            return;
        }

        status_print(mt,
                     "Project frame %d is outside the known finite master timeline.",
                     frame);
        return;
    }

    if(multitrack_mode_is_sample(play_mode)) {
        const int total = MAX(1, mt->sync_total[master]);
        const int local_frame = CLAMP(frame, 0, total - 1);
        const int absolute_frame = mt->sync_origin[master] + local_frame;

        vj_gui_vims_observe_external(VIMS_VIDEO_SET_FRAME,
                                     "%d",
                                     absolute_frame);
        gvr_queue_mvims(mt->preview,
                        master,
                        VIMS_VIDEO_SET_FRAME,
                        absolute_frame);
        return;
    }

    if(play_mode == MODE_STREAM) {
        const int stream_id = status[CURRENT_ID];

        if(seq_stream_buffer_ready_status(status) && stream_id > 0) {
            const int filled = MAX(1, status[STREAM_BUFFER_FILLED]);
            const int buffer_frame = CLAMP(frame, 0, filled - 1);

            vj_gui_vims_observe_external(VIMS_STREAM_BUFFER_SET_FRAME,
                                         "%d %d",
                                         stream_id,
                                         buffer_frame);
            gvr_queue_mmvims(mt->preview,
                             master,
                             VIMS_STREAM_BUFFER_SET_FRAME,
                             stream_id,
                             buffer_frame);
            return;
        }
    }

    status_print(mt,
                 "Cannot seek Video %d: %s.",
                 master + 1,
                 multitrack_seek_unavailable_reason(status));
}

static void multitrack_source_play_requested(GtkWidget *widget,
                                             gint track,
                                             gint sample_id,
                                             gint sample_type,
                                             gpointer user_data)
{
    multitracker_t *mt = user_data;
    int target_mode;
    (void)widget;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       sample_id <= 0 || sample_type < 0 ||
       !gvr_track_test(mt->preview, track) ||
       mt->status_cache[track][SEQ_ACT])
        return;

    target_mode = sample_type == 0 ? MODE_SAMPLE : MODE_STREAM;
    multitrack_drift_reset_phase(mt, track);
    mt->drift_source_changed_us[track] = g_get_monotonic_time();
    gvr_queue_mmvims(mt->preview,
                     track,
                     VIMS_SET_MODE_AND_GO,
                     sample_id,
                     target_mode);
    status_print(mt,
                 "Switching Video %d to %s %d",
                 track + 1,
                 sample_type == 0 ? "Sample" : "Stream",
                 sample_id);
}

static void multitrack_sequence_source_insert_requested(GtkWidget *widget,
                                                         gint track,
                                                         gint bank,
                                                         gint insertion_slot,
                                                         gint sample_id,
                                                         gint sample_type,
                                                         gpointer user_data)
{
    multitracker_t *mt = user_data;
    gboolean inserted;

    if(!mt || track != mt->project_master_track ||
       track != mt->current_ui_track ||
       track < 0 || track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, track) ||
       !mt->status_cache[track][SEQ_ACT]) {
        if(mt)
            status_print(mt,
                         "Switch control to the project-master video before editing its sequence");
        gvr_multi_track_edit_clear_pending_source(widget);
        return;
    }

    inserted = vj_gui_sequence_insert_source_at(bank,
                                                 insertion_slot,
                                                 sample_id,
                                                 sample_type);
    if(!inserted) {
        gvr_multi_track_edit_clear_pending_source(widget);
        return;
    }

    status_print(mt,
                 "Inserted %s %d into sequence bank %d at slot %d",
                 sample_type == 0 ? "Sample" : "Stream",
                 sample_id,
                 bank + 1,
                 insertion_slot + 1);
}

static void multitrack_reveal_sequence_slot_requested(GtkWidget *widget,
                                                       gint bank,
                                                       gint slot,
                                                       gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;

    if(!mt || mt->project_master_track != mt->current_ui_track) {
        if(mt)
            status_print(mt,
                         "Switch control to the project-master video before revealing its sequence slot");
        return;
    }
    vj_gui_reveal_sequence_slot(bank, slot);
}

static void multitrack_reveal_source_requested(GtkWidget *widget,
                                                gint sample_id,
                                                gint sample_type,
                                                gpointer user_data)
{
    (void)widget;
    (void)user_data;
    vj_gui_reveal_source(sample_id, sample_type);
}

static void multitrack_resync_requested(GtkWidget *widget,
                                        gint track,
                                        gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       track == mt->project_master_track ||
       !gvr_track_test(mt->preview, track))
        return;

    multitrack_drift_reset_phase(mt, track);
    mt->drift_last_correction_us[track] = 0;
    mt->drift_source_changed_us[track] = 0;
    multitrack_drift_update_internal(mt);
    status_print(mt, "Resynchronising Video %d to the project master", track + 1);
}

static void multitrack_switcher_source_selected(GtkWidget *widget,
                                                 gint track,
                                                 gint bus,
                                                 gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;

    if(!mt || bus != 1 || track < 0 || track >= MAX_TRACKS ||
       track == mt->project_master_track ||
       !gvr_track_test(mt->preview, track))
        return;

    if(mt->active_bus == 1) {
        status_print(mt,
                     "Return to Video 1 / Bus A before selecting another Bus B source");
        multitrack_switcher_sync_ui(mt);
        return;
    }

    mt->transition_pending = 0;
    mt->transition_pending_target_track = -1;
    mt->bus_a_track = mt->project_master_track;
    mt->bus_b_track = track;

    multitrack_drift_reset_phase(mt, track);
    multitrack_switcher_prepare_inputs(mt, 1);
    multitrack_switcher_sync_ui(mt);
    status_print(mt,
                 "Bus B source set to Video %d",
                 track + 1);
}

static void multitrack_transition_requested(GtkWidget *widget,
                                             gint target_track,
                                             gint duration,
                                             gint method,
                                             gint shape,
                                             gpointer user_data)
{
    multitracker_t *mt = user_data;
    int expected_track;
    (void)widget;

    if(!mt || target_track < 0 || target_track >= MAX_TRACKS)
        return;

    expected_track = mt->active_bus == 0 ?
                     mt->bus_b_track : mt->project_master_track;
    if(target_track != expected_track ||
       !gvr_track_test(mt->preview, target_track))
        return;

    multitrack_transition_prepare(mt,
                                  duration,
                                  method,
                                  shape,
                                  target_track);
}

static void multitrack_drift_lock_toggled(GtkWidget *widget,
                                          gboolean enabled,
                                          gpointer user_data)
{
    multitracker_t *mt = user_data;
    (void)widget;

    if(!mt)
        return;

    mt->drift_lock_enabled = enabled ? 1 : 0;
    multitrack_drift_reset_all_phases(mt);
    gvr_multi_track_edit_set_drift_lock(mt->timeline, enabled);
    if(!enabled) {
        for(int track = 0; track < MAX_TRACKS; track++) {
            mt->drift_frames[track] = 0;
            mt->drift_correcting[track] = 0;
            gvr_multi_track_edit_set_track_drift(mt->timeline,
                                                  track,
                                                  0,
                                                  0,
                                                  FALSE);
        }
    }
    status_print(mt, "Drift Lock %s", enabled ? "enabled" : "disabled");
}

void multitrack_sync_start(void *data)
{
    multitracker_t *mt = data;
    struct timespec target;

    if(!mt)
        return;

    clock_gettime(CLOCK_REALTIME, &target);
    target.tv_nsec += 750000000L;
    if(target.tv_nsec >= 1000000000L) {
        target.tv_sec++;
        target.tv_nsec -= 1000000000L;
    }

    gvr_queue_mmvims(mt->preview,
                     -1,
                     VIMS_VIDEO_SYNC_START,
                     (int)target.tv_sec,
                     (int)(target.tv_nsec / 1000L));
    status_print(mt,
                 "Armed synchronized playback start on all connected VeeJay instances");
}

void multitrack_sync_simple_cmd(void *data, int vims, int arg)
{
    multitracker_t *mt = data;
    (void)arg;
    if(mt)
        gvr_queue_vims(mt->preview, -1, vims);
}

void multitrack_sync_simple_cmd2(void *data, int vims, int arg)
{
    multitracker_t *mt = data;
    if(mt)
        gvr_queue_mvims(mt->preview, -1, vims, arg);
}

void *multitrack_sync(void *data)
{
    multitracker_t *mt = data;
    sync_info *sync;

    if(!mt)
        return NULL;
    sync = gvr_sync(mt->preview, mt);
    if(sync)
        sync->master = mt->current_ui_track >= 0 ?
                       mt->current_ui_track : 0;
    return sync;
}

static char *mt_new_connection_dialog(multitracker_t *mt,
                                      int *port_num,
                                      int *error)
{
    GtkWidget *dialog;
    GtkWidget *text_entry;
    GtkWidget *num_entry;
    GtkWidget *grid;
    GtkAdjustment *adjustment;
    const char *default_host;
    int response;

    dialog = gtk_dialog_new_with_buttons(
        "Connect VeeJay as Video Track",
        GTK_WINDOW(mt->main_window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_STOCK_CANCEL,
        GTK_RESPONSE_REJECT,
        GTK_STOCK_OK,
        GTK_RESPONSE_ACCEPT,
        NULL);
    add_class(dialog, "reloaded");

    default_host = multitrack_default_host(mt);
    text_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(text_entry), default_host);
    adjustment = gtk_adjustment_new(multitrack_next_port_hint(mt,
                                                               default_host),
                                    1024,
                                    65535,
                                    5,
                                    1000,
                                    0);
    num_entry = gtk_spin_button_new(adjustment, 5.0, 0);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Hostname"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), text_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), num_entry, 1, 1, 1, 1);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_ACCEPT) {
        const char *host = gtk_entry_get_text(GTK_ENTRY(text_entry));
        char *result = strdup(host && host[0] ? host : "localhost");
        *port_num = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(num_entry));
        *error = 0;
        gtk_widget_destroy(dialog);
        return result;
    }

    *error = response;
    *port_num = 0;
    gtk_widget_destroy(dialog);
    return NULL;
}

void *multitrack_new(
        void (*f)(int,char*,int),
        int (*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
        GtkWidget *win,
        GtkWidget *box,
        GtkWidget *msg,
        GtkWidget *preview_toggle,
        gint max_w,
        gint max_h,
        GtkWidget *main_preview_area,
        void *infog,
        int threads)
{
    multitracker_t *mt = vj_calloc(sizeof(multitracker_t));
    (void)f;
    (void)g;
    (void)main_preview_area;

    mt->main_window = win;
    mt->main_box = box;
    mt->status_bar = msg;
    gtk_widget_set_hexpand(mt->main_box, TRUE);
    gtk_widget_set_vexpand(mt->main_box, TRUE);
    gtk_widget_set_halign(mt->main_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(mt->main_box, GTK_ALIGN_FILL);
    (void)preview_toggle;
    mt->data = infog;
    mt->selected = -1;
    mt->current_ui_track = -1;
    mt->pending_ui_track = -1;
    mt->project_master_track = -1;
    mt->bus_a_track = -1;
    mt->bus_b_track = -1;
    mt->active_bus = 0;
    mt->transition_pending_target_track = -1;
    mt->transition_pending_method = VJ_MULTITRACK_TRANSITION_DISSOLVE;
    mt->transition_pending_shape = 0;
    mt->drift_lock_enabled = 1;
    mt->fps = 25.0f;
    mt->quality = 0;
    for(int track = 0; track < __MAX_TRACKS; track++) {
        mt->drift_last_source[track] = -1;
        mt->drift_last_mode[track] = -1;
        mt->drift_last_origin[track] = -1;
        mt->drift_last_total[track] = -1;
        mt->track_timeline_bank[track] = -1;
        mt->track_timeline_source_fps_x100[track] = 0;
        mt->track_timeline_master_fps_x100[track] = 0;
        mt->track_timeline_master_total[track] = 0;
        mt->track_timeline_play_mode[track] = MODE_PLAIN;
        mt->track_timeline_source_id[track] = 0;
        mt->track_timeline_source_total[track] = 0;
        mt->track_timeline_speed[track] = 0;
        mt->track_timeline_loop_type[track] = 0;
    }
    mt->logo = load_logo_image();
    multitrack_get_preview_dimensions(max_w,
                                      max_h,
                                      &mt->preview_width,
                                      &mt->preview_height);

    mt->preview = gvr_preview_init(MAX_TRACKS, threads);
    mt->timeline = gvr_multi_track_edit_new(MAX_TRACKS);
    gtk_widget_set_hexpand(mt->timeline, TRUE);
    gtk_widget_set_vexpand(mt->timeline, TRUE);
    gtk_container_add(GTK_CONTAINER(mt->main_box), mt->timeline);

    g_signal_connect(mt->timeline,
                     "track-selected",
                     G_CALLBACK(multitrack_track_selected),
                     mt);
    g_signal_connect(mt->timeline,
                     "switch-requested",
                     G_CALLBACK(multitrack_switch_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "preview-toggled",
                     G_CALLBACK(multitrack_preview_toggled),
                     mt);
    g_signal_connect(mt->timeline,
                     "seek-requested",
                     G_CALLBACK(multitrack_seek_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "source-play-requested",
                     G_CALLBACK(multitrack_source_play_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "sequence-source-insert-requested",
                     G_CALLBACK(multitrack_sequence_source_insert_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "reveal-sequence-slot-requested",
                     G_CALLBACK(multitrack_reveal_sequence_slot_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "reveal-source-requested",
                     G_CALLBACK(multitrack_reveal_source_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "resync-requested",
                     G_CALLBACK(multitrack_resync_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "transition-source-selected",
                     G_CALLBACK(multitrack_switcher_source_selected),
                     mt);
    g_signal_connect(mt->timeline,
                     "transition-requested",
                     G_CALLBACK(multitrack_transition_requested),
                     mt);
    g_signal_connect(mt->timeline,
                     "drift-lock-toggled",
                     G_CALLBACK(multitrack_drift_lock_toggled),
                     mt);

    multitrack_refresh_all_tracks(mt);
    gtk_widget_show_all(mt->main_box);
    return mt;
}

int multitrack_add_track(void *data)
{
    multitracker_t *mt = data;
    int port_num = 0;
    int error = 0;
    int track = -1;
    char *hostname;

    if(!mt)
        return 0;

    hostname = mt_new_connection_dialog(mt, &port_num, &error);
    if(error || !hostname)
        return 0;

    if(gvr_track_find_open(mt->preview, hostname, port_num, &track)) {
        status_print(mt,
                     "VeeJay %s:%d is already connected as Video %d. "
                     "Select that lane and press Switch for full control.",
                     hostname,
                     port_num,
                     track + 1);
        gvr_multi_track_edit_set_selected_track(mt->timeline, track);
        mt->selected = track;
        free(hostname);
        return 0;
    }

    if(!gvr_track_connect(mt->preview, hostname, port_num, &track)) {
        status_print(mt,
                     "Unable to connect Video track to %s:%d",
                     hostname,
                     port_num);
        free(hostname);
        return 0;
    }

    mt->track_status[track] = 1;
    if(mt->project_master_track < 0)
        mt->project_master_track = track;
    if(mt->current_ui_track < 0)
        multitrack_set_current_track(mt, track, 0);
    else if(track != mt->current_ui_track &&
            !gvr_track_prepare_ui_client(mt->preview, track))
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Unable to preconnect the main UI client for Video %d",
                   track + 1);

    multitrack_preview_configure(mt, track);
    gvr_track_toggle_preview(mt->preview, track, 1);
    mt->preview_enabled[track] =
        gvr_get_preview_status(mt->preview, track);
    gvr_multi_track_edit_set_track_preview_enabled(mt->timeline,
                                                    track,
                                                    mt->preview_enabled[track]);
    if(track == mt->current_ui_track)
        multitrack_sync_main_preview_toggle(mt,
                                            mt->preview_enabled[track]);
    multitrack_refresh_all_tracks(mt);
    multitrack_switcher_prepare_inputs(mt, 1);
    gvr_multi_track_edit_set_selected_track(mt->timeline, track);
    mt->selected = track;

    status_print(mt,
                 "Connected Video %d to VeeJay %s:%d",
                 track + 1,
                 hostname,
                 port_num);
    free(hostname);
    return 1;
}

int multitrack_prepare_ui_client(void *data, int track)
{
    multitracker_t *mt = data;
    if(!mt)
        return 0;
    return gvr_track_prepare_ui_client(mt->preview, track);
}

void *multitrack_take_ui_client(void *data, int track)
{
    multitracker_t *mt = data;
    return mt ? gvr_track_take_ui_client(mt->preview, track) : NULL;
}

void multitrack_store_ui_client(void *data, int track, void *client)
{
    multitracker_t *mt = data;
    if(mt)
        gvr_track_store_ui_client(mt->preview, track, client);
}

int multitrack_get_project_master_track(void *data)
{
    multitracker_t *mt = data;
    return mt ? mt->project_master_track : -1;
}

int multitrack_get_current_ui_track(void *data)
{
    multitracker_t *mt = data;
    return mt ? mt->current_ui_track : -1;
}

int multrack_audoadd(void *data, char *hostname, int port_num)
{
    multitracker_t *mt = data;
    int track = -1;

    if(!mt || !hostname)
        return -1;

    if(!gvr_track_find_open(mt->preview, hostname, port_num, &track) &&
       !gvr_track_connect(mt->preview, hostname, port_num, &track))
        return -1;

    mt->track_status[track] = 1;
    if(mt->project_master_track < 0)
        mt->project_master_track = track;
    if(mt->current_ui_track < 0)
        multitrack_set_current_track(mt, track, 0);
    else if(track != mt->current_ui_track &&
            !gvr_track_prepare_ui_client(mt->preview, track))
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Unable to preconnect the main UI client for Video %d",
                   track + 1);

    multitrack_preview_configure(mt, track);
    gvr_track_toggle_preview(mt->preview, track, 1);
    mt->preview_enabled[track] =
        gvr_get_preview_status(mt->preview, track);
    gvr_multi_track_edit_set_track_preview_enabled(mt->timeline,
                                                    track,
                                                    mt->preview_enabled[track]);
    if(track == mt->current_ui_track)
        multitrack_sync_main_preview_toggle(mt,
                                            mt->preview_enabled[track]);
    multitrack_refresh_all_tracks(mt);
    multitrack_switcher_prepare_inputs(mt, 1);
    return track;
}

int multitrack_get_track_status(void *data, int track)
{
    multitracker_t *mt = data;
    if(!mt || track < 0 || track >= __MAX_TRACKS)
        return 0;
    return mt->track_status[track];
}

void multitrack_cleanup_track(void *data, int track)
{
    multitracker_t *mt = data;
    int replacement;

    if(!mt || track < 0 || track >= MAX_TRACKS)
        return;

    if(track == mt->bus_a_track || track == mt->bus_b_track ||
       track == mt->transition_pending_target_track) {
        mt->transition_pending = 0;
        mt->transition_pending_target_track = -1;
    }

    if(((mt->active_bus == 0 && track == mt->bus_a_track) ||
        (mt->active_bus == 1 && track == mt->bus_b_track)) &&
       track != mt->project_master_track &&
       mt->project_master_track >= 0 &&
       gvr_track_test(mt->preview, mt->project_master_track)) {
        gvr_queue_mmmmvims(mt->preview,
                           mt->project_master_track,
                           VIMS_VIDEO_TRANSITION_TAKE,
                           0,
                           0,
                           VJ_MULTITRACK_TRANSITION_DISSOLVE,
                           0);
        status_print(mt,
                     "On-air Video %d disconnected; cutting safely to the project master",
                     track + 1);
    }

    mt->track_status[track] = 0;
    mt->preview_enabled[track] = 0;
    mt->status_lock[track] = 0;
    if(mt->pending_ui_track == track)
        mt->pending_ui_track = -1;
    veejay_memset(mt->status_cache[track],
                  0,
                  sizeof(mt->status_cache[track]));
    mt->sync_frame[track] = 0;
    mt->sync_origin[track] = 0;
    mt->sync_total[track] = 0;
    mt->sync_speed[track] = 0;
    mt->sync_play_mode[track] = MODE_PLAIN;
    mt->sync_fps_x100[track] = 0;
    mt->sync_status_us[track] = 0;
    mt->switcher_input_last_bind_us[track] = 0;
    mt->drift_frames[track] = 0;
    mt->drift_correcting[track] = 0;
    mt->drift_last_source[track] = -1;
    mt->drift_last_mode[track] = -1;
    mt->drift_last_origin[track] = -1;
    mt->drift_last_total[track] = -1;
    mt->drift_source_changed_us[track] = 0;
    mt->drift_last_correction_us[track] = 0;
    mt->drift_error_sign[track] = 0;
    mt->drift_error_samples[track] = 0;
    multitrack_drift_reset_phase(mt, track);
    mt->track_timeline_bank[track] = -1;
    mt->track_timeline_revision[track] = 0;
    mt->track_timeline_source_fps_x100[track] = 0;
    mt->track_timeline_master_fps_x100[track] = 0;
    mt->track_timeline_master_total[track] = 0;
    mt->track_timeline_clips_active[track] = 0;
    mt->track_timeline_play_mode[track] = MODE_PLAIN;
    mt->track_timeline_source_id[track] = 0;
    mt->track_timeline_source_total[track] = 0;
    mt->track_timeline_speed[track] = 0;
    mt->track_timeline_loop_type[track] = 0;
    veejay_memset(&mt->track_timeline[track],
                  0,
                  sizeof(mt->track_timeline[track]));
    gvr_multi_track_edit_clear_track(mt->timeline, track);

    if(mt->current_ui_track == track) {
        replacement = multitrack_first_open(mt, track);
        mt->current_ui_track = replacement;
        mt->selected = replacement;
        if(replacement >= 0)
            gvr_set_master(mt->preview, replacement);
    }

    if(mt->project_master_track == track) {
        mt->project_master_track = multitrack_first_open(mt, track);
        mt->bus_a_track = mt->project_master_track;
        mt->bus_b_track = multitrack_first_other(mt, mt->bus_a_track);
        mt->active_bus = 0;
        veejay_memset(mt->switcher_input_last_bind_us,
                      0,
                      sizeof(mt->switcher_input_last_bind_us));
        mt->switcher_track_list_request_us = 0;
        multitrack_drift_reset_all_phases(mt);
        multitrack_switcher_prepare_inputs(mt, 1);
    }
    else {
        if(mt->bus_a_track == track)
            mt->bus_a_track = mt->project_master_track;
        if(mt->bus_b_track == track || mt->bus_b_track == mt->bus_a_track)
            mt->bus_b_track = multitrack_first_other(mt, mt->bus_a_track);
        if(mt->active_bus == 1 && mt->bus_b_track < 0)
            mt->active_bus = 0;
    }

    multitrack_refresh_all_tracks(mt);
}

void multitrack_close_track(void *data)
{
    multitracker_t *mt = data;

    if(!mt || mt->selected < 0 || mt->selected >= MAX_TRACKS)
        return;

    if(mt->selected == mt->current_ui_track) {
        status_print(mt,
                     "Video %d currently owns the main Reloaded controls. "
                     "Use Switch on another connected lane before removing it.",
                     mt->selected + 1);
        return;
    }

    gvr_track_disconnect(mt->preview, mt->selected);
    multitrack_cleanup_track(mt, mt->selected);
}

void multitrack_close_tracks(void *data)
{
    multitracker_t *mt = data;
    if(!mt)
        return;

    for(int i = 0; i < MAX_TRACKS; i++) {
        gvr_track_disconnect(mt->preview, i);
        multitrack_cleanup_track(mt, i);
    }
    mt->selected = -1;
    mt->current_ui_track = -1;
    mt->pending_ui_track = -1;
    mt->project_master_track = -1;
    multitrack_refresh_all_tracks(mt);
}

void multitrack_disconnect(void *data)
{
    multitracker_t *mt = data;
    if(!mt || mt->current_ui_track < 0)
        return;
    int track = mt->current_ui_track;
    gvr_track_disconnect(mt->preview, track);
    multitrack_cleanup_track(mt, track);
}

void multitrack_set_master_track(void *data, int track)
{
    multitracker_t *mt = data;
    if(!mt)
        return;
    multitrack_set_current_track(mt, track, 0);
    if(mt->project_master_track < 0)
        mt->project_master_track = track;
    multitrack_refresh_all_tracks(mt);
}

void multitrack_set_project_master(void *data, int track)
{
    multitracker_t *mt = data;
    int old_master;

    if(!mt || track < 0 || track >= MAX_TRACKS ||
       !gvr_track_test(mt->preview, track))
        return;

    old_master = mt->project_master_track;
    if(old_master >= 0 && old_master != track &&
       gvr_track_test(mt->preview, old_master))
        gvr_queue_mmmmvims(mt->preview,
                           old_master,
                           VIMS_VIDEO_TRANSITION_TAKE,
                           0,
                           0,
                           VJ_MULTITRACK_TRANSITION_DISSOLVE,
                           0);

    mt->project_master_track = track;
    mt->bus_a_track = track;
    mt->active_bus = 0;
    mt->transition_pending = 0;
    mt->transition_pending_target_track = -1;
    multitrack_drift_reset_all_phases(mt);
    if(mt->bus_b_track == track ||
       mt->bus_b_track < 0 ||
       !gvr_track_test(mt->preview, mt->bus_b_track))
        mt->bus_b_track = multitrack_first_other(mt, track);
    gvr_multi_track_edit_set_project_master(mt->timeline, track);
    veejay_memset(mt->switcher_input_last_bind_us,
                  0,
                  sizeof(mt->switcher_input_last_bind_us));
    mt->switcher_track_list_request_us = 0;
    multitrack_switcher_prepare_inputs(mt, 1);
    multitrack_switcher_sync_ui(mt);
    multitrack_refresh_all_tracks(mt);
}

void multitrack_set_shape_catalog(void *data,
                                  const char *const *names,
                                  unsigned int count)
{
    multitracker_t *mt = data;

    if(!mt || !mt->timeline)
        return;

    gvr_multi_track_edit_set_shape_catalog(mt->timeline, names, count);
}

int multitrack_locked(void *data)
{
    multitracker_t *mt = data;
    if(!mt || mt->current_ui_track < 0 ||
       mt->current_ui_track >= MAX_TRACKS)
        return 1;
    return mt->status_lock[mt->current_ui_track];
}

void multitrack_configure(void *data,
                          float fps,
                          int video_width,
                          int video_height,
                          int *box_w,
                          int *box_h)
{
    multitracker_t *mt = data;

    if(!mt)
        return;

    mt->fps = fps > 0.0f ? fps : 25.0f;
    mt->width = box_w && *box_w > 0 ? *box_w : vj_get_preview_box_w();
    mt->height = box_h && *box_h > 0 ? *box_h : vj_get_preview_box_h();
    mt->width = MAX(16, mt->width & ~1);
    mt->height = MAX(16, mt->height & ~1);
    mt->aspect_ratio = video_height > 0 ?
        (float)video_width / (float)video_height : 1.0f;

    multitrack_get_preview_dimensions(video_width,
                                      video_height,
                                      &mt->preview_width,
                                      &mt->preview_height);
    for(int i = 0; i < MAX_TRACKS; i++)
        multitrack_preview_configure(mt, i);
}

void multitrack_set_quality(void *data, int quality)
{
    multitracker_t *mt = data;
    if(!mt)
        return;
    mt->quality = CLAMP(quality, 0, 3);
    for(int i = 0; i < MAX_TRACKS; i++)
        multitrack_preview_configure(mt, i);
}

void multitrack_resize(void *data, int w, int h)
{
    multitracker_t *mt = data;
    (void)w;
    (void)h;
    if(mt && mt->timeline)
        gtk_widget_queue_resize(mt->timeline);
}

void multitrack_set_logo(void *data, GtkWidget *img)
{
    (void)data;
    (void)img;
}

void multitrack_toggle_preview(void *data,
                               int track_id,
                               int status,
                               GtkWidget *img)
{
    multitracker_t *mt = data;
    int target;

    if(!mt)
        return;

    target = track_id == -1 ? mt->current_ui_track : track_id;
    if(target < 0 || target >= MAX_TRACKS)
        return;

    if(status)
        multitrack_preview_configure(mt, target);

    if(gvr_track_toggle_preview(mt->preview, target, status)) {
        mt->preview_enabled[target] = status ? 1 : 0;
        gvr_multi_track_edit_set_track_preview_enabled(mt->timeline,
                                                       target,
                                                       status);
    }

    (void)img;
}

void multitrack_release_track(void *data, int id, int release_this)
{
    multitracker_t *mt = data;
    int stream_id;

    if(!mt)
        return;
    stream_id = gvr_get_stream_id_for(mt->preview, id, release_this);
    if(stream_id > 0)
        gvr_queue_mvims(mt->preview, id, VIMS_STREAM_DELETE, stream_id);
}

void multitrack_bind_track(void *data, int id, int bind_this)
{
    multitracker_t *mt = data;
    char *host;
    int port;

    if(!mt || bind_this < 0 || bind_this >= MAX_TRACKS ||
       id < 0 || id >= MAX_TRACKS)
        return;

    host = gvr_track_get_hostname(mt->preview, bind_this);
    port = gvr_track_get_portnum(mt->preview, bind_this);
    if(host && port > 0) {
        gvr_queue_cxvims(mt->preview,
                         id,
                         VIMS_STREAM_NEW_UNICAST,
                         port,
                         (unsigned char *)host);
        gvr_need_track_list(mt->preview, id);
    }
}

static int multitrack_timeline_master_total(const multitracker_t *mt,
                                            int fallback)
{
    int total;

    if(!mt)
        return MAX(1, fallback);

    total = mt->timeline_total_frames;
    if(total <= 0) {
        for(unsigned int i = 0; i < mt->master_clip_count; i++)
            total = MAX(total, mt->master_clips[i].project_out + 1);
    }
    if(total <= 0)
        total = fallback;
    return MAX(0, total);
}

static void multitrack_clear_track_timeline(multitracker_t *mt, int track)
{
    if(!mt || track < 0 || track >= MAX_TRACKS)
        return;

    if(mt->track_timeline_clips_active[track])
        gvr_multi_track_edit_set_track_clips(mt->timeline, track, NULL, 0);

    mt->track_timeline_clips_active[track] = 0;
    mt->track_timeline_bank[track] = -1;
    mt->track_timeline_revision[track] = 0;
    mt->track_timeline_source_fps_x100[track] = 0;
    mt->track_timeline_master_fps_x100[track] = 0;
    mt->track_timeline_master_total[track] = 0;
    mt->track_timeline_play_mode[track] = MODE_PLAIN;
    mt->track_timeline_source_id[track] = 0;
    mt->track_timeline_source_total[track] = 0;
    mt->track_timeline_speed[track] = 0;
    mt->track_timeline_loop_type[track] = 0;
    veejay_memset(&mt->track_timeline[track],
                  0,
                  sizeof(mt->track_timeline[track]));
}

static int multitrack_direct_source_frames(const int *status, int play_mode)
{
    int total;

    if(multitrack_mode_is_sample(play_mode)) {
        int start = status[SAMPLE_START];
        int end = status[SAMPLE_END];
        if(end < start) {
            const int swap = start;
            start = end;
            end = swap;
        }
        return MAX(0, end - start + 1);
    }

    if(play_mode != MODE_STREAM)
        return 0;

    if(seq_stream_buffer_ready_status(status))
        return MAX(0, status[STREAM_BUFFER_FILLED]);

    if(seq_stream_buffer_supported_status(status)) {
        total = status[STREAM_BUFFER_CAPACITY];
        if(total > 0)
            return total;
    }

    return MAX(0, status[TOTAL_FRAMES]);
}

static int multitrack_direct_speed(const int *status, int play_mode)
{
    int speed = status[SAMPLE_SPEED];

    if(play_mode == MODE_STREAM && seq_stream_buffer_ready_status(status)) {
        speed = status[STREAM_BUFFER_SPEED];
        if(status[STREAM_BUFFER_DIRECTION] < 0 && speed > 0)
            speed = -speed;
    }
    return speed;
}

static int multitrack_direct_master_total(const multitracker_t *mt)
{
    const int master = mt ? mt->project_master_track : -1;
    const int *status;
    int play_mode;
    int source_total;
    int speed_abs;
    long long effective_source_frames;

    if(!mt || master < 0 || master >= MAX_TRACKS)
        return 0;

    status = mt->status_cache[master];
    play_mode = status[PLAY_MODE];
    if(status[CURRENT_ID] <= 0 || status[SEQ_ACT] ||
       !multitrack_mode_is_sample(play_mode))
        return 0;

    source_total = multitrack_direct_source_frames(status, play_mode);
    if(source_total <= 0)
        return 0;

    speed_abs = MAX(1, ABS(multitrack_direct_speed(status, play_mode)));
    effective_source_frames =
        ((long long)source_total + speed_abs - 1LL) / speed_abs;

    return multitrack_rescale_frame_count(
        effective_source_frames,
        multitrack_track_fps_x100(mt, master),
        multitrack_track_fps_x100(mt, master));
}

static void multitrack_update_direct_track_timeline(multitracker_t *mt,
                                                     int track,
                                                     const int *status)
{
    if(!mt || !status || track < 0 || track >= MAX_TRACKS ||
       track >= __MAX_TRACKS)
        return;

    GvrMultiTrackClip clip;
    char title[64];
    const int play_mode = status[PLAY_MODE];
    const int source_id = status[CURRENT_ID];
    const int source_fps_x100 = multitrack_track_fps_x100(mt, track);
    const int master_fps_x100 =
        multitrack_track_fps_x100(mt, mt->project_master_track);
    const int source_total = multitrack_direct_source_frames(status, play_mode);
    const int speed = multitrack_direct_speed(status, play_mode);
    const int loop_type = status[SAMPLE_LOOP];
    const int speed_abs = MAX(1, ABS(speed));
    int project_length;
    int master_total;
    int repeats;

    if(source_id <= 0 || !multitrack_mode_has_source_timeline(play_mode)) {
        multitrack_clear_track_timeline(mt, track);
        return;
    }

    if(play_mode == MODE_STREAM) {
        multitrack_clear_track_timeline(mt, track);
        return;
    }

    if(source_total > 0) {
        const long long effective_source_frames =
            ((long long)source_total + speed_abs - 1LL) / speed_abs;
        project_length = multitrack_rescale_frame_count(effective_source_frames,
                                                        source_fps_x100,
                                                        master_fps_x100);
    }
    else {
        project_length = 0;
    }

    master_total = multitrack_timeline_master_total(mt, 0);
    master_total = MAX(master_total, multitrack_direct_master_total(mt));
    if(master_total <= 0)
        master_total = MAX(1, project_length);
    if(project_length <= 0)
        project_length = master_total;

    if(mt->track_timeline_clips_active[track] &&
       mt->track_timeline_bank[track] == -1 &&
       mt->track_timeline_play_mode[track] == play_mode &&
       mt->track_timeline_source_id[track] == source_id &&
       mt->track_timeline_source_total[track] == source_total &&
       mt->track_timeline_speed[track] == speed &&
       mt->track_timeline_loop_type[track] == loop_type &&
       mt->track_timeline_source_fps_x100[track] == source_fps_x100 &&
       mt->track_timeline_master_fps_x100[track] == master_fps_x100 &&
       mt->track_timeline_master_total[track] == master_total)
        return;

    repeats = source_total > 0 &&
              (multitrack_mode_is_sample(play_mode) ||
               multitrack_loop_repeats(loop_type));

    veejay_memset(&clip, 0, sizeof(clip));
    snprintf(title,
             sizeof(title),
             "%s %d",
             play_mode == MODE_STREAM ? "Stream" : "Sample",
             source_id);
    clip.id = (guint)source_id;
    clip.sample_id = source_id;
    clip.sample_type = play_mode == MODE_STREAM ? MODE_STREAM : MODE_SAMPLE;
    clip.project_in = 0;
    clip.project_out = MIN(project_length, master_total) - 1;
    clip.source_in = 0;
    clip.source_length = project_length;
    clip.repeat_period = repeats ? project_length : 0;
    clip.repeat_until = repeats ? master_total - 1 : clip.project_out;
    clip.speed = speed;
    clip.loop_type = loop_type;
    clip.title = title;

    gvr_multi_track_edit_set_track_clips(mt->timeline, track, &clip, 1);
    mt->track_timeline_clips_active[track] = 1;
    mt->track_timeline_bank[track] = -1;
    mt->track_timeline_revision[track] = 0;
    mt->track_timeline_source_fps_x100[track] = source_fps_x100;
    mt->track_timeline_master_fps_x100[track] = master_fps_x100;
    mt->track_timeline_master_total[track] = master_total;
    mt->track_timeline_play_mode[track] = play_mode;
    mt->track_timeline_source_id[track] = source_id;
    mt->track_timeline_source_total[track] = source_total;
    mt->track_timeline_speed[track] = speed;
    mt->track_timeline_loop_type[track] = loop_type;
    veejay_memset(&mt->track_timeline[track],
                  0,
                  sizeof(mt->track_timeline[track]));
}

static void multitrack_update_track_timeline(multitracker_t *mt,
                                             int track,
                                             const int *status,
                                             int sequence_bank)
{
    gvr_sequence_timeline_t timeline;
    unsigned int revision;
    int source_fps_x100;
    int master_fps_x100;
    int master_total;
    int source_total;
    int repeat_period;
    GvrMultiTrackClip clips[GVR_SEQUENCE_TIMELINE_MAX_SLOTS];
    char titles[GVR_SEQUENCE_TIMELINE_MAX_SLOTS][64];

    if(!mt || !status || track < 0 || track >= MAX_TRACKS)
        return;

    if(!status[SEQ_ACT]) {
        multitrack_update_direct_track_timeline(mt, track, status);
        return;
    }

    if(track == mt->project_master_track) {
        multitrack_clear_track_timeline(mt, track);
        return;
    }

    if(mt->track_timeline_clips_active[track] &&
       mt->track_timeline_bank[track] == -1) {
        gvr_multi_track_edit_set_track_clips(mt->timeline, track, NULL, 0);
        mt->track_timeline_clips_active[track] = 0;
    }

    revision = (unsigned int)status[STATUS_SEQUENCE_BANK0_REVISION + sequence_bank];
    source_fps_x100 = multitrack_track_fps_x100(mt, track);
    master_fps_x100 =
        multitrack_track_fps_x100(mt, mt->project_master_track);
    master_total = multitrack_timeline_master_total(mt, 0);

    if(mt->track_timeline_bank[track] != sequence_bank ||
       (revision > 0 && mt->track_timeline_revision[track] != revision))
        gvr_need_sequence_timeline(mt->preview, track, sequence_bank);

    if(!gvr_get_sequence_timeline(mt->preview, track, &timeline) ||
       !timeline.valid || timeline.bank != sequence_bank)
        return;

    mt->track_timeline[track] = timeline;
    source_total = timeline.total_frames;
    if(source_total <= 0) {
        for(unsigned int i = 0; i < timeline.count; i++) {
            if(timeline.clips[i].project_out < INT_MAX)
                source_total = MAX(source_total,
                                   timeline.clips[i].project_out + 1);
            else
                source_total = INT_MAX;
        }
    }
    source_total = MAX(1, source_total);

    if(master_total <= 0) {
        master_total = multitrack_rescale_frame_count(source_total,
                                                      source_fps_x100,
                                                      master_fps_x100);
    }

    if(mt->track_timeline_clips_active[track] &&
       mt->track_timeline_bank[track] == sequence_bank &&
       mt->track_timeline_revision[track] == timeline.revision &&
       mt->track_timeline_source_fps_x100[track] == source_fps_x100 &&
       mt->track_timeline_master_fps_x100[track] == master_fps_x100 &&
       mt->track_timeline_master_total[track] == master_total)
        return;

    repeat_period = multitrack_rescale_frame_count(source_total,
                                                   source_fps_x100,
                                                   master_fps_x100);

    for(unsigned int i = 0; i < timeline.count; i++) {
        const gvr_sequence_timeline_clip_t *source = &timeline.clips[i];
        GvrMultiTrackClip *clip = &clips[i];
        const int project_in = multitrack_rescale_frame_count(source->project_in,
                                                               source_fps_x100,
                                                               master_fps_x100);
        int project_out_exclusive = multitrack_rescale_frame_count(
            (long long)source->project_out + 1LL,
            source_fps_x100,
            master_fps_x100);

        if(project_out_exclusive <= project_in)
            project_out_exclusive = project_in + 1;

        snprintf(titles[i],
                 sizeof(titles[i]),
                 "#%d - %s %d",
                 source->slot + 1,
                 source->sample_type == 0 ? "Sample" : "Stream",
                 source->sample_id);
        clip->id = (guint)source->slot;
        clip->sample_id = source->sample_id;
        clip->sample_type = source->sample_type;
        clip->project_in = project_in;
        clip->project_out = project_out_exclusive - 1;
        clip->source_in = 0;
        clip->source_length = project_out_exclusive - project_in;
        clip->repeat_period = repeat_period;
        clip->repeat_until = master_total - 1;
        clip->speed = 1;
        clip->loop_type = 1;
        clip->title = titles[i];
    }

    gvr_multi_track_edit_set_track_clips(mt->timeline,
                                         track,
                                         clips,
                                         timeline.count);
    mt->track_timeline_clips_active[track] = 1;
    mt->track_timeline_bank[track] = sequence_bank;
    mt->track_timeline_revision[track] = timeline.revision;
    mt->track_timeline_source_fps_x100[track] = source_fps_x100;
    mt->track_timeline_master_fps_x100[track] = master_fps_x100;
    mt->track_timeline_master_total[track] = master_total;
    mt->track_timeline_play_mode[track] = status[PLAY_MODE];
    mt->track_timeline_source_id[track] = status[CURRENT_ID];
    mt->track_timeline_source_total[track] = source_total;
    mt->track_timeline_speed[track] = status[SAMPLE_SPEED];
    mt->track_timeline_loop_type[track] = status[SAMPLE_LOOP];
}

int update_multitrack_widgets(void *data, int *array, int track)
{
    multitracker_t *mt = data;
    int play_mode;
    int source_type;
    int frame;
    int total;
    int speed;
    int sequence_bank;
    int sequence_slot;

    if(!mt || !array || track < 0 || track >= MAX_TRACKS)
        return 0;

    mt->status_lock[track] = 1;
    veejay_memcpy(mt->status_cache[track],
                  array,
                  sizeof(int) * VJ_STATUS_ARRAY_SIZE);

    play_mode = array[PLAY_MODE];
    source_type = play_mode == MODE_STREAM ? array[STREAM_TYPE] : 0;
    frame = array[FRAME_NUM];
    total = array[TOTAL_FRAMES];
    speed = array[SAMPLE_SPEED];
    mt->sync_origin[track] = 0;

    if(multitrack_mode_is_sample(play_mode)) {
        int start = array[SAMPLE_START];
        int end = array[SAMPLE_END];
        if(end < start) {
            int swap = start;
            start = end;
            end = swap;
        }
        mt->sync_origin[track] = start;
        frame -= start;
        total = end - start + 1;
        if(frame < 0)
            frame = 0;
        else if(total > 0 && frame >= total)
            frame = total - 1;
    }
    else if(play_mode == MODE_STREAM && seq_stream_buffer_ready_status(array)) {
        frame = array[STREAM_BUFFER_POSITION];
        total = array[STREAM_BUFFER_FILLED];
        speed = array[STREAM_BUFFER_SPEED];
        if(array[STREAM_BUFFER_DIRECTION] < 0 && speed > 0)
            speed = -speed;
    }

    {
        const gint64 status_now = g_get_monotonic_time();
        const int identity_origin =
            multitrack_mode_is_sample(play_mode) ? mt->sync_origin[track] : 0;
        const int identity_total =
            multitrack_mode_is_sample(play_mode) ? total :
            (play_mode == MODE_STREAM ?
                MAX(0, array[STREAM_BUFFER_CAPACITY]) : 0);

        if(mt->drift_last_source[track] != array[CURRENT_ID] ||
           mt->drift_last_mode[track] != play_mode ||
           mt->drift_last_origin[track] != identity_origin ||
           mt->drift_last_total[track] != identity_total) {
            mt->drift_last_source[track] = array[CURRENT_ID];
            mt->drift_last_mode[track] = play_mode;
            mt->drift_last_origin[track] = identity_origin;
            mt->drift_last_total[track] = identity_total;
            mt->drift_source_changed_us[track] = status_now;
            multitrack_drift_reset_phase(mt, track);
        }

        mt->sync_frame[track] = frame;
        mt->sync_total[track] = total;
        mt->sync_speed[track] = speed;
        mt->sync_play_mode[track] = play_mode;
        mt->sync_fps_x100[track] = array[CURRENT_FPS];
        mt->sync_status_us[track] = status_now;
    }

    sequence_bank = array[STATUS_SEQUENCE_ACTIVE_BANK];
    if(sequence_bank < 0 || sequence_bank >= 4)
        sequence_bank = 0;
    sequence_slot = array[SEQ_CUR];
    multitrack_update_track_timeline(mt, track, array, sequence_bank);

    gvr_multi_track_edit_set_track_status(
        mt->timeline,
        track,
        play_mode,
        array[CURRENT_ID],
        source_type,
        frame,
        total,
        speed,
        array[CURRENT_FPS],
        array[SAMPLE_LOOP],
        array[SAMPLE_FX] != 0,
        array[AUDIO_MUTED] != 0,
        array[SEQ_ACT] != 0,
        sequence_bank,
        sequence_slot,
        seq_stream_buffer_supported_status(array),
        array[STREAM_BUFFER_ENABLED] > 0,
        MAX(0, array[STREAM_BUFFER_CAPACITY]),
        MAX(0, array[STREAM_BUFFER_FILLED]),
        MAX(0, array[STREAM_BUFFER_POSITION]));

    if(track == mt->current_ui_track)
        multitrack_refresh_connection_topology(mt);

    if(track == mt->project_master_track) {
        gvr_multi_track_edit_set_seekability(
            mt->timeline,
            multitrack_status_seekable(array),
            multitrack_seek_unavailable_reason(array));
        const int transition_active =
            array[VJ_STATUS_MULTITRACK_TRANSITION_ACTIVE] != 0;
        const int program_stream =
            array[VJ_STATUS_MULTITRACK_PROGRAM_STREAM];
        const int target_stream =
            array[VJ_STATUS_MULTITRACK_PREVIEW_STREAM];
        int program_track = multitrack_track_for_stream(mt, program_stream);
        int target_track = multitrack_track_for_stream(mt, target_stream);

        if(program_stream > 0 && program_track < 0)
            gvr_need_track_list(mt->preview, mt->project_master_track);
        else if(program_track >= 0) {
            mt->bus_a_track = mt->project_master_track;
            if(program_track == mt->project_master_track)
                mt->active_bus = 0;
            else {
                mt->bus_b_track = program_track;
                mt->active_bus = 1;
            }
        }

        if(transition_active && target_track >= 0 &&
           target_track != mt->project_master_track)
            mt->bus_b_track = target_track;

        multitrack_switcher_sync_ui(mt);
        gvr_multi_track_edit_set_transition_state(
            mt->timeline,
            transition_active,
            array[VJ_STATUS_MULTITRACK_TRANSITION_PROGRESS]);
    }

    if(track == mt->project_master_track)
        multitrack_switcher_prepare_inputs(mt, 0);
    multitrack_transition_pending_tick(mt);
    mt->status_lock[track] = 0;
    return 1;
}

void multitrack_update_sequence_image(void *data,
                                      int track,
                                      GdkPixbuf *img)
{
    multitracker_t *mt = data;
    if(mt && track >= 0 && track < MAX_TRACKS &&
       mt->preview_enabled[track])
        gvr_multi_track_edit_set_track_preview(mt->timeline, track, img);
}

int multitrack_get_sequence_view_id(void *data)
{
    (void)data;
    return -1;
}

void multitrack_set_master_timeline(void *data,
                                    int bank,
                                    unsigned int revision,
                                    int total_frames,
                                    double fps,
                                    const multitrack_master_clip_t *clips,
                                    unsigned int count)
{
    multitracker_t *mt = data;
    GvrMultiTrackMasterClip converted[120];
    unsigned int copy_count;

    if(!mt)
        return;

    copy_count = MIN(count, (unsigned int)G_N_ELEMENTS(converted));
    mt->timeline_bank = bank;
    mt->timeline_revision = revision;
    mt->timeline_total_frames = total_frames;
    mt->master_clip_count = copy_count;
    for(unsigned int i = 0; i < copy_count; i++) {
        mt->master_clips[i] = clips[i];
        converted[i].slot = clips[i].slot;
        converted[i].sample_id = clips[i].sample_id;
        converted[i].sample_type = clips[i].sample_type;
        converted[i].project_in = clips[i].project_in;
        converted[i].project_out = clips[i].project_out;
    }

    gvr_multi_track_edit_set_project(mt->timeline,
                                     bank,
                                     revision,
                                     total_frames,
                                     fps > 0.0 ? fps : mt->fps);
    gvr_multi_track_edit_set_master_clips(mt->timeline,
                                          converted,
                                          copy_count);
}

void multitrack_set_project_position(void *data,
                                     int bank,
                                     int slot,
                                     int project_frame,
                                     int active)
{
    multitracker_t *mt = data;
    if(mt)
        gvr_multi_track_edit_set_playhead(mt->timeline,
                                          bank,
                                          slot,
                                          project_frame,
                                          active ? TRUE : FALSE);
}
