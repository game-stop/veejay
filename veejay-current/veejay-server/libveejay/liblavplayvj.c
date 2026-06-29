/* liblavplayvj - an extended librarified Linux Audio Video playback/Editing
 * VJ'fied by	Niels Elburg <nwelburg@gmail.com>
 *
 *
 * libveejay - a librarified Linux Audio Video PLAYback
 *
 * Copyright (C) 2000 Rainer Johanni <Rainer@Johanni.de>
 * Extended by:   Gernot Ziegler  <gz@lysator.liu.se>
 *                Ronald Bultje   <rbultje@ronald.bitfreak.net>
 *              & many others
 *
 * A library for playing back MJPEG video via software MJPEG
 * decompression (using SDL) 
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <config.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <math.h>
#include "jpegutils.h"
#include "vj-event.h"
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#ifdef HAVE_FREETYPE
#include <libveejay/vj-font.h>
#endif
#ifndef X_DISPLAY_MISSING
#include <libveejay/x11misc.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#endif
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/atomic.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-misc.h>
#include <libveejay/vj-perform.h>
#include <libveejay/vj-plug.h>
#include <libveejay/vj-lib.h>
#include <libveejay/vj-sdl.h>
#include <libel/vj-avcodec.h>
#include <libel/pixbuf.h>
#include <veejaycore/avcommon.h>
#include <veejaycore/vj-client.h>
#include <libveejay/audioscratcher.h>
#ifdef HAVE_JACK
#include <libveejay/vj-jack.h>
#include <libveejay/vj-audio-sync.h>
#include <libveejay/vj-audio-beat.h>
#endif
#include <veejaycore/yuvconv.h>
#include <libveejay/vj-composite.h>
#include <libveejay/vj-viewport.h>
#include <libveejay/vj-OSC.h>
#include <veejaycore/vj-task.h>
#include <libveejay/vj-split.h>
#include <libveejay/vj-macro.h>
#include <libveejay/vjkf.h>
#include <libplugger/plugload.h>
#include <libstream/vj-vloopback.h>
#include <veejaycore/vims.h>
#include <libqrwrap/qrwrapper.h>
#include <sched.h>
#include <libveejay/vj-shm.h>
#include <pthread.h>
#include <signal.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <libstream/vj-tag.h>
#include "libveejay.h"
#include <veejaycore/mjpeg_types.h>
#include "vj-perform.h"
#include <veejaycore/vj-server.h>
#ifdef HAVE_SDL
#include <SDL2/SDL.h>
#endif

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef HAVE_V4L2
#include <libstream/vj-vloopback.h>
#endif

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif
#define HZ 100
#define VJ_DYNAMIC_FPS_MIN 1.0f
#define VJ_DYNAMIC_FPS_MAX 240.0f
#define VJ_DYNAMIC_ALLOC_MIN_FPS 1.0
#include <libel/vj-el.h>

#include <libvje/libvje.h>
#include <libvje/effects/shapewipe.h>

#include <omp.h>

#define VALUE_NOT_FILLED -10000

#ifndef VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS
#define VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS 1000
#endif
#ifndef VJ_TRACK_ALIGN_SNAP_CONSUME_MIN_CONF
#define VJ_TRACK_ALIGN_SNAP_CONSUME_MIN_CONF 60
#endif
#ifndef VJ_TRACK_ALIGN_SNAP_CONSUME_COOLDOWN_MS
#define VJ_TRACK_ALIGN_SNAP_CONSUME_COOLDOWN_MS 700L
#endif
#ifndef VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_MIN_CONF
#define VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_MIN_CONF 58
#endif
#ifndef VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_COOLDOWN_MS
#define VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_COOLDOWN_MS 450L
#endif
#ifndef VJ_TRACK_ALIGN_AUDIO_GUARD_BACKWARD_MS
#define VJ_TRACK_ALIGN_AUDIO_GUARD_BACKWARD_MS 72L
#endif
#ifndef VJ_TRACK_ALIGN_AUDIO_GUARD_FORWARD_MS
#define VJ_TRACK_ALIGN_AUDIO_GUARD_FORWARD_MS 36L
#endif
#ifndef VJ_TRACK_ALIGN_AUDIO_GUARD_MAX_MS
#define VJ_TRACK_ALIGN_AUDIO_GUARD_MAX_MS 480L
#endif

#ifndef VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_MIN_BLOCKS
#define VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_MIN_BLOCKS 2.0
#endif
#ifndef VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_JITTER_FLOOR_MS
#define VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_JITTER_FLOOR_MS 8.0
#endif
#ifndef VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_COOLDOWN_MS
#define VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_COOLDOWN_MS 220L
#endif

#ifndef VJ_WAV_PLAIN_LOCK_MIN_CONF
#define VJ_WAV_PLAIN_LOCK_MIN_CONF 82
#endif
#ifndef VJ_WAV_PLAIN_LOCK_START_WINDOW_MS
#define VJ_WAV_PLAIN_LOCK_START_WINDOW_MS 250
#endif

#ifndef VJ_AUDIO_PRODUCER_QUEUE_LOW_WATER_FRACTION
#define VJ_AUDIO_PRODUCER_QUEUE_LOW_WATER_FRACTION 0.35
#endif
#ifndef VJ_AUDIO_PRODUCER_QUEUE_MIN_LOW_WATER_MS
#define VJ_AUDIO_PRODUCER_QUEUE_MIN_LOW_WATER_MS 4.0
#endif
#ifndef VJ_AUDIO_PRODUCER_QUEUE_MAX_LOW_WATER_MS
#define VJ_AUDIO_PRODUCER_QUEUE_MAX_LOW_WATER_MS 18.0
#endif
#ifndef VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_FRACTION
#define VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_FRACTION 0.85
#endif
#ifndef VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_MS
#define VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_MS 30.0
#endif

#ifndef VJ_AUDIO_PRODUCER_LIVE_LOW_WATER_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_LOW_WATER_FRACTION 0.50
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_TARGET_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_TARGET_FRACTION 1.25
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_HIGH_WATER_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_HIGH_WATER_FRACTION 2.25
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_UNDER_SLEEP_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_UNDER_SLEEP_FRACTION 0.25
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_STEADY_SLEEP_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_STEADY_SLEEP_FRACTION 0.75
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_HIGH_SLEEP_FRACTION
#define VJ_AUDIO_PRODUCER_LIVE_HIGH_SLEEP_FRACTION 1.00
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_MIN_SLEEP_MS
#define VJ_AUDIO_PRODUCER_LIVE_MIN_SLEEP_MS 3.0
#endif
#ifndef VJ_AUDIO_PRODUCER_LIVE_MAX_SLEEP_MS
#define VJ_AUDIO_PRODUCER_LIVE_MAX_SLEEP_MS 42.0
#endif
#ifndef VJ_AUDIO_PRODUCER_PACE_BUDGET_LOG_MS
#define VJ_AUDIO_PRODUCER_PACE_BUDGET_LOG_MS 650L
#endif

extern void vj_osc_set_veejay_t(veejay_t *t);
extern void GoMultiCast(const char *groupname);
 
#ifdef HAVE_SDL
extern int vj_event_single_fire(void *ptr, SDL_Event event, int pressed);
#endif

static	veejay_t	*veejay_instance_ = NULL;

#ifdef HAVE_JACK
#ifndef VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX
#define VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX 3
#endif
#ifndef VJ_AUDIO_BEAT_ACTION_BREAK_BEAT
#define VJ_AUDIO_BEAT_ACTION_BREAK_BEAT 4
#endif

static inline int veejay_audio_beat_action_is_breakbeat(int action)
{
    return action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT ||
           action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX;
}

static int veejay_audio_sync_thread_cli_disabled_ = 0;
static int veejay_audio_beat_thread_cli_disabled_ = 0;

void veejay_audio_sync_thread_set_enabled(int enabled)
{
    __sync_lock_test_and_set(&veejay_audio_sync_thread_cli_disabled_, enabled ? 0 : 1);
}

void veejay_audio_beat_thread_set_enabled(int enabled)
{
    __sync_lock_test_and_set(&veejay_audio_beat_thread_cli_disabled_, enabled ? 0 : 1);
}
#endif


static void veejay_playback_close(veejay_t *info);



int veejay_get_state(veejay_t *info) {
	video_playback_setup *settings = (video_playback_setup*)info->settings;

	return atomic_load_int(&settings->state);
}

int veejay_set_yuv_range(veejay_t *info)
{
    if(info->pixel_format == FMT_422) {
        vje_set_pixel_range(235, 240, 16, 16);
        veejay_msg(VEEJAY_MSG_DEBUG, "YUV pixel range set to limited (16-235 / 16-240)");
        return 0;
    }

    vje_set_pixel_range(255, 255, 0, 0);
    veejay_msg(VEEJAY_MSG_DEBUG, "Using full-range YUV (luma 0-255, chroma 0-255)");
    return 1;
}

static void veejay_free_frame_buffer(VJFrame *f)
{
    if(f) {
        free(f->data[0]);
        free(f);
    }
}

static	VJFrame *veejay_allocate_frame_buffer(veejay_t *info) {
	VJFrame *buf = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, vj_to_pixfmt(info->pixel_format) );
	if(!buf)
		return NULL;
	buf->fps = info->settings->output_fps;
	size_t padding = 256;
	size_t len = sizeof(uint8_t) * ( buf->len + buf->uv_len + buf->uv_len);
	uint8_t *plane = (uint8_t*)vj_malloc( len + padding );
	if(!plane) {
		free(buf);
		return NULL;
	}


	buf->data[0] = plane;
	buf->data[1] = plane + buf->len;
	buf->data[2] = plane + buf->len + buf->uv_len;
	buf->data[3] = NULL; 

	return buf;
}

static inline double monotonic_now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline long long monotonic_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

static inline int vj_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double vj_clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline long vj_clampl(long v, long lo, long hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double vj_runtime_master_clock_now_s(veejay_t *info,
                                                   int *uses_audio_clock)
{
    double master_s = 0.0;

    if(uses_audio_clock)
        *uses_audio_clock = 0;

#ifdef HAVE_JACK
    if(info && info->settings && info->audio == AUDIO_PLAY) {
        video_playback_setup *settings = info->settings;

        master_s = atomic_load_double(&settings->audio_master_s);
        if(master_s <= 0.0)
            master_s = atomic_load_double(&settings->audio_start_offset);

        if(master_s > 0.0) {
            if(uses_audio_clock)
                *uses_audio_clock = 1;
            return master_s;
        }
    }
#else
    (void)info;
#endif

    return monotonic_now_s();
}

static inline void vj_runtime_publish_audio_clocks(video_playback_setup *settings,
                                                   double hardware_s,
                                                   double queued_s)
{
    double anchor_s;

    if(!settings)
        return;

    anchor_s = atomic_load_double(&settings->audio_start_offset);

    if(hardware_s <= 0.0)
        hardware_s = anchor_s;
    if(queued_s <= 0.0)
        queued_s = hardware_s;

    if(hardware_s > 0.0 && queued_s < hardware_s)
        queued_s = hardware_s;

    atomic_store_double(&settings->audio_master_s, hardware_s);
    atomic_store_double(&settings->audio_queued_s, queued_s);
}


#ifdef HAVE_JACK
static void veejay_track_align_arm_audio_guard(video_playback_setup *settings,
                                               long long now_ms,
                                               int delta_frames,
                                               double media_fps,
                                               const char *reason)
{
    (void)now_ms;

    if(!settings || delta_frames >= -1)
        return;

    if(media_fps <= 0.0)
        media_fps = 25.0;

    atomic_store_long_long(&settings->track_align_audio_guard_until_ms, 0);
    atomic_store_int(&settings->track_align_force_audio_edge_reset, 0);

    const int abs_delta = -delta_frames;
    const double snap_ms = ((double)abs_delta * 1000.0) / media_fps;
    const int guard_ms = vj_clampi((int)(snap_ms + 0.5),
                                   VJ_TRACK_ALIGN_AUDIO_GUARD_BACKWARD_MS,
                                   VJ_TRACK_ALIGN_AUDIO_GUARD_MAX_MS);
    const int guard_blocks = vj_clampi((int)(((double)guard_ms * media_fps / 1000.0) + 0.999),
                                       1,
                                       8);
    const int audio_source = atomic_load_int(&settings->record_audio_source);
    const int sync_mode = atomic_load_int(&settings->audio_sync.mode);
    const int mute = atomic_load_int(&settings->audio_mute);

    vj_perform_audio_source_transition_guard_ex(guard_blocks,
                                                reason ? reason : "track-align-backward-snap",
                                                audio_source,
                                                audio_source,
                                                sync_mode,
                                                mute);
}

#endif

static inline float vj_runtime_clamp_fps(float fps)
{
    return (fps < VJ_DYNAMIC_FPS_MIN) ? VJ_DYNAMIC_FPS_MIN :
           ((fps > VJ_DYNAMIC_FPS_MAX) ? VJ_DYNAMIC_FPS_MAX : fps);
}

static inline double vj_runtime_spvf_from_fps(float fps)
{
    fps = vj_runtime_clamp_fps(fps);

    int fps_2dp = (int)(fps * 100.0f + 0.5f);

    switch (fps_2dp) {
        case 2398:
            return 1001.0 / 24000.0;
        case 2997:
            return 1001.0 / 30000.0;
        case 5994:
            return 1001.0 / 60000.0;
        default:
            return 1.0 / (double) fps;
    }
}


#ifdef HAVE_JACK
static inline double vj_runtime_tempo_follow_correction(video_playback_setup *settings)
{
    int mode;
    int state;
    double correction;
    double max_corr;

    if(!settings)
        return 1.0;

    mode = atomic_load_int(&settings->audio_sync.mode);
    if(mode != VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        return 1.0;

    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return 1.0;

    state = atomic_load_int(&settings->audio_sync.bridge_state);
    if(state != VJ_AUDIO_SYNC_BRIDGE_STATE_LOCKED &&
       state != VJ_AUDIO_SYNC_BRIDGE_STATE_HOLD)
        return 1.0;

    correction = settings->audio_sync.bridge_last_correction;
    if(correction < 0.25 || correction > 4.0)
        return 1.0;

    max_corr = (double)atomic_load_int(&settings->audio_sync.max_correction_pct) / 100.0;
    if(max_corr < 0.0)
        max_corr = 0.0;
    else if(max_corr > 0.25)
        max_corr = 0.25;

    if(max_corr <= 0.0)
        return 1.0;

    if(correction < 1.0 - max_corr)
        correction = 1.0 - max_corr;
    else if(correction > 1.0 + max_corr)
        correction = 1.0 + max_corr;

    return correction;
}
#endif
static inline double vj_runtime_effective_spvf(video_playback_setup *settings)
{
    double spvf;

    if(!settings)
        return 1.0 / 25.0;

    spvf = settings->spvf;
    if(spvf <= 0.0)
        spvf = 1.0 / 25.0;

#ifdef HAVE_JACK
    {
        double tempo_follow = vj_runtime_tempo_follow_correction(settings);
        if(tempo_follow > 0.25 && tempo_follow < 4.0)
            spvf /= tempo_follow;
    }
#endif

    if(spvf <= 0.0)
        spvf = 1.0 / 25.0;

    return spvf;
}

static inline double vj_runtime_target_time_s(video_playback_setup *settings, long long frame)
{
    double epoch_s = atomic_load_double(&settings->fps_epoch_s);
    long long epoch_frame = atomic_load_long_long(&settings->fps_epoch_frame);
    double spvf = vj_runtime_effective_spvf(settings);

    if (epoch_s <= 0.0) {
        double anchor = atomic_load_double(&settings->audio_start_offset);
        epoch_s = (anchor > 0.0) ? anchor : monotonic_now_s();
    }

    return epoch_s + ((double)(frame - epoch_frame) * spvf);
}

static void vj_runtime_reanchor_clock(veejay_t *info,
                                      long long frame,
                                      const char *reason)
{
    video_playback_setup *settings;
    double master_s;
    long long master_frame;
    long long requested_frame;
    long long anchor_frame;
    int using_audio_clock = 0;

    if(!info || !info->settings)
        return;

    settings = info->settings;

    if(frame < 0)
        frame = atomic_load_long_long(&settings->current_frame_num);
    if(frame < 0)
        frame = 0;

    requested_frame = frame;

    master_frame = atomic_load_long_long(&settings->master_frame_num);
    master_s = vj_runtime_master_clock_now_s(info, &using_audio_clock);

    anchor_frame = (master_frame >= 0) ? master_frame : requested_frame;

    atomic_store_double(&settings->fps_epoch_s, master_s);
    atomic_store_long_long(&settings->fps_epoch_frame, anchor_frame);
}

static void vj_runtime_update_frame_fps(veejay_t *info, float fps)
{
    if (info == NULL || info->settings == NULL)
        return;

    video_playback_setup *settings = info->settings;

    if (info->effect_frame1) info->effect_frame1->fps = fps;
    if (info->effect_frame2) info->effect_frame2->fps = fps;
    if (info->effect_frame3) info->effect_frame3->fps = fps;
    if (info->effect_frame4) info->effect_frame4->fps = fps;

    for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
        if (settings->buffers[i])
            settings->buffers[i]->fps = fps;
    }
}

static inline void
vj_spin_until_absolute_deadline(const struct timespec *deadline,
                                double pause_cost_ns)
{
    struct timespec now;

    long long deadline_ns =
        (long long)deadline->tv_sec * 1000000000LL + deadline->tv_nsec;

    long long pause_ns = (long long)pause_cost_ns;
    if(pause_ns <= 0)
        pause_ns = 1;

    const long long pause_ns_x2 = pause_ns * 2;

    for (;;) {

        clock_gettime(CLOCK_MONOTONIC, &now);

        long long now_ns =
            (long long)now.tv_sec * 1000000000LL + now.tv_nsec;

        long long remaining_ns = deadline_ns - now_ns;

        if (remaining_ns <= 0)
            break;

        int spin_batch = (remaining_ns > (pause_ns_x2 * 128)) ?
                         128 : (int)(remaining_ns / pause_ns_x2);
        if(spin_batch < 1)
            spin_batch = 1;

        for (int i = 0; i < spin_batch; ++i) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("yield");
#endif
        }
    }
}

void usleep_accurate(long long usec, video_playback_setup *settings)
{
    if (usec <= 0) return;

    struct timespec start, deadline;
    clock_gettime(CLOCK_MONOTONIC, &start);

    const long long target_ns    = usec * 1000LL;
    const long long threshold_ns = settings->clock_overshoot;

    const long long target_sec  = target_ns / 1000000000LL;
    const long long target_rem  = target_ns - target_sec * 1000000000LL;

    deadline.tv_sec  = start.tv_sec + target_sec;
    deadline.tv_nsec = start.tv_nsec + target_rem;
    if (deadline.tv_nsec >= 1000000000LL) {
        deadline.tv_nsec -= 1000000000LL;
        deadline.tv_sec++;
    }

    if (target_ns > threshold_ns) {
        const long long sleep_ns = target_ns - threshold_ns;

        const long long sleep_sec = sleep_ns / 1000000000LL;
        const long long sleep_rem = sleep_ns - sleep_sec * 1000000000LL;

        struct timespec req = { .tv_sec = sleep_sec, .tv_nsec = sleep_rem };
        struct timespec rem;

        while (nanosleep(&req, &rem) == -1) {
            if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) return;
            req = rem;
        }
    }

    vj_spin_until_absolute_deadline(&deadline, settings->pause_cost_ns);
}

static inline void usleep_hybrid(long long usec, video_playback_setup *settings)
{
    if (usec <= 0)
        return;

    struct timespec now, deadline;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long long target_ns = usec * 1000LL;

    deadline.tv_sec  = now.tv_sec  + target_ns / 1000000000LL;
    deadline.tv_nsec = now.tv_nsec + target_ns % 1000000000LL;

    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec++;
    }

    while (clock_nanosleep(CLOCK_MONOTONIC,
                           TIMER_ABSTIME,
                           &deadline,
                           NULL) == EINTR)
    {
        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
            return;
    }
}


static void veejay_seek(veejay_t *info, int speed, int max_sfd)
{
    video_playback_setup *settings =
        (video_playback_setup *)info->settings;

    atomic_store_int(&settings->audio_slice, 0);
    atomic_store_int(&settings->audio_slice_len, max_sfd);
    atomic_store_int(&settings->audio_flush_request, 1);
}


#ifdef HAVE_JACK
static int veejay_track_align_current_clip_active(veejay_t *info)
{
    video_playback_setup *settings;

    if(!info || !info->settings)
        return 0;

    settings = info->settings;

    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return 0;
    if(atomic_load_int(&settings->audio_sync.mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return 0;
    if(vj_audio_sync_get_target_mode(&settings->audio_sync) != VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return 0;

    return 1;
}

static double veejay_track_align_media_fps(veejay_t *info)
{
    editlist *el = NULL;

    if(!info)
        return 25.0;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        el = sample_get_editlist(info->uc->sample_id);
    if(!el)
        el = info->current_edit_list ? info->current_edit_list : info->edit_list;

    if(el && el->video_fps > 0.0)
        return (double)el->video_fps;
    if(info->settings && info->settings->output_fps > 0.0f)
        return (double)info->settings->output_fps;

    return 25.0;
}

static double veejay_track_align_transport_fps(veejay_t *info)
{
    video_playback_setup *settings;
    double fps;

    if(!info || !info->settings)
        return 25.0;

    settings = info->settings;
    fps = (double)settings->output_fps;
    if(fps <= 0.0) {
        double media_fps = veejay_track_align_media_fps(info);
        double runtime_rate = atomic_load_double(&settings->runtime_playback_rate);

        if(runtime_rate <= 0.0)
            runtime_rate = 1.0;

        fps = media_fps * runtime_rate;
    }

    return vj_clampd(fps, 1.0, 240.0);
}

#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL 0.075
#endif
#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL 0.035
#endif
#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL 1.25
#endif

static int veejay_track_align_current_sfd(veejay_t *info);

static int veejay_track_align_fps_close(double fps, double source_fps)
{
    double tol;
    double diff;

    if(source_fps <= 0.0)
        source_fps = 25.0;
    if(fps <= 0.0)
        return 0;

    diff = fabs(fps - source_fps);
    tol = vj_clampd(source_fps * VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL,
                    VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL,
                    VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL);

    return diff <= tol;
}

static int veejay_track_align_current_sfd(veejay_t *info)
{
    video_playback_setup *settings;
    int sfd = 1;

    if(!info || !info->settings)
        return 1;

    settings = info->settings;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int id = info->uc->sample_id;
        if(id >= 0)
            sfd = sample_get_framedup(id);
    }
    else {
        sfd = settings->sfd > 0 ? settings->sfd : info->sfd;
    }

    if(sfd <= 0)
        sfd = 1;

    return sfd;
}

static int veejay_track_align_is_normal_values(veejay_t *info,
                                               int speed,
                                               int sfd,
                                               double fps)
{
    double source_fps;

    if(!veejay_track_align_current_clip_active(info))
        return 0;

    if(speed != 1)
        return 0;

    if(sfd > 1)
        return 0;

    source_fps = veejay_track_align_media_fps(info);
    return veejay_track_align_fps_close(fps, source_fps);
}

static int veejay_track_align_is_normal_transport(veejay_t *info)
{
    video_playback_setup *settings;

    if(!info || !info->settings)
        return 0;

    settings = info->settings;
    return veejay_track_align_is_normal_values(info,
                                               settings->current_playback_speed,
                                               veejay_track_align_current_sfd(info),
                                               veejay_track_align_transport_fps(info));
}

static double veejay_track_align_audio_now_s(video_playback_setup *settings)
{
    double audio_s;

    if(!settings)
        return monotonic_now_s();

    audio_s = atomic_load_double(&settings->audio_master_s);
    if(audio_s <= 0.0)
        audio_s = monotonic_now_s();

    return audio_s;
}

static void veejay_track_align_integrate_linear_active(veejay_t *info, const char *reason)
{
    video_playback_setup *settings;
    double now_audio_s;
    double anchor_audio_s;
    double source_fps;
    double elapsed_s;
    double frames_accum;

    if(!info || !info->settings)
        return;

    settings = info->settings;
    if(!atomic_load_int(&settings->track_align_linear_active))
        return;

    now_audio_s = veejay_track_align_audio_now_s(settings);
    anchor_audio_s = atomic_load_double(&settings->track_align_linear_anchor_audio_s);
    if(anchor_audio_s <= 0.0)
        anchor_audio_s = now_audio_s;

    source_fps = atomic_load_double(&settings->track_align_linear_segment_fps);
    if(source_fps <= 0.0)
        source_fps = veejay_track_align_media_fps(info);

    elapsed_s = vj_clampd(now_audio_s - anchor_audio_s, 0.0, 300.0);
    frames_accum = elapsed_s * source_fps;

    atomic_store_double(&settings->track_align_linear_frame_accum, frames_accum);
    atomic_store_double(&settings->track_align_linear_segment_audio_s, now_audio_s);
}

static void veejay_track_align_bump_reacquire(veejay_t *info, const char *reason)
{
    video_playback_setup *settings;
    int seq;

    if(!veejay_track_align_current_clip_active(info))
        return;

    settings = info->settings;
    vj_audio_sync_track_align_reset_acquisition(&settings->audio_sync);
    seq = atomic_load_int(&settings->track_align_reacquire_seq) + 1;
    atomic_store_int(&settings->track_align_reacquire_seq, seq);

}

static void veejay_track_align_note_position_discontinuity(veejay_t *info,
                                                           long long frame,
                                                           const char *reason)
{
    video_playback_setup *settings;
    double audio_s;
    double source_fps;
    int mode;
    int id;

    if(!veejay_track_align_current_clip_active(info))
        return;

    settings = info->settings;
    if(!atomic_load_int(&settings->track_align_linear_active))
        return;

    audio_s = veejay_track_align_audio_now_s(settings);
    source_fps = veejay_track_align_media_fps(info);
    mode = info->uc ? info->uc->playback_mode : -1;
    id = info->uc ? info->uc->sample_id : -1;

    atomic_store_long_long(&settings->track_align_linear_anchor_frame, frame);
    atomic_store_double(&settings->track_align_linear_anchor_audio_s, audio_s);
    atomic_store_double(&settings->track_align_linear_segment_audio_s, audio_s);
    atomic_store_double(&settings->track_align_linear_segment_fps, source_fps);
    atomic_store_double(&settings->track_align_linear_frame_accum, 0.0);
    atomic_store_int(&settings->track_align_linear_mode, mode);
    atomic_store_int(&settings->track_align_linear_id, id);

    veejay_track_align_bump_reacquire(info, reason ? reason : "position-jump");

}

static void veejay_track_align_note_departure(veejay_t *info, const char *reason)
{
    video_playback_setup *settings;
    long long frame;
    double audio_s;
    int mode;
    int id;

    if(!veejay_track_align_current_clip_active(info))
        return;

    settings = info->settings;
    frame = atomic_load_long_long(&settings->current_frame_num);
    if(frame < 0)
        frame = atomic_load_long_long(&settings->master_frame_num);
    if(frame < 0)
        frame = 0;

    audio_s = atomic_load_double(&settings->audio_master_s);
    if(audio_s <= 0.0)
        audio_s = monotonic_now_s();

    mode = info->uc ? info->uc->playback_mode : -1;
    id = info->uc ? info->uc->sample_id : -1;

    atomic_store_long_long(&settings->track_align_linear_anchor_frame, frame);
    atomic_store_double(&settings->track_align_linear_anchor_audio_s, audio_s);
    atomic_store_double(&settings->track_align_linear_segment_audio_s, audio_s);
    atomic_store_double(&settings->track_align_linear_segment_fps,
                        veejay_track_align_media_fps(info));
    atomic_store_double(&settings->track_align_linear_frame_accum, 0.0);
    atomic_store_int(&settings->track_align_linear_mode, mode);
    atomic_store_int(&settings->track_align_linear_id, id);
    atomic_store_int(&settings->track_align_linear_active, 1);

    veejay_track_align_bump_reacquire(info, reason ? reason : "depart-normal");

}

static void veejay_track_align_note_return(veejay_t *info, const char *reason)
{
    video_playback_setup *settings;
    long long anchor_frame;
    long long cur_frame;
    long long estimated;
    double anchor_audio_s;
    double now_audio_s;
    double elapsed_s;
    double fps;
    int mode;
    int id;
    int anchor_mode;
    int anchor_id;

    if(!veejay_track_align_current_clip_active(info))
        return;

    settings = info->settings;
    if(!atomic_load_int(&settings->track_align_linear_active)) {
        veejay_track_align_bump_reacquire(info, reason ? reason : "return-normal");
        return;
    }

    mode = info->uc ? info->uc->playback_mode : -1;
    id = info->uc ? info->uc->sample_id : -1;
    anchor_mode = atomic_load_int(&settings->track_align_linear_mode);
    anchor_id = atomic_load_int(&settings->track_align_linear_id);

    if(mode != anchor_mode || id != anchor_id) {
        atomic_store_int(&settings->track_align_linear_active, 0);
        veejay_track_align_bump_reacquire(info, "return-normal-new-clip");
        return;
    }

    anchor_frame = atomic_load_long_long(&settings->track_align_linear_anchor_frame);
    anchor_audio_s = atomic_load_double(&settings->track_align_linear_anchor_audio_s);

    veejay_track_align_integrate_linear_active(info, reason ? reason : "return-normal");

    now_audio_s = veejay_track_align_audio_now_s(settings);
    elapsed_s = now_audio_s - anchor_audio_s;
    if(elapsed_s < 0.0)
        elapsed_s = 0.0;
    if(elapsed_s > 300.0)
        elapsed_s = 300.0;

    fps = atomic_load_double(&settings->track_align_linear_segment_fps);
    if(fps <= 0.0)
        fps = veejay_track_align_media_fps(info);
    {
        double frames_accum = atomic_load_double(&settings->track_align_linear_frame_accum);
        if(frames_accum < 0.0)
            frames_accum = 0.0;
        estimated = anchor_frame + (long long)(frames_accum + 0.5);
    }
    cur_frame = atomic_load_long_long(&settings->current_frame_num);

    atomic_store_int(&settings->track_align_linear_active, 0);

    if(cur_frame < 0)
        cur_frame = estimated;

    if(estimated != cur_frame) {
        veejay_msg(VEEJAY_MSG_INFO,
                   "[TRACK-ALIGN] source-clock reanchor on normal playback: frame %lld -> %lld (elapsed=%.3fs source_fps=%.3f reason=%s)",
                   cur_frame,
                   estimated,
                   elapsed_s,
                   fps,
                   reason ? reason : "return-normal");
        veejay_track_align_arm_audio_guard(settings,
                                           monotonic_now_ms(),
                                           (int)(estimated - cur_frame),
                                           fps,
                                           "runtime-fps-reanchor");
        veejay_set_frame(info, (long)estimated);
    }

    veejay_track_align_bump_reacquire(info, reason ? reason : "return-normal");
}

static void veejay_track_align_note_normal_state(veejay_t *info,
                                                 int was_normal,
                                                 int is_normal,
                                                 const char *reason)
{
    if(was_normal && !is_normal)
        veejay_track_align_note_departure(info, reason ? reason : "depart-normal");
    else if(!was_normal && is_normal)
        veejay_track_align_note_return(info, reason ? reason : "return-normal");
}
#endif

static void	veejay_reset_el_buffer( veejay_t *info );

void veejay_change_state(veejay_t * info, int new_state)
{
	video_playback_setup *settings = info->settings;
	atomic_store_int(&settings->state, new_state);
}

void veejay_change_state_save(veejay_t * info, int new_state)
{
	if(new_state == LAVPLAY_STATE_STOP )
	{
		pid_t my_pid = getpid();
        int len = snprintf(NULL, 0, "%s/recovery/recovery_samplelist_p%d.sl", info->homedir, (int)my_pid);
        char *recover_samples = vj_malloc(len + 1);
        if (!recover_samples) { 
			return;
		}
        snprintf(recover_samples, len + 1, "%s/recovery/recovery_samplelist_p%d.sl", info->homedir, (int)my_pid);
        len = snprintf(NULL, 0, "%s/recovery/recovery_editlist_p%d.edl", info->homedir, (int)my_pid);
        char *recover_edl = malloc(len + 1);
        if (!recover_edl) { 
			free(recover_samples); 
			return;
		}
        snprintf(recover_edl, len + 1, "%s/recovery/recovery_editlist_p%d.edl", info->homedir, (int)my_pid);
        
		int re = veejay_save_all( info, recover_edl, 0, 0 );
		int rs= 0;
		if(re) {
			rs = sample_writeToFile( recover_samples,info->composite,info->seq,info->font,
				info->uc->sample_id, info->uc->playback_mode );
		}

		if(rs)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved Samplelist to %s", recover_samples );
		if(re)
			veejay_msg(VEEJAY_MSG_WARNING, "Saved Editlist to %s", recover_edl );

		free(recover_samples);
	}

	veejay_change_state( info, new_state );
}

static inline int playback_dir(int speed)
{
    return (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
}

static inline int playback_retime_audio_slice(int old_sfd, int new_sfd, int old_slice)
{
    if (old_sfd <= 1 || new_sfd <= 1)
        return 0;

    old_slice = vj_clampi(old_slice, 0, old_sfd - 1);

    return vj_clampi((old_slice * new_sfd) / old_sfd, 0, new_sfd - 1);
}

static void veejay_output_hold_release_timed(veejay_t *info, const char *reason, int log_release)
{
    if(!info || !info->settings)
        return;

    video_playback_setup *settings = (video_playback_setup*) info->settings;

    if(!settings->output_hold_active && !settings->output_hold_capture &&
       settings->output_hold_frames_left <= 0 && settings->hold_status <= 0)
        return;

    settings->output_hold_active = 0;
    settings->output_hold_capture = 0;
    settings->output_hold_frames_left = 0;
    settings->output_hold_frames_total = 0;
    settings->hold_status = 0;
    settings->hold_pos = 0;
    settings->hold_resume = 0;

    if(!settings->hold_fx)
        settings->output_hold_ready = 0;

    atomic_store_double(&settings->smoothed_drift_us, 0.0);

    if(log_release) {
        if(reason && reason[0])
            veejay_msg(VEEJAY_MSG_INFO, "HOLD: Released full output freeze (%s)", reason);
        else
            veejay_msg(VEEJAY_MSG_INFO, "HOLD: Released full output freeze");
    }
}

static inline void veejay_output_hold_release_on_transport(veejay_t *info)
{
    veejay_output_hold_release_timed(info, "transport", 1);
}


int veejay_set_framedup(veejay_t *info, int n)
{
    video_playback_setup *settings = info->settings;

    veejay_output_hold_release_on_transport(info);

    if (n < 1)
        n = 1;

    const int speed = settings->current_playback_speed;
    const int cur_dir = playback_dir(speed);

    int cur_sfd = 0;
    int old_sfd_for_align = 1;

    switch (info->uc->playback_mode) {
        case VJ_PLAYBACK_MODE_PLAIN:
            cur_sfd = info->sfd;
            old_sfd_for_align = cur_sfd;

            if (cur_sfd != n) {
                const int soft_slow_retime =
                    (cur_sfd > 1 && n > 1 && cur_dir != 0);

                info->sfd = n;
                settings->sfd = n;

                if (soft_slow_retime) {
                    const int cur_slice = atomic_load_int(&settings->audio_slice);
                    const int new_slice = playback_retime_audio_slice(cur_sfd, n, cur_slice);
                    atomic_store_int(&settings->audio_slice, new_slice);
                    atomic_store_int(&settings->audio_slice_len, n);
                } else {
                    atomic_store_int(&settings->audio_slice, 0);
                    atomic_store_int(&settings->audio_slice_len, n);
                    settings->audio_last_stretched_samples = 0;

                    veejay_seek(info, speed, n);

                    vj_perform_initiate_edge_change(
                        info,
                        (cur_dir == 0) ? AUDIO_EDGE_SILENCE : AUDIO_EDGE_JUMP,
                        cur_dir,
                        cur_dir
                    );
                }
            } else {
                info->sfd = n;
                settings->sfd = n;
                atomic_store_int(&settings->audio_slice_len, n);
            }
            break;

        case VJ_PLAYBACK_MODE_SAMPLE:
            cur_sfd = sample_get_framedup(info->uc->sample_id);
            old_sfd_for_align = cur_sfd;

            if (cur_sfd != n) {
                const int soft_slow_retime =
                    (cur_sfd > 1 && n > 1 && cur_dir != 0);

                sample_set_framedup(info->uc->sample_id, n);

                info->sfd = n;
                settings->sfd = n;

                if (soft_slow_retime) {
                    const int cur_slice = atomic_load_int(&settings->audio_slice);
                    const int new_slice = playback_retime_audio_slice(cur_sfd, n, cur_slice);
                    atomic_store_int(&settings->audio_slice, new_slice);
                    atomic_store_int(&settings->audio_slice_len, n);
                } else {
                    atomic_store_int(&settings->audio_slice, 0);
                    atomic_store_int(&settings->audio_slice_len, n);
                    settings->audio_last_stretched_samples = 0;

                    veejay_seek(info, speed, n);

                    vj_perform_initiate_edge_change(
                        info,
                        (cur_dir == 0) ? AUDIO_EDGE_SILENCE : AUDIO_EDGE_JUMP,
                        cur_dir,
                        cur_dir
                    );
                }
            } else {
                info->sfd = n;
                settings->sfd = n;
                atomic_store_int(&settings->audio_slice_len, n);
            }
            break;

        default:
            return -1;
    }

    if(old_sfd_for_align != n)
        vj_runtime_reanchor_clock(info,
                                  atomic_load_long_long(&settings->current_frame_num),
                                  "framedup");

#ifdef HAVE_JACK
    if(old_sfd_for_align != n) {
        const double fps_now = veejay_track_align_transport_fps(info);
        veejay_track_align_note_normal_state(info,
                                             veejay_track_align_is_normal_values(info, speed, old_sfd_for_align, fps_now),
                                             veejay_track_align_is_normal_values(info, speed, n, fps_now),
                                             "framedup");
    }
#endif

    return 1;
}


int veejay_set_speed(veejay_t *info, int speed, int force_seek)
{
    video_playback_setup *settings =
        (video_playback_setup *)info->settings;

    veejay_output_hold_release_on_transport(info);

    int len = 0;

    speed = vj_clampi(speed, -MAX_SPEED, MAX_SPEED);

    const int old_speed = settings->current_playback_speed;
    const int prev_dir = playback_dir(old_speed);

    int max_sfd = 1;

    switch (info->uc->playback_mode) {
        case VJ_PLAYBACK_MODE_PLAIN:
            if (abs(speed) <= info->current_edit_list->total_frames) {
                settings->current_playback_speed = speed;
            } else {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "Speed %d too high to set",
                           speed);
            }
            max_sfd = info->sfd;
            break;

        case VJ_PLAYBACK_MODE_SAMPLE:
            len = sample_get_endFrame(info->uc->sample_id) -
                  sample_get_startFrame(info->uc->sample_id);

            if (speed < 0) {
                if ((-1 * len) > speed) {
                    veejay_msg(VEEJAY_MSG_ERROR,
                               "Speed %d too high to set",
                               speed);
                    return 1;
                }
            } else {
                if (len < speed) {
                    veejay_msg(VEEJAY_MSG_ERROR,
                               "Speed %d too high to set",
                               speed);
                    return 1;
                }
            }

            if (sample_set_speed(info->uc->sample_id, speed) != -1)
                settings->current_playback_speed = speed;

            max_sfd = sample_get_framedup(info->uc->sample_id);
            break;

        case VJ_PLAYBACK_MODE_TAG:
            settings->current_playback_speed = (speed == 0) ? 0 : 1;
            max_sfd = 1;
            break;

        default:
            veejay_msg(VEEJAY_MSG_ERROR, "Unknown playback mode");
            return 0;
    }

    const int effective_speed = settings->current_playback_speed;
    const int cur_dir = playback_dir(effective_speed);
    const int speed_changed = (old_speed != effective_speed);
    const int real_direction_flip =
        (prev_dir != 0 && cur_dir != 0 && prev_dir != cur_dir);

    int edge_type = AUDIO_EDGE_NONE;

    if (speed_changed) {
        if (effective_speed == 0)
            edge_type = AUDIO_EDGE_SILENCE;
        else if (old_speed == 0)
            edge_type = AUDIO_EDGE_JUMP;
        else if (real_direction_flip)
            edge_type = AUDIO_EDGE_DIRECTION;
    }

    if(speed_changed)
        vj_runtime_reanchor_clock(info,
                                  atomic_load_long_long(&settings->current_frame_num),
                                  "speed-change");

    if (real_direction_flip)
        atomic_store_int(&settings->audio_direction_changed, 1);

    if (edge_type != AUDIO_EDGE_NONE) {
        atomic_store_int(&settings->audio_slice, 0);

        if (edge_type != AUDIO_EDGE_DIRECTION)
            settings->audio_last_stretched_samples = 0;

        vj_perform_initiate_edge_change(info, edge_type, prev_dir, cur_dir);
    }

#ifdef HAVE_JACK
    if (speed_changed) {
        const int sfd_now = veejay_track_align_current_sfd(info);
        const double fps_now = veejay_track_align_transport_fps(info);
        veejay_track_align_note_normal_state(info,
                                             veejay_track_align_is_normal_values(info, old_speed, sfd_now, fps_now),
                                             veejay_track_align_is_normal_values(info, effective_speed, sfd_now, fps_now),
                                             "speed-change");
    }
#endif

    if (force_seek && old_speed != 0 && effective_speed != 0)
        veejay_seek(info, effective_speed, max_sfd);

    return 1;
}



int veejay_hold_frame(veejay_t * info, int rel_resume_pos, int hold_pos)
{
    (void) rel_resume_pos;

    if(!info || !info->settings)
        return 0;

    video_playback_setup *settings = (video_playback_setup *) info->settings;

    if(hold_pos <= 0) {
        veejay_output_hold_release_timed(info, NULL, 1);
        return 1;
    }

    int frames = hold_pos;
    if(frames < 1)
        frames = 1;
    else if(frames > 999)
        frames = 999;

    if(!settings->output_hold_active)
        settings->output_hold_capture = 1;

    settings->output_hold_active = 1;
    settings->output_hold_frames_left = frames;
    settings->output_hold_frames_total = frames;
    settings->hold_status = 1;
    settings->hold_pos = frames;
    settings->hold_resume = 0;

    veejay_msg(VEEJAY_MSG_INFO,
               "HOLD: Full output freeze for %d frame%s",
               frames, frames == 1 ? "" : "s");

    return 1;
}


static void veejay_sample_resume_at(veejay_t *info, int cur_id)
{
    video_playback_setup *settings = info->settings;

    long pos = sample_get_resume(cur_id);
    long start = sample_get_startFrame(cur_id);
    long end = sample_get_endFrame(cur_id);

    pos = vj_clampl(pos, start, end);

    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);

    if ((long long) pos != cur_frame) {
        int speed = settings->current_playback_speed;
        int dir = playback_dir(speed);

        int edge_type = (pos == start) ? AUDIO_EDGE_RESET : AUDIO_EDGE_JUMP;

        if (dir == 0)
            edge_type = AUDIO_EDGE_SILENCE;

        atomic_store_int(&settings->audio_slice, 0);

        vj_perform_initiate_edge_change(
            info,
            edge_type,
            dir,
            dir
        );
    }

    veejay_set_frame(info, pos);

    veejay_msg(
        VEEJAY_MSG_DEBUG,
        "Sample %d continues with frame %ld",
        cur_id,
        pos
    );
}

int veejay_increase_frame(veejay_t *info, long num)
{
    video_playback_setup *settings = info->settings;

    long long current_frame_num = atomic_load_long_long(&settings->current_frame_num);
    long long min_frame_num = atomic_load_long_long(&settings->min_frame_num);
    long long max_frame_num = atomic_load_long_long(&settings->max_frame_num);
    long long next_frame = current_frame_num + (long long) num;

    int speed = settings->current_playback_speed;

    if (num == 0)
        return 1;

    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int s_start = 0;
        int s_end = 0;
        int s_loop = 0;
        int s_speed = speed;

        if (sample_get_short_info(info->uc->sample_id,
                                  &s_start,
                                  &s_end,
                                  &s_loop,
                                  &s_speed) != 0)
            return 0;

        (void) s_loop;

        min_frame_num = s_start;
        max_frame_num = s_end;
        speed = s_speed;

		if(speed == 0)
            return 1;
        next_frame = current_frame_num + (long long) num;
    }

    if (next_frame < min_frame_num)
        return 0;

    if (next_frame > max_frame_num)
        return 0;

    const int play_dir = playback_dir(speed);
    const int move_dir = (num > 0) ? 1 : -1;

    int edge_type = AUDIO_EDGE_NONE;

    if (play_dir == 0) {
        edge_type = AUDIO_EDGE_SILENCE;
    } else if (num < -1 || num > 1) {
        edge_type = (next_frame == min_frame_num) ?
            AUDIO_EDGE_RESET : AUDIO_EDGE_JUMP;
    } else if (move_dir != play_dir) {
        edge_type = AUDIO_EDGE_JUMP;
    }

    atomic_store_long_long(&settings->current_frame_num, next_frame);

#ifdef HAVE_JACK
    veejay_track_align_note_position_discontinuity(info, next_frame, "frame-step");
#endif

    if (edge_type != AUDIO_EDGE_NONE) {
        atomic_store_int(&settings->audio_slice, 0);

        vj_perform_initiate_edge_change(
            info,
            edge_type,
            play_dir,
            play_dir
        );
    }

    return 1;
}


int veejay_free(veejay_t * info)
{
	video_playback_setup *settings = (video_playback_setup *) info->settings;

    veejay_playback_close(info);

	vj_event_destroy(info);

	vj_tag_free();
   	
	sample_free(info->edit_list);

	if( info->plain_editlist )
		vj_el_free(info->plain_editlist);


	if( info->composite )
		composite_destroy( info->composite );

	if( info->settings->action_scheduler.state )
	{
		if(info->settings->action_scheduler.sl )
			free(info->settings->action_scheduler.sl );
		info->settings->action_scheduler.state = 0;
	}

	if( info->effect_frame1) free(info->effect_frame1);
	if( info->effect_frame_info) free(info->effect_frame_info);
	if( info->effect_frame2) free(info->effect_frame2);
	if( info->effect_info) free( info->effect_info );
	if( info->effect_frame3) free(info->effect_frame3);
	if( info->effect_frame_info2) free(info->effect_frame_info2);
	if( info->effect_frame4) free(info->effect_frame4);
	if( info->effect_info2) free( info->effect_info2 );

	if(info->global_chain) {
		for( int i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
			free(info->global_chain->fx_chain[i]);
		}
		free(info->global_chain);
	}
	
    if( info->settings->transition.ptr ) {
        shapewipe_free(info->settings->transition.ptr);
    }

	if( info->dummy ) free(info->dummy );
	
    free( info->seq );
    free(info->status_what);
	free(info->homedir);  
    free(info->uc);
	free(info->status_line);	
	if(info->cpumask) free(info->cpumask);
	if(info->mask) free(info->mask);
	if(info->rlinks) free(info->rlinks);
    if(info->rmodes) free(info->rmodes );
	if(info->splitted_screens) free(info->splitted_screens);
    if(info->recording) {
        int i;
        for(i = 0; i < 4; i++) {
            if(info->recording->video.planes[i])
                free(info->recording->video.planes[i]);
        }
        if(info->recording->output_audio.buffer)
            free(info->recording->output_audio.buffer);
        if(info->recording->sync_audio.buffer)
            free(info->recording->sync_audio.buffer);
        free(info->recording);
    }
	free(settings);
    free(info);

    return 1;
}

static inline void safe_join(pthread_t *t, const char *name)
{
    if (t && *t) {
        pthread_join(*t, NULL);
        veejay_msg(VEEJAY_MSG_DEBUG, "%s thread finished.", name);
        *t = 0;
    }
}

static void veejay_request_stop_fast(veejay_t *info)
{
    if (!info || !info->settings)
        return;

    video_playback_setup *settings = (video_playback_setup*) info->settings;

    atomic_store_int(&settings->state, LAVPLAY_STATE_STOP);
    atomic_store_int(&settings->first_audio_frame_ready, 1);
    atomic_store_int(&info->audio_running, 0);

#ifdef HAVE_JACK
    atomic_store_int(&settings->audio_beat.stop_request, 1);
    atomic_store_int(&settings->audio_beat.enabled, 0);
    atomic_store_int(&settings->audio_sync.stop_request, 1);
    atomic_store_int(&settings->audio_sync.enabled, 0);
#endif
}

static void veejay_wake_playback_waiters(veejay_t *info)
{
    if (!info || !info->settings)
        return;

    video_playback_setup *settings = (video_playback_setup*) info->settings;

    veejay_request_stop_fast(info);

    pthread_mutex_lock(&settings->mutex);
    pthread_cond_broadcast(&settings->producer_wait_cv);
    pthread_cond_broadcast(&settings->renderer_wait_cv);
    pthread_cond_broadcast(&settings->data_ready_cv);
    pthread_mutex_unlock(&settings->mutex);

    pthread_mutex_lock(&settings->start_mutex);
    pthread_cond_broadcast(&settings->start_cond);
    pthread_mutex_unlock(&settings->start_mutex);
}

void veejay_busy(veejay_t * info)
{
	if (!info || !info->settings) {
        return;
    }

	video_playback_setup *settings = (video_playback_setup*)(info->settings);

    veejay_msg(VEEJAY_MSG_DEBUG, "Waiting for threads to finish...");
    veejay_wake_playback_waiters(info);

    safe_join(&settings->renderer_thread, "Renderer");
    safe_join(&settings->producer_thread, "Producer");
    safe_join(&settings->audio_playback_thread, "Audio playback");
#ifdef HAVE_JACK
    safe_join(&settings->audio_beat_thread, "Audio beat");
    safe_join(&settings->audio_sync_thread, "Audio sync");
    vj_perform_audio_stop(info);
#endif

    veejay_msg(VEEJAY_MSG_INFO, "Playback engine terminated.");
}

void veejay_quit(veejay_t * info)
{
    veejay_wake_playback_waiters(info);
}


int veejay_set_frame(veejay_t *info, long framenum)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;

    veejay_output_hold_release_on_transport(info);

    long long min_frame = atomic_load_long_long(&settings->min_frame_num);
    long long max_frame = atomic_load_long_long(&settings->max_frame_num);

    int sample_start = -1;

    if (framenum < min_frame)
        framenum = min_frame;
    if (framenum > max_frame)
        framenum = max_frame;

    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int start = 0;
        int end = 0;
        int loop = 0;
        int speed = 0;

        if (sample_get_short_info(info->uc->sample_id,
                                  &start, &end, &loop, &speed) != 0)
            return 0;

        (void)loop;
        (void)speed;

        sample_start = start;

        if (framenum < start)
            framenum = start;
        if (framenum > end)
            framenum = end;

        if (framenum == start || framenum == end)
            sample_set_framedups(info->uc->sample_id, 0);
    } else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) {
        if (framenum > max_frame)
            framenum = max_frame;
    }

    long long current_frame_num =
        atomic_load_long_long(&settings->current_frame_num);

#ifdef HAVE_JACK
    int track_align_force_reset_edge =
        atomic_load_int(&settings->track_align_force_audio_edge_reset);
#endif

    if ((long long)framenum != current_frame_num) {
        const int dir = playback_dir(settings->current_playback_speed);

        int edge_type = AUDIO_EDGE_JUMP;
        if (dir == 0)
            edge_type = AUDIO_EDGE_SILENCE;
        else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
            if (sample_start >= 0 && framenum == sample_start)
                edge_type = AUDIO_EDGE_RESET;
        } else {
            if ((long long)framenum == min_frame)
                edge_type = AUDIO_EDGE_RESET;
        }

#ifdef HAVE_JACK
        if (track_align_force_reset_edge) {
            atomic_store_int(&settings->track_align_force_audio_edge_reset, 0);
        }
#endif

        atomic_store_int(&settings->audio_slice, 0);
        settings->audio_last_stretched_samples = 0;

        vj_perform_initiate_edge_change(info, edge_type, dir, dir);
    }
#ifdef HAVE_JACK
    else if (track_align_force_reset_edge) {
        atomic_store_int(&settings->track_align_force_audio_edge_reset, 0);
    }
#endif

#ifdef HAVE_JACK
    if ((long long)framenum != current_frame_num)
        veejay_track_align_note_position_discontinuity(info, (long long)framenum, "set-frame");
#endif

    atomic_store_long_long(&settings->current_frame_num, framenum);
    return 1;
}

int	veejay_composite_active( veejay_t *info )
{
	return info->settings->composite;
}

void	veejay_auto_loop(veejay_t *info)
{
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN)
	{
		char sam[32];
		int len;

        len = snprintf(sam, sizeof(sam), "%03d:0 -1;", VIMS_SAMPLE_NEW);
        if(len > 0 && len < (int)sizeof(sam))
            vj_event_parse_msg(info, sam, len);

        len = snprintf(sam, sizeof(sam), "%03d:-1;", VIMS_SAMPLE_SELECT);
        if(len > 0 && len < (int)sizeof(sam))
            vj_event_parse_msg(info, sam, len);
	}
}

void	veejay_set_framerate( veejay_t *info , float fps )
{
    if (info == NULL || info->settings == NULL)
        return;

    video_playback_setup *settings = (video_playback_setup*) info->settings;

    fps = vj_runtime_clamp_fps(fps);

    if(settings->output_fps > 0.0f &&
       fabs((double)settings->output_fps - (double)fps) < 0.005)
    {
        return;
    }

    long long cur_frame = atomic_load_long_long(&settings->master_frame_num);
    if (cur_frame < 0)
        cur_frame = atomic_load_long_long(&settings->current_frame_num);
    if (cur_frame < 0)
        cur_frame = 0;

#ifdef HAVE_JACK
    const int align_speed_before = settings->current_playback_speed;
    const int align_sfd_before = veejay_track_align_current_sfd(info);
    const double align_old_fps = veejay_track_align_transport_fps(info);
    const int align_was_normal = veejay_track_align_is_normal_values(info,
                                                                     align_speed_before,
                                                                     align_sfd_before,
                                                                     align_old_fps);
#endif

    double old_spvf;
    double epoch_s;
    long long epoch_frame;
    double retime_s;
    double master_s;
    double media_fps;
    double new_runtime_rate;
    int using_audio_clock = 0;

    pthread_mutex_lock(&(settings->control_mutex));

    old_spvf = settings->spvf;
    if (old_spvf <= 0.0)
        old_spvf = vj_runtime_spvf_from_fps(settings->output_fps > 0.0f ? settings->output_fps : fps);

    epoch_s = atomic_load_double(&settings->fps_epoch_s);
    epoch_frame = atomic_load_long_long(&settings->fps_epoch_frame);

    master_s = vj_runtime_master_clock_now_s(info, &using_audio_clock);

    if (epoch_s <= 0.0) {
        epoch_s = master_s;
        epoch_frame = cur_frame;
    }

    retime_s = epoch_s + ((double)(cur_frame - epoch_frame) * old_spvf);

    if(using_audio_clock) {
        double max_audio_delta = old_spvf;
        if(max_audio_delta < 0.020)
            max_audio_delta = 0.020;
        else if(max_audio_delta > 0.080)
            max_audio_delta = 0.080;

        if(retime_s < master_s - max_audio_delta ||
           retime_s > master_s + max_audio_delta)
        {
            retime_s = master_s;
        }
    }
    else if(retime_s < master_s - (old_spvf * 2.0) || retime_s > master_s + 1.0) {
        retime_s = master_s;
    }

    atomic_store_double(&settings->fps_epoch_s, retime_s);
    atomic_store_long_long(&settings->fps_epoch_frame, cur_frame);

    settings->spvf = vj_runtime_spvf_from_fps(fps);
    settings->usec_per_frame = vj_el_get_usec_per_frame(fps);
    settings->output_fps = fps;
    media_fps = (info->edit_list && info->edit_list->video_fps > 0.0) ? info->edit_list->video_fps : fps;
    new_runtime_rate = (double)fps / media_fps;
    atomic_store_double(&settings->runtime_playback_rate, new_runtime_rate);
    settings->fps_generation++;

    vj_runtime_update_frame_fps(info, fps);

    pthread_mutex_unlock(&(settings->control_mutex));

#ifdef HAVE_JACK
    /* set_framerate() may be automated every frame.  Track Align treats it
     * as leaving/returning to the external source clock only when the exact
     * floating-point FPS crosses the source-FPS tolerance; no integer FPS
     * truncation is used here.
     */
    {
        const int align_speed_after = settings->current_playback_speed;
        const int align_sfd_after = veejay_track_align_current_sfd(info);
        const int align_is_normal = veejay_track_align_is_normal_values(info,
                                                                        align_speed_after,
                                                                        align_sfd_after,
                                                                        (double)fps);

        veejay_track_align_note_normal_state(info,
                                             align_was_normal,
                                             align_is_normal,
                                             "runtime-fps");
        if(atomic_load_int(&settings->track_align_linear_active))
            veejay_track_align_integrate_linear_active(info, "runtime-fps");
    }
    if(veejay_track_align_current_clip_active(info))
        vj_audio_sync_set_track_align_video_fps(&settings->audio_sync,
                                                veejay_track_align_media_fps(info));
#endif
}


int veejay_init_editlist(veejay_t * info)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    editlist *el = info->edit_list;

	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) el->total_frames);

    if (info->audio==AUDIO_PLAY && info->dummy->arate > 0)
    {
		settings->spas = 1.0 / (double) info->dummy->arate;
    }
    else
    {
 		settings->spas = 0.0;
    }

	if(info->video_output_height == 0 && info->video_output_width == 0) {
		info->video_output_width = el->video_width;
		info->video_output_height = el->video_height;
		veejay_msg(VEEJAY_MSG_WARNING, "Output dimensions not setup, using video resolution %dx%d", el->video_width, el->video_height);
	}

    return 0;
}

int	veejay_stop_playing_sample( veejay_t *info, int new_sample_id )
{
	video_playback_setup *settings = info->settings;
	int tx_active = atomic_load_int(&settings->transition.active);
    long long current_frame_num = atomic_load_long_long(&settings->current_frame_num);
	if( tx_active ) {
        return 0;
    }
	
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1; // back to top
		} 
	}

	sample_chain_free( info->uc->sample_id,0);
	sample_set_framedups(info->uc->sample_id,0);

	sample_set_resume(info->uc->sample_id, (long)current_frame_num);

	return 1;
}
static  void	veejay_stop_playing_stream( veejay_t *info, int new_stream_id )
{
	video_playback_setup *settings = info->settings;
	int tx_active = atomic_load_int(&settings->transition.active);
    if( tx_active ) {
        return;
    }

	vj_tag_disable( info->uc->sample_id );
	if( info->composite ) {
		if( info->settings->composite == 2 ) {
			info->settings->composite = 1;
		}
	}

	vj_tag_chain_free( info->uc->sample_id, 0);
}

static const char *vj_playback_mode_label(int pm)
{
    switch (pm) {
        case VJ_PLAYBACK_MODE_PLAIN:  return "plain";
        case VJ_PLAYBACK_MODE_SAMPLE: return "sample";
        case VJ_PLAYBACK_MODE_TAG:    return "stream";
        default:                      return "unknown";
    }
}

static const char *vj_looptype_label(int looptype)
{
    switch (looptype) {
        case 0: return "none";
        case 1: return "normal";
        case 2: return "pingpong";
        case 3: return "random";
        default: return "unknown";
    }
}

static void veejay_log_sample_transport_state(veejay_t *info,
                                              const char *where,
                                              int sample_id)
{
    if (!info || !info->settings)
        return;

    int start = 0;
    int end = 0;
    int looptype = 0;
    int speed = 0;

    if (sample_get_short_info(sample_id, &start, &end, &looptype, &speed) != 0)
        return;

    video_playback_setup *settings = info->settings;
    const long resume = sample_get_resume(sample_id);
    const long long cur = atomic_load_long_long(&settings->current_frame_num);
    const long long min = atomic_load_long_long(&settings->min_frame_num);
    const long long max = atomic_load_long_long(&settings->max_frame_num);
    const int seq_active = info->seq ? info->seq->active : 0;
    const int seq_slot = info->seq ? info->seq->current : -1;
    const int boundary = atomic_load_int(&settings->sequence_boundary);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[SEQ] %s sample=%d range=%d..%d resume=%ld transport=%lld minmax=%lld..%lld speed=%d slow=%d loop=%s loops=%d seq=%d slot=%d boundary=%d",
               where ? where : "state",
               sample_id,
               start,
               end,
               resume,
               cur,
               min,
               max,
               speed,
               sample_get_framedup(sample_id),
               vj_looptype_label(looptype),
               sample_get_loops(sample_id),
               seq_active,
               seq_slot,
               boundary);
}

int veejay_start_playing_sample(veejay_t *info, int sample_id)
{
    video_playback_setup *settings = info->settings;
    int looptype, speed, start, end;

    editlist *E = sample_get_editlist(sample_id);
    info->current_edit_list = E;
    veejay_reset_el_buffer(info);

    sample_get_short_info(sample_id, &start, &end, &looptype, &speed);

    settings->first_frame = 1;

    atomic_store_int(&settings->sequence_boundary, 0);
    settings->sequence_random_id = 0;
    settings->sequence_random_ticks_left = 0;

    atomic_store_long_long(&settings->min_frame_num, 0);
    atomic_store_long_long(&settings->max_frame_num,
                           (long long)sample_video_length(sample_id));

    info->uc->sample_id = sample_id;
    info->last_sample_id = sample_id;

    info->sfd = sample_get_framedup(sample_id);

    sample_set_loop_stats(sample_id, 0);
    sample_set_loops(sample_id, -1);

    if (speed != 0)
        settings->previous_playback_speed = speed;

    veejay_set_speed(info, speed, 1);

    vj_perform_global_chain_reset(info);

    veejay_msg(VEEJAY_MSG_INFO,
               "Playing sample %d (range %d..%d, speed %d, slow %d, loop %s, transport before seed %lld)",
               sample_id,
               start,
               end,
               speed,
               info->sfd,
               vj_looptype_label(looptype),
               atomic_load_long_long(&settings->current_frame_num));

    veejay_log_sample_transport_state(info, "start-sample", sample_id);

    return 1;
}


static int veejay_start_playing_stream(veejay_t *info, int stream_id)
{
    video_playback_setup *settings = info->settings;

    if (vj_tag_enable(stream_id) <= 0) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Unable to activate stream %d",
                   stream_id);
    }

    if (settings->current_playback_speed == 0)
        settings->current_playback_speed = 1;

    atomic_store_int(&settings->sequence_boundary, 0);
    settings->sequence_random_id = 0;
    settings->sequence_random_ticks_left = 0;

    atomic_store_long_long(&settings->min_frame_num, 0);
    atomic_store_long_long(&settings->max_frame_num,
                           (long long)vj_tag_get_n_frames(stream_id));

    info->last_tag_id = stream_id;
    info->uc->sample_id = stream_id;

    veejay_msg(VEEJAY_MSG_INFO,
               "Playing stream %d (%ld - %ld)",
               stream_id,
               settings->min_frame_num,
               settings->max_frame_num);

    info->current_edit_list = info->edit_list;

    veejay_reset_el_buffer(info);

    vj_tag_set_loop_stats(stream_id, 0);
    vj_tag_set_loops(stream_id, -1);

    vj_perform_global_chain_reset(info);

    return 1;
}

static void veejay_seq_prepare_sample_position(veejay_t *info, int sample_id)
{
    int start = 0;
    int end = 0;
    int loop = 0;
    int speed = 0;

    if (sample_get_short_info(sample_id, &start, &end, &loop, &speed) != 0)
        return;

    const long old_resume = sample_get_resume(sample_id);
    const long long old_frame = info && info->settings ?
        atomic_load_long_long(&info->settings->current_frame_num) : -1;

    sample_set_loop_stats(sample_id, 0);
    sample_set_loops(sample_id, -1);
    sample_set_resume_override(sample_id, -1);

    const int resume = (speed < 0) ? end : start;
    sample_set_resume(sample_id, resume);

    if (info && info->settings) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[SEQ] seed sample=%d range=%d..%d speed=%d loop=%s old_resume=%ld new_resume=%d old_transport=%lld bank=%d slot=%d",
                   sample_id,
                   start,
                   end,
                   speed,
                   vj_looptype_label(loop),
                   old_resume,
                   resume,
                   old_frame,
                   info->seq ? info->seq->active_bank : -1,
                   info->seq ? info->seq->current : -1);
    }
}

static int veejay_sequence_bank_valid(int bank)
{
    return bank >= 0 && bank < VJ_SEQUENCE_BANKS;
}

static int veejay_sequence_selected_bank_mask(veejay_t *info)
{
    sequencer_t *seq = info ? info->seq : NULL;
    int active_bank;
    int mask;

    if(!seq)
        return 0;

    active_bank = seq->active_bank;
    if(!veejay_sequence_bank_valid(active_bank))
        active_bank = 0;

    mask = seq->selected_bank_mask & ((1 << VJ_SEQUENCE_BANKS) - 1);
    if(mask == 0)
        mask = (1 << active_bank);

    mask |= (1 << active_bank);
    seq->selected_bank_mask = mask;

    return mask;
}

static int veejay_sequence_count_slots(const seq_sample_t *samples)
{
    int count = 0;

    for(int i = 0; i < MAX_SEQUENCES; i++)
        if(samples[i].sample_id > 0)
            count++;

    return count;
}

static void veejay_sequence_store_active_bank(veejay_t *info)
{
    sequencer_t *seq = info ? info->seq : NULL;
    int bank;

    if(!seq)
        return;

    bank = seq->active_bank;
    if(!veejay_sequence_bank_valid(bank))
        bank = 0;

    veejay_memcpy(seq->banks[bank].samples,
                  seq->samples,
                  sizeof(seq_sample_t) * MAX_SEQUENCES);
    seq->banks[bank].size = veejay_sequence_count_slots(seq->samples);
    seq->banks[bank].current = seq->current;
    seq->size = seq->banks[bank].size;
}

static void veejay_sequence_prepare_selected_sample_positions(veejay_t *info)
{
    int mask = veejay_sequence_selected_bank_mask(info);

    if(!info || !info->seq)
        return;

    veejay_sequence_store_active_bank(info);

    for(int bank = 0; bank < VJ_SEQUENCE_BANKS; bank++) {
        if(!(mask & (1 << bank)))
            continue;

        for(int i = 0; i < MAX_SEQUENCES; i++) {
            seq_sample_t *entry = &info->seq->banks[bank].samples[i];

            if(entry->type != 0)
                continue;

            int id = entry->sample_id;
            if(id <= 0 || !sample_exists(id))
                continue;

            veejay_seq_prepare_sample_position(info, id);
        }
    }
}


void veejay_change_playback_mode(veejay_t *info, int new_pm, int sample_id)
{
    video_playback_setup *settings = info->settings;
    int current_pm = info->uc->playback_mode;
    int cur_id = info->uc->sample_id;

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[PLAYBACK] change %s:%d -> %s:%d seq=%d slot=%d transition_ready=%d transport=%lld",
               vj_playback_mode_label(current_pm),
               cur_id,
               vj_playback_mode_label(new_pm),
               sample_id,
               info->seq ? info->seq->active : 0,
               info->seq ? info->seq->current : -1,
               settings->transition.ready,
               atomic_load_long_long(&settings->current_frame_num));

    if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        if (!sample_exists(sample_id)) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[Playback] Sample %d does not exist",
                       sample_id);
            return;
        }
    }
    else if (new_pm == VJ_PLAYBACK_MODE_TAG) {
        if (!vj_tag_exists(sample_id)) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[Playback] Stream %d does not exist",
                       sample_id);
            return;
        }
    }
    else if (new_pm == VJ_PLAYBACK_MODE_PLAIN) {
        if (info->edit_list->video_frames < 1) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[Playback] No video frames in EditList");
            return;
        }
    }

    veejay_output_hold_release_on_transport(info);

    if (current_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        if (cur_id == sample_id && new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
            if (!info->seq->active) {
                if (info->settings->sample_restart) {
                    veejay_msg(VEEJAY_MSG_INFO,
                               "[Playback] Restarting Sample %d",
                               cur_id);
                    sample_set_resume_override(cur_id, -1);
                    veejay_sample_resume_at(info, cur_id);
                }
                else {
                    veejay_msg(VEEJAY_MSG_INFO,
                               "[Playback] (Continuous mode) Sample %d already playing",
                               sample_id);
                }
                return;
            }
        }
        else {
            veejay_msg(VEEJAY_MSG_INFO,
                       "[Playback] Stopping Sample %d",
                       cur_id);
            veejay_stop_playing_sample(info, cur_id);
        }
    }

    if (current_pm == VJ_PLAYBACK_MODE_TAG) {
        if (cur_id != sample_id || new_pm != VJ_PLAYBACK_MODE_TAG) {
            veejay_msg(VEEJAY_MSG_INFO,
                       "[Playback] Stopping Stream %d",
                       cur_id);
            veejay_stop_playing_stream(info, cur_id);
        }
    }

    atomic_store_int(&settings->sequence_boundary, 0);
    settings->sequence_random_id = 0;
    settings->sequence_random_ticks_left = 0;

    if (new_pm == VJ_PLAYBACK_MODE_PLAIN) {
        info->uc->playback_mode = new_pm;
        info->current_edit_list = info->edit_list;

        atomic_store_long_long(&settings->min_frame_num, 0);
        atomic_store_long_long(&settings->max_frame_num,
                               (long long)info->edit_list->total_frames);
        atomic_store_long_long(&settings->current_frame_num, 0);
    }
    else if (new_pm == VJ_PLAYBACK_MODE_TAG) {
        info->uc->playback_mode = new_pm;

        atomic_store_long_long(&settings->min_frame_num, 0);
        atomic_store_long_long(&settings->max_frame_num,
                               (long long)vj_tag_get_n_frames(sample_id));
        atomic_store_long_long(&settings->current_frame_num, 0);

        veejay_start_playing_stream(info, sample_id);
    }
    else if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        info->uc->playback_mode = new_pm;
        veejay_start_playing_sample(info, sample_id);
    }

    if (info->seq->active) {
        if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
            veejay_seq_prepare_sample_position(info, sample_id);

            long pos = sample_get_resume(sample_id);
            veejay_set_frame(info, pos);
            veejay_log_sample_transport_state(info, "post-seed", sample_id);
        }
        else if (new_pm == VJ_PLAYBACK_MODE_TAG ||
                 new_pm == VJ_PLAYBACK_MODE_PLAIN) {
            veejay_set_frame(info, 0);
        }

        int next_id = sample_id;
        int next_bank = info->seq->active_bank;
        int next_slot = info->seq->current;
        int next_mode = new_pm;

        next_id = vj_perform_next_sequence(info, &next_mode, &next_bank, &next_slot);

        vj_perform_setup_transition(info,
                                    next_id,
                                    next_mode,
                                    sample_id,
                                    new_pm,
                                    next_bank,
                                    next_slot);
    }
    else if (new_pm == VJ_PLAYBACK_MODE_SAMPLE) {
        veejay_sample_resume_at(info, sample_id);
    }
}

void veejay_sample_set_initial_positions(veejay_t *info)
{
    int first_type = 0;
    int first_slot = 0;
    int first_id;

    if(!info || !info->seq)
        return;

    veejay_sequence_prepare_selected_sample_positions(info);

    first_id = vj_perform_get_next_sequence_id(info, &first_type, 0, &first_slot);
    if(first_id <= 0)
        return;

    info->seq->current = first_slot;
    if(veejay_sequence_bank_valid(info->seq->active_bank))
        info->seq->banks[info->seq->active_bank].current = first_slot;

    veejay_change_playback_mode(
        info,
        (first_type == 0 || first_type == VJ_PLAYBACK_MODE_SAMPLE
            ? VJ_PLAYBACK_MODE_SAMPLE
            : VJ_PLAYBACK_MODE_TAG),
        first_id);
}

void veejay_prepare_sample_positions(int id)
{
    veejay_seq_prepare_sample_position(NULL, id);
}

void veejay_reset_sample_positions(veejay_t *info, int sample_id)
{
    if(sample_id == -1) {
        veejay_sequence_prepare_selected_sample_positions(info);
    }
    else {
        if(sample_id <= 0 || !sample_exists(sample_id))
            return;

        veejay_seq_prepare_sample_position(info, sample_id);
    }
}

void veejay_set_sample(veejay_t * info, int sampleid)
{
   	if ( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
	{
		veejay_start_playing_stream(info,sampleid );
   	}
    else if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
	{
		if( info->uc->sample_id != sampleid )
			veejay_start_playing_sample(info, sampleid);
	}
}

int veejay_create_tag(veejay_t * info, int type, char *filename,
			int index, int channel, int device)
{
	if( type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST ) {
		if( (filename != NULL) && ((strcasecmp( filename, "localhost" ) == 0)  || (strcmp( filename, "127.0.0.1" ) == 0)) ) {
			if( channel == info->uc->port )	{
				veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to connect to myself (%s - %d)",
					filename,channel);
				return 0;
			}	   
		}
	}

	int id = vj_tag_new(type, filename, index, info->edit_list, info->pixel_format, channel, device,info->settings->composite );
	char descr[200];
	veejay_memset(descr,0,200);
	vj_tag_get_by_type(id,type,descr);
	if(id > 0 )	{
		info->nstreams++;
		veejay_msg(VEEJAY_MSG_INFO, "New Input Stream '%s' with ID %d created",descr, id );
		return id;
	} else	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create new Input Stream '%s'", descr );
    	}

 	return 0;
}

void veejay_stop_sampling(veejay_t * info)
{
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->sample_id = 0;
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
    info->current_edit_list = info->edit_list;
}

static int veejay_cond_wait_stop_poll(video_playback_setup *settings, pthread_cond_t *cv)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += 50000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec++;
    }

    pthread_cond_timedwait(cv, &settings->mutex, &ts);

    return atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP;
}

VJFrame *veejay_video_queue_reserve_buffer(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int idx = -1;

    pthread_mutex_lock(&settings->mutex);
    for (;;) {
        for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
            if (settings->states[i] == BUFFER_FREE) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) break;

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
        if (veejay_cond_wait_stop_poll(settings, &settings->producer_wait_cv)) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
    }

    settings->states[idx] = BUFFER_RESERVED;
    settings->buffers[idx]->queue_index = idx; 

    pthread_mutex_unlock(&settings->mutex);
    return settings->buffers[idx];
}

void veejay_video_queue_post_frame(veejay_t *info, VJFrame *vf)
{
    video_playback_setup *settings = info->settings;
    int idx = vf->queue_index;
    pthread_mutex_lock(&settings->mutex);

    settings->frame_seq[idx] = settings->next_seq++;
    settings->states[idx] = BUFFER_FILLED;

    pthread_cond_signal(&settings->renderer_wait_cv);
    pthread_mutex_unlock(&settings->mutex);
}
VJFrame *veejay_video_queue_get_frame(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int idx = -1;

    pthread_mutex_lock(&settings->mutex);
    for (;;) {
        uint64_t best_seq = UINT64_MAX;
        idx = -1;
        for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
            if (settings->states[i] == BUFFER_FILLED && settings->frame_seq[i] < best_seq) {
                best_seq = settings->frame_seq[i];
                idx = i;
            }
        }
        if (idx >= 0) break;
        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
        if (veejay_cond_wait_stop_poll(settings, &settings->renderer_wait_cv)) {
            pthread_mutex_unlock(&settings->mutex);
            return NULL;
        }
    }

    settings->states[idx] = BUFFER_IN_RENDER;
    pthread_mutex_unlock(&settings->mutex);
	
    return settings->buffers[idx];
}

void video_queue_return_frame(veejay_t *info, VJFrame *vf)
{
    video_playback_setup *settings = info->settings;
    int idx = vf->queue_index;

    pthread_mutex_lock(&settings->mutex);
    settings->states[idx] = BUFFER_FREE;

    pthread_cond_signal(&settings->producer_wait_cv);
    pthread_mutex_unlock(&settings->mutex);
}

static void veejay_screen_update(veejay_t *info, VJFrame *frame_to_display) {
    video_playback_setup *settings = (video_playback_setup*) info->settings;
    display_frame_t *df = &settings->display_frame;

    int render_vp  = settings->composite;
    int write_index = (df->current_write + 1) % VIDEO_QUEUE_LEN;
    uint8_t *pixels = df->pixels[write_index];

#ifdef HAVE_SDL
    if (info->video_out == 0 && !pixels && info->sdl) {
        pixels = vj_sdl_get_buffer(info->sdl, write_index);
        df->pixels[write_index] = pixels;
    }
#endif

    if (info->video_out == 0 && !pixels) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL display buffer %d is NULL; frame %lld skipped",
                   write_index,
                   frame_to_display ? frame_to_display->frame_num : -1LL);
        return;
    }

	switch (info->video_out) {
		case 0:
			if (render_vp == 0) {
				vj_sdl_convert_to_screen(info->sdl, frame_to_display, pixels );

			} else {
				composite_blit_yuyv(info->composite, frame_to_display->data, pixels, render_vp);
			}
			break;// SDL
		case 5:
			break;
		case 4: // Y4M
		case 6:
			if( vj_yuv_put_frame( info->y4m, frame_to_display->data ) == -1 ) {
				veejay_msg(0, "Failed to write a frame");
				veejay_change_state(info,LAVPLAY_STATE_STOP);
				return;
			}
			break;
		default:
			break; // Headless 
	}

	if( info->vloopback )
	{
		if(vj_vloopback_fill_buffer( info->vloopback , frame_to_display ))
			vj_vloopback_write( info->vloopback );
	}

	if( info->splitter ) {
		vj_split_render( info->splitter );
	}
	
	if( atomic_load_int(&info->settings->unicast_frame_sender) ) {
		vj_perform_send_primary_frame_s2(info, 0, info->uc->current_link, frame_to_display );
	}

	if( info->settings->mcast_frame_sender && info->settings->use_vims_mcast ) {
		vj_perform_send_primary_frame_s2(info, 1, info->uc->current_link, frame_to_display);
	}

	if( info->shm && vj_shm_get_status(info->shm) == 1 ) {
		int plane_sizes[4] = { frame_to_display->len, frame_to_display->uv_len,
		   		frame_to_display->uv_len,0 };	
		if( vj_shm_write(info->shm, frame_to_display->data,plane_sizes) == -1 ) {
			veejay_msg(0, "[DISPLAY] Failed to write to shared resource");
		}
	}


#ifdef HAVE_JPEG
#ifdef USE_GDK_PIXBUF 
	if (atomic_load_int(&info->uc->take_screenshot) == 1)
	{
		atomic_store_int(&info->uc->take_screenshot,0);
#ifdef USE_GDK_PIXBUF
		if(!vj_picture_save( info->settings->export_image, frame_to_display->data, 
			info->video_output_width, info->video_output_height,
			get_ffmpeg_pixfmt( info->pixel_format )) )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to write frame %lld to image as '%s'",	frame_to_display->frame_num, info->settings->export_image );
		}
#else
#ifdef HAVE_JPEG
		vj_perform_screenshot2(info, frame_to_display);
		if(info->uc->filename) free(info->uc->filename);
#endif
#endif
    }
#endif
#endif

    df->current_write = write_index;
    settings->display_frame.pixels[write_index] = pixels;
    atomic_store_long_long(&settings->display_frame.seq, frame_to_display->frame_num);
}

static int veejay_pipe_status_token_count(const char *s)
{
    int count = 0;
    int in_token = 0;

    if(!s)
        return 0;

    while(*s) {
        const unsigned char c = (unsigned char)*s++;
        if(c > ' ') {
            if(!in_token) {
                count++;
                in_token = 1;
            }
        } else {
            in_token = 0;
        }
    }

    return count;
}

static char *veejay_pipe_write_zero_base_status(char *ptr)
{
    for(int i = 0; i < VJ_STATUS_BASE_TOKENS; i++)
        ptr = vj_sprintf(ptr, 0);

    *ptr = '\0';
    return ptr;
}

static char *veejay_pipe_pad_status_tokens(char *start, char *ptr, int expected_tokens)
{
    int tokens = veejay_pipe_status_token_count(start);

    while(tokens < expected_tokens) {
        ptr = vj_sprintf(ptr, 0);
        tokens++;
    }

    return ptr;
}

static inline int veejay_record_audio_source_status(video_playback_setup *settings)
{
#ifndef HAVE_JACK
    return VJ_RECORD_AUDIO_SOURCE_ORIGINAL;
#endif

    int source = VJ_RECORD_AUDIO_SOURCE_AUTO;

    if(settings)
        source = atomic_load_int(&settings->record_audio_source);

    source = source < VJ_RECORD_AUDIO_SOURCE_AUTO
        ? VJ_RECORD_AUDIO_SOURCE_AUTO
        : (source > VJ_RECORD_AUDIO_SOURCE_SILENCE
            ? VJ_RECORD_AUDIO_SOURCE_SILENCE
            : source);

    return source;
}

static inline int veejay_audio_beat_runtime_action(int action)
{
#ifdef HAVE_JACK
    switch(action) {
        case VJ_AUDIO_BEAT_ACTION_AUTO_FX:
        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX:
        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT:
            return action;
        default:
            return VJ_AUDIO_BEAT_ACTION_NONE;
    }
#else
    (void)action;
    return 0;
#endif
}

static char *veejay_pipe_append_audio_beat_status(veejay_t *info, char *ptr)
{
    video_playback_setup *settings = info->settings;
    const int audio_mute = settings->audio_mute;
    const int record_audio_source = veejay_record_audio_source_status(settings);
#ifdef HAVE_JACK
    vj_audio_beat_snapshot_t snap;
    int ok = 0;

    if(info && info->settings)
        ok = vj_audio_beat_get_snapshot(&info->settings->audio_beat, &snap);

#define AB_PCT(v_) \
    ((int)((((v_) < 0.0f) ? 0.0f : (((v_) > 1.0f) ? 1.0f : (v_))) * 100.0f) + 0.5f)

#define AB_BPM10(v_) \
    ((int)((((v_) < 0.0f) ? 0.0f : (((v_) > 9999.9f) ? 9999.9f : (v_))) * 10.0f) + 0.5f)

#define AB_AGE(v_) \
    ((int)(((v_) < 0 || (v_) > 999999L) ? 999999L : (v_)))

#define AB_POS(v_) \
    ((int)(((v_) < 0) ? 0 : (v_)))

    ptr = vj_sprintf(ptr, ok ? (snap.enabled ? 1 : 0) : 0);         /* 39 */
    ptr = vj_sprintf(ptr, ok ? (snap.open ? 1 : 0) : 0);            /* 40 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.level) : 0);             /* 41 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.transient) : 0);         /* 42 */
    ptr = vj_sprintf(ptr, ok ? AB_POS((int)snap.hits) : 0);         /* 43 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.envelope) : 0);          /* 44 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.flux) : 0);              /* 45 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.bass) : 0);              /* 46 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.mid) : 0);               /* 47 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.high) : 0);              /* 48 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.beat_pulse) : 0);        /* 49 */
    ptr = vj_sprintf(ptr, ok ? AB_PCT(snap.beat_gate) : 0);         /* 50 */
    ptr = vj_sprintf(ptr, ok ? AB_BPM10(snap.bpm) : 0);             /* 51 */
    ptr = vj_sprintf(ptr, ok ? AB_AGE(snap.beat_age_ms) : 999999);  /* 52 */
    ptr = vj_sprintf(ptr, ok ? AB_POS(snap.sample_rate) : 0);       /* 53 */
    ptr = vj_sprintf(ptr, ok ? AB_POS(snap.hit_seq) : 0);           /* 54 */
    ptr = vj_sprintf(ptr, audio_mute);                              /* 55 */
    ptr = vj_sprintf(ptr, record_audio_source);                     /* 56 */
    {
        int action = ok ? vj_audio_beat_get_action(&info->settings->audio_beat) : 0;
        ptr = vj_sprintf(ptr, veejay_audio_beat_runtime_action(action)); /* 57 */
    }
#undef AB_POS
#undef AB_AGE
#undef AB_BPM10
#undef AB_PCT

#else
    ptr = vj_sprintf(ptr, 0);       /* 39 enabled */
    ptr = vj_sprintf(ptr, 0);       /* 40 open */
    ptr = vj_sprintf(ptr, 0);       /* 41 level */
    ptr = vj_sprintf(ptr, 0);       /* 42 transient */
    ptr = vj_sprintf(ptr, 0);       /* 43 hits */
    ptr = vj_sprintf(ptr, 0);       /* 44 envelope */
    ptr = vj_sprintf(ptr, 0);       /* 45 flux */
    ptr = vj_sprintf(ptr, 0);       /* 46 bass */
    ptr = vj_sprintf(ptr, 0);       /* 47 mid */
    ptr = vj_sprintf(ptr, 0);       /* 48 high */
    ptr = vj_sprintf(ptr, 0);       /* 49 pulse */
    ptr = vj_sprintf(ptr, 0);       /* 50 gate */
    ptr = vj_sprintf(ptr, 0);       /* 51 bpm_x10 */
    ptr = vj_sprintf(ptr, 999999);  /* 52 age_ms */
    ptr = vj_sprintf(ptr, 0);       /* 53 sample_rate */
    ptr = vj_sprintf(ptr, 0);       /* 54 hit_seq */
    ptr = vj_sprintf(ptr, 1);       /* 55 muted */
    ptr = vj_sprintf(ptr, record_audio_source); /* 56 */
    ptr = vj_sprintf(ptr, 0);       /* 57 action */
#endif

    return ptr;
}

static char *veejay_pipe_append_audio_sync_status(veejay_t *info, char *ptr)
{
#ifdef HAVE_JACK
    vj_audio_sync_snapshot_t snap;
    int ok = 0;

    if(info && info->settings)
        ok = vj_audio_sync_get_snapshot(&info->settings->audio_sync, &snap);

#define AS_PCT(v_) \
    ((int)((((v_) < 0.0f) ? 0.0f : (((v_) > 1.0f) ? 1.0f : (v_))) * 100.0f) + 0.5f)

#define AS_BPM10(v_) \
    ((int)((((v_) < 0.0f) ? 0.0f : (((v_) > 9999.9f) ? 9999.9f : (v_))) * 10.0f) + 0.5f)

#define AS_POS(v_) \
    ((int)(((v_) < 0) ? 0 : (v_)))

#define AS_RATIO1000(v_) \
    ((int)((((v_) < 0.001) ? 0.001 : (((v_) > 9.999) ? 9.999 : (v_))) * 1000.0) + 0.5)

#define AS_CORR100(v_) \
    ((int)((((v_) < 0.0) ? 0.0 : (((v_) > 9.999) ? 9.999 : (v_))) * 100.0) + 0.5)

    ptr = vj_sprintf(ptr, ok ? snap.enabled : 0);                         /* 58 */
    ptr = vj_sprintf(ptr, ok ? snap.open : 0);                            /* 59 */
    ptr = vj_sprintf(ptr, ok ? snap.running : 0);                         /* 60 */
    ptr = vj_sprintf(ptr, ok ? snap.mode : 0);                            /* 61 */
    ptr = vj_sprintf(ptr, ok ? snap.source : 0);                          /* 62 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.channels) : 0);                /* 63 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.sample_rate) : 0);             /* 64 */
    ptr = vj_sprintf(ptr, ok ? AS_PCT(snap.level) : 0);                   /* 65 */
    ptr = vj_sprintf(ptr, ok ? AS_PCT(snap.transient) : 0);               /* 66 */
    ptr = vj_sprintf(ptr, ok ? AS_BPM10(snap.bpm) : 0);                   /* 67 */
    ptr = vj_sprintf(ptr, ok ? AS_PCT(snap.beat_phase) : 0);              /* 68 */
    ptr = vj_sprintf(ptr, ok ? AS_PCT(snap.confidence) : 0);              /* 69 */
    ptr = vj_sprintf(ptr, ok ? ((snap.mode != VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW && vj_audio_sync_mode_uses_external_playback(snap.mode) && snap.open) ? 1 : 0) : 0); /* 70 external playback active */
    ptr = vj_sprintf(ptr, ok ? AS_RATIO1000(snap.bridge_ratio) : 1000);   /* 71 */
    ptr = vj_sprintf(ptr, ok ? AS_CORR100(snap.bridge_correction) : 100); /* 72 */
    ptr = vj_sprintf(ptr, ok ? snap.target_mode : 0);                     /* 73 */
    ptr = vj_sprintf(ptr, ok ? AS_BPM10(snap.target_bpm) : 0);            /* 74 */
    ptr = vj_sprintf(ptr, ok ? AS_PCT(snap.target_confidence) : 0);       /* 75 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.max_correction_pct) : 0);      /* 76 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.bridge_state) : 0);            /* 77 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.track_align_locked) : 0);      /* 78 */
    ptr = vj_sprintf(ptr, ok ? snap.track_align_offset_ms : 0);           /* 79 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.track_align_confidence_pct) : 0); /* 80 */
    ptr = vj_sprintf(ptr, ok ? snap.track_align_correction_ppm : 0);      /* 81 */
    ptr = vj_sprintf(ptr, ok ? AS_POS(snap.track_align_state) : 0);       /* 82 */

#undef AS_CORR100
#undef AS_RATIO1000
#undef AS_POS
#undef AS_BPM10
#undef AS_PCT

#else
    for(int i = 0; i < VJ_AUDIO_SYNC_STATUS_TOKENS; i++) {
        ptr = vj_sprintf(ptr, 0);
    }
#endif

    return ptr;
}

static char *veejay_pipe_append_zero_chain_entry_status(char *ptr)
{
    for(int i = 0; i < VJ_CHAIN_ENTRY_STATUS_TOKENS; i++)
        ptr = vj_sprintf(ptr, 0);

    return ptr;
}

static int veejay_pipe_chain_entry_display_arg(veejay_t *info, sample_eff_chain *entry, int param_nr)
{
    int value = entry->arg[param_nr];

    if(info && info->settings && entry->kf_status && entry->kf) {
        int kf_value = value;
        long long n_frame = atomic_load_long_long(&info->settings->current_frame_num);

        if(get_keyframe_value(entry->kf, n_frame, param_nr, &kf_value))
            value = kf_value;
    }

    return value;
}

static char *veejay_pipe_append_chain_entry_status(veejay_t *info, char *ptr)
{
    sample_eff_chain **src = NULL;
    sample_eff_chain *entry = NULL;
    int current_id;
    int entry_id = -1;
    int effect_id;
    int is_video;
    int num_params;

    if(!info || !info->uc)
        return veejay_pipe_append_zero_chain_entry_status(ptr);

    current_id = info->uc->sample_id;

    if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        src = sample_get_effect_chain(current_id);
        entry_id = sample_get_selected_entry(current_id);
    }
    else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) {
        src = vj_tag_get_effect_chain(current_id);
        entry_id = vj_tag_get_selected_entry(current_id);
    }

    if(!src || entry_id < 0 || entry_id >= SAMPLE_MAX_EFFECTS)
        return veejay_pipe_append_zero_chain_entry_status(ptr);

    entry = src[entry_id];
    if(!entry)
        return veejay_pipe_append_zero_chain_entry_status(ptr);

    effect_id = entry->effect_id;
    if(effect_id <= 0 || !vje_is_valid(effect_id))
        return veejay_pipe_append_zero_chain_entry_status(ptr);

    is_video = vje_get_extra_frame(effect_id) ? 1 : 0;
    num_params = vje_get_num_params(effect_id);
    if(num_params < 0)
        num_params = 0;
    else if(num_params > VJ_STATUS_CHAIN_ENTRY_PARAMETERS)
        num_params = VJ_STATUS_CHAIN_ENTRY_PARAMETERS;

    ptr = vj_sprintf(ptr, effect_id);              /* 83 */
    ptr = vj_sprintf(ptr, is_video);               /* 84 */
    ptr = vj_sprintf(ptr, num_params);             /* 85 */
    ptr = vj_sprintf(ptr, entry->kf_type);         /* 86 */
    ptr = vj_sprintf(ptr, entry->kf_status);       /* 87 */
    ptr = vj_sprintf(ptr, 0);                      /* 88 transition enabled */
    ptr = vj_sprintf(ptr, 0);                      /* 89 transition loop */
    ptr = vj_sprintf(ptr, entry->source_type);     /* 90 */
    ptr = vj_sprintf(ptr, entry->channel);         /* 91 */
    ptr = vj_sprintf(ptr, entry->e_flag);          /* 92 */
    ptr = vj_sprintf(ptr, entry->beat_flag);       /* 93 */
    ptr = vj_sprintf(ptr, entry->is_rendering);    /* 94 */

    for(int i = 0; i < VJ_STATUS_CHAIN_ENTRY_PARAMETERS; i++) {
        int value = 0;

        if(i < num_params)
            value = veejay_pipe_chain_entry_display_arg(info, entry, i);

        ptr = vj_sprintf(ptr, value); /* 95..110 */
    }

    return ptr;
}

static char *veejay_pipe_append_sequence_status(veejay_t *info, char *ptr)
{
    sequencer_t *seq = info->seq;

    if(!seq) {
        for(int i = 0; i < VJ_SEQUENCE_STATUS_TOKENS; i++)
            ptr = vj_sprintf(ptr, 0);
        return ptr;
    }

    ptr = vj_sprintf(ptr, seq->active_bank);
    ptr = vj_sprintf(ptr, (int)seq->revision);
    ptr = vj_sprintf(ptr, seq->size);

    for(int i = 0; i < VJ_SEQUENCE_BANKS; i++)
        ptr = vj_sprintf(ptr, (int)seq->banks[i].revision);

    int active_bank = seq->active_bank;
    if(active_bank < 0 || active_bank >= VJ_SEQUENCE_BANKS)
        active_bank = 0;
    int selected_mask = seq->selected_bank_mask & ((1 << VJ_SEQUENCE_BANKS) - 1);
    if(selected_mask == 0)
        selected_mask = (1 << active_bank);
    selected_mask |= (1 << active_bank);
    ptr = vj_sprintf(ptr, selected_mask);

    return ptr;
}

static int veejay_pipe_current_sample_audio_volume(veejay_t *info)
{
    int volume = 100;

    if(info && info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int sample_id = info->uc->sample_id;

        if(sample_exists(sample_id)) {
            volume = sample_get_audio_volume(sample_id);
            if(volume < 0)
                volume = 100;
        }
    }

    if(volume < 0)
        return 0;
    if(volume > 100)
        return 100;

    return volume;
}

static char *veejay_pipe_append_audio_mixer_status(veejay_t *info, char *ptr)
{
    ptr = vj_sprintf(ptr, veejay_pipe_current_sample_audio_volume(info));
    ptr = vj_sprintf(ptr, vj_perform_get_audio_mix_crossfade(info));

    return ptr;
}

static char *veejay_pipe_append_audio_beat_config_status(veejay_t *info, char *ptr)
{
#ifdef HAVE_JACK
    vj_audio_beat_shared_t *s = NULL;

    if(info && info->settings)
        s = &info->settings->audio_beat;

    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_freeze_ms(s) : 90);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_cooldown_ms(s) : 240);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_threshold(s) : 145);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_input_channels(s) : 2);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_pulse_ms(s) : 180);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_gate_ms(s) : 90);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_auto_mode(s) : 2);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_auto_amount(s) : 75);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_scratch_sensitivity(s) : 50);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_source_loss_pause(s) : 1);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_monitor_latency_ms(s) : VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS);
    ptr = vj_sprintf(ptr, s ? vj_audio_beat_get_effective_latency_ms(s) : -1);
#else
    ptr = vj_sprintf(ptr, 90);
    ptr = vj_sprintf(ptr, 240);
    ptr = vj_sprintf(ptr, 145);
    ptr = vj_sprintf(ptr, 2);
    ptr = vj_sprintf(ptr, 180);
    ptr = vj_sprintf(ptr, 90);
    ptr = vj_sprintf(ptr, 2);
    ptr = vj_sprintf(ptr, 75);
    ptr = vj_sprintf(ptr, 50);
    ptr = vj_sprintf(ptr, 1);
    ptr = vj_sprintf(ptr, VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS);
    ptr = vj_sprintf(ptr, -1);
#endif
    return ptr;
}

static void veejay_pipe_write_status(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    int d_len = 0;
    int pm = info->uc->playback_mode;

    int cache_used = 0;
    int mstatus = 0;
    int curfps  = (int) (100.0f / settings->spvf);
    int sample_count = sample_size();
    int tag_count = vj_tag_size();
    int total_slots = sample_count + tag_count;
    int seq_cur = (info->seq->active ? info->seq->current : MAX_SEQUENCES);

    veejay_memset(info->status_what, 0, VJ_STATUS_BUF_SIZE);

    switch (info->uc->playback_mode)
    {
        case VJ_PLAYBACK_MODE_SAMPLE:
            cache_used = sample_cache_used(0);

            mstatus = vj_macro_get_status(sample_get_macro(info->uc->sample_id));

            if (info->settings->randplayer.mode == RANDMODE_SAMPLE)
                pm = VJ_PLAYBACK_MODE_PATTERN;

            if (sample_chain_sprint_status(
                    info->uc->sample_id,
                    tag_count,
                    sample_count,
                    cache_used,
                    info->seq->active,
                    seq_cur,
                    info->real_fps,
                    settings->current_frame_num,
                    pm,
                    total_slots,
                    info->seq->rec_id,
                    curfps,
                    settings->cycle_count[0],
                    settings->cycle_count[1],
                    mstatus,
                    info->status_what,
                    settings->feedback,
                    info->global_chain->enabled,
                    info->uc->vims_mirror) != 0)
            {
                veejay_msg(VEEJAY_MSG_ERROR,
                           "Fatal error, tried to collect properties of invalid sample");
                veejay_change_state(info, LAVPLAY_STATE_STOP);
            }
            break;

        case VJ_PLAYBACK_MODE_PLAIN:
            {
                char *ptr = info->status_what;

                ptr = vj_sprintf(ptr, info->real_fps);                 // 0
                ptr = vj_sprintf(ptr, settings->current_frame_num);    // 1
                ptr = vj_sprintf(ptr, info->uc->playback_mode);        // 2

                *ptr++ = '0';
                *ptr++ = ' ';
                *ptr++ = '0';
                *ptr++ = ' ';                                          // 4

                ptr = vj_sprintf(ptr, settings->min_frame_num);        // 5
                ptr = vj_sprintf(ptr, settings->max_frame_num);        // 6
                ptr = vj_sprintf(ptr, settings->current_playback_speed); // 7

                for (int i = 0; i < 4; i++) {
                    *ptr++ = '0';
                    *ptr++ = ' ';
                }                                                       // 8..11

                ptr = vj_sprintf(ptr, sample_count);                   // 12

                for (int i = 0; i < 3; i++) {
                    *ptr++ = '0';
                    *ptr++ = ' ';
                }                                                       // 13..15

                ptr = vj_sprintf(ptr, total_slots);                    // 16
                ptr = vj_sprintf(ptr, cache_used);                     // 17
                ptr = vj_sprintf(ptr, curfps);                         // 18
                ptr = vj_sprintf(ptr, settings->cycle_count[0]);       // 19
                ptr = vj_sprintf(ptr, settings->cycle_count[1]);       // 20

                for (int i = 0; i < 3; i++) {
                    *ptr++ = '0';
                    *ptr++ = ' ';
                }                                                       // 21..23

                ptr = vj_sprintf(ptr, info->sfd);                      // 24
                ptr = vj_sprintf(ptr, mstatus);                        // 25

                for (int i = 0; i < 9; i++) {
                    *ptr++ = '0';
                    *ptr++ = ' ';
                }                                                       // 26..34

                ptr = vj_sprintf(ptr, settings->feedback);             // 35
                ptr = vj_sprintf(ptr, tag_count);                      // 36
                ptr = vj_sprintf(ptr, info->global_chain->enabled);    // 37
                ptr = vj_sprintf(ptr, info->uc->vims_mirror);          // 38
                *ptr = '\0';
            }
            break;

        case VJ_PLAYBACK_MODE_TAG:
            mstatus = vj_macro_get_status(vj_tag_get_macro(info->uc->sample_id));

            if (vj_tag_sprint_status(
                    info->uc->sample_id,
                    tag_count,
                    sample_count,
                    cache_used,
                    info->seq->active,
                    seq_cur,
                    info->real_fps,
                    settings->current_frame_num,
                    info->uc->playback_mode,
                    total_slots,
                    info->seq->rec_id,
                    curfps,
                    settings->cycle_count[0],
                    settings->cycle_count[1],
                    mstatus,
                    info->status_what,
                    settings->feedback,
                    info->global_chain->enabled,
                    info->uc->vims_mirror) != 0)
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid status");
            }
            break;
    }

    size_t base_len = strnlen(info->status_what, VJ_STATUS_BUF_SIZE);
    if (base_len >= VJ_STATUS_BUF_SIZE)
        base_len = VJ_STATUS_BUF_SIZE - 1;

    char *ptr = info->status_what + base_len;
    const size_t status_tail_room =
        (size_t)(VJ_AUDIO_STATUS_TOKENS +
                 VJ_CHAIN_ENTRY_STATUS_TOKENS +
                 VJ_SEQUENCE_STATUS_TOKENS +
                 VJ_AUDIO_BEAT_CONFIG_STATUS_TOKENS +
                 VJ_AUDIO_MIXER_STATUS_TOKENS) *
        (size_t)VJ_INT_FIELD_MAX;
    const int base_tokens = veejay_pipe_status_token_count(info->status_what);
    static int status_packet_warned = 0;

    if (base_tokens != VJ_STATUS_BASE_TOKENS ||
        base_len > (VJ_STATUS_BUF_SIZE - 1 - status_tail_room))
    {
        if(!status_packet_warned) {
            veejay_msg(VEEJAY_MSG_WARNING,
                       "Status packet base mismatch: got %d fields, expected %d; padding fixed %d-token packet",
                       base_tokens, VJ_STATUS_BASE_TOKENS, VIMS_STATUS_TOKENS);
            status_packet_warned = 1;
        }

        ptr = veejay_pipe_write_zero_base_status(info->status_what);
    }

    ptr = veejay_pipe_append_audio_beat_status(info, ptr);
    ptr = veejay_pipe_append_audio_sync_status(info, ptr);
    ptr = veejay_pipe_append_chain_entry_status(info, ptr);
    ptr = veejay_pipe_append_sequence_status(info, ptr);
    ptr = veejay_pipe_append_audio_beat_config_status(info, ptr);
    ptr = veejay_pipe_append_audio_mixer_status(info, ptr);
    ptr = veejay_pipe_pad_status_tokens(info->status_what, ptr, VIMS_STATUS_TOKENS);

    *ptr = '\0';

    d_len = (int)(ptr - info->status_what);

    if (d_len > VJ_STATUS_WIRE_MAX_PAYLOAD) {
        d_len = VJ_STATUS_WIRE_MAX_PAYLOAD;
        info->status_what[d_len] = '\0';
    }

    if (d_len > (VJ_STATUS_BUF_SIZE - VJ_STATUS_WIRE_HEADER_LEN - 1)) {
        d_len = VJ_STATUS_BUF_SIZE - VJ_STATUS_WIRE_HEADER_LEN - 1;
        info->status_what[d_len] = '\0';
    }

    info->status_line_len = d_len + VJ_STATUS_WIRE_HEADER_LEN;

    snprintf(info->status_line,
             VJ_STATUS_BUF_SIZE,
             "V%04dS%s",
             d_len,
             info->status_what);

    if (info->uc->chain_changed == 1)
        info->uc->chain_changed = 0;
}


static inline char *veejay_concat_paths(const char *path, const char *suffix)
{
    size_t len1 = strlen(path);
    size_t len2 = strlen(suffix);

    int need_slash = (len1 > 0 && path[len1 - 1] != '/');

    size_t total = len1 + len2 + (need_slash ? 1 : 0) + 1;

    char *str = vj_calloc(total);
	if(!str) {
		return NULL;
	}

    char *p = str;

    memcpy(p, path, len1);
    p += len1;

    if (need_slash) {
        *p++ = '/';
    }

    memcpy(p, suffix, len2);
    p += len2;

    *p = '\0';

    return str;
}

static inline int	veejay_is_dir(char *path)
{
	struct stat s;
	if( stat( path, &s ) == -1 )
	{
		veejay_msg(0, "%s (%s)", strerror(errno),path);
		return 0;
	}
	if( !S_ISDIR( s.st_mode )) {
		veejay_msg(0, "%s is not a valid path");
		return 0;
	}
	return 1;
}	
static	int	veejay_valid_homedir(char *path)
{
	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	char *theme_dir    = veejay_concat_paths( path, "theme" );
	int sum = veejay_is_dir( recovery_dir );
	sum += veejay_is_dir( theme_dir );
	sum += veejay_is_dir( path );
	free(theme_dir);
	free(recovery_dir);
	if( sum == 3 ) 
		return 1;
	return 0;
}
static	int	veejay_create_homedir(char *path)
{
	if( mkdir(path,0700 ) == -1 )
	{
		if( errno != EEXIST )
		{
			veejay_msg(0, "Unable to create %s - No veejay home setup (%s)", strerror(errno));
			return 0;	
		}
	}

	char *recovery_dir = veejay_concat_paths( path, "recovery" );
	if( mkdir(recovery_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(recovery_dir);
			return 0;
		}
	}
	free(recovery_dir);

	char *theme_dir = veejay_concat_paths( path, "theme" );
	if( mkdir(theme_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(theme_dir);
			return 0;
		}
	}
	free(theme_dir);
	
	char *font_dir = veejay_concat_paths( path, "fonts" );
	if( mkdir(font_dir,0700) == -1 ) {
		if( errno != EEXIST )
		{
			veejay_msg(0, "%s (%s)", strerror(errno), path);
			free(font_dir);
			return 0;
		}
	}
	free(font_dir);
	return 1;
}

#define PATH_TMP_SIZE 1024
#define PATH_SUFFIX_SIZE 64
void	veejay_check_homedir(void *arg)
{
	veejay_t *info = (veejay_t *) arg;
	char path[ PATH_TMP_SIZE - PATH_SUFFIX_SIZE ];
	char tmp[ PATH_TMP_SIZE ];
	char *home = getenv("HOME");
	if(!home)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "HOME environment variable not set");
		return;
	}
	
	int n = snprintf(path,sizeof(path), "%s/.veejay", home );
	if( n < 0 || n >= (int) sizeof(path)) {
		veejay_msg(VEEJAY_MSG_ERROR, "HOME path too long: %s", home );
		return;
	}

	info->homedir = vj_strndup( path, sizeof(path) );

	if( veejay_valid_homedir(path) == 0)
	{
		if( veejay_create_homedir(path) == 0 )
		{	
			veejay_msg(VEEJAY_MSG_ERROR,
				"Can't create %s",path);
			return;
		}
	}

	n = snprintf(tmp,sizeof(tmp), "%s/plugins.cfg", path );
	if(n < 0 || n >= (int) sizeof(tmp)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Path too long for plugins.cfg: %s", path );
		return;
	}

	struct statfs ts;
	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No plugin configuration file found in [%s] (see DOC/HowtoPlugins)",tmp);
	}
	
	n = snprintf(tmp,sizeof(tmp), "%s/viewport.cfg", path);
	if( n < 0 || n >= (int) sizeof(tmp)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Path too long for viewport.cfg: %s",path);
		return;
	}

	if( statfs( tmp, &ts ) != 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"No projection mapping file found in [%s]", tmp);
	    veejay_msg(VEEJAY_MSG_WARNING,"Press CTRL-s to setup then CTRL-h for help. Press CTRL-s again to exit, CTRL-p to switch");
	}
}

void veejay_handle_signal(int sig, siginfo_t *si, void *unused)
{
    veejay_t *info = veejay_instance_;

    switch (sig) {
        case SIGINT:
        case SIGQUIT:
            veejay_request_stop_fast(info);
            break;

        case SIGSEGV:
        case SIGBUS:
        case SIGPWR:
        case SIGABRT:
        case SIGFPE:
            if (info && info->homedir) {
                veejay_change_state_save(info, LAVPLAY_STATE_STOP);
            } else if (info) {
                veejay_change_state(info, LAVPLAY_STATE_STOP);
            }
            signal(sig, SIG_DFL);
            break;

        case SIGPIPE:
            if (info) veejay_change_state(info, LAVPLAY_STATE_STOP);
            break;

        default:
            veejay_msg(VEEJAY_MSG_WARNING, "Unhandled signal %d received", sig);
            break;
    }
}


static void veejay_handle_callbacks(veejay_t *info) {
	video_playback_setup *settings = info->settings;

	vj_osc_get_packet(info->osc);

	vj_event_update_remote( (void*)info );

	if( settings->randplayer.next_id != 0 ) {
		veejay_change_playback_mode( info, settings->randplayer.next_mode, settings->randplayer.next_id);
		settings->randplayer.next_id = 0;
	}

	veejay_pipe_write_status(info);

	int i;
	for( i = 0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		if( !vj_server_link_can_write( info->vjs[VEEJAY_PORT_STA],  i ) ) 
			continue;	
		int res = vj_server_send( info->vjs[VEEJAY_PORT_STA], i, (uint8_t*)info->status_line, info->status_line_len);
		if( res < 0 ) {
			_vj_server_del_client( info->vjs[VEEJAY_PORT_CMD], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_STA], i );
			_vj_server_del_client( info->vjs[VEEJAY_PORT_DAT], i );
		}
	}
}

void vj_lock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_lock(&(settings->control_mutex));
}
void vj_unlock(veejay_t *info)
{
	video_playback_setup *settings = info->settings;
	pthread_mutex_unlock(&(settings->control_mutex));
}	 

#ifdef HAVE_SDL
static int veejay_sdl_event_wanted(veejay_t *info, const SDL_Event *event)
{
    if(!info || !event)
        return 0;

    switch(event->type) {
        case SDL_QUIT:
            return 1;

        case SDL_KEYDOWN:
            if(!info->use_keyb)
                return 0;
            if(event->key.repeat)
                return 0;
            return 1;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_MOUSEMOTION:
            return info->use_mouse ? 1 : 0;

        case SDL_WINDOWEVENT:
            return event->window.event == SDL_WINDOWEVENT_CLOSE;

        default:
            return 0;
    }
}

static void veejay_sdl_push_event(SDL_Event *event, int mod)
{
    if(event)
        vj_event_push(event, mod);
}
#endif

static void 	veejay_consume_events(veejay_t *info) {
#ifdef HAVE_SDL
	SDL_Event event;
	int ctrl_pressed = 0;
	int shift_pressed = 0;
	int alt_pressed = 0;
	int mouse_x=0,mouse_y=0,but=0;
	int mod = 0;

	veejay_handle_callbacks(info);

	while( vj_event_pop(&event, &mod)) {
		if( event.type == SDL_QUIT ) {
			veejay_change_state(info, LAVPLAY_STATE_STOP );
		}
		if( event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)
		{
			vj_event_single_fire( (void*) info, event, 0);
		}
		if( event.type == SDL_MOUSEMOTION )
		{
			mouse_x = event.motion.x;
			mouse_y = event.motion.y;
		}

		if( info->use_mouse && event.type == SDL_MOUSEBUTTONDOWN )
		{
			mouse_x = event.button.x;
			mouse_y = event.button.y;
			shift_pressed = (mod & KMOD_LSHIFT );
			alt_pressed = (mod & KMOD_RSHIFT );
			if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
				ctrl_pressed = 1; 
			else
				ctrl_pressed = 0;

			SDL_MouseButtonEvent *mev = &(event.button);

			if( mev->button == SDL_BUTTON_LEFT && shift_pressed)
			{
				but = 6;
				info->uc->mouse[3] = 1;
			} else if( mev->button == SDL_BUTTON_LEFT && ctrl_pressed )
			{
				but = 10;
				info->uc->mouse[3] = 4;
			}
			if (mev->button == SDL_BUTTON_MIDDLE && shift_pressed )
			{
				but = 7;
				info->uc->mouse[3] = 2;
			}
			if( mev->button == SDL_BUTTON_LEFT && alt_pressed )
			{
				but = 11;
				info->uc->mouse[3] = 11;
			}
		}

		if( info->use_mouse && event.type == SDL_MOUSEBUTTONUP )
		{
			SDL_MouseButtonEvent *mev = &(event.button);
			alt_pressed = (mod & KMOD_RSHIFT );
			if( mod == 0x1080 || mod == 0x1040 || (mod & KMOD_LCTRL) || (mod & KMOD_RCTRL) )
				ctrl_pressed = 1; 
			else
				ctrl_pressed = 0;

			if( mev->button == SDL_BUTTON_LEFT )
			{
				if( info->uc->mouse[3] == 1 )
				{
					but = 6;
					info->uc->mouse[3] = 0;
				}
				else if (info->uc->mouse[3] == 4 )
				{	
					but = 10;
					info->uc->mouse[3] = 0;
				} else if (info->uc->mouse[3] == 0 )
				{
					but = 1;
				} else if ( info->uc->mouse[3] == 11 )
				{	
					but = 12;
					info->uc->mouse[3] = 0;
				}
			}
			else if (mev->button == SDL_BUTTON_RIGHT ) {
				but = 2;
			}
			else if (mev->button == SDL_BUTTON_MIDDLE ) {
				if( info->uc->mouse[3] == 2 )
				{	but = 0;
					info->uc->mouse[3] = 0;
				}
				else {if( info->uc->mouse[3] == 0 )
					but = 3;}
			}
			mouse_x = event.button.x;
			mouse_y = event.button.y;
			}
			if( info->use_mouse && event.type == SDL_MOUSEWHEEL ) {
			if ((event.wheel.y > 0 ) && !alt_pressed && !ctrl_pressed)
			{
				but = 4;
			}
			else if ((event.wheel.y < 0) && !alt_pressed && !ctrl_pressed)
			{
				but = 5;
			}
			else if ((event.wheel.y  > 0) && alt_pressed && !ctrl_pressed )
			{
				but = 13;
			}
			else if ((event.wheel.y < 0) && alt_pressed && !ctrl_pressed )
			{
				but = 14;
			}
			else if (event.wheel.y > 0)  
			{	
				but = 15;
			}	
			else if (event.wheel.y < 0)
			{
				but = 16;
			}
			SDL_GetMouseState(&mouse_x, &mouse_y);
		}
		
	}
	info->uc->mouse[0] = mouse_x;
	info->uc->mouse[1] = mouse_y;
	info->uc->mouse[2] = but;
#endif
}


void	veejay_event_handle(veejay_t *info)
{

#ifdef HAVE_SDL
	if( info->video_out == 0 || info->video_out == 2)
	{
		SDL_Event event;
		SDL_Event pending_motion;
		int have_pending_motion = 0;
		int pending_motion_mod = 0;

		while(SDL_PollEvent(&event) == 1) 
		{
			int mod = SDL_GetModState();

			if(!veejay_sdl_event_wanted(info, &event))
				continue;

			if(event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
				SDL_Event quit_event;
				veejay_memset(&quit_event, 0, sizeof(SDL_Event));
				quit_event.type = SDL_QUIT;
				if(have_pending_motion) {
					veejay_sdl_push_event(&pending_motion, pending_motion_mod);
					have_pending_motion = 0;
				}
				veejay_sdl_push_event(&quit_event, mod);
				continue;
			}

			if(event.type == SDL_MOUSEMOTION) {
				pending_motion = event;
				pending_motion_mod = mod;
				have_pending_motion = 1;
				continue;
			}

			if(have_pending_motion) {
				veejay_sdl_push_event(&pending_motion, pending_motion_mod);
				have_pending_motion = 0;
			}

			veejay_sdl_push_event(&event, mod);
		}

		if(have_pending_motion)
			veejay_sdl_push_event(&pending_motion, pending_motion_mod);
	}
#endif
}

#ifdef HAVE_SDL
static int veejay_bind_sdl_display_buffers(veejay_t *info)
{
    video_playback_setup *settings;
    display_frame_t *df;

    if (!info || !info->settings || !info->sdl)
        return 0;

    settings = info->settings;
    df = &settings->display_frame;

    df->current_write = 0;
    atomic_store_long_long(&df->seq, -1);

    for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
        df->pixels[i] = vj_sdl_get_buffer(info->sdl, i);
        if (!df->pixels[i]) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[DISPLAY] SDL display buffer %d is NULL", i);
            return 0;
        }
    }

    return 1;
}
#endif

int veejay_setup_video_out(veejay_t *info) {
	char *title;
	int x = 0, y = 0;
	editlist *el = info->edit_list;
	video_playback_setup *settings = info->settings;

	if( info->uc->geox != 0 && info->uc->geoy != 0 )
	{
		x = info->uc->geox;
		y = info->uc->geoy;
	}

	switch(info->video_out) {
		case 0:
			veejay_msg(VEEJAY_MSG_INFO, "Using output driver SDL");
#ifdef HAVE_SDL
			info->sdl = vj_sdl_allocate( info->effect_frame1, info->use_keyb, info->use_mouse,info->show_cursor, info->borderless);
			if( !info->sdl )
				return -1;

			title = veejay_title( info );

			if (!vj_sdl_init(info->sdl, x,y,info->video_output_width, info->video_output_height, info->bes_width, info->bes_height, title,1,info->settings->full_screen,info->pixel_format,el->video_fps, &settings->vsync_interval_s))
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Error initializing SDL");
				free(title);
				return -1;
			}

			if (!veejay_bind_sdl_display_buffers(info)) {
				veejay_msg(VEEJAY_MSG_ERROR, "Error binding SDL display buffers");
				free(title);
				return -1;
			}

			free(title);
#endif
		break;

		case 1:
		break;


		case 2:
		break;
		case 3:
	    case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode. Use -O [012345678]");
			return -1;
	}

	return 0;
}

static double vj_get_relative_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
    return t;
}

int veejay_mjpeg_software_frame_sync_interruptible(veejay_t *info, long wait_us, pthread_cond_t *sync_cv)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
    if (wait_us <= 0) return 0;

    double start_time = vj_get_relative_time();

    struct timespec abstime;
    clock_gettime(CLOCK_MONOTONIC, &abstime);

    long total_ns = abstime.tv_nsec + wait_us * 1000L;
    abstime.tv_sec += total_ns / 1000000000L;
    abstime.tv_nsec = total_ns % 1000000000L;

    pthread_mutex_lock(&settings->mutex);

    struct timespec now;
    const long max_sleep_us = 2000; // 2 ms maximum sleep interval

    while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        long rem_us = (abstime.tv_sec - now.tv_sec) * 1000000L +
                      (abstime.tv_nsec - now.tv_nsec) / 1000L;

        if (rem_us <= 0) break; // done waiting

        long sleep_us = rem_us > max_sleep_us ? max_sleep_us : rem_us;

        struct timespec short_abstime;
        clock_gettime(CLOCK_MONOTONIC, &short_abstime);
        long total_ns2 = short_abstime.tv_nsec + sleep_us * 1000L;
        short_abstime.tv_sec += total_ns2 / 1000000000L;
        short_abstime.tv_nsec = total_ns2 % 1000000000L;

        pthread_cond_timedwait(sync_cv, &settings->mutex, &short_abstime);
    }

    pthread_mutex_unlock(&settings->mutex);

    double end_time = vj_get_relative_time();
    veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Display thread slept %.6f s (requested %.6f s)",end_time - start_time, wait_us / 1.0e6);

    return 0;
}

char	*veejay_title(veejay_t *info)
{
	char tmp[128];
	snprintf(tmp,sizeof(tmp), "Veejay %s on port %d in %dx%d@%2.2f", VERSION,
	      info->uc->port, info->video_output_width,info->video_output_height,info->settings->output_fps );
	return strdup(tmp);
}

void *veejay_display_renderer_thread(void *arg)
{
    veejay_t *info = (veejay_t *) arg;
    video_playback_setup *settings = info->settings;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);


    pthread_mutex_lock(&settings->start_mutex);
    settings->video_out_ready = 1;
    pthread_cond_broadcast(&settings->start_cond);
    pthread_mutex_unlock(&settings->start_mutex);

	veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Waiting for first audio frame to be ready");
    while (atomic_load_int(&settings->first_audio_frame_ready) == 0 &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        usleep_accurate(200, settings);
    }

    double audio_start_offset = atomic_load_double(&settings->audio_start_offset);
    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Audio anchor established at %.6f s", audio_start_offset);

	while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {

		VJFrame *frame = veejay_video_queue_get_frame(info);
		if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) {
            if (frame) video_queue_return_frame(info, frame);
            break;
        }

        if (!frame) continue;

		if (frame->frame_num < 0) {
			video_queue_return_frame(info, frame);
			continue;
		}

		double audio_master = atomic_load_double(&settings->audio_master_s);
        double spvf = vj_runtime_effective_spvf(settings);
        double target_time_s = vj_runtime_target_time_s(settings, frame->frame_num);
		double delay_s = target_time_s - audio_master;

		if (delay_s < -spvf) {
			veejay_screen_update(info, frame);
			video_queue_return_frame(info,frame);
			continue;
		}

		if (delay_s > (spvf * 1.5f)) {
			usleep_accurate((long long)(spvf * 1.0e6), settings);
			veejay_screen_update(info, frame);		
			video_queue_return_frame(info, frame);
			continue;
		}
		else{
			if (delay_s > 0) {
				usleep_accurate((long long)(delay_s * 1.0e6), settings);
			}
			veejay_screen_update(info, frame);
			video_queue_return_frame(info, frame);
		}
	}

    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Renderer thread exiting...");
	pthread_exit(NULL);
    return NULL;
}


static void Welcome(veejay_t *info)
{
	veejay_msg(VEEJAY_MSG_INFO, "Video project settings: %ldx%ld, Norm: [%s], fps [%2.2f], %s",
			info->video_output_width,
			info->video_output_height,
			info->current_edit_list->video_norm == 'n' ? "NTSC" : "PAL",
			info->current_edit_list->video_fps, 
			info->current_edit_list->video_inter==0 ? "Not interlaced" : "Interlaced" );
	if(info->audio==AUDIO_PLAY && info->edit_list->has_audio)
	veejay_msg(VEEJAY_MSG_INFO, "                        %ldHz %d Channels %dBps (%d Bit)",
			info->current_edit_list->audio_rate,
			info->current_edit_list->audio_chans,
			info->current_edit_list->audio_bps,
			info->current_edit_list->audio_bits);

	veejay_msg(VEEJAY_MSG_INFO, "Bezeker: %s", (info->bezerk == 1 ? "Sample restart when switching mixing channels" : 
		"No sample restart when switching mixing channels" ));
  	veejay_msg(VEEJAY_MSG_INFO, "Log level: %s", (info->verbose == 0 ? "Normal" : "Verbose" ));

	if(info->settings->composite )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Software composite - projection screen is %d x %d",
			info->video_output_width, info->video_output_height );
	}

	veejay_msg(VEEJAY_MSG_INFO,"Type 'man veejay' in a shell to learn more about veejay");
	veejay_msg(VEEJAY_MSG_INFO,"For a list of events, type 'veejay -u |less' in a shell");
	veejay_msg(VEEJAY_MSG_INFO,"Use 'reloaded' to enter interactive mode");
	veejay_msg(VEEJAY_MSG_INFO,"Alternatives are OSC applications or 'sendVIMS' extension for PD"); 

	int k = verify_working_dir();
	if( k > 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING,
			"Found %d veejay project files in current working directory (.edl,.sl, .cfg,.avi)",k);
		veejay_msg(VEEJAY_MSG_WARNING,
			"If you want to start a new project, start veejay in an empty directory");
	}
	if( info->is_master ) {
		veejay_msg(VEEJAY_MSG_INFO, "This instance is the master video out");
	}
	else if(info->master_origin != NULL){
		veejay_msg(VEEJAY_MSG_INFO, "Syncing changes with %s:%d", info->master_origin, info->master_origin_port );
	}
}



#ifdef HAVE_JACK
static void veejay_audio_beat_ensure_default_action(vj_audio_beat_shared_t *s, const char *reason)
{
    if(!s)
        return;

    if(!atomic_load_int(&s->initialized))
        return;

    if(atomic_load_int(&s->action_mode) == VJ_AUDIO_BEAT_ACTION_NONE)
    {
        vj_audio_beat_set_action(s, VJ_AUDIO_BEAT_ACTION_AUTO_FX);
        veejay_msg(VEEJAY_MSG_INFO,
                   "[AUDIO-BEAT] default action corrected to auto-fx(2) at %s",
                   reason ? reason : "unknown");
    }
}

#endif

int veejay_open(veejay_t * info)
{
    video_playback_setup *settings = (video_playback_setup *) info->settings;
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    int i;
    
    pthread_mutex_init(&(settings->control_mutex), NULL);
    pthread_mutex_init(&(settings->start_mutex), NULL);

    pthread_mutex_init(&settings->mutex, NULL);
    pthread_cond_init(&settings->producer_wait_cv, &attr);
    pthread_cond_init(&settings->renderer_wait_cv, &attr);
    pthread_cond_init(&settings->data_ready_cv, &attr);
    pthread_cond_init(&settings->start_cond, &attr);
    pthread_condattr_destroy(&attr);
    
    settings->producer_index = 0;
    settings->renderer_index = 0;
    settings->frames_available = 0;
	settings->warmup_frames = 2;
    atomic_store_int(&settings->record_audio_source, VJ_RECORD_AUDIO_SOURCE_AUTO);

#ifdef HAVE_JACK
    vj_audio_sync_init(&settings->audio_sync, 2);

    /*
     * Beat analysis is independent from audio playback.
     * Starting veejay with playback disabled must not make the beat detector
     * unavailable; the detector thread can still open JACK capture later.
     */
    vj_audio_beat_init(&settings->audio_beat, 2);
    vj_audio_beat_bind_sync(&settings->audio_beat, &settings->audio_sync);
    veejay_audio_beat_ensure_default_action(&settings->audio_beat, "open");
#endif

    for(i = 0; i < VIDEO_QUEUE_LEN ; i ++ )
    {
        settings->buffers[i] = veejay_allocate_frame_buffer(info); 
        if (!settings->buffers[i]) {
             veejay_msg(VEEJAY_MSG_ERROR, "Failed to allocate queue buffer %d. Aborting.", i);
             return 0;
        }
    }

	return 1;
}

static int veejay_get_norm( char n )
{
	if( n == 'p' )
		return VIDEO_MODE_PAL;
	if( n == 's' )
		return VIDEO_MODE_SECAM;
	if( n == 'n' )
		return VIDEO_MODE_NTSC;

	return VIDEO_MODE_PAL;
}

static int veejay_mjpeg_set_playback_rate(veejay_t *info, float video_fps, int norm)
{
	video_playback_setup *settings = (video_playback_setup *) info->settings;

    video_fps = vj_runtime_clamp_fps(video_fps);
    settings->spvf = vj_runtime_spvf_from_fps(video_fps);
    settings->output_fps = video_fps;
	settings->usec_per_frame = vj_el_get_usec_per_frame(video_fps);
    double media_fps = (info->edit_list && info->edit_list->video_fps > 0.0) ? info->edit_list->video_fps : video_fps;
    atomic_store_double(&settings->runtime_playback_rate, (double)video_fps / media_fps);
    atomic_store_double(&settings->fps_epoch_s, 0.0);
    atomic_store_long_long(&settings->fps_epoch_frame, 0);
    atomic_store_int(&settings->fps_generation, 0);

	veejay_msg(
		VEEJAY_MSG_INFO,
		"Playback rate is %f FPS, %d usec_per_frame, SPVF=%g",
		video_fps,
		settings->usec_per_frame,
		settings->spvf
	);

	return 1;
}


int veejay_close(veejay_t *info)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;
    int success = 1;

    veejay_wake_playback_waiters(info);

    safe_join(&settings->renderer_thread, "Renderer");
    safe_join(&settings->producer_thread, "Producer");
    safe_join(&settings->audio_playback_thread, "Audio playback");
#ifdef HAVE_JACK
    safe_join(&settings->audio_beat_thread, "Audio beat");
    safe_join(&settings->audio_sync_thread, "Audio sync");
    vj_perform_audio_stop(info);
    vj_audio_sync_free(&settings->audio_sync);
#endif

    veejay_change_state_save(info, LAVPLAY_STATE_STOP);

    pthread_mutex_destroy(&settings->mutex);
    pthread_mutex_destroy(&settings->control_mutex);
    pthread_mutex_destroy(&settings->start_mutex);
    pthread_cond_destroy(&settings->producer_wait_cv);
    pthread_cond_destroy(&settings->renderer_wait_cv);
    pthread_cond_destroy(&settings->data_ready_cv);
    pthread_cond_destroy(&settings->start_cond);

    for (int i = 0; i < VIDEO_QUEUE_LEN; i++)
        if (settings->buffers[i]) veejay_free_frame_buffer(settings->buffers[i]);

    return success;
}

int veejay_init(veejay_t * info, int x, int y,char *arg, int def_tags, int gen_tags )
{
	editlist *el = NULL;
	video_playback_setup *settings = info->settings;

	int id=0;

	if(info->video_out<0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No video output driver selected (see man veejay)");
		return -1;
	}
	// override geometry set in config file
	if( info->uc->geox != 0 && info->uc->geoy != 0 )
	{
		x = info->uc->geox;
		y = info->uc->geoy;
	}

	vj_event_init((void*)info);

	if (veejay_init_editlist(info) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
		           "Cannot initialize the EditList");
		return -1;
	}

	el = info->edit_list;

	if(!vj_mem_threaded_init( info->video_output_width, info->video_output_height ) )
		return 0;

	vj_tag_set_veejay_t(info);

	int driver = 1;
	if (vj_tag_init(info->video_output_width, info->video_output_height, info->pixel_format,driver) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing Stream Manager");
		return -1;
	}
#ifdef HAVE_FREETYPE
	info->font = vj_font_init( info->video_output_width,info->video_output_height,el->video_fps,0 );

	if(!info->font) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system");
		return -1;
	}
#endif

	if(info->settings->splitscreen) {
		char split_cfg[1024];
		snprintf(split_cfg,sizeof(split_cfg),"%s/splitscreen.cfg", info->homedir );

		vj_split_set_master( info->uc->port );
		
		info->splitter = vj_split_new_from_file( split_cfg, info->video_output_width,info->video_output_height, PIX_FMT_YUV444P );
	}

	if(info->settings->composite)
	{
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir  );
	}
	else
	{
		info->osd = vj_font_single_init( info->video_output_width,info->video_output_height,el->video_fps,info->homedir );
	}

	if(!info->osd) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error while initializing font system for OSD");
		return -1;
	}

	if(!sample_init( (info->video_output_width * info->video_output_height), info->font, info->plain_editlist,info ) ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Internal error while initializing sample administrator");
		return -1;
	}

	sample_set_project(info->pixel_format,
	                   info->auto_deinterlace,
	                   info->preserve_pathnames,
	                   0,
	                   el->video_norm,
	                   info->video_output_width,
	                   info->video_output_height);

	int full_range = veejay_set_yuv_range( info );
	yuv_set_pixel_range(full_range);
    vje_set_rgb_parameter_conversion_type(full_range);

	info->settings->sample_mode = SSM_422_444;

	if(( info->effect_frame1->width % 4) != 0 || (info->effect_frame1->height % 4) != 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "You should specify an output resolution that is a multiple of 4");
	}

	if(!vj_perform_init(info))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize Veejay Performer");
		return -1;
	}

	info->shm = vj_shm_new_master( info->homedir,info->effect_frame1 );
	if( !info->shm ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to initialize shared resource");
	}

	if( info->settings->composite )
	{
		char path[1024];
		snprintf(path,sizeof(path),"%s/viewport.cfg", info->homedir);
		int comp_mode = info->settings->composite;

		info->composite = composite_init( info->video_output_width, info->video_output_height,
		                                 info->video_output_width, info->video_output_height,
		                                 path,
		                                 info->settings->sample_mode,
		                                 yuv_which_scaler(),
		                                 info->pixel_format,
		                                 &comp_mode);

		if(!info->composite) {
			return -1;
		}
		composite_set_file_mode( info->composite, info->homedir, info->uc->playback_mode,info->uc->sample_id);

		info->settings->zoom = 0;
		info->settings->composite = ( comp_mode == 1 ? 1: 0);
	}

	if(!info->bes_width)
		info->bes_width = info->video_output_width;
	if(!info->bes_height)
		info->bes_height = info->video_output_height;


	vje_init( info->video_output_width,info->video_output_height);
#ifdef HAVE_JACK
    /*
     * Build the audio-beat auto-FX metadata table for every JACK build,
     * even when audio playback is disabled.
     *
     * info->audio == NO_AUDIO only means playback is off.  The beat detector
     * can still run as JACK capture-only and needs this table to translate
     * active FX parameters into beat modulation targets.
     */
    if(!vj_audio_beat_auto_build_table()) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] auto-fx metadata table could not be built after vje_init(); auto-FX mapping will have no targets");
    }
#endif

	if(info->dump)
		vje_dump();
	
    if( info->settings->action_scheduler.sl && info->settings->action_scheduler.state )
	{
		int initial_sample = info->uc->sample_id;
		int initial_mode = info->uc->playback_mode;
		if(sample_open_and_watch( info->settings->action_scheduler.sl,
		                       info->composite,
		                       info->seq, info->font, el, &initial_sample, &initial_mode) )
			veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s from actionfile (mode %d, sample %d)",
			           info->settings->action_scheduler.sl, initial_mode, initial_sample );
		info->uc->playback_mode = initial_mode;
		info->uc->sample_id = initial_sample;
	}

	if( settings->action_scheduler.state )
	{
		settings->action_scheduler.state = 0;
	}

	int instances = 0;

	while( (instances < 4 ) && !vj_server_setup(info))
	{
		int port = info->uc->port;
		int new_port = info->uc->port + 1000;
		instances ++;
		veejay_msg((instances < 4 ? VEEJAY_MSG_WARNING: VEEJAY_MSG_ERROR),
			"Port %d -~ %d in use, trying to start on port %d (%d/%d attempts)", port,port+6, new_port , instances, 4 - instances);
		info->uc->port = new_port;
	}

	if( instances >= 4 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to start network server. Most likely, there is already a veejay running");
		veejay_msg(VEEJAY_MSG_ERROR,"If you want to run multiple veejay's on the same machine, use the '-p/--port' option");
		veejay_msg(VEEJAY_MSG_ERROR,"For example: $ veejay -p 4490 -d");
		return -1;
	}

	/* now setup the output driver */
	switch (info->video_out)
	{
		case 0:
		case 1:
		case 2:
			break;
		case 3:
			veejay_msg(VEEJAY_MSG_INFO, "Entering headless mode (no visual output)");
		break;

		case 4:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (420JPEG)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_420JPEG ) == -1 )
			{
				return -1;
			}
		break;

		case 5:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, -1 );
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;

		case 6:
			veejay_msg(VEEJAY_MSG_INFO, "Entering Y4M streaming mode (422)");
			info->y4m = vj_yuv4mpeg_alloc( info->video_output_width,info->video_output_height,info->effect_frame1->fps, info->pixel_format );
			if( vj_yuv_stream_start_write( info->y4m, info->effect_frame1, info->y4m_file, Y4M_CHROMA_422 ) == -1 ) {
				return -1;
			}
		break;
		case 7:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, PIX_FMT_YUV420P );
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;
		case 8:
			veejay_msg(VEEJAY_MSG_INFO, "Entering vloopback streaming mode. ");
			info->vloopback = vj_vloopback_open( info->y4m_file, info->effect_frame1, info->video_output_width, info->video_output_height, PIX_FMT_BGR24);
			if( info->vloopback == NULL )
			{
				veejay_msg(0, "Cannot open %s as vloopback", info->y4m_file);
				return -1;
			}
		break;		
		default:
			break;
	}

	if( gen_tags > 0 ) {
		int total  = 0;
		int *world = plug_find_all_generator_plugins( &total );
		if( total == 0 ) {
			veejay_msg(0,"No generator plugins found");
			return -1;
		}
		int i;
		int plugrdy = 0;
		for ( i = 0; i < total; i ++ ) {
			int plugid = world[i];
			if( vj_tag_new( VJ_TAG_TYPE_GENERATOR, NULL,-1, el, info->pixel_format,
			               plugid, 0, 0 ) > 0 )
				plugrdy++;
		}

		free(world);

		if( plugrdy > 0 ) {
			veejay_msg(VEEJAY_MSG_INFO, "Initialized %d generators", plugrdy);
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = ( gen_tags <= plugrdy ? gen_tags : 1 );
		} else {
			return -1;
		}
	}

	if(def_tags >= 0 && id <= 0)
	{
		char vidfile[1024];
		int default_chan = 1;
		char *chanid = getenv("VEEJAY_DEFAULT_CHANNEL");
		if(chanid != NULL )
			default_chan = atoi(chanid);
		else
			veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_DEFAULT_CHANNEL=channel not set (defaulting to 1)");

		snprintf(vidfile,sizeof(vidfile),"/dev/video%d", def_tags);
		int nid =	veejay_create_tag( info, VJ_TAG_TYPE_V4L, vidfile, info->nstreams, default_chan, def_tags );
		if( nid> 0)
		{
			veejay_msg(VEEJAY_MSG_INFO, "Requested capture device available as stream %d", nid );
		}
		else
		{
			return -1;
		}
		if(!info->load_sample_file && !info->load_action_file) {
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = nid;
		}
	
	}
	else if( info->uc->file_as_sample && id <= 0)
	{
		long i,n=el->num_video_files;
		for(i = 0; i < n; i ++ )
		{
			long start=0,end=2;
			if(vj_el_get_file_entry( info->edit_list, &start,&end, i ))
			{
				editlist *sample_el = veejay_edit_copy_to_new(	info,info->edit_list,start,end );
				if(!el)
				{
					veejay_msg(0, "Unable to start from file, Abort");
					return -1;
				}
			
				sample_info *skel = sample_skeleton_new( 0, end - start );
				if(skel)
				{
					skel->edit_list = sample_el;
          char* sample_name;
          if(!vj_get_sample_display_name(&sample_name, el->video_file_list[i])){
            veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample filename for '%s'", el->video_file_list[i]);
          } else {
            snprintf(skel->descr, SAMPLE_MAX_DESCR_LEN, "%s", sample_name);
            free (sample_name);
          }
					sample_store(skel,0);
				}
			}
		}
		if(!info->load_sample_file && !info->load_action_file) {
			info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
			info->uc->sample_id = 1;
		}
	}
	else if(info->dummy->active && id <= 0 && !info->load_sample_file && !info->load_action_file)
	{
		int dummy_id;
		/* Use dummy mode, action file could have specified something */
		if( vj_tag_size() <= 0 )
			dummy_id = vj_tag_new( VJ_TAG_TYPE_COLOR, (char*) "Solid", -1, el,info->pixel_format,-1,0,0);
		else
			dummy_id = vj_tag_highest_valid_id();
		
		if( info->uc->sample_id <= 0 )
		{
			info->uc->playback_mode = VJ_PLAYBACK_MODE_TAG;
			info->uc->sample_id = dummy_id;
		}
	}

	if (!veejay_mjpeg_set_playback_rate(info, info->dummy->fps, veejay_get_norm(info->dummy->norm) ))
	{
		return -1;
	}


    info->settings->transition.ptr = shapewipe_malloc( info->video_output_width, info->video_output_height);
    if(!info->settings->transition.ptr) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to initialize shapewipe");
        return -1;
    }

	if(veejay_open(info) != 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize the threading system");
		return -1;
	}

	return 0;
}

static double vj_measure_pause_cost_once(void)
{
    const int iterations = 10000000;
    struct timespec start, end;

    for (int i = 0; i < 10000; ++i) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; ++i) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long sec  = end.tv_sec  - start.tv_sec;
    long nsec = end.tv_nsec - start.tv_nsec;
    long long total_ns = (long long)sec * 1000000000LL + nsec;

    return (double)total_ns / (double)iterations;
}

double vj_calibrate_pause_cost_ns(void)
{
    double best = 1e9;

    for (int i = 0; i < 3; ++i) {
        double v = vj_measure_pause_cost_once();
        if (v < best)
            best = v;
    }

    return best;
}
static long long vj_calibrate_nanosleep_overshoot(int iterations)
{
    struct timespec t1, t2;
    const long long request_ns = 100000;

    long long max_overshoot = 0;

    for (int i = 0; i < iterations; i++) {

        struct timespec req = {
            .tv_sec = 0,
            .tv_nsec = request_ns
        };

        clock_gettime(CLOCK_MONOTONIC, &t1);
        nanosleep(&req, NULL);
        clock_gettime(CLOCK_MONOTONIC, &t2);

        long long elapsed =
            (t2.tv_sec - t1.tv_sec) * 1000000000LL +
            (t2.tv_nsec - t1.tv_nsec);

        long long overshoot = elapsed - request_ns;

        if (overshoot > max_overshoot)
            max_overshoot = overshoot;
    }

    return max_overshoot;
}

static int vj_detect_preempt_rt(void) {
    FILE *fp = fopen("/sys/kernel/realtime", "r");
    if (fp) {
        int is_rt = 0;
        if (fscanf(fp, "%d", &is_rt) == 1) {
            fclose(fp);
            return is_rt;
        }
        fclose(fp);
    }
    return 0;
}

static void vj_set_realtime_priority(video_playback_setup *settings, int user_max_priority) {
    struct sched_param param;
    int policy;
    int ret;

    ret = pthread_getschedparam(pthread_self(), &policy, &param);
    if (ret != 0) {
        perror("pthread_getschedparam failed");
        return;
    }

    int max_priority = sched_get_priority_max(SCHED_FIFO);
    if (max_priority == -1) {
        perror("sched_get_priority_max failed");
        return;
    }
    
    if(max_priority > user_max_priority)
	max_priority = user_max_priority;

    param.sched_priority = max_priority;
    
    ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	if(settings->is_rt_kernel) {
		veejay_msg(VEEJAY_MSG_INFO, " PERFORMANCE: PREEMPT_RT kernel detected!");
		veejay_msg(VEEJAY_MSG_INFO, " SCHEDULER:   Hard real-time constraints enabled.");
		veejay_msg(VEEJAY_MSG_INFO, " AUDIO:       Jitter minimized for stable playback.");
	}
	else {
		veejay_msg(VEEJAY_MSG_INFO, "[PERF] Standard kernel detected. Using safety sleep thresholds.");
	}
    
    if (ret != 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "pthread_setschedparam failed. Not running as RT thread.");
    } else {
        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] TID %lu: Priority successfully set to SCHED_FIFO with level %d", 
               (unsigned long)gettid(), max_priority);
    }
    

    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] TID %lu: Running as high-priority task, clock overshoot set to %lld µs", (unsigned long)gettid(), settings->clock_overshoot);
}

static void vj_audio_setup_rt_thread(video_playback_setup *settings) {

	long long overshoot = vj_calibrate_nanosleep_overshoot(10000);
	
	settings->clock_overshoot = (overshoot + overshoot / 4);
	settings->pause_cost_ns = vj_calibrate_pause_cost_ns();

	if (settings->clock_overshoot < 30000) 
		settings->clock_overshoot = 30000;
	if (settings->clock_overshoot > 300000) 
		settings->clock_overshoot = 300000;
	

	settings->is_rt_kernel = vj_detect_preempt_rt();

    vj_set_realtime_priority(settings,49);
	
	if (seteuid(getuid()) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to drop privileges to effective user: %s", strerror(errno));
		return;
	}

}

static void vj_suggest_fit_preview_size(int w, int h, int *out_w, int *out_h)
{
    const int max_w = 500;
    const int max_h = 300;

    double scale_w = (double)max_w / (double)w;
    double scale_h = (double)max_h / (double)h;

    double s = (scale_w < scale_h) ? scale_w : scale_h;

    if (s > 1.0) s = 1.0;

    *out_w = (int)(w * s);
    *out_h = (int)(h * s);
}

static void veejay_producer_initialize_playmode(veejay_t *info) {
	video_playback_setup *settings = info->settings;
 
	switch(info->uc->playback_mode)
    {
        case VJ_PLAYBACK_MODE_PLAIN:
            info->current_edit_list = info->edit_list;

			atomic_store_long_long(&settings->min_frame_num, 0);
			atomic_store_long_long(&settings->max_frame_num, info->edit_list->total_frames);
			veejay_msg(VEEJAY_MSG_INFO, "Playing plain video, frames %lld - %lld", settings->min_frame_num, settings->max_frame_num );
            settings->current_playback_speed = 1;
        break;

        case VJ_PLAYBACK_MODE_TAG:
            veejay_start_playing_stream(info,info->uc->sample_id);    
            veejay_msg(VEEJAY_MSG_INFO, "Playing stream %d", info->uc->sample_id);
        break;

        case VJ_PLAYBACK_MODE_PATTERN:
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;

        case VJ_PLAYBACK_MODE_SAMPLE:
            veejay_start_playing_sample(info, info->uc->sample_id);
            veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d", info->uc->sample_id);
        break;
    }
}  

#ifdef HAVE_JACK
static inline void vj_audio_wait_for_jack_space(
    int jack_frames_needed,
    video_playback_setup *settings
) {

    long min_busy_us = settings->clock_overshoot;

    while (vj_jack_get_ringbuffer_frames_free() < jack_frames_needed) {

        usleep_accurate(1500, settings);

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) return;
    }

    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (vj_jack_get_ringbuffer_frames_free() < jack_frames_needed) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield");
#endif
        clock_gettime(CLOCK_MONOTONIC, &current);

        long elapsed_us = (current.tv_sec - start.tv_sec) * 1000000L +
                          (current.tv_nsec - start.tv_nsec) / 1000L;

        if (elapsed_us >= min_busy_us) break;

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) break;
    }
}


static inline long vj_audio_required_jack_frames_for_client_chunk(int client_frames,
                                                                  long client_rate,
                                                                  long jack_rate)
{
    long required_jack;

    if(client_frames <= 0)
        return 0;

    required_jack = vj_jack_get_required_free_frames(client_frames);
    if(required_jack <= 0)
        required_jack = vj_jack_client_to_jack_frames(client_frames);
    if(required_jack <= 0 && client_rate > 0 && jack_rate > 0) {
        required_jack = (long)((((long long)client_frames * (long long)jack_rate) +
                                (long long)client_rate - 1LL) /
                               (long long)client_rate);
    }
    if(required_jack <= 0)
        required_jack = client_frames;

    return required_jack;
}

static inline int vj_audio_client_frames_that_fit_jack_free(int want_client_frames,
                                                            int free_jack_frames,
                                                            long client_rate,
                                                            long jack_rate)
{
    int lo;
    int hi;
    int best;

    if(want_client_frames <= 0 || free_jack_frames <= 0)
        return 0;

    lo = 1;
    hi = want_client_frames;
    best = 0;

    while(lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        long required = vj_audio_required_jack_frames_for_client_chunk(mid,
                                                                       client_rate,
                                                                       jack_rate);
        if(required <= free_jack_frames) {
            best = mid;
            lo = mid + 1;
        }
        else {
            hi = mid - 1;
        }
    }

    return best;
}

static inline int vj_audio_external_tape_min_partial_client_frames(long client_rate)
{
    int frames;

    if(client_rate <= 0)
        return 128;

    /* About 5ms at the client rate.  This is small enough to avoid waiting
     * for a full video-frame audio block, but large enough to avoid turning
     * the producer into a tiny busy-write loop.
     */
    frames = (int)(client_rate / 200L);
    if(frames < 64)
        frames = 64;
    else if(frames > 512)
        frames = 512;

    return frames;
}

static inline int vj_audio_external_tape_max_chunk_client_frames(long client_rate,
                                                                 long jack_rate,
                                                                 int *period_out,
                                                                 int *target_jack_out)
{
    int period = vj_jack_get_period_size();
    long target_jack;
    long frames;
    int min_partial;

    if(period <= 0)
        period = 1024;

    target_jack = period / 2;
    if(target_jack < 256)
        target_jack = period > 64 ? (period - 64) : period;
    if(target_jack < 64)
        target_jack = 64;
    if(target_jack > 1024)
        target_jack = 1024;

    if(client_rate > 0 && jack_rate > 0)
        frames = (target_jack * client_rate) / jack_rate;
    else
        frames = target_jack;

    min_partial = vj_audio_external_tape_min_partial_client_frames(client_rate);
    if(frames < min_partial)
        frames = min_partial;
    if(frames < 64)
        frames = 64;
    if(frames > 1024)
        frames = 1024;

    if(period_out)
        *period_out = period;
    if(target_jack_out)
        *target_jack_out = (int)target_jack;

    return (int)frames;
}

typedef struct
{
    const char *policy;
    double block_s;
    double played_s;
    double queued_s;
    double queue_depth_s;
    double low_water_s;
    double target_s;
    double high_water_s;
    double sleep_s;
} vj_audio_producer_pace_budget_t;

static inline double vj_audio_clampd_local(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline double vj_audio_producer_queue_pace_sleep_s(long client_rate,
                                                          long jack_rate,
                                                          int frames_written,
                                                          double queued_s,
                                                          video_playback_setup *settings,
                                                          vj_audio_producer_pace_budget_t *budget)
{
    double block_s;
    double low_water_s;
    double max_sleep_s;
    double played_now_s;
    double queue_depth_s;
    double sleep_s;

    (void)settings;

    if(budget)
        veejay_memset(budget, 0, sizeof(*budget));

    if(client_rate <= 0 || jack_rate <= 0 || frames_written <= 0 || queued_s <= 0.0)
        return 0.0005;

    block_s = (double)frames_written / (double)client_rate;
    if(block_s <= 0.0)
        return 0.0005;

    played_now_s = (double)vj_jack_get_played_frames() / (double)jack_rate;
    if(played_now_s <= 0.0)
        return 0.0005;

    queue_depth_s = queued_s - played_now_s;
    if(queue_depth_s <= 0.0)
        sleep_s = 0.0;
    else {
        low_water_s = block_s * VJ_AUDIO_PRODUCER_QUEUE_LOW_WATER_FRACTION;
        if(low_water_s < (VJ_AUDIO_PRODUCER_QUEUE_MIN_LOW_WATER_MS / 1000.0))
            low_water_s = VJ_AUDIO_PRODUCER_QUEUE_MIN_LOW_WATER_MS / 1000.0;
        if(low_water_s > (VJ_AUDIO_PRODUCER_QUEUE_MAX_LOW_WATER_MS / 1000.0))
            low_water_s = VJ_AUDIO_PRODUCER_QUEUE_MAX_LOW_WATER_MS / 1000.0;

        sleep_s = queue_depth_s - low_water_s;
        if(sleep_s <= 0.0)
            sleep_s = 0.0;
        else {
            max_sleep_s = block_s * VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_FRACTION;
            if(max_sleep_s > (VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_MS / 1000.0))
                max_sleep_s = VJ_AUDIO_PRODUCER_QUEUE_MAX_SLEEP_MS / 1000.0;
            if(max_sleep_s < 0.001)
                max_sleep_s = 0.001;

            if(sleep_s > max_sleep_s)
                sleep_s = max_sleep_s;
        }
    }

    if(budget) {
        budget->policy = "seekable-queue";
        budget->block_s = block_s;
        budget->played_s = played_now_s;
        budget->queued_s = queued_s;
        budget->queue_depth_s = queue_depth_s;
        budget->low_water_s = block_s * VJ_AUDIO_PRODUCER_QUEUE_LOW_WATER_FRACTION;
        budget->target_s = 0.0;
        budget->high_water_s = 0.0;
        budget->sleep_s = sleep_s;
    }

    return sleep_s;
}

static inline double vj_audio_producer_live_cadence_sleep_s(long client_rate,
                                                            long jack_rate,
                                                            int frames_written,
                                                            double queued_s,
                                                            video_playback_setup *settings,
                                                            vj_audio_producer_pace_budget_t *budget)
{
    double block_s;
    double played_now_s;
    double queue_depth_s;
    double low_water_s;
    double target_s;
    double high_water_s;
    double sleep_s;
    double min_sleep_s;
    double max_sleep_s;

    (void)settings;

    if(budget)
        veejay_memset(budget, 0, sizeof(*budget));

    if(client_rate <= 0 || jack_rate <= 0 || frames_written <= 0 || queued_s <= 0.0)
        return 0.0005;

    block_s = (double)frames_written / (double)client_rate;
    if(block_s <= 0.0)
        return 0.0005;

    played_now_s = (double)vj_jack_get_played_frames() / (double)jack_rate;
    if(played_now_s <= 0.0)
        return 0.0005;

    queue_depth_s = queued_s - played_now_s;

    low_water_s = block_s * VJ_AUDIO_PRODUCER_LIVE_LOW_WATER_FRACTION;
    target_s = block_s * VJ_AUDIO_PRODUCER_LIVE_TARGET_FRACTION;
    high_water_s = block_s * VJ_AUDIO_PRODUCER_LIVE_HIGH_WATER_FRACTION;

    min_sleep_s = VJ_AUDIO_PRODUCER_LIVE_MIN_SLEEP_MS / 1000.0;
    max_sleep_s = VJ_AUDIO_PRODUCER_LIVE_MAX_SLEEP_MS / 1000.0;

    if(queue_depth_s <= 0.0)
        sleep_s = block_s * VJ_AUDIO_PRODUCER_LIVE_UNDER_SLEEP_FRACTION;
    else if(queue_depth_s < low_water_s)
        sleep_s = block_s * VJ_AUDIO_PRODUCER_LIVE_UNDER_SLEEP_FRACTION;
    else if(queue_depth_s < target_s)
        sleep_s = block_s * 0.55;
    else if(queue_depth_s > high_water_s)
        sleep_s = block_s * VJ_AUDIO_PRODUCER_LIVE_HIGH_SLEEP_FRACTION;
    else
        sleep_s = block_s * VJ_AUDIO_PRODUCER_LIVE_STEADY_SLEEP_FRACTION;

    sleep_s = vj_audio_clampd_local(sleep_s, min_sleep_s, max_sleep_s);

    if(budget) {
        budget->policy = "external-live-cadence";
        budget->block_s = block_s;
        budget->played_s = played_now_s;
        budget->queued_s = queued_s;
        budget->queue_depth_s = queue_depth_s;
        budget->low_water_s = low_water_s;
        budget->target_s = target_s;
        budget->high_water_s = high_water_s;
        budget->sleep_s = sleep_s;
    }

    return sleep_s;
}

static inline double vj_audio_producer_pace_sleep_s(long client_rate,
                                                    long jack_rate,
                                                    int frames_written,
                                                    double queued_s,
                                                    int live_cadence,
                                                    video_playback_setup *settings,
                                                    vj_audio_producer_pace_budget_t *budget)
{
    if(live_cadence)
        return vj_audio_producer_live_cadence_sleep_s(client_rate,
                                                      jack_rate,
                                                      frames_written,
                                                      queued_s,
                                                      settings,
                                                      budget);

    return vj_audio_producer_queue_pace_sleep_s(client_rate,
                                                jack_rate,
                                                frames_written,
                                                queued_s,
                                                settings,
                                                budget);
}

#ifndef VJ_AUDIO_MONITOR_AUTO_FALLBACK_RATE
#define VJ_AUDIO_MONITOR_AUTO_FALLBACK_RATE 48000
#endif
#ifndef VJ_AUDIO_MONITOR_AUTO_FALLBACK_PERIOD
#define VJ_AUDIO_MONITOR_AUTO_FALLBACK_PERIOD 256
#endif
#ifndef VJ_AUDIO_MONITOR_AUTO_FALLBACK_FPS
#define VJ_AUDIO_MONITOR_AUTO_FALLBACK_FPS 25.0f
#endif
#ifndef VJ_AUDIO_MONITOR_AUTO_PERIODS
#define VJ_AUDIO_MONITOR_AUTO_PERIODS 2.0
#endif
#ifndef VJ_AUDIO_MONITOR_LATENCY_MAX_MS
#define VJ_AUDIO_MONITOR_LATENCY_MAX_MS 64
#endif

static inline float vj_audio_monitor_nominal_video_fps(veejay_t *info,
                                                       video_playback_setup *settings)
{
    editlist *el = NULL;

    if(info && info->dummy && info->dummy->fps > 0.0f)
        return vj_runtime_clamp_fps(info->dummy->fps);

    if(info)
        el = info->current_edit_list ? info->current_edit_list : info->edit_list;
    if(el && el->video_fps > 0.0)
        return vj_runtime_clamp_fps((float)el->video_fps);

    if(settings && settings->output_fps > 0.0f)
        return vj_runtime_clamp_fps(settings->output_fps);

    return VJ_AUDIO_MONITOR_AUTO_FALLBACK_FPS;
}

static inline int vj_audio_monitor_auto_latency_ms(veejay_t *info,
                                                   video_playback_setup *settings)
{
    int rate = vj_jack_get_rate();
    int period = vj_jack_get_period_size();
    float fps = vj_audio_monitor_nominal_video_fps(info, settings);
    int frame_ms;
    int ms;

    if(rate <= 0)
        rate = VJ_AUDIO_MONITOR_AUTO_FALLBACK_RATE;
    if(period <= 0)
        period = VJ_AUDIO_MONITOR_AUTO_FALLBACK_PERIOD;

    frame_ms = vj_clampi((int)((1000.0f / fps) + 0.5f),
                         1,
                         VJ_AUDIO_MONITOR_LATENCY_MAX_MS);

    ms = (int)(((double)period * VJ_AUDIO_MONITOR_AUTO_PERIODS * 1000.0 / (double)rate) + 0.5);
    return vj_clampi(ms, 0, frame_ms);
}

static inline int vj_audio_monitor_configured_latency_ms(video_playback_setup *settings)
{
    int ms = VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS;

    if(settings)
        ms = vj_audio_beat_get_monitor_latency_ms(&settings->audio_beat);

    return ms < 0 ? -1 : vj_clampi(ms, 0, VJ_AUDIO_MONITOR_LATENCY_MAX_MS);
}

static inline int vj_audio_monitor_passthrough_heard_latency_ms(veejay_t *info,
                                                                  video_playback_setup *settings)
{
    int ms = vj_audio_monitor_configured_latency_ms(settings);

    if(ms >= 0)
        return ms;

    return vj_audio_monitor_auto_latency_ms(info, settings);
}

static inline int vj_audio_uses_external_audio_playback_source(
    video_playback_setup *settings,
    int audio_source,
    int sync_mode,
    int sync_enabled
);

static inline int vj_audio_monitor_direct_heard_latency_ms(veejay_t *info,
                                                           video_playback_setup *settings,
                                                           int audio_source,
                                                           int sync_mode,
                                                           int sync_enabled)
{
    if(!vj_audio_uses_external_audio_playback_source(settings,
                                                     audio_source,
                                                     sync_mode,
                                                     sync_enabled))
        return -1;

    if(sync_mode != VJ_AUDIO_SYNC_MODE_MONITOR)
        return -1;

    if(atomic_load_int(&settings->audio_sync.source) != VJ_AUDIO_SYNC_SOURCE_JACK)
        return -1;

    {
        int configured_ms = vj_audio_monitor_configured_latency_ms(settings);
        if(configured_ms >= 0)
            return configured_ms;
    }

    return vj_jack_get_input_passthrough() ?
           vj_audio_monitor_passthrough_heard_latency_ms(info, settings) : 0;
}

static inline int vj_audio_uses_external_audio_playback_source(
    video_playback_setup *settings,
    int audio_source,
    int sync_mode,
    int sync_enabled
) {
    int source;

    if(!settings || !sync_enabled)
        return 0;

    if(sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        return 0;

    if(!vj_audio_sync_mode_uses_external_playback(sync_mode))
        return 0;

    source = atomic_load_int(&settings->audio_sync.source);
    if(source != VJ_AUDIO_SYNC_SOURCE_JACK &&
       source != VJ_AUDIO_SYNC_SOURCE_WAV_FILE)
        return 0;

    return audio_source != VJ_RECORD_AUDIO_SOURCE_SILENCE;
}

static int vj_tempo_bridge_reverse_should_reanchor(
    video_playback_setup *settings,
    double diff,
    double spvf,
    double skip_tolerance,
    long long now_ms,
    long long last_ms,
    double *threshold_out
) {
    double threshold_s;
    int sync_mode;

    if(threshold_out)
        *threshold_out = 0.0;

    if(!settings || spvf <= 0.0 || diff <= 0.0)
        return 0;

    if(settings->current_playback_speed >= 0)
        return 0;

    sync_mode = atomic_load_int(&settings->audio_sync.mode);
    if(sync_mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return 0;

    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return 0;

    if(!vj_audio_uses_external_audio_playback_source(
           settings,
           atomic_load_int(&settings->record_audio_source),
           sync_mode,
           1))
        return 0;

    threshold_s = spvf * VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_MIN_BLOCKS;

    if(threshold_s < skip_tolerance)
        threshold_s = skip_tolerance;

    if(threshold_s < (VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_JITTER_FLOOR_MS * 0.001))
        threshold_s = VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_JITTER_FLOOR_MS * 0.001;

    if(threshold_out)
        *threshold_out = threshold_s;

    if(diff <= threshold_s)
        return 0;

    if(last_ms > 0 &&
       (now_ms - last_ms) < VJ_TEMPO_BRIDGE_REVERSE_REANCHOR_COOLDOWN_MS)
        return 0;

    return 1;
}

static inline int vj_audio_external_jack_transport_is_neutral(
    int speed,
    double runtime_rate,
    int sfd
) {
    if(speed != 1)
        return 0;

    if(sfd > 1)
        return 0;

    return (runtime_rate >= 0.995 && runtime_rate <= 1.005);
}

static inline int vj_audio_pacing_effective_sfd(veejay_t *info)
{
    video_playback_setup *settings;
    int sfd = veejay_track_align_current_sfd(info);
    int settings_sfd = 1;
    int info_sfd = 1;
    int slice_len = 1;

    if(!info || !info->settings)
        return sfd > 0 ? sfd : 1;

    settings = info->settings;

    if(settings->sfd > 0)
        settings_sfd = settings->sfd;
    if(info->sfd > 0)
        info_sfd = info->sfd;

    slice_len = atomic_load_int(&settings->audio_slice_len);
    if(slice_len <= 0)
        slice_len = 1;

    if(settings_sfd > sfd)
        sfd = settings_sfd;
    if(info_sfd > sfd)
        sfd = info_sfd;
    if(slice_len > sfd)
        sfd = slice_len;

    if(sfd <= 0)
        sfd = 1;

    return sfd;
}

static inline int vj_audio_should_pace_direct_jack_monitor(
    video_playback_setup *settings,
    int audio_source,
    int sync_mode,
    int sync_enabled,
    int speed,
    double runtime_rate,
    int sfd,
    int *external_tape_source
) {
    int external_monitor = 0;
    int neutral = 0;
    int mute = 1;
    int external_tape = 0;

    if(external_tape_source)
        *external_tape_source = 0;

    if(!settings)
        return 0;

    external_monitor = vj_audio_uses_external_audio_playback_source(
        settings, audio_source, sync_mode, sync_enabled
    );

    neutral = vj_audio_external_jack_transport_is_neutral(
        speed, runtime_rate, sfd
    );

    mute = atomic_load_int(&settings->audio_mute);

    external_tape = external_monitor && !mute &&
        vj_audio_sync_mode_uses_transport_driven_playback(sync_mode) &&
        (sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
         sync_mode == VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY ||
         !neutral);

    if(external_tape_source)
        *external_tape_source = external_tape;

    return 0;
}

static inline double vj_audio_pace_direct_jack_monitor_block(
    video_playback_setup *settings,
    int client_frames,
    long client_rate,
    long jack_rate,
    unsigned long long *next_hw_frame,
    int *pace_active,
    unsigned long long *hw_before,
    unsigned long long *hw_target,
    unsigned long long *hw_after,
    int *waits
) {
    double start_s;
    unsigned long long now_hw;
    unsigned long long need_hw;
    unsigned long long target;
    int local_waits = 0;

    if(hw_before) *hw_before = 0;
    if(hw_target) *hw_target = 0;
    if(hw_after) *hw_after = 0;
    if(waits) *waits = 0;

    if(!settings || client_frames <= 0 || client_rate <= 0 || jack_rate <= 0 ||
       !next_hw_frame || !pace_active)
        return 0.0;

    now_hw = (unsigned long long)vj_jack_get_played_frames();
    need_hw = (((unsigned long long)client_frames * (unsigned long long)jack_rate) +
               (unsigned long long)client_rate - 1ULL) /
              (unsigned long long)client_rate;
    if(need_hw < 1ULL)
        need_hw = 1ULL;

    if(!(*pace_active) || *next_hw_frame == 0ULL ||
       now_hw > (*next_hw_frame + (need_hw * 4ULL)))
    {
        target = now_hw + need_hw;
        *pace_active = 1;
    }
    else {
        target = *next_hw_frame + need_hw;
        if(target <= now_hw)
            target = now_hw + need_hw;
    }

    *next_hw_frame = target;
    if(hw_before) *hw_before = now_hw;
    if(hw_target) *hw_target = target;

    start_s = monotonic_now_s();
    while(atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        unsigned long long cur = (unsigned long long)vj_jack_get_played_frames();
        if(cur >= target) {
            now_hw = cur;
            break;
        }

        unsigned long long remain = target - cur;
        long sleep_us = (long)((remain * 1000000ULL) / (unsigned long long)jack_rate);
        if(sleep_us < 100)
            sleep_us = 100;
        else if(sleep_us > 2000)
            sleep_us = 2000;

        local_waits++;
        usleep_accurate(sleep_us, settings);
    }

    now_hw = (unsigned long long)vj_jack_get_played_frames();
    if(hw_after) *hw_after = now_hw;
    if(waits) *waits = local_waits;

    return (monotonic_now_s() - start_s) * 1000.0;
}
 
#endif

#ifdef HAVE_JACK
typedef struct
{
    double rate_mul;
    int snap_frames;
    int locked;
    int confidence;
    int offset_ms;
} veejay_track_align_adjustment_t;
static int veejay_track_align_get_adjustment(veejay_t *info,
                                             double media_fps,
                                             veejay_track_align_adjustment_t *adj)
{
    static long last_snap_ms = 0;
    static int last_pm = -9999;
    static int last_id = -9999;
    static int last_reacquire_seq = -9999;
    static int wav_plain_lock_applied = 0;
    static long long wav_plain_last_frame = -1;

    video_playback_setup *settings;
    vj_audio_sync_snapshot_t snap;
    int mode;
    int pm;
    int id;

    if(adj) {
        adj->rate_mul = 1.0;
        adj->snap_frames = 0;
        adj->locked = 0;
        adj->confidence = 0;
        adj->offset_ms = 0;
    }

    if(!info || !info->settings || !adj)
        return 0;

    if(media_fps <= 0.0)
        media_fps = 25.0;

    settings = info->settings;
    mode = atomic_load_int(&settings->audio_sync.mode);
    if(mode != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return 0;
    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return 0;
    if(settings->current_playback_speed < 0)
        return 0;
    if(!vj_audio_sync_get_snapshot(&settings->audio_sync, &snap))
        return 0;

    pm = info->uc ? info->uc->playback_mode : -1;
    id = info->uc ? info->uc->sample_id : -1;
    {
        int reacquire_seq = atomic_load_int(&settings->track_align_reacquire_seq);
        if(pm != last_pm || id != last_id || reacquire_seq != last_reacquire_seq) {
            last_pm = pm;
            last_id = id;
            last_reacquire_seq = reacquire_seq;
            last_snap_ms = 0;
            wav_plain_lock_applied = 0;
            wav_plain_last_frame = -1;
        }
    }

    adj->locked = snap.track_align_locked ? 1 : 0;
    adj->confidence = snap.track_align_confidence_pct;
    adj->offset_ms = snap.track_align_offset_ms;

    if(pm == VJ_PLAYBACK_MODE_PLAIN &&
       snap.source == VJ_AUDIO_SYNC_SOURCE_WAV_FILE)
    {
        int cached_delta = 0;
        int cached_conf = 0;

        if(vj_audio_sync_wav_plain_lock_get(&settings->audio_sync,
                                            &cached_delta,
                                            &cached_conf))
        {
            long long cur = atomic_load_long_long(&settings->current_frame_num);
            long long min_frame = atomic_load_long_long(&settings->min_frame_num);
            int start_window_frames =
                (int)((media_fps * (double)VJ_WAV_PLAIN_LOCK_START_WINDOW_MS / 1000.0) + 0.5);

            if(start_window_frames < 2)
                start_window_frames = 2;

            if(wav_plain_last_frame >= 0 && cur < wav_plain_last_frame)
                wav_plain_lock_applied = 0;
            wav_plain_last_frame = cur;

            if(!wav_plain_lock_applied &&
               cur >= min_frame &&
               cur <= (min_frame + start_window_frames))
            {
                vj_audio_sync_wav_restart(&settings->audio_sync);

                adj->snap_frames = cached_delta;
                adj->confidence = cached_conf;
                adj->locked = 1;
                adj->offset_ms = 0;

                wav_plain_lock_applied = 1;
                last_snap_ms = (long)(monotonic_now_s() * 1000.0);

                veejay_msg(VEEJAY_MSG_INFO,
                           "[TRACK-ALIGN][WAV] applying cached PLAIN lock %+dfr conf=%d%% at replay-start frame=%lld",
                           cached_delta,
                           cached_conf,
                           cur);
                return 1;
            }
        }
    }


    {
        int snap_delta = 0;
        int snap_conf = 0;
        if(vj_audio_sync_track_align_consume_snap(&settings->audio_sync,
                                                  &snap_delta,
                                                  &snap_conf))
        {
            long now_ms = (long)(monotonic_now_s() * 1000.0);
            {
                int tiny_servo = ((snap_delta == 1 || snap_delta == -1) &&
                                  snap_conf >= VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_MIN_CONF);
                long required_cooldown = tiny_servo ?
                                         VJ_TRACK_ALIGN_SNAP_CONSUME_SERVO_COOLDOWN_MS :
                                         VJ_TRACK_ALIGN_SNAP_CONSUME_COOLDOWN_MS;

                if(snap_conf >= VJ_TRACK_ALIGN_SNAP_CONSUME_MIN_CONF &&
                   ((snap_delta <= -2 || snap_delta >= 2) || tiny_servo) &&
                   (last_snap_ms == 0 ||
                    snap_delta >= 0 ||
                    tiny_servo ||
                    fabs((double)snap_delta * 1000.0 / media_fps) >= VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS) &&
                   (last_snap_ms == 0 ||
                    (now_ms - last_snap_ms) >= required_cooldown))
                {
                    int original_delta = snap_delta;
                    int wav_plain_lock =
                        (pm == VJ_PLAYBACK_MODE_PLAIN &&
                         snap.source == VJ_AUDIO_SYNC_SOURCE_WAV_FILE &&
                         snap_conf >= VJ_WAV_PLAIN_LOCK_MIN_CONF);

                    if(wav_plain_lock) {
                        vj_audio_sync_wav_plain_lock_store(&settings->audio_sync,
                                                           original_delta,
                                                           snap_conf);
                        snap_delta = original_delta;
                    }
                    else {
                        int max_frames = (int)(media_fps * 8.0 + 0.5);
                        if(max_frames < 2)
                            max_frames = 2;
                        if(snap_delta > max_frames)
                            snap_delta = max_frames;
                        else if(snap_delta < -max_frames)
                            snap_delta = -max_frames;
                    }

                    adj->snap_frames = snap_delta;
                    adj->confidence = snap_conf;

                    if(!tiny_servo)
                        veejay_track_align_arm_audio_guard(settings,
                                                           now_ms,
                                                           snap_delta,
                                                           media_fps,
                                                           wav_plain_lock ?
                                                               "wav-plain-lock" :
                                                               "snap-consume");
                    last_snap_ms = now_ms;
                    return 1;
                }
            }
        }
    }

    adj->rate_mul = 1.0;
    return 0;
}
#endif

void *veejay_audio_producer_thread(void *arg)
{
    veejay_t *info = (veejay_t *)arg;
    video_playback_setup *settings = info->settings;
    editlist *el = info->current_edit_list ? info->current_edit_list : info->edit_list;
    int BPS = 4;
    int media_audio = 0;
    int embedded_media_audio = 0;

#ifdef HAVE_JACK
    const int playback_enabled =
        (info->audio == AUDIO_PLAY) &&
        !atomic_load_int(&settings->audio_threads_disabled);

    if(playback_enabled) {
        media_audio = vj_perform_init_audio(info,0) &&
                      vj_perform_init_audio(info,1);
        el = info->current_edit_list ? info->current_edit_list : info->edit_list;
    }
#else
    media_audio = (el && el->has_audio &&
                   vj_perform_init_audio(info,0) &&
                   vj_perform_init_audio(info,1) &&
                   info->audio == AUDIO_PLAY);
#endif

    if(el) {
        embedded_media_audio = (el->has_audio && el->audio_bps > 0);
        if(el->audio_bps > 0)
            BPS = el->audio_bps;
    }

#ifdef HAVE_JACK
    int has_audio = playback_enabled && media_audio;
#else
    int has_audio = media_audio;
#endif
#ifndef HAVE_JACK
	has_audio = 0;
#endif

#ifdef HAVE_JACK
    if(has_audio) {
        vj_audio_setup_rt_thread(settings);
    } else
#endif
    {
        settings->clock_overshoot = 30000;
        settings->pause_cost_ns = 10.0;
    }

#ifdef HAVE_JACK
    const long CLIENT_RATE = vj_jack_get_client_samplerate();
    const long JACK_RATE   = vj_jack_get_rate();
#else
    const long CLIENT_RATE = el->audio_rate;
#endif

#ifdef HAVE_JACK
    if(has_audio)
        vj_perform_record_output_audio_tap_configure(info, BPS, (int)CLIENT_RATE);
#endif

    double anchor_s = 0;
    double audio_frame_accum = 0.0;
    double slow_video_phase = 0.0;
    int last_dynamic_slow = 0;
    unsigned long long loop_count = 0;
    const int MAX_CLIENT_FRAMES = (int)(((double)CLIENT_RATE / VJ_DYNAMIC_ALLOC_MIN_FPS) + 4096.0);

    uint8_t *audio_chunk = NULL;
    uint8_t *silenced    = NULL;
#ifdef HAVE_JACK
    uint8_t *external_tape_pending = NULL;
    int external_tape_pending_frames = 0;
    int external_tape_pending_capacity = 0;
#endif

	if( has_audio ) {
		size_t audio_buf_len = MAX_CLIENT_FRAMES * BPS * sizeof(uint8_t);
		audio_chunk = (uint8_t*) vj_calloc(audio_buf_len);
    	if(!audio_chunk) {
			goto AUDIO_PRODUCER_THREAD_EXIT;
		}
		silenced    = (uint8_t*) vj_calloc(audio_buf_len);
		if(!silenced) {
			free(audio_chunk);
			goto AUDIO_PRODUCER_THREAD_EXIT;
		}
#ifdef HAVE_JACK
        external_tape_pending_capacity = MAX_CLIENT_FRAMES * 4;
        if(external_tape_pending_capacity < MAX_CLIENT_FRAMES)
            external_tape_pending_capacity = MAX_CLIENT_FRAMES;
        size_t external_tape_pending_len = (size_t)external_tape_pending_capacity * (size_t)BPS;
        external_tape_pending = (uint8_t*) vj_calloc(external_tape_pending_len);
        if(!external_tape_pending) {
            free(audio_chunk);
            free(silenced);
            goto AUDIO_PRODUCER_THREAD_EXIT;
        }
#endif
		mlock(audio_chunk,audio_buf_len);
		mlock(silenced,audio_buf_len);
#ifdef HAVE_JACK
        mlock(external_tape_pending, external_tape_pending_len);
#endif
	}

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    atomic_store_int(&info->audio_running, 1);
    atomic_store_long_long(&settings->current_frame_num, -1);

#ifdef HAVE_JACK
    if (has_audio) {
        anchor_s = (double) vj_jack_get_played_frames() / JACK_RATE;

        vj_runtime_publish_audio_clocks(settings, anchor_s, anchor_s);
        atomic_store_double(&settings->audio_start_offset, anchor_s);
        atomic_store_double(&settings->fps_epoch_s, anchor_s);
        atomic_store_long_long(&settings->fps_epoch_frame, 0);

        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Audio anchor established at %fs", anchor_s);
        atomic_store_long_long(&settings->current_frame_num, 0);
    } else {
#endif    
		anchor_s = monotonic_now_s();
        atomic_store_double(&settings->audio_start_offset, anchor_s);
        vj_runtime_publish_audio_clocks(settings, anchor_s, anchor_s);
        atomic_store_double(&settings->fps_epoch_s, anchor_s);
        atomic_store_long_long(&settings->fps_epoch_frame, 0);
        atomic_store_long_long(&settings->current_frame_num, 0);

        veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Monotonic clock anchor established at %fs", anchor_s);

#ifdef HAVE_JACK
    }
#endif

	if (loop_count == 0) {
		atomic_store_int(&settings->first_audio_frame_ready, 1);
		veejay_msg(VEEJAY_MSG_INFO, has_audio
			? "[AUDIO] First audio frame queued, starting video"
			: "[AUDIO] Monotonic clock initialized, starting video");
	}

    while (atomic_load_int(&info->audio_running) &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
    {

#ifdef HAVE_JACK
        if (has_audio) {
            double media_fps = (el != NULL && el->video_fps > 0.0) ? (double)el->video_fps : 25.0;
            double runtime_rate = atomic_load_double(&settings->runtime_playback_rate);
            if (runtime_rate <= 0.0) {
                double fps = settings->output_fps;
                runtime_rate = (fps > 0.0) ? (fps / media_fps) : 1.0;
            }
            if (runtime_rate < 0.01)
                runtime_rate = 0.01;
            else if (runtime_rate > 16.0)
                runtime_rate = 16.0;

            const int dynamic_slow = (runtime_rate < 0.9995);
            double spvf = dynamic_slow ? (1.0 / media_fps) : settings->spvf;
            if (dynamic_slow != last_dynamic_slow) {
                audio_frame_accum = 0.0;
                slow_video_phase = 0.0;
                last_dynamic_slow = dynamic_slow;
            }
            audio_frame_accum += spvf * (double)CLIENT_RATE;
            int needed = (int)audio_frame_accum;
            if (needed < 1) needed = 1;
            if (needed > MAX_CLIENT_FRAMES) needed = MAX_CLIENT_FRAMES;
            audio_frame_accum -= (double)needed;
            if (audio_frame_accum < 0.0) audio_frame_accum = 0.0;
            long long media_frame = atomic_load_long_long(&settings->current_frame_num); 
            const int speed = settings->current_playback_speed;
			int decoded;
            double audio_loop_start_s = monotonic_now_s();
            double master_before_s = atomic_load_double(&settings->audio_master_s);
            double queued_before_s = atomic_load_double(&settings->audio_queued_s);
            double played_before_s = (double)vj_jack_get_played_frames() / (double)JACK_RATE;
            if(queued_before_s <= 0.0)
                queued_before_s = master_before_s;
            if(played_before_s > 0.0) {
                if(queued_before_s < played_before_s)
                    queued_before_s = played_before_s;
                vj_runtime_publish_audio_clocks(settings, played_before_s, queued_before_s);
                master_before_s = played_before_s;
            }
            int audio_source_dbg = atomic_load_int(&settings->record_audio_source);
            int audio_sync_mode_dbg = atomic_load_int(&settings->audio_sync.mode);
            int audio_sync_enabled_dbg = vj_audio_sync_is_enabled(&settings->audio_sync);
            int external_tape_source_dbg = 0;
            int sfd_dbg = vj_audio_pacing_effective_sfd(info);

            const int external_tape_neutral_dbg =
                vj_audio_external_jack_transport_is_neutral(
                    speed,
                    runtime_rate,
                    sfd_dbg
                );
            int external_monitor_audio =
                vj_audio_uses_external_audio_playback_source(
                    settings,
                    audio_source_dbg,
                    audio_sync_mode_dbg,
                    audio_sync_enabled_dbg
                );
            external_tape_source_dbg =
                external_monitor_audio &&
                vj_audio_sync_mode_uses_transport_driven_playback(audio_sync_mode_dbg) &&
                (audio_sync_mode_dbg == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
                 audio_sync_mode_dbg == VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY ||
                 !external_tape_neutral_dbg);
            int external_live_source_dbg =
                external_monitor_audio && !external_tape_source_dbg;
				if(vj_jack_xrun_flag()) {
			
					atomic_add_fetch_old_int(&settings->xruns, 1);
					
					double played_hw = (double) vj_jack_get_played_frames() / (double)JACK_RATE;
					double master = atomic_load_double(&settings->audio_master_s);
					double queued = atomic_load_double(&settings->audio_queued_s);

					double delta = played_hw - master;
					double half_frame_s = 0.5 * spvf;
					if (delta > half_frame_s) {
						vj_runtime_publish_audio_clocks(settings, played_hw, queued > played_hw ? queued : played_hw);
					}
				}

			int tx_active = atomic_load_int(&settings->transition.active) && atomic_load_int(&settings->transition.global_state);

			if(has_audio && tx_active && embedded_media_audio && !external_monitor_audio) {
				long long b_frame = vj_calc_next_subframe(info, settings->transition.next_id);
				long long start = atomic_load_long_long(&settings->transition.start);
				long long end = atomic_load_long_long(&settings->transition.end);
				decoded = vj_perform_queue_audio_chunk_crossfade(info, needed, media_frame, b_frame, audio_chunk, settings->transition.next_id,start,end);
			}
			else if(has_audio) {
				decoded = vj_perform_queue_audio_chunk_ext(info, needed, media_frame, 0, audio_chunk);
				
				if (decoded <= 0) { 
					long sleep_us = 100;
                    int retries = 0;

					while (decoded <= 0 &&
                           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP &&
                           retries++ < 8) {
						usleep_accurate(sleep_us, settings);
						decoded = vj_perform_queue_audio_chunk_ext(info, needed, media_frame, 0, audio_chunk);
						sleep_us = sleep_us < 2000 ? sleep_us * 2 : 2000;
					}

                    if(decoded <= 0) {
                        veejay_memset(audio_chunk, 0, (size_t)needed * (size_t)BPS);
                        decoded = needed;
                    }
				}
			}
            else {
#ifdef HAVE_JACK
                vj_jack_set_input_passthrough(0);
#endif
                veejay_memset(audio_chunk, 0, (size_t)needed * (size_t)BPS);
                decoded = needed;
            }

            int frames_written = 0;
            int write_pos = 0;
            int remaining = decoded;
            int write_iters = 0;
            int write_waits = 0;
            int write_zero = 0;
            int write_short = 0;
            int write_split = 0;
            int write_tape_space_waits = 0;
            int write_tape_deferred = 0;
            int write_tape_pending_in = 0;
            int write_tape_pending_out = 0;
            int write_tape_pending_drop = 0;
            int write_wait_target_jack = 0;
            int write_tape_chunk_cap = 0;
            int write_tape_period = 0;
            int write_tape_target_jack = 0;
            int free_first = -1;
            int free_last = -1;
            int free_min = 0x7fffffff;
            int free_max = -1;

            const int external_tape_pending_allowed = 0;

            if(external_tape_source_dbg && external_tape_neutral_dbg &&
               external_tape_pending_frames > 0) {
                write_tape_pending_drop = external_tape_pending_frames;
                atomic_add_fetch_old_long_long(&settings->audio_osd.prod_pending_drop_frames,
                                               external_tape_pending_frames);
                external_tape_pending_frames = 0;
            }

            if(external_tape_pending_allowed &&
               external_tape_pending && external_tape_pending_capacity > 0) {

                write_tape_pending_in = external_tape_pending_frames;

                if(decoded > 0) {
                    int append_off = 0;
                    int append_frames = decoded;
                    int total = external_tape_pending_frames + decoded;

                    if(total > external_tape_pending_capacity) {
                        int drop = total - external_tape_pending_capacity;
                        write_tape_pending_drop = drop;

                        if(drop >= external_tape_pending_frames) {
                            int drop_from_current = drop - external_tape_pending_frames;
                            external_tape_pending_frames = 0;
                            if(drop_from_current > append_frames)
                                drop_from_current = append_frames;
                            append_off = drop_from_current;
                            append_frames -= drop_from_current;
                        }
                        else if(drop > 0) {
                            int keep = external_tape_pending_frames - drop;
                            memmove(external_tape_pending,
                                    external_tape_pending + ((size_t)drop * (size_t)BPS),
                                    (size_t)keep * (size_t)BPS);
                            external_tape_pending_frames = keep;
                        }
                    }

                    if(append_frames > 0) {
                        memcpy(external_tape_pending + ((size_t)external_tape_pending_frames * (size_t)BPS),
                               audio_chunk + ((size_t)append_off * (size_t)BPS),
                               (size_t)append_frames * (size_t)BPS);
                        external_tape_pending_frames += append_frames;
                    }
                }

                while (external_tape_pending_frames > 0 &&
                       atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
                {
                    int chunk = external_tape_pending_frames;
                    int cap_period = 0;
                    int cap_target_jack = 0;
                    int cap = vj_audio_external_tape_max_chunk_client_frames(CLIENT_RATE,
                                                                             JACK_RATE,
                                                                             &cap_period,
                                                                             &cap_target_jack);
                    if(cap > 0) {
                        if(write_tape_chunk_cap <= 0 || cap < write_tape_chunk_cap)
                            write_tape_chunk_cap = cap;
                        write_tape_period = cap_period;
                        write_tape_target_jack = cap_target_jack;
                        if(chunk > cap) {
                            chunk = cap;
                            write_split++;
                        }
                    }

                    if(chunk < 1)
                        chunk = 1;

                    long required_jack = vj_audio_required_jack_frames_for_client_chunk(chunk,
                                                                                       CLIENT_RATE,
                                                                                       JACK_RATE);
                    int free_jack = vj_jack_get_ringbuffer_frames_free();

                    if(free_first < 0)
                        free_first = free_jack;
                    free_last = free_jack;
                    if(free_jack < free_min)
                        free_min = free_jack;
                    if(free_jack > free_max)
                        free_max = free_jack;

                    if(free_jack < required_jack) {
                        int partial = vj_audio_client_frames_that_fit_jack_free(chunk,
                                                                                free_jack,
                                                                                CLIENT_RATE,
                                                                                JACK_RATE);
                        int min_partial = vj_audio_external_tape_min_partial_client_frames(CLIENT_RATE);

                        if(partial >= min_partial) {
                            chunk = partial;
                            required_jack = vj_audio_required_jack_frames_for_client_chunk(chunk,
                                                                                           CLIENT_RATE,
                                                                                           JACK_RATE);
                            write_split++;
                        }
                        else {
                            int wait_client = external_tape_pending_frames < min_partial ? external_tape_pending_frames : min_partial;
                            long wait_jack = vj_audio_required_jack_frames_for_client_chunk(wait_client,
                                                                                           CLIENT_RATE,
                                                                                           JACK_RATE);
                            if(wait_jack <= 0)
                                wait_jack = required_jack;
                            if(wait_jack > required_jack)
                                wait_jack = required_jack;
                            write_wait_target_jack = (int)wait_jack;
                            write_tape_deferred++;
                            break;
                        }
                    }

                    write_iters++;
                    int written = vj_perform_play_audio(info, settings,
                                                        external_tape_pending,
                                                        chunk * BPS,
                                                        silenced);
                    if(written <= 0) {
                        write_zero++;
                        write_tape_deferred++;
                        break;
                    }

                    if(written > chunk)
                        written = chunk;
                    if(written < chunk)
                        write_short++;

                    if(written < external_tape_pending_frames) {
                        memmove(external_tape_pending,
                                external_tape_pending + ((size_t)written * (size_t)BPS),
                                (size_t)(external_tape_pending_frames - written) * (size_t)BPS);
                    }
                    external_tape_pending_frames -= written;
                    frames_written += written;
                }

                remaining = external_tape_pending_frames;
                write_tape_pending_out = external_tape_pending_frames;

                if(frames_written <= 0 && external_tape_pending_frames > 0)
                    usleep_accurate(500, settings);
            }
            else if(external_tape_source_dbg) {

                if(external_tape_pending_frames > 0) {
                    write_tape_pending_drop = external_tape_pending_frames;
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_pending_drop_frames,
                                                   external_tape_pending_frames);
                    external_tape_pending_frames = 0;
                }

                while (remaining > 0 && atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
                    int chunk = remaining;
                    int cap_period = 0;
                    int cap_target_jack = 0;
                    int cap = vj_audio_external_tape_max_chunk_client_frames(CLIENT_RATE,
                                                                             JACK_RATE,
                                                                             &cap_period,
                                                                             &cap_target_jack);
                    if(cap > 0) {
                        if(write_tape_chunk_cap <= 0 || cap < write_tape_chunk_cap)
                            write_tape_chunk_cap = cap;
                        write_tape_period = cap_period;
                        write_tape_target_jack = cap_target_jack;
                        if(chunk > cap) {
                            chunk = cap;
                            write_split++;
                        }
                    }

                    if(chunk < 1)
                        chunk = 1;

                    long required_jack = vj_audio_required_jack_frames_for_client_chunk(chunk,
                                                                                       CLIENT_RATE,
                                                                                       JACK_RATE);
                    int free_jack = vj_jack_get_ringbuffer_frames_free();

                    if(free_first < 0)
                        free_first = free_jack;
                    free_last = free_jack;
                    if(free_jack < free_min)
                        free_min = free_jack;
                    if(free_jack > free_max)
                        free_max = free_jack;

                    if(free_jack < required_jack) {
                        int partial = vj_audio_client_frames_that_fit_jack_free(chunk,
                                                                                free_jack,
                                                                                CLIENT_RATE,
                                                                                JACK_RATE);
                        int min_partial = vj_audio_external_tape_min_partial_client_frames(CLIENT_RATE);

                        if(partial >= min_partial) {
                            chunk = partial;
                            required_jack = vj_audio_required_jack_frames_for_client_chunk(chunk,
                                                                                           CLIENT_RATE,
                                                                                           JACK_RATE);
                            write_split++;
                        }
                        else {
                            int wait_client = remaining < min_partial ? remaining : min_partial;
                            long wait_jack = vj_audio_required_jack_frames_for_client_chunk(wait_client,
                                                                                           CLIENT_RATE,
                                                                                           JACK_RATE);
                            if(wait_jack <= 0)
                                wait_jack = required_jack;
                            if(wait_jack > required_jack)
                                wait_jack = required_jack;

                            write_tape_space_waits++;
                            write_waits++;
                            write_wait_target_jack = (int)wait_jack;
                            vj_audio_wait_for_jack_space((int)wait_jack, settings);
                            continue;
                        }
                    }

                    write_iters++;
                    int written = vj_perform_play_audio(info, settings,
                                                        audio_chunk + ((size_t)write_pos * (size_t)BPS),
                                                        chunk * BPS,
                                                        silenced);
                    if(written <= 0) {
                        write_zero++;
                        usleep_accurate(500, settings);
                        continue;
                    }

                    if(written > chunk)
                        written = chunk;
                    if(written < chunk)
                        write_short++;

                    write_pos += written;
                    remaining -= written;
                    frames_written += written;
                }
            }
            else {
                if(external_tape_pending_frames > 0) {
                    write_tape_pending_drop = external_tape_pending_frames;
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_pending_drop_frames,
                                                   external_tape_pending_frames);
                    external_tape_pending_frames = 0;
                }

                while (remaining > 0 && atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
                    int chunk = remaining;
                    if (chunk > 8192)
                        chunk = 8192;
                    if (chunk < 1)
                        chunk = 1;

                    long required_jack = vj_audio_required_jack_frames_for_client_chunk(chunk,
                                                                                       CLIENT_RATE,
                                                                                       JACK_RATE);

                    int free_jack = vj_jack_get_ringbuffer_frames_free();

                    if(free_first < 0)
                        free_first = free_jack;
                    free_last = free_jack;
                    if(free_jack < free_min)
                        free_min = free_jack;
                    if(free_jack > free_max)
                        free_max = free_jack;

                    if (free_jack < required_jack) {
                        write_waits++;
                        write_wait_target_jack = (int)required_jack;
                        vj_audio_wait_for_jack_space((int)required_jack, settings);
                        continue;
                    }

                    write_iters++;
                    int written = vj_perform_play_audio(info, settings,
                                                        audio_chunk + ((size_t)write_pos * (size_t)BPS),
                                                        chunk * BPS,
                                                        silenced);
                    if (written <= 0) {
                        write_zero++;
                        usleep_accurate(500, settings);
                        continue;
                    }

                    if (written > chunk)
                        written = chunk;
                    if(written < chunk)
                        write_short++;

                    write_pos += written;
                    remaining -= written;
                    frames_written += written;
                }
            }
          
            (void)write_iters;
            (void)write_split;
            (void)write_tape_space_waits;
            (void)write_tape_deferred;
            (void)write_tape_pending_in;
            (void)write_tape_pending_out;
            (void)write_wait_target_jack;
            (void)write_tape_chunk_cap;
            (void)write_tape_period;
            (void)write_tape_target_jack;
            (void)free_first;
            (void)free_max;
            (void)played_before_s;

            double played_hw = (double) vj_jack_get_played_frames() / (double)JACK_RATE;
            double predicted = played_hw + (double)frames_written / (double)CLIENT_RATE;
            if(predicted < queued_before_s && external_tape_source_dbg)
                predicted = queued_before_s;
            vj_runtime_publish_audio_clocks(settings, played_hw, predicted);

            if(settings) {
                double queued_ms = (predicted - played_hw) * 1000.0;
                double jack_total_ms = vj_jack_get_total_latency() * 1000.0;
                int output_latency_ms;
                int heard_latency_ms;
                int monitor_heard_ms;

                if(queued_ms < 0.0)
                    queued_ms = 0.0;
                else if(queued_ms > 5000.0)
                    queued_ms = 5000.0;

                output_latency_ms = (int)(queued_ms + 0.5);
                heard_latency_ms = output_latency_ms;

                monitor_heard_ms = vj_audio_monitor_direct_heard_latency_ms(info,
                                                                            settings,
                                                                            audio_source_dbg,
                                                                            audio_sync_mode_dbg,
                                                                            audio_sync_enabled_dbg);
                if(monitor_heard_ms >= 0)
                    heard_latency_ms = monitor_heard_ms;
                else if(jack_total_ms > 0.0 && jack_total_ms < 5000.0)
                    heard_latency_ms = (int)(jack_total_ms + 0.5);

                vj_audio_beat_set_output_latency_ms(&settings->audio_beat, output_latency_ms);
                vj_audio_beat_set_heard_latency_ms(&settings->audio_beat, heard_latency_ms);
            }

            {
                double audio_loop_elapsed_ms = (monotonic_now_s() - audio_loop_start_s) * 1000.0;
                long long predicted_ms = (long long)(predicted * 1000.0 + 0.5);

                if(free_min == 0x7fffffff)
                    free_min = -1;

                atomic_store_long_long(&settings->audio_osd.prod_loops, (long long)loop_count);
                if(write_zero > 0)
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_write_zero, write_zero);
                if(write_short > 0)
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_write_short, write_short);
                if(write_waits > 0)
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_waits, write_waits);
                if(write_tape_pending_drop > 0 &&
                   !(external_tape_source_dbg && external_tape_neutral_dbg))
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_pending_drop_frames,
                                                   write_tape_pending_drop);

                atomic_store_int(&settings->audio_osd.last_src, audio_source_dbg);
                atomic_store_int(&settings->audio_osd.last_sync, audio_sync_mode_dbg);
                atomic_store_int(&settings->audio_osd.last_speed, speed);
                atomic_store_int(&settings->audio_osd.last_sfd, sfd_dbg);
                atomic_store_int(&settings->audio_osd.last_needed, needed);
                atomic_store_int(&settings->audio_osd.last_decoded, decoded);
                atomic_store_int(&settings->audio_osd.last_written, frames_written);
                atomic_store_int(&settings->audio_osd.last_pending, external_tape_pending_frames);
                atomic_store_int(&settings->audio_osd.last_free_jack, free_last);
                atomic_store_int(&settings->audio_osd.last_elapsed_ms,
                                 (int)(audio_loop_elapsed_ms + 0.5));
                atomic_store_long_long(&settings->audio_osd.last_predicted_ms, predicted_ms);
                atomic_store_long_long(&settings->audio_osd.last_media_frame, media_frame);
            }

            if (dynamic_slow) {
                slow_video_phase += runtime_rate;
                int guard = 0;
                while (slow_video_phase >= 1.0 && guard < 32) {
                    vj_perform_inc_frame(info, settings->current_playback_speed);
                    slow_video_phase -= 1.0;
                    guard++;
                }
            } else {
                slow_video_phase = 0.0;
                vj_perform_inc_frame(info, settings->current_playback_speed);
            }

				loop_count++;

            {
                const int external_live_cadence_pace =
                    (external_live_source_dbg || external_tape_source_dbg);
                vj_audio_producer_pace_budget_t pace_budget;
                double sleep_s = vj_audio_producer_pace_sleep_s(CLIENT_RATE,
                                                                JACK_RATE,
                                                                frames_written,
                                                                predicted,
                                                                external_live_cadence_pace,
                                                                settings,
                                                                &pace_budget);


                if(external_live_source_dbg && frames_written > 0 && CLIENT_RATE > 0) {
                    double block_s = (double)frames_written / (double)CLIENT_RATE;
                    double elapsed_s = monotonic_now_s() - audio_loop_start_s;
                    double wall_sleep_s = block_s - elapsed_s;
                    double max_sleep_s = VJ_AUDIO_PRODUCER_LIVE_MAX_SLEEP_MS / 1000.0;

                    if(wall_sleep_s < 0.0)
                        wall_sleep_s = 0.0;
                    if(max_sleep_s < 0.001)
                        max_sleep_s = 0.001;
                    if(wall_sleep_s > max_sleep_s)
                        wall_sleep_s = max_sleep_s;

                    sleep_s = wall_sleep_s;

                    pace_budget.policy = "external-live-wall";
                    pace_budget.block_s = block_s;
                    pace_budget.sleep_s = sleep_s;
                }

                atomic_store_int(&settings->audio_osd.last_qdepth_ms,
                                 (int)(pace_budget.queue_depth_s * 1000.0 + 0.5));
                atomic_store_int(&settings->audio_osd.last_sleep_ms,
                                 (int)(sleep_s * 1000.0 + 0.5));

                {
                    long long sleep_us = (long long)(sleep_s * 1000000.0 + 0.5);
                    if(sleep_us > 0)
                        usleep_accurate(sleep_us, settings);
                }
            }

        } else {
#endif
			double spvf = vj_runtime_effective_spvf(settings);
			const double next_frame_target = vj_runtime_target_time_s(settings, (long long)(loop_count + 1));

			double now = monotonic_now_s();
			double remaining_s = next_frame_target - now;

			if (remaining_s > 0.0) {
				if (remaining_s > spvf)
					remaining_s = spvf;

				usleep_hybrid((long long)(remaining_s * 1e6), settings);
			}

			now = monotonic_now_s();

			if (atomic_load_int(&info->sync_correction)) {

				double drift = now - next_frame_target;

				static double integral = 0.0;

				const double KP = 0.6;
				const double KI = 0.15;

				integral = integral * 0.90 + drift;

				if (integral > 0.15)  integral = 0.15;
				if (integral < -0.15) integral = -0.15;

				double correction = (KP * drift) + (KI * integral);
				const double max_correction = 0.5 * spvf;

				if (correction >  max_correction) correction =  max_correction;
				if (correction < -max_correction) correction = -max_correction;

				static double phase_offset = 0.0;

				phase_offset = 0.95 * phase_offset + correction;

				if (phase_offset >  spvf) phase_offset =  spvf;
				if (phase_offset < -spvf) phase_offset = -spvf;

				vj_runtime_publish_audio_clocks(settings, now + phase_offset, now + phase_offset);

			} else {
				vj_runtime_publish_audio_clocks(settings, now, now);
			}

			vj_perform_inc_frame(info, settings->current_playback_speed);
			loop_count++;

		}
#ifdef HAVE_JACK
	}
#endif

	atomic_store_int(&info->audio_running, 0);

    free(audio_chunk);
    free(silenced);
#ifdef HAVE_JACK
    free(external_tape_pending);
#endif
AUDIO_PRODUCER_THREAD_EXIT:
    pthread_exit(NULL);
    return NULL;
}



static int veejay_audio_sync_mode_is_playback(int mode)
{
    if(mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        return 0;
    return vj_audio_sync_mode_uses_external_playback(mode);
}

static int veejay_audio_threads_disabled(video_playback_setup *settings)
{
    /* Legacy coarse kill-switch kept for older action files/config paths. */
    return settings ? (atomic_load_int(&settings->audio_threads_disabled) ? 1 : 0) : 1;
}

static int veejay_audio_sync_thread_disabled(video_playback_setup *settings)
{
    if(veejay_audio_threads_disabled(settings))
        return 1;

    return __sync_add_and_fetch(&veejay_audio_sync_thread_cli_disabled_, 0) ? 1 : 0;
}

static int veejay_audio_beat_thread_disabled(video_playback_setup *settings)
{
    if(veejay_audio_threads_disabled(settings))
        return 1;

    return __sync_add_and_fetch(&veejay_audio_beat_thread_cli_disabled_, 0) ? 1 : 0;
}

static int veejay_audio_beat_provider_needed(video_playback_setup *settings)
{
    if(!settings || veejay_audio_beat_thread_disabled(settings))
        return 0;

    return atomic_load_int(&settings->audio_beat.initialized) &&
           atomic_load_int(&settings->audio_beat.enabled);
}

static int veejay_audio_external_provider_needed(video_playback_setup *settings)
{
    int source;

    if(!settings || veejay_audio_sync_thread_disabled(settings))
        return 0;

    if(veejay_audio_beat_provider_needed(settings))
        return 1;

    source = atomic_load_int(&settings->record_audio_source);
    return source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK;
}

static void veejay_audio_sync_leave_external_playback(veejay_t *info)
{
    video_playback_setup *settings;
    int mode;

    if(!info || !info->settings)
        return;

    settings = info->settings;

    mode = atomic_load_int(&settings->audio_sync.mode);
    const int sync_enabled = vj_audio_sync_is_enabled(&settings->audio_sync);

    if(veejay_audio_sync_mode_is_playback(mode) && sync_enabled) {
        int pending_guard = vj_perform_audio_source_transition_guard_pending();
        if(pending_guard <= 0) {
            vj_perform_audio_source_transition_guard_ex(4,
                                                        "leave-external-playback",
                                                        atomic_load_int(&settings->record_audio_source),
                                                        atomic_load_int(&settings->record_audio_source),
                                                        mode,
                                                        atomic_load_int(&settings->audio_mute));
        }
    }

    vj_jack_set_input_passthrough(0);

    if(!veejay_audio_sync_mode_is_playback(mode))
        return;

    if(veejay_audio_external_provider_needed(settings)) {
        vj_audio_sync_set_mode(&settings->audio_sync,
                               VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
        vj_audio_sync_enable(&settings->audio_sync);
    } else {
        vj_audio_sync_set_mode(&settings->audio_sync,
                               VJ_AUDIO_SYNC_MODE_OFF);
        vj_audio_sync_disable(&settings->audio_sync);
    }
}

static void veejay_producer_thread_audio_startup(veejay_t *info)
{
    video_playback_setup *settings = info->settings;

	int has_audio = (info->audio == AUDIO_PLAY);
#ifndef HAVE_JACK
	has_audio = 0;
#endif

#ifdef HAVE_JACK
    if(veejay_audio_threads_disabled(settings)) {
        has_audio = 0;
        info->audio = NO_AUDIO;
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO] JACK audio services disabled; using monotonic fallback");
    }
#endif

    if (has_audio) {
#ifdef HAVE_JACK	    
        if (!vj_perform_audio_start(info)) {
            has_audio = 0;
            info->audio = NO_AUDIO;
        } else {
            vj_jack_enable();
        }
#endif
    } else {
        info->audio = NO_AUDIO;
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Audio playback not enabled; using monotonic fallback");
    }

	veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Starting Audio Producer (Master Clock)");
    int ret = pthread_create(&(settings->audio_playback_thread), NULL,
                             veejay_audio_producer_thread, info);
    if (ret != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Failed to start Audio Producer thread.");
        if (has_audio) {
            vj_perform_audio_stop(info);
            info->audio = NO_AUDIO;
        }
        atomic_store_int(&settings->audio_threads_disabled, 1);
        veejay_wake_playback_waiters(info);
        return;
    }

    while (!atomic_load_int(&info->audio_running) &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
        usleep_accurate(200,settings);

}

#ifdef HAVE_JACK


static void veejay_audio_sync_thread_startup(veejay_t *info)
{
    video_playback_setup *settings;
    int ret;

    if(!info || !info->settings)
        return;

    settings = info->settings;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_INFO,
                   "[AUDIO-SYNC] Audio sync/control thread disabled by command line");
        return;
    }

    if(atomic_load_int(&settings->audio_sync.running))
        return;

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    atomic_store_int(&settings->audio_sync.stop_request, 0);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-SYNC] Starting Audio Sync/Control thread");

    ret = pthread_create(
        &(settings->audio_sync_thread),
        NULL,
        vj_audio_sync_thread,
        &settings->audio_sync
    );

    if(ret != 0)
    {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[AUDIO-SYNC] Failed to start Audio Sync/Control thread.");
        vj_audio_sync_request_stop(&settings->audio_sync);
        settings->audio_sync_thread = 0;
        return;
    }

    while(!atomic_load_int(&settings->audio_sync.running) &&
          atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
        usleep_accurate(200, settings);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-SYNC] Sync thread state: initialized=%d running=%d enabled=%d open=%d",
               atomic_load_int(&settings->audio_sync.initialized),
               atomic_load_int(&settings->audio_sync.running),
               atomic_load_int(&settings->audio_sync.enabled),
               atomic_load_int(&settings->audio_sync.open));
}


int veejay_audio_sync_set_mode_control(veejay_t *info, int mode)
{
    video_playback_setup *settings;

    if(!info || !info->settings)
        return -1;

    if(mode < VJ_AUDIO_SYNC_MODE_OFF)
        mode = VJ_AUDIO_SYNC_MODE_OFF;
    else if(mode > VJ_AUDIO_SYNC_MODE_MAX)
        mode = VJ_AUDIO_SYNC_MODE_MAX;

    settings = info->settings;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC] mode request ignored; audio sync/control thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    if(mode == VJ_AUDIO_SYNC_MODE_OFF) {
        if(veejay_audio_external_provider_needed(settings)) {
            vj_audio_sync_set_mode(&settings->audio_sync,
                                   VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
            vj_audio_sync_enable(&settings->audio_sync);
        } else {
            vj_audio_sync_set_mode(&settings->audio_sync,
                                   VJ_AUDIO_SYNC_MODE_OFF);
            vj_audio_sync_disable(&settings->audio_sync);
        }

        return VJ_AUDIO_SYNC_MODE_OFF;
    }

    veejay_audio_sync_thread_startup(info);

    if(atomic_load_int(&settings->audio_sync.source) == VJ_AUDIO_SYNC_SOURCE_NONE)
        vj_audio_sync_set_source_jack(&settings->audio_sync, 2);

    vj_audio_sync_set_mode(&settings->audio_sync, mode);
    if(mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
       mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW ||
       mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        vj_audio_sync_set_target_mode(&settings->audio_sync,
                                      VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP);

    return vj_audio_sync_enable(&settings->audio_sync) > 0 ? mode : -1;
}

int veejay_audio_sync_set_enabled(veejay_t *info, int enabled)
{
    video_playback_setup *settings;
    int mode;

    settings = info->settings;
    enabled = enabled ? 1 : 0;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC] request ignored; audio sync/control thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    if(!enabled) {
        veejay_audio_sync_leave_external_playback(info);

        if(!veejay_audio_external_provider_needed(settings))
            return vj_audio_sync_disable(&settings->audio_sync) > 0 ? 0 : -1;

        return 0;
    }

    veejay_audio_sync_thread_startup(info);

    if(atomic_load_int(&settings->audio_sync.source) == VJ_AUDIO_SYNC_SOURCE_NONE)
        vj_audio_sync_set_source_jack(&settings->audio_sync, 2);

    mode = atomic_load_int(&settings->audio_sync.mode);
    if(mode == VJ_AUDIO_SYNC_MODE_OFF)
        vj_audio_sync_set_mode(&settings->audio_sync,
                               VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);

    return vj_audio_sync_enable(&settings->audio_sync) > 0 ? 1 : -1;
}

int veejay_audio_sync_set_bridge_correction(veejay_t *info, int max_pct)
{
    if(!info || !info->settings)
        return -1;

    if(veejay_audio_sync_thread_disabled(info->settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC] bridge correction request ignored; audio sync/control thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&info->settings->audio_sync.initialized))
        vj_audio_sync_init(&info->settings->audio_sync, 2);

    if(max_pct < 0)
        max_pct = 0;
    else if(max_pct > 25)
        max_pct = 25;

    vj_audio_sync_set_bridge_correction(&info->settings->audio_sync, max_pct);
    return max_pct;
}

static void veejay_audio_beat_thread_startup(veejay_t *info)
{
    video_playback_setup *settings;
    int ret;

    if(!info || !info->settings)
        return;

    settings = info->settings;

    if(veejay_audio_beat_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_INFO,
                   "[AUDIO-BEAT] Audio beat detector thread disabled by command line");
        return;
    }

    if(atomic_load_int(&settings->audio_beat.running))
        return;

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    if(!atomic_load_int(&settings->audio_beat.initialized))
        vj_audio_beat_init(&settings->audio_beat, 2);

    if(!settings->audio_beat.sync)
        vj_audio_beat_bind_sync(&settings->audio_beat, &settings->audio_sync);

    veejay_audio_sync_thread_startup(info);

    veejay_audio_beat_ensure_default_action(&settings->audio_beat, "thread-startup");

    atomic_store_int(&settings->audio_beat.stop_request, 0);
    atomic_store_int(&settings->audio_beat.enabled, 0);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] Starting Audio Beat Detector thread%s",
               (info->audio == NO_AUDIO) ? " (capture only; playback disabled)" : "");

    ret = pthread_create(&(settings->audio_beat_thread), NULL,
                         vj_audio_beat_thread, &settings->audio_beat);

    if(ret != 0)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO-BEAT] Failed to start Audio Beat Detector thread.");
        vj_audio_beat_request_stop(&settings->audio_beat);
        settings->audio_beat_thread = 0;
        return;
    }

    while(!atomic_load_int(&settings->audio_beat.running) &&
          atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP)
        usleep_accurate(200, settings);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] Detector thread state: initialized=%d running=%d enabled=%d open=%d audio=%d",
               atomic_load_int(&settings->audio_beat.initialized),
               atomic_load_int(&settings->audio_beat.running),
               atomic_load_int(&settings->audio_beat.enabled),
               atomic_load_int(&settings->audio_beat.open),
               info->audio);
}

static editlist *veejay_audio_beat_current_editlist(veejay_t *info)
{
    editlist *el = NULL;

    if(!info)
        return NULL;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        el = sample_get_editlist(info->uc->sample_id);

    if(!el)
        el = info->current_edit_list;
    if(!el)
        el = info->edit_list;

    return el;
}

static int veejay_audio_beat_prepare_push_source(veejay_t *info,
                                                 video_playback_setup *settings)
{
    editlist *el = veejay_audio_beat_current_editlist(info);
    int channels;
    int frame_bytes;
    int bits;
    int rate;

    if(!el || !settings)
        return 0;

    channels = el->audio_chans;
    frame_bytes = el->audio_bps;
    rate = el->audio_rate;

    if(channels < 1)
        channels = 1;
    else if(channels > 2)
        channels = 2;

    if(frame_bytes <= 0 || rate <= 0)
        return 0;

    if(frame_bytes % channels != 0)
        return 0;

    bits = (frame_bytes / channels) * 8;
    if(bits != 8 && bits != 16)
        bits = 16;

    vj_audio_sync_set_source_push(&settings->audio_sync,
                                  channels,
                                  bits,
                                  rate);
    return 1;
}

static void veejay_audio_beat_prepare_sync_source(veejay_t *info)
{
    video_playback_setup *settings;
    int mode;
    int source;
    int channels;
    int current_channels;
    int record_source;
    int external_mode;
    int use_push_source;

    if(!info || !info->settings)
        return;

    settings = info->settings;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] Cannot prepare JACK/push analysis source; audio sync/control thread is disabled");
        return;
    }

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    veejay_audio_sync_thread_startup(info);

    channels = atomic_load_int(&settings->audio_beat.input_channels_request);
    if(channels < 1)
        channels = 2;
    else if(channels > 2)
        channels = 2;

    mode = atomic_load_int(&settings->audio_sync.mode);
    if(mode == VJ_AUDIO_SYNC_MODE_OFF) {
        vj_audio_sync_set_mode(&settings->audio_sync,
                               VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
        mode = VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL;
    }

    source = atomic_load_int(&settings->audio_sync.source);
    current_channels = atomic_load_int(&settings->audio_sync.input_channels_request);
    record_source = atomic_load_int(&settings->record_audio_source);

    external_mode = vj_audio_sync_mode_is_audio_sync_family(mode);

    use_push_source = 0;
    if(record_source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL)
        use_push_source = 1;
    else if(record_source == VJ_RECORD_AUDIO_SOURCE_AUTO && !external_mode)
        use_push_source = 1;

    if(source != VJ_AUDIO_SYNC_SOURCE_WAV_FILE) {
        if(use_push_source) {
            if(!veejay_audio_beat_prepare_push_source(info, settings)) {
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-BEAT] Unable to prepare original-media push source; falling back to JACK input");
                if(source == VJ_AUDIO_SYNC_SOURCE_NONE ||
                   source == VJ_AUDIO_SYNC_SOURCE_PUSH ||
                   (source == VJ_AUDIO_SYNC_SOURCE_JACK && current_channels != channels))
                {
                    vj_audio_sync_set_source_jack(&settings->audio_sync, channels);
                }
            }
        }
        else if(source == VJ_AUDIO_SYNC_SOURCE_NONE ||
                source == VJ_AUDIO_SYNC_SOURCE_PUSH ||
                (source == VJ_AUDIO_SYNC_SOURCE_JACK && current_channels != channels))
        {
            vj_audio_sync_set_source_jack(&settings->audio_sync, channels);
        }
    }

    vj_audio_sync_enable(&settings->audio_sync);
}

#endif


int veejay_audio_beat_set_enabled(veejay_t *info, int enabled)
{
#ifdef HAVE_JACK
    video_playback_setup *settings;
    int rc;

    if(!info || !info->settings)
        return -1;

    settings = info->settings;
    enabled = enabled ? 1 : 0;

    if(veejay_audio_beat_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] request ignored; audio beat detector thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    if(!atomic_load_int(&settings->audio_beat.initialized))
        vj_audio_beat_init(&settings->audio_beat, 2);

    if(!settings->audio_beat.sync)
        vj_audio_beat_bind_sync(&settings->audio_beat, &settings->audio_sync);

    veejay_audio_beat_thread_startup(info);

    if(enabled)
        veejay_audio_beat_prepare_sync_source(info);


    if(enabled && !vj_audio_beat_auto_build_table()) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] auto-fx metadata table is not ready while enabling analysis; detector will run, but auto-FX mapping will have no targets");
    }

    if(!atomic_load_int(&settings->audio_beat.running))
    {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] Detector thread is not running; cannot %s analysis",
                   enabled ? "enable" : "disable");
        return -1;
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] set_enabled request=%d current_enabled=%d open=%d running=%d action=%d hits=%ld audio=%d",
               enabled,
               atomic_load_int(&settings->audio_beat.enabled),
               atomic_load_int(&settings->audio_beat.open),
               atomic_load_int(&settings->audio_beat.running),
               atomic_load_int(&settings->audio_beat.action_mode),
               __sync_add_and_fetch(&settings->audio_beat.hits, 0),
               info->audio);

    rc = enabled ? vj_audio_beat_enable(&settings->audio_beat)
                 : vj_audio_beat_disable(&settings->audio_beat);

    if(rc <= 0)
        return -1;

    return enabled;
#else
    (void)info;
    (void)enabled;
    return -1;
#endif
}


int veejay_audio_beat_toggle(veejay_t *info)
{
#ifdef HAVE_JACK
    video_playback_setup *settings;
    int enabled;
    int rc;

    if(!info || !info->settings)
        return -1;

    settings = info->settings;

    if(veejay_audio_beat_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] toggle ignored; audio beat detector thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, 2);

    if(!atomic_load_int(&settings->audio_beat.initialized))
        vj_audio_beat_init(&settings->audio_beat, 2);

    if(!settings->audio_beat.sync)
        vj_audio_beat_bind_sync(&settings->audio_beat, &settings->audio_sync);

    veejay_audio_beat_thread_startup(info);


    if(!atomic_load_int(&settings->audio_beat.running))
    {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] Detector thread is not running; cannot toggle analysis");
        return -1;
    }

    enabled = atomic_load_int(&settings->audio_beat.enabled) ? 1 : 0;

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] toggle request current_enabled=%d open=%d running=%d action=%d hits=%ld audio=%d",
               enabled,
               atomic_load_int(&settings->audio_beat.open),
               atomic_load_int(&settings->audio_beat.running),
               atomic_load_int(&settings->audio_beat.action_mode),
               __sync_add_and_fetch(&settings->audio_beat.hits, 0),
               info->audio);

    if(enabled)
    {
        rc = vj_audio_beat_disable(&settings->audio_beat);
        return (rc > 0) ? 0 : -1;
    }

    veejay_audio_beat_prepare_sync_source(info);

    if(!vj_audio_beat_auto_build_table()) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-BEAT] auto-fx metadata table is not ready while toggling analysis on; detector will run, but auto-FX mapping will have no targets");
    }

    rc = vj_audio_beat_enable(&settings->audio_beat);
    return (rc > 0) ? 1 : -1;
#else
    (void)info;
    return -1;
#endif
}


int veejay_audio_beat_push_config_ex(veejay_t *info,
                                      int hold_ms,
                                      int cooldown_ms,
                                      int threshold,
                                      int input_channels,
                                      int scratch_sensitivity,
                                      int source_loss_pause)
{
#ifdef HAVE_JACK
    video_playback_setup *settings;

    if(!info || !info->settings)
        return -1;

    settings = info->settings;

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] push_config request: hold_ms=%d cooldown_ms=%d threshold=%d input_channels=%d scratch=%d source_loss_pause=%d initialized=%d enabled=%d open=%d running=%d action=%d audio=%d",
               hold_ms,
               cooldown_ms,
               threshold,
               input_channels,
               scratch_sensitivity,
               source_loss_pause,
               atomic_load_int(&settings->audio_beat.initialized),
               atomic_load_int(&settings->audio_beat.enabled),
               atomic_load_int(&settings->audio_beat.open),
               atomic_load_int(&settings->audio_beat.running),
               atomic_load_int(&settings->audio_beat.action_mode),
               info->audio);

    if(!atomic_load_int(&settings->audio_sync.initialized))
        vj_audio_sync_init(&settings->audio_sync, input_channels > 0 ? input_channels : 2);

    if(!atomic_load_int(&settings->audio_beat.initialized))
        vj_audio_beat_init(&settings->audio_beat, input_channels > 0 ? input_channels : 2);

    if(!settings->audio_beat.sync)
        vj_audio_beat_bind_sync(&settings->audio_beat, &settings->audio_sync);


    if(hold_ms >= 0)
        vj_audio_beat_set_freeze_ms(&settings->audio_beat, hold_ms);

    if(cooldown_ms >= 0)
        vj_audio_beat_set_cooldown_ms(&settings->audio_beat, cooldown_ms);

    if(threshold >= 0)
        vj_audio_beat_set_threshold(&settings->audio_beat, threshold);

    if(input_channels > 0)
        vj_audio_beat_set_input_channels(&settings->audio_beat, input_channels);

    if(scratch_sensitivity >= 0)
        vj_audio_beat_set_scratch_sensitivity(&settings->audio_beat, scratch_sensitivity);

    if(source_loss_pause >= 0)
        vj_audio_beat_set_source_loss_pause(&settings->audio_beat, source_loss_pause);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] push_config applied: hold_ms=%d cooldown_ms=%d threshold=%d input_channels=%d scratch=%d source_loss_pause=%d action=%d enabled=%d open=%d running=%d reset_seq=%d",
               atomic_load_int(&settings->audio_beat.freeze_ms),
               atomic_load_int(&settings->audio_beat.cooldown_ms),
               atomic_load_int(&settings->audio_beat.threshold),
               atomic_load_int(&settings->audio_beat.input_channels_request),
               vj_audio_beat_get_scratch_sensitivity(&settings->audio_beat),
               vj_audio_beat_get_source_loss_pause(&settings->audio_beat),
               atomic_load_int(&settings->audio_beat.action_mode),
               atomic_load_int(&settings->audio_beat.enabled),
               atomic_load_int(&settings->audio_beat.open),
               atomic_load_int(&settings->audio_beat.running),
               atomic_load_int(&settings->audio_beat.reset_seq));

    return 1;
#else
    (void)info;
    (void)hold_ms;
    (void)cooldown_ms;
    (void)threshold;
    (void)input_channels;
    (void)scratch_sensitivity;
    (void)source_loss_pause;
    return -1;
#endif
}

int veejay_audio_beat_push_config(veejay_t *info,
                                  int hold_ms,
                                  int cooldown_ms,
                                  int threshold,
                                  int input_channels)
{
    return veejay_audio_beat_push_config_ex(info,
                                            hold_ms,
                                            cooldown_ms,
                                            threshold,
                                            input_channels,
                                            -1,
                                            -1);
}


int veejay_audio_beat_is_enabled(veejay_t *info)
{
#ifdef HAVE_JACK
    return vj_audio_beat_is_enabled(&info->settings->audio_beat);
#else
    (void)info;
    return 0;
#endif
}

int veejay_audio_beat_get_status(veejay_t *info, int *enabled, int *open, long *hits, int *level_q15, int *transient_q8)
{
#ifdef HAVE_JACK
    vj_audio_beat_shared_t *s;

    if(!info || !info->settings)
        return -1;

    s = &info->settings->audio_beat;

    if(!atomic_load_int(&s->initialized))
        vj_audio_beat_init(s, 2);

    if(enabled)
        *enabled = atomic_load_int(&s->enabled);
    if(open)
        *open = atomic_load_int(&s->open);
    if(hits)
        *hits = __sync_add_and_fetch(&s->hits, 0);
    if(level_q15)
        *level_q15 = atomic_load_int(&s->level_q15);
    if(transient_q8)
        *transient_q8 = atomic_load_int(&s->transient_q8);

    return 1;
#else
    (void)info;
    (void)enabled;
    (void)open;
    (void)hits;
    (void)level_q15;
    (void)transient_q8;
    return -1;
#endif
}

static void veejay_openmp_warmup(int len)
{
    int n_threads = vje_max_threads(len);
    omp_set_dynamic(0);
    omp_set_num_threads(n_threads);

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp single
        {
            veejay_msg(VEEJAY_MSG_INFO, "OpenMP warmed with %d threads", omp_get_num_threads());
        }
    }
}

static void *veejay_producer_thread_loop(void *ptr)
{
    veejay_t *info = (veejay_t*) ptr;
    video_playback_setup *settings = info->settings;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    veejay_openmp_warmup(info->effect_frame1->len);

    while (atomic_load_int(&settings->warmup_active) &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        usleep_accurate(100, settings);
    }

    if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
        pthread_exit(NULL);

    atomic_store_long_long(&settings->current_frame_num, -1);
    atomic_store_int(&settings->audio_mode, AUDIO_MODE_SILENCE_FILL);

	if(info->audio) {
#ifdef HAVE_JACK
    	veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] waiting for audio anchor");
    	while (atomic_load_double(&settings->audio_start_offset) <= 0.0 &&
               atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        	usleep_accurate(100, settings);
    	}

        if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
            pthread_exit(NULL);
#endif
	}

    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] waiting for first audio frame ready");
    while (atomic_load_int(&settings->first_audio_frame_ready) == 0 &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        usleep_accurate(100, settings);
    }

    if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
        pthread_exit(NULL);

    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] Wait for playback to reach anchor");

    veejay_set_speed(info, 1, 0);
	veejay_producer_initialize_playmode(info);

    atomic_store_int(&settings->audio_mode, AUDIO_MODE_CONTENT);
    {
        double start_anchor = atomic_load_double(&settings->audio_master_s);
        if(start_anchor <= 0.0)
            start_anchor = monotonic_now_s();
        atomic_store_double(&settings->audio_start_offset, start_anchor);
        atomic_store_double(&settings->fps_epoch_s, start_anchor);
        atomic_store_long_long(&settings->fps_epoch_frame, 0);
        atomic_store_long_long(&settings->master_frame_num, 0);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[PRODUCER] playback clock anchored to current audio master %.6fs",
                   start_anchor);
    }

#ifdef HAVE_JACK
    long long tempo_bridge_reverse_reanchor_last_ms = 0;
#endif

	while (atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {

		veejay_consume_events(info);

#ifdef HAVE_JACK
        {
            int beat_action = veejay_audio_beat_runtime_action(
                vj_audio_beat_get_action(&settings->audio_beat));

            if(veejay_audio_beat_action_is_breakbeat(beat_action))
                vj_audio_beat_consume(info, &settings->audio_beat);
            else
                vj_audio_beat_resume_if_due(info, &settings->audio_beat);
        }
#endif

		sample_watch_list();
		if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
        	break;

        info->stats.skipped_frames = 0;

        long long frame = atomic_load_long_long(&settings->master_frame_num);
        double spvf = settings->spvf;
        double skip_tolerance = 1.1 * spvf + 0.002;
        double audio_now = atomic_load_double(&settings->audio_master_s);
        double pts = vj_runtime_target_time_s(settings, frame);
        double diff = pts - audio_now;

#ifdef HAVE_JACK
        if(audio_now > 0.0) {
            double reanchor_threshold = 0.0;
            long long now_ms = monotonic_now_ms();

            if(vj_tempo_bridge_reverse_should_reanchor(
                   settings,
                   diff,
                   spvf,
                   skip_tolerance,
                   now_ms,
                   tempo_bridge_reverse_reanchor_last_ms,
                   &reanchor_threshold))
            {
                vj_runtime_reanchor_clock(info, frame,
                                          "tempo-bridge-reverse-producer-lead");

                tempo_bridge_reverse_reanchor_last_ms = now_ms;
                audio_now = atomic_load_double(&settings->audio_master_s);
                pts = vj_runtime_target_time_s(settings, frame);
                diff = pts - audio_now;
            }
        }
#endif

        if (atomic_load_int(&info->sync_correction) && diff < -skip_tolerance) {
            long long frames_to_skip = (long long)((-diff / spvf));
            
            if (frames_to_skip > 0) {
                 if(frame > 0)
                    atomic_add_fetch_old_long_long(&settings->audio_osd.prod_video_drop_frames,
                                                   frames_to_skip);
                 atomic_add_fetch_old_long_long(&settings->master_frame_num, frames_to_skip);
                 info->stats.skipped_frames = frames_to_skip;
                 info->stats.total_frames_skipped += frames_to_skip;
                 frame += frames_to_skip;
                 
                 pts = vj_runtime_target_time_s(settings, frame);
                 diff = pts - audio_now;
            }
        }

        if (diff > 0) {
            double sleep_s = (diff > spvf) ? spvf : diff;
            
            if (sleep_s > 0.0001) {
                usleep_accurate((long long)(sleep_s * 1e6), settings);
                
                audio_now = atomic_load_double(&settings->audio_master_s);
                diff = pts - audio_now;
            }
        }

#ifdef HAVE_JACK

        if (veejay_track_align_is_normal_transport(info) &&
            vj_audio_sync_is_enabled(&settings->audio_sync) &&
            atomic_load_int(&settings->audio_sync.mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN &&
            vj_audio_sync_get_target_mode(&settings->audio_sync) == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        {
            veejay_track_align_adjustment_t align_adj;
            const double media_fps = veejay_track_align_media_fps(info);

            veejay_track_align_get_adjustment(info, media_fps, &align_adj);
            if (align_adj.snap_frames != 0) {
                long long cur = atomic_load_long_long(&settings->current_frame_num);
                long long dst = cur + (long long)align_adj.snap_frames;

                veejay_set_frame(info, (long)dst);
                if(align_adj.snap_frames <= -2 || align_adj.snap_frames >= 2)
                    vj_audio_sync_track_align_reset_target(&settings->audio_sync);

                {
                    char local_offset_text[32];
                    if(align_adj.locked && align_adj.confidence >= 55)
                        snprintf(local_offset_text, sizeof(local_offset_text), "%+dms", align_adj.offset_ms);
                    else
                        snprintf(local_offset_text, sizeof(local_offset_text), "n/a");

                    veejay_msg(VEEJAY_MSG_INFO,
                               "[TRACK-ALIGN] %s video by %+d frames (side-channel, conf=%d%%, local offset=%s, current=%lld->%lld master_tick=%lld)",
                               (align_adj.snap_frames == 1 || align_adj.snap_frames == -1) ? "nudged" : "snapped",
                               align_adj.snap_frames,
                               align_adj.confidence,
                               local_offset_text,
                               cur,
                               atomic_load_long_long(&settings->current_frame_num),
                               frame);
                }
            }
        }
#endif

        VJFrame *vf = veejay_video_queue_reserve_buffer(info);
        if (!vf) {
            if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP) break;
            atomic_add_fetch_old_long_long(&settings->audio_osd.prod_queue_nulls, 1);
            usleep_accurate(1000, settings);
            continue;
        }

        vf->frame_num = frame;

		double t_before = monotonic_now_s();
        vj_perform_queue_video_frame(info, vf);
        
		double t_after = monotonic_now_s();
		info->stats.render_duration = (t_after - t_before);
        if(info->stats.render_duration > (0.85 * spvf))
            atomic_add_fetch_old_long_long(&settings->audio_osd.prod_slow_renders, 1);
		
        veejay_video_queue_post_frame(info, vf);
		long long master_frame = atomic_add_fetch_old_long_long(&settings->master_frame_num, 1);

        info->stats.current_frame = atomic_load_long_long(&settings->current_frame_num);
        info->stats.total_frames_produced = master_frame;
        info->stats.last_pts_s = pts;
        info->stats.delta_s = diff;
		info->stats.xruns = atomic_load_int(&settings->xruns);
    }

    pthread_exit(NULL);
    return NULL;
}

static void veejay_playback_close(veejay_t *info)
{
    int i;
    
    if(info->uc->is_server) {
		for(i = 0; i < 4; i ++ )
			if(info->vjs[i]) vj_server_shutdown(info->vjs[i]); 
    }

    if(info->osc) vj_osc_free(info->osc);

	if( info->shm ) {
		vj_shm_stop(info->shm);
		vj_shm_free(info->shm);
	}
	if( info->splitter ) {
		vj_split_free(info->splitter);
	}

    if( info->y4m ) {
	    vj_yuv_stream_stop_write( info->y4m );
	    vj_yuv4mpeg_free(info->y4m );
	    info->y4m = NULL;
	}

	if( info->vloopback ) {
		vj_vloopback_close( info->vloopback );
		info->vloopback = NULL;

	}
	
#ifdef HAVE_SDL
	if( info->sdl ) {
		vj_sdl_enable_screensaver(); 
        vj_sdl_free(info->sdl);
    }
    vj_sdl_quit();
#endif
#ifdef HAVE_FREETYPE
	vj_font_destroy( info->osd );
#endif
    vj_perform_free(info);

}

int vj_server_setup(veejay_t * info)
{
	if (info->uc->port == 0)
		info->uc->port = VJ_PORT;

	size_t recv_len   = (info->edit_list->video_width * info->edit_list->video_height * 3);
	size_t status_len = 4096 * 16;

	info->vjs[VEEJAY_PORT_CMD] = vj_server_alloc(info->uc->port, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_CMD]) {
		return 0;
	}

	info->vjs[VEEJAY_PORT_STA] = vj_server_alloc(info->uc->port, NULL, V_STATUS, status_len);
	if(!info->vjs[VEEJAY_PORT_STA])
	{
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		return 0;
	}
	//@ second VIMS control port
	info->vjs[VEEJAY_PORT_DAT] = vj_server_alloc(info->uc->port + 5, NULL, V_CMD, recv_len);
	if(!info->vjs[VEEJAY_PORT_DAT]) {
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		return 0;
	}

	info->vjs[VEEJAY_PORT_MAT] = NULL;
	if( info->settings->use_vims_mcast ) 
	{
		info->vjs[VEEJAY_PORT_MAT] =
			vj_server_alloc(info->uc->port, info->settings->vims_group_name, V_CMD, recv_len );
		if(!info->vjs[VEEJAY_PORT_MAT])
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize multicast sender");
			return 0;
		}
	}
	if(info->settings->use_mcast)
	{
		GoMultiCast( info->settings->group_name );
	}

	info->server_origin = vj_server_find_best_ip();
	veejay_msg(VEEJAY_MSG_DEBUG, "Listening to VIMS on %s", info->server_origin);

	info->osc = (void*) vj_osc_allocate(info->uc->port+6);

	if(!info->osc) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,  "Unable to start OSC server at port %d", info->uc->port + 6 );
		vj_server_shutdown(info->vjs[VEEJAY_PORT_CMD]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_STA]);
		vj_server_shutdown(info->vjs[VEEJAY_PORT_DAT]);
		return 0;
	}

	if( info->settings->use_mcast )
		veejay_msg(VEEJAY_MSG_INFO, "UDP multicast OSC channel ready at port %d (group '%s')",
			info->uc->port + 6, info->settings->group_name );
	else
		veejay_msg(VEEJAY_MSG_INFO, "UDP unicast OSC channel ready at port %d",
			info->uc->port + 6 );

	if(vj_osc_setup_addr_space(info->osc) == 0)
		veejay_msg(VEEJAY_MSG_DEBUG, "Initialized OSC (http://www.cnmat.berkeley.edu/OpenSoundControl/)");

   	info->uc->is_server = 1;

	return 1;
}

int	prepare_cache_line(int perc, int n_slots)
{
	int total = 0; 
	char line[128];
	FILE *file = fopen( "/proc/meminfo","r");
	if(!file)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant open proc, memory size cannot be determined");
		veejay_msg(VEEJAY_MSG_ERROR, "Cache disabled");
		return 1;
	}

	if(fgets(line, 128, file ) == NULL ) {
		fclose(file);
		return 1;
	}

	sscanf( line, "%*s %i", &total );
	fclose(file);

	double p = (double) perc * 0.01f;
	long max_memory = (p * total);
	long mmap_memory = (long) (0.005f * (float) total);

	char *user_defined_mmap = getenv( "VEEJAY_MMAP_PER_FILE" );
	if( user_defined_mmap ) {
		max_memory = atoi( user_defined_mmap ) * 1024;
		veejay_msg(VEEJAY_MSG_DEBUG, "User-defined %2.2f Kb mmap size per AVI file",(float) (max_memory / 1024.0f));
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "You can define mmap size per AVI file with VEEJAY_MMAP_PER_FILE=Kb");
	}

	max_memory -= mmap_memory;

	if( perc > 0 && max_memory <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Please enter a larger value for -m");
		return 1;
	}

	if( n_slots <= 0)
		n_slots = 1;

	return 1;
}

static int smp_check(void)
{
	return get_nprocs();
}

veejay_t *veejay_malloc()
{
    
    veejay_t *info = (veejay_t *) vj_calloc(sizeof(veejay_t));
    if (!info)
		return NULL;

    info->settings = (video_playback_setup *) vj_calloc(sizeof(video_playback_setup));
    if (!(info->settings)) 
		return NULL;

    info->recording = (video_recording_setup *) vj_calloc(sizeof(video_recording_setup));
    if (!(info->recording)) {
        return NULL;
    }
	info->settings->fxdepth = 1; //@ default to on (VEEJAY_CLASSIC env turns it off)
	info->settings->color_vibrance = 98;
	
	veejay_memset( &(info->settings->action_scheduler), 0, sizeof(vj_schedule_t));
    veejay_memset( &(info->settings->viewport ), 0, sizeof(VJRectangle)); 

    info->status_what = (char*) vj_calloc(sizeof(char) * VJ_STATUS_BUF_SIZE );

	info->uc = (user_control *) vj_calloc(sizeof(user_control));
	info->uc->drawsize = 4;

	info->global_chain = (global_chain_t*) vj_calloc(sizeof(global_chain_t));
	if(!info->global_chain)
		return NULL;

	for( int i = 0; i < SAMPLE_MAX_EFFECTS ; i ++ ) {
		info->global_chain->fx_chain[i] = (sample_eff_chain*) vj_calloc(sizeof(sample_eff_chain));
		if(!info->global_chain->fx_chain[i])
			return NULL;
	}

	if (!(info->uc)) 
		return NULL;

    info->effect_frame_info = (VJFrameInfo*) vj_calloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info)
		return NULL;
    info->effect_frame_info2 = (VJFrameInfo*) vj_calloc(sizeof(VJFrameInfo));
	if(!info->effect_frame_info2)
		return NULL;

    info->effect_info = (vjp_kf*) vj_calloc(sizeof(vjp_kf));
	if(!info->effect_info) 
		return NULL;   
    
    info->effect_info2 = (vjp_kf*) vj_calloc(sizeof(vjp_kf));
	if(!info->effect_info2) 
		return NULL;   

	info->dummy = (dummy_t*) vj_calloc(sizeof(dummy_t));
    if(!info->dummy)
		return NULL;
	
	info->seq = (sequencer_t*) vj_calloc(sizeof( sequencer_t) );
    if(info->seq)
        info->seq->selected_bank_mask = 1;
    if(info->settings)
        info->settings->transition.seq_bank = -1;
    info->audio = AUDIO_PLAY;
    info->continuous = 1;
    info->sync_correction = 1;
    info->sync_ins_frames = 1;
    info->sync_skip_frames = 0;
    info->double_factor = 1;
    info->bezerk = 0;
    info->nstreams = 1;
    info->stream_outformat = -1;
    info->rlinks = (int*) vj_malloc(sizeof(int) * VJ_MAX_CONNECTIONS );
    info->rmodes = (int*) vj_malloc(sizeof(int) * VJ_MAX_CONNECTIONS );
	info->splitted_screens = (int*) vj_malloc( sizeof(int) * VJ_MAX_CONNECTIONS );
   	info->settings->currently_processed_entry = -1;
    info->settings->first_frame = 1;
    info->settings->state = LAVPLAY_STATE_PLAYING;
    info->settings->composite = 1;
    info->uc->playback_mode = VJ_PLAYBACK_MODE_PLAIN;
    info->uc->use_timer = 2;
    info->uc->sample_key = 1;
    info->uc->direction = 1;	/* pause */
    info->uc->sample_start = 0;
    info->uc->sample_end = 0;
	info->uc->ram_chain = 1; /* enable, keep FX chain buffers in memory (reduces the number of malloc/free of frame buffers) */
	info->net = 1;
	info->status_line = (char*) vj_calloc(sizeof(char) * VJ_STATUS_BUF_SIZE );
	info->status_line_len = 0;
    for( int i =0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
		info->rlinks[i] = -1;
		info->rmodes[i] = -1;
		info->splitted_screens[i] = -1;
	}

    veejay_memset(info->action_file[0],0,sizeof(info->action_file[0])); 
    veejay_memset(info->action_file[1],0,sizeof(info->action_file[1])); 
	veejay_memset( info->dummy, 0, sizeof(dummy_t));
	veejay_memset(&(info->settings->sws_templ), 0, sizeof(sws_template));

	info->pixel_format = FMT_422F; //@default 
	info->settings->ncpu = smp_check();

    omp_set_num_threads( info->settings->ncpu );

	int status = 0;
	int acj    = 0;
	char *interpolate_chroma = getenv("VEEJAY_INTERPOLATE_CHROMA");
	if( interpolate_chroma ) {
		sscanf( interpolate_chroma, "%d", &status );
		}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_INTERPOLATE_CHROMA=[0|1] not set");
	}

	char *auto_ccir_jpeg = getenv("VEEJAY_AUTO_SCALE_PIXELS");
	if( auto_ccir_jpeg ) {
		sscanf( auto_ccir_jpeg, "%d", &acj );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_AUTO_SCALE_PIXELS=[0|1] not set");
	}

	char *key_repeat_interval = getenv("VEEJAY_SDL_KEY_REPEAT_INTERVAL");
	char *key_repeat_delay    = getenv("VEEJAY_SDL_KEY_REPEAT_DELAY");
	if(key_repeat_interval) {
		sscanf( key_repeat_interval, "%d", &(info->settings->repeat_interval));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_INTERVAL=[Num] not set");
	}
	if( key_repeat_delay) {
		sscanf( key_repeat_delay, "%d", &(info->settings->repeat_delay));
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SDL_KEY_REPEAT_DELAY=[Num] not set");
	}

	char *best_performance = getenv( "VEEJAY_PERFORMANCE");
	int default_zoomer = 1;
	if(!best_performance) {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_PERFORMANCE=[fastest|quality] not set");
	}

	char *sdlfs = getenv("VEEJAY_FULLSCREEN");
	if( sdlfs ) {
		int val = 0;
		if( sscanf( sdlfs, "%d", &val ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Playing in %s mode",
				(val== 1 ? "fullscreen" : "windowed" ) );
			info->settings->full_screen = val;
		}
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_FULLSCREEN=[0|1] not set");
	}


	char *audiostats = getenv("VEEJAY_AUDIO_STATS");
	if(audiostats) {
		int val = 0;
		if( sscanf( audiostats, "%d", &val ) ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Outputing audio synchronization statistics is %s", (val == 0 ? "disabled" : "enabled" ));
			info->settings->audiostats = val;
		}
	}

	if( best_performance) {
		if (strncasecmp( best_performance, "quality", 7 ) == 0 ) {
			default_zoomer = 2;
			status = 1;
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum quality");
		}
		else if( strncasecmp( best_performance, "fastest", 7) == 0 ) {
			veejay_msg(VEEJAY_MSG_WARNING, "Performance set to maximum speed");
			if( acj ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_AUTO_SCALE_PIXELS");
				acj = 0;
			}
			if( status ) {
				veejay_msg(VEEJAY_MSG_WARNING, "\tdisabling flag VEEJAY_INTERPOLATE_CHROMA");
				status = 0;
			}
			default_zoomer = 1;
		}
	}

	yuv_init_lib( status ,acj, default_zoomer);

	if(!vj_avcodec_init( info->pixel_format, info->verbose))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize encoders");
		return 0;
	}

    vj_osc_set_veejay_t(info);
    vj_tag_set_veejay_t(info);

	veejay_instance_ = info;
	
    return info;
}

int veejay_main(veejay_t *info)
{
    video_playback_setup *settings = (video_playback_setup *)info->settings;
    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;
    cpu_set_t cpuset;
    int attr_inited = 0;
    int err;

    if (vj_task_get_num_cpus() > 1) {
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);

        err = pthread_attr_init(&attr);
        if (err != 0) {
            veejay_msg(err == ENOMEM ? VEEJAY_MSG_ERROR : VEEJAY_MSG_WARNING,
                       "[DISPLAY] Unable to initialize thread attributes, code %d", err);
            if (err == ENOMEM)
                return 0;
        } else {
            attr_inited = 1;
            attrp = &attr;

            if (pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset) != 0) {
                veejay_msg(VEEJAY_MSG_WARNING, "[DISPLAY] Unable to pin display/renderer thread to CPU #1.");
            } else {
                veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Thread affinity set to CPU #1.");
            }
        }
    }

	if(info->load_action_file)
    {
        if(veejay_load_action_file(info, info->action_file[0] ))
        {
            veejay_msg(VEEJAY_MSG_INFO, "Loaded configuration file %s", info->action_file[0] );
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "File %s is not an action file", info->action_file[0]);
        }
    }

    if(info->load_sample_file ) {
		int initial_sample = info->uc->sample_id;
		int initial_mode = info->uc->playback_mode;
        if(sample_open_and_watch( info->action_file[1],info->composite,info->seq,info->font,info->edit_list,
             &initial_sample, &initial_mode))
        {
			int pw,ph;
            veejay_msg(VEEJAY_MSG_INFO, "Loaded samplelist %s", info->action_file[1]);
			veejay_msg(VEEJAY_MSG_INFO, "Watching for changes (auto-reload enabled)");
			vj_suggest_fit_preview_size( info->video_output_width, info->video_output_height, &pw,&ph);
			veejay_msg(VEEJAY_MSG_INFO, "You can start another veejay for offline editing:");
			veejay_msg(VEEJAY_MSG_INFO, "  veejay -w %d -h %d -l %s [options]", pw, ph, info->action_file[1]);
			info->uc->sample_id = initial_sample;
			info->uc->playback_mode = initial_mode;
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "Failed to load sample list %s", info->action_file[1]);
        }
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Starting Video Renderer Thread (Display)");
    err = pthread_create(&settings->renderer_thread, attrp,
                         veejay_display_renderer_thread, (void *)info);
    if (err != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to create Renderer thread, code %d", err);
        if (attr_inited)
            pthread_attr_destroy(&attr);
        return 0;
    }

    if (attr_inited)
        pthread_attr_destroy(&attr);
    veejay_msg(VEEJAY_MSG_DEBUG, "[PRODUCER] Starting Video Producer Thread (Decode/Effects)");
    if (pthread_create(&settings->producer_thread, NULL,
                       veejay_producer_thread_loop, (void *)info) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to create Producer thread");
        veejay_change_state(info, LAVPLAY_STATE_STOP);
        pthread_join(settings->renderer_thread, NULL);
        settings->renderer_thread = 0;
        return 0;
    }


    veejay_producer_thread_audio_startup(info);


    while (atomic_load_int(&settings->first_audio_frame_ready) == 0 &&
           atomic_load_int(&settings->state) != LAVPLAY_STATE_STOP) {
        usleep_accurate(5000, settings);
    }

    if (atomic_load_int(&settings->state) == LAVPLAY_STATE_STOP)
        return 0;

    Welcome(info);

    return 1;
}

static void	veejay_reset_el_buffer( veejay_t *info )
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    if (settings->save_list)
	free(settings->save_list);

    settings->save_list = NULL;
    settings->save_list_len = 0;
}

static int veejay_edit_range_valid(editlist *el, long start, long end, const char *what)
{
    if(!el || el->is_empty || !el->frame_list || el->video_frames <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to %s", what ? what : "edit");
        return 0;
    }

    if(start < 0 || end < start || (uint64_t)end >= (uint64_t)el->video_frames) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Incorrect parameters for %s frames: %ld - %ld, range is 0 - %llu",
                   what ? what : "editing",
                   start,
                   end,
                   (unsigned long long)(el->video_frames > 0 ? el->video_frames - 1 : 0));
        return 0;
    }

    return 1;
}

int veejay_edit_copy(veejay_t * info, editlist *el, long start, long end)
{
    if(!veejay_edit_range_valid(el, start, end, "copy"))
        return 0;

    video_playback_setup *settings =
	(video_playback_setup *) info->settings;

    uint64_t k, i;
    uint64_t n1 = (uint64_t) start;
    uint64_t n2 = (uint64_t) end;
    uint64_t copy_len = n2 - n1 + 1;

    if(copy_len > (uint64_t)SIZE_MAX / sizeof(uint64_t)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Copy range is too large");
        return 0;
    }

    uint64_t *new_save_list =
		(uint64_t *) vj_calloc((size_t)(copy_len * sizeof(uint64_t)));

	if (!new_save_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
	}

    k = 0;

#pragma omp simd
    for (i = n1; i <= n2; i++) {
	new_save_list[k] = el->frame_list[i];
        k++;
    }

    if (settings->save_list)
		free(settings->save_list);

    settings->save_list = new_save_list;
    settings->save_list_len = copy_len;

    veejay_msg(VEEJAY_MSG_DEBUG,
               "Copied frames %ld - %ld to buffer (of size %llu)",
               start,
               end,
               (unsigned long long)k );

    return 1;
}
editlist *veejay_edit_copy_to_new(veejay_t * info, editlist *el, long start, long end)
{
	long len = end - start;
  
	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EDL to copy");
		return 0;
	}

	if( end > el->total_frames)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample end is outside of editlist");
		return NULL;
	}

    if( start < 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Sample start cannot be a negative value");
        return NULL;
    }

	if(len < 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample too short");
		return NULL;
	}

    veejay_msg(VEEJAY_MSG_DEBUG, "New EDL %ld - %ld (%ld frames)", start,end, len);
	/* Copy edl */
	editlist *new_el = vj_el_soft_clone_range( el,start,end );
	if(!new_el)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot soft clone EDL");
		return NULL;
	}

	return new_el;
}

int veejay_edit_delete(veejay_t *info, editlist *el, long start, long end)
{
    if(!veejay_edit_range_valid(el, start, end, "delete"))
        return 0;

    video_playback_setup *settings = (video_playback_setup *)info->settings;

    if (info->dummy->active) {
        veejay_msg(VEEJAY_MSG_ERROR, "Playing dummy video");
        return 0;
    }

    uint64_t i;
    uint64_t n1 = (uint64_t)start;
    uint64_t n2 = (uint64_t)end;
    uint64_t removed = n2 - n1 + 1;
    uint64_t old_count = (uint64_t)el->video_frames;
    uint64_t new_count = old_count - removed;

    for (i = n2 + 1; i < old_count; i++) {
        el->frame_list[i - removed] = el->frame_list[i];
	}

	long long min_fn = atomic_load_long_long(&settings->min_frame_num);
	long long max_fn = atomic_load_long_long(&settings->max_frame_num);
    long long cur = atomic_load_long_long(&settings->current_frame_num);
    long long s_n1 = (long long)n1;
    long long s_n2 = (long long)n2;
    long long s_removed = (long long)removed;

    if (s_n1 - 1 < min_fn) {
        if (s_n2 < min_fn)
            min_fn -= s_removed;
        else
            min_fn = s_n1;
    }

    if (s_n1 - 1 < max_fn) {
        if (s_n2 <= max_fn)
            max_fn -= s_removed;
        else
            max_fn = s_n1 - 1;
    }

    if (s_n1 <= cur) {
        if (cur <= s_n2)
            cur = s_n1;
        else
            cur -= s_removed;
    }

    el->video_frames = (long)new_count;
    el->total_frames = new_count > 0 ? (new_count - 1) : 0;
    el->is_empty = new_count == 0 ? 1 : 0;

    long long new_max = new_count > 0 ? (long long)(new_count - 1) : 0;

    if(min_fn < 0) {
        min_fn = 0;
    }
    
    if(min_fn > new_max) {
        min_fn = new_max;
    }

    if(max_fn < 0) {
        max_fn = 0;
    }
    if(max_fn > new_max) {
        max_fn = new_max;
    }

    if(cur < min_fn) {
        cur = min_fn;
    }
    if(cur > max_fn) {
        cur = max_fn;
    }

	atomic_store_long_long(&settings->min_frame_num, min_fn);
	atomic_store_long_long(&settings->max_frame_num, max_fn);
    atomic_store_long_long(&settings->current_frame_num, cur);

    return 1;
}

int veejay_edit_cut(veejay_t * info, editlist *el, long start, long end)
{
	if( el->is_empty )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Nothing to cut in EDL");
		return 0;
	}
    if (!veejay_edit_copy(info, el,start, end))
	return 0;
    if (!veejay_edit_delete(info, el,start, end))
	return 0;

    return 1;
}

int veejay_edit_paste(veejay_t * info, editlist *el, long destination)
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t i;

	if (!settings->save_list_len || !settings->save_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, 
			    "No frames in the buffer to paste");
		return 0;
	 }

	if(!el) {
		veejay_msg(VEEJAY_MSG_ERROR, "No Edit List to paste into");
		return 0;
	}

	if(el->is_empty)
	{
		destination = 0;
	}
	else
	{
		if (destination < 0 || destination > el->total_frames)
		{
			if(destination < 0)
				veejay_msg(VEEJAY_MSG_ERROR, 
					    "Destination cannot be negative");
			if(destination > el->total_frames)
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot paste beyond Edit List");
			return 0;
    		}
	}

    uint64_t old_count = el->is_empty ? 0 : (uint64_t)el->video_frames;
    uint64_t add_count = settings->save_list_len;
    uint64_t new_count = old_count + add_count;

    if(new_count < old_count ||
       new_count > (uint64_t)LONG_MAX ||
       new_count > (uint64_t)SIZE_MAX / sizeof(uint64_t))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Paste would make the Edit List too large");
        return 0;
    }

    uint64_t *new_frame_list = (uint64_t*)realloc(el->frame_list,
        (size_t)(new_count * sizeof(uint64_t)));

	if (!new_frame_list)
	{
		veejay_change_state_save(info, LAVPLAY_STATE_STOP);
		return 0;
   	}

    el->frame_list = new_frame_list;

    uint64_t dst = (uint64_t)destination;
    for (i = old_count; i > dst; i--)
		el->frame_list[(i - 1) + add_count] = el->frame_list[i - 1];

	for (i = 0; i < add_count; i++)
		el->frame_list[dst + i] = settings->save_list[i];

    long long min_fn = atomic_load_long_long(&settings->min_frame_num);
    if( destination < min_fn ) {
        atomic_store_long_long(&settings->min_frame_num, destination);
    }
	el->video_frames = (long)new_count;
	el->total_frames = new_count - 1;
	el->is_empty = 0;

    atomic_store_long_long(&settings->max_frame_num, (long long)el->total_frames);
    atomic_store_long_long(&settings->current_frame_num, destination);

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Pasted %llu frames from buffer into position %ld in movie",
			(unsigned long long)settings->save_list_len, destination );
	return 1;
}

int veejay_edit_move(veejay_t * info,editlist *el, long start, long end,
		      long destination)
{
    long dest_real;
    if( el->is_empty )
		return 0;
	
    if (destination > el->total_frames || destination < 0
		|| start < 0 || end < 0 || start >= el->total_frames
		|| end > el->total_frames || end < start)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid parameters for moving video from %ld - %ld to position %ld",
			start,end,destination);
		veejay_msg(VEEJAY_MSG_ERROR, "Range is 0 - %ld", el->total_frames);   
		return 0;
    }

    if (destination < start)
		dest_real = destination;
    else if (destination > end)
		dest_real = destination - (end - start + 1);
    else
		dest_real = start;

    if (!veejay_edit_cut(info, el, start, end))
		return 0;

    if (!veejay_edit_paste(info, el,dest_real))
		return 0;


	return 1;
}

int veejay_edit_addmovie_sample(veejay_t * info, char *movie, int id )
{
	char *files[1];

	files[0] = strdup(movie);
	sample_info *sample = NULL;
	editlist *sample_edl = NULL;
	// if sample exists, get it for update
	if(sample_exists(id) )
		sample = sample_get(id);

	// if sample exists, it could have a edit list */
	if( sample )
	{
		if( !sample_usable_edl( id ) )
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d is a picture", id );
			if(files[0])
				free(files[0]);
			return -1;
		}

		sample_edl = sample_get_editlist( id );
	}

	// if both, append it to sample's edit list
	if(sample_edl && sample)
	{
		int real_start = 0;
		int real_end = 0;

		if(sample_get_el_position(id, &real_start, &real_end) != 1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to read real sample range for sample %d", id);
			if(files[0])
				free(files[0]);
			return -1;
		}

		veejay_msg(VEEJAY_MSG_DEBUG, "Adding video file to existing sample %d", id );
		long endpos = real_end;
		
		int res = veejay_edit_addmovie( info, sample_edl, movie, endpos );

		if(files[0]) 
			free(files[0]);

		if( res > 0 ) {
			sample_set_endframe( id, sample_edl->total_frames );
			veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d new ending position is %ld",id, sample_edl->video_frames );
			return id;
		}
		return -1;
	}

	// create initial edit list for sample (is currently playing)
	if(!sample_edl) 
		sample_edl = vj_el_init_with_args( files,1,info->preserve_pathnames,info->auto_deinterlace,0,
				info->edit_list->video_norm , info->pixel_format, info->video_output_width, info->video_output_height);
	// if that fails, bye
	if(!sample_edl)
	{
		veejay_msg(0, "Error while creating EDL");
		if(files[0]) free(files[0]);

		return -1;
	}

	vj_cache_print_status();

	// check audio properties 
	if( info->edit_list->has_audio && info->audio == AUDIO_PLAY ) {
		if( sample_edl->audio_rate != info->edit_list->audio_rate ||
		    sample_edl->audio_chans != info->edit_list->audio_chans ||
		    sample_edl->audio_bits != info->edit_list->audio_bits ||
		    sample_edl->audio_bps != info->edit_list->audio_bps ) {

			veejay_msg(VEEJAY_MSG_WARNING, "Silencing this sample (mismatching audio properties)" );
			sample_edl->has_audio = 0;
			sample_edl->audio_chans = 0;
			sample_edl->audio_rate = 0;
			sample_edl->audio_bps = 0;
			sample_edl->audio_bits = 0;
		}
	}


	// the sample is not there yet,create it
	if(!sample)
	{
		sample = sample_skeleton_new( 0, sample_edl->total_frames );
		if(sample)
		{
			sample->edit_list = sample_edl;
			sample_store(sample,0);
		//	sample->speed = info->settings->current_playback_speed;
			veejay_msg(VEEJAY_MSG_INFO,"Created new sample %d from file %s",sample->sample_id,	files[0]);
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample from file '%s'", files[0]);
			if(files[0])
				free(files[0]);
			return -1;
		}
	}

  char* sample_name;
  if(!vj_get_sample_display_name(&sample_name, files[0])){
    veejay_msg(VEEJAY_MSG_ERROR,"Failed to create new sample filename for '%s'", files[0]);
  } else {
    sample_set_description(sample->sample_id, sample_name);
    free (sample_name);
  }

	// free temporary values
   	if(files[0]) free(files[0]);

    return sample->sample_id;
}

int veejay_edit_addmovie(veejay_t * info, editlist *el, char *movie, long start )
{
	video_playback_setup *settings =
		(video_playback_setup *) info->settings;
	uint64_t i,n;
	uint64_t c = el->video_frames;
	if( el->is_empty )
		c -= 2;

	n = open_video_file(movie, el, info->preserve_pathnames, info->auto_deinterlace,1,
		info->edit_list->video_norm, get_ffmpeg_pixfmt(info->pixel_format), info->video_output_width, info->video_output_height );

	if (n < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error adding file '%s' to EDL", movie );
		return 0;
	}

	el->frame_list = (uint64_t *) realloc(el->frame_list, (c + el->num_frames[n])*sizeof(uint64_t));
	if (el->frame_list==NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
		vj_el_free(el);
		return 0;
	}

	for (i = 0; i < el->num_frames[n]; i++)
	{
		el->frame_list[c ++] = EL_ENTRY(n, i);
	}
 
	el->video_frames = c;
    el->total_frames = el->video_frames - 1;
	atomic_store_long_long(&settings->min_frame_num, 0);
	atomic_store_long_long(&settings->max_frame_num, (long long) el->total_frames);

	return 1;
}

int veejay_toggle_audio(veejay_t * info, int audio)
{
    video_playback_setup *settings =
	(video_playback_setup *) info->settings;
    editlist *el = info->current_edit_list;
    int old_mute;
    int new_mute;
    int dir;

    (void) audio;

    (void)el;

    old_mute = atomic_load_int(&settings->audio_mute) ? 1 : 0;
    new_mute = old_mute ? 0 : 1;

    atomic_store_int(&settings->audio_mute, new_mute);

    dir = playback_dir(settings->current_playback_speed);
    atomic_store_int(&settings->audio_slice, 0);
    atomic_store_int(&settings->audio_slice_len, settings->sfd > 0 ? settings->sfd : 1);
    settings->audio_last_stretched_samples = 0;

#ifdef HAVE_JACK

    vj_perform_audio_source_transition_guard_ex(2,
                                                new_mute ? "mute-on" : "mute-off",
                                                atomic_load_int(&settings->record_audio_source),
                                                atomic_load_int(&settings->record_audio_source),
                                                atomic_load_int(&settings->audio_sync.mode),
                                                new_mute);

    if(new_mute)
        vj_jack_set_input_passthrough(0);
#endif

    vj_perform_initiate_edge_change(
        info,
        new_mute ? AUDIO_EDGE_SILENCE : AUDIO_EDGE_JUMP,
        new_mute ? dir : 0,
        new_mute ? 0 : dir
    );

    veejay_msg(VEEJAY_MSG_DEBUG,
               "Audio playback was %s; edge=%s",
               new_mute ? "muted" : "unmuted",
               new_mute ? "silence" : "jump");
 
    return 1;
}

#ifdef HAVE_JACK
int veejay_audio_sync_set_external_jack(veejay_t *info, int mode, int channels)
{
    video_playback_setup *settings;

    if(!info || !info->settings)
        return -1;

    settings = info->settings;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC] external JACK request ignored; audio sync/control thread is disabled");
        return -1;
    }

    veejay_audio_sync_thread_startup(info);
    vj_audio_sync_set_mode(&settings->audio_sync, mode);
    if(mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
       mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        vj_audio_sync_set_target_mode(&settings->audio_sync,
                                      VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP);
    vj_audio_sync_set_source_jack(&settings->audio_sync, channels);

    return vj_audio_sync_enable(&settings->audio_sync) > 0 ? 1 : -1;
}
static int veejay_audio_sync_wav_plain_limit_ms(veejay_t *info)
{
    editlist *el;
    double fps;
    long long frames;

    if(!info || !info->uc)
        return 0;

    if(info->uc->playback_mode != VJ_PLAYBACK_MODE_PLAIN)
        return 0;

    el = info->edit_list;
    if(!el)
        return 0;

    fps = el->video_fps;
    if(fps <= 0.0 && info->settings && info->settings->output_fps > 0.0f)
        fps = (double)info->settings->output_fps;
    if(fps <= 0.0)
        fps = 25.0;

    frames = (long long)el->video_frames;
    if(frames <= 0)
        frames = (long long)el->total_frames + 1LL;
    if(frames <= 0)
        return 0;

    return (int)(((double)frames * 1000.0 / fps) + 0.5);
}

int veejay_audio_sync_set_external_wav(veejay_t *info, const char *path, int loop, int mode)
{
    video_playback_setup *settings;
    struct stat st;
    int limit_ms;
    int enable_rc;

    if(!info || !info->settings) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] rejected request: missing veejay/settings");
        return -1;
    }

    if(!path || !path[0]) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] rejected request: empty path");
        return -1;
    }

    if(mode < VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL ||
       mode > VJ_AUDIO_SYNC_MODE_MAX)
    {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] invalid mode=%d, forcing clean monitor(%d)",
                   mode,
                   VJ_AUDIO_SYNC_MODE_MONITOR);
        mode = VJ_AUDIO_SYNC_MODE_MONITOR;
    }

    if(!vj_audio_sync_mode_supports_wav_master(mode)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] mode=%d does not support a WAV master, forcing clean monitor(%d)",
                   mode,
                   VJ_AUDIO_SYNC_MODE_MONITOR);
        mode = VJ_AUDIO_SYNC_MODE_MONITOR;
    }

    loop = loop ? 1 : 0;

    if(stat(path, &st) != 0) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] path validation failed: '%s': %s",
                   path,
                   strerror(errno));
        return -1;
    }

    if(!S_ISREG(st.st_mode)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] path validation failed: '%s' is not a regular file",
                   path);
        return -1;
    }

    if(access(path, R_OK) != 0) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] path validation failed: '%s' is not readable: %s",
                   path,
                   strerror(errno));
        return -1;
    }

    if(st.st_size < 44) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] path validation failed: '%s' is too small to be a PCM WAV (%lld bytes)",
                   path,
                   (long long)st.st_size);
        return -1;
    }

    settings = info->settings;

    if(veejay_audio_sync_thread_disabled(settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] request ignored; audio sync/control thread is disabled");
        return -1;
    }

    limit_ms = loop ? 0 : veejay_audio_sync_wav_plain_limit_ms(info);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-SYNC][WAV] request mode=%d loop=%d limit=%dms size=%lld path='%s'",
               mode,
               loop,
               limit_ms,
               (long long)st.st_size,
               path);

    veejay_audio_sync_thread_startup(info);

    vj_audio_sync_set_mode(&settings->audio_sync, mode);

    if(mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
       mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        vj_audio_sync_set_target_mode(&settings->audio_sync,
                                      VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP);

    if(!vj_audio_sync_set_source_wav_limited(&settings->audio_sync, path, loop, limit_ms)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] failed to select WAV source mode=%d loop=%d limit=%dms path='%s'",
                   mode,
                   loop,
                   limit_ms,
                   path);
        return -1;
    }

    enable_rc = vj_audio_sync_enable(&settings->audio_sync);
    if(enable_rc <= 0) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC][WAV] failed to enable sync engine mode=%d loop=%d path='%s'",
                   mode,
                   loop,
                   path);
        return -1;
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-SYNC][WAV] armed mode=%d loop=%d limit=%dms path='%s'",
               mode,
               loop,
               limit_ms,
               path);

    return 1;
}

int veejay_audio_sync_set_target_clock(veejay_t *info, int bpm_x10, int phase_pct, int confidence_pct)
{
    if(!info || !info->settings)
        return -1;

    if(veejay_audio_sync_thread_disabled(info->settings)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO-SYNC] target clock request ignored; audio sync/control thread is disabled");
        return -1;
    }

    if(!atomic_load_int(&info->settings->audio_sync.initialized))
        vj_audio_sync_init(&info->settings->audio_sync, 2);

    if(bpm_x10 < 0) {
        vj_audio_sync_set_target_mode(&info->settings->audio_sync,
                                      VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP);
        return 2;
    }

    if(phase_pct < 0) phase_pct = 0;
    if(phase_pct > 100) phase_pct = 100;
    if(confidence_pct < 0) confidence_pct = 0;
    if(confidence_pct > 100) confidence_pct = 100;

    vj_audio_sync_set_target_clock(
        &info->settings->audio_sync,
        (float)bpm_x10 * 0.1f,
        (float)phase_pct * 0.01f,
        (float)confidence_pct * 0.01f
    );

    return 1;
}

#endif

int veejay_set_record_audio_source(veejay_t *info, int source)
{
    video_playback_setup *settings;
    int old;

    if(!info || !info->settings)
        return -1;

    if(source < VJ_RECORD_AUDIO_SOURCE_AUTO)
        source = VJ_RECORD_AUDIO_SOURCE_AUTO;
    else if(source > VJ_RECORD_AUDIO_SOURCE_SILENCE)
        source = VJ_RECORD_AUDIO_SOURCE_SILENCE;

    settings = info->settings;
    old = atomic_load_int(&settings->record_audio_source);
    atomic_store_int(&settings->record_audio_source, source);

#ifdef HAVE_JACK
    vj_perform_record_audio_source_reset(info);
#endif

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-REC] recording audio policy changed %d -> %d; recorder taps reset",
               old,
               source);

    return source;
}


int veejay_save_all(veejay_t * info, char *filename, long n1, long n2)
{
	editlist *e = info->edit_list;
	if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
		e = info->current_edit_list;
	}

	if( e == NULL ) 
		return 0;

	if( e->num_video_files <= 0 )
		return 0;
		
	if(n1 == 0 && n2 == 0 )
		n2 = e->total_frames;

	if( vj_el_write_editlist( filename, n1,n2, e ) )
		veejay_msg(VEEJAY_MSG_INFO, "Saved EDL to file %s", filename);
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error while saving EDL to %s", filename);
		return 0;
	}	

	return 1;
}

static int	veejay_open_video_files(veejay_t *info, char **files, int num_files, int force , char override_norm)
{
	if( info->dummy->active )
	{
		info->plain_editlist = vj_el_dummy( 0, 
				info->auto_deinterlace,
				info->dummy->chroma,
				info->dummy->norm,
				info->dummy->width,
				info->dummy->height,
				info->dummy->fps,
				info->pixel_format 
				);

		if( info->dummy->arate )
		{
			editlist *el = info->plain_editlist;
			el->has_audio = 1;
			el->audio_rate = info->dummy->arate;
			el->audio_chans = info->dummy->achans;
			el->audio_bits = info->dummy->abits;
			el->audio_bps = info->dummy->abps;
			veejay_msg(VEEJAY_MSG_DEBUG, "Dummy Audio: %f KHz, %d channels, %d bps, %d bit audio",
				(float)el->audio_rate/1000.0,el->audio_chans,el->audio_bps,el->audio_bits);
		}
		
		veejay_msg(VEEJAY_MSG_DEBUG,"Dummy Video: %dx%d, chroma %x, framerate %2.2f, norm %s",
					info->dummy->width,info->dummy->height, info->dummy->chroma,info->dummy->fps,
					(info->dummy->norm == 'n' ? "NTSC" :"PAL"));

		info->video_output_width = info->dummy->width;
		info->video_output_height = info->dummy->height;
	}
	else
	{
		int tmp_wid = info->video_output_width;
		int tmp_hei = info->video_output_height;

	    info->plain_editlist = 
			vj_el_init_with_args(
					files,
					num_files,
					info->preserve_pathnames,
					info->auto_deinterlace,
				    force,
					override_norm,
					info->pixel_format, tmp_wid, tmp_hei);
		if(!info->plain_editlist ) 
			return 0;
	}	
	info->edit_list = info->plain_editlist;
	info->current_edit_list = info->edit_list;

	info->effect_frame_info->width = info->video_output_width;
	info->effect_frame_info->height= info->video_output_height;

	info->effect_frame_info2->width = info->video_output_width;
	info->effect_frame_info2->height= info->video_output_height;

	return 1;
}


static int configure_dummy_defaults(veejay_t *info, char override_norm, float fps, char **files, int n_files)
{
	int default_dw = 720;
	int default_dh = (override_norm == 'n' ? 480 : 576);
	int default_norm = (override_norm == '\0' ? VIDEO_MODE_PAL : veejay_get_norm(override_norm));
	float default_fps = vj_el_get_default_framerate(default_norm);

	if(has_env_setting("VEEJAY_RUN_MODE", "CLASSIC")) {
		default_dw = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 352 : 360);
		default_dh = (default_norm == VIDEO_MODE_PAL || default_norm == VIDEO_MODE_SECAM ? 288 : 240);
		info->settings->fxdepth = 0;
	}

	int dw = default_dw;
	int dh = default_dh;

	float dfps = (fps <= 0.0f ? default_fps : fps);
	float tmp_fps = 0.0f;
	long tmp_arate = 0;

	if(n_files > 0) {
		int in_w = 0;
		int in_h = 0;

		vj_el_scan_video_file(files[0], &in_w, &in_h, &tmp_fps, &tmp_arate);

		if(in_w <= 0 || in_h <= 0) {
			veejay_msg(VEEJAY_MSG_WARNING, "Unable to determine video properties");
			return 0;
		}

		if(info->video_output_width <= 0)
			dw = in_w;
		if(info->video_output_height <= 0)
			dh = in_h;

		if(tmp_fps > 0.0f && fps == 0.0f)
			dfps = tmp_fps;

		if(dw == default_dw && in_w > 0)
			dw = in_w;
		if(dh == default_dh && in_h > 0)
			dh = in_h;

		default_norm = (override_norm == '\0' ? veejay_get_norm(vj_el_get_default_norm(tmp_fps)) :
			veejay_get_norm(override_norm));

		veejay_msg(VEEJAY_MSG_DEBUG, "Video input source is: %dx%d %2.2f fps norm %d",
			in_w, in_h, tmp_fps, default_norm);
	}

	if(info->video_output_width <= 0) {
		if(info->dummy->width > 0)
			info->video_output_width = info->dummy->width;
		else
			info->video_output_width = dw;
	} else {
		dw = info->video_output_width;
	}

	if(info->video_output_height <= 0) {
		if(info->dummy->height > 0)
			info->video_output_height = info->dummy->height;
		else
			info->video_output_height = dh;
	} else {
		dh = info->video_output_height;
	}

	dw = info->video_output_width;
	dh = info->video_output_height;

	if(info->dummy->norm == '\0') {
		if(override_norm != '\0')
			info->dummy->norm = override_norm;
		else
			info->dummy->norm = vj_el_get_default_norm(dfps);
	}

	if(info->dummy->width <= 0)
		info->dummy->width = dw;

	if(info->dummy->height <= 0)
		info->dummy->height = dh;

	if(info->dummy->fps <= 0.0f)
		info->dummy->fps = dfps;

	dfps = info->dummy->fps;

	lav_set_project(dw, dh, dfps, info->pixel_format);

	info->dummy->chroma = get_chroma_from_pixfmt(vj_to_pixfmt(info->pixel_format));
	info->settings->output_fps = dfps;

	if(info->audio) {
		if(tmp_arate > 0 && info->audio == AUDIO_PLAY && fps > 0.0f) {
			veejay_msg(VEEJAY_MSG_WARNING,"Going to run with user specified FPS. This will affect audio playback");
			veejay_msg(VEEJAY_MSG_WARNING,"Specify -a0 to start without audio playback");
		}

		if(info->dummy->arate <= 0) {
			if(tmp_arate > 0) {
				info->dummy->arate = tmp_arate;
			} else {
				info->dummy->arate = 48000;
				veejay_msg(VEEJAY_MSG_WARNING, "Defaulting to 48Khz audio");
			}
		}

		if(info->dummy->achans <= 0)
			info->dummy->achans = 2;

		if(info->dummy->abits <= 0)
			info->dummy->abits = 16;

		if(info->dummy->abps <= 0) {
			if((info->dummy->abits % 8) != 0) {
				veejay_msg(VEEJAY_MSG_ERROR,"Audio bits must be byte-aligned, got %d",
					info->dummy->abits);
				return 0;
			}

			info->dummy->abps = info->dummy->achans * (info->dummy->abits / 8);
		}
	}

	if(n_files <= 0) {
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Dummy source is: %dx%d %2.2f fps norm %d",
			info->dummy->width,
			info->dummy->height,
			info->dummy->fps,
			info->dummy->norm);
	}

	if(info->audio) {
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Dummy audio is: %ld Hz, %d channels, %d bits, %d bytes/sample-frame",
			info->dummy->arate,
			info->dummy->achans,
			info->dummy->abits,
			info->dummy->abps);
	}

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Video output is %dx%d pixels, %2.2f fps",
		info->video_output_width,
		info->video_output_height,
		dfps);

	return 1;
}

int veejay_open_files(veejay_t * info, char **files, int num_files, float ofps, int force,int force_pix_fmt, char override_norm, int switch_jpeg)
{
	int ret = 0;
	switch( force_pix_fmt ) {
			case 1: info->pixel_format = FMT_422;break;
			case 2: info->pixel_format = FMT_422F;break;
			default:
				break;
	}

	char text[24];
	switch(info->pixel_format) {
        case FMT_422:
            snprintf(text, sizeof(text), "4:2:2 [16-235][16-240]");
            break;
        case FMT_422F:
            snprintf(text, sizeof(text), "4:2:2 [0-255]");
            break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported pixel format selected"); 
			return 0;
	}

	if(force_pix_fmt > 0 ) {
	  	veejay_msg(VEEJAY_MSG_WARNING , "Output pixel format set to %s by user", text );
	}
	else
		veejay_msg(VEEJAY_MSG_DEBUG, "Processing set to YUV %s", text );

	if(!configure_dummy_defaults(info,override_norm, ofps,files,num_files)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup video dimensions (did we load any video file ?)");
		return 0;
	}

	vj_el_init( info->pixel_format, switch_jpeg, info->video_output_width,info->video_output_height, info->dummy->fps );
#ifdef USE_GDK_PIXBUF	
    vj_picture_init( &(info->settings->sws_templ));
#endif

	info->effect_frame1 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame1->fps = info->settings->output_fps;
	info->effect_frame2 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame2->fps = info->settings->output_fps;
	info->effect_frame3 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame3->fps = info->settings->output_fps;
	info->effect_frame4 = yuv_yuv_template( NULL,NULL,NULL, info->video_output_width, info->video_output_height, yuv_to_alpha_fmt(vj_to_pixfmt(info->pixel_format)) );
	info->effect_frame4->fps = info->settings->output_fps;

	veejay_msg(VEEJAY_MSG_DEBUG,"Performer is working in %s (%d)", yuv_get_pixfmt_description(info->effect_frame1->format), info->effect_frame1->format);

	if(num_files == 0)
	{
		if(!info->dummy->active)
			info->dummy->active = 1;
		ret = veejay_open_video_files( info, NULL, 0 , force, override_norm );
	}
	else
	{
		ret = veejay_open_video_files( info, files, num_files, force, override_norm );
	}

	return ret;
}
