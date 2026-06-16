/*
 * Copyright (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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

#ifdef HAVE_JACK

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>

#include <veejaycore/defs.h>
#include <veejaycore/atomic.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <veejaycore/vjmem.h>
#include <libveejay/vj-jack.h>
#include <libveejay/vj-audio-sync.h>

#ifndef VJ_AUDIO_SYNC_RING_SECONDS
#define VJ_AUDIO_SYNC_RING_SECONDS 8
#endif

#ifndef VJ_AUDIO_SYNC_MIN_RING_BYTES
#define VJ_AUDIO_SYNC_MIN_RING_BYTES 65536
#endif

#ifndef VJ_AUDIO_SYNC_ANALYSIS_FRAMES
#define VJ_AUDIO_SYNC_ANALYSIS_FRAMES 512
#endif

#ifndef VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES
#define VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES 4096
#endif

#ifndef VJ_AUDIO_SYNC_MIN_BPM
#define VJ_AUDIO_SYNC_MIN_BPM 40.0
#endif

#ifndef VJ_AUDIO_SYNC_MAX_BPM
#define VJ_AUDIO_SYNC_MAX_BPM 240.0
#endif

#ifndef VJ_AUDIO_SYNC_MIN_PERIOD_MS
#define VJ_AUDIO_SYNC_MIN_PERIOD_MS ((long)(60000.0 / VJ_AUDIO_SYNC_MAX_BPM))
#endif

#ifndef VJ_AUDIO_SYNC_MAX_PERIOD_MS
#define VJ_AUDIO_SYNC_MAX_PERIOD_MS ((long)(60000.0 / VJ_AUDIO_SYNC_MIN_BPM))
#endif

#ifndef VJ_AUDIO_SYNC_BRIDGE_LATENCY_MS
#define VJ_AUDIO_SYNC_BRIDGE_LATENCY_MS 80
#endif
#ifndef VJ_AUDIO_SYNC_BRIDGE_MAX_LATENCY_MS
#define VJ_AUDIO_SYNC_BRIDGE_MAX_LATENCY_MS 260
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_HOP_MS
#define VJ_AUDIO_SYNC_ALIGN_HOP_MS 10
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES
#define VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES 24
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_LOCK_MIN_TARGET_FEATURES
#define VJ_AUDIO_SYNC_ALIGN_LOCK_MIN_TARGET_FEATURES 96
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_MAX_LAG_MS
#define VJ_AUDIO_SYNC_ALIGN_MAX_LAG_MS 8000
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_LOCK_CONF
#define VJ_AUDIO_SYNC_ALIGN_LOCK_CONF 58
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SCORE
#define VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SCORE 0.72
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SEPARATION
#define VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SEPARATION 0.045
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_EDGE_GUARD_STEPS
#define VJ_AUDIO_SYNC_ALIGN_EDGE_GUARD_STEPS 4
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_PROBE_MIN_CONTRAST
#define VJ_AUDIO_SYNC_ALIGN_PROBE_MIN_CONTRAST 0.06
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_HOLD_BLOCKS
#define VJ_AUDIO_SYNC_ALIGN_HOLD_BLOCKS 24
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES
#define VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES 768
#endif
#ifndef VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS
#define VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS 9000L
#endif

#ifndef VJ_AUDIO_SYNC_TARGET_RING_SECONDS
#define VJ_AUDIO_SYNC_TARGET_RING_SECONDS 4
#endif
#ifndef VJ_AUDIO_SYNC_TARGET_MIN_RING_BYTES
#define VJ_AUDIO_SYNC_TARGET_MIN_RING_BYTES 65536
#endif
#ifndef VJ_AUDIO_SYNC_TARGET_PROCESS_MAX_FRAMES
#define VJ_AUDIO_SYNC_TARGET_PROCESS_MAX_FRAMES 4096
#endif

#ifndef VJ_AUDIO_SYNC_TARGET_MIN_HISTORY_MS
#define VJ_AUDIO_SYNC_TARGET_MIN_HISTORY_MS 250
#endif
#ifndef VJ_AUDIO_SYNC_TARGET_WANTED_HISTORY_MS
#define VJ_AUDIO_SYNC_TARGET_WANTED_HISTORY_MS 750
#endif

#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OFFSET_MS
#define VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OFFSET_MS 900
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_MIN_CONF
#define VJ_AUDIO_SYNC_LIVE_SNAP_MIN_CONF 88
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_OFFSET_MS
#define VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_OFFSET_MS 4000
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_CONF
#define VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_CONF 70
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_STABLE_COUNT
#define VJ_AUDIO_SYNC_LIVE_SNAP_STABLE_COUNT 3
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_STABLE_COUNT
#define VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_STABLE_COUNT 2
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_TOLERANCE_MS
#define VJ_AUDIO_SYNC_LIVE_SNAP_TOLERANCE_MS 450
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_TICK_MS
#define VJ_AUDIO_SYNC_LIVE_SNAP_TICK_MS 500L
#endif

#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OBSERVE_MS
#define VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OBSERVE_MS 180L
#endif

#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_SETTLE_MAX_FRAMES
#define VJ_AUDIO_SYNC_LIVE_SNAP_SETTLE_MAX_FRAMES 36
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_SNAP_REACQUIRE_CONF
#define VJ_AUDIO_SYNC_LIVE_SNAP_REACQUIRE_CONF 92
#endif

#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_SCORE
#define VJ_AUDIO_SYNC_ALIGN_MIN_SCORE 0.001
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_OVERLAP_PCT
#define VJ_AUDIO_SYNC_ALIGN_MIN_OVERLAP_PCT 75
#endif
#ifndef VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE
#define VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE 1.0e-7
#endif

#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_MIN_CONF
#define VJ_AUDIO_SYNC_ROUGH_HINT_MIN_CONF 18
#endif
#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_MIN_OFFSET_MS
#define VJ_AUDIO_SYNC_ROUGH_HINT_MIN_OFFSET_MS 500
#endif
#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SCORE
#define VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SCORE 0.58
#endif
#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SEPARATION
#define VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SEPARATION 0.018
#endif
#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_TOLERANCE_MS
#define VJ_AUDIO_SYNC_ROUGH_HINT_TOLERANCE_MS 1200
#endif
#ifndef VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS
#define VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS 1800L
#endif
#ifndef VJ_AUDIO_SYNC_QUIET_LIVE_LOG_MS
#define VJ_AUDIO_SYNC_QUIET_LIVE_LOG_MS 1500L
#endif

#ifndef VJ_AUDIO_SYNC_LIVE_LOCK_LOG_DELTA_MS
#define VJ_AUDIO_SYNC_LIVE_LOCK_LOG_DELTA_MS 80
#endif
#ifndef VJ_AUDIO_SYNC_LIVE_LOCK_LOG_CONF_DELTA
#define VJ_AUDIO_SYNC_LIVE_LOCK_LOG_CONF_DELTA 8
#endif
#ifndef VJ_AUDIO_SYNC_SETTLED_BACKWARD_SNAP_BLOCK_MS
#define VJ_AUDIO_SYNC_SETTLED_BACKWARD_SNAP_BLOCK_MS 1000
#endif

static inline int sync_load_i(const volatile int *p) { return atomic_load_int(p); }
static inline long sync_load_l(const volatile long *p) { return atomic_load_long(p); }
static inline void sync_store_i(volatile int *p, int v) { atomic_store_int(p, v); }
static inline void sync_store_l(volatile long *p, long v) { atomic_store_long(p, v); }
static inline long sync_add_l(volatile long *p, long v) { return atomic_add_fetch_long(p, v); }

static long sync_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long)ts.tv_sec * 1000L) + ((long)ts.tv_nsec / 1000000L);
}


static void sync_sleep_us(long usec)
{
    struct timespec ts;
    if(usec <= 0)
        return;
    ts.tv_sec = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;
    while(nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static inline void sync_lock(vj_audio_sync_shared_t *s)
{
    while(__sync_lock_test_and_set(&s->ring_lock, 1))
        sync_sleep_us(50);
}

static inline int sync_try_lock(vj_audio_sync_shared_t *s)
{
    return (__sync_lock_test_and_set(&s->ring_lock, 1) == 0);
}

static inline int sync_try_lock_for_us(vj_audio_sync_shared_t *s, long max_wait_us)
{
    long waited = 0;

    if(!s)
        return 0;

    if(max_wait_us < 0)
        max_wait_us = 0;

    do {
        if(sync_try_lock(s))
            return 1;

        if(waited >= max_wait_us)
            break;

        sync_sleep_us(25);
        waited += 25;
    } while(1);

    return 0;
}

static inline void sync_unlock(vj_audio_sync_shared_t *s)
{
    __sync_lock_release(&s->ring_lock);
}

static inline double sync_clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int sync_q15(double v)
{
    v = sync_clampd(v, 0.0, 1.0);
    return (int)(v * 32767.0 + 0.5);
}

static inline float sync_from_q15(int v)
{
    if(v <= 0) return 0.0f;
    if(v >= 32767) return 1.0f;
    return (float)v / 32767.0f;
}

static inline int sync_q8_bpm(double bpm)
{
    if(bpm < 0.0) bpm = 0.0;
    if(bpm > 9999.0) bpm = 9999.0;
    return (int)(bpm * 256.0 + 0.5);
}

static inline double sync_bpm_from_q8(int q)
{
    return (q <= 0) ? 0.0 : ((double)q / 256.0);
}

static inline int sync_clip16(int v)
{
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

static inline int sync_read_sample(const uint8_t *p, int sample_bytes)
{
    if(sample_bytes == 1)
        return ((int)p[0] - 128) << 8;
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline void sync_write_sample(uint8_t *p, int sample_bytes, int v)
{
    v = sync_clip16(v);
    if(sample_bytes == 1) {
        p[0] = (uint8_t)((v >> 8) + 128);
        return;
    }
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static inline int sync_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int sync_align_hop_frames(int sample_rate)
{
    int hop;
    if(sample_rate <= 0)
        sample_rate = 44100;
    hop = (sample_rate * VJ_AUDIO_SYNC_ALIGN_HOP_MS) / 1000;
    if(hop < 64)
        hop = 64;
    return hop;
}

static void sync_track_align_clear_live_candidate_locked(vj_audio_sync_shared_t *s);
static void sync_track_align_clear_rough_hint_locked(vj_audio_sync_shared_t *s);
static void sync_clear_target_queue_locked(vj_audio_sync_shared_t *s);
static void sync_wav_side_clear(vj_audio_sync_shared_t *s);

static void sync_track_align_clear_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    memset(s->align_source_feat, 0, sizeof(s->align_source_feat));
    memset(s->align_target_feat, 0, sizeof(s->align_target_feat));
    s->align_source_pos = 0;
    s->align_target_pos = 0;
    s->align_source_count = 0;
    s->align_target_count = 0;
    s->align_source_accum = 0.0;
    s->align_target_accum = 0.0;
    s->align_source_accum_frames = 0;
    s->align_target_accum_frames = 0;
    s->align_source_prev_level = 0.0;
    s->align_target_prev_level = 0.0;
    s->align_source_rate = 0;
    s->align_target_rate = 0;
    s->align_offset_smooth_ms = 0.0;
    s->align_conf_smooth = 0.0;
    s->align_hold_blocks = 0;
    s->align_last_frame_action = 1;
    sync_clear_target_queue_locked(s);
    s->align_last_snap_ms = 0;
    s->align_snap_cooldown_ms = VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS;
    if(sync_load_i(&s->align_video_fps_x1000) <= 0)
        sync_store_i(&s->align_video_fps_x1000, 25000);
    sync_track_align_clear_live_candidate_locked(s);
    sync_track_align_clear_rough_hint_locked(s);
    s->align_live_last_snap_ms = 0;
    s->align_live_last_snap_delta = 0;
    sync_store_i(&s->track_align_locked, 0);
    sync_store_i(&s->track_align_offset_ms, 0);
    sync_store_i(&s->track_align_confidence_pct, 0);
    sync_store_i(&s->track_align_correction_ppm, 0);
    sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_IDLE);
    sync_store_l(&s->track_align_last_update_ms, 0);
    sync_store_i(&s->track_align_snap_pending, 0);
    sync_store_i(&s->track_align_snap_delta_frames, 0);
    sync_store_i(&s->track_align_snap_confidence_pct, 0);
}

static void sync_track_align_clear_target_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    memset(s->align_target_feat, 0, sizeof(s->align_target_feat));
    s->align_target_pos = 0;
    s->align_target_count = 0;
    s->align_target_accum = 0.0;
    s->align_target_accum_frames = 0;
    s->align_target_prev_level = 0.0;
    s->align_target_rate = 0;
    s->align_offset_smooth_ms = 0.0;
    s->align_conf_smooth = 0.0;
    s->align_hold_blocks = 0;
    s->align_last_frame_action = 1;
    sync_clear_target_queue_locked(s);
    sync_track_align_clear_live_candidate_locked(s);
    sync_track_align_clear_rough_hint_locked(s);
    sync_store_i(&s->track_align_locked, 0);
    sync_store_i(&s->track_align_offset_ms, 0);
    sync_store_i(&s->track_align_confidence_pct, 0);
    sync_store_i(&s->track_align_correction_ppm, 0);
    sync_store_i(&s->track_align_state,
                 (s->align_source_count >= VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES) ?
                    VJ_AUDIO_SYNC_TRACK_STATE_WAIT_TARGET :
                    VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE);
    sync_store_l(&s->track_align_last_update_ms, sync_now_ms());
    sync_store_i(&s->track_align_snap_pending, 0);
    sync_store_i(&s->track_align_snap_delta_frames, 0);
    sync_store_i(&s->track_align_snap_confidence_pct, 0);
}

static inline float sync_align_ring_get(const float *ring, int pos, int count, int newest_back)
{
    int idx;
    if(count <= 0)
        return 0.0f;
    if(newest_back < 0)
        newest_back = 0;
    if(newest_back >= count)
        newest_back = count - 1;
    idx = pos - 1 - newest_back;
    while(idx < 0)
        idx += VJ_AUDIO_SYNC_ALIGN_FEATURES;
    idx %= VJ_AUDIO_SYNC_ALIGN_FEATURES;
    return ring[idx];
}

static void sync_track_align_append_feature_locked(vj_audio_sync_shared_t *s,
                                                   int is_target,
                                                   double level,
                                                   int sample_rate)
{
    float *ring;
    int *pos;
    int *count;
    double *prev;
    double rise;
    double feat;

    if(!s)
        return;

    ring  = is_target ? s->align_target_feat : s->align_source_feat;
    pos   = is_target ? &s->align_target_pos : &s->align_source_pos;
    count = is_target ? &s->align_target_count : &s->align_source_count;
    prev  = is_target ? &s->align_target_prev_level : &s->align_source_prev_level;

    if(is_target)
        s->align_target_rate = sample_rate;
    else
        s->align_source_rate = sample_rate;

    rise = level - *prev;
    if(rise < 0.0)
        rise = 0.0;

    feat = (level * 3.0) + (rise * 10.0);
    feat = sync_clampd(feat, 0.0, 1.0);

    ring[*pos] = (float)feat;
    *pos = (*pos + 1) % VJ_AUDIO_SYNC_ALIGN_FEATURES;
    if(*count < VJ_AUDIO_SYNC_ALIGN_FEATURES)
        (*count)++;

    *prev = (0.85 * (*prev)) + (0.15 * level);
}

static const char *sync_track_align_state_name_dbg(int state)
{
    switch(state) {
        case VJ_AUDIO_SYNC_TRACK_STATE_IDLE: return "idle";
        case VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE: return "wait-source";
        case VJ_AUDIO_SYNC_TRACK_STATE_WAIT_TARGET: return "wait-target";
        case VJ_AUDIO_SYNC_TRACK_STATE_SEARCHING: return "searching";
        case VJ_AUDIO_SYNC_TRACK_STATE_LOCKED: return "locked";
        case VJ_AUDIO_SYNC_TRACK_STATE_HOLD: return "hold";
        case VJ_AUDIO_SYNC_TRACK_STATE_FALLBACK: return "fallback";
        default: return "unknown";
    }
}


typedef struct
{
    int n;
    double mean;
    double min;
    double max;
    double variance;
    double sd;
    double energy;
    int near_zero;
} sync_track_align_feature_stats_t;

static void sync_track_align_stats_clear(sync_track_align_feature_stats_t *st)
{
    if(!st)
        return;
    st->n = 0;
    st->mean = 0.0;
    st->min = 0.0;
    st->max = 0.0;
    st->variance = 0.0;
    st->sd = 0.0;
    st->energy = 0.0;
    st->near_zero = 0;
}

static void sync_track_align_feature_stats_array(const float *v,
                                                 int n,
                                                 sync_track_align_feature_stats_t *st)
{
    double sum = 0.0;
    double sum2 = 0.0;

    sync_track_align_stats_clear(st);

    if(!v || !st || n <= 0)
        return;

    st->n = n;
    st->min = (double)v[0];
    st->max = (double)v[0];

    for(int i = 0; i < n; i++) {
        double x = (double)v[i];
        sum += x;
        sum2 += x * x;
        if(x < st->min)
            st->min = x;
        if(x > st->max)
            st->max = x;
        if(x > -0.000001 && x < 0.000001)
            st->near_zero++;
    }

    st->mean = sum / (double)n;
    st->energy = sum2 / (double)n;
    st->variance = st->energy - (st->mean * st->mean);
    if(st->variance < 0.0)
        st->variance = 0.0;
    st->sd = sqrt(st->variance);
}

static void sync_track_align_feature_stats_ring(const float *ring,
                                                int pos,
                                                int count,
                                                int window,
                                                sync_track_align_feature_stats_t *st)
{
    float tmp[256];

    if(window > (int)(sizeof(tmp) / sizeof(tmp[0])))
        window = (int)(sizeof(tmp) / sizeof(tmp[0]));
    if(window > count)
        window = count;
    if(window < 0)
        window = 0;

    for(int i = 0; i < window; i++)
        tmp[i] = sync_align_ring_get(ring, pos, count, i);

    sync_track_align_feature_stats_array(tmp, window, st);
}

static void sync_track_align_pcm_stats(const uint8_t *data,
                                       int frames,
                                       int frame_bytes,
                                       int channels,
                                       int bits,
                                       double *rms,
                                       double *peak,
                                       int *nonzero)
{
    int sample_bytes;
    double sum2 = 0.0;
    double pk = 0.0;
    int nz = 0;

    if(rms)
        *rms = 0.0;
    if(peak)
        *peak = 0.0;
    if(nonzero)
        *nonzero = 0;

    if(!data || frames <= 0 || frame_bytes <= 0 || channels <= 0 ||
       (bits != 8 && bits != 16))
        return;

    sample_bytes = bits / 8;
    if(sample_bytes <= 0 || frame_bytes < sample_bytes * channels)
        return;

    for(int i = 0; i < frames; i++) {
        const uint8_t *f = data + ((size_t)i * (size_t)frame_bytes);
        double mono = 0.0;

        for(int c = 0; c < channels; c++) {
            int v = sync_read_sample(f + (c * sample_bytes), sample_bytes);
            mono += (double)v * (1.0 / 32768.0);
        }

        mono /= (double)channels;
        if(mono < 0.0)
            mono = -mono;
        if(mono > pk)
            pk = mono;
        if(mono > 0.000030)
            nz++;
        sum2 += mono * mono;
    }

    if(rms)
        *rms = sqrt(sum2 / (double)frames);
    if(peak)
        *peak = pk;
    if(nonzero)
        *nonzero = nz;
}

static int sync_track_align_probe_debug_should_log(const char *reason, int confidence_pct)
{
    static long last_log_ms = 0;
    long now_ms = sync_now_ms();

    if(confidence_pct >= 70)
        return 1;

    if(reason && strcmp(reason, "ok") != 0 &&
       (last_log_ms == 0 || (now_ms - last_log_ms) >= 2500L))
    {
        last_log_ms = now_ms;
        return 1;
    }

    return 0;
}


static void sync_track_align_clear_live_candidate_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    s->align_live_candidate_delta = 0;
    s->align_live_candidate_conf_min = 0;
    s->align_live_candidate_conf_sum = 0;
    s->align_live_candidate_count = 0;
    s->align_live_candidate_ms = 0;
}

static void sync_track_align_clear_rough_hint_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    s->align_rough_hint_offset_ms = 0;
    s->align_rough_hint_conf = 0;
    s->align_rough_hint_count = 0;
    s->align_rough_hint_ms = 0;
}

static int sync_track_align_candidate_same_frames(int a, int b, int tol)
{
    int d = a - b;
    if(d < 0)
        d = -d;
    return d <= tol;
}

static int sync_track_align_offer_snap_ex_locked(vj_audio_sync_shared_t *s,
                                                 int delta_frames,
                                                 int confidence_pct,
                                                 const char *reason,
                                                 int tiny_servo)
{
    long now_ms;
    long cooldown_ms;
    long since;
    int seq;
    int pending;
    int conf_clamped;
    char since_buf[32];

    if(!s || delta_frames == 0 || confidence_pct < 1)
        return 0;

    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return 0;

    conf_clamped = sync_clampi(confidence_pct, 0, 100);
    now_ms = sync_now_ms();
    cooldown_ms = s->align_snap_cooldown_ms;
    since = (s->align_last_snap_ms == 0) ? -1 : (now_ms - s->align_last_snap_ms);
    if(cooldown_ms <= 0)
        cooldown_ms = VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS;

    if(tiny_servo && delta_frames != -1 && delta_frames != 1) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] ignored invalid-servo delta=%+d conf=%d%% reason=%s",
                   delta_frames,
                   conf_clamped,
                   reason ? reason : "servo");
        return 0;
    }

    pending = sync_load_i(&s->track_align_snap_pending);
    if(pending) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] suppressed pending delta=%+d conf=%d%% pending=1 reason=%s",
                   delta_frames,
                   conf_clamped,
                   reason ? reason : (tiny_servo ? "servo" : "worker"));
        return 0;
    }

    if(!tiny_servo &&
       s->align_last_snap_ms != 0 &&
       (now_ms - s->align_last_snap_ms) < cooldown_ms)
    {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] suppressed cooldown delta=%+d conf=%d%% since_last=%ldms cooldown=%ldms pending=0 reason=%s",
                   delta_frames,
                   conf_clamped,
                   since,
                   cooldown_ms,
                   reason ? reason : "worker");
        return 0;
    }

    seq = sync_load_i(&s->track_align_snap_seq) + 1;
    sync_store_i(&s->track_align_snap_delta_frames, delta_frames);
    sync_store_i(&s->track_align_snap_confidence_pct, conf_clamped);
    sync_store_i(&s->track_align_snap_pending, 1);
    sync_store_i(&s->track_align_snap_seq, seq);

    if(!tiny_servo) {
        s->align_last_snap_ms = now_ms;
        s->align_live_last_snap_ms = now_ms;
        s->align_live_last_snap_delta = delta_frames;
    }

    if(since < 0)
        snprintf(since_buf, sizeof(since_buf), "n/a");
    else
        snprintf(since_buf, sizeof(since_buf), "%ldms", since);

    if(tiny_servo) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] accepted servo delta=%+d conf=%d%% seq=%d since_hard=%s reason=%s",
                   delta_frames,
                   conf_clamped,
                   seq,
                   since_buf,
                   reason ? reason : "servo");
    }
    else {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] accepted delta=%+d conf=%d%% seq=%d since_last=%s cooldown=%ldms reason=%s",
                   delta_frames, conf_clamped, seq, since_buf, cooldown_ms,
                   reason ? reason : "worker");
    }
    return 1;
}

static int sync_track_align_offer_snap_locked(vj_audio_sync_shared_t *s,
                                              int delta_frames,
                                              int confidence_pct,
                                              const char *reason)
{
    return sync_track_align_offer_snap_ex_locked(s,
                                                delta_frames,
                                                confidence_pct,
                                                reason,
                                                0);
}

static int sync_track_align_offer_servo_locked(vj_audio_sync_shared_t *s,
                                               int delta_frames,
                                               int confidence_pct,
                                               const char *reason)
{
    return sync_track_align_offer_snap_ex_locked(s,
                                                delta_frames,
                                                confidence_pct,
                                                reason,
                                                1);
}

static void sync_track_align_live_requirements_locked(vj_audio_sync_shared_t *s,
                                                        int abs_ms,
                                                        int confidence_pct,
                                                        int *required_conf,
                                                        int *required_stable,
                                                        long *required_observe_ms)
{
    int req_conf = VJ_AUDIO_SYNC_LIVE_SNAP_MIN_CONF;
    int req_stable = VJ_AUDIO_SYNC_LIVE_SNAP_STABLE_COUNT;
    long req_observe = VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OBSERVE_MS;
    const int first_acquire = (s && s->align_live_last_snap_ms == 0);

    (void)confidence_pct;

    if(first_acquire && abs_ms >= 1800) {
        int urgency = abs_ms - 1800;
        if(urgency < 0)
            urgency = 0;
        if(urgency > 3200)
            urgency = 3200;

        /* 1800ms -> about 76%, 5000ms+ -> about 58%. */
        req_conf = 76 - ((urgency * 18) / 3200);
        if(req_conf < 58)
            req_conf = 58;
        if(req_conf > VJ_AUDIO_SYNC_LIVE_SNAP_MIN_CONF)
            req_conf = VJ_AUDIO_SYNC_LIVE_SNAP_MIN_CONF;

        req_stable = 1;
        req_observe = 120L;
    }

    if(abs_ms >= VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_OFFSET_MS) {
        if(first_acquire) {
            if(req_conf > VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_CONF)
                req_conf = VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_CONF;
            req_stable = 1;
            if(req_observe > 120L)
                req_observe = 120L;
        } else {
            req_conf = VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_CONF;
            req_stable = VJ_AUDIO_SYNC_LIVE_SNAP_LARGE_STABLE_COUNT;
        }
    }

    if(!first_acquire) {
        req_observe = VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OBSERVE_MS;
    }

    if(required_conf)
        *required_conf = req_conf;
    if(required_stable)
        *required_stable = req_stable;
    if(required_observe_ms)
        *required_observe_ms = req_observe;
}

static void sync_track_align_try_live_snap_locked(vj_audio_sync_shared_t *s,
                                                  double raw_offset_ms,
                                                  int confidence_pct,
                                                  long now_ms)
{
    int abs_ms;
    int required_conf;
    int required_stable;
    int fps_x1000;
    double fps;
    int delta_frames;
    int abs_delta_frames;
    int tol_frames;
    int same_candidate = 0;
    long elapsed_ms = 0;
    long required_observe_ms = VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OBSERVE_MS;

    if(!s)
        return;

    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return;
    if(sync_load_i(&s->target_mode) != VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return;

    if(sync_load_i(&s->track_align_snap_pending)) {
        if(s->align_live_candidate_count > 0)
            sync_track_align_clear_live_candidate_locked(s);
        return;
    }

    confidence_pct = sync_clampi(confidence_pct, 0, 100);

    abs_ms = (raw_offset_ms < 0.0) ? (int)(-raw_offset_ms + 0.5)
                                  : (int)( raw_offset_ms + 0.5);
    if(abs_ms < VJ_AUDIO_SYNC_LIVE_SNAP_MIN_OFFSET_MS) {
        if(s->align_live_candidate_count > 0 &&
           now_ms - s->align_live_candidate_ms > VJ_AUDIO_SYNC_LIVE_SNAP_TICK_MS)
            sync_track_align_clear_live_candidate_locked(s);
        return;
    }

    sync_track_align_live_requirements_locked(s,
                                                abs_ms,
                                                confidence_pct,
                                                &required_conf,
                                                &required_stable,
                                                &required_observe_ms);

    if(confidence_pct < required_conf) {
        if(s->align_live_candidate_count > 0 &&
           s->align_live_candidate_ms > 0 &&
           (now_ms - s->align_live_candidate_ms) > VJ_AUDIO_SYNC_LIVE_SNAP_TICK_MS)
            sync_track_align_clear_live_candidate_locked(s);
        return;
    }

    fps_x1000 = sync_load_i(&s->align_video_fps_x1000);
    fps = (fps_x1000 > 0) ? ((double)fps_x1000 / 1000.0) : 25.0;
    if(fps < 1.0)
        fps = 25.0;
    else if(fps > 240.0)
        fps = 240.0;

    delta_frames = (int)((raw_offset_ms * fps / 1000.0) +
                         (raw_offset_ms >= 0.0 ? 0.5 : -0.5));
    if(delta_frames == 0)
        return;

    abs_delta_frames = delta_frames < 0 ? -delta_frames : delta_frames;

    if(s->align_live_last_snap_ms != 0 &&
       delta_frames < 0 &&
       abs_ms < VJ_AUDIO_SYNC_SETTLED_BACKWARD_SNAP_BLOCK_MS)
    {
        if(s->align_live_candidate_count > 0)
            sync_track_align_clear_live_candidate_locked(s);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN] live settled backward snap held delta=%+d from %+dms conf=%d%% block_ms=%d reason=sync-worker",
                   delta_frames,
                   (int)(raw_offset_ms + (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                   confidence_pct,
                   VJ_AUDIO_SYNC_SETTLED_BACKWARD_SNAP_BLOCK_MS);
        return;
    }

    if(s->align_live_last_snap_ms != 0 &&
       abs_delta_frames > VJ_AUDIO_SYNC_LIVE_SNAP_SETTLE_MAX_FRAMES &&
       confidence_pct < VJ_AUDIO_SYNC_LIVE_SNAP_REACQUIRE_CONF)
    {
        if(s->align_live_candidate_count > 0)
            sync_track_align_clear_live_candidate_locked(s);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN] live offset reacquire suppressed delta=%+d from %+dms conf=%d%% need_conf=%d%% settle_max=%d reason=sync-worker",
                   delta_frames,
                   (int)(raw_offset_ms + (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                   confidence_pct,
                   VJ_AUDIO_SYNC_LIVE_SNAP_REACQUIRE_CONF,
                   VJ_AUDIO_SYNC_LIVE_SNAP_SETTLE_MAX_FRAMES);
        return;
    }

    tol_frames = (int)(fps * ((double)VJ_AUDIO_SYNC_LIVE_SNAP_TOLERANCE_MS / 1000.0) + 0.5);
    if(tol_frames < 4)
        tol_frames = 4;

    if(s->align_live_candidate_count > 0 &&
       s->align_live_candidate_ms > 0)
        elapsed_ms = now_ms - s->align_live_candidate_ms;

    if(s->align_live_candidate_count > 0 &&
       elapsed_ms >= 0 &&
       elapsed_ms <= VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS &&
       sync_track_align_candidate_same_frames(s->align_live_candidate_delta,
                                              delta_frames,
                                              tol_frames))
    {
        same_candidate = 1;

        if(elapsed_ms < required_observe_ms) {
            static long last_hold_log_ms = 0;
            static int last_hold_delta = 0;
            if(last_hold_log_ms == 0 ||
               now_ms - last_hold_log_ms >= 200L ||
               last_hold_delta != s->align_live_candidate_delta)
            {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[TRACK-ALIGN] live consensus holding %+d frames from %+dms stable=%d/%d elapsed=%ld/%ldms conf=%d%%",
                           s->align_live_candidate_delta,
                           (int)(raw_offset_ms + (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                           s->align_live_candidate_count,
                           required_stable,
                           elapsed_ms,
                           required_observe_ms,
                           confidence_pct);
                last_hold_log_ms = now_ms;
                last_hold_delta = s->align_live_candidate_delta;
            }
            return;
        }

        if(s->align_live_candidate_count < required_stable)
            s->align_live_candidate_count++;
        if(s->align_live_candidate_conf_min <= 0 ||
           confidence_pct < s->align_live_candidate_conf_min)
            s->align_live_candidate_conf_min = confidence_pct;
        if(s->align_live_candidate_count <= 1)
            s->align_live_candidate_conf_sum = confidence_pct;
        else
            s->align_live_candidate_conf_sum += confidence_pct;
    } else {
        s->align_live_candidate_delta = delta_frames;
        s->align_live_candidate_count = 1;
        s->align_live_candidate_conf_min = confidence_pct;
        s->align_live_candidate_conf_sum = confidence_pct;
    }

    s->align_live_candidate_ms = now_ms;

    if(!same_candidate ||
       s->align_live_candidate_count < required_stable)
    {
        int avg_dbg = (s->align_live_candidate_count > 0) ?
            sync_clampi(s->align_live_candidate_conf_sum / s->align_live_candidate_count, 0, 100) : confidence_pct;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN] live offset consensus candidate %+d frames from %+dms conf_min=%d%% conf_cur=%d%% conf_avg=%d%% stable=%d/%d need_conf=%d%% observe=%ldms first=%d reason=sync-worker",
                   s->align_live_candidate_delta,
                   (int)(raw_offset_ms + (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                   sync_clampi(s->align_live_candidate_conf_min, 0, 100),
                   confidence_pct,
                   avg_dbg,
                   s->align_live_candidate_count,
                   required_stable,
                   required_conf,
                   required_observe_ms,
                   s->align_live_last_snap_ms == 0);
        return;
    }

    {
        int avg_conf = (s->align_live_candidate_count > 0) ?
            (s->align_live_candidate_conf_sum / s->align_live_candidate_count) : confidence_pct;
        avg_conf = sync_clampi(avg_conf, 0, 100);

        if(sync_clampi(s->align_live_candidate_conf_min, 0, 100) < required_conf ||
           avg_conf < required_conf)
        {
            sync_track_align_clear_live_candidate_locked(s);
            return;
        }

        if(sync_track_align_offer_snap_locked(s,
                                              s->align_live_candidate_delta,
                                              avg_conf,
                                              "live-sync-worker"))
        {
            veejay_msg(VEEJAY_MSG_INFO,
                       "[TRACK-ALIGN] stable live-offset candidate %+d frames from %+dms conf=%d%% stable=%d/%d observe=%ldms first=%d worker",
                       s->align_live_candidate_delta,
                       (int)(raw_offset_ms + (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                       avg_conf,
                       required_stable,
                       required_stable,
                       required_observe_ms,
                       s->align_live_last_snap_ms == 0);
            s->align_live_last_snap_ms = now_ms;
            s->align_live_last_snap_delta = s->align_live_candidate_delta;
            sync_track_align_clear_live_candidate_locked(s);
        } else {
            sync_track_align_clear_live_candidate_locked(s);
        }
    }
}


static void sync_track_align_publish_locked(vj_audio_sync_shared_t *s)
{
    float source_feat[VJ_AUDIO_SYNC_ALIGN_FEATURES];
    float target_feat[VJ_AUDIO_SYNC_ALIGN_FEATURES];
    int source_pos;
    int target_pos;
    int source_count;
    int target_count;
    int source_rate;
    int target_rate;
    long long target_queue_frames = 0;
    long target_overruns = 0;
    long target_reads = 0;
    int target_queue_ms = 0;

    int min_count;
    int max_lag_steps;
    int best_lag = 0;
    double best_score = -1.0;
    double second_score = -1.0;
    int window;
    int min_overlap;
    int state;
    double conf;
    double peak_sep;
    int edge_lag;
    double offset_ms;
    double raw_offset_ms;
    double correction_ppm;
    int lock_eligible;
    int published_locked;
    int valid_lags = 0;
    int overlap_rejects = 0;
    int lowvar_rejects = 0;
    int lowvar_source_rejects = 0;
    int lowvar_target_rejects = 0;
    int lowscore_rejects = 0;
    int total_lags = 0;
    int best_overlap = 0;
    int no_valid_score = 0;
    double min_src_var = 0.0;
    double min_tgt_var = 0.0;
    double max_src_var = 0.0;
    double max_tgt_var = 0.0;
    int var_seen = 0;
    sync_track_align_feature_stats_t src_stats;
    sync_track_align_feature_stats_t tgt_stats;
    const char *quality_reason = "ok";

    if(!s)
        return;

    sync_lock(s);
    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        sync_store_i(&s->track_align_locked, 0);
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_IDLE);
        sync_unlock(s);
        return;
    }

    if(s->align_source_count < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES) {
        sync_store_i(&s->track_align_locked, 0);
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE);
        sync_unlock(s);
        return;
    }

    if(s->align_target_count < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES) {
        sync_store_i(&s->track_align_locked, 0);
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_WAIT_TARGET);
        sync_unlock(s);
        return;
    }

    source_pos = s->align_source_pos;
    target_pos = s->align_target_pos;
    source_count = s->align_source_count;
    target_count = s->align_target_count;
    source_rate = s->align_source_rate;
    target_rate = s->align_target_rate;
    memcpy(source_feat, s->align_source_feat, sizeof(source_feat));
    memcpy(target_feat, s->align_target_feat, sizeof(target_feat));

    if(s->target_write_frame_abs > s->target_read_frame_abs)
        target_queue_frames = s->target_write_frame_abs - s->target_read_frame_abs;
    target_overruns = sync_load_l(&s->target_queue_overruns);
    target_reads = sync_load_l(&s->target_queue_reads);
    if(s->target_sample_rate > 0 && target_queue_frames > 0) {
        long long qms = (target_queue_frames * 1000LL) / (long long)s->target_sample_rate;
        if(qms > 999999LL)
            qms = 999999LL;
        target_queue_ms = (int)qms;
    }
    sync_unlock(s);

    if(target_queue_ms < VJ_AUDIO_SYNC_TARGET_MIN_HISTORY_MS ||
       target_count < VJ_AUDIO_SYNC_ALIGN_LOCK_MIN_TARGET_FEATURES)
    {
        sync_lock(s);
        if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
            sync_store_i(&s->track_align_locked, 0);
            sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_WAIT_TARGET);
            sync_store_i(&s->track_align_confidence_pct,
                         sync_clampi((int)(s->align_conf_smooth + 0.5), 0, 100));
            sync_store_l(&s->track_align_last_update_ms, sync_now_ms());
        }
        sync_unlock(s);
        return;
    }

    min_count = source_count < target_count ? source_count : target_count;
    window = min_count;
    if(window > 128)
        window = 128;
    if(window < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES) {
        sync_lock(s);
        sync_store_i(&s->track_align_locked, 0);
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_SEARCHING);
        sync_unlock(s);
        return;
    }

    sync_track_align_feature_stats_ring(source_feat,
                                        source_pos,
                                        source_count,
                                        window,
                                        &src_stats);
    sync_track_align_feature_stats_ring(target_feat,
                                        target_pos,
                                        target_count,
                                        window,
                                        &tgt_stats);

    min_overlap = (window * VJ_AUDIO_SYNC_ALIGN_MIN_OVERLAP_PCT + 99) / 100;
    if(min_overlap < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES)
        min_overlap = VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES;
    if(min_overlap > window)
        min_overlap = window;

    max_lag_steps = VJ_AUDIO_SYNC_ALIGN_MAX_LAG_MS / VJ_AUDIO_SYNC_ALIGN_HOP_MS;
    if(max_lag_steps > source_count - min_overlap)
        max_lag_steps = source_count - min_overlap;
    if(max_lag_steps > target_count - min_overlap)
        max_lag_steps = target_count - min_overlap;
    if(max_lag_steps < 2)
        max_lag_steps = 2;

    for(int lag = -max_lag_steps; lag <= max_lag_steps; lag++) {
        double sum_x = 0.0, sum_y = 0.0;
        double sum_xx = 0.0, sum_yy = 0.0, sum_xy = 0.0;
        int n = 0;

        total_lags++;

        for(int i = 0; i < window; i++) {
            int src_back = i;
            int tgt_back = i - lag;
            if(tgt_back < 0 || tgt_back >= target_count)
                continue;
            if(src_back < 0 || src_back >= source_count)
                continue;

            double x = sync_align_ring_get(source_feat,
                                           source_pos,
                                           source_count,
                                           src_back);
            double y = sync_align_ring_get(target_feat,
                                           target_pos,
                                           target_count,
                                           tgt_back);
            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_yy += y * y;
            sum_xy += x * y;
            n++;
        }

        if(n < min_overlap) {
            overlap_rejects++;
            continue;
        }

        {
            double nf = (double)n;
            double cov = sum_xy - (sum_x * sum_y / nf);
            double vx = sum_xx - (sum_x * sum_x / nf);
            double vy = sum_yy - (sum_y * sum_y / nf);
            double score = 0.0;

            if(!var_seen) {
                min_src_var = vx;
                min_tgt_var = vy;
                max_src_var = vx;
                max_tgt_var = vy;
                var_seen = 1;
            } else {
                if(vx < min_src_var) min_src_var = vx;
                if(vy < min_tgt_var) min_tgt_var = vy;
                if(vx > max_src_var) max_src_var = vx;
                if(vy > max_tgt_var) max_tgt_var = vy;
            }

            if(vx <= VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE ||
               vy <= VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE)
            {
                if(vx <= VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE)
                    lowvar_source_rejects++;
                if(vy <= VJ_AUDIO_SYNC_ALIGN_MIN_VARIANCE)
                    lowvar_target_rejects++;
                lowvar_rejects++;
                continue;
            }

            score = cov / sqrt(vx * vy);
            if(score < 0.0)
                score = 0.0;
            else if(score > 1.0)
                score = 1.0;

            if(score < VJ_AUDIO_SYNC_ALIGN_MIN_SCORE) {
                lowscore_rejects++;
                continue;
            }

            valid_lags++;

            if(score > best_score) {
                second_score = best_score;
                best_score = score;
                best_lag = lag;
                best_overlap = n;
            } else if(score > second_score) {
                second_score = score;
            }
        }
    }

    if(valid_lags <= 0 || best_score < VJ_AUDIO_SYNC_ALIGN_MIN_SCORE) {
        no_valid_score = 1;
        best_lag = 0;
        best_score = 0.0;
        second_score = 0.0;
        best_overlap = 0;
        quality_reason = (lowvar_rejects > 0 && lowvar_rejects >= overlap_rejects) ?
                         "low-variance" :
                         (overlap_rejects > 0 ? "low-overlap" : "no-valid-score");
    } else if(second_score < 0.0) {
        second_score = 0.0;
    }

    peak_sep = best_score - second_score;
    if(peak_sep < 0.0)
        peak_sep = 0.0;

    edge_lag = (!no_valid_score &&
                abs(best_lag) >= (max_lag_steps - VJ_AUDIO_SYNC_ALIGN_EDGE_GUARD_STEPS));

    conf = best_score * 100.0;
    conf *= sync_clampd(peak_sep * 4.0 + 0.45, 0.0, 1.0);

    if(edge_lag && !(best_score >= 0.90 && peak_sep >= 0.12)) {
        conf *= 0.25;
        quality_reason = "edge";
    }

    if(best_score < VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SCORE ||
       peak_sep < VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SEPARATION)
    {
        conf *= 0.55;
        if(!no_valid_score && !edge_lag)
            quality_reason = (best_score < VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SCORE) ?
                             "weak-score" : "weak-separation";
    }

    if(no_valid_score)
        conf = 0.0;

    offset_ms = no_valid_score ? 0.0 : ((double)best_lag * (double)VJ_AUDIO_SYNC_ALIGN_HOP_MS);
    raw_offset_ms = offset_ms;

    lock_eligible = 1;
    if(no_valid_score)
        lock_eligible = 0;
    if(edge_lag)
        lock_eligible = 0;
    if(target_count < VJ_AUDIO_SYNC_ALIGN_LOCK_MIN_TARGET_FEATURES)
        lock_eligible = 0;
    if(best_score < VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SCORE ||
       peak_sep < VJ_AUDIO_SYNC_ALIGN_MIN_LOCK_SEPARATION)
        lock_eligible = 0;

    correction_ppm = 0.0;

    sync_lock(s);
    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        sync_store_i(&s->track_align_locked, 0);
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_IDLE);
        sync_unlock(s);
        return;
    }

    s->align_conf_smooth = (0.80 * s->align_conf_smooth) + (0.20 * conf);

    if(lock_eligible &&
       s->align_conf_smooth >= (double)VJ_AUDIO_SYNC_ALIGN_LOCK_CONF) {
        s->align_offset_smooth_ms = (0.86 * s->align_offset_smooth_ms) +
                                    (0.14 * offset_ms);
        s->align_hold_blocks = VJ_AUDIO_SYNC_ALIGN_HOLD_BLOCKS;
        state = VJ_AUDIO_SYNC_TRACK_STATE_LOCKED;
    } else if(!no_valid_score && !edge_lag && s->align_hold_blocks > 0) {
        s->align_hold_blocks--;
        state = VJ_AUDIO_SYNC_TRACK_STATE_HOLD;
    } else {
        state = (s->align_conf_smooth > 25.0) ?
                VJ_AUDIO_SYNC_TRACK_STATE_SEARCHING :
                VJ_AUDIO_SYNC_TRACK_STATE_FALLBACK;
    }

    published_locked = (lock_eligible &&
                        state == VJ_AUDIO_SYNC_TRACK_STATE_LOCKED) ? 1 : 0;

    {
        double export_offset_ms = 0.0;
        double export_conf = s->align_conf_smooth;
        long hint_now_ms = sync_now_ms();
        int raw_hint_ms = sync_clampi((int)(raw_offset_ms +
                                           (raw_offset_ms >= 0.0 ? 0.5 : -0.5)),
                                      -8000, 8000);
        int raw_hint_conf = sync_clampi((int)(conf + 0.5), 0, 100);
        int rough_usable = (!no_valid_score && !edge_lag &&
                            abs(raw_hint_ms) >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_OFFSET_MS &&
                            raw_hint_conf >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_CONF &&
                            best_score >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SCORE &&
                            peak_sep >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_SEPARATION &&
                            best_overlap >= min_overlap);

        if(rough_usable) {
            int old = s->align_rough_hint_offset_ms;
            int same_region = 0;
            if(old != 0 &&
               ((old < 0 && raw_hint_ms < 0) || (old > 0 && raw_hint_ms > 0)) &&
               abs(old - raw_hint_ms) <= VJ_AUDIO_SYNC_ROUGH_HINT_TOLERANCE_MS)
                same_region = 1;

            if(!same_region) {
                s->align_rough_hint_offset_ms = raw_hint_ms;
                s->align_rough_hint_conf = raw_hint_conf;
                s->align_rough_hint_count = 1;
            } else {
                int c = s->align_rough_hint_count;
                if(c < 1) c = 1;
                if(c > 5) c = 5;
                s->align_rough_hint_offset_ms = ((old * c) + raw_hint_ms) / (c + 1);
                s->align_rough_hint_conf = (s->align_rough_hint_conf > raw_hint_conf) ?
                                           s->align_rough_hint_conf : raw_hint_conf;
                if(s->align_rough_hint_count < 16)
                    s->align_rough_hint_count++;
            }
            s->align_rough_hint_ms = hint_now_ms;
        } else if(s->align_rough_hint_ms > 0 &&
                  (hint_now_ms - s->align_rough_hint_ms) > VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS)
        {
            sync_track_align_clear_rough_hint_locked(s);
        }

        if(published_locked) {
            export_offset_ms = s->align_offset_smooth_ms;
            export_conf = s->align_conf_smooth;
        } else if(s->align_rough_hint_ms > 0 &&
                  (hint_now_ms - s->align_rough_hint_ms) <= VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS &&
                  s->align_rough_hint_conf >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_CONF)
        {
            export_offset_ms = (double)s->align_rough_hint_offset_ms;
            export_conf = (double)s->align_rough_hint_conf;
        }

        sync_store_i(&s->track_align_locked, published_locked);
        sync_store_i(&s->track_align_offset_ms,
                     sync_clampi((int)(export_offset_ms +
                                       (export_offset_ms >= 0.0 ? 0.5 : -0.5)),
                                 -8000, 8000));
        sync_store_i(&s->track_align_confidence_pct,
                     sync_clampi((int)(export_conf + 0.5), 0, 100));
    }
    sync_store_i(&s->track_align_correction_ppm,
                 sync_clampi((int)(correction_ppm +
                                   (correction_ppm >= 0.0 ? 0.5 : -0.5)),
                             -20000, 20000));
    sync_store_i(&s->track_align_state, state);
    {
        long publish_now_ms = sync_now_ms();
        sync_store_l(&s->track_align_last_update_ms, publish_now_ms);

        if(published_locked) {
            sync_track_align_try_live_snap_locked(
                s,
                raw_offset_ms,
                sync_clampi((int)(s->align_conf_smooth + 0.5), 0, 100),
                publish_now_ms
            );
        } else if(s->align_live_candidate_count > 0 &&
                  s->align_live_candidate_ms > 0 &&
                  (publish_now_ms - s->align_live_candidate_ms) > VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS) {
            sync_track_align_clear_live_candidate_locked(s);
        }
    }

    {
        static int dbg_last_state = -9999;
        static int dbg_last_locked = -9999;
        static int dbg_last_hint = 999999;
        static int dbg_last_conf = -1;
        static long dbg_last_ms = 0;
        long now_dbg_ms = sync_now_ms();
        int locked_dbg = published_locked;
        int hint_dbg = sync_load_i(&s->track_align_offset_ms);
        int conf_dbg = sync_load_i(&s->track_align_confidence_pct);
        int should_log = 0;

        if(dbg_last_locked != locked_dbg)
            should_log = 1;
        else if(locked_dbg &&
                (abs(hint_dbg - dbg_last_hint) >= VJ_AUDIO_SYNC_LIVE_LOCK_LOG_DELTA_MS ||
                 abs(conf_dbg - dbg_last_conf) >= VJ_AUDIO_SYNC_LIVE_LOCK_LOG_CONF_DELTA ||
                 dbg_last_ms == 0 ||
                 (now_dbg_ms - dbg_last_ms) >= VJ_AUDIO_SYNC_QUIET_LIVE_LOG_MS))
            should_log = 1;
        else if(s->align_rough_hint_ms > 0 &&
                (now_dbg_ms - s->align_rough_hint_ms) <= VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS &&
                abs(hint_dbg) >= VJ_AUDIO_SYNC_ROUGH_HINT_MIN_OFFSET_MS &&
                (abs(hint_dbg - dbg_last_hint) >= 500 ||
                 abs(conf_dbg - dbg_last_conf) >= 10 ||
                 dbg_last_ms == 0 ||
                 (now_dbg_ms - dbg_last_ms) >= VJ_AUDIO_SYNC_QUIET_LIVE_LOG_MS))
            should_log = 1;
        else if(state != dbg_last_state && (dbg_last_ms == 0 ||
                (now_dbg_ms - dbg_last_ms) >= VJ_AUDIO_SYNC_QUIET_LIVE_LOG_MS))
            should_log = 1;

        if(should_log) {
            if(locked_dbg) {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[TRACK-ALIGN][LIVE-LOCK] off=%+dms conf=%d%% raw=%+.0fms best=%.3f sep=%.3f src_feat=%d tgt_feat=%d reason=%s",
                           hint_dbg,
                           conf_dbg,
                           raw_offset_ms,
                           best_score,
                           peak_sep,
                           source_count,
                           target_count,
                           quality_reason);
            } else if(s->align_rough_hint_ms > 0 &&
                      (now_dbg_ms - s->align_rough_hint_ms) <= VJ_AUDIO_SYNC_ROUGH_HINT_HOLD_MS) {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[TRACK-ALIGN][LIVE-HINT] hint=%+dms conf=%d%% raw=%+.0fms best=%.3f sep=%.3f count=%d src_feat=%d tgt_feat=%d state=%s reason=%s",
                           hint_dbg,
                           conf_dbg,
                           raw_offset_ms,
                           best_score,
                           peak_sep,
                           s->align_rough_hint_count,
                           source_count,
                           target_count,
                           sync_track_align_state_name_dbg(state),
                           quality_reason);
            } else {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[TRACK-ALIGN][LIVE] state=%s src_feat=%d tgt_feat=%d conf=%.1f%% best=%.3f sep=%.3f reason=%s",
                           sync_track_align_state_name_dbg(state),
                           source_count,
                           target_count,
                           s->align_conf_smooth,
                           best_score,
                           peak_sep,
                           quality_reason);
            }
            dbg_last_state = state;
            dbg_last_locked = locked_dbg;
            dbg_last_hint = hint_dbg;
            dbg_last_conf = conf_dbg;
            dbg_last_ms = now_dbg_ms;
        }
    }

    sync_unlock(s);
}

static void sync_track_align_publish(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    sync_track_align_publish_locked(s);
}


static int sync_track_align_extract_features_from_audio(const uint8_t *data,
                                                       int frames,
                                                       int frame_bytes,
                                                       int channels,
                                                       int bits,
                                                       int sample_rate,
                                                       float *out,
                                                       int max_features)
{
    int sample_bytes;
    int hop;
    int done = 0;
    int out_count = 0;
    double accum = 0.0;
    int accum_frames = 0;
    double prev = 0.0;
    int prev_initialized = 0;

    if(!data || !out || frames <= 0 || frame_bytes <= 0 ||
       channels <= 0 || sample_rate <= 0 || max_features <= 0 ||
       (bits != 8 && bits != 16))
        return 0;

    sample_bytes = bits / 8;
    hop = sync_align_hop_frames(sample_rate);

    while(done < frames && out_count < max_features) {
        int n = frames - done;
        if(n > hop - accum_frames)
            n = hop - accum_frames;
        if(n <= 0)
            n = 1;

        double sum = 0.0;
        for(int i = 0; i < n; i++) {
            const uint8_t *f = data + ((size_t)(done + i) * (size_t)frame_bytes);
            int l, r;
            if(channels == 1) {
                l = r = sync_read_sample(f, sample_bytes);
            } else {
                l = sync_read_sample(f, sample_bytes);
                r = sync_read_sample(f + sample_bytes, sample_bytes);
            }
            double m = ((double)l + (double)r) * (1.0 / 65536.0);
            sum += m * m;
        }

        accum += sum;
        accum_frames += n;
        done += n;

        if(accum_frames >= hop) {
            double level = sqrt(accum / (double)accum_frames);
            double rise;
            double feat;
            if(!prev_initialized) {
                prev = level;
                prev_initialized = 1;
            }
            rise = level - prev;
            if(rise < 0.0)
                rise = 0.0;
            feat = (level * 3.0) + (rise * 10.0);
            feat = sync_clampd(feat, 0.0, 1.0);
            out[out_count++] = (float)feat;
            prev = (0.85 * prev) + (0.15 * level);
            accum = 0.0;
            accum_frames = 0;
        }
    }

    return out_count;
}

static int sync_track_align_master_latency_steps(vj_audio_sync_shared_t *s)
{
    double latency_s = 0.0;

    if(s && sync_load_i(&s->source) == VJ_AUDIO_SYNC_SOURCE_JACK)
        latency_s = vj_jack_get_total_latency();

    if(latency_s < 0.0)
        latency_s = 0.0;

    if(latency_s > 0.500)
        latency_s = 0.500;

    return (int)((latency_s * 1000.0 / (double)VJ_AUDIO_SYNC_ALIGN_HOP_MS) + 0.5);
}

static double sync_track_align_score_feature_vectors(const float *src,
                                                     const float *tgt,
                                                     int n)
{
    double sum_x = 0.0, sum_y = 0.0;
    double sum_xx = 0.0, sum_yy = 0.0, sum_xy = 0.0;
    double nf, cov, vx, vy;

    if(!src || !tgt || n < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES)
        return 0.0;

    for(int i = 0; i < n; i++) {
        double x = (double)src[i];
        double y = (double)tgt[i];
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_yy += y * y;
        sum_xy += x * y;
    }

    nf = (double)n;
    cov = sum_xy - (sum_x * sum_y / nf);
    vx  = sum_xx - (sum_x * sum_x / nf);
    vy  = sum_yy - (sum_y * sum_y / nf);

    if(vx <= 1.0e-9 || vy <= 1.0e-9)
        return 0.0;

    double score = cov / sqrt(vx * vy);
    if(score < 0.0)
        score = 0.0;
    else if(score > 1.0)
        score = 1.0;

    return score;
}


static double sync_track_align_feature_contrast(const float *v, int n)
{
    double mean = 0.0, var = 0.0;
    if(!v || n <= 1)
        return 0.0;
    for(int i = 0; i < n; i++)
        mean += (double)v[i];
    mean /= (double)n;
    for(int i = 0; i < n; i++) {
        double d = (double)v[i] - mean;
        var += d * d;
    }
    var = sqrt(var / (double)n) * 8.0;
    return sync_clampd(var, 0.0, 1.0);
}

static double sync_track_align_score_feature_deltas(const float *src,
                                                    const float *tgt,
                                                    int n)
{
    float dx[VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES];
    float dy[VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES];
    int m;

    if(!src || !tgt || n < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES + 1)
        return 0.0;
    if(n > VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES)
        n = VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES;

    m = n - 1;
    for(int i = 0; i < m; i++) {
        double x = (double)src[i + 1] - (double)src[i];
        double y = (double)tgt[i + 1] - (double)tgt[i];
        if(x < 0.0) x *= 0.35;
        if(y < 0.0) y *= 0.35;
        dx[i] = (float)x;
        dy[i] = (float)y;
    }

    return sync_track_align_score_feature_vectors(dx, dy, m);
}

int vj_audio_sync_track_align_source_ready(vj_audio_sync_shared_t *s,
                                            int min_source_features)
{
    int ready = 0;

    if(!s)
        return 0;

    if(min_source_features < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES)
        min_source_features = VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES;
    if(min_source_features > VJ_AUDIO_SYNC_ALIGN_FEATURES)
        min_source_features = VJ_AUDIO_SYNC_ALIGN_FEATURES;

    sync_lock(s);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN &&
       s->align_source_count >= min_source_features)
        ready = 1;
    sync_unlock(s);

    return ready;
}

int vj_audio_sync_track_align_last_snap(vj_audio_sync_shared_t *s,
                                        long *snap_ms,
                                        int *delta_frames)
{
    long ms = 0;
    int delta = 0;

    if(snap_ms)
        *snap_ms = 0;
    if(delta_frames)
        *delta_frames = 0;

    if(!s)
        return 0;

    sync_lock(s);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        ms = s->align_live_last_snap_ms;
        delta = s->align_live_last_snap_delta;
    }
    sync_unlock(s);

    if(ms <= 0)
        return 0;

    if(snap_ms)
        *snap_ms = ms;
    if(delta_frames)
        *delta_frames = delta;

    return 1;
}

int vj_audio_sync_track_align_probe_target_audio(vj_audio_sync_shared_t *s,
                                                 const uint8_t *src,
                                                 int frames,
                                                 int frame_bytes,
                                                 int channels,
                                                 int bits_per_channel,
                                                 int sample_rate,
                                                 int *confidence_pct)
{
    float tgt[VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES];
    float master[VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES];
    int tgt_count;
    int n;
    int latency_steps = 0;
    double score;
    double contrast;
    double conf;
    double dscore = 0.0;
    double tgt_contrast = 0.0;
    double src_contrast = 0.0;
    double pcm_rms = 0.0;
    double pcm_peak = 0.0;
    int pcm_nonzero = 0;
    int source_count_snapshot = 0;
    int source_needed = 0;
    const char *reject_reason = "ok";
    sync_track_align_feature_stats_t tgt_stats;
    sync_track_align_feature_stats_t master_stats;

    sync_track_align_stats_clear(&tgt_stats);
    sync_track_align_stats_clear(&master_stats);

    if(confidence_pct)
        *confidence_pct = 0;

    if(!s || !src || frames <= 0 || frame_bytes <= 0 || channels <= 0)
        return 0;

    sync_track_align_pcm_stats(src,
                               frames,
                               frame_bytes,
                               channels,
                               bits_per_channel,
                               &pcm_rms,
                               &pcm_peak,
                               &pcm_nonzero);

    tgt_count = sync_track_align_extract_features_from_audio(src,
                                                             frames,
                                                             frame_bytes,
                                                             channels,
                                                             bits_per_channel,
                                                             sample_rate,
                                                             tgt,
                                                             VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES);
    sync_track_align_feature_stats_array(tgt, tgt_count, &tgt_stats);

    if(tgt_count < VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES) {
        reject_reason = "few-target-features";
        if(sync_track_align_probe_debug_should_log(reject_reason, 0))
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[TRACK-ALIGN][PROBE] reject=%s frames=%d pcm_rms=%.6f pcm_peak=%.6f pcm_nonzero=%d/%d tgt_feat=%d tgt_mean=%.5f tgt_sd=%.8f need_feat=%d",
                       reject_reason, frames, pcm_rms, pcm_peak, pcm_nonzero, frames,
                       tgt_count, tgt_stats.mean, tgt_stats.sd,
                       VJ_AUDIO_SYNC_ALIGN_MIN_FEATURES);
        return 0;
    }

    latency_steps = sync_track_align_master_latency_steps(s);

    sync_lock(s);
    source_count_snapshot = s->align_source_count;
    source_needed = tgt_count + latency_steps;
    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
       s->align_source_count < source_needed)
    {
        int mode_snapshot = sync_load_i(&s->mode);
        sync_unlock(s);
        reject_reason = (mode_snapshot != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) ?
                        "wrong-mode" : "wait-source-features";
        if(sync_track_align_probe_debug_should_log(reject_reason, 0))
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[TRACK-ALIGN][PROBE] reject=%s frames=%d pcm_rms=%.6f pcm_peak=%.6f pcm_nonzero=%d/%d tgt_feat=%d tgt_mean=%.5f tgt_sd=%.8f src_feat=%d need_src=%d latency_steps=%d mode=%d",
                       reject_reason, frames, pcm_rms, pcm_peak, pcm_nonzero, frames,
                       tgt_count, tgt_stats.mean, tgt_stats.sd,
                       source_count_snapshot, source_needed, latency_steps, mode_snapshot);
        return 0;
    }

    n = tgt_count;
    if(n > s->align_source_count - latency_steps)
        n = s->align_source_count - latency_steps;
    if(n > VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES)
        n = VJ_AUDIO_SYNC_ALIGN_PROBE_MAX_FEATURES;

    for(int i = 0; i < n; i++) {
        master[n - 1 - i] = sync_align_ring_get(s->align_source_feat,
                                                s->align_source_pos,
                                                s->align_source_count,
                                                i + latency_steps);
    }
    sync_unlock(s);

    sync_track_align_feature_stats_array(master, n, &master_stats);

    score = sync_track_align_score_feature_vectors(master, tgt, n);

    {
        tgt_contrast = sync_track_align_feature_contrast(tgt, n);
        src_contrast = sync_track_align_feature_contrast(master, n);
        dscore = sync_track_align_score_feature_deltas(master, tgt, n);
        contrast = tgt_contrast < src_contrast ? tgt_contrast : src_contrast;
        if(contrast < VJ_AUDIO_SYNC_ALIGN_PROBE_MIN_CONTRAST)
            contrast *= 0.35;
        score = (0.68 * score) + (0.32 * dscore);
    }

    conf = score * 100.0;
    conf *= (0.35 + 0.65 * contrast);

    if(confidence_pct)
        *confidence_pct = sync_clampi((int)(conf + 0.5), 0, 100);

    if(conf < 1.0)
        reject_reason = (contrast < VJ_AUDIO_SYNC_ALIGN_PROBE_MIN_CONTRAST) ?
                        "low-contrast" : "low-confidence";

    if(sync_track_align_probe_debug_should_log(reject_reason,
                                               sync_clampi((int)(conf + 0.5), 0, 100)))
    {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][PROBE] %s frames=%d pcm_rms=%.6f pcm_peak=%.6f pcm_nonzero=%d/%d feat=%d n=%d src_feat=%d latency_steps=%d score=%.3f dscore=%.3f conf=%.1f%% contrast=%.3f src_contrast=%.3f tgt_contrast=%.3f src_mean=%.5f src_sd=%.8f src_minmax=%.5f..%.5f tgt_mean=%.5f tgt_sd=%.8f tgt_minmax=%.5f..%.5f",
                   (conf >= 1.0) ? "ok" : reject_reason,
                   frames, pcm_rms, pcm_peak, pcm_nonzero, frames,
                   tgt_count, n, source_count_snapshot, latency_steps,
                   score, dscore, conf, contrast, src_contrast, tgt_contrast,
                   master_stats.mean, master_stats.sd, master_stats.min, master_stats.max,
                   tgt_stats.mean, tgt_stats.sd, tgt_stats.min, tgt_stats.max);
    }

    return (conf >= 1.0) ? 1 : 0;
}

void vj_audio_sync_track_align_offer_snap(vj_audio_sync_shared_t *s,
                                          int delta_frames,
                                          int confidence_pct)
{
    if(!s)
        return;

    if(delta_frames == 0 || confidence_pct < 1) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] ignored invalid delta=%+d conf=%d%%",
                   delta_frames, confidence_pct);
        return;
    }

    sync_lock(s);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        if(sync_track_align_offer_snap_locked(s,
                                              delta_frames,
                                              sync_clampi(confidence_pct, 0, 100),
                                              "external"))
            sync_track_align_clear_live_candidate_locked(s);
    }
    else {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] ignored mode=%d delta=%+d conf=%d%%",
                   sync_load_i(&s->mode), delta_frames,
                   sync_clampi(confidence_pct, 0, 100));
    }
    sync_unlock(s);
}

int vj_audio_sync_track_align_offer_servo_nudge(vj_audio_sync_shared_t *s,
                                                int delta_frames,
                                                int confidence_pct)
{
    int accepted = 0;

    if(!s)
        return 0;

    if((delta_frames != -1 && delta_frames != 1) || confidence_pct < 1) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] ignored invalid servo delta=%+d conf=%d%%",
                   delta_frames, confidence_pct);
        return 0;
    }

    sync_lock(s);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        accepted = sync_track_align_offer_servo_locked(s,
                                                       delta_frames,
                                                       sync_clampi(confidence_pct, 0, 100),
                                                       "settled-servo");
        if(accepted)
            sync_track_align_clear_live_candidate_locked(s);
    }
    else {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-OFFER] ignored mode=%d servo delta=%+d conf=%d%%",
                   sync_load_i(&s->mode), delta_frames,
                   sync_clampi(confidence_pct, 0, 100));
    }
    sync_unlock(s);

    return accepted;
}

int vj_audio_sync_track_align_consume_snap(vj_audio_sync_shared_t *s,
                                           int *delta_frames,
                                           int *confidence_pct)
{
    int pending;

    if(delta_frames)
        *delta_frames = 0;
    if(confidence_pct)
        *confidence_pct = 0;

    if(!s)
        return 0;

    sync_lock(s);
    pending = sync_load_i(&s->track_align_snap_pending);
    if(pending) {
        int d = sync_load_i(&s->track_align_snap_delta_frames);
        int c = sync_load_i(&s->track_align_snap_confidence_pct);
        if(delta_frames)
            *delta_frames = d;
        if(confidence_pct)
            *confidence_pct = c;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][SNAP-CONSUME] pending=1 delta=%+d conf=%d%% seq=%d",
                   d, c, sync_load_i(&s->track_align_snap_seq));
        sync_store_i(&s->track_align_snap_pending, 0);
        sync_store_i(&s->track_align_snap_delta_frames, 0);
        sync_store_i(&s->track_align_snap_confidence_pct, 0);
        sync_track_align_clear_live_candidate_locked(s);
    }
    sync_unlock(s);

    return pending ? 1 : 0;
}

static void sync_track_align_push_block(vj_audio_sync_shared_t *s,
                                        const uint8_t *data,
                                        int frames,
                                        int frame_bytes,
                                        int channels,
                                        int bits,
                                        int sample_rate,
                                        int is_target)
{
    int sample_bytes = bits / 8;
    int hop;
    int done = 0;

    if(!s || !data || frames <= 0 || frame_bytes <= 0 ||
       channels <= 0 || sample_rate <= 0 || (bits != 8 && bits != 16))
        return;

    hop = sync_align_hop_frames(sample_rate);

    sync_lock(s);
    while(done < frames) {
        int n = frames - done;
        double *accum = is_target ? &s->align_target_accum : &s->align_source_accum;
        int *accum_frames = is_target ? &s->align_target_accum_frames : &s->align_source_accum_frames;
        if(n > hop - *accum_frames)
            n = hop - *accum_frames;
        if(n <= 0)
            n = 1;

        double sum = 0.0;
        for(int i = 0; i < n; i++) {
            const uint8_t *f = data + ((size_t)(done + i) * (size_t)frame_bytes);
            int l, r;
            if(channels == 1) {
                l = r = sync_read_sample(f, sample_bytes);
            } else {
                l = sync_read_sample(f, sample_bytes);
                r = sync_read_sample(f + sample_bytes, sample_bytes);
            }
            double m = ((double)l + (double)r) * (1.0 / 65536.0);
            sum += m * m;
        }

        *accum += sum;
        *accum_frames += n;
        done += n;

        if(*accum_frames >= hop) {
            double level = sqrt(*accum / (double)(*accum_frames));
            sync_track_align_append_feature_locked(s, is_target, level, sample_rate);
            *accum = 0.0;
            *accum_frames = 0;

            sync_unlock(s);
            sync_track_align_publish(s);
            sync_lock(s);
        }
    }
    sync_unlock(s);
}

static inline int sync_ring_index(const vj_audio_sync_shared_t *s, long long frame)
{
    long long r;
    if(!s || s->ring_frames <= 0)
        return 0;
    r = frame % (long long)s->ring_frames;
    if(r < 0)
        r += s->ring_frames;
    return (int)r;
}

static void sync_clear_clock(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    s->fast_energy = 0.0;
    s->slow_energy = 0.0;
    s->envelope = 0.0;
    s->last_level = 0.0;
    s->beat_period_ms = 0.0;
    s->last_analysis_ms = 0;
    sync_store_i(&s->level_q15, 0);
    sync_store_i(&s->envelope_q15, 0);
    sync_store_i(&s->transient_q15, 0);
    sync_store_i(&s->source_bpm_q8, 0);
    sync_store_i(&s->source_phase_q15, 0);
    sync_store_i(&s->confidence_q15, 0);
    sync_store_l(&s->last_hit_ms, 0);
}

static void sync_clear_target_clock(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    s->target_fast_energy = 0.0;
    s->target_slow_energy = 0.0;
    s->target_envelope = 0.0;
    s->target_last_level = 0.0;
    s->target_beat_period_ms = 0.0;
    s->target_last_analysis_ms = 0;
    s->target_last_hit_ms = 0;
    sync_store_i(&s->target_bpm_q8, 0);
    sync_store_i(&s->target_phase_q15, 0);
    sync_store_i(&s->target_confidence_q15, 0);
}

static void sync_clear_monitor_transport_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    s->monitor_read_pos = 0.0;
    s->monitor_read_valid = 0;
    s->monitor_last_l = 0;
    s->monitor_last_r = 0;
    s->monitor_last_valid = 0;
    s->monitor_resyncs = 0;
    s->monitor_last_debug_ms = 0;
}

static void sync_monitor_hold_last(vj_audio_sync_shared_t *s,
                                   uint8_t *dst,
                                   int dst_frames,
                                   int dst_frame_bytes,
                                   int dst_channels)
{
    int l = 0;
    int r = 0;

    if(s && s->monitor_last_valid) {
        l = s->monitor_last_l;
        r = s->monitor_last_r;
    }

    for(int i = 0; i < dst_frames; i++) {
        uint8_t *df = dst + ((size_t)i * (size_t)dst_frame_bytes);
        if(dst_channels == 1) {
            sync_write_sample(df, 2, l);
        } else {
            sync_write_sample(df, 2, l);
            sync_write_sample(df + 2, 2, r);
        }
    }
}

void vj_audio_sync_init(vj_audio_sync_shared_t *s, int input_channels)
{
    if(!s)
        return;

    memset(s, 0, sizeof(*s));

    if(input_channels < 1)
        input_channels = 2;
    else if(input_channels > 2)
        input_channels = 2;

    sync_store_i(&s->input_channels_request, input_channels);
    sync_store_i(&s->mode, VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
    sync_store_i(&s->source, VJ_AUDIO_SYNC_SOURCE_JACK);
    sync_store_i(&s->target_mode, VJ_AUDIO_SYNC_TARGET_MANUAL);
    sync_store_i(&s->max_correction_pct, 4);
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_track_align_clear_locked(s);
    sync_store_i(&s->initialized, 1);
}

void vj_audio_sync_free(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    sync_lock(s);
    if(s->ring) {
        free(s->ring);
        s->ring = NULL;
    }
    if(s->target_ring) {
        free(s->target_ring);
        s->target_ring = NULL;
    }
    s->target_ring_frames = 0;
    s->target_ring_write_frame = 0;
    s->target_write_frame_abs = 0;
    s->target_read_frame_abs = 0;
    s->target_channels = 0;
    s->target_bytes_per_frame = 0;
    s->target_bits_per_channel = 0;
    s->target_sample_rate = 0;
    s->ring_frames = 0;
    s->ring_write_frame = 0;
    s->write_frame_abs = 0;
    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_pos = 0.0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_IDLE);
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_clear_monitor_transport_locked(s);
    sync_track_align_clear_locked(s);
    sync_unlock(s);
    sync_wav_side_clear(s);
}

void vj_audio_sync_request_stop(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    sync_store_i(&s->stop_request, 1);
    sync_store_i(&s->enabled, 0);
}

static int sync_prepare_ring_locked(vj_audio_sync_shared_t *s,
                                    int channels,
                                    int bits,
                                    int rate)
{
    int sample_bytes;
    int frame_bytes;
    int ring_frames;
    size_t ring_bytes;
    uint8_t *ring;

    if(!s || channels <= 0 || channels > 2 || rate <= 0)
        return 0;

    if(bits != 8 && bits != 16)
        return 0;

    sample_bytes = bits / 8;
    frame_bytes = sample_bytes * channels;
    ring_frames = rate * VJ_AUDIO_SYNC_RING_SECONDS;
    ring_bytes = (size_t)ring_frames * (size_t)frame_bytes;

    if(ring_bytes < VJ_AUDIO_SYNC_MIN_RING_BYTES) {
        ring_bytes = VJ_AUDIO_SYNC_MIN_RING_BYTES;
        ring_frames = (int)(ring_bytes / (size_t)frame_bytes);
    }

    if(s->ring &&
       s->ring_frames >= ring_frames &&
       sync_load_i(&s->channels) == channels &&
       sync_load_i(&s->bytes_per_frame) == frame_bytes &&
       sync_load_i(&s->bits_per_channel) == bits &&
       sync_load_i(&s->sample_rate) == rate)
        return 1;

    ring = (uint8_t*)vj_malloc(ring_bytes);
    if(!ring)
        return 0;

    if(s->ring)
        free(s->ring);

    s->ring = ring;
    s->ring_frames = ring_frames;
    s->ring_write_frame = 0;
    s->write_frame_abs = 0;
    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_pos = 0.0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
    sync_clear_monitor_transport_locked(s);
    s->bridge_ratio_smooth = 1.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);

    sync_track_align_clear_locked(s);

    sync_store_i(&s->channels, channels);
    sync_store_i(&s->bytes_per_frame, frame_bytes);
    sync_store_i(&s->bits_per_channel, bits);
    sync_store_i(&s->sample_rate, rate);

    s->record_channels = channels;
    s->record_bytes_per_frame = frame_bytes;
    s->record_bits_per_channel = bits;
    s->record_sample_rate = rate;

    sync_clear_clock(s);
    return 1;
}

static int sync_prepare_ring(vj_audio_sync_shared_t *s,
                             int channels,
                             int bits,
                             int rate)
{
    int ok;
    sync_lock(s);
    ok = sync_prepare_ring_locked(s, channels, bits, rate);
    sync_unlock(s);
    return ok;
}

static void sync_publish_audio(vj_audio_sync_shared_t *s,
                               const uint8_t *src,
                               int frames,
                               int channels,
                               int bits,
                               int rate)
{
    int frame_bytes;
    int remaining;

    if(!s || !src || frames <= 0 || channels <= 0 || rate <= 0)
        return;

    if(!sync_prepare_ring(s, channels, bits, rate))
        return;

    frame_bytes = sync_load_i(&s->bytes_per_frame);
    if(frame_bytes <= 0)
        return;

    sync_lock(s);

    if(frames > s->ring_frames) {
        src += (size_t)(frames - s->ring_frames) * (size_t)frame_bytes;
        frames = s->ring_frames;
        sync_add_l(&s->overruns, 1);
    }

    remaining = frames;
    while(remaining > 0) {
        int pos = s->ring_write_frame;
        int room = s->ring_frames - pos;
        int n = (remaining < room) ? remaining : room;

        veejay_memcpy(s->ring + ((size_t)pos * (size_t)frame_bytes),
                      src,
                      (size_t)n * (size_t)frame_bytes);

        src += (size_t)n * (size_t)frame_bytes;
        pos += n;
        if(pos >= s->ring_frames)
            pos = 0;

        s->ring_write_frame = pos;
        s->write_frame_abs += n;
        remaining -= n;
    }

    sync_store_i(&s->open, 1);
    sync_unlock(s);
}


static int sync_prepare_target_ring_locked(vj_audio_sync_shared_t *s,
                                           int channels,
                                           int bits,
                                           int rate)
{
    int sample_bytes;
    int frame_bytes;
    int ring_frames;
    size_t ring_bytes;
    uint8_t *ring;

    if(!s || channels <= 0 || channels > 2 || rate <= 0)
        return 0;
    if(bits != 8 && bits != 16)
        return 0;

    sample_bytes = bits / 8;
    frame_bytes = sample_bytes * channels;
    ring_frames = rate * VJ_AUDIO_SYNC_TARGET_RING_SECONDS;
    ring_bytes = (size_t)ring_frames * (size_t)frame_bytes;

    if(ring_bytes < VJ_AUDIO_SYNC_TARGET_MIN_RING_BYTES) {
        ring_bytes = VJ_AUDIO_SYNC_TARGET_MIN_RING_BYTES;
        ring_frames = (int)(ring_bytes / (size_t)frame_bytes);
    }

    if(s->target_ring &&
       s->target_ring_frames >= ring_frames &&
       s->target_channels == channels &&
       s->target_bytes_per_frame == frame_bytes &&
       s->target_bits_per_channel == bits &&
       s->target_sample_rate == rate)
        return 1;

    ring = (uint8_t*)vj_malloc(ring_bytes);
    if(!ring)
        return 0;

    if(s->target_ring)
        free(s->target_ring);

    s->target_ring = ring;
    s->target_ring_frames = ring_frames;
    s->target_ring_write_frame = 0;
    s->target_write_frame_abs = 0;
    s->target_read_frame_abs = 0;
    s->target_process_frame_abs = 0;
    s->target_channels = channels;
    s->target_bytes_per_frame = frame_bytes;
    s->target_bits_per_channel = bits;
    s->target_sample_rate = rate;
    sync_store_l(&s->target_queue_overruns, 0);
    sync_store_l(&s->target_queue_reads, 0);
    return 1;
}

static void sync_clear_target_queue_locked(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    s->target_ring_write_frame = 0;
    s->target_write_frame_abs = 0;
    s->target_read_frame_abs = 0;
    s->target_process_frame_abs = 0;
}

static int sync_enqueue_target_audio(vj_audio_sync_shared_t *s,
                                     const uint8_t *src,
                                     int frames,
                                     int frame_bytes,
                                     int channels,
                                     int bits,
                                     int sample_rate)
{
    int remaining;

    if(!s || !src || frames <= 0 || frame_bytes <= 0 ||
       channels <= 0 || sample_rate <= 0)
        return 0;

    if(!sync_try_lock_for_us(s, 50)) {
        long drops = sync_add_l(&s->target_queue_overruns, 1);
        static long dbg_last_target_enqueue_drop_ms = 0;
        long now_dbg_ms = sync_now_ms();
        if(dbg_last_target_enqueue_drop_ms == 0 ||
           (now_dbg_ms - dbg_last_target_enqueue_drop_ms) >= 1000 ||
           drops <= 8 || (drops % 16) == 0)
        {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[TRACK-ALIGN][TARGET-Q] enqueue drop reason=lock-contention frames=%d fmt=%dch/%dbit/%dHz drops=%ld wait_us=50",
                       frames, channels, bits, sample_rate, drops);
            dbg_last_target_enqueue_drop_ms = now_dbg_ms;
        }
        return 0;
    }

    if(!sync_prepare_target_ring_locked(s, channels, bits, sample_rate)) {
        sync_unlock(s);
        return 0;
    }

    if(frame_bytes != s->target_bytes_per_frame) {
        sync_unlock(s);
        return 0;
    }

    if(frames > s->target_ring_frames) {
        src += (size_t)(frames - s->target_ring_frames) * (size_t)frame_bytes;
        frames = s->target_ring_frames;
        sync_add_l(&s->target_queue_overruns, 1);
    }

    remaining = frames;
    while(remaining > 0) {
        int pos = s->target_ring_write_frame;
        int room = s->target_ring_frames - pos;
        int n = (remaining < room) ? remaining : room;

        veejay_memcpy(s->target_ring + ((size_t)pos * (size_t)frame_bytes),
                      src,
                      (size_t)n * (size_t)frame_bytes);

        src += (size_t)n * (size_t)frame_bytes;
        pos += n;
        if(pos >= s->target_ring_frames)
            pos = 0;

        s->target_ring_write_frame = pos;
        s->target_write_frame_abs += n;
        remaining -= n;
    }

    if((s->target_write_frame_abs - s->target_read_frame_abs) >
       (long long)s->target_ring_frames)
    {
        long long old_read = s->target_read_frame_abs;
        s->target_read_frame_abs =
            s->target_write_frame_abs - (long long)s->target_ring_frames;
        if(s->target_process_frame_abs < s->target_read_frame_abs)
            s->target_process_frame_abs = s->target_read_frame_abs;
        sync_add_l(&s->target_queue_overruns, 1);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[TRACK-ALIGN][TARGET-Q] overflow drop=%lldfr write=%lld read:%lld->%lld process=%lld ring=%dfr",
                   s->target_read_frame_abs - old_read,
                   s->target_write_frame_abs,
                   old_read,
                   s->target_read_frame_abs,
                   s->target_process_frame_abs,
                   s->target_ring_frames);
    }

    sync_unlock(s);
    return frames;
}

static int sync_dequeue_target_audio(vj_audio_sync_shared_t *s,
                                     uint8_t **buffer,
                                     int *buffer_size,
                                     int *channels,
                                     int *bits,
                                     int *sample_rate,
                                     int *frame_bytes)
{
    long long new_avail;
    long long retained_avail;
    int frames;
    int bytes;
    int pos;
    int first;

    if(!s || !buffer || !buffer_size || !channels || !bits ||
       !sample_rate || !frame_bytes)
        return 0;

    if(!sync_try_lock(s))
        return 0;

    if(!s->target_ring || s->target_ring_frames <= 0 ||
       s->target_bytes_per_frame <= 0 ||
       s->target_write_frame_abs <= s->target_process_frame_abs)
    {
        sync_unlock(s);
        return 0;
    }

    retained_avail = s->target_write_frame_abs - s->target_read_frame_abs;
    if(retained_avail > (long long)s->target_ring_frames) {
        s->target_read_frame_abs =
            s->target_write_frame_abs - (long long)s->target_ring_frames;
        if(s->target_process_frame_abs < s->target_read_frame_abs)
            s->target_process_frame_abs = s->target_read_frame_abs;
        retained_avail = s->target_write_frame_abs - s->target_read_frame_abs;
        sync_add_l(&s->target_queue_overruns, 1);
    }

    if(s->target_process_frame_abs < s->target_read_frame_abs)
        s->target_process_frame_abs = s->target_read_frame_abs;
    if(s->target_process_frame_abs > s->target_write_frame_abs)
        s->target_process_frame_abs = s->target_write_frame_abs;

    new_avail = s->target_write_frame_abs - s->target_process_frame_abs;
    frames = (new_avail > VJ_AUDIO_SYNC_TARGET_PROCESS_MAX_FRAMES) ?
             VJ_AUDIO_SYNC_TARGET_PROCESS_MAX_FRAMES : (int)new_avail;
    if(frames <= 0) {
        sync_unlock(s);
        return 0;
    }

    bytes = frames * s->target_bytes_per_frame;
    if(*buffer_size < bytes) {
        uint8_t *nb = (uint8_t*)vj_malloc((size_t)bytes);
        if(!nb) {
            sync_unlock(s);
            return 0;
        }
        if(*buffer)
            free(*buffer);
        *buffer = nb;
        *buffer_size = bytes;
    }

    pos = (int)(s->target_process_frame_abs % (long long)s->target_ring_frames);
    if(pos < 0)
        pos += s->target_ring_frames;

    first = s->target_ring_frames - pos;
    if(first > frames)
        first = frames;

    veejay_memcpy(*buffer,
                  s->target_ring + ((size_t)pos * (size_t)s->target_bytes_per_frame),
                  (size_t)first * (size_t)s->target_bytes_per_frame);

    if(frames > first) {
        veejay_memcpy(*buffer + ((size_t)first * (size_t)s->target_bytes_per_frame),
                      s->target_ring,
                      (size_t)(frames - first) * (size_t)s->target_bytes_per_frame);
    }

    s->target_process_frame_abs += frames;
    *channels = s->target_channels;
    *bits = s->target_bits_per_channel;
    *sample_rate = s->target_sample_rate;
    *frame_bytes = s->target_bytes_per_frame;
    sync_add_l(&s->target_queue_reads, 1);

    {
        static long dbg_last_target_q_ms = 0;
        static long dbg_last_target_q_drops = -1;
        long now_dbg_ms = sync_now_ms();
        long long after_new_avail = s->target_write_frame_abs - s->target_process_frame_abs;
        long long before_new_avail = after_new_avail + frames;
        long long retained_after = s->target_write_frame_abs - s->target_read_frame_abs;
        long reads_dbg = sync_load_l(&s->target_queue_reads);
        long drops_dbg = sync_load_l(&s->target_queue_overruns);
        int before_ms = 0;
        int retained_ms = 0;
        if(s->target_sample_rate > 0) {
            before_ms = (int)((before_new_avail * 1000LL) / (long long)s->target_sample_rate);
            retained_ms = (int)((retained_after * 1000LL) / (long long)s->target_sample_rate);
        }
        if(dbg_last_target_q_ms == 0 ||
           (now_dbg_ms - dbg_last_target_q_ms) >= 1000 ||
           retained_ms < VJ_AUDIO_SYNC_TARGET_MIN_HISTORY_MS ||
           retained_ms > VJ_AUDIO_SYNC_TARGET_WANTED_HISTORY_MS ||
           drops_dbg != dbg_last_target_q_drops)
        {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[TRACK-ALIGN][TARGET-Q] processed=%d new_before=%lldfr/%dms new_after=%lldfr retained=%lldfr/%dms ring=%dfr reads=%ld drops=%ld fmt=%dch/%dbit/%dHz",
                       frames,
                       before_new_avail,
                       before_ms,
                       after_new_avail,
                       retained_after,
                       retained_ms,
                       s->target_ring_frames,
                       reads_dbg,
                       drops_dbg,
                       s->target_channels,
                       s->target_bits_per_channel,
                       s->target_sample_rate);
            dbg_last_target_q_ms = now_dbg_ms;
            dbg_last_target_q_drops = drops_dbg;
        }
    }

    sync_unlock(s);
    return frames;
}


static void sync_update_clock_from_block(vj_audio_sync_shared_t *s,
                                         const uint8_t *data,
                                         int frames,
                                         int frame_bytes,
                                         int channels,
                                         int bits,
                                         int sample_rate)
{
    const int sample_bytes = bits / 8;
    double sum = 0.0;
    double level;
    double fast;
    double slow;
    double transient;
    double confidence;
    long now = sync_now_ms();
    long last_hit;

    if(!s || !data || frames <= 0 || frame_bytes <= 0 || channels <= 0)
        return;

    for(int i = 0; i < frames; i++) {
        const uint8_t *f = data + ((size_t)i * (size_t)frame_bytes);
        int l, r;
        if(channels == 1) {
            l = r = sync_read_sample(f, sample_bytes);
        } else {
            l = sync_read_sample(f, sample_bytes);
            r = sync_read_sample(f + sample_bytes, sample_bytes);
        }
        {
            double m = ((double)l + (double)r) * (1.0 / 65536.0);
            sum += m * m;
        }
    }

    level = sqrt(sum / (double)frames);
    s->envelope = (level > s->envelope) ? (0.65 * s->envelope + 0.35 * level)
                                        : (0.94 * s->envelope + 0.06 * level);

    fast = (s->fast_energy <= 0.0) ? level : (0.55 * s->fast_energy + 0.45 * level);
    slow = (s->slow_energy <= 0.0) ? level : (0.985 * s->slow_energy + 0.015 * level);
    s->fast_energy = fast;
    s->slow_energy = slow;

    transient = 0.0;
    if(slow > 0.00001)
        transient = sync_clampd((fast / slow - 1.0) * 0.85, 0.0, 1.0);

    sync_store_i(&s->level_q15, sync_q15(level * 4.0));
    sync_store_i(&s->envelope_q15, sync_q15(s->envelope * 4.0));
    sync_store_i(&s->transient_q15, sync_q15(transient));

    last_hit = sync_load_l(&s->last_hit_ms);

    if(level > 0.012 && transient > 0.20) {
        long interval = last_hit > 0 ? now - last_hit : 0;
        int accept = 0;

        if(last_hit <= 0)
            accept = 1;
        else if(interval >= VJ_AUDIO_SYNC_MIN_PERIOD_MS && interval <= VJ_AUDIO_SYNC_MAX_PERIOD_MS)
            accept = 1;
        else if(s->beat_period_ms > 1.0 && interval > (long)(s->beat_period_ms * 0.55))
            accept = 1;

        if(accept) {
            if(interval >= VJ_AUDIO_SYNC_MIN_PERIOD_MS && interval <= VJ_AUDIO_SYNC_MAX_PERIOD_MS) {
                if(s->beat_period_ms <= 1.0)
                    s->beat_period_ms = (double)interval;
                else
                    s->beat_period_ms = (0.82 * s->beat_period_ms) + (0.18 * (double)interval);

                sync_store_i(&s->source_bpm_q8, sync_q8_bpm(60000.0 / s->beat_period_ms));
            }

            sync_store_l(&s->last_hit_ms, now);
            sync_add_l(&s->hits, 1);
        }
    }

    last_hit = sync_load_l(&s->last_hit_ms);
    if(last_hit > 0 && s->beat_period_ms > 1.0) {
        double phase = fmod((double)(now - last_hit) / s->beat_period_ms, 1.0);
        if(phase < 0.0)
            phase += 1.0;
        sync_store_i(&s->source_phase_q15, sync_q15(phase));

        confidence = sync_from_q15(sync_load_i(&s->confidence_q15));
        confidence += (transient > 0.15) ? 0.025 : 0.002;
        confidence *= 0.999;
        sync_store_i(&s->confidence_q15, sync_q15(confidence));
    } else {
        confidence = sync_from_q15(sync_load_i(&s->confidence_q15));
        sync_store_i(&s->confidence_q15, sync_q15(confidence * 0.995));
    }

    (void)sample_rate;
    s->last_level = level;
    s->last_analysis_ms = now;
}

static void sync_update_target_clock_from_block(vj_audio_sync_shared_t *s,
                                                const uint8_t *data,
                                                int frames,
                                                int frame_bytes,
                                                int channels,
                                                int bits,
                                                int sample_rate)
{
    const int sample_bytes = bits / 8;
    double sum = 0.0;
    double level;
    double fast;
    double slow;
    double transient;
    double confidence;
    long now = sync_now_ms();
    long last_hit;

    if(!s || !data || frames <= 0 || frame_bytes <= 0 || channels <= 0)
        return;

    for(int i = 0; i < frames; i++) {
        const uint8_t *f = data + ((size_t)i * (size_t)frame_bytes);
        int l, r;
        if(channels == 1) {
            l = r = sync_read_sample(f, sample_bytes);
        } else {
            l = sync_read_sample(f, sample_bytes);
            r = sync_read_sample(f + sample_bytes, sample_bytes);
        }
        {
            double m = ((double)l + (double)r) * (1.0 / 65536.0);
            sum += m * m;
        }
    }

    level = sqrt(sum / (double)frames);

    s->target_envelope = (level > s->target_envelope)
        ? (0.80 * s->target_envelope + 0.20 * level)
        : (0.97 * s->target_envelope + 0.03 * level);

    fast = (s->target_fast_energy <= 0.0) ? level
        : (0.70 * s->target_fast_energy + 0.30 * level);
    slow = (s->target_slow_energy <= 0.0) ? level
        : (0.992 * s->target_slow_energy + 0.008 * level);
    s->target_fast_energy = fast;
    s->target_slow_energy = slow;

    transient = 0.0;
    if(slow > 0.00001)
        transient = sync_clampd((fast / slow - 1.0) * 0.70, 0.0, 1.0);

    last_hit = s->target_last_hit_ms;

    double min_level = 0.020;
    double min_transient = 0.32;

    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW) {
        min_level = 0.008;
        min_transient = 0.16;
    }

    if(level > min_level && transient > min_transient) {
        long interval = last_hit > 0 ? now - last_hit : 0;
        int accept = 0;

        if(last_hit <= 0)
            accept = 1;
        else if(interval >= VJ_AUDIO_SYNC_MIN_PERIOD_MS && interval <= VJ_AUDIO_SYNC_MAX_PERIOD_MS)
            accept = 1;
        else if(s->target_beat_period_ms > 1.0 &&
                interval > (long)(s->target_beat_period_ms * 0.70))
            accept = 1;

        if(accept) {
            if(interval >= VJ_AUDIO_SYNC_MIN_PERIOD_MS && interval <= VJ_AUDIO_SYNC_MAX_PERIOD_MS) {
                double candidate = (double)interval;

                if(s->target_beat_period_ms <= 1.0) {
                    s->target_beat_period_ms = candidate;
                } else {
                    double ratio = candidate / s->target_beat_period_ms;

                    if(ratio < 0.58)
                        candidate *= 2.0;
                    else if(ratio > 1.72)
                        candidate *= 0.5;

                    ratio = candidate / s->target_beat_period_ms;
                    if(ratio > 0.68 && ratio < 1.47) {
                        s->target_beat_period_ms =
                            (0.94 * s->target_beat_period_ms) +
                            (0.06 * candidate);
                    } else {
                        confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
                        sync_store_i(&s->target_confidence_q15, sync_q15(confidence * 0.92));
                    }
                }

                if(s->target_beat_period_ms > 1.0) {
                    sync_store_i(&s->target_bpm_q8,
                                 sync_q8_bpm(60000.0 / s->target_beat_period_ms));
                }
            }

            s->target_last_hit_ms = now;
        }
    }

    last_hit = s->target_last_hit_ms;
    if(last_hit > 0 && s->target_beat_period_ms > 1.0) {
        double phase = fmod((double)(now - last_hit) / s->target_beat_period_ms, 1.0);
        if(phase < 0.0)
            phase += 1.0;
        sync_store_i(&s->target_phase_q15, sync_q15(phase));

        confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
        confidence += (transient > (min_transient * 0.875)) ? 0.006 : 0.0005;
        confidence *= 0.9985;
        sync_store_i(&s->target_confidence_q15, sync_q15(confidence));
    } else {
        confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
        sync_store_i(&s->target_confidence_q15, sync_q15(confidence * 0.990));
    }

    (void)sample_rate;
    s->target_last_level = level;
    s->target_last_analysis_ms = now;
}

static int sync_configure_jack(vj_audio_sync_shared_t *s)
{
    int req_ch;
    int ch;
    int rate;
    int bits;
    unsigned long bpf;
    static long last_fail_ms = 0;

    if(!s)
        return 0;

    req_ch = sync_load_i(&s->input_channels_request);
    if(req_ch < 1)
        req_ch = 2;
    else if(req_ch > 2)
        req_ch = 2;

    if(!vj_jack_is_running()) {
        vj_jack_initialize();
        if(!vj_jack_init_capture(req_ch, 16, 0)) {
            long now = sync_now_ms();
            if(last_fail_ms == 0 || (now - last_fail_ms) >= 2000) {
                last_fail_ms = now;
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-SYNC] unable to initialize JACK capture ports (%d channel%s)",
                           req_ch, req_ch == 1 ? "" : "s");
            }
            sync_store_i(&s->open, 0);
            return 0;
        }
        vj_jack_enable();
    }
    else if(!vj_jack_has_input()) {
        long now = sync_now_ms();

        if(!vj_jack_has_output()) {
            vj_jack_stop();
            vj_jack_initialize();
            if(!vj_jack_init_capture(req_ch, 16, 0)) {
                if(last_fail_ms == 0 || (now - last_fail_ms) >= 2000) {
                    last_fail_ms = now;
                    veejay_msg(VEEJAY_MSG_WARNING,
                               "[AUDIO-SYNC] unable to reopen JACK as capture-only client (%d channel%s)",
                               req_ch, req_ch == 1 ? "" : "s");
                }
                sync_store_i(&s->open, 0);
                return 0;
            }
            vj_jack_enable();
        } else {
            if(last_fail_ms == 0 || (now - last_fail_ms) >= 2000) {
                last_fail_ms = now;
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-SYNC] JACK is open for playback but has no capture ports; start JACK in duplex/capture mode");
            }
            sync_store_i(&s->open, 0);
            return 0;
        }
    }

    ch = vj_jack_get_input_channels();
    bpf = vj_jack_get_bytes_per_input_frame();
    rate = vj_jack_get_client_samplerate();
    if(rate <= 0)
        rate = vj_jack_get_rate();

    if(ch <= 0 || bpf == 0 || rate <= 0) {
        sync_store_i(&s->open, 0);
        return 0;
    }

    bits = ((int)bpf * 8) / ch;
    if(bits != 8 && bits != 16) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[AUDIO-SYNC] unsupported JACK capture format: channels=%d bpf=%lu bits=%d",
                   ch, bpf, bits);
        sync_store_i(&s->open, 0);
        return 0;
    }

    if(!sync_prepare_ring(s, ch, bits, rate)) {
        veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO-SYNC] unable to allocate capture ring");
        sync_store_i(&s->open, 0);
        return 0;
    }

    sync_store_i(&s->open, 1);
    last_fail_ms = 0;
    return 1;
}

typedef struct
{
    FILE *fp;
    int channels;
    int bits;
    int sample_rate;
    int frame_bytes;
    long data_start;
    long data_bytes;
    long data_pos;
} sync_wav_t;

static uint32_t wav_u32le(const uint8_t b[4])
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t wav_u16le(const uint8_t b[2])
{
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static void sync_wav_close(sync_wav_t *w)
{
    if(w && w->fp) {
        fclose(w->fp);
        w->fp = NULL;
    }
}

static int sync_wav_open(sync_wav_t *w, const char *path)
{
    uint8_t hdr[12];
    int got_fmt = 0;
    int got_data = 0;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sample_rate = 0;

    if(!w || !path || !path[0])
        return 0;

    memset(w, 0, sizeof(*w));
    w->fp = fopen(path, "rb");
    if(!w->fp)
        return 0;

    if(fread(hdr, 1, sizeof(hdr), w->fp) != sizeof(hdr))
        goto fail;

    if(memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
        goto fail;

    while(!got_data) {
        uint8_t chdr[8];
        uint32_t sz;
        long next;

        if(fread(chdr, 1, sizeof(chdr), w->fp) != sizeof(chdr))
            break;

        sz = wav_u32le(chdr + 4);
        next = ftell(w->fp) + (long)sz + (long)(sz & 1U);

        if(memcmp(chdr, "fmt ", 4) == 0) {
            uint8_t fmt[40];
            size_t n = sz < sizeof(fmt) ? sz : sizeof(fmt);
            if(fread(fmt, 1, n, w->fp) != n)
                goto fail;
            if(n >= 16) {
                audio_format = wav_u16le(fmt + 0);
                channels = wav_u16le(fmt + 2);
                sample_rate = wav_u32le(fmt + 4);
                bits = wav_u16le(fmt + 14);
                got_fmt = 1;
            }
        }
        else if(memcmp(chdr, "data", 4) == 0) {
            if(!got_fmt)
                goto fail;
            w->data_start = ftell(w->fp);
            w->data_bytes = (long)sz;
            w->data_pos = 0;
            got_data = 1;
        }

        if(fseek(w->fp, next, SEEK_SET) != 0)
            break;
    }

    if(!got_fmt || !got_data)
        goto fail;
    if(audio_format != 1 || (channels < 1 || channels > 2) ||
       (bits != 8 && bits != 16) || sample_rate == 0)
        goto fail;

    w->channels = channels;
    w->bits = bits;
    w->sample_rate = (int)sample_rate;
    w->frame_bytes = (bits / 8) * channels;
    fseek(w->fp, w->data_start, SEEK_SET);
    return 1;

fail:
    sync_wav_close(w);
    return 0;
}


#ifndef VJ_AUDIO_SYNC_WAV_SIDE_SLOTS
#define VJ_AUDIO_SYNC_WAV_SIDE_SLOTS 8
#endif

typedef struct {
    vj_audio_sync_shared_t *owner;
    int wav_limit_ms;
    int plain_lock_valid;
    int plain_lock_delta_frames;
    int plain_lock_confidence_pct;
} sync_wav_side_t;

static sync_wav_side_t sync_wav_side_[VJ_AUDIO_SYNC_WAV_SIDE_SLOTS];
static volatile int sync_wav_side_lock_ = 0;

static void sync_wav_side_lock(void)
{
    while(__sync_lock_test_and_set(&sync_wav_side_lock_, 1))
        sync_sleep_us(50);
}

static void sync_wav_side_unlock(void)
{
    __sync_lock_release(&sync_wav_side_lock_);
}

static sync_wav_side_t *sync_wav_side_get_locked(vj_audio_sync_shared_t *s, int create)
{
    int i;
    int free_slot = -1;

    if(!s)
        return NULL;

    for(i = 0; i < VJ_AUDIO_SYNC_WAV_SIDE_SLOTS; i++) {
        if(sync_wav_side_[i].owner == s)
            return &sync_wav_side_[i];
        if(!sync_wav_side_[i].owner && free_slot < 0)
            free_slot = i;
    }

    if(create && free_slot >= 0) {
        memset(&sync_wav_side_[free_slot], 0, sizeof(sync_wav_side_[free_slot]));
        sync_wav_side_[free_slot].owner = s;
        return &sync_wav_side_[free_slot];
    }

    return NULL;
}

static void sync_wav_side_clear(vj_audio_sync_shared_t *s)
{
    sync_wav_side_t *side;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 0);
    if(side)
        memset(side, 0, sizeof(*side));
    sync_wav_side_unlock();
}

static void sync_wav_side_set_limit(vj_audio_sync_shared_t *s, int limit_ms)
{
    sync_wav_side_t *side;

    if(limit_ms < 0)
        limit_ms = 0;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 1);
    if(side)
        side->wav_limit_ms = limit_ms;
    sync_wav_side_unlock();
}

static int sync_wav_side_get_limit(vj_audio_sync_shared_t *s)
{
    sync_wav_side_t *side;
    int limit_ms = 0;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 0);
    if(side)
        limit_ms = side->wav_limit_ms;
    sync_wav_side_unlock();

    return limit_ms;
}

static void sync_wav_plain_lock_clear(vj_audio_sync_shared_t *s)
{
    sync_wav_side_t *side;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 0);
    if(side) {
        side->plain_lock_valid = 0;
        side->plain_lock_delta_frames = 0;
        side->plain_lock_confidence_pct = 0;
    }
    sync_wav_side_unlock();
}

static int sync_wav_read(sync_wav_t *w, uint8_t *dst, int max_bytes, int loop)
{
    int done = 0;

    if(!w || !w->fp || !dst || max_bytes <= 0 || w->frame_bytes <= 0)
        return 0;

    max_bytes -= max_bytes % w->frame_bytes;
    if(max_bytes <= 0)
        return 0;

    while(done < max_bytes) {
        long left = w->data_bytes - w->data_pos;
        int want;
        int got;

        if(left <= 0) {
            if(!loop)
                break;
            w->data_pos = 0;
            fseek(w->fp, w->data_start, SEEK_SET);
            left = w->data_bytes;
        }

        want = max_bytes - done;
        if((long)want > left)
            want = (int)left;
        want -= want % w->frame_bytes;
        if(want <= 0)
            break;

        got = (int)fread(dst + done, 1, (size_t)want, w->fp);
        if(got <= 0)
            break;
        got -= got % w->frame_bytes;

        done += got;
        w->data_pos += got;
    }

    return done;
}

static int sync_frames_available_locked(vj_audio_sync_shared_t *s, long long cursor)
{
    long long oldest;
    long long newest;

    if(!s || !s->ring || s->ring_frames <= 0)
        return 0;

    newest = s->write_frame_abs;
    oldest = newest - (long long)s->ring_frames;
    if(oldest < 0)
        oldest = 0;

    if(cursor < oldest)
        cursor = oldest;
    if(cursor > newest)
        return 0;

    return (int)(newest - cursor);
}

static int sync_read_cursor_audio(vj_audio_sync_shared_t *s,
                                  uint8_t *dst,
                                  int max_bytes,
                                  long long *cursor_ptr)
{
    int frame_bytes;
    int max_frames;
    int available;
    int frames;
    long long oldest;
    long long cursor;

    if(!s || !dst || !cursor_ptr || max_bytes <= 0)
        return 0;
    if(!sync_load_i(&s->enabled) || !sync_load_i(&s->open))
        return 0;

    if(!sync_try_lock(s))
        return 0;

    frame_bytes = sync_load_i(&s->bytes_per_frame);
    if(!s->ring || s->ring_frames <= 0 || frame_bytes <= 0) {
        sync_unlock(s);
        return 0;
    }

    max_frames = max_bytes / frame_bytes;
    if(max_frames <= 0) {
        sync_unlock(s);
        return 0;
    }

    oldest = s->write_frame_abs - (long long)s->ring_frames;
    if(oldest < 0)
        oldest = 0;

    cursor = *cursor_ptr;

    if(cursor <= 0 || cursor < oldest) {
        long long live_start = s->write_frame_abs - (long long)(max_frames * 2);
        if(live_start < oldest)
            live_start = oldest;
        cursor = live_start;
    }
    else if((s->write_frame_abs - cursor) > (long long)(max_frames * 8)) {
        long long live_start = s->write_frame_abs - (long long)(max_frames * 2);
        if(live_start < oldest)
            live_start = oldest;
        cursor = live_start;
    }

    available = sync_frames_available_locked(s, cursor);
    if(available <= 0) {
        sync_unlock(s);
        return 0;
    }

    frames = available < max_frames ? available : max_frames;

    for(int i = 0; i < frames; i++) {
        int ri = sync_ring_index(s, cursor + i);
        veejay_memcpy(dst + ((size_t)i * (size_t)frame_bytes),
                      s->ring + ((size_t)ri * (size_t)frame_bytes),
                      frame_bytes);
    }

    *cursor_ptr = cursor + frames;
    sync_unlock(s);
    return frames * frame_bytes;
}

int vj_audio_sync_read_analysis_audio(vj_audio_sync_shared_t *s,
                                      uint8_t *dst,
                                      int max_bytes)
{
    return sync_read_cursor_audio(s, dst, max_bytes,
                                  s ? &s->analysis_read_frame : NULL);
}

int vj_audio_sync_read_beat_audio(vj_audio_sync_shared_t *s,
                                  uint8_t *dst,
                                  int max_bytes)
{
    return sync_read_cursor_audio(s, dst, max_bytes,
                                  s ? &s->beat_read_frame : NULL);
}

void vj_audio_sync_reset_beat_reader(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    sync_lock(s);
    s->beat_read_frame = 0;
    sync_unlock(s);
}

int vj_audio_sync_copy_record_audio(vj_audio_sync_shared_t *s,
                                    uint8_t *dst,
                                    int dst_frames,
                                    int dst_frame_bytes,
                                    int dst_channels,
                                    int dst_sample_rate)
{
    int src_rate;
    int src_channels;
    int src_frame_bytes;
    int src_sample_bytes;
    int dst_sample_bytes;
    int available;
    int need_src_frames;
    int out_frames;
    long long cursor;
    long long oldest;

    if(!s || !dst || dst_frames <= 0 || dst_frame_bytes <= 0 ||
       dst_channels <= 0 || dst_channels > 2 || dst_sample_rate <= 0)
        return 0;
    if(!sync_load_i(&s->enabled) || !sync_load_i(&s->open))
        return 0;

    if(!sync_try_lock(s))
        return 0;

    if(!s->ring || s->ring_frames <= 0 || s->bytes_per_frame <= 0 ||
       s->channels <= 0 || s->sample_rate <= 0) {
        sync_unlock(s);
        return 0;
    }

    src_rate = s->sample_rate;
    src_channels = s->channels;
    src_frame_bytes = s->bytes_per_frame;

    if((src_frame_bytes % src_channels) != 0 ||
       (dst_frame_bytes % dst_channels) != 0) {
        sync_unlock(s);
        return 0;
    }

    src_sample_bytes = src_frame_bytes / src_channels;
    dst_sample_bytes = dst_frame_bytes / dst_channels;
    if((src_sample_bytes != 1 && src_sample_bytes != 2) ||
       (dst_sample_bytes != 1 && dst_sample_bytes != 2)) {
        sync_unlock(s);
        return 0;
    }

    oldest = s->write_frame_abs - (long long)s->ring_frames;
    if(oldest < 0)
        oldest = 0;

    cursor = s->record_read_frame;
    if(cursor <= 0 || cursor < oldest)
        cursor = oldest;

    available = sync_frames_available_locked(s, cursor);
    if(available <= 0) {
        sync_add_l(&s->underruns, 1);
        sync_unlock(s);
        return 0;
    }

    need_src_frames = (int)(((long long)dst_frames * (long long)src_rate +
                             (long long)dst_sample_rate - 1LL) /
                            (long long)dst_sample_rate);
    if(need_src_frames < 1)
        need_src_frames = 1;

    if(available < need_src_frames) {
        out_frames = (int)(((long long)available * (long long)dst_sample_rate) /
                           (long long)src_rate);
        if(out_frames <= 0) {
            sync_add_l(&s->underruns, 1);
            sync_unlock(s);
            return 0;
        }
        if(out_frames > dst_frames)
            out_frames = dst_frames;
        need_src_frames = available;
        sync_add_l(&s->underruns, 1);
    } else {
        out_frames = dst_frames;
    }

    for(int i = 0; i < out_frames; i++) {
        int src_i = (int)(((long long)i * (long long)src_rate) /
                          (long long)dst_sample_rate);
        long long af;
        int ri;
        const uint8_t *sf;
        uint8_t *df;
        int l, r;

        if(src_i >= need_src_frames)
            src_i = need_src_frames - 1;

        af = cursor + src_i;
        ri = sync_ring_index(s, af);
        sf = s->ring + ((size_t)ri * (size_t)src_frame_bytes);
        df = dst + ((size_t)i * (size_t)dst_frame_bytes);

        if(src_channels == 1) {
            l = r = sync_read_sample(sf, src_sample_bytes);
        } else {
            l = sync_read_sample(sf, src_sample_bytes);
            r = sync_read_sample(sf + src_sample_bytes, src_sample_bytes);
        }

        if(dst_channels == 1) {
            sync_write_sample(df, dst_sample_bytes, (l + r) / 2);
        } else {
            sync_write_sample(df, dst_sample_bytes, l);
            sync_write_sample(df + dst_sample_bytes, dst_sample_bytes, r);
        }
    }

    s->record_read_frame = cursor + need_src_frames;
    sync_unlock(s);
    return out_frames;
}

static int sync_sample_at_locked(vj_audio_sync_shared_t *s,
                                 double pos,
                                 int channel,
                                 int dst_channels)
{
    int src_channels = s->channels;
    int frame_bytes = s->bytes_per_frame;
    int sample_bytes = frame_bytes / src_channels;
    long long base = (long long)floor(pos);
    double frac = pos - (double)base;
    long long newest = s->write_frame_abs - 1;
    long long oldest = s->write_frame_abs - (long long)s->ring_frames;
    int ri0, ri1;
    const uint8_t *f0;
    const uint8_t *f1;
    int c = channel;
    int a, b;

    if(oldest < 0)
        oldest = 0;
    if(base < oldest)
        base = oldest;
    if(base > newest)
        base = newest;
    if(base + 1 > newest)
        frac = 0.0;

    if(src_channels == 1)
        c = 0;
    else if(dst_channels == 1)
        c = 0;
    else if(c >= src_channels)
        c = src_channels - 1;

    ri0 = sync_ring_index(s, base);
    ri1 = sync_ring_index(s, (base + 1 <= newest) ? base + 1 : base);
    f0 = s->ring + ((size_t)ri0 * (size_t)frame_bytes);
    f1 = s->ring + ((size_t)ri1 * (size_t)frame_bytes);

    if(dst_channels == 1 && src_channels > 1) {
        int a0 = sync_read_sample(f0, sample_bytes);
        int a1 = sync_read_sample(f0 + sample_bytes, sample_bytes);
        int b0 = sync_read_sample(f1, sample_bytes);
        int b1 = sync_read_sample(f1 + sample_bytes, sample_bytes);
        a = (a0 + a1) / 2;
        b = (b0 + b1) / 2;
    } else {
        a = sync_read_sample(f0 + c * sample_bytes, sample_bytes);
        b = sync_read_sample(f1 + c * sample_bytes, sample_bytes);
    }

    return sync_clip16((int)((double)a + ((double)b - (double)a) * frac));
}

static double sync_fold_bpm_near(double bpm, double ref)
{
    if(bpm <= 0.0 || ref <= 0.0)
        return bpm;

    while(bpm < ref * 0.70 && bpm * 2.0 <= VJ_AUDIO_SYNC_MAX_BPM)
        bpm *= 2.0;
    while(bpm > ref * 1.40 && bpm * 0.5 >= VJ_AUDIO_SYNC_MIN_BPM)
        bpm *= 0.5;

    return bpm;
}


static void sync_tempo_follow_publish(vj_audio_sync_shared_t *s)
{
    int source_bpm_q8;
    int target_bpm_q8;
    int target_mode;
    int have_source_clock;
    int have_target_clock;
    int bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_IDLE;
    double source_bpm;
    double target_bpm;
    double source_confidence;
    double target_confidence;
    double max_corr;
    double live_pull;
    double stale_pull_scale = 1.0;
    double desired_ratio = 1.0;
    double ratio;
    long now_ms;
    long latch_age_ms = 0;

    if(!s)
        return;

    if(!sync_load_i(&s->enabled) ||
       sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        return;

    now_ms = sync_now_ms();

    sync_lock(s);
    if(!sync_load_i(&s->enabled) ||
       sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW) {
        sync_unlock(s);
        return;
    }

    max_corr = (double)sync_load_i(&s->max_correction_pct) / 100.0;
    if(max_corr < 0.0) max_corr = 0.0;
    if(max_corr > 0.25) max_corr = 0.25;

    source_bpm_q8 = sync_load_i(&s->source_bpm_q8);
    target_bpm_q8 = sync_load_i(&s->target_bpm_q8);
    target_mode = sync_load_i(&s->target_mode);
    source_confidence = sync_from_q15(sync_load_i(&s->confidence_q15));
    target_confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
    source_bpm = sync_bpm_from_q8(source_bpm_q8);
    target_bpm = sync_bpm_from_q8(target_bpm_q8);

    if(source_bpm < VJ_AUDIO_SYNC_MIN_BPM || source_bpm > VJ_AUDIO_SYNC_MAX_BPM)
        source_bpm = 0.0;
    if(source_confidence < 0.38)
        source_bpm = 0.0;

    if(target_bpm < VJ_AUDIO_SYNC_MIN_BPM || target_bpm > VJ_AUDIO_SYNC_MAX_BPM)
        target_bpm = 0.0;

    if(target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP && target_confidence < 0.68)
        target_bpm = 0.0;

    have_source_clock = (source_bpm > 0.0);
    have_target_clock = (target_bpm > 0.0);

    if(have_source_clock && have_target_clock) {
        const double src_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.010 : 0.020;
        const double dst_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.008 : 0.030;

        if(!s->bridge_bpm_latch_valid ||
           s->bridge_source_bpm_latched <= 0.0 ||
           s->bridge_target_bpm_latched <= 0.0)
        {
            s->bridge_source_bpm_latched = source_bpm;
            s->bridge_target_bpm_latched = target_bpm;
            s->bridge_bpm_latch_valid = 1;
        }
        else
        {
            double folded_source = sync_fold_bpm_near(source_bpm,
                                                      s->bridge_source_bpm_latched);
            double folded_target = sync_fold_bpm_near(target_bpm,
                                                      s->bridge_target_bpm_latched);
            double src_rel = fabs(folded_source - s->bridge_source_bpm_latched) /
                             ((s->bridge_source_bpm_latched > 1.0) ? s->bridge_source_bpm_latched : 1.0);
            double dst_rel = fabs(folded_target - s->bridge_target_bpm_latched) /
                             ((s->bridge_target_bpm_latched > 1.0) ? s->bridge_target_bpm_latched : 1.0);

            if(src_rel < 0.18)
                s->bridge_source_bpm_latched +=
                    src_alpha * (folded_source - s->bridge_source_bpm_latched);

            if(dst_rel < 0.22 || target_mode == VJ_AUDIO_SYNC_TARGET_MANUAL)
                s->bridge_target_bpm_latched +=
                    dst_alpha * (folded_target - s->bridge_target_bpm_latched);
        }

        s->bridge_latch_updated_ms = now_ms;
        source_bpm = s->bridge_source_bpm_latched;
        target_bpm = s->bridge_target_bpm_latched;
        bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_LOCKED;
    }
    else if(s->bridge_bpm_latch_valid &&
            s->bridge_source_bpm_latched > 0.0 &&
            s->bridge_target_bpm_latched > 0.0)
    {
        latch_age_ms = now_ms - s->bridge_latch_updated_ms;
        if(latch_age_ms < 0)
            latch_age_ms = 0;

        if(latch_age_ms <= 8000L) {
            source_bpm = s->bridge_source_bpm_latched;
            target_bpm = s->bridge_target_bpm_latched;
            bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_HOLD;

            if(latch_age_ms > 3000L) {
                stale_pull_scale = 1.0 - ((double)(latch_age_ms - 3000L) / 5000.0);
                if(stale_pull_scale < 0.0)
                    stale_pull_scale = 0.0;
            }
        } else {
            s->bridge_source_bpm_latched = 0.0;
            s->bridge_target_bpm_latched = 0.0;
            s->bridge_bpm_latch_valid = 0;
            s->bridge_latch_updated_ms = 0;
            source_bpm = 0.0;
            target_bpm = 0.0;
            bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_FALLBACK;
        }
    }
    else
    {
        source_bpm = 0.0;
        target_bpm = 0.0;
        bridge_state = have_source_clock ?
            VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET :
            VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE;
    }

    live_pull = max_corr;
    if(live_pull <= 0.0001)
        live_pull = 0.0;
    live_pull *= stale_pull_scale;

    if(live_pull > 0.0 && source_bpm > 0.0 && target_bpm > 0.0) {
        desired_ratio = target_bpm / source_bpm;
        desired_ratio = sync_clampd(desired_ratio,
                                    1.0 - live_pull,
                                    1.0 + live_pull);
    }

    if(desired_ratio < 0.25) desired_ratio = 0.25;
    if(desired_ratio > 4.0) desired_ratio = 4.0;

    if(s->bridge_ratio_smooth <= 0.0)
        ratio = desired_ratio;
    else {
        const double ratio_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.012 : 0.025;
        ratio = s->bridge_ratio_smooth + ratio_alpha * (desired_ratio - s->bridge_ratio_smooth);
    }

    if(ratio < 0.25) ratio = 0.25;
    if(ratio > 4.0) ratio = 4.0;

    s->bridge_ratio_smooth = ratio;
    s->bridge_last_correction = ratio;
    sync_store_i(&s->bridge_active,
                 (bridge_state == VJ_AUDIO_SYNC_BRIDGE_STATE_LOCKED ||
                  bridge_state == VJ_AUDIO_SYNC_BRIDGE_STATE_HOLD) ? 1 : 0);
    sync_store_i(&s->bridge_state, bridge_state);
    sync_unlock(s);
}


int vj_audio_sync_render_bridge_s16(vj_audio_sync_shared_t *s,
                                    uint8_t *dst,
                                    int dst_frames,
                                    int dst_frame_bytes,
                                    int dst_channels,
                                    int dst_sample_rate)
{
    int src_rate;
    int src_channels;
    int src_frame_bytes;
    int src_sample_bytes;
    int dst_sample_bytes;
    int source_bpm_q8;
    int target_bpm_q8;
    int target_mode;
    int have_source_clock;
    int have_target_clock;
    int bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_IDLE;
    double source_bpm;
    double target_bpm;
    double source_confidence;
    double target_confidence;
    double rate_ratio;
    double desired_ratio;
    double tempo_ratio = 1.0;
    double ratio;
    double max_corr;
    double live_pull;
    double stale_pull_scale = 1.0;
    double pull_amount;
    double read_pos;
    long now_ms;
    long latch_age_ms = 0;
    long long oldest;
    long long newest;
    long long latency_frames;
    long long guard_frames;
    long long latency_ms;
    long long target_read;

    if(!s || !dst || dst_frames <= 0 || dst_frame_bytes <= 0 ||
       dst_channels <= 0 || dst_channels > 2 || dst_sample_rate <= 0)
        return 0;

    if(!sync_load_i(&s->enabled) ||
       !sync_load_i(&s->open) ||
       sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
        return 0;

    if(!sync_try_lock(s))
        return 0;

    if(!s->ring || s->ring_frames <= 8 || s->channels <= 0 ||
       s->bytes_per_frame <= 0 || s->sample_rate <= 0) {
        sync_store_i(&s->bridge_active, 0);
        sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
        sync_unlock(s);
        return 0;
    }

    src_rate = s->sample_rate;
    src_channels = s->channels;
    src_frame_bytes = s->bytes_per_frame;
    src_sample_bytes = src_frame_bytes / src_channels;
    dst_sample_bytes = dst_frame_bytes / dst_channels;

    if(src_sample_bytes != 2 || dst_sample_bytes != 2) {
        sync_store_i(&s->bridge_active, 0);
        sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_FALLBACK);
        sync_unlock(s);
        return 0;
    }

    oldest = s->write_frame_abs - (long long)s->ring_frames;
    if(oldest < 0)
        oldest = 0;
    newest = s->write_frame_abs - 2;
    if(newest <= oldest + 4) {
        sync_add_l(&s->underruns, 1);
        sync_store_i(&s->bridge_active, 0);
        sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
        sync_unlock(s);
        return 0;
    }

    now_ms = sync_now_ms();

    max_corr = (double)sync_load_i(&s->max_correction_pct) / 100.0;
    if(max_corr < 0.0) max_corr = 0.0;
    if(max_corr > 0.25) max_corr = 0.25;

    source_bpm_q8 = sync_load_i(&s->source_bpm_q8);
    target_bpm_q8 = sync_load_i(&s->target_bpm_q8);
    target_mode = sync_load_i(&s->target_mode);
    source_confidence = sync_from_q15(sync_load_i(&s->confidence_q15));
    target_confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
    source_bpm = sync_bpm_from_q8(source_bpm_q8);
    target_bpm = sync_bpm_from_q8(target_bpm_q8);

    if(source_bpm < VJ_AUDIO_SYNC_MIN_BPM || source_bpm > VJ_AUDIO_SYNC_MAX_BPM)
        source_bpm = 0.0;
    if(source_confidence < 0.38)
        source_bpm = 0.0;

    if(target_bpm < VJ_AUDIO_SYNC_MIN_BPM || target_bpm > VJ_AUDIO_SYNC_MAX_BPM)
        target_bpm = 0.0;

    if(target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP && target_confidence < 0.68)
        target_bpm = 0.0;

    have_source_clock = (source_bpm > 0.0);
    have_target_clock = (target_bpm > 0.0);

    if(have_source_clock && have_target_clock) {
        const double src_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.006 : 0.012;
        const double dst_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.004 : 0.035;

        if(!s->bridge_bpm_latch_valid ||
           s->bridge_source_bpm_latched <= 0.0 ||
           s->bridge_target_bpm_latched <= 0.0)
        {
            s->bridge_source_bpm_latched = source_bpm;
            s->bridge_target_bpm_latched = target_bpm;
            s->bridge_bpm_latch_valid = 1;
        }
        else
        {
            double folded_source = sync_fold_bpm_near(source_bpm,
                                                      s->bridge_source_bpm_latched);
            double folded_target = sync_fold_bpm_near(target_bpm,
                                                      s->bridge_target_bpm_latched);
            double src_rel = fabs(folded_source - s->bridge_source_bpm_latched) /
                             ((s->bridge_source_bpm_latched > 1.0) ? s->bridge_source_bpm_latched : 1.0);
            double dst_rel = fabs(folded_target - s->bridge_target_bpm_latched) /
                             ((s->bridge_target_bpm_latched > 1.0) ? s->bridge_target_bpm_latched : 1.0);

            if(src_rel < 0.18)
                s->bridge_source_bpm_latched +=
                    src_alpha * (folded_source - s->bridge_source_bpm_latched);

            if(dst_rel < 0.22 || target_mode == VJ_AUDIO_SYNC_TARGET_MANUAL)
                s->bridge_target_bpm_latched +=
                    dst_alpha * (folded_target - s->bridge_target_bpm_latched);
        }

        s->bridge_latch_updated_ms = now_ms;
        source_bpm = s->bridge_source_bpm_latched;
        target_bpm = s->bridge_target_bpm_latched;
        bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_LOCKED;
    }
    else if(s->bridge_bpm_latch_valid &&
            s->bridge_source_bpm_latched > 0.0 &&
            s->bridge_target_bpm_latched > 0.0)
    {
        latch_age_ms = now_ms - s->bridge_latch_updated_ms;
        if(latch_age_ms < 0)
            latch_age_ms = 0;

        if(latch_age_ms <= 8000L) {
            source_bpm = s->bridge_source_bpm_latched;
            target_bpm = s->bridge_target_bpm_latched;
            bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_HOLD;

            if(latch_age_ms > 3000L) {
                stale_pull_scale = 1.0 - ((double)(latch_age_ms - 3000L) / 5000.0);
                if(stale_pull_scale < 0.0)
                    stale_pull_scale = 0.0;
            }
        } else {
            s->bridge_source_bpm_latched = 0.0;
            s->bridge_target_bpm_latched = 0.0;
            s->bridge_bpm_latch_valid = 0;
            s->bridge_latch_updated_ms = 0;
            source_bpm = 0.0;
            target_bpm = 0.0;
            bridge_state = VJ_AUDIO_SYNC_BRIDGE_STATE_FALLBACK;
        }
    }
    else
    {
        source_bpm = 0.0;
        target_bpm = 0.0;
        bridge_state = have_source_clock ?
            VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET :
            VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE;
    }

    live_pull = max_corr;
    if(live_pull <= 0.0001)
        live_pull = 0.0;
    else if(target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) {
        if(live_pull > 0.035)
            live_pull = 0.035;
    }
    live_pull *= stale_pull_scale;

    rate_ratio = (double)src_rate / (double)dst_sample_rate;
    desired_ratio = rate_ratio;

    if(live_pull > 0.0 && source_bpm > 0.0 && target_bpm > 0.0) {
        tempo_ratio = target_bpm / source_bpm;
        tempo_ratio = sync_clampd(tempo_ratio,
                                  1.0 - live_pull,
                                  1.0 + live_pull);
        desired_ratio = rate_ratio * tempo_ratio;
    }

    if(desired_ratio < 0.25) desired_ratio = 0.25;
    if(desired_ratio > 4.0) desired_ratio = 4.0;

    if(s->bridge_ratio_smooth <= 0.0)
        ratio = desired_ratio;
    else {
        const double ratio_alpha =
            (target_mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP) ? 0.0060 : 0.1800;
        ratio = s->bridge_ratio_smooth + ratio_alpha * (desired_ratio - s->bridge_ratio_smooth);
    }

    if(ratio < 0.25) ratio = 0.25;
    if(ratio > 4.0) ratio = 4.0;

    pull_amount = fabs(tempo_ratio - 1.0) * stale_pull_scale;

    latency_ms = VJ_AUDIO_SYNC_BRIDGE_LATENCY_MS + (long long)(pull_amount * 4500.0);
    if(latency_ms < VJ_AUDIO_SYNC_BRIDGE_LATENCY_MS)
        latency_ms = VJ_AUDIO_SYNC_BRIDGE_LATENCY_MS;
    if(latency_ms > VJ_AUDIO_SYNC_BRIDGE_MAX_LATENCY_MS)
        latency_ms = VJ_AUDIO_SYNC_BRIDGE_MAX_LATENCY_MS;

    latency_frames = ((long long)src_rate * latency_ms) / 1000LL;
    if(latency_frames < (long long)dst_frames * 4)
        latency_frames = (long long)dst_frames * 4;
    if(latency_frames < 16)
        latency_frames = 16;
    if(latency_frames > (long long)s->ring_frames / 2)
        latency_frames = (long long)s->ring_frames / 2;

    guard_frames = ((long long)src_rate * 35LL) / 1000LL;
    if(guard_frames < (long long)dst_frames * 2)
        guard_frames = (long long)dst_frames * 2;
    if(guard_frames > latency_frames / 2)
        guard_frames = latency_frames / 2;
    if(guard_frames < 8)
        guard_frames = 8;

    target_read = s->write_frame_abs - latency_frames;
    if(target_read < oldest + guard_frames)
        target_read = oldest + guard_frames;
    if(target_read > newest - guard_frames)
        target_read = newest - guard_frames;

    if(!s->bridge_read_valid) {
        s->bridge_read_pos = (double)target_read;
        s->bridge_read_valid = 1;
    }

    read_pos = s->bridge_read_pos;

    if(read_pos < (double)(oldest + guard_frames) ||
       read_pos > (double)(newest - guard_frames)) {
        read_pos = (double)target_read;
        sync_add_l(&s->underruns, 1);
    }

    {
        double max_end = (double)(newest - guard_frames);
        double min_end = (double)(oldest + guard_frames);
        double max_ratio = (max_end - read_pos) / (double)dst_frames;
        double min_ratio = (min_end - read_pos) / (double)dst_frames;

        if(max_ratio < 0.25)
            max_ratio = 0.25;
        if(min_ratio < 0.25)
            min_ratio = 0.25;

        if(ratio > max_ratio) {
            ratio = max_ratio;
            sync_add_l(&s->underruns, 1);
        }

        if(read_pos + ((double)dst_frames * ratio) < min_end) {
            double catchup_ratio = ((double)target_read - read_pos) / (double)dst_frames;
            if(catchup_ratio < 0.25)
                catchup_ratio = 0.25;
            if(catchup_ratio > 4.0)
                catchup_ratio = 4.0;
            if(catchup_ratio > ratio)
                ratio = catchup_ratio;
        }
    }

    for(int i = 0; i < dst_frames; i++) {
        uint8_t *df = dst + ((size_t)i * (size_t)dst_frame_bytes);
        if(dst_channels == 1) {
            int v = sync_sample_at_locked(s, read_pos, 0, 1);
            sync_write_sample(df, 2, v);
        } else {
            int l = sync_sample_at_locked(s, read_pos, 0, 2);
            int r = sync_sample_at_locked(s, read_pos, 1, 2);
            sync_write_sample(df, 2, l);
            sync_write_sample(df + 2, 2, r);
        }
        read_pos += ratio;
    }

    s->bridge_read_pos = read_pos;
    s->bridge_ratio_smooth = ratio;
    s->bridge_last_correction = (rate_ratio > 0.0) ? (ratio / rate_ratio) : 1.0;
    if(s->bridge_last_correction < 0.25)
        s->bridge_last_correction = 0.25;
    if(s->bridge_last_correction > 4.0)
        s->bridge_last_correction = 4.0;
    sync_store_i(&s->bridge_active, 1);
    sync_store_i(&s->bridge_state, bridge_state);
    sync_unlock(s);
    return dst_frames;
}

#ifndef VJ_AUDIO_SYNC_MONITOR_LATENCY_MS
#define VJ_AUDIO_SYNC_MONITOR_LATENCY_MS 70
#endif

int vj_audio_sync_render_monitor_s16(vj_audio_sync_shared_t *s,
                                     uint8_t *dst,
                                     int dst_frames,
                                     int dst_frame_bytes,
                                     int dst_channels,
                                     int dst_sample_rate)
{
    int src_rate;
    int src_channels;
    int src_frame_bytes;
    int src_sample_bytes;
    int dst_sample_bytes;
    double ratio;
    double read_pos;
    long long oldest;
    long long newest;
    long long latency_frames;
    int resync = 0;
    int old_last_l = 0;
    int old_last_r = 0;
    int have_old_last = 0;

    if(!s || !dst || dst_frames <= 0 || dst_frame_bytes <= 0 ||
       dst_channels <= 0 || dst_channels > 2 || dst_sample_rate <= 0)
        return 0;

    {
        int mode = sync_load_i(&s->mode);
        if(!sync_load_i(&s->enabled) ||
           !sync_load_i(&s->open) ||
           (mode != VJ_AUDIO_SYNC_MODE_MONITOR &&
            mode != VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY &&
            mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE &&
            mode != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN))
            return 0;
    }

    if(!sync_try_lock_for_us(s, 3000)) {
        sync_add_l(&s->underruns, 1);
        sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
        return dst_frames;
    }

    if(!s->ring || s->ring_frames <= 8 || s->channels <= 0 ||
       s->bytes_per_frame <= 0 || s->sample_rate <= 0) {
        sync_add_l(&s->underruns, 1);
        sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
        sync_unlock(s);
        return dst_frames;
    }

    src_rate = s->sample_rate;
    src_channels = s->channels;
    src_frame_bytes = s->bytes_per_frame;

    if(src_channels <= 0 || dst_channels <= 0 ||
       (src_frame_bytes % src_channels) != 0 ||
       (dst_frame_bytes % dst_channels) != 0) {
        sync_add_l(&s->underruns, 1);
        sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
        sync_unlock(s);
        return dst_frames;
    }

    src_sample_bytes = src_frame_bytes / src_channels;
    dst_sample_bytes = dst_frame_bytes / dst_channels;

    if(src_sample_bytes != 2 || dst_sample_bytes != 2) {
        sync_add_l(&s->underruns, 1);
        sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
        sync_unlock(s);
        return dst_frames;
    }

    oldest = s->write_frame_abs - (long long)s->ring_frames;
    if(oldest < 0)
        oldest = 0;

    newest = s->write_frame_abs - 2;
    if(newest <= oldest + 4) {
        sync_add_l(&s->underruns, 1);
        sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
        sync_unlock(s);
        return dst_frames;
    }

    ratio = (double)src_rate / (double)dst_sample_rate;

    {
        long long block_src_frames = (long long)(((double)dst_frames * ratio) + 0.999);
        long long min_latency = block_src_frames + 512;

        if(min_latency < 1024)
            min_latency = 1024;

        latency_frames = ((long long)src_rate * VJ_AUDIO_SYNC_MONITOR_LATENCY_MS) / 1000LL;
        if(latency_frames < min_latency)
            latency_frames = min_latency;
        if(latency_frames < 64)
            latency_frames = 64;
        if(latency_frames > (long long)s->ring_frames / 2)
            latency_frames = (long long)s->ring_frames / 2;
    }

    if(!s->monitor_read_valid) {
        s->monitor_read_pos = (double)(s->write_frame_abs - latency_frames);
        s->monitor_read_valid = 1;
    }

    read_pos = s->monitor_read_pos;

    if(s->monitor_last_valid) {
        old_last_l = s->monitor_last_l;
        old_last_r = s->monitor_last_r;
        have_old_last = 1;
    }

    {
        double need = ((double)dst_frames * ratio);
        double safe_oldest = (double)oldest;
        double safe_newest = (double)newest - 4.0;
        double max_start = safe_newest - need;
        double target = (double)(s->write_frame_abs - latency_frames);

        if(max_start < safe_oldest) {
            sync_add_l(&s->underruns, 1);
            sync_monitor_hold_last(s, dst, dst_frames, dst_frame_bytes, dst_channels);
            sync_unlock(s);
            return dst_frames;
        }

        if(target > max_start)
            target = max_start;
        if(target < safe_oldest)
            target = safe_oldest;

        if(read_pos < safe_oldest || read_pos > safe_newest ||
           read_pos + need > safe_newest)
        {
            read_pos = target;
            resync = 1;
            s->monitor_resyncs++;
            sync_add_l(&s->underruns, 1);
        }
        else {
            double err = target - read_pos;
            double pull = err / ((double)latency_frames * 12.0);
            if(pull > 0.0025)
                pull = 0.0025;
            else if(pull < -0.0025)
                pull = -0.0025;
            ratio *= (1.0 + pull);
        }
    }

    for(int i = 0; i < dst_frames; i++) {
        uint8_t *df = dst + ((size_t)i * (size_t)dst_frame_bytes);
        int l, r;

        if(dst_channels == 1) {
            l = sync_sample_at_locked(s, read_pos, 0, 1);
            r = l;
        } else {
            l = sync_sample_at_locked(s, read_pos, 0, 2);
            r = sync_sample_at_locked(s, read_pos, 1, 2);
        }

        if(resync && have_old_last && i < 128) {
            int a = i + 1;
            int b = 128 - i;
            l = ((old_last_l * b) + (l * a)) >> 7;
            r = ((old_last_r * b) + (r * a)) >> 7;
        }

        if(dst_channels == 1) {
            sync_write_sample(df, 2, l);
        } else {
            sync_write_sample(df, 2, l);
            sync_write_sample(df + 2, 2, r);
        }

        s->monitor_last_l = l;
        s->monitor_last_r = r;
        s->monitor_last_valid = 1;

        read_pos += ratio;
    }

    s->monitor_read_pos = read_pos;
    sync_store_i(&s->bridge_active, 0);
    sync_unlock(s);
    return dst_frames;
}


void *vj_audio_sync_thread(void *arg)
{
    vj_audio_sync_shared_t *s = (vj_audio_sync_shared_t*)arg;
    uint8_t *buffer = NULL;
    int buffer_size = 0;
    uint8_t *target_buffer = NULL;
    int target_buffer_size = 0;
    int last_reset = -1;
    int last_source = -1;
    int last_file_generation = -1;
    int last_wav_limit_ms = 0;
    long long wav_limit_frames = 0;
    long long wav_read_frames_total = 0;
    sync_wav_t wav;

    memset(&wav, 0, sizeof(wav));

    if(!s)
        return NULL;

    if(!sync_load_i(&s->initialized))
        vj_audio_sync_init(s, 2);

    sync_store_i(&s->running, 1);
    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO-SYNC] external audio sync thread started");

    while(!sync_load_i(&s->stop_request)) {
        int source;
        int mode;
        int reset_seq;
        int got = 0;
        int ch = 0;
        int bits = 16;
        int rate = 0;
        int frame_bytes = 0;
        int target_frames;
        int target_bytes;

        if(sync_load_i(&s->enabled)) {
            int tq_ch = 0, tq_bits = 0, tq_rate = 0, tq_frame_bytes = 0;
            int tq_frames = sync_dequeue_target_audio(s,
                                                       &target_buffer,
                                                       &target_buffer_size,
                                                       &tq_ch,
                                                       &tq_bits,
                                                       &tq_rate,
                                                       &tq_frame_bytes);
            if(tq_frames > 0) {
                sync_update_target_clock_from_block(s,
                                                    target_buffer,
                                                    tq_frames,
                                                    tq_frame_bytes,
                                                    tq_ch,
                                                    tq_bits,
                                                    tq_rate);
                if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
                    sync_tempo_follow_publish(s);
                if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
                    sync_track_align_push_block(s,
                                                target_buffer,
                                                tq_frames,
                                                tq_frame_bytes,
                                                tq_ch,
                                                tq_bits,
                                                tq_rate,
                                                1);
            }
        }

        if(!sync_load_i(&s->enabled) || sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_OFF) {
            sync_store_i(&s->open, 0);
            sync_sleep_us(20000);
            continue;
        }

        mode = sync_load_i(&s->mode);
        source = sync_load_i(&s->source);
        reset_seq = sync_load_i(&s->reset_seq);

        if(reset_seq != last_reset || source != last_source) {
            sync_lock(s);
            s->analysis_read_frame = 0;
            s->beat_read_frame = 0;
            s->record_read_frame = 0;
            s->bridge_read_valid = 0;
            s->bridge_ratio_smooth = 0.0;
            s->bridge_last_correction = 1.0;
            s->bridge_latch_updated_ms = 0;
            s->bridge_source_bpm_latched = 0.0;
            s->bridge_target_bpm_latched = 0.0;
            s->bridge_bpm_latch_valid = 0;
            sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
            s->write_frame_abs = 0;
            s->ring_write_frame = 0;
            sync_clear_clock(s);
            sync_clear_target_queue_locked(s);
            sync_unlock(s);
            sync_wav_close(&wav);
            wav_limit_frames = 0;
            wav_read_frames_total = 0;
            last_wav_limit_ms = 0;
            sync_store_i(&s->open, 0);
            last_reset = reset_seq;
            last_source = source;
        }

        if(source == VJ_AUDIO_SYNC_SOURCE_JACK) {
            long stored;
            if(!vj_jack_is_running() || !vj_jack_has_input()) {
                if(!sync_configure_jack(s)) {
                    sync_sleep_us(250000);
                    continue;
                }
                vj_jack_reset_input();
            }

            ch = vj_jack_get_input_channels();
            frame_bytes = (int)vj_jack_get_bytes_per_input_frame();
            rate = vj_jack_get_client_samplerate();
            if(rate <= 0)
                rate = vj_jack_get_rate();
            if(ch <= 0 || frame_bytes <= 0 || rate <= 0) {
                sync_store_i(&s->open, 0);
                sync_sleep_us(100000);
                continue;
            }
            bits = (frame_bytes * 8) / ch;

            target_frames = (rate / 1000) * 8; /* ~8ms */
            if(target_frames < VJ_AUDIO_SYNC_ANALYSIS_FRAMES)
                target_frames = VJ_AUDIO_SYNC_ANALYSIS_FRAMES;
            if(target_frames > VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES)
                target_frames = VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES;
            target_bytes = target_frames * frame_bytes;

            if(buffer_size < target_bytes) {
                uint8_t *nb = (uint8_t*)vj_malloc((size_t)target_bytes);
                if(!nb) {
                    sync_sleep_us(100000);
                    continue;
                }
                if(buffer) free(buffer);
                buffer = nb;
                buffer_size = target_bytes;
            }

            stored = vj_jack_get_input_bytes_stored();
            if(stored < frame_bytes * 32) {
                sync_sleep_us(1000);
                continue;
            }
            if(stored > target_bytes)
                stored = target_bytes;
            stored -= stored % frame_bytes;
            if(stored <= 0)
                continue;

            got = vj_jack_capture_read(buffer, (int)stored);
            if(got <= 0) {
                sync_add_l(&s->underruns, 1);
                sync_sleep_us(2000);
                continue;
            }
            got -= got % frame_bytes;
            sync_store_l(&s->overruns, vj_jack_input_overruns());
        }
        else if(source == VJ_AUDIO_SYNC_SOURCE_WAV_FILE) {
            int gen = sync_load_i(&s->file_generation);
            if(!wav.fp || gen != last_file_generation) {
                char path[VJ_AUDIO_SYNC_PATH_MAX];
                sync_lock(s);
                strncpy(path, s->wav_path, sizeof(path) - 1);
                path[sizeof(path) - 1] = '\0';
                sync_unlock(s);

                sync_wav_close(&wav);
                if(!sync_wav_open(&wav, path)) {
                    veejay_msg(VEEJAY_MSG_WARNING,
                               "[AUDIO-SYNC] unable to open WAV source '%s'",
                               path);
                    sync_store_i(&s->open, 0);
                    sync_sleep_us(1000000);
                    last_file_generation = gen;
                    continue;
                }
                last_file_generation = gen;
                last_wav_limit_ms = sync_wav_side_get_limit(s);
                wav_limit_frames = 0;
                wav_read_frames_total = 0;
                if(last_wav_limit_ms > 0 && wav.sample_rate > 0)
                    wav_limit_frames = ((long long)wav.sample_rate * (long long)last_wav_limit_ms) / 1000LL;
                sync_prepare_ring(s, wav.channels, wav.bits, wav.sample_rate);
                veejay_msg(VEEJAY_MSG_INFO,
                           "[AUDIO-SYNC] WAV source ready: '%s' %dch %dHz %dbit",
                           path, wav.channels, wav.sample_rate, wav.bits);
            }

            ch = wav.channels;
            bits = wav.bits;
            rate = wav.sample_rate;
            frame_bytes = wav.frame_bytes;
            target_frames = (rate / 1000) * 10; /* ~10ms */
            if(target_frames < VJ_AUDIO_SYNC_ANALYSIS_FRAMES)
                target_frames = VJ_AUDIO_SYNC_ANALYSIS_FRAMES;
            if(target_frames > VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES)
                target_frames = VJ_AUDIO_SYNC_MAX_ANALYSIS_FRAMES;
            target_bytes = target_frames * frame_bytes;

            if(buffer_size < target_bytes) {
                uint8_t *nb = (uint8_t*)vj_malloc((size_t)target_bytes);
                if(!nb) {
                    sync_sleep_us(100000);
                    continue;
                }
                if(buffer) free(buffer);
                buffer = nb;
                buffer_size = target_bytes;
            }

            if(wav_limit_frames > 0) {
                long long remaining_frames = wav_limit_frames - wav_read_frames_total;

                if(remaining_frames <= 0) {
                    sync_store_i(&s->open, 0);
                    sync_store_i(&s->enabled, 0);
                    continue;
                }

                if((long long)target_bytes > remaining_frames * (long long)frame_bytes)
                    target_bytes = (int)(remaining_frames * (long long)frame_bytes);
            }

            got = sync_wav_read(&wav, buffer, target_bytes, sync_load_i(&s->wav_loop));
            if(got <= 0) {
                sync_store_i(&s->open, 0);
                sync_store_i(&s->enabled, 0);
                continue;
            }

            if(frame_bytes > 0)
                wav_read_frames_total += got / frame_bytes;

            if(rate > 0 && frame_bytes > 0)
                sync_sleep_us(((long long)(got / frame_bytes) * 1000000LL) / rate);
        }
        else if(source == VJ_AUDIO_SYNC_SOURCE_PUSH) {
            sync_sleep_us(2000);
            continue;
        }
        else {
            sync_store_i(&s->open, 0);
            sync_sleep_us(20000);
            continue;
        }

        if(got > 0 && frame_bytes > 0) {
            int frames = got / frame_bytes;
            sync_publish_audio(s, buffer, frames, ch, bits, rate);
            sync_update_clock_from_block(s, buffer, frames, frame_bytes, ch, bits, rate);
            if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
                sync_tempo_follow_publish(s);
            if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
                sync_track_align_push_block(s, buffer, frames, frame_bytes, ch, bits, rate, 0);
            sync_add_l(&s->reads, 1);
        }

        (void)mode;
    }

    sync_wav_close(&wav);
    if(buffer)
        free(buffer);
    if(target_buffer)
        free(target_buffer);

    vj_audio_sync_free(s);
    sync_store_i(&s->open, 0);
    sync_store_i(&s->running, 0);
    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO-SYNC] external audio sync thread stopped");
    return NULL;
}

int vj_audio_sync_enable(vj_audio_sync_shared_t *s)
{
    if(!s)
        return 0;
    if(!sync_load_i(&s->initialized))
        vj_audio_sync_init(s, 2);
    sync_store_i(&s->enabled, 1);
    return 1;
}

int vj_audio_sync_disable(vj_audio_sync_shared_t *s)
{
    if(!s)
        return 0;
    sync_store_i(&s->enabled, 0);
    sync_store_i(&s->open, 0);
    sync_store_i(&s->bridge_active, 0);
    return 1;
}

int vj_audio_sync_is_enabled(vj_audio_sync_shared_t *s)
{
    return s ? (sync_load_i(&s->enabled) ? 1 : 0) : 0;
}

int vj_audio_sync_is_running(vj_audio_sync_shared_t *s)
{
    return s ? (sync_load_i(&s->running) ? 1 : 0) : 0;
}

int vj_audio_sync_is_open(vj_audio_sync_shared_t *s)
{
    return s ? (sync_load_i(&s->open) ? 1 : 0) : 0;
}

int vj_audio_sync_get_mode(vj_audio_sync_shared_t *s)
{
    return s ? sync_load_i(&s->mode) : VJ_AUDIO_SYNC_MODE_OFF;
}

void vj_audio_sync_set_mode(vj_audio_sync_shared_t *s, int mode)
{
    int old_mode;

    if(!s)
        return;

    if(mode < VJ_AUDIO_SYNC_MODE_OFF)
        mode = VJ_AUDIO_SYNC_MODE_OFF;
    else if(mode > VJ_AUDIO_SYNC_MODE_MAX)
        mode = VJ_AUDIO_SYNC_MODE_MAX;

    sync_lock(s);
    old_mode = sync_load_i(&s->mode);

    if(old_mode != mode) {
        sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);

        if(old_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
           mode     == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
           old_mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW ||
           mode     == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW)
        {
            s->bridge_read_valid = 0;
            s->bridge_ratio_smooth = 0.0;
            s->bridge_last_correction = 1.0;
            s->bridge_latch_updated_ms = 0;
            s->bridge_source_bpm_latched = 0.0;
            s->bridge_target_bpm_latched = 0.0;
            s->bridge_bpm_latch_valid = 0;
            sync_store_i(&s->bridge_active, 0);
            sync_store_i(&s->bridge_state,
                         (mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
                          mode == VJ_AUDIO_SYNC_MODE_TEMPO_FOLLOW) ?
                         VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE :
                         VJ_AUDIO_SYNC_BRIDGE_STATE_IDLE);
        }

        if(old_mode == VJ_AUDIO_SYNC_MODE_MONITOR ||
           mode     == VJ_AUDIO_SYNC_MODE_MONITOR ||
           old_mode == VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY ||
           mode     == VJ_AUDIO_SYNC_MODE_MONITOR_TRICKPLAY ||
           old_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
           mode     == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        {
            sync_clear_monitor_transport_locked(s);
        }

        if(old_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN ||
           mode     == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        {
            sync_track_align_clear_locked(s);
            sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);
            if(mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
                sync_store_i(&s->target_mode, VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP);
                sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE);
            } else {
                sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_IDLE);
            }
        }
    }

    sync_store_i(&s->mode, mode);
    sync_unlock(s);
}

void vj_audio_sync_set_input_channels(vj_audio_sync_shared_t *s, int channels)
{
    if(!s)
        return;
    if(channels < 1)
        channels = 1;
    else if(channels > 2)
        channels = 2;
    if(sync_load_i(&s->input_channels_request) != channels) {
        sync_store_i(&s->input_channels_request, channels);
        sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);
    }
}

void vj_audio_sync_set_source_jack(vj_audio_sync_shared_t *s, int channels)
{
    int old_source;
    int old_channels;

    if(!s)
        return;

    if(channels < 1)
        channels = 1;
    else if(channels > 2)
        channels = 2;

    old_source = sync_load_i(&s->source);
    old_channels = sync_load_i(&s->input_channels_request);

    if(old_source == VJ_AUDIO_SYNC_SOURCE_JACK &&
       old_channels == channels)
    {
        return;
    }

    sync_lock(s);
    sync_store_i(&s->input_channels_request, channels);
    sync_store_i(&s->source, VJ_AUDIO_SYNC_SOURCE_JACK);
    sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);

    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {
        sync_track_align_clear_locked(s);
        s->align_last_snap_ms = 0;
        s->align_live_last_snap_ms = 0;
        s->align_live_last_snap_delta = 0;
    }

    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_clear_monitor_transport_locked(s);
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);

    sync_clear_clock(s);
    sync_track_align_clear_locked(s);
    sync_unlock(s);
}

void vj_audio_sync_set_source_push(vj_audio_sync_shared_t *s,
                                   int channels,
                                   int bits_per_channel,
                                   int sample_rate)
{
    int old_source;
    int old_channels;
    int old_bits;
    int old_rate;

    if(!s)
        return;

    if(channels < 1)
        channels = 1;
    else if(channels > 2)
        channels = 2;

    if(bits_per_channel != 8 && bits_per_channel != 16)
        bits_per_channel = 16;

    if(sample_rate <= 0)
        sample_rate = 44100;

    old_source = sync_load_i(&s->source);
    old_channels = sync_load_i(&s->channels);
    old_bits = sync_load_i(&s->bits_per_channel);
    old_rate = sync_load_i(&s->sample_rate);

    if(old_source == VJ_AUDIO_SYNC_SOURCE_PUSH &&
       old_channels == channels &&
       old_bits == bits_per_channel &&
       old_rate == sample_rate)
    {
        return;
    }

    sync_lock(s);
    sync_store_i(&s->source, VJ_AUDIO_SYNC_SOURCE_PUSH);
    sync_store_i(&s->input_channels_request, channels);
    sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);
    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_clear_monitor_transport_locked(s);
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
    sync_clear_clock(s);
    sync_track_align_clear_locked(s);
    sync_unlock(s);
}

int vj_audio_sync_push_audio(vj_audio_sync_shared_t *s,
                             const uint8_t *src,
                             int frames,
                             int frame_bytes,
                             int channels,
                             int sample_rate)
{
    int bits;

    if(!s || !src || frames <= 0 || frame_bytes <= 0 ||
       channels <= 0 || sample_rate <= 0)
        return 0;

    if(!sync_load_i(&s->enabled))
        return 0;

    if(sync_load_i(&s->source) != VJ_AUDIO_SYNC_SOURCE_PUSH)
        return 0;

    if(frame_bytes % channels != 0)
        return 0;

    bits = (frame_bytes / channels) * 8;
    if(bits != 8 && bits != 16)
        return 0;

    if(!sync_prepare_ring(s, channels, bits, sample_rate))
        return 0;

    sync_publish_audio(s, src, frames, channels, bits, sample_rate);
    sync_update_clock_from_block(s, src, frames, frame_bytes, channels, bits, sample_rate);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        sync_track_align_push_block(s, src, frames, frame_bytes, channels, bits, sample_rate, 0);
    sync_add_l(&s->reads, 1);
    return frames;
}

int vj_audio_sync_set_source_wav_limited(vj_audio_sync_shared_t *s,
                                         const char *path,
                                         int loop,
                                         int limit_ms);

int vj_audio_sync_set_source_wav(vj_audio_sync_shared_t *s, const char *path, int loop)
{
    return vj_audio_sync_set_source_wav_limited(s, path, loop, 0);
}

int vj_audio_sync_set_source_wav_limited(vj_audio_sync_shared_t *s,
                                         const char *path,
                                         int loop,
                                         int limit_ms)
{
    if(!s || !path || !path[0])
        return 0;

    sync_wav_side_set_limit(s, loop ? 0 : limit_ms);
    sync_wav_plain_lock_clear(s);

    sync_lock(s);
    strncpy(s->wav_path, path, sizeof(s->wav_path) - 1);
    s->wav_path[sizeof(s->wav_path) - 1] = '\0';
    sync_store_i(&s->wav_loop, loop ? 1 : 0);
    sync_store_i(&s->source, VJ_AUDIO_SYNC_SOURCE_WAV_FILE);
    sync_store_i(&s->file_generation, sync_load_i(&s->file_generation) + 1);
    sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);

    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_clear_monitor_transport_locked(s);
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
    sync_clear_clock(s);
    sync_track_align_clear_locked(s);
    sync_unlock(s);
    return 1;
}

void vj_audio_sync_wav_restart(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    sync_lock(s);
    sync_store_i(&s->file_generation, sync_load_i(&s->file_generation) + 1);
    sync_store_i(&s->reset_seq, sync_load_i(&s->reset_seq) + 1);
    s->analysis_read_frame = 0;
    s->beat_read_frame = 0;
    s->record_read_frame = 0;
    s->bridge_read_valid = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_source_bpm_latched = 0.0;
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    sync_clear_monitor_transport_locked(s);
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE);
    sync_clear_clock(s);
    sync_track_align_clear_locked(s);
    sync_unlock(s);
}

void vj_audio_sync_wav_plain_lock_store(vj_audio_sync_shared_t *s,
                                        int delta_frames,
                                        int confidence_pct)
{
    sync_wav_side_t *side;

    if(confidence_pct < 0)
        confidence_pct = 0;
    else if(confidence_pct > 100)
        confidence_pct = 100;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 1);
    if(side) {
        side->plain_lock_valid = 1;
        side->plain_lock_delta_frames = delta_frames;
        side->plain_lock_confidence_pct = confidence_pct;
    }
    sync_wav_side_unlock();
}

int vj_audio_sync_wav_plain_lock_get(vj_audio_sync_shared_t *s,
                                     int *delta_frames,
                                     int *confidence_pct)
{
    sync_wav_side_t *side;
    int valid = 0;

    sync_wav_side_lock();
    side = sync_wav_side_get_locked(s, 0);
    if(side && side->plain_lock_valid) {
        if(delta_frames)
            *delta_frames = side->plain_lock_delta_frames;
        if(confidence_pct)
            *confidence_pct = side->plain_lock_confidence_pct;
        valid = 1;
    }
    sync_wav_side_unlock();

    return valid;
}

int vj_audio_sync_wav_plain_lock_valid(vj_audio_sync_shared_t *s)
{
    return vj_audio_sync_wav_plain_lock_get(s, NULL, NULL);
}

void vj_audio_sync_set_target_clock(vj_audio_sync_shared_t *s,
                                    float bpm,
                                    float phase,
                                    float confidence)
{
    if(!s)
        return;
    if(bpm < 0.0f) bpm = 0.0f;
    if(phase < 0.0f) phase = 0.0f;
    if(phase > 1.0f) phase = 1.0f;
    if(confidence < 0.0f) confidence = 0.0f;
    if(confidence > 1.0f) confidence = 1.0f;

    sync_lock(s);
    sync_store_i(&s->target_mode, VJ_AUDIO_SYNC_TARGET_MANUAL);
    sync_store_i(&s->target_bpm_q8, sync_q8_bpm((double)bpm));
    sync_store_i(&s->target_phase_q15, sync_q15((double)phase));
    sync_store_i(&s->target_confidence_q15, sync_q15((double)confidence));

    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_ratio_smooth = 0.0;
    s->bridge_last_correction = 1.0;
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state,
                 (bpm > 0.0f) ?
                 VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE :
                 VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET);
    sync_unlock(s);
}

void vj_audio_sync_set_target_mode(vj_audio_sync_shared_t *s, int mode)
{
    if(!s)
        return;

    if(mode != VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        mode = VJ_AUDIO_SYNC_TARGET_MANUAL;

    sync_lock(s);
    if(sync_load_i(&s->target_mode) == mode) {
        sync_unlock(s);
        return;
    }

    sync_store_i(&s->target_mode, mode);

    if(mode == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        sync_clear_target_clock(s);

    /* Target mode changes invalidate the BPM latch, but keep the live
     * read-head continuous to avoid audible capstan jumps.
     */
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_last_correction = 1.0;
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET);
    sync_unlock(s);
}

int vj_audio_sync_get_target_mode(vj_audio_sync_shared_t *s)
{
    if(!s)
        return VJ_AUDIO_SYNC_TARGET_MANUAL;
    return sync_load_i(&s->target_mode);
}

void vj_audio_sync_set_track_align_video_fps(vj_audio_sync_shared_t *s, double fps)
{
    int q;
    int old_q;

    if(!s)
        return;
    if(fps < 1.0)
        fps = 25.0;
    else if(fps > 240.0)
        fps = 240.0;
    q = (int)(fps * 1000.0 + 0.5);
    old_q = sync_load_i(&s->align_video_fps_x1000);
    sync_store_i(&s->align_video_fps_x1000, q);

    if(old_q != q) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FPSDBG][SYNC] track-align video fps %.3f->%.3f mode=%d target=%d "
                   "state=%d locked=%d offset=%dms conf=%d%% reset_seq=%d",
                   old_q > 0 ? ((double)old_q / 1000.0) : 0.0,
                   (double)q / 1000.0,
                   sync_load_i(&s->mode),
                   sync_load_i(&s->target_mode),
                   sync_load_i(&s->track_align_state),
                   sync_load_i(&s->track_align_locked),
                   sync_load_i(&s->track_align_offset_ms),
                   sync_load_i(&s->track_align_confidence_pct),
                   sync_load_i(&s->reset_seq));
    }
}

void vj_audio_sync_reset_target_clock(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FPSDBG][SYNC] reset target clock before write=%lld read=%lld process=%lld "
                   "overruns=%ld reads=%ld mode=%d target=%d state=%d",
                   s->target_write_frame_abs,
                   s->target_read_frame_abs,
                   s->target_process_frame_abs,
                   sync_load_l(&s->target_queue_overruns),
                   sync_load_l(&s->target_queue_reads),
                   sync_load_i(&s->mode),
                   sync_load_i(&s->target_mode),
                   sync_load_i(&s->track_align_state));
    }

    sync_lock(s);
    sync_clear_target_clock(s);
    s->bridge_target_bpm_latched = 0.0;
    s->bridge_bpm_latch_valid = 0;
    s->bridge_latch_updated_ms = 0;
    s->bridge_last_correction = 1.0;
    sync_store_i(&s->bridge_active, 0);
    sync_store_i(&s->bridge_state, VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET);
    sync_unlock(s);
}

int vj_audio_sync_push_target_audio(vj_audio_sync_shared_t *s,
                                    const uint8_t *src,
                                    int frames,
                                    int frame_bytes,
                                    int channels,
                                    int sample_rate)
{
    int bits;

    if(!s || !src || frames <= 0 || frame_bytes <= 0 ||
       channels <= 0 || sample_rate <= 0)
        return 0;

    if(!sync_load_i(&s->enabled))
        return 0;

    if(sync_load_i(&s->target_mode) != VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return 0;

    if(frame_bytes % channels != 0)
        return 0;

    bits = (frame_bytes / channels) * 8;
    if(bits != 8 && bits != 16)
        return 0;

    return sync_enqueue_target_audio(s,
                                     src,
                                     frames,
                                     frame_bytes,
                                     channels,
                                     bits,
                                     sample_rate);
}

void vj_audio_sync_set_bridge_correction(vj_audio_sync_shared_t *s, int max_pct)
{
    if(!s)
        return;
    if(max_pct < 0) max_pct = 0;
    if(max_pct > 25) max_pct = 25;

    sync_lock(s);
    sync_store_i(&s->max_correction_pct, max_pct);

    s->bridge_last_correction = 1.0;
    sync_unlock(s);
}


void vj_audio_sync_track_align_reset(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    sync_lock(s);
    sync_track_align_clear_locked(s);
    if(sync_load_i(&s->mode) == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        sync_store_i(&s->track_align_state, VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE);
    sync_unlock(s);
}

void vj_audio_sync_track_align_reset_target(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;
    sync_lock(s);
    sync_track_align_clear_target_locked(s);
    sync_unlock(s);
}

void vj_audio_sync_track_align_reset_acquisition(vj_audio_sync_shared_t *s)
{
    if(!s)
        return;

    sync_lock(s);
    sync_track_align_clear_target_locked(s);
    s->align_last_snap_ms = 0;
    s->align_live_last_snap_ms = 0;
    s->align_live_last_snap_delta = 0;
    s->align_snap_cooldown_ms = VJ_AUDIO_SYNC_TRACK_SNAP_OFFER_COOLDOWN_MS;
    sync_store_i(&s->track_align_snap_pending, 0);
    sync_store_i(&s->track_align_snap_delta_frames, 0);
    sync_store_i(&s->track_align_snap_confidence_pct, 0);
    sync_unlock(s);
}

int vj_audio_sync_track_align_frame_action(vj_audio_sync_shared_t *s,
                                           double video_fps,
                                           int current_speed)
{
    (void)video_fps;

    if(!s)
        return 1;
    if(sync_load_i(&s->mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return 1;
    if(current_speed < 0)
        return 1;

    sync_store_i(&s->track_align_correction_ppm, 0);
    s->align_last_frame_action = 1;
    return 1;
}

int vj_audio_sync_get_format(vj_audio_sync_shared_t *s,
                             int *channels,
                             int *bytes_per_frame,
                             int *bits_per_channel,
                             int *sample_rate)
{
    if(!s || !sync_load_i(&s->open))
        return 0;

    if(channels) *channels = sync_load_i(&s->channels);
    if(bytes_per_frame) *bytes_per_frame = sync_load_i(&s->bytes_per_frame);
    if(bits_per_channel) *bits_per_channel = sync_load_i(&s->bits_per_channel);
    if(sample_rate) *sample_rate = sync_load_i(&s->sample_rate);

    return sync_load_i(&s->channels) > 0 &&
           sync_load_i(&s->bytes_per_frame) > 0 &&
           sync_load_i(&s->sample_rate) > 0;
}

int vj_audio_sync_get_snapshot(vj_audio_sync_shared_t *s,
                               vj_audio_sync_snapshot_t *dst)
{
    if(!s || !dst)
        return 0;

    memset(dst, 0, sizeof(*dst));
    dst->enabled = sync_load_i(&s->enabled) ? 1 : 0;
    dst->open = sync_load_i(&s->open) ? 1 : 0;
    dst->running = sync_load_i(&s->running) ? 1 : 0;
    dst->mode = sync_load_i(&s->mode);
    dst->source = sync_load_i(&s->source);
    dst->target_mode = sync_load_i(&s->target_mode);
    dst->channels = sync_load_i(&s->channels);
    dst->bytes_per_frame = sync_load_i(&s->bytes_per_frame);
    dst->bits_per_channel = sync_load_i(&s->bits_per_channel);
    dst->sample_rate = sync_load_i(&s->sample_rate);
    dst->level = sync_from_q15(sync_load_i(&s->level_q15));
    dst->envelope = sync_from_q15(sync_load_i(&s->envelope_q15));
    dst->transient = sync_from_q15(sync_load_i(&s->transient_q15));
    dst->bpm = (float)sync_bpm_from_q8(sync_load_i(&s->source_bpm_q8));
    dst->beat_phase = sync_from_q15(sync_load_i(&s->source_phase_q15));
    dst->confidence = sync_from_q15(sync_load_i(&s->confidence_q15));
    dst->target_bpm = (float)sync_bpm_from_q8(sync_load_i(&s->target_bpm_q8));
    dst->target_phase = sync_from_q15(sync_load_i(&s->target_phase_q15));
    dst->target_confidence = sync_from_q15(sync_load_i(&s->target_confidence_q15));
    dst->hits = sync_load_l(&s->hits);
    dst->last_hit_ms = sync_load_l(&s->last_hit_ms);
    dst->overruns = sync_load_l(&s->overruns);
    dst->underruns = sync_load_l(&s->underruns);
    dst->reads = sync_load_l(&s->reads);
    dst->max_correction_pct = sync_load_i(&s->max_correction_pct);
    dst->bridge_state = sync_load_i(&s->bridge_state);
    dst->track_align_locked = sync_load_i(&s->track_align_locked);
    dst->track_align_offset_ms = sync_load_i(&s->track_align_offset_ms);
    dst->track_align_confidence_pct = sync_load_i(&s->track_align_confidence_pct);
    dst->track_align_correction_ppm = sync_load_i(&s->track_align_correction_ppm);
    dst->track_align_state = sync_load_i(&s->track_align_state);
    dst->track_align_snap_pending = sync_load_i(&s->track_align_snap_pending);
    dst->track_align_snap_delta_frames = sync_load_i(&s->track_align_snap_delta_frames);
    dst->track_align_snap_confidence_pct = sync_load_i(&s->track_align_snap_confidence_pct);

    sync_lock(s);
    dst->bridge_ratio = s->bridge_ratio_smooth;
    dst->bridge_correction = s->bridge_last_correction;
    sync_unlock(s);

    return 1;
}

#endif /* HAVE_JACK */
