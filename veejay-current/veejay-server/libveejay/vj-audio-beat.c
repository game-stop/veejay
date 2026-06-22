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
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sched.h>
#include <veejaycore/defs.h>
#include <veejaycore/atomic.h>
#include <libsubsample/subsample.h>
#include <libvje/vje.h>
#include <libvje/libvje.h>
#include <libsample/sampleadm.h>
#include <libveejay/vjkf.h>
#include <libveejay/vj-lib.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vevo.h>
#include <veejaycore/libvevo.h>
#include <libveejay/vj-jack.h>
#include <libveejay/vj-audio-sync.h>
#include "vj-audio-beat.h"

#ifndef VJ_AUDIO_CTRL_SCRATCH_ENVELOPE
#define VJ_AUDIO_CTRL_SCRATCH_ENVELOPE 17
#endif
#ifndef VJ_AUDIO_CTRL_SCRATCH_DIRECTION
#define VJ_AUDIO_CTRL_SCRATCH_DIRECTION 18
#endif
#ifndef VJ_AUDIO_CTRL_SCRATCH_SIGNED
#define VJ_AUDIO_CTRL_SCRATCH_SIGNED 19
#endif

extern int veejay_set_speed(veejay_t *v, int speed, int force_seek);
extern int veejay_set_frame(veejay_t *info, long framenum);
extern void veejay_set_framerate(veejay_t *info, float fps);

static volatile int ab_transport_command_depth = 0;

static int ab_set_speed_from_beat(veejay_t *v, int speed, int force_seek)
{
    int r;

    __sync_add_and_fetch(&ab_transport_command_depth, 1);
    r = veejay_set_speed(v, speed, force_seek);
    __sync_sub_and_fetch(&ab_transport_command_depth, 1);

    return r;
}

int vj_audio_beat_transport_is_internal(vj_audio_beat_shared_t *s)
{
    (void)s;
    return __sync_fetch_and_add(&ab_transport_command_depth, 0) > 0;
}

#ifndef VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX
#define VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX 3
#endif
#ifndef VJ_AUDIO_BEAT_ACTION_BREAK_BEAT
#define VJ_AUDIO_BEAT_ACTION_BREAK_BEAT 4
#endif

#ifndef VJ_BEAT_SOFT_UNSET
#define VJ_BEAT_SOFT_UNSET INT_MIN
#endif

static inline int ab_action_is_breakbeat(int action)
{
    return action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT ||
           action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX;
}

static inline int ab_action_uses_auto_fx(int action)
{
    return action == VJ_AUDIO_BEAT_ACTION_AUTO_FX ||
           action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX;
}


//#define VEEJAY_AUDIO_BEAT_DEBUG 1
//#define VEEJAY_AUDIO_BEAT_AUTO_DEBUG 1
//#define VEEJAY_AUDIO_BEAT_TRACE_REJECTS 1

#ifndef VEEJAY_AUDIO_BEAT_DEBUG_INTERVAL_MS
#define VEEJAY_AUDIO_BEAT_DEBUG_INTERVAL_MS 1000L
#endif

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
#define AB_DBG(fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO-BEAT-DBG] " fmt, ##__VA_ARGS__)
#else
#define AB_DBG(fmt, ...) do { } while(0)
#endif

#ifndef VEEJAY_AUDIO_BEAT_AUTO_DEBUG_INTERVAL_MS
#define VEEJAY_AUDIO_BEAT_AUTO_DEBUG_INTERVAL_MS 700L
#endif

#ifndef VEEJAY_AUDIO_BEAT_AUTO_SIG_CHECK_MS
#define VEEJAY_AUDIO_BEAT_AUTO_SIG_CHECK_MS 250L
#endif

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
#define AB_AUTO_DBG(fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO-BEAT-AUTO-DBG] " fmt, ##__VA_ARGS__)
#else
#define AB_AUTO_DBG(fmt, ...) do { } while(0)
#endif

#ifndef VEEJAY_AUDIO_BEAT_BREAKBEAT_FRAME_DEBUG
#define VEEJAY_AUDIO_BEAT_BREAKBEAT_FRAME_DEBUG 0
#endif

#ifndef VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE
#define VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE 0
#endif

#ifndef VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE_INTERVAL_MS
#define VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE_INTERVAL_MS 250L
#endif

#ifndef VEEJAY_AUDIO_BEAT_LATENCY_TRACE
#define VEEJAY_AUDIO_BEAT_LATENCY_TRACE 1
#endif

#ifndef VEEJAY_AUDIO_BEAT_WINDOW_MIN_MS
#define VEEJAY_AUDIO_BEAT_WINDOW_MIN_MS 20
#endif
#ifndef VEEJAY_AUDIO_BEAT_WINDOW_MAX_MS
#define VEEJAY_AUDIO_BEAT_WINDOW_MAX_MS 1000
#endif
#ifndef VEEJAY_AUDIO_BEAT_COOLDOWN_MIN_MS
#define VEEJAY_AUDIO_BEAT_COOLDOWN_MIN_MS 20
#endif
#ifndef VEEJAY_AUDIO_BEAT_COOLDOWN_MAX_MS
#define VEEJAY_AUDIO_BEAT_COOLDOWN_MAX_MS 2000
#endif
#ifndef VEEJAY_AUDIO_BEAT_PULSE_MIN_MS
#define VEEJAY_AUDIO_BEAT_PULSE_MIN_MS 20
#endif
#ifndef VEEJAY_AUDIO_BEAT_PULSE_MAX_MS
#define VEEJAY_AUDIO_BEAT_PULSE_MAX_MS 2000
#endif
#ifndef VEEJAY_AUDIO_BEAT_GATE_MIN_MS
#define VEEJAY_AUDIO_BEAT_GATE_MIN_MS 10
#endif
#ifndef VEEJAY_AUDIO_BEAT_GATE_MAX_MS
#define VEEJAY_AUDIO_BEAT_GATE_MAX_MS 1000
#endif
#ifndef VEEJAY_AUDIO_BEAT_THRESHOLD_MIN
#define VEEJAY_AUDIO_BEAT_THRESHOLD_MIN 30
#endif
#ifndef VEEJAY_AUDIO_BEAT_THRESHOLD_MAX
#define VEEJAY_AUDIO_BEAT_THRESHOLD_MAX 400
#endif


#if VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE
#define AB_BREAK_TRACE(fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO-BEAT][BREAK] " fmt, ##__VA_ARGS__)
#else
#define AB_BREAK_TRACE(fmt, ...) do { } while(0)
#endif


#if VEEJAY_AUDIO_BEAT_BREAKBEAT_FRAME_DEBUG
#define AB_BREAK_FRAME_DBG(fmt, ...) \
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO-BEAT][BREAK-FRAME] " fmt, ##__VA_ARGS__)
#else
#define AB_BREAK_FRAME_DBG(fmt, ...) do { } while(0)
#endif


#ifndef VEEJAY_AUDIO_BEAT_ARM_BLOCKS
#define VEEJAY_AUDIO_BEAT_ARM_BLOCKS 10
#endif

#ifndef VEEJAY_AUDIO_BEAT_ADAPT_WARMUP_BLOCKS
#define VEEJAY_AUDIO_BEAT_ADAPT_WARMUP_BLOCKS 32
#endif

#ifndef VEEJAY_AUDIO_BEAT_SETTLE_BLOCKS
#define VEEJAY_AUDIO_BEAT_SETTLE_BLOCKS 18
#endif

#ifndef VEEJAY_AUDIO_BEAT_MIN_RELIABLE_RMS
#define VEEJAY_AUDIO_BEAT_MIN_RELIABLE_RMS 0.0135
#endif

#ifndef VEEJAY_AUDIO_BEAT_FIRST_HIT_MIN_RMS
#define VEEJAY_AUDIO_BEAT_FIRST_HIT_MIN_RMS 0.0200
#endif

#ifndef VEEJAY_AUDIO_BEAT_MIN_AUTO_COOLDOWN_MS
#define VEEJAY_AUDIO_BEAT_MIN_AUTO_COOLDOWN_MS 170L
#endif

#ifndef VEEJAY_AUDIO_BEAT_MAX_AUTO_COOLDOWN_MS
#define VEEJAY_AUDIO_BEAT_MAX_AUTO_COOLDOWN_MS 520L
#endif

#ifndef VEEJAY_AUDIO_BEAT_MAX_ANALYSIS_FRAMES
#define VEEJAY_AUDIO_BEAT_MAX_ANALYSIS_FRAMES 2048
#endif

#ifndef VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS
#define VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS 200L   /* 300 BPM */
#endif

#ifndef VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS
#define VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS 2400L  /* 25 BPM */
#endif

#ifndef VEEJAY_AUDIO_BEAT_TARGET_ANALYSIS_MS
#define VEEJAY_AUDIO_BEAT_TARGET_ANALYSIS_MS 8L
#endif

#ifndef VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES
#define VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES 96
#endif

#ifndef VEEJAY_AUDIO_BEAT_BACKLOG_YIELD_US
#define VEEJAY_AUDIO_BEAT_BACKLOG_YIELD_US 1000L
#endif

#ifndef VEEJAY_AUDIO_BEAT_SYNC_READ_ARM_MS
#define VEEJAY_AUDIO_BEAT_SYNC_READ_ARM_MS 35L
#endif

#ifndef VEEJAY_AUDIO_BEAT_SYNC_READ_SLOW_MS
#define VEEJAY_AUDIO_BEAT_SYNC_READ_SLOW_MS 25L
#endif

enum
{
    AB_HIT_NONE = 0,
    AB_HIT_KICK = 1,
    AB_HIT_SNARE = 2,
    AB_HIT_HAT = 3,
    AB_HIT_FULL = 4,
    AB_HIT_SCRATCH = 5
};

static int ab_classify_hit(double kick_score, double snare_score, double hat_score)
{
    if(kick_score >= 0.38 &&
       kick_score >= snare_score * 1.05 &&
       kick_score >= hat_score * 1.15)
        return AB_HIT_KICK;

    if(snare_score >= 0.42 &&
       snare_score >= kick_score * 0.90 &&
       snare_score >= hat_score * 0.90)
        return AB_HIT_SNARE;

    if(hat_score >= 0.45 &&
       hat_score > kick_score &&
       hat_score > snare_score)
        return AB_HIT_HAT;

    if(kick_score >= 0.32 || snare_score >= 0.36)
        return AB_HIT_FULL;

    return AB_HIT_NONE;
}

static inline int ab_is_body_hit(int hit_kind)
{
    return hit_kind == AB_HIT_KICK ||
           hit_kind == AB_HIT_SNARE ||
           hit_kind == AB_HIT_FULL;
}

static inline int ab_is_scratch_hit(int hit_kind)
{
    return hit_kind == AB_HIT_SCRATCH;
}

static inline const char *ab_hit_kind_name(int hit_kind)
{
    switch(hit_kind)
    {
        case AB_HIT_KICK:    return "kick";
        case AB_HIT_SNARE:   return "snare";
        case AB_HIT_HAT:     return "hat";
        case AB_HIT_FULL:    return "full";
        case AB_HIT_SCRATCH: return "scratch";
        default:             return "none";
    }
}

static inline double ab_bound_period_ms(double period_ms)
{
    if(period_ms < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS)
        return (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS;

    if(period_ms > (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        return (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS;

    return period_ms;
}


#ifndef VEEJAY_AUDIO_BEAT_USE_SAMPLE_LUTS
#define VEEJAY_AUDIO_BEAT_USE_SAMPLE_LUTS 1
#endif

#if VEEJAY_AUDIO_BEAT_USE_SAMPLE_LUTS
static int ab_analysis_luts_ready = 0;
static float ab_s16_norm_lut[65536];
static float ab_u8_norm_lut[256];

static void ab_analysis_luts_init(void)
{
    if(ab_analysis_luts_ready)
        return;

    for(int i = 0; i < 65536; i++)
    {
        int v = i < 32768 ? i : i - 65536;
        ab_s16_norm_lut[i] = (float)v * (1.0f / 32768.0f);
    }

    for(int i = 0; i < 256; i++)
        ab_u8_norm_lut[i] = ((float)i - 128.0f) * (1.0f / 128.0f);

    ab_analysis_luts_ready = 1;
}

static inline float ab_lut_s16(const uint8_t *p)
{
    return ab_s16_norm_lut[(uint16_t)p[0] | ((uint16_t)p[1] << 8)];
}

static inline float ab_lut_u8(const uint8_t *p)
{
    return ab_u8_norm_lut[p[0]];
}
#else
static inline void ab_analysis_luts_init(void) { }
static inline float ab_lut_s16(const uint8_t *p)
{
    int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return (float)v * (1.0f / 32768.0f);
}
static inline float ab_lut_u8(const uint8_t *p)
{
    return ((float)p[0] - 128.0f) * (1.0f / 128.0f);
}
#endif

static inline void ab_accum_mono_sample(
    float mono,
    float *last,
    float low_a,
    float mid_a,
    float *low_lp,
    float *mid_lp,
    float *energy,
    float *flux,
    float *low_sum,
    float *mid_sum,
    float *high_sum)
{
    float a = mono < 0.0f ? -mono : mono;
    float d = mono >= *last ? mono - *last : *last - mono;
    float low;
    float mid_band;
    float high_band;

    *low_lp += low_a * (mono - *low_lp);
    *mid_lp += mid_a * (mono - *mid_lp);

    low = *low_lp;
    mid_band = *mid_lp - *low_lp;
    high_band = mono - *mid_lp;

    *low_sum += low * low;
    *mid_sum += mid_band * mid_band;
    *high_sum += high_band * high_band;
    *energy += a * a;
    *flux += d;
    *last = mono;
}

typedef struct
{
    int open;
    int channels;
    int bytes_per_frame;
    int bits_per_channel;
    int sample_rate;
    int buffer_size;
    int last_reset_seq;
    uint8_t *buffer;

    double fast_energy;
    double slow_energy;
    double fast_flux;
    double slow_flux;
    double envelope;
    double beat_period_ms;
    double last_sample;
    double last_log_energy;

    double rise_mean;
    double rise_var;
    double flux_mean;
    double flux_var;
    double onset_mean;
    double onset_var;
    double onset_smooth;

    double band_low_lp;
    double band_mid_lp;
    double band_low_env;
    double band_mid_env;
    double band_high_env;
    double band_low_slow;
    double band_mid_slow;
    double band_high_slow;

    long last_hit_ms;
    long tempo_last_hit_ms;
    long last_interval_ms;
    int tempo_fast_count;
    int tempo_slow_count;
    double last_accept_score;
    double last_accept_level;
    long body_last_hit_ms;
    long body_last_interval_ms;
    long body_last_accent_ms;
    double body_period_ms;
    double body_last_score;
    double body_last_level;
    int body_last_kind;
    int body_slow_count;
    int body_seed_count;
    long body_cold_probe_ms;
    long body_cold_gap_ms;
    long body_cold_dbg_gap_ms;
    long body_cold_dbg_min_ms;
    long body_cold_dbg_first_ms;
    double body_cold_probe_score;
    double body_cold_probe_level;
    int body_cold_probe_kind;
    long body_cold_meter_last_ms;
    long body_cold_meter_dbg_interval_ms;
    double body_cold_meter_period_ms;
    int body_cold_meter_count;
    double last_kick_score;
    double last_snare_score;
    double last_hat_score;
    double last_scratch_score;
    int last_scratch_candidate_raw;
    int last_scratch_dominant;
    int last_scratch_candidate;
    int last_scratch_reject;
    int last_scratch_gesture_dir;
    long last_scratch_gesture_ms;
    int last_scratch_turn_dir;
    long last_scratch_turn_ms;
    double last_scratch_turn_score;
    int last_scratch_turn_edge;
    double scratch_env;
    double scratch_slow;
    double scratch_burst_env;
    double scratch_last_amount;
    int scratch_dir;
    int last_hit_kind;
    int blocks_seen;

    int filter_sample_rate;
    float filter_low_alpha;
    float filter_mid_alpha;

    int pub_level_q15;
    int pub_envelope_q15;
    int pub_flux_q15;
    int pub_band_low_q15;
    int pub_band_mid_q15;
    int pub_band_high_q15;
    int pub_band_balance_q15;
    int pub_kick_q15;
    int pub_snare_q15;
    int pub_hat_q15;
    int pub_transient_norm_q15;
    int pub_transient_q8;
    int pub_bpm_q8;
    int beat_toggle_state;
    long pub_overruns;
    long sync_read_arm_until_ms;
    long sync_read_last_slow_log_ms;
    int sync_read_probe_pending;
    int sync_read_probe_source;
    int sync_read_probe_seq;

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    long last_debug_ms;
    long debug_blocks;
    long debug_candidates;
    long debug_accepted;
    long debug_cooldown_rejects;
    long debug_silence_rejects;
    long debug_flux_rejects;
    long debug_score_rejects;

    double dbg_energy;
    double dbg_flux;
    double dbg_block_level;
    double dbg_level;
    double dbg_envelope;
    double dbg_bass;
    double dbg_mid;
    double dbg_high;
    double dbg_energy_ratio;
    double dbg_flux_ratio;
    double dbg_score;
    double dbg_min_hit_level;
    double dbg_min_hit_flux;
    int dbg_threshold;
    int dbg_audible;
    int dbg_transient_present;
    int dbg_hit_candidate;
#endif
} vj_audio_beat_thread_t;


typedef struct
{
    int valid;
    int has_range;
    int score;
    int role;
    int invert;
    int amount_pct;
    int impulse;
    int min_value;
    int max_value;
    int has_hint;
    int hint_class;
    unsigned int hint_flags;
    int soft_min;
    int soft_max;
    int normal_depth_pct;
    int climax_depth_pct;
    int attack_ms;
    int release_ms;
    int hold_ms;
    int priority;
} ab_auto_param_meta_t;

typedef struct
{
    int valid;
    int effect_id;
    int chain_pos;
    int param_nr;
    int base_value;
    int min_value;
    int max_value;
    int last_value;
    int score;
    int role;
    int invert;
    int amount_pct;
    int impulse;
    int active;
    int has_hint;
    int hint_class;
    unsigned int hint_flags;
    int soft_min;
    int soft_max;
    int normal_depth_pct;
    int climax_depth_pct;
    int attack_ms;
    int release_ms;
    int hold_ms;
    int priority;
    sample_eff_chain *entry_ptr;
    int curve_owned;
    int curve_mixable;
    float mod_value;
    float raw_value;
    float climax_value;
    int mod_initialized;
    long last_slew_ms;
    long last_change_ms;
} ab_auto_target_t;

typedef struct
{
    int valid;
    int num_params;
    ab_auto_param_meta_t *params;
} ab_auto_fx_meta_t;

static int ab_auto_fx_table_ready = 0;
static int ab_auto_fx_table_building = 0;
static int ab_auto_fx_table_len = 0;
static ab_auto_fx_meta_t *ab_auto_fx_table = NULL;
static volatile int ab_auto_mode = VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION;
static volatile int ab_auto_amount = 75;
static volatile int ab_scratch_sensitivity = 50;
static volatile int ab_source_loss_pause = 1;
static volatile int ab_source_loss_paused = 0;
static volatile int ab_source_seen = 0;
static volatile long ab_source_first_seen_ms = 0;
static volatile long ab_source_last_block_ms = 0;
static volatile long ab_source_last_active_ms = 0;
static volatile int ab_source_level_q15 = 0;
static volatile int ab_output_latency_ms = -1;
static volatile int ab_heard_latency_ms = -1;
static volatile int ab_monitor_latency_ms = VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS;
static volatile int ab_auto_dirty = 1;
static volatile int ab_auto_video_fps_q16 = (25 << 16);
static volatile int ab_band_low_q15 = 0;
static volatile int ab_band_mid_q15 = 0;
static volatile int ab_band_high_q15 = 0;
static volatile int ab_band_balance_q15 = 0;
static float ab_auto_climax_level = 0.0f;
static long ab_auto_climax_last_ms = 0;
static int ab_auto_climax_last_hit_seq = 0;
static float ab_auto_groove_level = 0.0f;
static float ab_auto_phrase_level = 0.0f;
static long ab_auto_groove_last_ms = 0;
static int ab_auto_groove_last_hit_seq = 0;
static long ab_auto_last_apply_ms = 0;
static long ab_auto_signature_last_check_ms = 0;
static int ab_auto_signature_last_chain_len = -1;
static long ab_auto_resume_guard_until_ms = 0;
static int ab_auto_resume_guard_active = 0;
static long ab_auto_debug_last_apply_ms = 0;
static int ab_auto_signature = 0;
static int ab_auto_target_count = 0;
static int ab_auto_active = 0;
static ab_auto_target_t ab_auto_targets[VJ_AUDIO_BEAT_AUTO_MAX_TARGETS];
static volatile int ab_kick_q15 = 0;
static volatile int ab_snare_q15 = 0;
static volatile int ab_hat_q15 = 0;
static volatile int ab_last_hit_kind = AB_HIT_NONE;
static volatile int ab_scratch_q15 = 0;
static volatile int ab_scratch_velocity_q15 = 0;
static volatile int ab_scratch_burst_q15 = 0;
static volatile int ab_scratch_dir = 1;
static volatile int ab_scratch_visual_env_q15 = 0;
static volatile int ab_scratch_visual_dir = 0;
static volatile long ab_scratch_visual_last_ms = 0;
static volatile long ab_scratch_visual_hold_until_ms = 0;
static volatile long ab_scratch_visual_decay_until_ms = 0;

typedef struct
{
    int active;
    int anchor_valid;
    long long anchor_frame;
    int repeat_count;
    int direction;
    int saved_speed;
    int fallback_active;
    int fallback_dir;
    int local_loop_active;
    long local_loop_until_ms;
    float saved_fps;
    float base_fps;
    float current_fps;
    float target_fps;
    float burst_fps;
    long fps_last_ms;
    long fps_write_last_ms;
    long burst_until_ms;
    long long loop_lo;
    long long loop_hi;
    int anchor_scene_id;
    int last_hit_seq;
    long last_hit_ms;
    float music_groove;
    float music_phrase;
    float music_climax;
    long music_last_ms;
    int music_last_hit_seq;
    long last_transport_action_ms;
    float tempo_drive;
    long tempo_last_hit_ms;
    long tempo_prev_interval_ms;
    float rhythm_interval_ema_ms;
    float rhythm_power_ema;
    float rhythm_density;
    float rhythm_regularity;
    float rhythm_accent;
    float rhythm_accel;
    int scratch_transport_dir;
    long scratch_transport_ms;
} ab_breakbeat_state_t;

static ab_breakbeat_state_t ab_breakbeat_state;
static volatile long ab_breakbeat_user_override_until_ms = 0;


static void ab_breakbeat_reset_state(void);

#ifndef AB_BREAKBEAT_USER_PAUSE_OVERRIDE
#define AB_BREAKBEAT_USER_PAUSE_OVERRIDE LONG_MAX
#endif

#ifndef AB_SOURCE_LOSS_NO_BLOCK_MS
#define AB_SOURCE_LOSS_NO_BLOCK_MS 900L
#endif

#ifndef AB_SOURCE_LOSS_SILENCE_MS
#define AB_SOURCE_LOSS_SILENCE_MS 1800L
#endif

#ifndef AB_SOURCE_LOSS_MIN_ACTIVITY_RMS
#define AB_SOURCE_LOSS_MIN_ACTIVITY_RMS 0.0035
#endif

#ifndef AB_BREAKBEAT_HIT_QUEUE_SIZE
#define AB_BREAKBEAT_HIT_QUEUE_SIZE 32
#endif

typedef struct
{
    float density_events_per_beat;
    float relock_rel_err;
    float regular_rel_err;

    float body_gap_calm_beats;
    float body_gap_hot_beats;
    float body_tail_guard_beats;
    float body_tail_strong_extra_beats;
    float body_tail_unknown_cooldown_mul;
    float body_cold_probe_cooldown_mul;
    float body_cold_cluster_cooldown_mul;
    float body_cold_gap_expand_seed_mul;
    float body_cold_seed_min_cooldown_mul;
    float body_cold_first_seed_cooldown_mul;
    float body_cold_gap_match_rel;
    float body_cold_score_ratio;
    float body_cold_level_ratio;
    float body_cold_freshness;
    float body_tail_reaccent_score_ratio;
    float body_tail_reaccent_level_ratio;
    float body_tail_reaccent_freshness;
    float body_early_guard_mul;
    float body_early_score_ratio;
    float body_early_level_ratio;
    float body_early_freshness;
    float body_subdivision_guard_beats;
    float body_subdivision_accent_min_beats;
    float body_subdivision_accent_lock_beats;
    float body_subdivision_score_ratio;
    float body_subdivision_level_ratio;
    float body_subdivision_freshness;
    int body_slow_shift_accepts;
    float body_decay_score_ratio;
    float body_decay_level_ratio;
    float body_decay_freshness;
    float body_period_min_track_mul;
    float body_period_max_track_mul;
    float body_period_slow_shift_mul;
    float body_period_double_min_mul;
    float body_period_double_max_mul;
    float body_period_full_shift_alpha;
    int body_warmup_accepts;
    float body_warmup_alpha;
    float hat_gap_calm_beats;
    float hat_gap_hot_beats;
    float early_escape_beats;
    float pace_hold_beats;
    float pace_fade_beats;

    float scratch_gap_min_beats;
    float scratch_gap_max_beats;
    long scratch_open_min_ms;
    long scratch_open_max_ms;
    long scratch_repeat_min_ms;
    long scratch_repeat_max_ms;
    float scratch_percussive_dominance;
    float scratch_strong_dominance;
    float scratch_strong_velocity;
    float scratch_strong_burst;
    float scratch_body_tail_guard_beats;
    float scratch_body_tail_dominance;
    float scratch_body_tail_velocity;
    float scratch_body_tail_burst;
    float scratch_cold_dominance;
    float scratch_cold_velocity;
    float scratch_cold_burst;
    float scratch_body_cycle_guard_beats;
    float scratch_body_cycle_dominance;
    float scratch_body_cycle_velocity;
    float scratch_body_cycle_burst;

    float open_user_mix;
    float body_open_beats;
    float snare_open_beats;
    float hat_open_beats;
    float open_excite_gain;
    float steady_open_duck;
    float hat_transport_min_intensity;

    float fps_attack_beats;
    float fps_release_beats;
    float fps_write_beats;
    float fps_external_snap_beats;
    float effect_fps_max_mul;

    float tonal_guard_bias;
    float tonal_guard_percussive;
    float tonal_guard_body;

    float stale_event_beats;
    long stale_min_ms;
    long stale_max_ms;

    float repeat_body_beats;
    float repeat_climax_extra_beats;
    long repeat_max_ms;

    float recent_floor_hold_beats;

    float intensity_percussive_w;
    float intensity_expression_w;
    float intensity_accel_w;
    float intensity_climax_w;
    float intensity_accent_w;
} ab_breakbeat_policy_t;

enum {
    AB_SCRATCH_REJECT_NONE = 0,
    AB_SCRATCH_REJECT_RAW,
    AB_SCRATCH_REJECT_DOMINANCE,
    AB_SCRATCH_REJECT_COLD,
    AB_SCRATCH_REJECT_TAIL,
    AB_SCRATCH_REJECT_CYCLE
};

typedef struct
{
    int raw;
    int dominant;
    int candidate;
    int reject;
} ab_scratch_decision_t;

static inline double ab_scratch_sensitivity_norm(void)
{
    int v = atomic_load_int(&ab_scratch_sensitivity);

    if(v < 0)
        v = 0;
    else if(v > 100)
        v = 100;

    return ((double)v - 50.0) / 50.0;
}

static inline double ab_scratch_gate_mul(void)
{
    double n = ab_scratch_sensitivity_norm();
    return 1.0 - (n * 0.22);
}

static inline double ab_scratch_dom_mul(void)
{
    double n = ab_scratch_sensitivity_norm();
    return 1.0 - (n * 0.18);
}

static inline double ab_scratch_escape_mul(void)
{
    double n = ab_scratch_sensitivity_norm();
    return 1.0 - (n * 0.15);
}

static inline int ab_breakbeat_fast_body_scratch_escape(const vj_audio_beat_thread_t *t,
                                                        double scratch_amount,
                                                        double scratch_velocity,
                                                        double scratch_burst,
                                                        double kick_score,
                                                        double snare_score,
                                                        double hat_score,
                                                        int scratch_dominant)
{
    double body_bpm;
    double percussive_max;
    double gate_mul;
    double dom_mul;

    if(!t || t->body_period_ms <= 1.0)
        return 1;

    body_bpm = 60000.0 / ab_bound_period_ms(t->body_period_ms);
    if(body_bpm < 132.0)
        return 1;

    percussive_max = kick_score;
    if(snare_score > percussive_max)
        percussive_max = snare_score;
    if(hat_score > percussive_max)
        percussive_max = hat_score;

    gate_mul = ab_scratch_escape_mul();
    dom_mul = ab_scratch_dom_mul();

    return scratch_amount >= 0.66 * gate_mul &&
           scratch_velocity >= 0.62 * gate_mul &&
           scratch_burst >= 0.62 * gate_mul &&
           (scratch_dominant ||
            scratch_amount >= percussive_max * 1.20 * dom_mul ||
            scratch_amount >= 0.76 * gate_mul);
}

typedef struct
{
    int keep;
    int accent;
    int accept_path;
    const char *reason;
} ab_body_decision_t;

static inline int ab_breakbeat_body_reason_is_accept_path(const char *reason)
{
    return reason &&
           (strcmp(reason, "body-accent") == 0 ||
            strcmp(reason, "body-cold-seed") == 0 ||
            strcmp(reason, "body-cold-seed-wide") == 0 ||
            strcmp(reason, "body-cold-seed-probe") == 0 ||
            strcmp(reason, "body-cold-seed-first") == 0);
}

static inline ab_body_decision_t ab_breakbeat_body_decision_make(int keep,
                                                                 const char *reason)
{
    ab_body_decision_t d;

    d.keep = keep ? 1 : 0;
    d.reason = reason;
    d.accent = reason && strcmp(reason, "body-accent") == 0;
    d.accept_path = ab_breakbeat_body_reason_is_accept_path(reason);

    return d;
}

static const ab_breakbeat_policy_t ab_breakbeat_policy = {
    .density_events_per_beat = 4.0f,
    .relock_rel_err = 0.42f,
    .regular_rel_err = 0.33f,

    .body_gap_calm_beats = 0.56f,
    .body_gap_hot_beats = 0.40f,
    .body_tail_guard_beats = 0.34f,
    .body_tail_strong_extra_beats = 0.06f,
    .body_tail_unknown_cooldown_mul = 1.60f,
    .body_cold_probe_cooldown_mul = 1.60f,
    .body_cold_cluster_cooldown_mul = 1.35f,
    .body_cold_gap_expand_seed_mul = 1.70f,
    .body_cold_seed_min_cooldown_mul = 2.80f,
    .body_cold_first_seed_cooldown_mul = 4.00f,
    .body_cold_gap_match_rel = 0.32f,
    .body_cold_score_ratio = 1.08f,
    .body_cold_level_ratio = 0.96f,
    .body_cold_freshness = 0.58f,
    .body_tail_reaccent_score_ratio = 1.42f,
    .body_tail_reaccent_level_ratio = 1.22f,
    .body_tail_reaccent_freshness = 0.72f,
    .body_early_guard_mul = 0.72f,
    .body_early_score_ratio = 1.30f,
    .body_early_level_ratio = 1.14f,
    .body_early_freshness = 0.68f,
    .body_subdivision_guard_beats = 0.80f,
    .body_subdivision_accent_min_beats = 0.46f,
    .body_subdivision_accent_lock_beats = 0.24f,
    .body_subdivision_score_ratio = 1.30f,
    .body_subdivision_level_ratio = 1.12f,
    .body_subdivision_freshness = 0.52f,
    .body_slow_shift_accepts = 3,
    .body_decay_score_ratio = 0.74f,
    .body_decay_level_ratio = 0.82f,
    .body_decay_freshness = 0.42f,
    .body_period_min_track_mul = 0.72f,
    .body_period_max_track_mul = 1.80f,
    .body_period_slow_shift_mul = 1.80f,
    .body_period_double_min_mul = 1.72f,
    .body_period_double_max_mul = 2.28f,
    .body_period_full_shift_alpha = 0.32f,
    .body_warmup_accepts = 8,
    .body_warmup_alpha = 0.46f,
    .hat_gap_calm_beats = 0.72f,
    .hat_gap_hot_beats = 0.54f,
    .early_escape_beats = 0.50f,
    .pace_hold_beats = 2.0f,
    .pace_fade_beats = 3.0f,

    .scratch_gap_min_beats = 0.12f,
    .scratch_gap_max_beats = 0.30f,
    .scratch_open_min_ms = 26L,
    .scratch_open_max_ms = 132L,
    .scratch_repeat_min_ms = 85L,
    .scratch_repeat_max_ms = 360L,
    .scratch_percussive_dominance = 1.20f,
    .scratch_strong_dominance = 1.10f,
    .scratch_strong_velocity = 0.82f,
    .scratch_strong_burst = 0.86f,
    .scratch_body_tail_guard_beats = 0.30f,
    .scratch_body_tail_dominance = 1.55f,
    .scratch_body_tail_velocity = 0.90f,
    .scratch_body_tail_burst = 0.92f,
    .scratch_cold_dominance = 1.60f,
    .scratch_cold_velocity = 0.82f,
    .scratch_cold_burst = 0.90f,
    .scratch_body_cycle_guard_beats = 1.50f,
    .scratch_body_cycle_dominance = 1.70f,
    .scratch_body_cycle_velocity = 0.84f,
    .scratch_body_cycle_burst = 0.88f,

    .open_user_mix = 0.34f,
    .body_open_beats = 0.175f,
    .snare_open_beats = 0.145f,
    .hat_open_beats = 0.052f,
    .open_excite_gain = 0.78f,
    .steady_open_duck = 0.22f,
    .hat_transport_min_intensity = 0.34f,

    .fps_attack_beats = 0.28f,
    .fps_release_beats = 0.55f,
    .fps_write_beats = 0.055f,
    .fps_external_snap_beats = 0.35f,
    .effect_fps_max_mul = 3.0f,

    .tonal_guard_bias = 0.32f,
    .tonal_guard_percussive = 0.50f,
    .tonal_guard_body = 0.46f,

    .stale_event_beats = 0.84f,
    .stale_min_ms = 120L,
    .stale_max_ms = 1200L,

    .repeat_body_beats = 1.0f,
    .repeat_climax_extra_beats = 0.32f,
    .repeat_max_ms = 3600L,

    .recent_floor_hold_beats = 1.25f,

    .intensity_percussive_w = 0.34f,
    .intensity_expression_w = 0.24f,
    .intensity_accel_w = 0.18f,
    .intensity_climax_w = 0.16f,
    .intensity_accent_w = 0.08f
};

static ab_scratch_decision_t ab_breakbeat_scratch_decide(vj_audio_beat_thread_t *t,
                                                         long now_ms,
                                                         int breakbeat_mode,
                                                         int settled,
                                                         int audible,
                                                         int kick_onset,
                                                         int snare_onset,
                                                         double kick_score,
                                                         double snare_score,
                                                         double hat_score,
                                                         double scratch_amount,
                                                         double scratch_velocity,
                                                         double scratch_burst,
                                                         double mid_norm,
                                                         double high_norm,
                                                         double flux_norm,
                                                         double flux_z,
                                                         double flux_ratio_abs)
{
    ab_scratch_decision_t d;
    double percussive_max;
    double body_period;
    double gate_mul;
    double dom_mul;
    double escape_mul;
    long body_since;
    int body_escape;

    d.raw = 0;
    d.dominant = 0;
    d.candidate = 0;
    d.reject = AB_SCRATCH_REJECT_NONE;

    percussive_max = kick_score;
    if(snare_score > percussive_max)
        percussive_max = snare_score;
    if(hat_score > percussive_max)
        percussive_max = hat_score;

    gate_mul = ab_scratch_gate_mul();
    dom_mul = ab_scratch_dom_mul();
    escape_mul = ab_scratch_escape_mul();

    d.dominant =
        scratch_amount >= percussive_max * (double)ab_breakbeat_policy.scratch_percussive_dominance * dom_mul ||
        (scratch_velocity >= (double)ab_breakbeat_policy.scratch_strong_velocity * gate_mul &&
         t->scratch_burst_env >= (double)ab_breakbeat_policy.scratch_strong_burst * gate_mul &&
         scratch_amount >= percussive_max * (double)ab_breakbeat_policy.scratch_strong_dominance * dom_mul);

    d.raw =
        breakbeat_mode &&
        settled &&
        audible &&
        scratch_amount >= 0.46 * gate_mul &&
        scratch_velocity >= 0.38 * gate_mul &&
        t->scratch_burst_env >= 0.24 * gate_mul &&
        (mid_norm + high_norm + flux_norm) >= 0.66 * gate_mul &&
        (scratch_amount >= 0.52 * gate_mul ||
         t->scratch_burst_env >= 0.40 * gate_mul ||
         scratch_burst >= 0.42 * gate_mul) &&
        (flux_z >= 0.34 * gate_mul ||
         flux_ratio_abs >= 1.18 + ((gate_mul - 1.0) * 0.35) ||
         t->scratch_burst_env >= 0.40 * gate_mul) &&
        !(kick_onset && kick_score > scratch_amount * 0.96) &&
        !(snare_onset && snare_score > scratch_amount * 0.98);

    d.candidate = d.raw && d.dominant;

    if(!d.raw)
        d.reject = AB_SCRATCH_REJECT_RAW;
    else if(!d.dominant)
        d.reject = AB_SCRATCH_REJECT_DOMINANCE;

    if(d.candidate && breakbeat_mode)
    {
        if(t->body_period_ms <= 1.0)
        {
            body_escape =
                scratch_amount >= percussive_max *
                    (double)ab_breakbeat_policy.scratch_cold_dominance * dom_mul &&
                scratch_velocity >= (double)ab_breakbeat_policy.scratch_cold_velocity * escape_mul &&
                t->scratch_burst_env >= (double)ab_breakbeat_policy.scratch_cold_burst * escape_mul;

            if(!body_escape)
            {
                d.candidate = 0;
                d.reject = AB_SCRATCH_REJECT_COLD;
            }
        }
        else if(t->body_last_hit_ms > 0 && now_ms > t->body_last_hit_ms)
        {
            body_period = t->body_period_ms;
            body_since = now_ms - t->body_last_hit_ms;

            if((double)body_since <=
               body_period * (double)ab_breakbeat_policy.scratch_body_tail_guard_beats)
            {
                body_escape =
                    scratch_amount >= percussive_max *
                        (double)ab_breakbeat_policy.scratch_body_tail_dominance * dom_mul &&
                    scratch_velocity >= (double)ab_breakbeat_policy.scratch_body_tail_velocity * escape_mul &&
                    t->scratch_burst_env >= (double)ab_breakbeat_policy.scratch_body_tail_burst * escape_mul;

                if(!body_escape)
                {
                    d.candidate = 0;
                    d.reject = AB_SCRATCH_REJECT_TAIL;
                }
            }
            else if((double)body_since <=
                    body_period * (double)ab_breakbeat_policy.scratch_body_cycle_guard_beats)
            {
                body_escape =
                    scratch_amount >= percussive_max *
                        (double)ab_breakbeat_policy.scratch_body_cycle_dominance * dom_mul &&
                    scratch_velocity >= (double)ab_breakbeat_policy.scratch_body_cycle_velocity * escape_mul &&
                    t->scratch_burst_env >= (double)ab_breakbeat_policy.scratch_body_cycle_burst * escape_mul;

                if(!body_escape)
                {
                    d.candidate = 0;
                    d.reject = AB_SCRATCH_REJECT_CYCLE;
                }
            }
        }
    }

    if(d.candidate && breakbeat_mode &&
       !ab_breakbeat_fast_body_scratch_escape(t,
                                              scratch_amount,
                                              scratch_velocity,
                                              scratch_burst,
                                              kick_score,
                                              snare_score,
                                              hat_score,
                                              d.dominant))
    {
        d.candidate = 0;
        d.reject = AB_SCRATCH_REJECT_CYCLE;
    }

    if(d.candidate)
        d.reject = AB_SCRATCH_REJECT_NONE;

    return d;
}
typedef struct
{
    int valid;
    int seq;
    long hit_ms;
    long publish_ms;
    long block_ms;
    float scratch_amount;
    float scratch_velocity;
    float scratch_burst;
    int scratch_dir;
    vj_audio_beat_snapshot_t snap;
} ab_breakbeat_hit_event_t;

static ab_breakbeat_hit_event_t ab_breakbeat_hit_queue[AB_BREAKBEAT_HIT_QUEUE_SIZE];
static volatile int ab_breakbeat_hit_queue_read = 0;
static volatile int ab_breakbeat_hit_queue_write = 0;
static volatile int ab_breakbeat_hit_queue_lock = 0;

int vj_audio_beat_get_snapshot(vj_audio_beat_shared_t *s, vj_audio_beat_snapshot_t *dst);
static void ab_breakbeat_hit_queue_clear(void);
static void ab_breakbeat_hit_queue_push(vj_audio_beat_shared_t *s,
                                        const vj_audio_beat_thread_t *t,
                                        int seq,
                                        long hit_ms,
                                        long publish_ms,
                                        long block_ms);
static int ab_breakbeat_hit_queue_pop_after(int consumed_seq, ab_breakbeat_hit_event_t *dst);

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
typedef struct
{
    int lo;
    int hi;
    int span;
    int direction;
    int capacity;
    int min_delta;
    float depth;
    float drive;
    float delta;
    const char *reason;
} ab_auto_calc_debug_t;

static ab_auto_calc_debug_t ab_auto_calc_dbg;

static void ab_auto_calc_debug_reset(void)
{
    ab_auto_calc_dbg.lo = 0;
    ab_auto_calc_dbg.hi = 0;
    ab_auto_calc_dbg.span = 0;
    ab_auto_calc_dbg.direction = 0;
    ab_auto_calc_dbg.capacity = 0;
    ab_auto_calc_dbg.min_delta = 0;
    ab_auto_calc_dbg.depth = 0.0f;
    ab_auto_calc_dbg.drive = 0.0f;
    ab_auto_calc_dbg.delta = 0.0f;
    ab_auto_calc_dbg.reason = "init";
}
#endif

static inline int ab_load_i(const volatile int *p)
{
    return atomic_load_int(p);
}

static inline long ab_load_l(const volatile long *p)
{
    return atomic_load_long(p);
}

static inline void ab_store_i(volatile int *p, int v)
{
    atomic_store_int(p, v);
}

static inline void ab_store_l(volatile long *p, long v)
{
    atomic_store_long(p, v);
}

static inline void ab_publish_i_cached(volatile int *p, int *cache, int v)
{
    if(!cache || *cache != v)
    {
        if(cache)
            *cache = v;
        ab_store_i(p, v);
    }
}

static inline void ab_publish_l_cached(volatile long *p, long *cache, long v)
{
    if(!cache || *cache != v)
    {
        if(cache)
            *cache = v;
        ab_store_l(p, v);
    }
}

static inline int ab_publish_transient_q8(double transient_norm)
{
    if(transient_norm < 0.0)
        transient_norm = 0.0;
    else if(transient_norm > 1.0)
        transient_norm = 1.0;

    return (int)(transient_norm * 256.0 + 0.5);
}

static inline int ab_add_i(volatile int *p, int v)
{
    return atomic_add_fetch_old_int(p, v) + v;
}

static inline long ab_add_l(volatile long *p, long v)
{
    return atomic_add_fetch_long(p, v);
}

static long ab_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long)ts.tv_sec * 1000L) + ((long)ts.tv_nsec / 1000000L);
}

static void ab_source_activity_reset(void)
{
    atomic_store_int(&ab_source_seen, 0);
    atomic_store_long(&ab_source_first_seen_ms, 0);
    atomic_store_long(&ab_source_last_block_ms, 0);
    atomic_store_long(&ab_source_last_active_ms, 0);
    atomic_store_int(&ab_source_level_q15, 0);
    atomic_store_int(&ab_source_loss_paused, 0);
}

static void ab_source_activity_note(long now_ms,
                                    double block_level,
                                    double min_hit_level,
                                    double envelope,
                                    double flux_norm)
{
    double gate = min_hit_level * 0.42;
    int active;

    if(now_ms <= 0)
        now_ms = ab_now_ms();

    if(gate < AB_SOURCE_LOSS_MIN_ACTIVITY_RMS)
        gate = AB_SOURCE_LOSS_MIN_ACTIVITY_RMS;

    active = block_level >= gate ||
             envelope >= gate * 1.35 ||
             (block_level >= gate * 0.55 && flux_norm >= 0.025);

    if(!atomic_load_int(&ab_source_seen))
    {
        atomic_store_int(&ab_source_seen, 1);
        atomic_store_long(&ab_source_first_seen_ms, now_ms);
    }

    {
        double lv = block_level;
        int q;

        if(lv < 0.0)
            lv = 0.0;
        else if(lv > 1.0)
            lv = 1.0;

        q = (int)(lv * 32767.0 + 0.5);
        atomic_store_long(&ab_source_last_block_ms, now_ms);
        atomic_store_int(&ab_source_level_q15, q);
    }

    if(active)
    {
        atomic_store_long(&ab_source_last_active_ms, now_ms);
        atomic_store_int(&ab_source_loss_paused, 0);
    }
}

static int ab_source_loss_is_active(vj_audio_beat_shared_t *s, long now_ms)
{
    long first_ms;
    long last_block_ms;
    long last_active_ms;
    long no_block_age;
    long silence_age;
    int level_q15;
    int silence_q15 = (int)(AB_SOURCE_LOSS_MIN_ACTIVITY_RMS * 32767.0 + 0.5);

    if(!s)
        return 0;

    if(!atomic_load_int(&ab_source_loss_pause))
        return 0;

    if(!atomic_load_int(&ab_source_seen))
        return 0;

    if(!ab_load_i(&s->open))
        return 1;

    if(now_ms <= 0)
        now_ms = ab_now_ms();

    last_block_ms = atomic_load_long(&ab_source_last_block_ms);
    if(last_block_ms <= 0)
        return 0;

    no_block_age = now_ms - last_block_ms;
    if(no_block_age >= AB_SOURCE_LOSS_NO_BLOCK_MS)
        return 1;

    level_q15 = atomic_load_int(&ab_source_level_q15);

    last_active_ms = atomic_load_long(&ab_source_last_active_ms);
    if(last_active_ms > 0)
    {
        silence_age = now_ms - last_active_ms;
        return silence_age >= AB_SOURCE_LOSS_SILENCE_MS && level_q15 <= silence_q15;
    }

    first_ms = atomic_load_long(&ab_source_first_seen_ms);
    if(first_ms <= 0)
        return 0;

    silence_age = now_ms - first_ms;
    return silence_age >= AB_SOURCE_LOSS_SILENCE_MS && level_q15 <= silence_q15;
}

#define AB_NSEC_PER_SEC  1000000000L
#define AB_USEC_PER_SEC  1000000L
#define AB_NSEC_PER_USEC 1000L

static inline void ab_timespec_add_us(struct timespec *ts, long usec)
{
    long sec = usec / AB_USEC_PER_SEC;
    long rem = usec - sec * AB_USEC_PER_SEC;

    ts->tv_sec  += sec;
    ts->tv_nsec += rem * AB_NSEC_PER_USEC;

    if(ts->tv_nsec >= AB_NSEC_PER_SEC)
    {
        ts->tv_nsec -= AB_NSEC_PER_SEC;
        ts->tv_sec++;
    }
}

static void ab_sleep_us(long usec)
{
    if(usec <= 0)
        return;

#if defined(CLOCK_MONOTONIC) && defined(TIMER_ABSTIME)
    struct timespec deadline;

    if(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0)
    {
        int saved_errno = errno;

        ab_timespec_add_us(&deadline, usec);

        while(clock_nanosleep(CLOCK_MONOTONIC,
                              TIMER_ABSTIME,
                              &deadline,
                              NULL) == EINTR)
        {
        }

        errno = saved_errno;
        return;
    }
#endif

    long sec = usec / AB_USEC_PER_SEC;
    long rem = usec - sec * AB_USEC_PER_SEC;

    struct timespec ts;
    ts.tv_sec  = sec;
    ts.tv_nsec = rem * AB_NSEC_PER_USEC;

    int saved_errno = errno;

    while(nanosleep(&ts, &ts) == -1)
    {
        if(errno != EINTR)
            break;
    }

    errno = saved_errno;
}

static inline int ab_floor_to_multiple_i(int v, int step)
{
    if(v <= 0 || step <= 0)
        return 0;

    return (v / step) * step;
}

static inline long ab_floor_to_multiple_l(long v, long step)
{
    if(v <= 0 || step <= 0)
        return 0;

    return (v / step) * step;
}

static inline long ab_target_analysis_bytes(const vj_audio_beat_thread_t *t)
{
    long frames;

    if(!t || t->bytes_per_frame <= 0)
        return 0;

    frames = ((long)t->sample_rate * VEEJAY_AUDIO_BEAT_TARGET_ANALYSIS_MS) / 1000L;

    if(frames < VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES)
        frames = VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES;

    if(frames > VEEJAY_AUDIO_BEAT_MAX_ANALYSIS_FRAMES)
        frames = VEEJAY_AUDIO_BEAT_MAX_ANALYSIS_FRAMES;

    return frames * (long)t->bytes_per_frame;
}

static inline double ab_absd(double v)
{
    return v < 0.0 ? -v : v;
}

static inline double ab_clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}


static inline double ab_soft_ratio_unit(double env, double slow, double knee, double gain)
{
    double ratio;
    double x;

    if(slow < 0.000001)
        slow = 0.000001;

    ratio = env / slow;
    x = (ratio - knee) * gain;

    if(x <= 0.0)
        return 0.0;

    x = x / (1.0 + x);
    x = ab_clampd(x, 0.0, 1.0);

    return x * x * (3.0 - 2.0 * x);
}

static inline void ab_ewstat_update(double *mean, double *var, double x, double alpha)
{
    double d;

    if(!mean || !var)
        return;

    if(alpha < 0.0001)
        alpha = 0.0001;
    else if(alpha > 1.0)
        alpha = 1.0;

    d = x - *mean;
    *mean += alpha * d;
    *var = (1.0 - alpha) * (*var + alpha * d * d);

    if(*var < 1.0e-9)
        *var = 1.0e-9;
}

static inline double ab_zscore(double x, double mean, double var)
{
    return (x - mean) / sqrt(var + 1.0e-9);
}

static inline double ab_threshold_to_sigma(int threshold)
{
    double t;

    if(threshold < 30)
        threshold = 30;
    else if(threshold > 400)
        threshold = 400;

    t = (double)(threshold - 30) / 370.0;

    return 1.20 + (t * 2.30);
}

static inline double ab_filter_alpha(double cutoff_hz, double sample_rate)
{
    double a;

    sample_rate = sample_rate <= 1.0 ? 44100.0 : sample_rate;
    cutoff_hz = cutoff_hz < 1.0 ? 1.0 : (cutoff_hz > sample_rate * 0.45 ? sample_rate * 0.45 : cutoff_hz);

    a = 1.0 - exp((-2.0 * M_PI * cutoff_hz) / sample_rate);
    return a < 0.00001 ? 0.00001 : (a > 1.0 ? 1.0 : a);
}

static inline void ab_prepare_analysis_filters(vj_audio_beat_thread_t *t)
{
    if(!t)
        return;

    if(t->filter_sample_rate == t->sample_rate &&
       t->filter_low_alpha > 0.0f &&
       t->filter_mid_alpha > 0.0f)
        return;

    t->filter_sample_rate = t->sample_rate;
    t->filter_low_alpha = (float)ab_filter_alpha(160.0, (double)t->sample_rate);
    t->filter_mid_alpha = (float)ab_filter_alpha(1800.0, (double)t->sample_rate);
}

static inline void ab_thread_publish_cache_reset(vj_audio_beat_thread_t *t)
{
    if(!t)
        return;

    t->pub_level_q15 = INT_MIN;
    t->pub_envelope_q15 = INT_MIN;
    t->pub_flux_q15 = INT_MIN;
    t->pub_band_low_q15 = INT_MIN;
    t->pub_band_mid_q15 = INT_MIN;
    t->pub_band_high_q15 = INT_MIN;
    t->pub_band_balance_q15 = INT_MIN;
    t->pub_kick_q15 = INT_MIN;
    t->pub_snare_q15 = INT_MIN;
    t->pub_hat_q15 = INT_MIN;
    t->pub_transient_norm_q15 = INT_MIN;
    t->pub_transient_q8 = INT_MIN;
    t->pub_bpm_q8 = INT_MIN;
    t->beat_toggle_state = 0;
    t->pub_overruns = LONG_MIN;
}

static inline long ab_clamp_l(long v, long lo, long hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int ab_q15(double v)
{
    v = ab_clampd(v, 0.0, 1.0);
    return (int)(v * 32767.0 + 0.5);
}

static inline float ab_from_q15(int v)
{
    if(v <= 0)
        return 0.0f;

    if(v >= 32767)
        return 1.0f;

    return (float)v * (1.0f / 32767.0f);
}

static inline void ab_scratch_visual_reset(void)
{
    ab_store_i(&ab_scratch_visual_env_q15, 0);
    ab_store_i(&ab_scratch_visual_dir, 0);
    ab_store_l(&ab_scratch_visual_last_ms, 0);
    ab_store_l(&ab_scratch_visual_hold_until_ms, 0);
    ab_store_l(&ab_scratch_visual_decay_until_ms, 0);
}

static float ab_scratch_visual_env_now(long now)
{
    int q = ab_load_i(&ab_scratch_visual_env_q15);
    long last = ab_load_l(&ab_scratch_visual_last_ms);
    long hold_until = ab_load_l(&ab_scratch_visual_hold_until_ms);
    long decay_until = ab_load_l(&ab_scratch_visual_decay_until_ms);
    float env;
    float t;

    if(q <= 0 || last <= 0 || now <= 0)
        return 0.0f;

    env = ab_from_q15(q);

    if(now <= hold_until)
        return env;

    if(decay_until <= hold_until || now >= decay_until)
        return 0.0f;

    t = (float)(now - hold_until) / (float)(decay_until - hold_until);
    if(t < 0.0f)
        t = 0.0f;
    else if(t > 1.0f)
        t = 1.0f;

    t = 1.0f - t;
    return env * t * t;
}

static int ab_scratch_visual_dir_now(long now)
{
    if(ab_scratch_visual_env_now(now) <= 0.001f)
        return 0;

    return ab_load_i(&ab_scratch_visual_dir) < 0 ? -1 : 1;
}

static float ab_scratch_visual_direction_signal(long now)
{
    int dir = ab_scratch_visual_dir_now(now);

    if(dir < 0)
        return 0.0f;
    if(dir > 0)
        return 1.0f;

    return 0.5f;
}

static float ab_scratch_visual_signed_signal(long now)
{
    float env = ab_scratch_visual_env_now(now);
    int dir = ab_scratch_visual_dir_now(now);

    if(dir < 0)
        return 0.5f - env * 0.5f;
    if(dir > 0)
        return 0.5f + env * 0.5f;

    return 0.5f;
}

static void ab_scratch_visual_pulse(long now, int dir, float amount, float velocity, float burst, float late_drive)
{
    float old_env;
    float strength;
    float env;
    long hold_ms;
    long decay_ms;

    late_drive = (float)ab_clampd((double)late_drive, 0.0, 1.0);

    if(now <= 0)
        now = ab_now_ms();

    if(dir == 0)
        dir = 1;

    amount = (float)ab_clampd((double)amount, 0.0, 1.0);
    velocity = (float)ab_clampd((double)velocity, 0.0, 1.0);
    burst = (float)ab_clampd((double)burst, 0.0, 1.0);

    old_env = ab_scratch_visual_env_now(now);
    strength = amount * 0.24f + velocity * 0.38f + burst * 0.38f;
    strength = (float)ab_clampd((double)strength, 0.0, 1.0);
    if(late_drive > 0.0f)
    {
        float late_push = 0.18f + late_drive * 0.22f;
        strength += (1.0f - strength) * late_push;
        if(strength > 1.0f)
            strength = 1.0f;
    }

    env = old_env * 0.48f + strength * 0.88f;
    if(env < strength)
        env = strength;
    if(env > 1.0f)
        env = 1.0f;

    hold_ms = 96L + (long)(strength * 112.0f + 0.5f);
    if(velocity > 0.70f && burst > 0.70f)
        hold_ms += 36L;
    if(late_drive > 0.0f)
    {
        long reduce = (long)((double)hold_ms * (0.18 + 0.22 * (double)late_drive) + 0.5);
        hold_ms -= reduce;
    }

    if(hold_ms < 72L)
        hold_ms = 72L;
    else if(hold_ms > 260L)
        hold_ms = 260L;

    decay_ms = hold_ms + 96L + (long)(strength * 128.0f + 0.5f);
    if(late_drive > 0.0f)
    {
        long tail_reduce = (long)(64.0 * (double)late_drive + 0.5);
        decay_ms -= tail_reduce;
    }
    if(decay_ms < hold_ms + 80L)
        decay_ms = hold_ms + 80L;
    else if(decay_ms > 460L)
        decay_ms = 460L;

    ab_store_i(&ab_scratch_visual_env_q15, ab_q15(env));
    ab_store_i(&ab_scratch_visual_dir, dir < 0 ? -1 : 1);
    ab_store_l(&ab_scratch_visual_last_ms, now);
    ab_store_l(&ab_scratch_visual_hold_until_ms, now + hold_ms);
    ab_store_l(&ab_scratch_visual_decay_until_ms, now + decay_ms);
}


static inline void ab_breakbeat_hit_queue_lock_enter(void)
{
#if defined(__GNUC__) || defined(__clang__)
    while(__atomic_exchange_n(&ab_breakbeat_hit_queue_lock, 1, __ATOMIC_ACQUIRE) != 0)
#else
    while(__sync_lock_test_and_set(&ab_breakbeat_hit_queue_lock, 1) != 0)
#endif
    {
        sched_yield();
    }
}

static inline void ab_breakbeat_hit_queue_lock_leave(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(&ab_breakbeat_hit_queue_lock, 0, __ATOMIC_RELEASE);
#else
    __sync_lock_release(&ab_breakbeat_hit_queue_lock);
#endif
}

static void ab_breakbeat_hit_queue_clear(void)
{
    ab_breakbeat_hit_queue_lock_enter();
    memset(ab_breakbeat_hit_queue, 0, sizeof(ab_breakbeat_hit_queue));
    ab_breakbeat_hit_queue_read = 0;
    ab_breakbeat_hit_queue_write = 0;
    ab_breakbeat_hit_queue_lock_leave();
}

static void ab_breakbeat_hit_queue_push(vj_audio_beat_shared_t *s,
                                        const vj_audio_beat_thread_t *t,
                                        int seq,
                                        long hit_ms,
                                        long publish_ms,
                                        long block_ms)
{
    ab_breakbeat_hit_event_t ev;
    int w;
    int n;

    if(!s || !t || seq <= 0 || !ab_action_is_breakbeat(ab_load_i(&s->action_mode)))
        return;

    memset(&ev, 0, sizeof(ev));
    ev.valid = 1;
    ev.seq = seq;
    ev.hit_ms = hit_ms > 0 ? hit_ms : publish_ms;
    ev.publish_ms = publish_ms > 0 ? publish_ms : ev.hit_ms;
    ev.block_ms = block_ms > 0 ? block_ms : 0;

    if(!vj_audio_beat_get_snapshot(s, &ev.snap))
        memset(&ev.snap, 0, sizeof(ev.snap));

    ev.snap.hit_seq = seq;
    ev.snap.last_hit_ms = ev.hit_ms;
    ev.snap.beat_age_ms = ev.publish_ms >= ev.hit_ms ? ev.publish_ms - ev.hit_ms : 0;
    ev.snap.hit_kind = t->last_hit_kind;
    ev.snap.level = ab_from_q15(t->pub_level_q15 == INT_MIN ? 0 : t->pub_level_q15);
    ev.snap.envelope = ab_from_q15(t->pub_envelope_q15 == INT_MIN ? 0 : t->pub_envelope_q15);
    ev.snap.flux = ab_from_q15(t->pub_flux_q15 == INT_MIN ? 0 : t->pub_flux_q15);
    ev.snap.kick = ab_from_q15(ab_load_i(&ab_kick_q15));
    ev.snap.snare = ab_from_q15(ab_load_i(&ab_snare_q15));
    ev.snap.hat = ab_from_q15(ab_load_i(&ab_hat_q15));

    ev.scratch_amount = ab_from_q15(ab_load_i(&ab_scratch_q15));
    ev.scratch_velocity = ab_from_q15(ab_load_i(&ab_scratch_velocity_q15));
    ev.scratch_burst = ab_from_q15(ab_load_i(&ab_scratch_burst_q15));
    ev.scratch_dir = ab_load_i(&ab_scratch_dir) < 0 ? -1 : 1;

    if(ev.snap.hit_kind == AB_HIT_SCRATCH)
    {
        ev.snap.transient = ev.scratch_burst;
        ev.snap.flux = ev.scratch_velocity;
        ev.snap.beat_density = ev.scratch_amount;
        ev.snap.beat_pulse = ev.scratch_burst;
        ev.snap.beat_gate = ev.scratch_amount > 0.18f ? 1.0f : 0.0f;
    }

    ab_breakbeat_hit_queue_lock_enter();

    w = ab_breakbeat_hit_queue_write;
    n = w + 1;
    if(n >= AB_BREAKBEAT_HIT_QUEUE_SIZE)
        n = 0;

    if(n == ab_breakbeat_hit_queue_read)
    {
        int r = ab_breakbeat_hit_queue_read + 1;
        if(r >= AB_BREAKBEAT_HIT_QUEUE_SIZE)
            r = 0;
        ab_breakbeat_hit_queue_read = r;
    }

    ab_breakbeat_hit_queue[w] = ev;
    ab_breakbeat_hit_queue_write = n;

    ab_breakbeat_hit_queue_lock_leave();
}

static int ab_breakbeat_hit_queue_pop_after(int consumed_seq, ab_breakbeat_hit_event_t *dst)
{
    int found = 0;

    if(!dst)
        return 0;

    ab_breakbeat_hit_queue_lock_enter();

    while(ab_breakbeat_hit_queue_read != ab_breakbeat_hit_queue_write)
    {
        int r = ab_breakbeat_hit_queue_read;
        ab_breakbeat_hit_event_t ev = ab_breakbeat_hit_queue[r];

        r++;
        if(r >= AB_BREAKBEAT_HIT_QUEUE_SIZE)
            r = 0;
        ab_breakbeat_hit_queue_read = r;

        if(ev.valid && ev.seq > consumed_seq)
        {
            *dst = ev;
            found = 1;
            break;
        }
    }

    ab_breakbeat_hit_queue_lock_leave();
    return found;
}


static inline long ab_effective_snapshot_window_ms(int configured_ms, float bpm, int hit_kind, int gate)
{
    double value;
    double period;
    double fraction;
    double target;
    double tempo_weight;
    long lo;
    long hi;

    if(gate)
    {
        if(configured_ms < 10)
            configured_ms = 10;
        else if(configured_ms > 1000)
            configured_ms = 1000;

        lo = 24L;
        hi = 360L;
    }
    else
    {
        if(configured_ms < 20)
            configured_ms = 20;
        else if(configured_ms > 2000)
            configured_ms = 2000;

        lo = 55L;
        hi = 900L;
    }

    value = (double)configured_ms;

    if(bpm > 1.0f)
    {
        period = 60000.0 / (double)bpm;
        period = ab_bound_period_ms(period);

        if(gate)
            fraction = 0.18;
        else
            fraction = 0.42;

        if(hit_kind == AB_HIT_HAT)
            fraction *= 0.72;
        else if(hit_kind == AB_HIT_SNARE)
            fraction *= 0.92;
        else if(hit_kind == AB_HIT_FULL)
            fraction *= 1.08;

        target = period * fraction;

        if(target < (double)lo)
            target = (double)lo;
        else if(target > (double)hi)
            target = (double)hi;

        tempo_weight = bpm < 85.0f ? 0.74 : (bpm > 165.0f ? 0.62 : 0.68);
        value = value * (1.0 - tempo_weight) + target * tempo_weight;
    }

    if(value < (double)lo)
        value = (double)lo;
    else if(value > (double)hi)
        value = (double)hi;

    return (long)(value + 0.5);
}

static inline double ab_sample_s16(const uint8_t *p)
{
    int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return (double)v * (1.0 / 32768.0);
}

static inline double ab_sample_u8(const uint8_t *p)
{
    return ((double)p[0] - 128.0) * (1.0 / 128.0);
}

static const char *ab_action_name(int action)
{
    switch(action)
    {
        case VJ_AUDIO_BEAT_ACTION_NONE:
            return "none";

        case VJ_AUDIO_BEAT_ACTION_AUTO_FX:
            return "auto-fx";

        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX:
            return "break-beat+auto-fx";

        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT:
            return "break-beat";

        default:
            return "unknown";
    }
}

static void ab_log_config(vj_audio_beat_shared_t *s, const char *reason)
{
    int action;

    if(!s)
        return;

    action = ab_load_i(&s->action_mode);

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] config%s%s hold=%dms cooldown=%dms threshold=%d scratch=%d source_loss_pause=%d monitor_latency=%d heard_latency=%dms input_channels=%d action=%s(%d) pulse=%dms gate=%dms auto_mode=%d auto_amount=%d enabled=%d open=%d running=%d",
               reason ? " " : "",
               reason ? reason : "",
               ab_load_i(&s->freeze_ms),
               ab_load_i(&s->cooldown_ms),
               ab_load_i(&s->threshold),
               atomic_load_int(&ab_scratch_sensitivity),
               atomic_load_int(&ab_source_loss_pause),
               atomic_load_int(&ab_monitor_latency_ms),
               atomic_load_int(&ab_heard_latency_ms),
               ab_load_i(&s->input_channels_request),
               ab_action_name(action),
               action,
               ab_load_i(&s->pulse_ms),
               ab_load_i(&s->gate_ms),
               ab_load_i(&ab_auto_mode),
               ab_load_i(&ab_auto_amount),
               ab_load_i(&s->enabled),
               ab_load_i(&s->open),
               ab_load_i(&s->running));
}


static inline int ab_sync_source(vj_audio_beat_shared_t *s)
{
    return (s && s->sync) ? ab_load_i(&s->sync->source) : -1;
}

static void ab_arm_sync_read_probe(vj_audio_beat_shared_t *s,
                                   vj_audio_beat_thread_t *t,
                                   int reset_seq)
{
    long now;
    int source;

    if(!s || !t || !s->sync)
        return;

    source = ab_sync_source(s);

    if(source != VJ_AUDIO_SYNC_SOURCE_PUSH)
        return;

    now = ab_now_ms();
    t->sync_read_arm_until_ms = now + VEEJAY_AUDIO_BEAT_SYNC_READ_ARM_MS;
    t->sync_read_probe_pending = 1;
    t->sync_read_probe_source = source;
    t->sync_read_probe_seq = reset_seq;

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] sync reader armed source=push reset_seq=%d guard=%ldms",
               reset_seq,
               (long)VEEJAY_AUDIO_BEAT_SYNC_READ_ARM_MS);
}

void vj_audio_beat_init(vj_audio_beat_shared_t *s, int input_channels)
{
    if(!s)
        return;

    memset(s, 0, sizeof(*s));

    if(input_channels < 1)
        input_channels = 2;
    else if(input_channels > 2)
        input_channels = 2;

    ab_store_i(&s->input_channels_request, input_channels);
    ab_store_i(&s->freeze_ms, 90);
    ab_store_i(&s->cooldown_ms, 180);
    ab_store_i(&s->threshold, 145);
    ab_store_i(&s->resume_speed, 1);
    atomic_store_int(&ab_scratch_sensitivity, 50);
    atomic_store_int(&ab_source_loss_pause, 1);
    ab_source_activity_reset();
    atomic_store_int(&ab_output_latency_ms, -1);
    atomic_store_int(&ab_heard_latency_ms, -1);
    atomic_store_int(&ab_monitor_latency_ms, VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS);

    ab_store_i(&s->action_mode, VJ_AUDIO_BEAT_ACTION_AUTO_FX);
    ab_store_i(&s->pulse_ms, 180);
    ab_store_i(&s->gate_ms, 90);

    ab_store_i(&ab_auto_mode, VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION);
    ab_store_i(&ab_auto_amount, 75);
    ab_store_i(&ab_auto_dirty, 1);
    ab_auto_signature = 0;
    ab_auto_target_count = 0;
    ab_auto_active = 0;
    ab_auto_climax_level = 0.0f;
    ab_auto_climax_last_ms = 0;
    ab_auto_climax_last_hit_seq = 0;
    ab_auto_groove_level = 0.0f;
    ab_auto_phrase_level = 0.0f;
    ab_auto_groove_last_ms = 0;
    ab_auto_groove_last_hit_seq = 0;
    ab_auto_last_apply_ms = 0;
    ab_auto_signature_last_check_ms = 0;
    ab_auto_signature_last_chain_len = -1;
    ab_auto_resume_guard_until_ms = 0;
    ab_auto_resume_guard_active = 0;
    ab_auto_debug_last_apply_ms = 0;
    memset(ab_auto_targets, 0, sizeof(ab_auto_targets));
    memset(&ab_breakbeat_state, 0, sizeof(ab_breakbeat_state));
    ab_breakbeat_hit_queue_clear();
    ab_scratch_visual_reset();
    ab_store_l(&ab_breakbeat_user_override_until_ms, 0);

    ab_store_i(&s->level_q15, 0);
    ab_store_i(&s->envelope_q15, 0);
    ab_store_i(&s->transient_q8, 0);
    ab_store_i(&s->transient_norm_q15, 0);
    ab_store_i(&s->flux_q15, 0);
    ab_store_i(&ab_band_low_q15, 0);
    ab_store_i(&ab_band_mid_q15, 0);
    ab_store_i(&ab_band_high_q15, 0);
    ab_store_i(&ab_band_balance_q15, 0);
    ab_store_i(&s->beat_toggle_q15, 0);
    ab_store_i(&s->bpm_q8, 0);

    ab_store_i(&ab_kick_q15, 0);
    ab_store_i(&ab_snare_q15, 0);
    ab_store_i(&ab_hat_q15, 0);
    ab_store_i(&ab_scratch_q15, 0);
    ab_store_i(&ab_scratch_velocity_q15, 0);
    ab_store_i(&ab_scratch_burst_q15, 0);
    ab_store_i(&ab_scratch_dir, 1);
    ab_scratch_visual_reset();
    ab_source_activity_reset();
    ab_store_i(&ab_last_hit_kind, AB_HIT_NONE);

    ab_store_i(&s->initialized, 1);
    ab_log_config(s, "initialized defaults");
}

void vj_audio_beat_bind_sync(vj_audio_beat_shared_t *s, vj_audio_sync_shared_t *sync)
{
    if(!s)
        return;

    s->sync = sync;
}

void vj_audio_beat_request_stop(vj_audio_beat_shared_t *s)
{
    if(!s)
        return;

    ab_store_i(&s->stop_request, 1);
    ab_store_i(&s->enabled, 0);
}

static void ab_thread_reset(vj_audio_beat_thread_t *t)
{
    t->fast_energy = 0.0;
    t->slow_energy = 0.0;
    t->fast_flux = 0.0;
    t->slow_flux = 0.0;
    t->envelope = 0.0;
    t->beat_period_ms = 0.0;
    t->last_sample = 0.0;
    t->last_log_energy = 0.0;
    t->rise_mean = 0.0;
    t->rise_var = 0.010;
    t->flux_mean = 0.0;
    t->flux_var = 0.010;
    t->onset_mean = 0.0;
    t->onset_var = 0.250;
    t->onset_smooth = 0.0;
    t->band_low_lp = 0.0;
    t->band_mid_lp = 0.0;
    t->band_low_env = 0.0;
    t->band_mid_env = 0.0;
    t->band_high_env = 0.0;
    t->band_low_slow = 0.000001;
    t->band_mid_slow = 0.000001;
    t->band_high_slow = 0.000001;
    t->last_hit_ms = 0;
    t->tempo_last_hit_ms = 0;
    t->blocks_seen = 0;
    t->last_interval_ms = 0;
    t->tempo_fast_count = 0;
    t->tempo_slow_count = 0;
    t->last_accept_score = 0.0;
    t->last_accept_level = 0.0;
    t->body_last_hit_ms = 0;
    t->body_last_interval_ms = 0;
    t->body_last_accent_ms = 0;
    t->body_period_ms = 0.0;
    t->body_last_score = 0.0;
    t->body_last_level = 0.0;
    t->body_last_kind = AB_HIT_NONE;
    t->body_slow_count = 0;
    t->body_seed_count = 0;
    t->body_cold_probe_ms = 0;
    t->body_cold_gap_ms = 0;
    t->body_cold_dbg_gap_ms = 0;
    t->body_cold_dbg_min_ms = 0;
    t->body_cold_dbg_first_ms = 0;
    t->body_cold_probe_score = 0.0;
    t->body_cold_probe_level = 0.0;
    t->body_cold_probe_kind = AB_HIT_NONE;
    t->body_cold_meter_last_ms = 0;
    t->body_cold_meter_dbg_interval_ms = 0;
    t->body_cold_meter_period_ms = 0.0;
    t->body_cold_meter_count = 0;
    t->last_kick_score = 0.0;
    t->last_snare_score = 0.0;
    t->last_hat_score = 0.0;
    t->last_scratch_score = 0.0;
    t->last_scratch_candidate_raw = 0;
    t->last_scratch_dominant = 0;
    t->last_scratch_candidate = 0;
    t->last_scratch_reject = AB_SCRATCH_REJECT_NONE;
    t->last_scratch_gesture_dir = 0;
    t->last_scratch_gesture_ms = 0;
    t->last_scratch_turn_dir = 0;
    t->last_scratch_turn_ms = 0;
    t->last_scratch_turn_score = 0.0;
    t->last_scratch_turn_edge = 0;
    t->scratch_env = 0.0;
    t->scratch_slow = 0.0;
    t->scratch_burst_env = 0.0;
    t->scratch_last_amount = 0.0;
    t->scratch_dir = 1;
    t->last_hit_kind = AB_HIT_NONE;
    t->filter_sample_rate = 0;
    t->filter_low_alpha = 0.0f;
    t->filter_mid_alpha = 0.0f;
    t->sync_read_arm_until_ms = 0;
    t->sync_read_last_slow_log_ms = 0;
    t->sync_read_probe_pending = 0;
    t->sync_read_probe_source = -1;
    t->sync_read_probe_seq = 0;
    ab_thread_publish_cache_reset(t);

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    t->last_debug_ms = 0;
    t->debug_blocks = 0;
    t->debug_candidates = 0;
    t->debug_accepted = 0;
    t->debug_cooldown_rejects = 0;
    t->debug_silence_rejects = 0;
    t->debug_flux_rejects = 0;
    t->debug_score_rejects = 0;

    t->dbg_energy = 0.0;
    t->dbg_flux = 0.0;
    t->dbg_block_level = 0.0;
    t->dbg_level = 0.0;
    t->dbg_envelope = 0.0;
    t->dbg_bass = 0.0;
    t->dbg_mid = 0.0;
    t->dbg_high = 0.0;
    t->dbg_energy_ratio = 0.0;
    t->dbg_flux_ratio = 0.0;
    t->dbg_score = 0.0;
    t->dbg_min_hit_level = 0.0;
    t->dbg_min_hit_flux = 0.0;
    t->dbg_threshold = 0;
    t->dbg_audible = 0;
    t->dbg_transient_present = 0;
    t->dbg_hit_candidate = 0;
#endif
}

static void ab_clear_published_control(vj_audio_beat_shared_t *s)
{
    ab_store_i(&s->level_q15, 0);
    ab_store_i(&s->envelope_q15, 0);
    ab_store_i(&s->transient_q8, 0);
    ab_store_i(&s->transient_norm_q15, 0);
    ab_store_i(&s->flux_q15, 0);
    ab_store_i(&ab_band_low_q15, 0);
    ab_store_i(&ab_band_mid_q15, 0);
    ab_store_i(&ab_band_high_q15, 0);
    ab_store_i(&ab_band_balance_q15, 0);
    ab_store_i(&ab_kick_q15, 0);
    ab_store_i(&ab_snare_q15, 0);
    ab_store_i(&ab_hat_q15, 0);
    ab_store_i(&ab_scratch_q15, 0);
    ab_store_i(&ab_scratch_velocity_q15, 0);
    ab_store_i(&ab_scratch_burst_q15, 0);
    ab_store_i(&ab_scratch_dir, 1);
    ab_scratch_visual_reset();
    ab_store_i(&ab_last_hit_kind, AB_HIT_NONE);
    ab_store_i(&s->beat_toggle_q15, 0);
    ab_store_i(&s->bpm_q8, 0);
    ab_store_l(&s->last_hit_ms, 0);
    ab_store_l(&s->hold_until_ms, 0);
}

static int ab_thread_prepare_buffer(vj_audio_beat_thread_t *t)
{
    int min_size;

    if(!t || t->bytes_per_frame <= 0)
        return 0;

    min_size = t->bytes_per_frame * 8192;

    if(t->sample_rate > 0)
    {
        int tenth = (t->sample_rate / 10) * t->bytes_per_frame;

        if(tenth > min_size)
            min_size = tenth;
    }

    if(min_size < 65536)
        min_size = 65536;

    if(t->buffer && t->buffer_size >= min_size)
        return 1;

    if(t->buffer)
    {
        free(t->buffer);
        t->buffer = NULL;
    }

    t->buffer = (uint8_t *)vj_malloc((size_t)min_size);

    if(!t->buffer)
    {
        t->buffer_size = 0;
        return 0;
    }

    t->buffer_size = min_size;
    return 1;
}


static inline void ab_cpu_relax(void)
{
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#else
    sched_yield();
#endif
}

static inline int ab_record_try_lock(vj_audio_beat_shared_t *s)
{
#if defined(__GNUC__) || defined(__clang__)
    if(__atomic_load_n(&s->record_lock, __ATOMIC_RELAXED) != 0)
        return 0;

    return __atomic_exchange_n(&s->record_lock, 1, __ATOMIC_ACQUIRE) == 0;
#else
    return __sync_lock_test_and_set(&s->record_lock, 1) == 0;
#endif
}

static inline void ab_record_lock(vj_audio_beat_shared_t *s)
{
    int rounds = 0;

    while(!ab_record_try_lock(s))
    {
        if(rounds < 256)
        {
            for(int i = 0; i < 32; i++)
                ab_cpu_relax();

            rounds++;
        }
        else if(rounds < 512)
        {
            sched_yield();
            rounds++;
        }
        else
        {
            ab_sleep_us(50);
        }
    }
}

static inline void ab_record_unlock(vj_audio_beat_shared_t *s)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(&s->record_lock, 0, __ATOMIC_RELEASE);
#else
    __sync_lock_release(&s->record_lock);
#endif
}

static void ab_record_ring_clear_locked(vj_audio_beat_shared_t *s)
{
    if(!s)
        return;

    s->record_write_pos = 0;
    s->record_bytes_available = 0;
}

static void ab_record_ring_clear(vj_audio_beat_shared_t *s)
{
    if(!s)
        return;

    ab_record_lock(s);
    ab_record_ring_clear_locked(s);
    ab_record_unlock(s);
}

static void ab_record_ring_free(vj_audio_beat_shared_t *s)
{
    if(!s)
        return;

    ab_record_lock(s);

    if(s->record_ring) {
        free(s->record_ring);
        s->record_ring = NULL;
    }

    s->record_ring_size = 0;
    s->record_write_pos = 0;
    s->record_bytes_available = 0;
    s->record_channels = 0;
    s->record_bytes_per_frame = 0;
    s->record_bits_per_channel = 0;
    s->record_sample_rate = 0;

    ab_record_unlock(s);
}

static int ab_record_ring_prepare(vj_audio_beat_shared_t *s,
                                  const vj_audio_beat_thread_t *t)
{
    int wanted;

    if(!s || !t || t->bytes_per_frame <= 0 || t->sample_rate <= 0)
        return 0;

    wanted = t->sample_rate * t->bytes_per_frame * 4; /* 4 seconds */

    if(wanted < 65536)
        wanted = 65536;

    wanted = ab_floor_to_multiple_i(wanted, t->bytes_per_frame);

    if(wanted < t->bytes_per_frame)
        wanted = t->bytes_per_frame * 1024;

    ab_record_lock(s);

    if(s->record_ring &&
       s->record_ring_size >= wanted &&
       s->record_channels == t->channels &&
       s->record_bytes_per_frame == t->bytes_per_frame &&
       s->record_bits_per_channel == t->bits_per_channel &&
       s->record_sample_rate == t->sample_rate)
    {
        ab_record_unlock(s);
        return 1;
    }

    if(s->record_ring) {
        free(s->record_ring);
        s->record_ring = NULL;
    }

    s->record_ring = (uint8_t *)vj_malloc((size_t)wanted);

    if(!s->record_ring) {
        s->record_ring_size = 0;
        s->record_write_pos = 0;
        s->record_bytes_available = 0;
        ab_record_unlock(s);
        return 0;
    }

    s->record_ring_size = wanted;
    s->record_write_pos = 0;
    s->record_bytes_available = 0;

    s->record_channels = t->channels;
    s->record_bytes_per_frame = t->bytes_per_frame;
    s->record_bits_per_channel = t->bits_per_channel;
    s->record_sample_rate = t->sample_rate;

    ab_record_unlock(s);
    return 1;
}

static void ab_record_ring_publish(vj_audio_beat_shared_t *s,
                                   const vj_audio_beat_thread_t *t,
                                   const uint8_t *src,
                                   int bytes)
{
    int pos;
    int first;
    int drop;

    if(!s || !t || !src || bytes <= 0)
        return;

    if(!ab_record_ring_prepare(s, t))
        return;

    bytes = ab_floor_to_multiple_i(bytes, t->bytes_per_frame);

    if(bytes <= 0)
        return;

    ab_record_lock(s);

    if(bytes > s->record_ring_size) {
        src += bytes - s->record_ring_size;
        bytes = s->record_ring_size;
    }

    drop = (s->record_bytes_available + bytes) - s->record_ring_size;

    if(drop > 0) {
        drop = ab_floor_to_multiple_i(drop, s->record_bytes_per_frame);
        s->record_bytes_available -= drop;

        if(s->record_bytes_available < 0)
            s->record_bytes_available = 0;

        ab_add_l(&s->record_overruns, 1);
    }

    pos = s->record_write_pos;
    first = s->record_ring_size - pos;

    if(first > bytes)
        first = bytes;

    veejay_memcpy(s->record_ring + pos, src, first);

    if(bytes > first)
        veejay_memcpy(s->record_ring, src + first, bytes - first);

    pos += bytes;

    if(pos >= s->record_ring_size)
        pos -= s->record_ring_size;

    s->record_write_pos = pos;
    s->record_bytes_available += bytes;

    if(s->record_bytes_available > s->record_ring_size)
        s->record_bytes_available = s->record_ring_size;

    ab_record_unlock(s);
}

static inline int ab_read_sample(const uint8_t *p, int sample_bytes)
{
    if(sample_bytes == 1)
        return ((int)p[0] - 128) << 8;

    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline void ab_write_sample(uint8_t *p, int sample_bytes, int v)
{
    v = (v < -32768) ? -32768 : ((v > 32767) ? 32767 : v);

    if(sample_bytes == 1) {
        p[0] = (uint8_t)((v >> 8) + 128);
        return;
    }

    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

int vj_audio_beat_copy_record_audio(vj_audio_beat_shared_t *s,
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
    int available_src_frames;
    int need_src_frames;
    int out_frames;
    int read_pos;
    int consume_bytes;

    if(s && s->sync) {
        return vj_audio_sync_copy_record_audio(
            s->sync,
            dst,
            dst_frames,
            dst_frame_bytes,
            dst_channels,
            dst_sample_rate
        );
    }

    if(!s || !dst || dst_frames <= 0 || dst_frame_bytes <= 0 ||
       dst_channels <= 0 || dst_sample_rate <= 0)
        return 0;

    if(!ab_load_i(&s->enabled) ||
       !ab_load_i(&s->running) ||
       !ab_load_i(&s->open))
        return 0;

    ab_record_lock(s);

    if(!s->record_ring ||
       s->record_ring_size <= 0 ||
       s->record_bytes_available <= 0 ||
       s->record_bytes_per_frame <= 0 ||
       s->record_channels <= 0 ||
       s->record_sample_rate <= 0)
    {
        ab_record_unlock(s);
        return 0;
    }

    src_rate = s->record_sample_rate;
    src_channels = s->record_channels;
    src_frame_bytes = s->record_bytes_per_frame;

    if((src_frame_bytes % src_channels) != 0 ||
       (dst_frame_bytes % dst_channels) != 0)
    {
        ab_record_unlock(s);
        return 0;
    }

    src_sample_bytes = src_frame_bytes / src_channels;
    dst_sample_bytes = dst_frame_bytes / dst_channels;

    if((src_sample_bytes != 1 && src_sample_bytes != 2) ||
       (dst_sample_bytes != 1 && dst_sample_bytes != 2) ||
       src_channels > 2 ||
       dst_channels > 2)
    {
        ab_record_unlock(s);
        return 0;
    }

    available_src_frames = s->record_bytes_available / src_frame_bytes;

    need_src_frames = (int)(((long long)dst_frames * (long long)src_rate +
                             (long long)dst_sample_rate - 1LL) /
                            (long long)dst_sample_rate);

    if(need_src_frames < 1)
        need_src_frames = 1;

    if(available_src_frames < need_src_frames) {
        out_frames = (int)(((long long)available_src_frames *
                            (long long)dst_sample_rate) /
                           (long long)src_rate);

        if(out_frames <= 0) {
            ab_add_l(&s->record_underruns, 1);
            ab_record_unlock(s);
            return 0;
        }

        if(out_frames > dst_frames)
            out_frames = dst_frames;

        need_src_frames = available_src_frames;
        ab_add_l(&s->record_underruns, 1);
    } else {
        out_frames = dst_frames;
    }

    read_pos = s->record_write_pos - s->record_bytes_available;
    if(src_rate == dst_sample_rate &&
       src_channels == dst_channels &&
       src_frame_bytes == dst_frame_bytes)
    {
        int copy_bytes = out_frames * src_frame_bytes;
        int first = s->record_ring_size - read_pos;

        if(first > copy_bytes)
            first = copy_bytes;

        veejay_memcpy(dst, s->record_ring + read_pos, first);

        if(copy_bytes > first)
            veejay_memcpy(dst + first, s->record_ring, copy_bytes - first);

        consume_bytes = copy_bytes;

        if(consume_bytes > s->record_bytes_available)
            consume_bytes = s->record_bytes_available;

        s->record_bytes_available -= consume_bytes;

        if(s->record_bytes_available < 0)
            s->record_bytes_available = 0;

        ab_record_unlock(s);
        return out_frames;
    }
    while(read_pos < 0)
        read_pos += s->record_ring_size;

    for(int i = 0; i < out_frames; i++) {
        int src_i = (int)(((long long)i * (long long)src_rate) /
                          (long long)dst_sample_rate);
        int src_pos;
        const uint8_t *sf;
        uint8_t *df;
        int l;
        int r;

        if(src_i >= need_src_frames)
            src_i = need_src_frames - 1;

        src_pos = read_pos + (src_i * src_frame_bytes);

        while(src_pos >= s->record_ring_size)
            src_pos -= s->record_ring_size;

        sf = s->record_ring + src_pos;
        df = dst + ((size_t)i * (size_t)dst_frame_bytes);

        if(src_channels == 1) {
            l = r = ab_read_sample(sf, src_sample_bytes);
        } else {
            l = ab_read_sample(sf, src_sample_bytes);
            r = ab_read_sample(sf + src_sample_bytes, src_sample_bytes);
        }

        if(dst_channels == 1) {
            ab_write_sample(df, dst_sample_bytes, (l + r) / 2);
        } else {
            ab_write_sample(df, dst_sample_bytes, l);
            ab_write_sample(df + dst_sample_bytes, dst_sample_bytes, r);
        }
    }

    consume_bytes = need_src_frames * src_frame_bytes;

    if(consume_bytes > s->record_bytes_available)
        consume_bytes = s->record_bytes_available;

    s->record_bytes_available -= consume_bytes;

    if(s->record_bytes_available < 0)
        s->record_bytes_available = 0;

    ab_record_unlock(s);

    return out_frames;
}

static int ab_configure_from_jack(vj_audio_beat_shared_t *s, vj_audio_beat_thread_t *t)
{
    int ch;
    int rate;
    int bits;
    int req_ch;
    unsigned long bpf;
    static long last_capture_fail_log_ms = 0;

    if(!s || !t)
        return 0;

    req_ch = ab_load_i(&s->input_channels_request);

    if(req_ch < 1)
        req_ch = 2;
    else if(req_ch > 2)
        req_ch = 2;

    if(!vj_jack_is_running())
    {
        vj_jack_initialize();

        if(!vj_jack_init_capture(req_ch, 16, 0))
        {
            long now = ab_now_ms();

            if(last_capture_fail_log_ms == 0 ||
               (now - last_capture_fail_log_ms) >= 2000)
            {
                last_capture_fail_log_ms = now;
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-BEAT] unable to initialize JACK capture ports (%d channel%s)",
                           req_ch,
                           req_ch == 1 ? "" : "s");
            }

            ab_store_i(&s->open, 0);
            return 0;
        }

        vj_jack_enable();
    }
    else if(!vj_jack_has_input())
    {
        long now = ab_now_ms();

        if(!vj_jack_has_output())
        {
            vj_jack_stop();
            vj_jack_initialize();

            if(!vj_jack_init_capture(req_ch, 16, 0))
            {
                if(last_capture_fail_log_ms == 0 ||
                   (now - last_capture_fail_log_ms) >= 2000)
                {
                    last_capture_fail_log_ms = now;
                    veejay_msg(VEEJAY_MSG_WARNING,
                               "[AUDIO-BEAT] unable to reopen JACK as capture-only client (%d channel%s)",
                               req_ch,
                               req_ch == 1 ? "" : "s");
                }

                ab_store_i(&s->open, 0);
                return 0;
            }

            vj_jack_enable();
        }
        else
        {
            if(last_capture_fail_log_ms == 0 ||
               (now - last_capture_fail_log_ms) >= 2000)
            {
                last_capture_fail_log_ms = now;
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-BEAT] JACK is already open for playback but has no capture input ports; this JACK wrapper cannot add inputs to an open output-only client, restart audio in duplex/capture mode");
            }

            ab_store_i(&s->open, 0);
            return 0;
        }
    }
    else
    {
        ch = vj_jack_get_input_channels();

        if(ch > 0 && ch != req_ch && !vj_jack_has_output())
        {
            vj_jack_stop();
            vj_jack_initialize();

            if(!vj_jack_init_capture(req_ch, 16, 0))
            {
                long now = ab_now_ms();

                if(last_capture_fail_log_ms == 0 ||
                   (now - last_capture_fail_log_ms) >= 2000)
                {
                    last_capture_fail_log_ms = now;
                    veejay_msg(VEEJAY_MSG_WARNING,
                               "[AUDIO-BEAT] unable to reconfigure JACK capture channel count old=%d requested=%d",
                               ch,
                               req_ch);
                }

                ab_store_i(&s->open, 0);
                return 0;
            }

            vj_jack_enable();
        }
        else if(ch > 0 && ch != req_ch)
        {
            long now = ab_now_ms();

            if(last_capture_fail_log_ms == 0 ||
               (now - last_capture_fail_log_ms) >= 2000)
            {
                last_capture_fail_log_ms = now;
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[AUDIO-BEAT] requested %d JACK capture channel(s), but open duplex client has %d; using existing ports",
                           req_ch,
                           ch);
            }
        }
    }

    if(!vj_jack_has_input())
    {
        long now = ab_now_ms();

        if(last_capture_fail_log_ms == 0 ||
           (now - last_capture_fail_log_ms) >= 2000)
        {
            last_capture_fail_log_ms = now;
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[AUDIO-BEAT] JACK capture is not available; expected input ports are missing");
        }

        ab_store_i(&s->open, 0);
        return 0;
    }

    ch = vj_jack_get_input_channels();
    bpf = vj_jack_get_bytes_per_input_frame();
    rate = vj_jack_get_client_samplerate();

    if(rate <= 0)
        rate = vj_jack_get_rate();

    if(ch <= 0 || bpf == 0 || rate <= 0)
    {
        long now = ab_now_ms();

        if(last_capture_fail_log_ms == 0 ||
           (now - last_capture_fail_log_ms) >= 2000)
        {
            last_capture_fail_log_ms = now;
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[AUDIO-BEAT] JACK capture has invalid format: channels=%d bpf=%lu rate=%d",
                       ch,
                       bpf,
                       rate);
        }

        ab_store_i(&s->open, 0);
        return 0;
    }

    bits = ((int)bpf * 8) / ch;

    if(bits != 16 && bits != 8)
    {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[AUDIO-BEAT] unsupported JACK capture format %d bit",
                   bits);
        ab_store_i(&s->open, 0);
        return 0;
    }

    t->channels = ch;
    t->bytes_per_frame = (int)bpf;
    t->bits_per_channel = bits;
    t->sample_rate = rate;

    if(!ab_thread_prepare_buffer(t))
    {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[AUDIO-BEAT] unable to allocate capture buffer");
        ab_store_i(&s->open, 0);
        return 0;
    }

    ab_thread_reset(t);
    vj_jack_reset_input();
    ab_record_ring_clear(s);

    t->open = 1;
    t->last_reset_seq = ab_load_i(&s->reset_seq);

    ab_store_i(&s->channels, ch);
    ab_store_i(&s->bytes_per_frame, (int)bpf);
    ab_store_i(&s->bits_per_channel, bits);
    ab_store_i(&s->sample_rate, rate);
    ab_store_i(&s->open, 1);

    last_capture_fail_log_ms = 0;

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] JACK input analysis ready: %d channel(s), %d Hz, %d bit",
               ch,
               rate,
               bits);
    ab_log_config(s, "after JACK open");
    return 1;
}

static int ab_configure_from_sync(vj_audio_beat_shared_t *s, vj_audio_beat_thread_t *t)
{
    int ch = 0;
    int bpf = 0;
    int bits = 0;
    int rate = 0;

    if(!s || !t || !s->sync)
        return 0;

    if(!vj_audio_sync_get_format(s->sync, &ch, &bpf, &bits, &rate)) {
        ab_store_i(&s->open, 0);
        return 0;
    }

    if(ch <= 0 || bpf <= 0 || rate <= 0 || (bits != 8 && bits != 16)) {
        ab_store_i(&s->open, 0);
        return 0;
    }

    t->channels = ch;
    t->bytes_per_frame = bpf;
    t->bits_per_channel = bits;
    t->sample_rate = rate;

    if(!ab_thread_prepare_buffer(t)) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[AUDIO-BEAT] unable to allocate sync analysis buffer");
        ab_store_i(&s->open, 0);
        return 0;
    }

    ab_thread_reset(t);
    t->open = 1;
    t->last_reset_seq = ab_load_i(&s->reset_seq);

    ab_store_i(&s->channels, ch);
    ab_store_i(&s->bytes_per_frame, bpf);
    ab_store_i(&s->bits_per_channel, bits);
    ab_store_i(&s->sample_rate, rate);
    ab_store_i(&s->open, 1);

    if(ab_sync_source(s) == VJ_AUDIO_SYNC_SOURCE_PUSH)
        ab_arm_sync_read_probe(s, t, t->last_reset_seq);
    else
        vj_audio_sync_reset_beat_reader(s->sync);

    return 1;
}

static int ab_analyse_block(vj_audio_beat_shared_t *s, vj_audio_beat_thread_t *t, const uint8_t *data, int bytes)
{
    int frames;
    int threshold;
    int hit_candidate;
    double energy;
    double flux;
    double last;
    double low_sum;
    double mid_sum;
    double high_sum;
    double low_a;
    double mid_a;
    double bass_norm;
    double mid_norm;
    double high_norm;
    double band_balance;
    double block_level;
    double log_energy;
    double energy_rise;
    double flux_feature;
    double rise_z;
    double flux_z;
    double onset_z;
    double onset_threshold_z;
#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    double onset_score;
#endif
    double level;
    double transient_norm;
    double flux_norm;
    double min_hit_level;
    double stat_alpha;
    double threshold_norm;
    double level_ratio;
    double flux_ratio_abs;
    double abs_level_gate;
    double abs_flux_gate;
    double beat_age_ms;
    double expected_period_ms;
    double tempo_early_ms;
    double tempo_late_ms;
    double groove_early_ms;
    double groove_late_ms;
    double soft_onset_z;
    long now_ms;
    int audible;
    int coherent_onset;
    int armed;
    int settled;
    int first_hit_ready;
    int absolute_onset;
    int tempo_known;
    int tempo_onset;
    int groove_onset;
    int transient_q8;
    double level_boost;
    double flux_boost;
    double kick_score;
    double snare_score;
    double hat_score;
    double level_floor;
    double envelope_floor;
    double broadband_norm;
    double scratch_tone;
    double scratch_raw;
    double scratch_amount;
    double scratch_velocity;
    double scratch_burst;
    double scratch_rise;
    double scratch_turn_score;
    long scratch_turn_since_ms;
    int scratch_turn_edge;
    int scratch_candidate_raw;
    int scratch_candidate;
    int scratch_dominant;
    int scratch_reject;
    int scratch_dir;
    ab_scratch_decision_t scratch_decision;
    int breakbeat_mode;
    int dense_onset;
    int dense_body_onset;
    int kick_onset;
    int snare_onset;
#if defined(VEEJAY_AUDIO_BEAT_DEBUG) || defined(VEEJAY_AUDIO_BEAT_TRACE_REJECTS)
    const char *reject_reason;
#endif

    if(!s || !t || !data || bytes <= 0 || t->bytes_per_frame <= 0 || t->channels <= 0)
        return 0;

    frames = bytes / t->bytes_per_frame;

    if(frames <= 0)
        return 0;

    ab_analysis_luts_init();
    ab_prepare_analysis_filters(t);

    {
        const int bpf = t->bytes_per_frame;
        const int ch = t->channels;
        const float inv_frames = 1.0f / (float)frames;
        const float inv_ch = ch > 0 ? 1.0f / (float)ch : 1.0f;
        const float low_af = t->filter_low_alpha;
        const float mid_af = t->filter_mid_alpha;
        const uint8_t *p = data;
        float energy_f = 0.0f;
        float flux_f = 0.0f;
        float low_sum_f = 0.0f;
        float mid_sum_f = 0.0f;
        float high_sum_f = 0.0f;
        float last_f = (float)t->last_sample;
        float low_lp = (float)t->band_low_lp;
        float mid_lp = (float)t->band_mid_lp;

        if(t->bits_per_channel == 16)
        {
            if(ch == 2 && bpf >= 4)
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = (ab_lut_s16(p) + ab_lut_s16(p + 2)) * 0.5f;
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
            else if(ch == 1 && bpf >= 2)
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = ab_lut_s16(p);
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
            else
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = 0.0f;

                    for(int c = 0; c < ch; c++)
                        mono += ab_lut_s16(p + (c * 2));

                    mono *= inv_ch;
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
        }
        else
        {
            if(ch == 2 && bpf >= 2)
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = (ab_lut_u8(p) + ab_lut_u8(p + 1)) * 0.5f;
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
            else if(ch == 1 && bpf >= 1)
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = ab_lut_u8(p);
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
            else
            {
                for(int i = 0; i < frames; i++, p += bpf)
                {
                    float mono = 0.0f;

                    for(int c = 0; c < ch; c++)
                        mono += ab_lut_u8(p + c);

                    mono *= inv_ch;
                    ab_accum_mono_sample(mono, &last_f, low_af, mid_af, &low_lp, &mid_lp,
                                         &energy_f, &flux_f, &low_sum_f, &mid_sum_f, &high_sum_f);
                }
            }
        }

        t->band_low_lp = (double)low_lp;
        t->band_mid_lp = (double)mid_lp;
        last = (double)last_f;
        energy = (double)(energy_f * inv_frames);
        flux = (double)(flux_f * inv_frames);
        low_sum = (double)(low_sum_f * inv_frames);
        mid_sum = (double)(mid_sum_f * inv_frames);
        high_sum = (double)(high_sum_f * inv_frames);
        low_a = (double)low_af;
        mid_a = (double)mid_af;
        (void)low_a;
        (void)mid_a;
    }

    t->last_sample = last;
    t->blocks_seen++;

    t->band_low_env = t->band_low_env * 0.72 + sqrt(low_sum) * 0.28;
    t->band_mid_env = t->band_mid_env * 0.68 + sqrt(mid_sum) * 0.32;
    t->band_high_env = t->band_high_env * 0.62 + sqrt(high_sum) * 0.38;

    t->band_low_slow = t->band_low_slow * 0.992 + t->band_low_env * 0.008;
    t->band_mid_slow = t->band_mid_slow * 0.992 + t->band_mid_env * 0.008;
    t->band_high_slow = t->band_high_slow * 0.992 + t->band_high_env * 0.008;

    bass_norm = ab_soft_ratio_unit(t->band_low_env,  t->band_low_slow,  1.055, 1.08);
    mid_norm  = ab_soft_ratio_unit(t->band_mid_env,  t->band_mid_slow,  1.070, 1.02);
    high_norm = ab_soft_ratio_unit(t->band_high_env, t->band_high_slow, 1.085, 0.98);

    band_balance = ab_clampd((high_norm - bass_norm + 1.0) * 0.5, 0.0, 1.0);

    block_level = sqrt(energy);
    log_energy = log(energy + 1.0e-12);

    if(t->blocks_seen <= 1 || t->last_log_energy == 0.0)
    {
        t->last_log_energy = log_energy;
        t->fast_energy = energy;
        t->slow_energy = energy;
        t->fast_flux = flux;
        t->slow_flux = flux;
        t->envelope = block_level;

        ab_publish_i_cached(&s->level_q15, &t->pub_level_q15, ab_q15(block_level));
        ab_publish_i_cached(&s->envelope_q15, &t->pub_envelope_q15, ab_q15(t->envelope));
        ab_publish_i_cached(&s->flux_q15, &t->pub_flux_q15, 0);
        ab_publish_i_cached(&ab_band_low_q15, &t->pub_band_low_q15, ab_q15(bass_norm));
        ab_publish_i_cached(&ab_band_mid_q15, &t->pub_band_mid_q15, ab_q15(mid_norm));
        ab_publish_i_cached(&ab_band_high_q15, &t->pub_band_high_q15, ab_q15(high_norm));
        ab_publish_i_cached(&ab_band_balance_q15, &t->pub_band_balance_q15, ab_q15(band_balance));
        ab_publish_i_cached(&s->transient_norm_q15, &t->pub_transient_norm_q15, 0);
        ab_publish_i_cached(&s->transient_q8, &t->pub_transient_q8, 0);
        ab_source_activity_note(ab_now_ms(),
                                block_level,
                                VEEJAY_AUDIO_BEAT_MIN_RELIABLE_RMS,
                                t->envelope,
                                0.0);

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
        t->debug_blocks++;
        {
            long now = ab_now_ms();

            if(t->last_debug_ms == 0 || (now - t->last_debug_ms) >= VEEJAY_AUDIO_BEAT_DEBUG_INTERVAL_MS)
            {
                t->last_debug_ms = now;

                AB_DBG("warmup frames=%d energy=%.8f rms=%.6f flux=%.8f adaptive_settle=%d",
                    frames,
                    energy,
                    block_level,
                    flux,
                    VEEJAY_AUDIO_BEAT_ADAPT_WARMUP_BLOCKS + VEEJAY_AUDIO_BEAT_SETTLE_BLOCKS);
            }
        }
#endif

        return 0;
    }

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    t->debug_blocks++;
#endif

    threshold = ab_load_i(&s->threshold);

    if(threshold < 1)
        threshold = 1;

    energy_rise = log_energy > t->last_log_energy
        ? log_energy - t->last_log_energy
        : 0.0;

    t->last_log_energy = log_energy;

    flux_feature = log(1.0 + (flux * 512.0));

    rise_z = ab_zscore(energy_rise, t->rise_mean, t->rise_var);
    flux_z = ab_zscore(flux_feature, t->flux_mean, t->flux_var);

    if(rise_z < 0.0)
        rise_z = 0.0;

    if(flux_z < 0.0)
        flux_z = 0.0;

    onset_z = (rise_z * 0.45) + (flux_z * 0.55);
    onset_threshold_z = ab_threshold_to_sigma(threshold);
#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    onset_score = onset_z * 100.0;
#endif

    threshold_norm = ((double)threshold - 30.0) / 370.0;

    if(threshold_norm < 0.0)
        threshold_norm = 0.0;
    else if(threshold_norm > 1.0)
        threshold_norm = 1.0;

    t->fast_energy = t->fast_energy * 0.55 + energy * 0.45;
    t->slow_energy = t->slow_energy * 0.992 + energy * 0.008;
    t->fast_flux = t->fast_flux * 0.50 + flux * 0.50;
    t->slow_flux = t->slow_flux * 0.985 + flux * 0.015;

    level = sqrt(t->fast_energy);

    level_ratio = level / (sqrt(t->slow_energy) + 0.000001);
    flux_ratio_abs = t->fast_flux / (t->slow_flux + 0.000001);

    abs_level_gate = 1.08 + (threshold_norm * 0.24);
    abs_flux_gate = 1.12 + (threshold_norm * 0.52);

    t->envelope = level > t->envelope
        ? (t->envelope * 0.35 + level * 0.65)
        : (t->envelope * 0.94 + level * 0.06);

    t->onset_smooth = onset_z > t->onset_smooth
        ? (t->onset_smooth * 0.25 + onset_z * 0.75)
        : (t->onset_smooth * 0.88 + onset_z * 0.12);

    min_hit_level = 0.0075 + ((double)threshold * 0.000030);

    if(min_hit_level < VEEJAY_AUDIO_BEAT_MIN_RELIABLE_RMS)
        min_hit_level = VEEJAY_AUDIO_BEAT_MIN_RELIABLE_RMS;

    audible = block_level >= min_hit_level;

    now_ms = ab_now_ms();
    beat_age_ms = t->last_hit_ms > 0 ? (double)(now_ms - t->last_hit_ms) : 0.0;
    expected_period_ms = t->beat_period_ms;

    tempo_known = expected_period_ms >= (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
                  expected_period_ms <= (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS;

    if(tempo_known)
    {
        double min_period = (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS;

        tempo_early_ms = expected_period_ms * 0.62;
        tempo_late_ms = expected_period_ms * 1.45;
        groove_early_ms = expected_period_ms * 0.42;
        groove_late_ms = expected_period_ms * 0.62;

        if(tempo_early_ms < min_period * 0.60)
            tempo_early_ms = min_period * 0.60;

        if(groove_early_ms < min_period * 0.72)
            groove_early_ms = min_period * 0.72;

        if(groove_late_ms < min_period * 0.98)
            groove_late_ms = min_period * 0.98;

        if(groove_late_ms > expected_period_ms * 0.66)
            groove_late_ms = expected_period_ms * 0.66;

        if(groove_late_ms <= groove_early_ms)
            groove_late_ms = groove_early_ms + 1.0;
    }
    else
    {
        tempo_early_ms = 0.0;
        tempo_late_ms = 0.0;
        groove_early_ms = 0.0;
        groove_late_ms = 0.0;
    }

    armed = t->blocks_seen > VEEJAY_AUDIO_BEAT_ARM_BLOCKS &&
            t->blocks_seen > VEEJAY_AUDIO_BEAT_ADAPT_WARMUP_BLOCKS;

    settled = armed &&
              (t->last_hit_ms > 0 ||
               t->blocks_seen > (VEEJAY_AUDIO_BEAT_ADAPT_WARMUP_BLOCKS +
                                 VEEJAY_AUDIO_BEAT_SETTLE_BLOCKS));

    first_hit_ready = t->last_hit_ms > 0 ||
                      block_level >= VEEJAY_AUDIO_BEAT_FIRST_HIT_MIN_RMS;


    soft_onset_z = onset_threshold_z * (0.42 + threshold_norm * 0.10);

    transient_norm = ab_clampd(onset_z / (onset_threshold_z * 2.0), 0.0, 1.0);
    flux_norm = ab_clampd(flux_z / 6.0, 0.0, 1.0);

    level_boost = ab_clampd((level_ratio - 1.0) * 1.80, 0.0, 1.0);
    flux_boost = ab_clampd((flux_ratio_abs - 1.0) * 1.25, 0.0, 1.0);

    level_floor = ab_clampd((level - min_hit_level) / (min_hit_level * 7.0 + 0.000001), 0.0, 1.0);
    envelope_floor = ab_clampd((t->envelope - min_hit_level) / (min_hit_level * 8.5 + 0.000001), 0.0, 1.0);

    ab_source_activity_note(now_ms, block_level, min_hit_level, t->envelope, flux_norm);

    broadband_norm =
        level_floor * 0.24 +
        envelope_floor * 0.22 +
        bass_norm * 0.14 +
        mid_norm * 0.16 +
        high_norm * 0.12 +
        flux_norm * 0.16 +
        level_boost * 0.08 +
        flux_boost * 0.08;

    broadband_norm = ab_clampd(broadband_norm, 0.0, 1.0);

    scratch_tone =
        mid_norm * 0.30 +
        high_norm * 0.38 +
        flux_norm * 0.22 +
        flux_boost * 0.14 +
        broadband_norm * 0.12 -
        bass_norm * 0.16;

    if(scratch_tone < 0.0)
        scratch_tone = 0.0;
    else if(scratch_tone > 1.0)
        scratch_tone = 1.0;

    scratch_raw =
        scratch_tone * 0.50 +
        flux_norm * 0.20 +
        flux_boost * 0.14 +
        transient_norm * 0.08 +
        level_floor * 0.08;

    scratch_raw = ab_clampd(scratch_raw, 0.0, 1.0);

    t->scratch_env = scratch_raw > t->scratch_env
        ? (t->scratch_env * 0.38 + scratch_raw * 0.62)
        : (t->scratch_env * 0.86 + scratch_raw * 0.14);

    t->scratch_slow = t->scratch_slow * 0.988 + t->scratch_env * 0.012;

    scratch_rise = t->scratch_env - t->scratch_slow;
    if(scratch_rise < 0.0)
        scratch_rise = 0.0;

    scratch_burst = scratch_rise * 2.10 + flux_z * 0.055 + flux_ratio_abs * 0.015;
    scratch_burst = ab_clampd(scratch_burst, 0.0, 1.0);

    t->scratch_burst_env = scratch_burst > t->scratch_burst_env
        ? (t->scratch_burst_env * 0.42 + scratch_burst * 0.58)
        : (t->scratch_burst_env * 0.82 + scratch_burst * 0.18);

    scratch_amount = ab_clampd(t->scratch_env * 0.72 + t->scratch_burst_env * 0.28, 0.0, 1.0);
    scratch_velocity = ab_clampd((flux_norm * 0.46 + flux_boost * 0.22 + high_norm * 0.18 + mid_norm * 0.10 + scratch_burst * 0.18), 0.0, 1.0);
    scratch_turn_score = ab_clampd(scratch_amount * 0.34 +
                                   scratch_velocity * 0.38 +
                                   scratch_burst * 0.36 +
                                   scratch_tone * 0.12 -
                                   bass_norm * 0.08,
                                   0.0,
                                   1.0);

    scratch_turn_since_ms = t->last_scratch_turn_ms > 0 ?
        now_ms - t->last_scratch_turn_ms : 2147483647L;
    if(scratch_turn_since_ms < 0)
        scratch_turn_since_ms = 0;

    breakbeat_mode = ab_action_is_breakbeat(ab_load_i(&s->action_mode));

    scratch_turn_edge =
        breakbeat_mode &&
        settled &&
        audible &&
        scratch_amount >= 0.30 &&
        scratch_velocity >= 0.34 &&
        scratch_burst >= 0.22 &&
        scratch_turn_score >= 0.42 &&
        scratch_turn_since_ms >= 24L &&
        (
            scratch_velocity >= 0.50 ||
            scratch_burst >= 0.44 ||
            scratch_amount > t->scratch_last_amount + 0.025 ||
            scratch_amount + 0.045 < t->scratch_last_amount
        );

    if(scratch_amount < 0.10 && t->scratch_last_amount < 0.12)
    {
        t->scratch_dir = 1;
        t->last_scratch_turn_dir = 0;
        t->last_scratch_turn_ms = 0;
        t->last_scratch_turn_score = 0.0;
        t->last_scratch_turn_edge = 0;
    }
    else if(scratch_turn_edge)
    {
        if(t->last_scratch_turn_dir == 0)
        {
            if(t->scratch_dir == 0)
                t->scratch_dir = 1;
            t->last_scratch_turn_dir = t->scratch_dir;
        }
        else
        {
            t->scratch_dir = -t->last_scratch_turn_dir;
            t->last_scratch_turn_dir = t->scratch_dir;
        }

        t->last_scratch_turn_ms = now_ms;
        t->last_scratch_turn_score = scratch_turn_score;
        t->last_scratch_turn_edge = 1;
    }
    else
    {
        if(scratch_amount > t->scratch_last_amount + 0.035 &&
           (scratch_velocity > 0.34 || scratch_burst > 0.22))
            t->scratch_dir = -t->scratch_dir;
        t->last_scratch_turn_edge = 0;
    }

    if(t->scratch_dir == 0)
        t->scratch_dir = 1;
    scratch_dir = t->scratch_dir;
    t->scratch_last_amount = scratch_amount;

    dense_onset =
        audible &&
        (
            (broadband_norm >= 0.42 &&
             (rise_z >= 0.055 || flux_z >= 0.075 || level_ratio >= 1.030 || flux_ratio_abs >= 1.050)) ||
            (level_floor >= 0.62 && (mid_norm + high_norm + flux_norm) >= 0.40 &&
             (rise_z >= 0.040 || flux_z >= 0.060 || level_ratio >= 1.020))
        );

    if(dense_onset && transient_norm < broadband_norm * 0.42)
        transient_norm = broadband_norm * 0.42;

    if(dense_onset && flux_norm < broadband_norm * 0.36)
        flux_norm = broadband_norm * 0.36;

    /*
     * Cheap drum classifier:
     * kick  = low-band body + level rise
     * snare = mid/high crack + flux
     * hat   = high transient with little bass
     */
    kick_score =
        bass_norm * 0.62 +
        level_boost * 0.16 +
        transient_norm * 0.10 +
        level_floor * 0.08 +
        broadband_norm * 0.06;

    kick_score *= 1.0 - (0.32 * ab_clampd(high_norm - bass_norm, 0.0, 1.0));
    kick_score = ab_clampd(kick_score, 0.0, 1.0);

    snare_score =
        mid_norm * 0.38 +
        high_norm * 0.24 +
        flux_norm * 0.13 +
        flux_boost * 0.08 +
        transient_norm * 0.07 +
        broadband_norm * 0.10;

    snare_score *= 1.0 - (0.18 * bass_norm);
    snare_score = ab_clampd(snare_score, 0.0, 1.0);

    hat_score =
        high_norm * 0.60 +
        flux_norm * 0.15 +
        flux_boost * 0.09 +
        transient_norm * 0.06 +
        broadband_norm * 0.05;

    hat_score *= 1.0 - (0.55 * bass_norm);
    hat_score = ab_clampd(hat_score, 0.0, 1.0);

    kick_onset =
        kick_score >= 0.42 &&
        bass_norm >= 0.32 &&
        (rise_z >= 0.12 || flux_z >= 0.12);

    snare_onset =
        snare_score >= 0.46 &&
        (mid_norm + high_norm) >= 0.58 &&
        (flux_z >= 0.18 || rise_z >= 0.12);

    scratch_decision = ab_breakbeat_scratch_decide(t,
                                                     now_ms,
                                                     breakbeat_mode,
                                                     settled,
                                                     audible,
                                                     kick_onset,
                                                     snare_onset,
                                                     kick_score,
                                                     snare_score,
                                                     hat_score,
                                                     scratch_amount,
                                                     scratch_velocity,
                                                     scratch_burst,
                                                     mid_norm,
                                                     high_norm,
                                                     flux_norm,
                                                     flux_z,
                                                     flux_ratio_abs);

    scratch_candidate_raw = scratch_decision.raw;
    scratch_dominant = scratch_decision.dominant;
    scratch_candidate = scratch_decision.candidate;
    scratch_reject = scratch_decision.reject;

    t->last_scratch_candidate_raw = scratch_candidate_raw ? 1 : 0;
    t->last_scratch_dominant = scratch_dominant ? 1 : 0;
    t->last_scratch_candidate = scratch_candidate ? 1 : 0;
    t->last_scratch_reject = scratch_reject;

    /*
     * Dense/compressed material is useful as a continuous body/density
     * lane, but it must not become a machine-gun beat source.  Promote
     * dense blocks to beat events only when they have a body-hit shape and,
     * once a tempo exists, when they land in the musical window.
     */
    dense_body_onset =
        dense_onset &&
        (
            kick_onset ||
            snare_onset ||
            ((kick_score >= 0.44 || snare_score >= 0.46) &&
             (rise_z >= 0.12 || flux_z >= 0.14 || level_ratio >= 1.045)) ||
            (tempo_known &&
             beat_age_ms >= tempo_early_ms &&
             beat_age_ms <= tempo_late_ms &&
             (kick_score >= 0.34 || snare_score >= 0.38) &&
             (rise_z >= 0.08 || flux_z >= 0.10 || level_ratio >= 1.030))
        );

    absolute_onset =
        audible &&
        (
            kick_onset ||
            snare_onset ||

            (level_ratio >= abs_level_gate &&
             flux_ratio_abs >= abs_flux_gate) ||

            (level_ratio >= (abs_level_gate + 0.08) &&
             flux_z >= (0.20 + threshold_norm * 0.25)) ||

            (flux_ratio_abs >= (abs_flux_gate + 0.18) &&
             rise_z >= (0.18 + threshold_norm * 0.20)) ||

            (bass_norm >= 0.42 &&
             (flux_z >= 0.18 || rise_z >= 0.18)) ||

            dense_body_onset
        );

    groove_onset =
        tempo_known &&
        expected_period_ms >= ((double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS * 1.75) &&
        audible &&
        first_hit_ready &&
        beat_age_ms >= groove_early_ms &&
        beat_age_ms <= groove_late_ms &&
        (
            kick_onset ||
            snare_onset ||
            (kick_score >= 0.36 && bass_norm >= 0.26) ||
            (snare_score >= 0.40 && (mid_norm + high_norm) >= 0.48)
        ) &&
        hat_score < 0.82 &&
        (rise_z >= 0.10 || flux_z >= 0.14);

    tempo_onset =
        tempo_known &&
        audible &&
        first_hit_ready &&
        (
            groove_onset ||

            (beat_age_ms >= tempo_early_ms &&
             (
                absolute_onset ||

                (beat_age_ms <= tempo_late_ms &&
                 onset_z >= soft_onset_z &&
                 (rise_z >= 0.10 || flux_z >= 0.14)) ||

                (beat_age_ms > tempo_late_ms &&
                 (onset_z >= (soft_onset_z * 0.82) ||
                  rise_z >= 0.28 ||
                  flux_z >= 0.30))
             ))
        );

    coherent_onset =
        kick_onset ||
        snare_onset ||
        absolute_onset ||
        tempo_onset ||
        dense_body_onset ||
        ((rise_z >= 0.25) && (flux_z >= 0.25)) ||
        ((onset_z >= (onset_threshold_z + 0.85)) &&
         ((rise_z >= 0.18 && flux_z >= 0.10) ||
          (rise_z >= 0.10 && flux_z >= 0.18)));


    if(!settled)
    {
        bass_norm = 0.0;
        mid_norm = 0.0;
        high_norm = 0.0;
        band_balance = 0.0;
        transient_norm = 0.0;
        flux_norm = 0.0;
#ifdef VEEJAY_AUDIO_BEAT_DEBUG
        onset_score = 0.0;
#endif
        kick_score = 0.0;
        snare_score = 0.0;
        hat_score = 0.0;
        broadband_norm = 0.0;
        scratch_amount = 0.0;
        scratch_velocity = 0.0;
        scratch_burst = 0.0;
        scratch_turn_score = 0.0;
        scratch_turn_edge = 0;
        t->last_scratch_turn_edge = 0;
        scratch_candidate_raw = 0;
        scratch_candidate = 0;
        scratch_dominant = 0;
        scratch_reject = AB_SCRATCH_REJECT_RAW;
        dense_onset = 0;
        dense_body_onset = 0;
        kick_onset = 0;
        snare_onset = 0;
        groove_onset = 0;
    }

    ab_publish_i_cached(&s->level_q15, &t->pub_level_q15, ab_q15(level));
    ab_publish_i_cached(&s->envelope_q15, &t->pub_envelope_q15, ab_q15(t->envelope));
    ab_publish_i_cached(&s->flux_q15, &t->pub_flux_q15, ab_q15(flux_norm));
    ab_publish_i_cached(&ab_band_low_q15, &t->pub_band_low_q15, ab_q15(bass_norm));
    ab_publish_i_cached(&ab_band_mid_q15, &t->pub_band_mid_q15, ab_q15(mid_norm));
    ab_publish_i_cached(&ab_band_high_q15, &t->pub_band_high_q15, ab_q15(high_norm));
    ab_publish_i_cached(&ab_band_balance_q15, &t->pub_band_balance_q15, ab_q15(band_balance));
    ab_publish_i_cached(&ab_kick_q15, &t->pub_kick_q15, ab_q15(kick_score));
    ab_publish_i_cached(&ab_snare_q15, &t->pub_snare_q15, ab_q15(snare_score));
    ab_publish_i_cached(&ab_hat_q15, &t->pub_hat_q15, ab_q15(hat_score));
    ab_store_i(&ab_scratch_q15, ab_q15(scratch_amount));
    ab_store_i(&ab_scratch_velocity_q15, ab_q15(scratch_velocity));
    ab_store_i(&ab_scratch_burst_q15, ab_q15(scratch_burst));
    ab_store_i(&ab_scratch_dir, scratch_dir < 0 ? -1 : 1);

    transient_q8 = ab_publish_transient_q8(transient_norm);

    ab_publish_i_cached(&s->transient_norm_q15, &t->pub_transient_norm_q15, ab_q15(transient_norm));
    ab_publish_i_cached(&s->transient_q8, &t->pub_transient_q8, transient_q8);

    hit_candidate = settled &&
                    first_hit_ready &&
                    audible &&
                    ((coherent_onset &&
                      (absolute_onset || tempo_onset || dense_body_onset || onset_z >= onset_threshold_z)) ||
                     scratch_candidate ||
                     scratch_turn_edge);

    t->last_kick_score = kick_score;
    t->last_snare_score = snare_score;
    t->last_hat_score = hat_score;
    t->last_scratch_score = scratch_amount;

    if(hit_candidate && scratch_candidate &&
       scratch_amount >= kick_score * 1.04 &&
       scratch_amount >= snare_score * 1.02 &&
       scratch_amount >= hat_score * 0.92)
    {
        t->last_hit_kind = AB_HIT_SCRATCH;
    }
    else
    {
        t->last_hit_kind = hit_candidate
            ? ab_classify_hit(kick_score, snare_score, hat_score)
            : AB_HIT_NONE;

        if(hit_candidate && dense_body_onset && t->last_hit_kind == AB_HIT_NONE)
            t->last_hit_kind = AB_HIT_FULL;
    }

#if defined(VEEJAY_AUDIO_BEAT_DEBUG) || defined(VEEJAY_AUDIO_BEAT_TRACE_REJECTS)
    reject_reason = !armed ? "arming" :
        (!settled ? "settling" :
        (!first_hit_ready ? "first-hit-level" :
        (!audible ? "silence" :
        (!coherent_onset && !scratch_candidate ? "coherence" :
        (!scratch_candidate && !absolute_onset && !tempo_onset && !dense_body_onset && onset_z < onset_threshold_z ? "novelty" : "score")))));
#endif

    stat_alpha = !settled ? 0.055 :
                 (hit_candidate ? 0.0015 :
                 ((absolute_onset || tempo_onset || dense_body_onset || scratch_candidate) ? 0.0060 : 0.018));

    ab_ewstat_update(&t->rise_mean, &t->rise_var, energy_rise, stat_alpha);
    ab_ewstat_update(&t->flux_mean, &t->flux_var, flux_feature, stat_alpha);
    ab_ewstat_update(&t->onset_mean, &t->onset_var, onset_z, stat_alpha);

#ifdef VEEJAY_AUDIO_BEAT_TRACE_REJECTS
    if(!hit_candidate)
    {
        static long last_plain_analysis_log_ms = 0;
        long now_dbg = ab_now_ms();

        if(last_plain_analysis_log_ms == 0 ||
           (now_dbg - last_plain_analysis_log_ms) >= VEEJAY_AUDIO_BEAT_DEBUG_INTERVAL_MS)
        {
            last_plain_analysis_log_ms = now_dbg;
            veejay_msg(VEEJAY_MSG_INFO,
                       "[AUDIO-BEAT] analyse reject: reason=%s onset=%.2f sigma=%.2f threshold=%d rms=%.6f gate=%.6f flux=%.8f rise_z=%.3f flux_z=%.3f blocks=%d",
                       reject_reason,
                       onset_z,
                       onset_threshold_z,
                       threshold,
                       block_level,
                       min_hit_level,
                       flux,
                       rise_z,
                       flux_z,
                       t->blocks_seen);
        }
    }
#endif

#ifdef VEEJAY_AUDIO_BEAT_DEBUG
    t->dbg_energy = energy;
    t->dbg_flux = flux;
    t->dbg_block_level = block_level;
    t->dbg_level = level;
    t->dbg_envelope = t->envelope;
    t->dbg_bass = bass_norm;
    t->dbg_mid = mid_norm;
    t->dbg_high = high_norm;
    t->dbg_energy_ratio = rise_z;
    t->dbg_flux_ratio = flux_z;
    t->dbg_score = onset_score;
    t->dbg_min_hit_level = min_hit_level;
    t->dbg_min_hit_flux = onset_threshold_z;
    t->dbg_threshold = threshold;
    t->dbg_audible = audible;
    t->dbg_transient_present = coherent_onset && settled;
    t->dbg_hit_candidate = hit_candidate;

    if(hit_candidate)
    {
        t->debug_candidates++;

        AB_DBG("candidate onset=%.3f sigma=%.3f score=%.2f thr=%d rms=%.6f gate=%.6f flux=%.8f rise_z=%.3f flux_z=%.3f bass=%.3f mid=%.3f high=%.3f rise_mu=%.6f flux_mu=%.6f level=%.6f env=%.6f blocks=%d",
            onset_z,
            onset_threshold_z,
            onset_score,
            threshold,
            block_level,
            min_hit_level,
            flux,
            rise_z,
            flux_z,
            bass_norm,
            mid_norm,
            high_norm,
            t->rise_mean,
            t->flux_mean,
            level,
            t->envelope,
            t->blocks_seen);
    }
    else
    {
        if(!audible)
            t->debug_silence_rejects++;
        else if(!coherent_onset || !armed)
            t->debug_flux_rejects++;
        else
            t->debug_score_rejects++;

        {
            long now = ab_now_ms();

            if(t->last_debug_ms == 0 || (now - t->last_debug_ms) >= VEEJAY_AUDIO_BEAT_DEBUG_INTERVAL_MS)
            {
                t->last_debug_ms = now;

                AB_DBG("reject reason=%s onset=%.3f sigma=%.3f score=%.2f thr=%d rms=%.6f gate=%.6f flux=%.8f rise_z=%.3f flux_z=%.3f bass=%.3f mid=%.3f high=%.3f rise_mu=%.6f flux_mu=%.6f blocks=%d cand=%ld acc=%ld coolrej=%ld silrej=%ld fluxrej=%ld scorerej=%ld",
                    reject_reason,
                    onset_z,
                    onset_threshold_z,
                    onset_score,
                    threshold,
                    block_level,
                    min_hit_level,
                    flux,
                    rise_z,
                    flux_z,
                    bass_norm,
                    mid_norm,
                    high_norm,
                    t->rise_mean,
                    t->flux_mean,
                    t->blocks_seen,
                    t->debug_candidates,
                    t->debug_accepted,
                    t->debug_cooldown_rejects,
                    t->debug_silence_rejects,
                    t->debug_flux_rejects,
                    t->debug_score_rejects);
            }
        }
    }
#endif

    return hit_candidate;
}


static long ab_effective_cooldown_ms(vj_audio_beat_shared_t *s, const vj_audio_beat_thread_t *t)
{
    long configured;
    int action;

    if(!s)
        return 180L;

    configured = (long)ab_load_i(&s->cooldown_ms);

    if(configured < 40L)
        configured = 40L;
    else if(configured > 2000L)
        configured = 2000L;

    action = ab_load_i(&s->action_mode);

    if(ab_action_is_breakbeat(action))
    {
        long user_bias = configured;
        long musical = user_bias;
        int hit_kind = t ? t->last_hit_kind : AB_HIT_NONE;
        double hit_confidence = 0.0;

        if(t)
        {
            hit_confidence = t->last_kick_score;

            if(t->last_snare_score > hit_confidence)
                hit_confidence = t->last_snare_score;

            if(t->last_hat_score > hit_confidence)
                hit_confidence = t->last_hat_score;
        }

        if(hit_kind == AB_HIT_SCRATCH)
        {
            configured = (long)(36.0 + (1.0 - ab_clampd(t ? t->last_scratch_score : 0.0, 0.0, 1.0)) * 44.0 + 0.5);
            if(configured < 28L)
                configured = 28L;
            else if(configured > 96L)
                configured = 96L;
            return configured;
        }

        if(t &&
           t->beat_period_ms >= (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
           t->beat_period_ms <= (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        {
            double period = t->beat_period_ms;
            double frac;

            if(hit_kind == AB_HIT_HAT)
                frac = 0.105;
            else if(hit_kind == AB_HIT_SNARE)
                frac = 0.215;
            else if(hit_kind == AB_HIT_KICK || hit_kind == AB_HIT_FULL)
                frac = 0.245;
            else
                frac = 0.230;

            if(hit_confidence > 0.72)
                frac -= 0.030;
            else if(hit_confidence < 0.36)
                frac += 0.045;

            musical = (long)(period * frac + 0.5);

            if(hit_kind == AB_HIT_HAT)
            {
                if(musical < 24L)
                    musical = 24L;
                else if(musical > 115L)
                    musical = 115L;
            }
            else if(period <= 360.0)
            {
                if(musical < 42L)
                    musical = 42L;
                else if(musical > 165L)
                    musical = 165L;
            }
            else if(period <= 760.0)
            {
                if(musical < 52L)
                    musical = 52L;
                else if(musical > 230L)
                    musical = 230L;
            }
            else
            {
                if(musical < 72L)
                    musical = 72L;
                else if(musical > 360L)
                    musical = 360L;
            }

            configured = (long)((double)musical * 0.72 + (double)user_bias * 0.28 + 0.5);
        }
        else
        {
            configured = (long)((double)configured * 0.56 + 0.5);
        }

        if(hit_kind == AB_HIT_HAT)
        {
            if(configured < 24L)
                configured = 24L;
            else if(configured > 130L)
                configured = 130L;
        }
        else
        {
            if(configured < 40L)
                configured = 40L;
            else if(configured > 390L)
                configured = 390L;
        }

        return configured;
    }

    if(t &&
       t->beat_period_ms >= (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
       t->beat_period_ms <= (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
    {
        double period = t->beat_period_ms;
        double factor;
        long tempo_guard;

        if(period < 360.0)
            factor = 0.52;
        else if(period > 760.0)
            factor = 0.64;
        else
            factor = 0.58;

        tempo_guard = (long)(period * factor);

        if(tempo_guard < 190L)
            tempo_guard = 190L;
        else if(tempo_guard > 1250L)
            tempo_guard = 1250L;

        if(configured < tempo_guard)
            configured = tempo_guard;
    }

    return configured;
}

static const char *ab_update_dynamic_bpm(vj_audio_beat_shared_t *s,
                                         vj_audio_beat_thread_t *t,
                                         long interval,
                                         int hit_kind,
                                         double hit_confidence,
                                         int *advance_anchor)
{
    const char *reason = "ignore";
    double period;
    double interval_d;
    int body_hit;

    if(advance_anchor)
        *advance_anchor = 0;

    if(!s || !t)
        return reason;

    if(ab_is_scratch_hit(hit_kind))
        return reason;

    if(interval < VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS ||
       interval > VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        return reason;

    period = t->beat_period_ms;
    interval_d = (double)interval;
    body_hit = ab_is_body_hit(hit_kind);

    if(hit_confidence < 0.0)
        hit_confidence = 0.0;
    else if(hit_confidence > 1.0)
        hit_confidence = 1.0;

    if(period > 1.0)
    {
        if(hit_kind == AB_HIT_HAT && interval_d < period * 0.95)
        {
            t->last_interval_ms = interval;
            return "hat-hold";
        }

        if(!body_hit && hit_confidence < 0.18 && interval_d < period * 0.75)
        {
            t->last_interval_ms = interval;
            return "weak-subdivision-hold";
        }
    }

    if(advance_anchor)
        *advance_anchor = 1;

    if(period <= 1.0)
    {
        t->beat_period_ms = interval_d;
        t->last_interval_ms = interval;
        t->tempo_fast_count = 0;
        t->tempo_slow_count = 0;
        reason = "init";
    }
    else if(interval_d < period * 0.68)
    {
        t->tempo_fast_count++;
        t->tempo_slow_count = 0;

        if(body_hit &&
           hit_confidence >= 0.30 &&
           interval_d >= period * 0.40)
        {
            double alpha = t->tempo_fast_count >= 2 ? 0.46 : 0.34;

            t->beat_period_ms = (period * (1.0 - alpha)) + (interval_d * alpha);
            reason = t->tempo_fast_count >= 2 ? "double-time-shift" : "double-time-track";
        }
        else if(t->tempo_fast_count >= 3)
        {
            t->beat_period_ms = (period * 0.55) + (interval_d * 0.45);
            reason = "fast-shift";
        }
        else
        {
            reason = "subdivision-hold";
        }

        t->last_interval_ms = interval;
    }
    else if(interval_d > period * 1.55)
    {
        t->tempo_slow_count++;
        t->tempo_fast_count = 0;

        if(body_hit &&
           hit_confidence >= 0.28 &&
           interval_d <= period * 2.65)
        {
            double alpha = t->tempo_slow_count >= 2 ? 0.44 : 0.26;

            t->beat_period_ms = (period * (1.0 - alpha)) + (interval_d * alpha);
            reason = t->tempo_slow_count >= 2 ? "half-time-shift" : "half-time-track";
        }
        else if(t->tempo_slow_count >= 2)
        {
            t->beat_period_ms = (period * 0.70) + (interval_d * 0.30);
            reason = "slow-shift";
        }
        else
        {
            reason = "gap-hold";
        }

        t->last_interval_ms = interval;
    }
    else
    {
        double diff = fabs((interval_d - period) / period);
        double alpha = diff > 0.25 ? 0.30 : 0.18;

        if(body_hit)
            alpha += 0.04 * hit_confidence;
        else if(hit_kind == AB_HIT_HAT)
            alpha *= 0.55;

        if(alpha < 0.05)
            alpha = 0.05;
        else if(alpha > 0.45)
            alpha = 0.45;

        t->beat_period_ms = (period * (1.0 - alpha)) + (interval_d * alpha);
        t->last_interval_ms = interval;
        t->tempo_fast_count = 0;
        t->tempo_slow_count = 0;
        reason = "track";
    }

    t->beat_period_ms = ab_bound_period_ms(t->beat_period_ms);

    ab_publish_i_cached(&s->bpm_q8,
                        &t->pub_bpm_q8,
                        (int)((60000.0 / t->beat_period_ms) * 256.0 + 0.5));

    return reason;
}

static inline int ab_valid_period_ms(double period)
{
    return period >= (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
           period <= (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS;
}

static inline double ab_breakbeat_body_period_ms(const vj_audio_beat_thread_t *t)
{
    if(!t)
        return 0.0;

    if(ab_valid_period_ms(t->body_period_ms))
        return t->body_period_ms;

    return 0.0;
}

static int ab_breakbeat_body_candidate_keep(vj_audio_beat_thread_t *t,
                                            int hit_kind,
                                            long hit_ms,
                                            long since_last_ms,
                                            long cooldown_ms,
                                            double hit_score,
                                            double hit_level,
                                            const char **reason)
{
    double period;
    double previous_score;
    double previous_level;
    double score_ratio;
    double level_ratio;
    double transient;
    double flux;
    double freshness;
    double guard_ms;
    long since_body_ms;
    int same_body_kind;

    if(reason)
        *reason = "keep";

    if(!t || !ab_is_body_hit(hit_kind))
        return 1;

    if(t->body_last_hit_ms <= 0 || hit_ms <= t->body_last_hit_ms)
        return 1;

    previous_score = t->body_last_score > 0.000001 ? t->body_last_score : t->last_accept_score;
    previous_level = t->body_last_level > 0.000001 ? t->body_last_level : t->last_accept_level;

    if(previous_score <= 0.000001 || previous_level <= 0.000001)
        return 1;

    since_body_ms = hit_ms - t->body_last_hit_ms;
    if(since_body_ms < 0)
        since_body_ms = 0;

    period = ab_breakbeat_body_period_ms(t);

    if(period > 1.0)
    {
        double score_protection;

        guard_ms = period * (double)ab_breakbeat_policy.body_tail_guard_beats;

        if(t->body_last_interval_ms > 0 &&
           t->body_last_interval_ms < guard_ms)
            guard_ms = (guard_ms + (double)t->body_last_interval_ms) * 0.5;

        score_protection = previous_score > (1.0 / (double)ab_breakbeat_policy.body_tail_reaccent_score_ratio)
            ? (double)ab_breakbeat_policy.body_tail_strong_extra_beats
            : 0.0;
        guard_ms += period * score_protection;

        if(guard_ms < (double)cooldown_ms)
            guard_ms = (double)cooldown_ms;
    }
    else
    {
        guard_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_tail_unknown_cooldown_mul;
    }

    (void)since_last_ms;

    score_ratio = hit_score / (previous_score + 0.000001);
    level_ratio = hit_level / (previous_level + 0.000001);
    transient = ab_from_q15(t->pub_transient_norm_q15 == INT_MIN ? 0 : t->pub_transient_norm_q15);
    flux = ab_from_q15(t->pub_flux_q15 == INT_MIN ? 0 : t->pub_flux_q15);
    freshness = (transient * 0.58) + (flux * 0.42);
    same_body_kind = hit_kind == t->body_last_kind;

    if(period <= 1.0)
    {
        double cold_probe_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_cold_probe_cooldown_mul;
        double cold_cluster_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_cold_cluster_cooldown_mul;
        double cold_seed_min_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_cold_seed_min_cooldown_mul;
        double cold_first_seed_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_cold_first_seed_cooldown_mul;

        if(cold_probe_ms < guard_ms)
            cold_probe_ms = guard_ms;
        if(cold_cluster_ms < cold_probe_ms)
            cold_cluster_ms = cold_probe_ms;
        if(cold_seed_min_ms < cold_cluster_ms)
            cold_seed_min_ms = cold_cluster_ms;
        if(cold_first_seed_ms < cold_seed_min_ms)
            cold_first_seed_ms = cold_seed_min_ms;

        t->body_cold_dbg_gap_ms = 0;
        t->body_cold_dbg_min_ms = (long)(cold_seed_min_ms + 0.5);
        t->body_cold_dbg_first_ms = (long)(cold_first_seed_ms + 0.5);

        if((double)since_body_ms <= cold_probe_ms)
        {
            if(reason)
                *reason = same_body_kind ? "body-cold-sub" : "body-cold-cross";
            return 0;
        }

        if(t->body_cold_probe_ms <= 0 &&
           (double)since_body_ms < cold_seed_min_ms)
        {
            if(reason)
                *reason = "body-cold-short";
            return 0;
        }

        if(t->body_cold_probe_ms > 0 && hit_ms > t->body_cold_probe_ms)
        {
            long cold_gap_ms = hit_ms - t->body_cold_probe_ms;

            t->body_cold_dbg_gap_ms = cold_gap_ms;

            if((double)cold_gap_ms <= cold_cluster_ms)
            {
                if(hit_score > t->body_cold_probe_score ||
                   hit_level > t->body_cold_probe_level)
                {
                    t->body_cold_probe_ms = hit_ms;
                    t->body_cold_probe_score = hit_score;
                    t->body_cold_probe_level = hit_level;
                    t->body_cold_probe_kind = hit_kind;
                }

                if(reason)
                    *reason = "body-cold-probe";
                return 0;
            }

            if(cold_gap_ms >= VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
               cold_gap_ms <= VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
            {
                if((double)cold_gap_ms >= cold_seed_min_ms)
                {
                    if((double)cold_gap_ms >= cold_first_seed_ms)
                    {
                        t->body_period_ms = (double)cold_gap_ms;
                        t->body_last_hit_ms = t->body_cold_probe_ms;
                        t->body_last_interval_ms = cold_gap_ms;
                        t->body_last_score = t->body_cold_probe_score;
                        t->body_last_level = t->body_cold_probe_level;
                        t->body_last_kind = t->body_cold_probe_kind;
                        t->body_slow_count = 0;
                        t->body_seed_count = 1;
                        t->body_cold_probe_ms = 0;
                        t->body_cold_gap_ms = 0;
                        t->body_cold_probe_score = 0.0;
                        t->body_cold_probe_level = 0.0;
                        t->body_cold_probe_kind = AB_HIT_NONE;

                        if(reason)
                            *reason = "body-cold-seed-probe";
                        return 1;
                    }

                    if(t->body_cold_gap_ms >= VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
                       t->body_cold_gap_ms <= VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS &&
                       (double)t->body_cold_gap_ms >= cold_seed_min_ms)
                    {
                        double prev_gap = (double)t->body_cold_gap_ms;
                        double gap = (double)cold_gap_ms;
                        double rel = fabs(gap - prev_gap) / (((gap + prev_gap) * 0.5) + 0.000001);

                        if(rel <= (double)ab_breakbeat_policy.body_cold_gap_match_rel ||
                           gap >= prev_gap * (double)ab_breakbeat_policy.body_cold_gap_expand_seed_mul)
                        {
                            t->body_period_ms = rel <= (double)ab_breakbeat_policy.body_cold_gap_match_rel
                                ? (prev_gap + gap) * 0.5
                                : gap;
                            t->body_last_hit_ms = t->body_cold_probe_ms;
                            t->body_last_interval_ms = cold_gap_ms;
                            t->body_last_score = t->body_cold_probe_score;
                            t->body_last_level = t->body_cold_probe_level;
                            t->body_last_kind = t->body_cold_probe_kind;
                            t->body_slow_count = 0;
                            t->body_seed_count = 1;
                            t->body_cold_probe_ms = 0;
                            t->body_cold_gap_ms = 0;
                            t->body_cold_probe_score = 0.0;
                            t->body_cold_probe_level = 0.0;
                            t->body_cold_probe_kind = AB_HIT_NONE;

                            if(reason)
                                *reason = rel <= (double)ab_breakbeat_policy.body_cold_gap_match_rel
                                    ? "body-cold-seed"
                                    : "body-cold-seed-wide";
                            return 1;
                        }
                    }

                    t->body_cold_gap_ms = cold_gap_ms;
                }
                else
                {
                    if(hit_score > t->body_cold_probe_score ||
                       hit_level > t->body_cold_probe_level)
                    {
                        t->body_cold_probe_ms = hit_ms;
                        t->body_cold_probe_score = hit_score;
                        t->body_cold_probe_level = hit_level;
                        t->body_cold_probe_kind = hit_kind;
                    }

                    if(reason)
                        *reason = "body-cold-short";
                    return 0;
                }
            }
        }

        if(t->body_cold_probe_ms <= 0 &&
           (double)since_body_ms >= cold_first_seed_ms)
        {
            t->body_period_ms = (double)since_body_ms;
            t->body_slow_count = 0;
            t->body_seed_count = 1;

            if(reason)
                *reason = "body-cold-seed-first";
            return 1;
        }

        t->body_cold_probe_ms = hit_ms;
        t->body_cold_probe_score = hit_score;
        t->body_cold_probe_level = hit_level;
        t->body_cold_probe_kind = hit_kind;

        if(reason)
            *reason = "body-cold-probe";
        return 0;
    }

    if((double)since_body_ms > guard_ms)
    {
        double subdivision_guard_ms = period > 1.0
            ? period * (double)ab_breakbeat_policy.body_subdivision_guard_beats
            : 0.0;

        if(subdivision_guard_ms <= 0.0 || (double)since_body_ms > subdivision_guard_ms)
            return 1;
    }

    if(period > 1.0 &&
       t->body_last_accent_ms > t->body_last_hit_ms &&
       hit_ms > t->body_last_accent_ms)
    {
        double accent_tail_guard_ms = period * (double)ab_breakbeat_policy.body_subdivision_accent_lock_beats;

        if(accent_tail_guard_ms < (double)cooldown_ms)
            accent_tail_guard_ms = (double)cooldown_ms;

        if((double)(hit_ms - t->body_last_accent_ms) <= accent_tail_guard_ms)
        {
            if(reason)
                *reason = "body-accent-tail";
            return 0;
        }
    }

    if(period > 1.0 &&
       (double)since_body_ms <= period * (double)ab_breakbeat_policy.body_subdivision_guard_beats)
    {
        double accent_min_ms = period * (double)ab_breakbeat_policy.body_subdivision_accent_min_beats;
        double accent_lock_ms = period * (double)ab_breakbeat_policy.body_subdivision_accent_lock_beats;
        int accent_locked = 0;

        if(t->body_last_accent_ms > t->body_last_hit_ms &&
           hit_ms > t->body_last_accent_ms &&
           (double)(hit_ms - t->body_last_accent_ms) <= accent_lock_ms)
            accent_locked = 1;

        if(!accent_locked &&
           (double)since_body_ms >= accent_min_ms &&
           score_ratio >= (double)ab_breakbeat_policy.body_subdivision_score_ratio &&
           level_ratio >= (double)ab_breakbeat_policy.body_subdivision_level_ratio &&
           freshness >= (double)ab_breakbeat_policy.body_subdivision_freshness)
        {
            if(reason)
                *reason = "body-accent";
            return 1;
        }

        if(reason)
            *reason = accent_locked ? "body-accent-tail" : "body-sub";
        return 0;
    }

    if(same_body_kind && (double)since_body_ms <= guard_ms)
    {
        if(score_ratio >= (double)ab_breakbeat_policy.body_tail_reaccent_score_ratio &&
           level_ratio >= (double)ab_breakbeat_policy.body_tail_reaccent_level_ratio &&
           freshness >= (double)ab_breakbeat_policy.body_tail_reaccent_freshness)
        {
            if(reason)
                *reason = "body-reaccent";
            return 1;
        }

        if(reason)
            *reason = "body-tail";
        return 0;
    }

    if((double)since_body_ms <= guard_ms * (double)ab_breakbeat_policy.body_early_guard_mul)
    {
        if(score_ratio >= (double)ab_breakbeat_policy.body_early_score_ratio &&
           level_ratio >= (double)ab_breakbeat_policy.body_early_level_ratio &&
           freshness >= (double)ab_breakbeat_policy.body_early_freshness)
        {
            if(reason)
                *reason = "body-early-fresh";
            return 1;
        }

        if(reason)
            *reason = "body-early";
        return 0;
    }

    if(score_ratio < (double)ab_breakbeat_policy.body_decay_score_ratio &&
       level_ratio < (double)ab_breakbeat_policy.body_decay_level_ratio &&
       freshness < (double)ab_breakbeat_policy.body_decay_freshness)
    {
        if(reason)
            *reason = "body-decay";
        return 0;
    }

    return 1;
}


static ab_body_decision_t ab_breakbeat_body_candidate_decide(vj_audio_beat_thread_t *t,
                                                             int hit_kind,
                                                             long hit_ms,
                                                             long since_last_ms,
                                                             long cooldown_ms,
                                                             double hit_score,
                                                             double hit_level)
{
    const char *reason = NULL;
    int keep;

    keep = ab_breakbeat_body_candidate_keep(t,
                                            hit_kind,
                                            hit_ms,
                                            since_last_ms,
                                            cooldown_ms,
                                            hit_score,
                                            hit_level,
                                            &reason);

    return ab_breakbeat_body_decision_make(keep, reason);
}

static void ab_breakbeat_cold_meter_note(vj_audio_beat_thread_t *t,
                                         long hit_ms,
                                         long cooldown_ms)
{
    long interval;
    double min_ms;

    min_ms = (double)cooldown_ms * (double)ab_breakbeat_policy.body_cold_first_seed_cooldown_mul;
    if(min_ms < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS)
        min_ms = (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS;

    t->body_cold_meter_dbg_interval_ms = 0;

    if(t->body_cold_meter_last_ms > 0 && hit_ms > t->body_cold_meter_last_ms)
    {
        interval = hit_ms - t->body_cold_meter_last_ms;
        t->body_cold_meter_dbg_interval_ms = interval;

        if((double)interval >= min_ms &&
           interval >= VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS &&
           interval <= VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        {
            if(ab_valid_period_ms(t->body_cold_meter_period_ms))
            {
                double period = t->body_cold_meter_period_ms;
                double id = (double)interval;
                double rel = fabs(id - period) / (((id + period) * 0.5) + 0.000001);

                if(rel <= (double)ab_breakbeat_policy.body_cold_gap_match_rel)
                {
                    t->body_cold_meter_period_ms = (period * 0.65) + (id * 0.35);
                    if(t->body_cold_meter_count < 32767)
                        t->body_cold_meter_count++;
                }
                else if(t->body_cold_meter_count < 2)
                {
                    t->body_cold_meter_period_ms = id;
                    t->body_cold_meter_count = 1;
                }
            }
            else
            {
                t->body_cold_meter_period_ms = (double)interval;
                t->body_cold_meter_count = 1;
            }
        }
    }

    t->body_cold_meter_last_ms = hit_ms;
}


typedef enum
{
    AB_BODY_PERIOD_UPDATE_NONE = 0,
    AB_BODY_PERIOD_UPDATE_SEED,
    AB_BODY_PERIOD_UPDATE_TRACK,
    AB_BODY_PERIOD_UPDATE_HOLD_DOUBLE,
    AB_BODY_PERIOD_UPDATE_FULL_SHIFT,
    AB_BODY_PERIOD_UPDATE_FOLD,
    AB_BODY_PERIOD_UPDATE_SLOW_SHIFT,
    AB_BODY_PERIOD_UPDATE_IGNORE
} ab_body_period_update_kind_t;

typedef struct
{
    int valid_interval;
    int changed;
    int publish;
    ab_body_period_update_kind_t kind;
    long interval_ms;
    double period_before;
    double period_after;
} ab_body_period_update_t;

static inline void ab_breakbeat_publish_body_period(vj_audio_beat_shared_t *s,
                                                    vj_audio_beat_thread_t *t)
{
    if(!ab_action_is_breakbeat(ab_load_i(&s->action_mode)) ||
       !ab_valid_period_ms(t->body_period_ms))
        return;

    t->beat_period_ms = t->body_period_ms;
    ab_publish_i_cached(&s->bpm_q8,
                        &t->pub_bpm_q8,
                        (int)((60000.0 / t->beat_period_ms) * 256.0 + 0.5));
}

static inline void ab_breakbeat_clear_body_cold_state(vj_audio_beat_thread_t *t)
{
    t->body_cold_probe_ms = 0;
    t->body_cold_gap_ms = 0;
    t->body_cold_dbg_gap_ms = 0;
    t->body_cold_dbg_min_ms = 0;
    t->body_cold_dbg_first_ms = 0;
    t->body_cold_probe_score = 0.0;
    t->body_cold_probe_level = 0.0;
    t->body_cold_probe_kind = AB_HIT_NONE;
    t->body_cold_meter_last_ms = 0;
    t->body_cold_meter_dbg_interval_ms = 0;
    t->body_cold_meter_period_ms = 0.0;
    t->body_cold_meter_count = 0;
}

static int ab_breakbeat_seed_body_period_from_cold_meter(vj_audio_beat_thread_t *t)
{
    if(ab_valid_period_ms(t->body_period_ms) ||
       t->body_cold_meter_count < 2 ||
       !ab_valid_period_ms(t->body_cold_meter_period_ms))
        return 0;

    t->body_period_ms = ab_bound_period_ms(t->body_cold_meter_period_ms);
    t->body_last_interval_ms = (long)(t->body_period_ms + 0.5);
    t->body_slow_count = 0;
    t->body_seed_count = 1;

    return 1;
}

static ab_body_period_update_t ab_breakbeat_update_body_period(vj_audio_beat_thread_t *t,
                                                               long interval,
                                                               double hit_score)
{
    ab_body_period_update_t u;
    double interval_d;
    double period;

    memset(&u, 0, sizeof(u));
    u.kind = AB_BODY_PERIOD_UPDATE_NONE;
    u.interval_ms = interval;
    u.period_before = t->body_period_ms;
    u.period_after = t->body_period_ms;

    if(interval < VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS ||
       interval > VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        return u;

    u.valid_interval = 1;

    interval_d = (double)interval;
    period = t->body_period_ms;

    if(!ab_valid_period_ms(period))
    {
        t->body_period_ms = interval_d;
        t->body_slow_count = 0;
        t->body_seed_count = 0;
        u.changed = 1;
        u.kind = AB_BODY_PERIOD_UPDATE_SEED;
    }
    else if(interval_d >= period * (double)ab_breakbeat_policy.body_period_min_track_mul &&
            interval_d <= period * (double)ab_breakbeat_policy.body_period_max_track_mul)
    {
        double diff = fabs((interval_d - period) / period);
        double alpha = diff > 0.22 ? 0.30 : 0.16;

        if(hit_score > t->body_last_score)
            alpha += (hit_score - t->body_last_score) * 0.12;

        if(alpha < 0.08)
            alpha = 0.08;
        else if(alpha > 0.38)
            alpha = 0.38;

        if(t->body_seed_count > 0 &&
           t->body_seed_count < ab_breakbeat_policy.body_warmup_accepts &&
           alpha < (double)ab_breakbeat_policy.body_warmup_alpha)
            alpha = (double)ab_breakbeat_policy.body_warmup_alpha;

        t->body_period_ms = (period * (1.0 - alpha)) + (interval_d * alpha);
        t->body_slow_count = 0;
        u.changed = 1;
        u.kind = AB_BODY_PERIOD_UPDATE_TRACK;
    }
    else if(interval_d > period * (double)ab_breakbeat_policy.body_period_slow_shift_mul)
    {
        double folded = interval_d;
        int multiple = (int)floor((interval_d / period) + 0.5);

        if(multiple > 1)
            folded = interval_d / (double)multiple;

        if(multiple == 2 &&
           interval_d >= period * (double)ab_breakbeat_policy.body_period_double_min_mul &&
           interval_d <= period * (double)ab_breakbeat_policy.body_period_double_max_mul)
        {
            t->body_slow_count++;
            u.kind = AB_BODY_PERIOD_UPDATE_HOLD_DOUBLE;

            if(t->body_slow_count >= ab_breakbeat_policy.body_slow_shift_accepts)
            {
                double alpha = (double)ab_breakbeat_policy.body_period_full_shift_alpha;

                t->body_period_ms = (period * (1.0 - alpha)) + (interval_d * alpha);
                t->body_slow_count = 0;
                u.changed = 1;
                u.kind = AB_BODY_PERIOD_UPDATE_FULL_SHIFT;
            }
        }
        else if(folded >= period * (double)ab_breakbeat_policy.body_period_min_track_mul &&
                folded <= period * (double)ab_breakbeat_policy.body_period_max_track_mul)
        {
            double diff = fabs((folded - period) / period);
            double alpha = diff > 0.22 ? 0.24 : 0.12;

            t->body_period_ms = (period * (1.0 - alpha)) + (folded * alpha);
            t->body_slow_count = 0;
            u.changed = 1;
            u.kind = AB_BODY_PERIOD_UPDATE_FOLD;
        }
        else
        {
            t->body_slow_count++;
            u.kind = AB_BODY_PERIOD_UPDATE_IGNORE;

            if(t->body_slow_count >= ab_breakbeat_policy.body_slow_shift_accepts)
            {
                t->body_period_ms = (period * 0.72) + (interval_d * 0.28);
                t->body_slow_count = 0;
                u.changed = 1;
                u.kind = AB_BODY_PERIOD_UPDATE_SLOW_SHIFT;
            }
        }
    }
    else
    {
        t->body_slow_count = 0;
        u.kind = AB_BODY_PERIOD_UPDATE_IGNORE;
    }

    t->body_period_ms = ab_bound_period_ms(t->body_period_ms);
    t->body_last_interval_ms = interval;

    if(t->body_seed_count < 32767)
        t->body_seed_count++;

    u.period_after = t->body_period_ms;
    u.publish = ab_valid_period_ms(t->body_period_ms);

    return u;
}

static void ab_breakbeat_note_accepted_body(vj_audio_beat_shared_t *s,
                                            vj_audio_beat_thread_t *t,
                                            long hit_ms,
                                            int hit_kind,
                                            double hit_score,
                                            double hit_level)
{
    ab_body_period_update_t period_update;

    if(!s || !t || !ab_is_body_hit(hit_kind) || hit_ms <= 0)
        return;

    if(ab_breakbeat_seed_body_period_from_cold_meter(t))
        ab_breakbeat_publish_body_period(s, t);

    if(t->body_last_hit_ms > 0)
    {
        period_update = ab_breakbeat_update_body_period(t,
                                                        hit_ms - t->body_last_hit_ms,
                                                        hit_score);

        if(period_update.publish)
            ab_breakbeat_publish_body_period(s, t);
    }

    if(ab_valid_period_ms(t->body_period_ms))
        ab_breakbeat_clear_body_cold_state(t);

    t->body_last_hit_ms = hit_ms;
    t->body_last_score = hit_score;
    t->body_last_level = hit_level;
    t->body_last_kind = hit_kind;
}

#if VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE
static const char *ab_scratch_reject_name(int reject)
{
    switch(reject)
    {
        case AB_SCRATCH_REJECT_NONE: return "none";
        case AB_SCRATCH_REJECT_RAW: return "raw";
        case AB_SCRATCH_REJECT_DOMINANCE: return "dom";
        case AB_SCRATCH_REJECT_COLD: return "cold";
        case AB_SCRATCH_REJECT_TAIL: return "tail";
        case AB_SCRATCH_REJECT_CYCLE: return "cycle";
        default: return "?";
    }
}

static void ab_breakbeat_trace_detector(vj_audio_beat_shared_t *s,
                                        const vj_audio_beat_thread_t *t,
                                        const char *path,
                                        int hit_seq,
                                        long hit_ms,
                                        long publish_ms,
                                        long block_ms,
                                        long cooldown_ms,
                                        long since_last_ms,
                                        double score)
{
    double bpm = 0.0;
    double body_bpm = 0.0;
    double level = 0.0;
    long body_since_ms = 0;
    long cold_probe_age_ms = 0;

    if(!s || !ab_action_is_breakbeat(ab_load_i(&s->action_mode)))
        return;

    if(t && t->beat_period_ms > 1.0)
        bpm = 60000.0 / t->beat_period_ms;

    if(t)
    {
        if(t->body_period_ms > 1.0)
            body_bpm = 60000.0 / t->body_period_ms;

        if(t->body_last_hit_ms > 0 && hit_ms >= t->body_last_hit_ms)
            body_since_ms = hit_ms - t->body_last_hit_ms;

        if(t->body_cold_probe_ms > 0 && hit_ms >= t->body_cold_probe_ms)
            cold_probe_age_ms = hit_ms - t->body_cold_probe_ms;

        level = ab_from_q15(t->pub_level_q15 == INT_MIN ? 0 : t->pub_level_q15);
    }

    AB_BREAK_TRACE(
        "detector path=%s seq=%d kind=%s hit=%ld pub=%ld age=%ld block=%ld cooldown=%ld since=%ld body_since=%ld bpm=%.1f body_bpm=%.1f score=%.3f lvl=%.3f prev=%.3f prevlvl=%.3f cold_age=%ld cold_prev=%ld cold_gap=%ld cold_min=%ld cold_first=%ld cold_meter=%.0f cold_mcnt=%d cold_mint=%ld cold_kind=%s seed=%d k=%.3f sn=%.3f hat=%.3f scr=%.3f scrv=%.3f scrb=%.3f scrraw=%d scrdom=%d scrcand=%d srej=%s dir=%d",
        path ? path : "hit",
        hit_seq,
        ab_hit_kind_name(t ? t->last_hit_kind : AB_HIT_NONE),
        hit_ms,
        publish_ms,
        publish_ms >= hit_ms ? publish_ms - hit_ms : 0L,
        block_ms,
        cooldown_ms,
        since_last_ms,
        body_since_ms,
        bpm,
        body_bpm,
        score,
        level,
        t ? t->last_accept_score : 0.0,
        t ? t->last_accept_level : 0.0,
        cold_probe_age_ms,
        t ? t->body_cold_gap_ms : 0L,
        t ? t->body_cold_dbg_gap_ms : 0L,
        t ? t->body_cold_dbg_min_ms : 0L,
        t ? t->body_cold_dbg_first_ms : 0L,
        t ? t->body_cold_meter_period_ms : 0.0,
        t ? t->body_cold_meter_count : 0,
        t ? t->body_cold_meter_dbg_interval_ms : 0L,
        ab_hit_kind_name(t ? t->body_cold_probe_kind : AB_HIT_NONE),
        t ? t->body_seed_count : 0,
        t ? t->last_kick_score : 0.0,
        t ? t->last_snare_score : 0.0,
        t ? t->last_hat_score : 0.0,
        t ? t->last_scratch_score : 0.0,
        ab_from_q15(ab_load_i(&ab_scratch_velocity_q15)),
        ab_from_q15(ab_load_i(&ab_scratch_burst_q15)),
        t ? t->last_scratch_candidate_raw : 0,
        t ? t->last_scratch_dominant : 0,
        t ? t->last_scratch_candidate : 0,
        ab_scratch_reject_name(t ? t->last_scratch_reject : AB_SCRATCH_REJECT_RAW),
        ab_load_i(&ab_scratch_dir) < 0 ? -1 : 1);
}
#else
#define ab_breakbeat_trace_detector(s, t, path, hit_seq, hit_ms, publish_ms, block_ms, cooldown_ms, since_last_ms, score) do { } while(0)
#endif


typedef struct
{
    long cooldown_ms;
    long block_ms;
    long hit_ms;
    long since_last_ms;
    long body_since_ms;
    long candidate_since_ms;
    int hit_kind;
    int scratch_hit;
    int breakbeat_mode;
    int body_hit;
    int body_accent_hit;
    double hit_confidence;
    double hit_level;
    ab_body_decision_t body_gate;
    const char *accept_path;
} ab_breakbeat_detector_decision_t;

static ab_breakbeat_detector_decision_t ab_breakbeat_detector_decision_prepare(vj_audio_beat_shared_t *s,
                                                                               vj_audio_beat_thread_t *t,
                                                                               int got_bytes,
                                                                               long now_ms)
{
    ab_breakbeat_detector_decision_t d;

    memset(&d, 0, sizeof(d));

    d.cooldown_ms = ab_effective_cooldown_ms(s, t);
    d.block_ms = 0;
    d.hit_ms = now_ms;

    if(t->bytes_per_frame > 0 && t->sample_rate > 0 && got_bytes > 0)
    {
        long frames = (long)(got_bytes / t->bytes_per_frame);
        d.block_ms = (frames * 1000L + (long)t->sample_rate / 2L) / (long)t->sample_rate;
        if(d.block_ms > 0)
            d.hit_ms = now_ms - (d.block_ms / 2L);
    }

    if(d.hit_ms <= 0)
        d.hit_ms = now_ms;
    if(t->last_hit_ms > 0 && d.hit_ms <= t->last_hit_ms)
        d.hit_ms = t->last_hit_ms + 1L;
    if(d.hit_ms > now_ms)
        d.hit_ms = now_ms;

    if(d.cooldown_ms < 1)
        d.cooldown_ms = 1;

    d.since_last_ms = t->last_hit_ms > 0 ? d.hit_ms - t->last_hit_ms : 2147483647L;
    if(d.since_last_ms < 0)
        d.since_last_ms = 0;

    d.hit_kind = t->last_hit_kind;
    d.scratch_hit = d.hit_kind == AB_HIT_SCRATCH;
    d.breakbeat_mode = ab_action_is_breakbeat(ab_load_i(&s->action_mode));
    d.body_hit = ab_is_body_hit(d.hit_kind);
    d.body_since_ms = (t->body_last_hit_ms > 0 && d.hit_ms >= t->body_last_hit_ms) ? d.hit_ms - t->body_last_hit_ms : 2147483647L;
    d.candidate_since_ms = d.body_hit && d.breakbeat_mode ? d.body_since_ms : d.since_last_ms;
    d.hit_confidence = d.scratch_hit ? t->last_scratch_score : t->last_kick_score;
    d.hit_level = ab_from_q15(t->pub_level_q15 == INT_MIN ? 0 : t->pub_level_q15);
    d.body_gate = ab_breakbeat_body_decision_make(1, NULL);

    if(!d.scratch_hit)
    {
        if(t->last_snare_score > d.hit_confidence)
            d.hit_confidence = t->last_snare_score;

        if(t->last_hat_score > d.hit_confidence)
            d.hit_confidence = t->last_hat_score;
    }

    if(d.breakbeat_mode && d.body_hit)
    {
        d.body_gate = ab_breakbeat_body_candidate_decide(t,
                                                         d.hit_kind,
                                                         d.hit_ms,
                                                         d.since_last_ms,
                                                         d.cooldown_ms,
                                                         d.hit_confidence,
                                                         d.hit_level);
    }

    d.body_accent_hit = d.breakbeat_mode && d.body_hit && d.body_gate.accent;
    d.accept_path = d.body_gate.accept_path ? d.body_gate.reason : "accept";

    return d;
}


static int ab_breakbeat_detector_promote_scratch_gesture(vj_audio_beat_thread_t *t,
                                                         ab_breakbeat_detector_decision_t *d)
{
    float scratch_amount;
    float scratch_velocity;
    float scratch_burst;
    float percussive_max;
    float gate_mul;
    float dom_mul;
    int scratch_dir;
    int protected_reject;
    int raw_motion_reject;
    int usable_reject;
    int legacy_gate;
    int edge_gate;
    int turn_gate;
    int strong_motion;
    int decisive_motion;
    int direction_changed;
    long gesture_since_ms;

    if(!t || !d || !d->breakbeat_mode || d->scratch_hit)
        return 0;

    scratch_amount = (float)t->last_scratch_score;
    scratch_velocity = ab_from_q15(ab_load_i(&ab_scratch_velocity_q15));
    scratch_burst = ab_from_q15(ab_load_i(&ab_scratch_burst_q15));
    scratch_dir = ab_load_i(&ab_scratch_dir) < 0 ? -1 : 1;

    percussive_max = (float)t->last_kick_score;
    if((float)t->last_snare_score > percussive_max)
        percussive_max = (float)t->last_snare_score;
    if((float)t->last_hat_score > percussive_max)
        percussive_max = (float)t->last_hat_score;

    gate_mul = (float)ab_scratch_gate_mul();
    dom_mul = (float)ab_scratch_dom_mul();

    protected_reject =
        t->last_scratch_reject == AB_SCRATCH_REJECT_TAIL ||
        t->last_scratch_reject == AB_SCRATCH_REJECT_CYCLE ||
        t->last_scratch_reject == AB_SCRATCH_REJECT_COLD ||
        t->last_scratch_reject == AB_SCRATCH_REJECT_DOMINANCE;

    raw_motion_reject =
        t->last_scratch_reject == AB_SCRATCH_REJECT_RAW &&
        scratch_amount >= 0.72f * gate_mul &&
        scratch_velocity >= 0.72f * gate_mul &&
        scratch_burst >= 0.70f * gate_mul &&
        scratch_amount >= percussive_max * 0.84f * dom_mul;

    usable_reject = protected_reject || raw_motion_reject;

    legacy_gate =
        t->last_scratch_candidate_raw &&
        t->last_scratch_dominant &&
        (t->last_scratch_reject == AB_SCRATCH_REJECT_TAIL ||
         t->last_scratch_reject == AB_SCRATCH_REJECT_CYCLE) &&
        scratch_amount >= 0.46f * gate_mul &&
        scratch_velocity >= 0.42f * gate_mul &&
        scratch_burst >= 0.52f * gate_mul;

    strong_motion =
        scratch_amount >= 0.60f * gate_mul &&
        scratch_velocity >= 0.56f * gate_mul &&
        scratch_burst >= 0.56f * gate_mul;

    decisive_motion =
        scratch_amount >= 0.72f * gate_mul &&
        scratch_velocity >= 0.72f * gate_mul &&
        scratch_burst >= 0.70f * gate_mul;

    direction_changed =
        t->last_scratch_gesture_dir != 0 &&
        scratch_dir != t->last_scratch_gesture_dir;

    gesture_since_ms = t->last_scratch_gesture_ms > 0 ?
        d->hit_ms - t->last_scratch_gesture_ms : 2147483647L;
    if(gesture_since_ms < 0)
        gesture_since_ms = 0;

    edge_gate =
        strong_motion &&
        usable_reject &&
        (direction_changed || t->last_scratch_gesture_dir == 0) &&
        gesture_since_ms >= ab_breakbeat_policy.scratch_open_min_ms &&
        ab_breakbeat_fast_body_scratch_escape(t,
                                              scratch_amount,
                                              scratch_velocity,
                                              scratch_burst,
                                              (double)t->last_kick_score,
                                              (double)t->last_snare_score,
                                              (double)t->last_hat_score,
                                              t->last_scratch_dominant) &&
        (
            t->last_scratch_candidate_raw ||
            t->last_scratch_dominant ||
            decisive_motion ||
            scratch_amount >= percussive_max * 0.86f * dom_mul
        );

    turn_gate =
        t->last_scratch_turn_edge &&
        (direction_changed || t->last_scratch_gesture_dir == 0) &&
        gesture_since_ms >= ab_breakbeat_policy.scratch_open_min_ms &&
        scratch_amount >= 0.30f * gate_mul &&
        scratch_velocity >= 0.34f * gate_mul &&
        scratch_burst >= 0.22f * gate_mul &&
        ab_breakbeat_fast_body_scratch_escape(t,
                                              scratch_amount,
                                              scratch_velocity,
                                              scratch_burst,
                                              (double)t->last_kick_score,
                                              (double)t->last_snare_score,
                                              (double)t->last_hat_score,
                                              t->last_scratch_dominant) &&
        (
            t->last_scratch_turn_score >= 0.42 * gate_mul ||
            scratch_velocity >= 0.50f * gate_mul ||
            scratch_burst >= 0.44f * gate_mul
        ) &&
        (
            scratch_amount >= percussive_max * 0.58f * dom_mul ||
            scratch_velocity >= 0.52f * gate_mul ||
            scratch_burst >= 0.48f * gate_mul
        );

    if(!legacy_gate && !edge_gate && !turn_gate)
        return 0;

    d->hit_kind = AB_HIT_SCRATCH;
    d->scratch_hit = 1;
    d->body_hit = 0;
    d->body_accent_hit = 0;
    d->hit_confidence = scratch_amount;
    d->candidate_since_ms = d->since_last_ms;
    d->cooldown_ms = (edge_gate || turn_gate) ? ab_breakbeat_policy.scratch_open_min_ms :
                                                ab_breakbeat_policy.scratch_repeat_min_ms;
    d->body_gate = ab_breakbeat_body_decision_make(1,
        turn_gate ? "scratch-turn" : (edge_gate ? "scratch-edge" : "scratch-gesture"));
    d->accept_path = turn_gate ? "scratch-turn" : (edge_gate ? "scratch-edge" : "scratch-gesture");

    t->last_scratch_gesture_dir = scratch_dir;
    t->last_scratch_gesture_ms = d->hit_ms;

    return 1;
}

static void ab_breakbeat_detector_accept(vj_audio_beat_shared_t *s,
                                         vj_audio_beat_thread_t *t,
                                         const ab_breakbeat_detector_decision_t *d,
                                         long now_ms)
{
    long tempo_prev;
    long interval;
    int advance_tempo_anchor;
    int hit_seq;

    tempo_prev = t->tempo_last_hit_ms;
    interval = tempo_prev > 0 ? d->hit_ms - tempo_prev : 0;
    if(interval < 0)
        interval = 0;

    advance_tempo_anchor = tempo_prev <= 0 ? 1 : 0;

    if(!d->scratch_hit && (!d->breakbeat_mode || (d->body_hit && !d->body_accent_hit)))
    {
        if(tempo_prev > 0)
            (void) ab_update_dynamic_bpm(s, t, interval, d->hit_kind, d->hit_confidence, &advance_tempo_anchor);

        if(advance_tempo_anchor)
            t->tempo_last_hit_ms = d->hit_ms;
    }
    else
    {
        advance_tempo_anchor = 0;
    }

    if(d->breakbeat_mode &&
       !d->scratch_hit &&
       !ab_valid_period_ms(t->body_period_ms))
        ab_breakbeat_cold_meter_note(t, d->hit_ms, d->cooldown_ms);

    t->last_hit_ms = d->hit_ms;
    t->last_accept_score = d->hit_confidence;
    t->last_accept_level = d->hit_level;

    if(d->breakbeat_mode && d->body_hit)
    {
        if(d->body_accent_hit)
            t->body_last_accent_ms = d->hit_ms;
        else
            ab_breakbeat_note_accepted_body(s, t, d->hit_ms, d->hit_kind, d->hit_confidence, d->hit_level);
    }

    ab_store_l(&s->last_hit_ms, d->hit_ms);
    t->last_hit_kind = d->hit_kind;
    ab_store_i(&ab_last_hit_kind, d->hit_kind);
    ab_add_l(&s->hits, 1);
    hit_seq = ab_add_i(&s->hit_seq, 1);

    t->beat_toggle_state = t->beat_toggle_state > 0 ? 0 : 32767;
    ab_store_i(&s->beat_toggle_q15, t->beat_toggle_state);

    ab_breakbeat_hit_queue_push(s, t, hit_seq, d->hit_ms, now_ms, d->block_ms);
    ab_breakbeat_trace_detector(s, t, d->accept_path, hit_seq, d->hit_ms, now_ms, d->block_ms, d->cooldown_ms, d->candidate_since_ms, d->hit_confidence);
}

static void ab_breakbeat_process_detector_hit(vj_audio_beat_shared_t *s,
                                              vj_audio_beat_thread_t *t,
                                              int got_bytes,
                                              long now_ms)
{
    ab_breakbeat_detector_decision_t d;

    d = ab_breakbeat_detector_decision_prepare(s, t, got_bytes, now_ms);

    ab_breakbeat_detector_promote_scratch_gesture(t, &d);

    if(!d.body_gate.keep)
    {
        ab_breakbeat_trace_detector(s, t, d.body_gate.reason, ab_load_i(&s->hit_seq), d.hit_ms, now_ms, d.block_ms, d.cooldown_ms, d.candidate_since_ms, d.hit_confidence);
    }
    else if(d.candidate_since_ms >= d.cooldown_ms)
    {
        ab_breakbeat_detector_accept(s, t, &d, now_ms);
    }
    else
    {
        ab_breakbeat_trace_detector(s, t, "cooldown", ab_load_i(&s->hit_seq), d.hit_ms, now_ms, d.block_ms, d.cooldown_ms, d.candidate_since_ms, d.hit_confidence);
    }
}


void *vj_audio_beat_thread(void *arg)
{
    vj_audio_beat_shared_t *s = (vj_audio_beat_shared_t *)arg;
    vj_audio_beat_thread_t t;
    int first_capture_logged = 0;

    memset(&t, 0, sizeof(t));

    if(!s)
        return NULL;

    if(!ab_load_i(&s->initialized))
        vj_audio_beat_init(s, 2);

    ab_store_i(&s->running, 1);

    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO-BEAT] analysis thread started");

    while(!ab_load_i(&s->stop_request))
    {
        int reset_seq;
        long stored;
        long target_bytes;
        long min_read_bytes;
        int got;
        int hit;
        long now;

        if(!ab_load_i(&s->enabled))
        {
            ab_sleep_us(20000);
            continue;
        }

        if(!t.open)
        {
            if(s->sync)
            {
                if(!vj_audio_sync_is_enabled(s->sync))
                    vj_audio_sync_enable(s->sync);

                if(!ab_configure_from_sync(s, &t))
                {
                    ab_sleep_us(20000);
                    continue;
                }
            }
            else if(!ab_configure_from_jack(s, &t))
            {
                ab_sleep_us(250000);
                continue;
            }
        }

        reset_seq = ab_load_i(&s->reset_seq);

        if(reset_seq != t.last_reset_seq)
        {
            int req_ch = ab_load_i(&s->input_channels_request);

            if(req_ch < 1)
                req_ch = 2;
            else if(req_ch > 2)
                req_ch = 2;

            ab_thread_reset(&t);

            if(!s->sync)
                ab_record_ring_clear(s);

            ab_clear_published_control(s);
            ab_breakbeat_hit_queue_clear();

            if(s->sync)
            {
                if(ab_sync_source(s) == VJ_AUDIO_SYNC_SOURCE_PUSH)
                    ab_arm_sync_read_probe(s, &t, reset_seq);
                else
                    vj_audio_sync_reset_beat_reader(s->sync);
            }

            if(!s->sync)
                vj_jack_reset_input();

            t.last_reset_seq = reset_seq;
            first_capture_logged = 0;

            ab_store_i(&s->consumed_seq, ab_load_i(&s->hit_seq));
            ab_log_config(s, "analysis reset");

            if(t.channels > 0 && t.channels != req_ch)
            {
                t.open = 0;
                ab_store_i(&s->open, 0);
                continue;
            }
        }

        if(s->sync)
        {
            if(!vj_audio_sync_is_open(s->sync))
            {
                t.open = 0;
                ab_store_i(&s->open, 0);
                ab_sleep_us(10000);
                continue;
            }

            now = ab_now_ms();

            if(t.sync_read_arm_until_ms > 0 && now < t.sync_read_arm_until_ms)
            {
                ab_sleep_us(1000);
                continue;
            }

            t.sync_read_arm_until_ms = 0;

            {
                long read_start = now;
                int probe = t.sync_read_probe_pending;

                if(probe)
                {
                    t.sync_read_probe_pending = 0;
                    veejay_msg(VEEJAY_MSG_INFO,
                               "[AUDIO-BEAT] sync read probe enter source=%d reset_seq=%d open=%d enabled=%d buffer=%d bpf=%d",
                               t.sync_read_probe_source,
                               t.sync_read_probe_seq,
                               ab_load_i(&s->open),
                               ab_load_i(&s->enabled),
                               t.buffer_size,
                               t.bytes_per_frame);
                }

                target_bytes = ab_target_analysis_bytes(&t);
                if(target_bytes <= 0 || target_bytes > t.buffer_size)
                    target_bytes = t.buffer_size;

                target_bytes = ab_floor_to_multiple_l(target_bytes,
                                                       (long)t.bytes_per_frame);

                if(target_bytes <= 0)
                {
                    ab_sleep_us(1000);
                    continue;
                }

                got = vj_audio_sync_read_beat_audio(
                    s->sync,
                    t.buffer,
                    (int)target_bytes
                );

                now = ab_now_ms();

                if(probe)
                {
                    veejay_msg(VEEJAY_MSG_INFO,
                               "[AUDIO-BEAT] sync read probe leave got=%d elapsed=%ldms request=%ld",
                               got,
                               now - read_start,
                               target_bytes);
                }
                else if((now - read_start) >= VEEJAY_AUDIO_BEAT_SYNC_READ_SLOW_MS &&
                        (t.sync_read_last_slow_log_ms == 0 ||
                         (now - t.sync_read_last_slow_log_ms) >= 1000L))
                {
                    t.sync_read_last_slow_log_ms = now;
                    veejay_msg(VEEJAY_MSG_WARNING,
                               "[AUDIO-BEAT] sync read slow source=%d got=%d elapsed=%ldms request=%ld buffer=%d bpf=%d",
                               ab_sync_source(s),
                               got,
                               now - read_start,
                               target_bytes,
                               t.buffer_size,
                               t.bytes_per_frame);
                }
            }

            if(got <= 0)
            {
                ab_sleep_us(1000);
                continue;
            }

            got = ab_floor_to_multiple_i(got, t.bytes_per_frame);
            if(got <= 0)
                continue;

            ab_add_l(&s->reads, 1);

            hit = ab_analyse_block(s, &t, t.buffer, got);
            now = ab_now_ms();

            goto beat_process_hit;
        }

        if(!vj_jack_is_running() || !vj_jack_has_input())
        {
            t.open = 0;
            ab_store_i(&s->open, 0);
            ab_sleep_us(100000);
            continue;
        }

        now = ab_now_ms();

        ab_publish_l_cached(&s->overruns, &t.pub_overruns, vj_jack_input_overruns());

        stored = vj_jack_get_input_bytes_stored();

        if(stored <= 0)
        {
            ab_sleep_us(2000);
            continue;
        }

        target_bytes = ab_target_analysis_bytes(&t);

        if(target_bytes <= 0)
        {
            ab_sleep_us(2000);
            continue;
        }

        min_read_bytes = target_bytes / 4;

        if(min_read_bytes < (long)VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES * (long)t.bytes_per_frame)
            min_read_bytes = (long)VEEJAY_AUDIO_BEAT_MIN_READ_FRAMES * (long)t.bytes_per_frame;

        if(stored < min_read_bytes)
        {
            ab_sleep_us(1000);
            continue;
        }

        if(stored > target_bytes)
            stored = target_bytes;

        if(stored > t.buffer_size)
            stored = t.buffer_size;

        stored = ab_floor_to_multiple_l(stored, (long)t.bytes_per_frame);

        if(stored <= 0)
        {
            ab_sleep_us(2000);
            continue;
        }

        got = vj_jack_capture_read(t.buffer, (int)stored);

        if(got <= 0)
        {
            ab_add_l(&s->read_errors, 1);
            ab_sleep_us(2000);
            continue;
        }

        got = ab_floor_to_multiple_i(got, t.bytes_per_frame);

        if(got <= 0)
            continue;

        ab_record_ring_publish(s, &t, t.buffer, got);

        if(!first_capture_logged)
            first_capture_logged = 1;

        ab_add_l(&s->reads, 1);

        hit = ab_analyse_block(s, &t, t.buffer, got);
        now = ab_now_ms();

beat_process_hit:
        if(hit)
            ab_breakbeat_process_detector_hit(s, &t, got, now);

        /*
         * If there is still legacy JACK backlog, yield deliberately.
         * Sync-provider input owns its own read pacing.
         */
        if(!s->sync && vj_jack_get_input_bytes_stored() > target_bytes)
            ab_sleep_us(VEEJAY_AUDIO_BEAT_BACKLOG_YIELD_US);
    }

    if(t.buffer)
        free(t.buffer);

    if(!s->sync)
        ab_record_ring_free(s);

    ab_store_i(&s->open, 0);
    ab_store_i(&s->running, 0);

    veejay_msg(VEEJAY_MSG_DEBUG, "Beat analysis thread finished");
    return NULL;
}

int vj_audio_beat_enable(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    if(!ab_load_i(&s->initialized))
        vj_audio_beat_init(s, 2);

    if(s->sync) {
        int mode = vj_audio_sync_get_mode(s->sync);
        int channels = ab_load_i(&s->input_channels_request);
        int source = s->sync->source;
        int current_channels = s->sync->input_channels_request;

        if(mode == VJ_AUDIO_SYNC_MODE_OFF)
            vj_audio_sync_set_mode(s->sync,
                                   VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);

        if(channels <= 0)
            channels = 2;
        else if(channels > 2)
            channels = 2;

        /*
         * Do not let the beat detector steal monitor/tempo-bridge playback
         * or repeatedly reset the same JACK source/ring.
         */
        if(source == VJ_AUDIO_SYNC_SOURCE_NONE ||
           (source == VJ_AUDIO_SYNC_SOURCE_JACK && current_channels != channels))
        {
            vj_audio_sync_set_source_jack(s->sync, channels);
        }

        vj_audio_sync_enable(s->sync);
        vj_audio_sync_reset_beat_reader(s->sync);
    }

    ab_store_i(&s->consumed_seq, ab_load_i(&s->hit_seq));
    ab_breakbeat_hit_queue_clear();
    ab_source_activity_reset();
    ab_add_i(&s->reset_seq, 1);
    ab_store_i(&s->enabled, 1);

    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO-BEAT] analysis enabled");
    ab_log_config(s, "on enable");

    if(vj_jack_is_running() && !vj_jack_has_input())
        veejay_msg(VEEJAY_MSG_WARNING,"[AUDIO-BEAT] enabled but JACK has no capture input ports yet");

    return 1;
}

int vj_audio_beat_disable(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    ab_store_i(&s->enabled, 0);
    ab_store_i(&s->consumed_seq, ab_load_i(&s->hit_seq));

    ab_clear_published_control(s);
    ab_breakbeat_hit_queue_clear();
    ab_record_ring_clear(s);

    if(ab_breakbeat_state.active)
    {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-BEAT] disable requested while break-beat owns transport; release deferred to transport-aware path");
    }
    else
    {
        ab_breakbeat_reset_state();
    }


    ab_store_l(&ab_breakbeat_user_override_until_ms, 0);
    atomic_store_int(&ab_source_loss_paused, 0);

    veejay_msg(VEEJAY_MSG_INFO, "[AUDIO-BEAT] analysis disabled");
    ab_log_config(s, "on disable");
    return 1;
}

int vj_audio_beat_toggle(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    if(ab_load_i(&s->enabled))
        return vj_audio_beat_disable(s);

    return vj_audio_beat_enable(s);
}

static void ab_resume_from_consumer(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int speed;

    if(!v || !v->settings || !s)
        return;

    speed = ab_load_i(&s->resume_speed);

    if(speed == 0)
        speed = 1;

    ab_set_speed_from_beat(v, speed, 0);

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
}

static void ab_breakbeat_reset_state(void)
{
    memset(&ab_breakbeat_state, 0, sizeof(ab_breakbeat_state));
    ab_breakbeat_hit_queue_clear();
    ab_scratch_visual_reset();
}

static inline long ab_breakbeat_param_ms(vj_audio_beat_shared_t *s, volatile int *field, int lo, int hi)
{
    int ms = ab_load_i(field);

    (void)s;

    if(ms < lo)
        ms = lo;
    else if(ms > hi)
        ms = hi;

    return (long)ms;
}

static inline int ab_breakbeat_current_sfd(veejay_t *v)
{
    int sfd = 1;

    if(v && v->settings)
    {
        sfd = atomic_load_int(&v->settings->audio_slice_len);

        if(sfd < 1)
            sfd = v->settings->sfd;

        if(sfd < 1)
        {
            if(v->uc && v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
            {
                int id = v->uc->sample_id;
                if(id > 0 && sample_exists(id))
                    sfd = sample_get_framedup(id);
            }
            else if(v->sfd > 0)
                sfd = v->sfd;
        }
    }

    if(sfd < 1)
        sfd = 1;
    else if(sfd > 64)
        sfd = 64;

    return sfd;
}

static inline float ab_breakbeat_sfd_scale(veejay_t *v)
{
    int sfd = ab_breakbeat_current_sfd(v);

    return sfd > 1 ? (1.0f / (float)sfd) : 1.0f;
}

static inline long ab_breakbeat_ms_from_beats(const vj_audio_beat_snapshot_t *snap,
                                              float beats,
                                              long min_ms,
                                              long max_ms);
static inline float ab_breakbeat_alpha_from_beats(long dt_ms, float beats);
static inline float ab_breakbeat_clampf(float v);
static inline float ab_breakbeat_expression_drive(void);
static inline float ab_breakbeat_accel_drive(void);

static inline double ab_breakbeat_video_fps(void)
{
    int q16 = ab_load_i(&ab_auto_video_fps_q16);

    if(q16 <= 0)
        return 25.0;

    return (double)q16 * (1.0 / 65536.0);
}

#define AB_BREAKBEAT_LATE_HIT_FRAMES 1.25f
#define AB_BREAKBEAT_VERY_LATE_HIT_FRAMES 1.75f
#define AB_BREAKBEAT_SCRATCH_BLOCK_ESCAPE_MAX_AGE_MS 78L
#define AB_BREAKBEAT_SCRATCH_BLOCK_ESCAPE_MAX_EFF_FRAMES 0.90f

static inline float ab_breakbeat_latency_frames_from_age(long event_age_ms)
{
    double fps;

    if(event_age_ms <= 0)
        return 0.0f;

    fps = ab_breakbeat_video_fps();
    if(fps <= 0.01 || fps > 240.0)
        fps = 25.0;

    return (float)(((double)event_age_ms * fps) / 1000.0);
}

static inline float ab_breakbeat_signed_frames_from_ms(long ms)
{
    double fps;

    fps = ab_breakbeat_video_fps();
    if(fps <= 0.01 || fps > 240.0)
        fps = 25.0;

    return (float)(((double)ms * fps) / 1000.0);
}

static inline float ab_breakbeat_late_hit_drive(float latency_frames)
{
    if(latency_frames <= AB_BREAKBEAT_LATE_HIT_FRAMES)
        return 0.0f;
    if(latency_frames >= AB_BREAKBEAT_VERY_LATE_HIT_FRAMES)
        return 1.0f;

    return (latency_frames - AB_BREAKBEAT_LATE_HIT_FRAMES) /
           (AB_BREAKBEAT_VERY_LATE_HIT_FRAMES - AB_BREAKBEAT_LATE_HIT_FRAMES);
}

static inline float ab_breakbeat_effective_late_frames(long event_age_ms)
{
    long heard_latency_ms;
    long speaker_late_ms;

    heard_latency_ms = atomic_load_int(&ab_heard_latency_ms);
    if(heard_latency_ms > 0)
    {
        speaker_late_ms = event_age_ms - heard_latency_ms;
        if(speaker_late_ms <= 0)
            return 0.0f;

        return ab_breakbeat_signed_frames_from_ms(speaker_late_ms);
    }

    return ab_breakbeat_latency_frames_from_age(event_age_ms);
}

static inline float ab_breakbeat_runtime_fps(veejay_t *v)
{
    float fps = 0.0f;

    if(v && v->settings)
        fps = v->settings->output_fps;

    if(fps <= 0.01f)
        fps = (float)ab_breakbeat_video_fps();
    if(fps <= 0.01f)
        fps = 25.0f;

    return fps;
}

static inline float ab_breakbeat_base_fps(veejay_t *v)
{
    float fps = ab_breakbeat_state.base_fps;

    if(fps <= 0.01f)
        fps = (float)ab_breakbeat_video_fps();
    if(fps <= 0.01f)
        fps = ab_breakbeat_runtime_fps(v);
    if(fps <= 0.01f)
        fps = 25.0f;

    if(fps > 60.0f)
        fps = 60.0f;

    fps *= ab_breakbeat_sfd_scale(v);

    if(fps < 1.0f)
        fps = 1.0f;

    return fps;
}

static inline float ab_breakbeat_clamp_fps(float fps)
{
    if(fps < 5.0f)
        fps = 5.0f;
    else if(fps > 240.0f)
        fps = 240.0f;

    return fps;
}

static inline float ab_breakbeat_max_effect_fps(veejay_t *v)
{
    float base = ab_breakbeat_base_fps(v);
    float max_fps = base * ab_breakbeat_policy.effect_fps_max_mul;

    if(max_fps < base)
        max_fps = base;

    return ab_breakbeat_clamp_fps(max_fps);
}

static inline float ab_breakbeat_clamp_effect_fps(veejay_t *v, float fps)
{
    float max_fps = ab_breakbeat_base_fps(v);

    if(max_fps < 1.0f)
        max_fps = ab_breakbeat_max_effect_fps(v);

    if(fps < 5.0f)
        fps = 5.0f;
    else if(fps > max_fps)
        fps = max_fps;

    return fps;
}

static inline void ab_breakbeat_set_fps_immediate(veejay_t *v, float fps)
{
    long now;

    if(!v || !v->settings)
        return;

    fps = ab_breakbeat_clamp_fps(fps);
    now = ab_now_ms();

    if(fabs((double)ab_breakbeat_runtime_fps(v) - (double)fps) >= 0.05)
        veejay_set_framerate(v, fps);

    ab_breakbeat_state.current_fps = fps;
    ab_breakbeat_state.target_fps = fps;
    ab_breakbeat_state.fps_last_ms = now;
    ab_breakbeat_state.fps_write_last_ms = now;
}

static inline void ab_breakbeat_apply_fps(veejay_t *v, float fps)
{
    long now;
    long dt;
    long since_write;
    long write_gap;
    float runtime;
    float cur;
    float diff;
    float adiff;
    float alpha;
    float base;
    float runtime_delta;

    if(!v || !v->settings)
        return;

    fps = ab_breakbeat_clamp_effect_fps(v, fps);
    now = ab_now_ms();
    runtime = ab_breakbeat_runtime_fps(v);

    cur = ab_breakbeat_state.current_fps;
    if(cur <= 0.01f)
        cur = runtime;
    if(cur <= 0.01f)
        cur = ab_breakbeat_base_fps(v);

    if(ab_breakbeat_state.fps_write_last_ms <= 0 ||
       (fabs((double)runtime - (double)cur) > ab_breakbeat_base_fps(v) * 0.32 &&
        (now - ab_breakbeat_state.fps_write_last_ms) >
            ab_breakbeat_ms_from_beats(NULL, ab_breakbeat_policy.fps_external_snap_beats, 40L, 260L)))
        cur = runtime;

    dt = ab_breakbeat_state.fps_last_ms > 0 ? now - ab_breakbeat_state.fps_last_ms : 1L;
    if(dt < 1)
        dt = 1;
    else if(dt > ab_breakbeat_ms_from_beats(NULL, 0.50f, 80L, 320L))
        dt = ab_breakbeat_ms_from_beats(NULL, 0.50f, 80L, 320L);

    diff = fps - cur;
    adiff = diff < 0.0f ? -diff : diff;
    ab_breakbeat_state.target_fps = fps;

    if(adiff >= 0.05f)
    {
        alpha = ab_breakbeat_alpha_from_beats(
            dt,
            diff > 0.0f ?
                ab_breakbeat_policy.fps_attack_beats :
                ab_breakbeat_policy.fps_release_beats
        );

        if(diff > 0.0f)
        {
            float boost = ab_breakbeat_clampf(ab_breakbeat_state.tempo_drive * 0.34f +
                                              ab_breakbeat_expression_drive() * 0.22f +
                                              ab_breakbeat_accel_drive() * 0.18f);

            alpha += (1.0f - alpha) * boost * 0.35f;
        }

        if(alpha > 1.0f)
            alpha = 1.0f;

        cur += diff * alpha;
    }
    else
    {
        cur = fps;
    }

    base = ab_breakbeat_base_fps(v);
    if(cur < base * 0.16f)
        cur = base * 0.16f;

    cur = ab_breakbeat_clamp_effect_fps(v, cur);
    since_write = ab_breakbeat_state.fps_write_last_ms > 0 ? now - ab_breakbeat_state.fps_write_last_ms : LONG_MAX;
    write_gap = ab_breakbeat_ms_from_beats(NULL, ab_breakbeat_policy.fps_write_beats, 16L, 80L);
    runtime_delta = (float)fabs((double)runtime - (double)cur);

    if(runtime_delta >= 0.15f &&
       (since_write >= write_gap ||
        runtime_delta >= base * 0.16f ||
        fabs((double)fps - (double)cur) < 0.08))
    {
        veejay_set_framerate(v, cur);
        ab_breakbeat_state.fps_write_last_ms = now;
    }

    ab_breakbeat_state.current_fps = cur;
    ab_breakbeat_state.fps_last_ms = now;
}

static inline void ab_breakbeat_restore_fps(veejay_t *v)
{
    if(ab_breakbeat_state.saved_fps > 0.01f)
        ab_breakbeat_set_fps_immediate(v, ab_breakbeat_state.saved_fps);
}

static inline int ab_breakbeat_base_speed(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int speed = 0;

    if(ab_breakbeat_state.active && ab_breakbeat_state.saved_speed != 0)
        speed = ab_breakbeat_state.saved_speed;
    else if(v && v->settings)
        speed = v->settings->current_playback_speed;

    if(speed == 0)
        speed = ab_breakbeat_state.saved_speed;
    if(speed == 0)
        speed = ab_load_i(&s->resume_speed);
    if(speed == 0)
        speed = 1;
    if(speed < 0)
        speed = -speed;
    if(speed < 1)
        speed = 1;

    return speed;
}

static inline int ab_breakbeat_user_pause_override_active(void)
{
    return ab_load_l(&ab_breakbeat_user_override_until_ms) == AB_BREAKBEAT_USER_PAUSE_OVERRIDE;
}

static int ab_breakbeat_respect_user_pause(veejay_t *v, vj_audio_beat_shared_t *s, int hit_seq)
{
    if(!v || !v->settings || !s)
        return 0;

    if(v->settings->current_playback_speed != 0)
        return 0;

    if(ab_load_i(&s->paused_by_beat))
        return 0;

    if(hit_seq < 0)
        hit_seq = ab_load_i(&s->hit_seq);

    if(ab_breakbeat_state.active)
        ab_breakbeat_restore_fps(v);

    ab_breakbeat_reset_state();

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->resume_speed, 0);
    ab_store_i(&s->consumed_seq, hit_seq);
    ab_store_l(&ab_breakbeat_user_override_until_ms, AB_BREAKBEAT_USER_PAUSE_OVERRIDE);

    return 1;
}

static int ab_breakbeat_detect_user_resume(veejay_t *v, vj_audio_beat_shared_t *s, long now, int hit_seq)
{
    int speed;

    if(!v || !v->settings || !s)
        return 0;

    if(ab_load_l(&ab_breakbeat_user_override_until_ms) != AB_BREAKBEAT_USER_PAUSE_OVERRIDE)
        return 0;

    speed = v->settings->current_playback_speed;
    if(speed == 0)
        return 0;

    if(hit_seq < 0)
        hit_seq = ab_load_i(&s->hit_seq);

    ab_breakbeat_reset_state();
    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->resume_speed, speed);
    ab_store_i(&s->consumed_seq, hit_seq);
    ab_store_l(&ab_breakbeat_user_override_until_ms, now + 220L);

    return 1;
}

static inline long long ab_breakbeat_current_frame(veejay_t *v)
{
    long long frame = 0;

    if(v && v->settings)
        frame = atomic_load_long_long(&v->settings->current_frame_num);

    if(frame < 0)
        frame = 0;

    return frame;
}

static inline int ab_breakbeat_bounds(veejay_t *v, long long *lo, long long *hi)
{
    long long b_lo = 0;
    long long b_hi = LONG_MAX;

    if(v && v->uc && v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
    {
        int id = v->uc->sample_id;

        if(id > 0 && sample_exists(id))
        {
            long start = sample_get_startFrame(id);
            long end = sample_get_endFrame(id);

            if(end >= start)
            {
                b_lo = (long long)start;
                b_hi = (long long)end;

                if(lo)
                    *lo = b_lo;
                if(hi)
                    *hi = b_hi;

                return 1;
            }
        }
    }

    if(v && v->settings)
    {
        long long min_frame = atomic_load_long_long(&v->settings->min_frame_num);
        long long max_frame = atomic_load_long_long(&v->settings->max_frame_num);

        if(max_frame >= min_frame)
        {
            b_lo = min_frame;
            b_hi = max_frame;

            if(b_lo < 0)
                b_lo = 0;
        }
    }

    if(lo)
        *lo = b_lo;
    if(hi)
        *hi = b_hi;

    return b_hi > b_lo;
}

static inline long long ab_breakbeat_clamp_frame(veejay_t *v, long long frame)
{
    long long lo = 0;
    long long hi = LONG_MAX;

    ab_breakbeat_bounds(v, &lo, &hi);

    if(frame < lo)
        frame = lo;
    else if(frame > hi)
        frame = hi;

    return frame;
}

static inline int ab_breakbeat_span_frames(veejay_t *v)
{
    long long lo = 0;
    long long hi = 0;
    long long span;

    if(!ab_breakbeat_bounds(v, &lo, &hi))
        return 0;

    span = hi - lo + 1;

    if(span <= 0)
        return 0;
    if(span > INT_MAX)
        return INT_MAX;

    return (int)span;
}

static inline long long ab_breakbeat_frame_from_hit_time(veejay_t *v,
                                                         vj_audio_beat_shared_t *s,
                                                         long hit_ms,
                                                         long now)
{
    long long frame = ab_breakbeat_current_frame(v);
    long delta_ms;
    int speed = 1;
    int dir = 1;
    int mag;
    double fps;
    long long delta_frames;

    if(hit_ms <= 0 || now <= hit_ms)
        return ab_breakbeat_clamp_frame(v, frame);

    if(v && v->settings)
        speed = v->settings->current_playback_speed;
    else if(ab_breakbeat_state.direction != 0)
        speed = ab_breakbeat_state.direction;
    else if(s)
        speed = ab_load_i(&s->resume_speed);

    if(speed == 0)
        return ab_breakbeat_clamp_frame(v, frame);

    if(speed < 0)
    {
        dir = -1;
        mag = -speed;
    }
    else
    {
        dir = 1;
        mag = speed;
    }

    if(mag < 1)
        mag = 1;
    else if(mag > 16)
        mag = 16;

    fps = (double)ab_breakbeat_runtime_fps(v);
    if(fps < 1.0)
        fps = ab_breakbeat_video_fps();
    if(fps < 1.0)
        fps = 25.0;

    delta_ms = now - hit_ms;
    if(delta_ms > 1000L)
        delta_ms = 1000L;

    delta_frames = (long long)(((double)delta_ms * fps * (double)mag) / 1000.0 + 0.5);

    if(dir < 0)
        frame += delta_frames;
    else
        frame -= delta_frames;

    return ab_breakbeat_clamp_frame(v, frame);
}


static inline long long ab_llabs(long long v)
{
    return v < 0 ? -v : v;
}

static inline int ab_breakbeat_scene_strong(vj_scene_detect_t *sc)
{
    int score;

    if(!sc || !atomic_load_int(&sc->valid))
        return 0;

    score = atomic_load_int(&sc->cut_score_q15);

    return atomic_load_int(&sc->hard_cut) || score >= 24576;
}

static inline int ab_breakbeat_scene_snap_window(const vj_audio_beat_snapshot_t *snap)
{
    int win = 1;

    if(snap)
    {
        if(snap->transient > 0.72f || snap->kick > 0.70f || snap->snare > 0.70f)
            win = 2;
        if(snap->hit_kind == AB_HIT_HAT && snap->high < 0.80f)
            win = 1;
    }

    if(ab_breakbeat_video_fps() > 50.0 && win < 2)
        win = 2;

    return win;
}

static inline int ab_breakbeat_scene_id_for_frame(veejay_t *v, long long frame)
{
    vj_scene_detect_t *sc;
    int id;
    long long cut;

    if(!v || !v->settings)
        return 0;

    sc = &v->settings->scene_detect;
    if(!atomic_load_int(&sc->valid))
        return 0;

    id = atomic_load_int(&sc->scene_id);
    cut = atomic_load_long_long(&sc->last_cut_frame);

    if(id <= 0)
        return 0;

    if(cut >= 0 && frame < cut && id > 1)
        id--;

    return id;
}

static inline long long ab_breakbeat_scene_refine_frame(veejay_t *v,
                                                        long long frame,
                                                        const vj_audio_beat_snapshot_t *snap)
{
    vj_scene_detect_t *sc;
    long long cut;
    long long d;
    int win;

    if(!v || !v->settings)
        return ab_breakbeat_clamp_frame(v, frame);

    sc = &v->settings->scene_detect;
    if(!ab_breakbeat_scene_strong(sc))
        return ab_breakbeat_clamp_frame(v, frame);

    cut = atomic_load_long_long(&sc->last_cut_frame);
    if(cut < 0)
        return ab_breakbeat_clamp_frame(v, frame);

    win = ab_breakbeat_scene_snap_window(snap);
    d = ab_llabs(frame - cut);

    if(d <= (long long)win)
        frame = cut;

    return ab_breakbeat_clamp_frame(v, frame);
}

static inline long long ab_breakbeat_scene_guard_target(veejay_t *v,
                                                        long long from,
                                                        long long target)
{
    vj_scene_detect_t *sc;
    long long cut;
    long long lo;
    long long hi;

    if(!v || !v->settings)
        return target;

    sc = &v->settings->scene_detect;
    if(!ab_breakbeat_scene_strong(sc))
        return target;

    cut = atomic_load_long_long(&sc->last_cut_frame);
    if(cut < 0)
        return target;

    if(from <= target)
    {
        lo = from;
        hi = target;
    }
    else
    {
        lo = target;
        hi = from;
    }

    if(cut <= lo || cut >= hi)
        return target;

    if(target >= from)
        return cut;

    return cut > 0 ? cut - 1 : cut;
}

static inline void ab_breakbeat_scene_guard_loop(veejay_t *v,
                                                 long long center,
                                                 long long *a,
                                                 long long *b)
{
    vj_scene_detect_t *sc;
    long long cut;

    if(!v || !v->settings || !a || !b)
        return;

    sc = &v->settings->scene_detect;
    if(!ab_breakbeat_scene_strong(sc))
        return;

    cut = atomic_load_long_long(&sc->last_cut_frame);
    if(cut < 0)
        return;

    if(cut <= *a || cut > *b)
        return;

    if(center >= cut)
        *a = cut;
    else
        *b = cut - 1;

    if(*b <= *a)
    {
        *a = center;
        *b = center;
    }
}

static inline int ab_breakbeat_is_sample(veejay_t *v)
{
    return v && v->uc && v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE;
}

static inline int ab_breakbeat_sample_looptype(veejay_t *v)
{
    if(ab_breakbeat_is_sample(v))
    {
        int id = v->uc->sample_id;

        if(id > 0 && sample_exists(id))
            return sample_get_looptype(id);
    }

    return 1;
}

static inline long long ab_breakbeat_mod_range(long long value, long long lo, long long hi)
{
    long long span = hi - lo + 1;
    long long rel;

    if(span <= 1)
        return lo;

    rel = (value - lo) % span;
    if(rel < 0)
        rel += span;

    return lo + rel;
}

static inline long long ab_breakbeat_reflect_range(long long value, long long lo, long long hi, int *dir)
{
    long long span = hi - lo;
    long long period;
    long long rel;

    if(span <= 0)
        return lo;

    period = span * 2;
    rel = (value - lo) % period;
    if(rel < 0)
        rel += period;

    if(rel > span)
    {
        if(dir)
            *dir = -*dir;
        return hi - (rel - span);
    }

    return lo + rel;
}

static inline long long ab_breakbeat_project_frame(veejay_t *v, long long frame, int *dir, int *park)
{
    long long lo = 0;
    long long hi = 0;

    if(park)
        *park = 0;

    if(!ab_breakbeat_bounds(v, &lo, &hi))
        return frame;

    if(frame >= lo && frame <= hi)
        return frame;

    if(ab_breakbeat_is_sample(v))
    {
        switch(ab_breakbeat_sample_looptype(v))
        {
            case 1:
                return ab_breakbeat_mod_range(frame, lo, hi);

            case 2:
                return ab_breakbeat_reflect_range(frame, lo, hi, dir);

            case 3:
                return ab_breakbeat_mod_range(frame, lo, hi);

            default:
                if(park)
                    *park = 1;
                return frame < lo ? lo : hi;
        }
    }

    return frame < lo ? lo : hi;
}

static inline float ab_breakbeat_clampf(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

static inline float ab_breakbeat_groove_drive(void)
{
    float v = ab_auto_groove_level;

    if(v < ab_breakbeat_state.music_groove)
        v = ab_breakbeat_state.music_groove;

    return ab_breakbeat_clampf(v);
}

static inline float ab_breakbeat_phrase_drive(void)
{
    float v = ab_auto_phrase_level;

    if(v < ab_breakbeat_state.music_phrase)
        v = ab_breakbeat_state.music_phrase;

    return ab_breakbeat_clampf(v);
}

static inline float ab_breakbeat_climax_drive(void)
{
    float auto_v = ab_auto_climax_level * 0.62f +
                   ab_auto_groove_level * 0.26f +
                   ab_auto_phrase_level * 0.12f;
    float local_v = ab_breakbeat_state.music_climax * 0.68f +
                    ab_breakbeat_state.music_phrase * 0.22f +
                    ab_breakbeat_state.music_groove * 0.10f;
    float v = auto_v > local_v ? auto_v : local_v;

    return ab_breakbeat_clampf(v);
}

static inline float ab_breakbeat_tonal_bias(const vj_audio_beat_snapshot_t *snap)
{
    float body;
    float tonal;
    float bias;

    if(!snap)
        return 0.0f;

    body = snap->kick > snap->snare ? snap->kick : snap->snare;
    if(body < snap->bass * 0.58f)
        body = snap->bass * 0.58f;

    tonal = snap->mid > snap->high ? snap->mid : snap->high;
    if(tonal < snap->hat * 0.72f)
        tonal = snap->hat * 0.72f;

    bias = (tonal - body * 1.06f) * (1.0f / 0.62f);

    return ab_breakbeat_clampf(bias);
}

static inline float ab_breakbeat_percussive_drive(const vj_audio_beat_snapshot_t *snap, int hit_kind)
{
    float body;
    float conf;
    float tonal;

    if(!snap)
        return 0.0f;

    body = snap->kick > snap->snare ? snap->kick : snap->snare;

    if(hit_kind == AB_HIT_FULL && body < snap->bass * 0.66f)
        body = snap->bass * 0.66f;

    tonal = ab_breakbeat_tonal_bias(snap);

    conf = body * 0.54f +
           snap->transient * 0.25f +
           snap->beat_gate * 0.10f +
           snap->flux * 0.07f +
           snap->bass * 0.04f;

    if(hit_kind == AB_HIT_KICK)
        conf += snap->kick * 0.13f;
    else if(hit_kind == AB_HIT_SNARE)
        conf += snap->snare * 0.13f;
    else if(hit_kind == AB_HIT_FULL && body < 0.30f)
        conf *= 0.78f;
    else if(hit_kind == AB_HIT_HAT)
        conf *= 0.42f + snap->transient * 0.18f;

    if(tonal > 0.18f && body < 0.40f)
        conf *= 1.0f - tonal * (0.42f + (0.40f - body) * 0.55f);

    if(snap->envelope > snap->level * 1.12f &&
       snap->transient < 0.58f &&
       body < 0.36f)
        conf *= 0.68f;

    if(snap->transient > 0.74f && snap->flux > 0.68f && body > 0.28f)
        conf += 0.08f;

    return ab_breakbeat_clampf(conf);
}

static inline int ab_breakbeat_tonal_transport_hit(const vj_audio_beat_snapshot_t *snap, int hit_kind)
{
    float body;
    float tonal;
    float percussive;

    if(!snap || hit_kind == AB_HIT_HAT || hit_kind == AB_HIT_SCRATCH)
        return 0;

    body = snap->kick > snap->snare ? snap->kick : snap->snare;
    tonal = ab_breakbeat_tonal_bias(snap);
    percussive = ab_breakbeat_percussive_drive(snap, hit_kind);

    return tonal >= ab_breakbeat_policy.tonal_guard_bias &&
           percussive <= ab_breakbeat_policy.tonal_guard_percussive &&
           body <= ab_breakbeat_policy.tonal_guard_body;
}

static inline float ab_breakbeat_velocity(const vj_audio_beat_snapshot_t *snap)
{
    float body;
    float hat_drive;
    float v;

    if(!snap)
        return 0.0f;

    body = snap->kick > snap->snare ? snap->kick : snap->snare;
    hat_drive = snap->hat * (snap->hit_kind == AB_HIT_HAT ? 0.46f : 0.20f);

    v = body * 0.42f +
        snap->transient * 0.31f +
        snap->flux * 0.14f +
        snap->level * 0.09f +
        hat_drive * 0.04f;

    if(snap->hit_kind == AB_HIT_HAT)
        v *= 0.66f + snap->transient * 0.16f;

    if(v < 0.0f)
        v = 0.0f;
    else if(v > 1.0f)
        v = 1.0f;

    return v;
}

static inline double ab_breakbeat_transport_period_ms(const vj_audio_beat_snapshot_t *snap);

static inline float ab_breakbeat_activity(const vj_audio_beat_snapshot_t *snap)
{
    float activity;

    if(!snap)
        return 0.0f;

    activity = snap->level;
    if(activity < snap->envelope)
        activity = snap->envelope;
    if(activity < snap->transient)
        activity = snap->transient;
    if(activity < snap->flux)
        activity = snap->flux;
    if(activity < snap->bass)
        activity = snap->bass;
    if(activity < snap->mid)
        activity = snap->mid;
    if(activity < snap->high)
        activity = snap->high;

    return activity;
}

static inline float ab_breakbeat_interval_events_per_beat(long interval_ms, double period_ms)
{
    float ratio;

    if(interval_ms <= 0 || period_ms < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS)
        return 1.0f;

    ratio = (float)(period_ms / (double)interval_ms);

    if(ratio < 0.0f)
        ratio = 0.0f;
    else if(ratio > ab_breakbeat_policy.density_events_per_beat)
        ratio = ab_breakbeat_policy.density_events_per_beat;

    return ratio;
}

static inline float ab_breakbeat_density_from_ratio(float events_per_beat)
{
    float span = ab_breakbeat_policy.density_events_per_beat - 1.0f;
    float density;

    if(span <= 0.001f || events_per_beat <= 1.0f)
        return 0.0f;

    density = (events_per_beat - 1.0f) / span;
    return ab_breakbeat_clampf(density);
}

static inline float ab_breakbeat_pace_drive_from_interval(const vj_audio_beat_snapshot_t *snap, long interval_ms)
{
    double period = ab_breakbeat_transport_period_ms(snap);
    float events_per_beat = ab_breakbeat_interval_events_per_beat(interval_ms, period);

    return ab_breakbeat_density_from_ratio(events_per_beat);
}

static inline float ab_breakbeat_pace_drive(const vj_audio_beat_snapshot_t *snap)
{
    double period;
    float drive;

    if(!snap)
        return 0.0f;

    period = ab_breakbeat_transport_period_ms(snap);
    drive = ab_breakbeat_pace_drive_from_interval(snap, ab_breakbeat_state.tempo_prev_interval_ms);

    if(snap->beat_age_ms > 0 && period > 1.0)
    {
        float hold_ms = (float)period * ab_breakbeat_policy.pace_hold_beats;
        float fade_ms = (float)period * ab_breakbeat_policy.pace_fade_beats;

        if((float)snap->beat_age_ms > hold_ms)
        {
            float k = 1.0f - (((float)snap->beat_age_ms - hold_ms) / fade_ms);

            if(k < 0.0f)
                k = 0.0f;

            drive *= k;
        }
    }

    return ab_breakbeat_clampf(drive);
}

static inline float ab_breakbeat_density_from_interval(const vj_audio_beat_snapshot_t *snap, long interval_ms)
{
    double period = ab_breakbeat_transport_period_ms(snap);
    float events_per_beat = ab_breakbeat_interval_events_per_beat(interval_ms, period);

    return ab_breakbeat_density_from_ratio(events_per_beat);
}

static inline float ab_breakbeat_regular_drive(void)
{
    return ab_breakbeat_clampf(ab_breakbeat_state.rhythm_regularity);
}

static inline float ab_breakbeat_accent_drive(void)
{
    return ab_breakbeat_clampf(ab_breakbeat_state.rhythm_accent);
}

static inline float ab_breakbeat_accel_drive(void)
{
    return ab_breakbeat_clampf(ab_breakbeat_state.rhythm_accel);
}

static inline float ab_breakbeat_syncopation_drive(void)
{
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    float regular = ab_breakbeat_regular_drive();

    return ab_breakbeat_clampf((1.0f - regular) * (0.38f + density * 0.62f));
}

static inline float ab_breakbeat_expression_drive(void)
{
    float accent = ab_breakbeat_accent_drive();
    float accel = ab_breakbeat_accel_drive();
    float sync = ab_breakbeat_syncopation_drive();

    return ab_breakbeat_clampf(accent * 0.56f + sync * 0.26f + accel * 0.18f);
}

static inline double ab_breakbeat_transport_period_ms(const vj_audio_beat_snapshot_t *snap)
{
    double period = 0.0;

    if(snap && snap->bpm > 1.0f)
        period = 60000.0 / (double)snap->bpm;

    if(period < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS ||
       period > (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        period = (double)ab_breakbeat_state.rhythm_interval_ema_ms;

    if(period < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS ||
       period > (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        period = (double)ab_breakbeat_state.tempo_prev_interval_ms;

    return ab_bound_period_ms(period);
}

static inline double ab_breakbeat_state_period_ms(void)
{
    double period = (double)ab_breakbeat_state.rhythm_interval_ema_ms;

    if(period < (double)VEEJAY_AUDIO_BEAT_MIN_PERIOD_MS ||
       period > (double)VEEJAY_AUDIO_BEAT_MAX_PERIOD_MS)
        period = (double)ab_breakbeat_state.tempo_prev_interval_ms;

    return ab_bound_period_ms(period);
}

static inline long ab_breakbeat_ms_from_beats(const vj_audio_beat_snapshot_t *snap,
                                              float beats,
                                              long min_ms,
                                              long max_ms)
{
    long ms;

    if(beats < 0.0f)
        beats = 0.0f;

    ms = (long)(ab_breakbeat_transport_period_ms(snap) * (double)beats + 0.5);

    if(ms < min_ms)
        ms = min_ms;
    else if(ms > max_ms)
        ms = max_ms;

    return ms;
}

static inline float ab_breakbeat_alpha_from_beats(long dt_ms, float beats)
{
    float tau;

    if(dt_ms <= 0)
        return 0.0f;

    if(beats < 0.05f)
        beats = 0.05f;

    tau = (float)ab_breakbeat_state_period_ms() * beats;

    if(tau < 8.0f)
        tau = 8.0f;

    return ab_breakbeat_clampf(1.0f - expf(-((float)dt_ms / tau)));
}

static inline float ab_breakbeat_transport_intensity(const vj_audio_beat_snapshot_t *snap,
                                                     int hit_kind)
{
    float percussive = ab_breakbeat_percussive_drive(snap, hit_kind);
    float expression = ab_breakbeat_expression_drive();
    float accel = ab_breakbeat_accel_drive();
    float climax = ab_breakbeat_climax_drive();
    float accent = ab_breakbeat_accent_drive();

    return ab_breakbeat_clampf(
        percussive * ab_breakbeat_policy.intensity_percussive_w +
        expression * ab_breakbeat_policy.intensity_expression_w +
        accel * ab_breakbeat_policy.intensity_accel_w +
        climax * ab_breakbeat_policy.intensity_climax_w +
        accent * ab_breakbeat_policy.intensity_accent_w
    );
}

static inline float ab_breakbeat_steady_drive(void)
{
    float regular = ab_breakbeat_regular_drive();
    float expression = ab_breakbeat_expression_drive();
    float accel = ab_breakbeat_accel_drive();

    return ab_breakbeat_clampf(regular * (1.0f - expression) * (1.0f - accel));
}

static inline long ab_breakbeat_open_ms(vj_audio_beat_shared_t *s,
                                         const vj_audio_beat_snapshot_t *snap,
                                         int hit_kind)
{
    long configured = ab_breakbeat_param_ms(s, &s->freeze_ms, 20, 1000);
    double period = ab_breakbeat_transport_period_ms(snap);
    float intensity = ab_breakbeat_transport_intensity(snap, hit_kind);
    float steady = ab_breakbeat_steady_drive();
    float frac;
    long musical;
    long value;

    if(hit_kind == AB_HIT_SCRATCH)
    {
        float amount = snap ? snap->beat_density : 0.0f;
        float burst = snap ? snap->transient : 0.0f;
        float velocity = snap ? snap->flux : 0.0f;
        float drive;

        if(amount < velocity)
            amount = velocity;

        drive = ab_breakbeat_clampf(amount * 0.62f + burst * 0.28f + velocity * 0.10f);
        value = ab_breakbeat_policy.scratch_open_min_ms +
                (long)((double)(ab_breakbeat_policy.scratch_open_max_ms -
                                ab_breakbeat_policy.scratch_open_min_ms) *
                       (double)drive + 0.5);

        if(burst > amount)
            value -= (long)((double)value * (double)(burst - amount) * 0.18 + 0.5);

        if(value < ab_breakbeat_policy.scratch_open_min_ms)
            value = ab_breakbeat_policy.scratch_open_min_ms;
        else if(value > ab_breakbeat_policy.scratch_open_max_ms)
            value = ab_breakbeat_policy.scratch_open_max_ms;

        return value;
    }

    if(hit_kind == AB_HIT_HAT)
        frac = ab_breakbeat_policy.hat_open_beats;
    else if(hit_kind == AB_HIT_SNARE)
        frac = ab_breakbeat_policy.snare_open_beats;
    else
        frac = ab_breakbeat_policy.body_open_beats;

    frac *= 1.0f + intensity * ab_breakbeat_policy.open_excite_gain;
    frac *= 1.0f - steady * ab_breakbeat_policy.steady_open_duck;

    if(frac < 0.025f)
        frac = 0.025f;

    musical = (long)(period * (double)frac + 0.5);

    if(hit_kind == AB_HIT_HAT)
        musical = ab_clamp_l(musical, 18L, ab_breakbeat_ms_from_beats(snap, 0.20f, 70L, 260L));
    else
        musical = ab_clamp_l(musical, 35L, ab_breakbeat_ms_from_beats(snap, 0.58f, 160L, 760L));

    value = (long)((double)configured * (double)ab_breakbeat_policy.open_user_mix +
                   (double)musical * (1.0 - (double)ab_breakbeat_policy.open_user_mix) + 0.5);

    if(hit_kind == AB_HIT_HAT)
        return ab_clamp_l(value, 18L, ab_breakbeat_ms_from_beats(snap, 0.24f, 90L, 300L));

    return ab_clamp_l(value, 40L, ab_breakbeat_ms_from_beats(snap, 0.66f, 200L, 900L));
}

static inline long ab_breakbeat_transport_min_gap_ms(const vj_audio_beat_snapshot_t *snap,
                                                     int hit_kind,
                                                     int repeated)
{
    double period = ab_breakbeat_transport_period_ms(snap);
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    float steady = ab_breakbeat_steady_drive();
    float intensity = ab_breakbeat_transport_intensity(snap, hit_kind);
    float calm_gap;
    float hot_gap;
    float frac;

    if(hit_kind == AB_HIT_SCRATCH)
    {
        float scratch = snap ? snap->beat_density : 0.0f;
        float burst = snap ? snap->transient : 0.0f;
        float velocity = snap ? snap->flux : 0.0f;
        float scratch_drive;

        if(scratch < velocity)
            scratch = velocity;

        scratch_drive = ab_breakbeat_clampf(scratch * 0.62f + burst * 0.38f);
        frac = ab_breakbeat_policy.scratch_gap_max_beats -
               scratch_drive * (ab_breakbeat_policy.scratch_gap_max_beats -
                                ab_breakbeat_policy.scratch_gap_min_beats);

        if(repeated)
            frac *= 1.0f - scratch_drive * 0.18f;

        return (long)(period * (double)frac + 0.5);
    }

    if(hit_kind == AB_HIT_HAT)
    {
        calm_gap = ab_breakbeat_policy.hat_gap_calm_beats;
        hot_gap = ab_breakbeat_policy.hat_gap_hot_beats;
    }
    else
    {
        calm_gap = ab_breakbeat_policy.body_gap_calm_beats;
        hot_gap = ab_breakbeat_policy.body_gap_hot_beats;
    }

    frac = calm_gap - intensity * (calm_gap - hot_gap);
    frac += steady * (hit_kind == AB_HIT_HAT ? 0.18f : 0.14f);
    frac -= density * (hit_kind == AB_HIT_HAT ? 0.05f : 0.04f);

    if(repeated)
        frac += (1.0f - intensity) * (hit_kind == AB_HIT_HAT ? 0.10f : 0.06f);

    if(frac < hot_gap)
        frac = hot_gap;
    else if(frac > 0.96f)
        frac = 0.96f;

    return (long)(period * (double)frac + 0.5);
}

static inline int ab_breakbeat_transport_hit_allowed(const vj_audio_beat_snapshot_t *snap,
                                                     long now,
                                                     int hit_kind,
                                                     int repeated)
{
    long last = ab_breakbeat_state.last_transport_action_ms;
    long age;
    long min_gap;
    double period;
    float intensity;
    float density;

    intensity = ab_breakbeat_transport_intensity(snap, hit_kind);
    density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);

    if(hit_kind == AB_HIT_HAT &&
       intensity < ab_breakbeat_policy.hat_transport_min_intensity &&
       density < 0.70f)
        return 0;

    if(last <= 0)
    {
        ab_breakbeat_state.last_transport_action_ms = now;
        return 1;
    }

    age = now - last;
    if(age < 0)
        age = 0;

    min_gap = ab_breakbeat_transport_min_gap_ms(snap, hit_kind, repeated);
    if(age >= min_gap)
    {
        ab_breakbeat_state.last_transport_action_ms = now;
        return 1;
    }

    if(ab_is_body_hit(hit_kind))
    {
        period = ab_breakbeat_transport_period_ms(snap);

        if(age >= (long)(period * (double)ab_breakbeat_policy.early_escape_beats + 0.5) &&
           intensity > 0.78f)
        {
            ab_breakbeat_state.last_transport_action_ms = now;
            return 1;
        }
    }

    return 0;
}


static inline const char *ab_breakbeat_transport_block_reason(const vj_audio_beat_snapshot_t *snap,
                                                             long event_ms,
                                                             int hit_kind,
                                                             int repeated,
                                                             long repeat_ms,
                                                             int max_repeats,
                                                             int scratch_dir)
{
    long last = ab_breakbeat_state.last_transport_action_ms;
    long age;
    long min_gap;
    float intensity = ab_breakbeat_transport_intensity(snap, hit_kind);
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);

    (void)repeated;

    if(hit_kind == AB_HIT_HAT &&
       intensity < ab_breakbeat_policy.hat_transport_min_intensity &&
       density < 0.70f)
        return "hat-weak";

    if(ab_breakbeat_state.anchor_valid &&
       ab_breakbeat_state.last_hit_ms > 0 &&
       event_ms >= ab_breakbeat_state.last_hit_ms &&
       (event_ms - ab_breakbeat_state.last_hit_ms) <= repeat_ms &&
       ab_breakbeat_state.repeat_count >= max_repeats)
        return "repeat-limit";

    if(last <= 0)
        return "none";

    age = event_ms - last;
    if(age < 0)
        age = 0;

    min_gap = ab_breakbeat_transport_min_gap_ms(snap, hit_kind, repeated);
    if(age < min_gap)
    {
        if(hit_kind == AB_HIT_SCRATCH)
        {
            int last_dir = ab_breakbeat_state.scratch_transport_dir;
            if(last_dir != 0 && scratch_dir != 0 && scratch_dir != last_dir)
                return "scratch-turn-gate";
            return "scratch-gap";
        }
        return "transport-gap";
    }

    return "transport";
}

static inline int ab_breakbeat_scratch_transport_reversal_allowed(const vj_audio_beat_snapshot_t *snap,
                                                                  long now,
                                                                  int scratch_dir)
{
    long last_ms = ab_breakbeat_state.scratch_transport_ms;
    int last_dir = ab_breakbeat_state.scratch_transport_dir;
    long age;
    float amount;
    float velocity;
    float burst;
    float drive;
    float required;

    if(!snap || scratch_dir == 0 || last_dir == 0 || scratch_dir == last_dir || last_ms <= 0)
        return 0;

    age = now - last_ms;
    if(age < 0)
        age = 0;

    if(age < ab_breakbeat_policy.scratch_open_min_ms ||
       age > ab_breakbeat_policy.scratch_repeat_max_ms)
        return 0;

    amount = ab_breakbeat_clampf(snap->beat_density);
    velocity = ab_breakbeat_clampf(snap->flux);
    burst = ab_breakbeat_clampf(snap->transient);
    drive = ab_breakbeat_clampf(amount * 0.42f + velocity * 0.34f + burst * 0.24f);
    required = age <= ab_breakbeat_policy.scratch_open_max_ms ? 0.28f : 0.36f;

    if(drive < required && velocity < 0.48f && burst < 0.44f)
        return 0;

    return 1;
}

static inline float ab_breakbeat_scratch_block_escape_drive(float amount,
                                                            float velocity,
                                                            float burst)
{
    return ab_breakbeat_clampf(amount * 0.48f +
                              velocity * 0.34f +
                              burst * 0.18f);
}

static inline int ab_breakbeat_scratch_block_escape_allowed(const vj_audio_beat_snapshot_t *snap,
                                                            long event_age_ms,
                                                            float effective_late_frames,
                                                            int scratch_dir,
                                                            float amount,
                                                            float velocity,
                                                            float burst)
{
    float drive;

    if(!snap || snap->hit_kind != AB_HIT_SCRATCH)
        return 0;
    if(!ab_breakbeat_state.active)
        return 0;
    if(scratch_dir == 0 || ab_breakbeat_state.direction == 0)
        return 0;
    if(scratch_dir == ab_breakbeat_state.direction)
        return 0;
    if(event_age_ms < 0 || event_age_ms > AB_BREAKBEAT_SCRATCH_BLOCK_ESCAPE_MAX_AGE_MS)
        return 0;
    if(effective_late_frames > AB_BREAKBEAT_SCRATCH_BLOCK_ESCAPE_MAX_EFF_FRAMES)
        return 0;

    drive = ab_breakbeat_scratch_block_escape_drive(amount, velocity, burst);
    if(drive < 0.30f && velocity < 0.38f && burst < 0.34f)
        return 0;

    return 1;
}

static inline void ab_breakbeat_update_music_state(const vj_audio_beat_snapshot_t *snap, long now)
{
    float dt;
    float velocity;
    float activity;
    float body;
    float target;
    float alpha;
    float hit_power;
    long hit_interval;

    if(!snap)
        return;

    if(ab_breakbeat_state.music_last_ms <= 0)
    {
        ab_breakbeat_state.music_last_ms = now;
        ab_breakbeat_state.music_last_hit_seq = snap->hit_seq;
    }

    dt = (float)(now - ab_breakbeat_state.music_last_ms) * 0.001f;

    if(dt < 0.0f)
        dt = 0.0f;
    else if(dt > 0.25f)
        dt = 0.25f;

    ab_breakbeat_state.music_last_ms = now;

    velocity = ab_breakbeat_velocity(snap);
    activity = ab_breakbeat_activity(snap);
    body = snap->kick > snap->snare ? snap->kick : snap->snare;
    hit_power = velocity * 0.62f + activity * 0.24f + body * 0.14f;

    {
        float pace = ab_breakbeat_pace_drive(snap);
        float decay;

        if(activity < 0.025f)
            decay = 0.72f;
        else if(snap->beat_age_ms > 700)
            decay = 0.18f + (1.0f - pace) * 0.18f;
        else
            decay = 0.060f + (1.0f - pace) * 0.050f;

        ab_breakbeat_state.tempo_drive -= decay * dt;
        ab_breakbeat_state.rhythm_accent -= (0.85f + (activity < 0.08f ? 0.65f : 0.0f)) * dt;
        ab_breakbeat_state.rhythm_accel -= 0.95f * dt;

        if(snap->beat_age_ms > 1200)
        {
            ab_breakbeat_state.rhythm_density -= 0.32f * dt;
            ab_breakbeat_state.rhythm_regularity -= 0.24f * dt;
        }
    }

    target = activity * 0.44f + velocity * 0.26f + snap->beat_pulse * 0.13f;
    target = (target - 0.085f) * (1.0f / 0.86f);
    target = ab_breakbeat_clampf(target);
    target = target * (2.0f - target);

    alpha = target > ab_breakbeat_state.music_groove ? 2.00f * dt : 1.10f * dt;
    if(alpha > 0.32f)
        alpha = 0.32f;

    ab_breakbeat_state.music_groove +=
        (target - ab_breakbeat_state.music_groove) * alpha;

    if(snap->hit_seq != ab_breakbeat_state.music_last_hit_seq)
    {
        float impact_raw = velocity * 0.58f + activity * 0.18f + body * 0.24f;
        float impact = (impact_raw - 0.30f) * (1.0f / 0.64f);
        float accent = 0.0f;
        float accel = 0.0f;
        float density = 0.0f;
        float regularity = ab_breakbeat_state.rhythm_regularity;
        float sync;
        float expression;

        impact = ab_breakbeat_clampf(impact);

        hit_interval = ab_breakbeat_state.tempo_last_hit_ms > 0 ?
                       now - ab_breakbeat_state.tempo_last_hit_ms : 0;

        if(ab_breakbeat_state.rhythm_power_ema <= 0.001f)
            ab_breakbeat_state.rhythm_power_ema = hit_power;

        accent = (hit_power - ab_breakbeat_state.rhythm_power_ema + 0.055f) * (1.0f / 0.42f);
        accent = ab_breakbeat_clampf(accent) * (0.45f + impact * 0.55f);

        if(accent > ab_breakbeat_state.rhythm_accent)
            ab_breakbeat_state.rhythm_accent += (accent - ab_breakbeat_state.rhythm_accent) * 0.68f;
        else
            ab_breakbeat_state.rhythm_accent += (accent - ab_breakbeat_state.rhythm_accent) * 0.36f;

        ab_breakbeat_state.rhythm_power_ema +=
            (hit_power - ab_breakbeat_state.rhythm_power_ema) * (0.080f + ab_breakbeat_state.rhythm_accent * 0.15f);

        if(hit_interval > 0)
        {
            float pace = ab_breakbeat_pace_drive_from_interval(snap, hit_interval);
            float interval_f = (float)hit_interval;
            float ema = ab_breakbeat_state.rhythm_interval_ema_ms;
            float rel_err = 1.0f;
            float relock = 0.0f;

            density = ab_breakbeat_density_from_interval(snap, hit_interval);

            if(ema <= 1.0f)
            {
                ema = interval_f;
                rel_err = 0.0f;
            }
            else
            {
                rel_err = (float)fabs((double)(interval_f - ema));
                if(ema > 1.0f)
                    rel_err /= ema;

                if(rel_err > ab_breakbeat_policy.relock_rel_err)
                {
                    relock = ab_breakbeat_clampf((rel_err - ab_breakbeat_policy.relock_rel_err) /
                                                 (1.0f - ab_breakbeat_policy.relock_rel_err));
                    ema += (interval_f - ema) * (0.54f + relock * 0.34f);
                    ab_breakbeat_state.tempo_drive *= 1.0f - relock * 0.62f;
                    ab_breakbeat_state.rhythm_regularity *= 1.0f - relock * 0.76f;
                    ab_breakbeat_state.rhythm_accent *= 1.0f - relock * 0.38f;
                    ab_breakbeat_state.rhythm_accel *= 1.0f - relock * 0.45f;
                    ab_breakbeat_state.music_phrase *= 1.0f - relock * 0.34f;
                    ab_breakbeat_state.music_climax *= 1.0f - relock * 0.46f;
                }
                else
                {
                    ema += (interval_f - ema) * (0.10f + density * 0.07f);
                }
            }

            regularity = 1.0f - rel_err * (1.0f / ab_breakbeat_policy.regular_rel_err);
            regularity = ab_breakbeat_clampf(regularity);

            if(ab_breakbeat_state.tempo_prev_interval_ms > 0)
            {
                float prev = (float)ab_breakbeat_state.tempo_prev_interval_ms;
                float cur = interval_f;
                float delta = (prev - cur) / prev;

                if(delta > 0.0f)
                    accel = delta * (1.0f / ab_breakbeat_policy.relock_rel_err);
                else
                    ab_breakbeat_state.tempo_drive += delta * 0.18f;
            }

            accel = ab_breakbeat_clampf(accel);

            ab_breakbeat_state.rhythm_interval_ema_ms = ema;
            ab_breakbeat_state.rhythm_density += (density - ab_breakbeat_state.rhythm_density) * (0.20f + relock * 0.20f);
            ab_breakbeat_state.rhythm_regularity += (regularity - ab_breakbeat_state.rhythm_regularity) * (0.20f + relock * 0.32f);

            if(accel > ab_breakbeat_state.rhythm_accel)
                ab_breakbeat_state.rhythm_accel += (accel - ab_breakbeat_state.rhythm_accel) * (0.58f + relock * 0.22f);
            else
                ab_breakbeat_state.rhythm_accel += (accel - ab_breakbeat_state.rhythm_accel) * (0.26f + relock * 0.16f);

            sync = ab_breakbeat_syncopation_drive();
            expression = ab_breakbeat_expression_drive();

            {
                float tempo_target = hit_power * (density * 0.10f + accel * 0.24f + expression * 0.12f + pace * 0.06f);

                ab_breakbeat_state.tempo_drive +=
                    hit_power * (density * 0.018f + accel * 0.12f + expression * 0.038f + pace * 0.012f);

                if(ab_breakbeat_state.tempo_drive < tempo_target)
                {
                    float attack = 0.06f + accel * 0.18f + expression * 0.10f + density * 0.03f;

                    if(attack > 0.32f)
                        attack = 0.32f;

                    ab_breakbeat_state.tempo_drive +=
                        (tempo_target - ab_breakbeat_state.tempo_drive) * attack;
                }
            }

            if(hit_power > 0.78f && expression > 0.46f && relock < 0.25f)
                ab_breakbeat_state.tempo_drive += 0.006f;

            ab_breakbeat_state.tempo_prev_interval_ms = hit_interval;
        }
        else
        {
            sync = ab_breakbeat_syncopation_drive();
            expression = ab_breakbeat_expression_drive();
        }

        ab_breakbeat_state.tempo_last_hit_ms = now;

        sync = ab_breakbeat_syncopation_drive();
        expression = ab_breakbeat_expression_drive();

        if(ab_is_body_hit(snap->hit_kind))
        {
            float phrase_inc = impact * (0.010f + expression * 0.064f + sync * 0.016f) +
                               snap->beat_pulse * 0.005f;

            if(ab_breakbeat_state.rhythm_regularity > 0.66f && expression < 0.24f)
                phrase_inc *= 0.36f;

            ab_breakbeat_state.music_phrase += phrase_inc;

            if(impact > 0.10f &&
               (expression > 0.20f || sync > 0.30f || ab_breakbeat_state.music_phrase > 0.58f))
            {
                float climax_inc = impact * (0.006f + expression * 0.058f + sync * 0.012f) +
                                    ab_breakbeat_state.music_phrase * expression * 0.006f;

                if(ab_breakbeat_state.rhythm_regularity > 0.70f && expression < 0.32f)
                    climax_inc *= 0.42f;

                ab_breakbeat_state.music_climax += climax_inc;
            }
        }
        else if(snap->hit_kind == AB_HIT_HAT)
        {
            ab_breakbeat_state.music_phrase += impact * (0.003f + expression * 0.014f);

            if(impact > 0.62f && expression > 0.48f)
                ab_breakbeat_state.music_climax += impact * expression * 0.010f;
        }

        ab_breakbeat_state.music_last_hit_seq = snap->hit_seq;
    }

    target = ab_breakbeat_state.music_groove * 0.38f + activity * 0.085f +
             ab_breakbeat_expression_drive() * 0.10f;
    alpha = target > ab_breakbeat_state.music_phrase ? 0.25f * dt : 0.18f * dt;
    if(alpha > 0.10f)
        alpha = 0.10f;

    ab_breakbeat_state.music_phrase +=
        (target - ab_breakbeat_state.music_phrase) * alpha;

    ab_breakbeat_state.music_phrase -= (0.026f + (activity < 0.10f ? 0.070f : 0.0f)) * dt;

    target = velocity * 0.24f + activity * 0.17f +
             ab_breakbeat_state.music_groove * 0.10f +
             ab_breakbeat_state.music_phrase * 0.22f +
             ab_breakbeat_expression_drive() * 0.25f;
    target = (target - 0.30f) * (1.0f / 0.70f);
    target = ab_breakbeat_clampf(target) * 0.82f;

    alpha = target > ab_breakbeat_state.music_climax ? 0.46f * dt : 0.42f * dt;
    if(alpha > 0.16f)
        alpha = 0.16f;

    ab_breakbeat_state.music_climax +=
        (target - ab_breakbeat_state.music_climax) * alpha;

    if(activity < 0.025f)
        ab_breakbeat_state.music_climax -= 0.24f * dt;
    else if(snap->beat_age_ms > 900)
        ab_breakbeat_state.music_climax -= 0.075f * dt;
    else
        ab_breakbeat_state.music_climax -= 0.020f * dt;

    {
        float regular = ab_breakbeat_regular_drive();
        float expression = ab_breakbeat_expression_drive();
        float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);

        if(regular > 0.55f && expression < 0.34f && ab_breakbeat_state.tempo_drive < 0.74f)
        {
            float gate = (regular - 0.55f) * (1.0f / 0.45f);
            float phrase_cap = 0.40f + activity * 0.10f + density * 0.16f + expression * 0.36f;
            float climax_cap = 0.24f + velocity * 0.08f + density * 0.10f + expression * 0.42f;
            float pull = dt * (0.80f + gate * 1.35f);

            if(pull > 0.22f)
                pull = 0.22f;

            if(ab_breakbeat_state.music_phrase > phrase_cap)
                ab_breakbeat_state.music_phrase += (phrase_cap - ab_breakbeat_state.music_phrase) * pull;
            if(ab_breakbeat_state.music_climax > climax_cap)
                ab_breakbeat_state.music_climax += (climax_cap - ab_breakbeat_state.music_climax) * pull;
        }
    }

    {
        float regular = ab_breakbeat_regular_drive();
        float expression = ab_breakbeat_expression_drive();
        float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
        float accel = ab_breakbeat_accel_drive();

        if(regular > 0.58f && expression < 0.44f && accel < 0.42f)
        {
            float tempo_cap = 0.34f + density * 0.16f + ab_breakbeat_state.rhythm_accent * 0.10f;

            if(tempo_cap > 0.62f)
                tempo_cap = 0.62f;
            if(ab_breakbeat_state.tempo_drive > tempo_cap)
                ab_breakbeat_state.tempo_drive += (tempo_cap - ab_breakbeat_state.tempo_drive) * 0.22f;
        }
    }

    ab_breakbeat_state.music_groove = ab_breakbeat_clampf(ab_breakbeat_state.music_groove);
    ab_breakbeat_state.music_phrase = ab_breakbeat_clampf(ab_breakbeat_state.music_phrase);
    ab_breakbeat_state.music_climax = ab_breakbeat_clampf(ab_breakbeat_state.music_climax);
    ab_breakbeat_state.tempo_drive = ab_breakbeat_clampf(ab_breakbeat_state.tempo_drive);
    ab_breakbeat_state.rhythm_density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    ab_breakbeat_state.rhythm_regularity = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_regularity);
    ab_breakbeat_state.rhythm_accent = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_accent);
    ab_breakbeat_state.rhythm_accel = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_accel);
}

static inline void ab_breakbeat_clear_local_loop(void)
{
    ab_breakbeat_state.local_loop_active = 0;
    ab_breakbeat_state.local_loop_until_ms = 0;
    ab_breakbeat_state.loop_lo = 0;
    ab_breakbeat_state.loop_hi = 0;
}

static inline int ab_breakbeat_should_use_local_loop(const vj_audio_beat_snapshot_t *snap, int repeated)
{
    (void)snap;
    (void)repeated;
    return 0;
}

static inline void ab_breakbeat_apply_transport(veejay_t *v, int speed)
{
    vj_audio_beat_shared_t *s;
    int beat_owned_pause;

    if(!v || !v->settings)
        return;

    s = &v->settings->audio_beat;
    beat_owned_pause = ab_load_i(&s->paused_by_beat);

    if(speed != 0)
        speed = 1;

    if(v->settings->current_playback_speed == 0 && speed != 0)
    {
        if(!beat_owned_pause)
        {
            ab_store_l(&ab_breakbeat_user_override_until_ms, AB_BREAKBEAT_USER_PAUSE_OVERRIDE);
            ab_breakbeat_hit_queue_clear();
            ab_store_i(&s->consumed_seq, ab_load_i(&s->hit_seq));
            return;
        }

        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-BEAT][BREAK] taking over beat-owned pause requested speed=%d hit=%d consumed=%d hold=%ld",
                   speed,
                   ab_load_i(&s->hit_seq),
                   ab_load_i(&s->consumed_seq),
                   ab_load_l(&s->hold_until_ms));
    }

    if(v->settings->current_playback_speed != speed)
        ab_set_speed_from_beat(v, speed, 0);

    if(speed == 0)
        ab_store_i(&s->paused_by_beat, 1);
    else if(beat_owned_pause)
        ab_store_i(&s->paused_by_beat, 0);
}

static int ab_breakbeat_resume_owned_pause(veejay_t *v, vj_audio_beat_shared_t *s,
                                           long now, int hit_seq)
{
    int speed;

    (void)now;

    if(!v || !v->settings || !s)
        return 0;

    if(ab_breakbeat_state.active)
        return 0;

    if(!ab_load_i(&s->paused_by_beat))
        return 0;

    if(v->settings->current_playback_speed != 0)
        return 0;

    speed = ab_load_i(&s->resume_speed);
    if(speed == 0)
        speed = v->settings->previous_playback_speed;
    if(speed == 0)
        speed = 1;

    ab_breakbeat_reset_state();
    ab_store_i(&s->resume_speed, speed);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->consumed_seq, hit_seq >= 0 ? hit_seq : ab_load_i(&s->hit_seq));
    ab_store_l(&ab_breakbeat_user_override_until_ms, 0);

    ab_breakbeat_apply_transport(v, speed);
    ab_store_i(&s->paused_by_beat, 0);

    return 1;
}
static inline int ab_breakbeat_local_radius(int slice_frames, const vj_audio_beat_snapshot_t *snap)
{
    float climax = ab_breakbeat_climax_drive();
    float velocity = ab_breakbeat_velocity(snap);
    int radius = 10 + (slice_frames / 10) + (int)(velocity * 22.0f) + (int)(climax * 26.0f);

    if(snap && snap->hit_kind == AB_HIT_HAT)
    {
        radius = 2 + (int)(snap->hat * 5.0f) + (int)(snap->high * 4.0f);
        if(radius < 2)
            radius = 2;
        else if(radius > 12)
            radius = 12;
        return radius;
    }

    if(snap && snap->hit_kind == AB_HIT_SCRATCH)
    {
        radius = 1 + (int)(snap->beat_density * 5.0f) + (int)(snap->flux * 4.0f);
        if(radius < 1)
            radius = 1;
        else if(radius > 10)
            radius = 10;
        return radius;
    }

    if(radius < 8)
        radius = 8;
    else if(radius > 96)
        radius = 96;

    return radius;
}

static inline void ab_breakbeat_set_local_loop(veejay_t *v, long long center, int radius)
{
    long long lo = 0;
    long long hi = 0;
    long long a;
    long long b;

    if(!ab_breakbeat_bounds(v, &lo, &hi))
    {
        ab_breakbeat_state.loop_lo = center - radius;
        ab_breakbeat_state.loop_hi = center + radius;
        ab_breakbeat_state.local_loop_active = 1;
        return;
    }

    if(radius < 2)
        radius = 2;

    a = center - (long long)radius;
    b = center + (long long)radius;

    if(a < lo)
        a = lo;
    if(b > hi)
        b = hi;

    if(b <= a)
    {
        a = center;
        b = center;
        if(a > lo)
            a--;
        if(b < hi)
            b++;
    }

    ab_breakbeat_scene_guard_loop(v, center, &a, &b);

    ab_breakbeat_state.loop_lo = a;
    ab_breakbeat_state.loop_hi = b;
    ab_breakbeat_state.local_loop_active = 1;
}

static inline int ab_breakbeat_slice_frames(veejay_t *v, vj_audio_beat_shared_t *s, const vj_audio_beat_snapshot_t *snap)
{
    long pulse_ms = ab_breakbeat_param_ms(s, &s->pulse_ms, 20, 2000);
    double fps = (double)ab_breakbeat_base_fps(v);
    float climax = ab_breakbeat_climax_drive();
    float velocity = ab_breakbeat_velocity(snap);
    double boost = 1.0 + (double)climax * 0.85 + (double)velocity * 0.62;
    int frames;

    if(snap)
    {
        boost += (double)snap->transient * 0.42 +
                 (double)snap->flux * 0.25 +
                 (double)snap->bass * 0.18;

        if(snap->hit_kind == AB_HIT_KICK)
            boost += (double)snap->kick * 0.48;
        else if(snap->hit_kind == AB_HIT_SNARE)
            boost += (double)snap->snare * 0.44 + (double)snap->mid * 0.20;
        else if(snap->hit_kind == AB_HIT_FULL)
            boost += (double)(snap->kick > snap->snare ? snap->kick : snap->snare) * 0.34;
        else if(snap->hit_kind == AB_HIT_HAT)
            boost = 0.45 +
                    (double)snap->hat * 0.62 +
                    (double)snap->high * 0.24 +
                    (double)climax * 0.20;
        else if(snap->hit_kind == AB_HIT_SCRATCH)
            boost = 0.30 +
                    (double)snap->beat_density * 0.58 +
                    (double)snap->flux * 0.46 +
                    (double)snap->transient * 0.36;
    }

    if(boost < 0.38)
        boost = 0.38;
    else if(boost > 3.80)
        boost = 3.80;

    frames = (int)(((double)pulse_ms * fps * boost) / 1000.0 + 0.5);

    if(snap && snap->hit_kind == AB_HIT_HAT)
    {
        if(frames < 1)
            frames = 1;
        else if(frames > 22)
            frames = 22;
    }
    else if(snap && snap->hit_kind == AB_HIT_SCRATCH)
    {
        if(frames < 1)
            frames = 1;
        else if(frames > 18)
            frames = 18;
    }
    else
    {
        int lo = climax > 0.65f || velocity > 0.80f ? 8 : 4;
        int hi = climax > 0.70f ? 520 : 320;

        if(frames < lo)
            frames = lo;
        else if(frames > hi)
            frames = hi;
    }

    return frames;
}

static inline int ab_breakbeat_bound_slice_frames(veejay_t *v, vj_audio_beat_shared_t *s, int frames, const vj_audio_beat_snapshot_t *snap)
{
    int span = ab_breakbeat_span_frames(v);
    int cap;

    if(span <= 0)
        return frames;

    if(snap && snap->hit_kind == AB_HIT_SCRATCH)
    {
        cap = span / 24;
        if(cap < 2)
            cap = 2;
        else if(cap > 24)
            cap = 24;

        if(frames > cap)
            frames = cap;
        if(frames < 1)
            frames = 1;
        return frames;
    }

    if(snap && snap->hit_kind == AB_HIT_HAT)
    {
        cap = span / 18;
        if(cap < 2)
            cap = 2;
        else if(cap > 28)
            cap = 28;
    }
    else
    {
        long pulse_ms = ab_breakbeat_param_ms(s, &s->pulse_ms, 20, 2000);
        float climax = ab_breakbeat_climax_drive();
        float velocity = ab_breakbeat_velocity(snap);
        double pulse = (double)pulse_ms / 2000.0;
        double drama = 0.010 + pulse * 0.15 + (double)climax * 0.14 + (double)velocity * 0.060;
        int dramatic;

        if(drama > 0.42)
            drama = 0.42;

        dramatic = (int)((double)span * drama + 0.5);

        if(frames < dramatic)
            frames = dramatic;

        cap = (int)((double)span * (0.20 + (double)climax * 0.38 + (double)velocity * 0.18) + 0.5);
        if(cap < 8)
            cap = 8;
        else if(cap > 720)
            cap = 720;
    }

    if(frames > cap)
        frames = cap;
    if(frames < 1)
        frames = 1;

    return frames;
}

static inline int ab_breakbeat_add_quant_candidate(int *candidates, int count, int max_count, int value)
{
    if(value < 1 || count >= max_count)
        return count;

    for(int i = 0; i < count; i++)
        if(candidates[i] == value)
            return count;

    candidates[count++] = value;
    return count;
}

static inline int ab_breakbeat_quantize_slice_frames(veejay_t *v, int frames, const vj_audio_beat_snapshot_t *snap)
{
    int candidates[40];
    int count = 0;
    int best;
    int best_d;
    static const int fixed_grid[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768 };

    if(frames < 1)
        frames = 1;

    for(int i = 0; i < (int)(sizeof(fixed_grid) / sizeof(fixed_grid[0])); i++)
        count = ab_breakbeat_add_quant_candidate(candidates, count, 40, fixed_grid[i]);

    if(snap && snap->bpm > 1.0f)
    {
        double beat_frames = ((double)ab_breakbeat_base_fps(v) * 60.0) / (double)snap->bpm;
        static const double divs[] = { 0.125, 0.1666667, 0.25, 0.3333333, 0.5, 0.6666667, 1.0, 1.5, 2.0, 3.0, 4.0 };

        if(beat_frames >= 1.0 && beat_frames <= 400.0)
        {
            for(int i = 0; i < (int)(sizeof(divs) / sizeof(divs[0])); i++)
            {
                int q = (int)(beat_frames * divs[i] + 0.5);
                count = ab_breakbeat_add_quant_candidate(candidates, count, 40, q);
            }
        }
    }

    best = frames;
    best_d = INT_MAX;

    for(int i = 0; i < count; i++)
    {
        int c = candidates[i];
        int d;

        if(snap && snap->hit_kind == AB_HIT_HAT && c > 16)
            continue;

        d = c > frames ? c - frames : frames - c;
        if(d < best_d)
        {
            best = c;
            best_d = d;
        }
    }

    if(best < 1)
        best = 1;

    return best;
}

static inline float ab_breakbeat_hit_fps(veejay_t *v, const vj_audio_beat_snapshot_t *snap, int repeated, int hit_kind)
{
    float base_fps = ab_breakbeat_base_fps(v);
    float phrase = ab_breakbeat_phrase_drive();
    float climax = ab_breakbeat_climax_drive();
    float tempo = ab_breakbeat_state.tempo_drive;
    float pace = ab_breakbeat_pace_drive(snap);
    float velocity = ab_breakbeat_velocity(snap);
    float activity = ab_breakbeat_activity(snap);
    float percussive = ab_breakbeat_percussive_drive(snap, hit_kind);
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    float regular = ab_breakbeat_regular_drive();
    float accent = ab_breakbeat_accent_drive();
    float accel = ab_breakbeat_accel_drive();
    float expression = ab_breakbeat_expression_drive();
    float mul;

    if(tempo < pace * 0.22f)
        tempo = pace * 0.22f;

    if(hit_kind == AB_HIT_SCRATCH)
    {
        float scratch = snap ? snap->beat_density : 0.0f;
        float burst = snap ? snap->transient : 0.0f;
        float sv = snap ? snap->flux : velocity;

        mul = 0.46f + scratch * 0.36f + sv * 0.54f + burst * 0.26f;
        if(repeated)
            mul += 0.10f + burst * 0.10f;
        if(mul < 0.34f)
            mul = 0.34f;
        else if(mul > 1.72f)
            mul = 1.72f;

        return ab_breakbeat_clamp_effect_fps(v, base_fps * mul);
    }

    if(hit_kind == AB_HIT_HAT)
    {
        mul = 0.34f + density * 0.12f + velocity * 0.34f +
              phrase * 0.08f + climax * 0.12f + accent * 0.14f + tempo * 0.08f;

        if(mul < 0.28f)
            mul = 0.28f;
        else if(mul > 1.08f)
            mul = 1.08f;

        return ab_breakbeat_clamp_effect_fps(v, base_fps * mul);
    }

    mul = 0.50f +
          density * 0.20f +
          velocity * 0.24f +
          activity * 0.08f +
          tempo * 0.18f +
          phrase * 0.18f +
          climax * 0.34f +
          accent * 0.22f +
          accel * 0.16f;

    if(repeated)
        mul += expression * 0.05f + climax * 0.04f;

    if(hit_kind == AB_HIT_SNARE)
        mul += 0.035f + accent * 0.10f + expression * 0.06f;
    else if((hit_kind == AB_HIT_KICK || hit_kind == AB_HIT_FULL) &&
            (accent > 0.28f || phrase > 0.52f || climax > 0.48f))
        mul += 0.035f + accent * 0.07f + climax * 0.06f;

    if(phrase > 0.72f && climax > 0.68f && expression > 0.32f)
        mul += 0.12f + climax * 0.10f;

    if(activity < 0.16f && climax < 0.30f && tempo < 0.18f)
        mul *= 0.74f;

    if(regular > 0.58f && expression < 0.42f && accel < 0.28f && climax < 0.74f)
    {
        float gate = (regular - 0.58f) * (1.0f / 0.42f);
        float cap = 0.82f + density * 0.24f + accent * 0.18f + phrase * 0.08f + velocity * 0.08f;

        if(hit_kind == AB_HIT_SNARE)
            cap += 0.06f;

        if(cap > 1.34f)
            cap = 1.34f;

        cap += (1.0f - gate) * 0.18f;

        if(mul > cap)
            mul = cap;
    }

    if(expression > 0.50f || accel > 0.42f || (phrase > 0.74f && climax > 0.68f))
    {
        float expressive_floor = 0.78f + density * 0.20f + expression * 0.26f + accel * 0.18f;

        if(percussive < 0.46f && expressive_floor > 0.92f)
            expressive_floor = 0.92f;
        else if(percussive < 0.60f && expressive_floor > 1.08f)
            expressive_floor = 1.08f;

        if(mul < expressive_floor)
            mul = expressive_floor;
    }

    if(ab_breakbeat_tonal_transport_hit(snap, hit_kind))
    {
        float tonal_cap = 0.72f + velocity * 0.10f + expression * 0.10f +
                          accel * 0.08f + climax * 0.06f;

        if(tonal_cap > 1.02f)
            tonal_cap = 1.02f;

        if(mul > tonal_cap)
            mul = tonal_cap;
    }

    if(percussive < 0.40f && mul > 0.94f)
        mul = 0.94f;
    else if(percussive < 0.55f && expression < 0.54f && accel < 0.50f && mul > 1.14f)
        mul = 1.14f;
    else if(percussive < 0.66f && expression < 0.46f && accel < 0.42f && mul > 1.28f)
        mul = 1.28f;

    if(regular > 0.52f && expression < 0.50f && accel < 0.46f && mul > 1.72f)
        mul = 1.72f + climax * 0.12f + accent * 0.08f;

    if(mul < 0.32f)
        mul = 0.32f;
    else if(mul > 2.55f)
        mul = 2.55f;

    return ab_breakbeat_clamp_effect_fps(v, base_fps * mul);
}

static inline float ab_breakbeat_fallback_fps(veejay_t *v, const vj_audio_beat_snapshot_t *snap)
{
    float base_fps = ab_breakbeat_base_fps(v);
    float groove = ab_breakbeat_groove_drive();
    float phrase = ab_breakbeat_phrase_drive();
    float climax = ab_breakbeat_climax_drive();
    float tempo = ab_breakbeat_state.tempo_drive;
    float pace = ab_breakbeat_pace_drive(snap);
    float velocity = ab_breakbeat_velocity(snap);
    float activity = ab_breakbeat_activity(snap);
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    float regular = ab_breakbeat_regular_drive();
    float accent = ab_breakbeat_accent_drive();
    float accel = ab_breakbeat_accel_drive();
    float expression = ab_breakbeat_expression_drive();
    float mul;

    if(tempo < pace * 0.20f)
        tempo = pace * 0.20f;

    mul = 0.19f +
          density * 0.18f +
          groove * 0.12f +
          phrase * 0.14f +
          climax * 0.24f +
          tempo * 0.18f +
          velocity * 0.08f +
          activity * 0.09f +
          expression * 0.10f;

    if(snap && snap->beat_age_ms > 1200)
        mul *= 0.54f;
    else if(snap && snap->beat_age_ms > 700)
        mul *= 0.72f;

    if(activity < 0.055f && tempo < 0.16f)
        mul = 0.20f;
    else if(activity < 0.14f && climax < 0.25f && tempo < 0.28f)
        mul *= 0.64f;

    if(pace > 0.0f && snap && snap->beat_age_ms < 1600)
    {
        float floor_mul = 0.17f + density * 0.14f + pace * 0.10f + tempo * 0.06f;

        if(mul < floor_mul)
            mul = floor_mul;
    }

    if(regular > 0.58f && expression < 0.40f && snap && snap->beat_age_ms < 900)
    {
        float floor_mul = 0.54f + density * 0.16f + accent * 0.05f + accel * 0.05f;
        float cap_mul = 0.88f + density * 0.13f + phrase * 0.05f + climax * 0.04f;

        if(mul < floor_mul)
            mul = floor_mul;
        if(mul > cap_mul)
            mul = cap_mul;
    }

    if(regular > 0.60f && expression < 0.34f && accel < 0.34f)
        mul *= 0.92f;

    if(mul < 0.20f)
        mul = 0.20f;
    else if(mul > 0.86f)
        mul = 0.86f;

    return ab_breakbeat_clamp_effect_fps(v, base_fps * mul);
}

static inline float ab_breakbeat_recent_fallback_floor(veejay_t *v, float fps, long now, const vj_audio_beat_snapshot_t *snap)
{
    long age;
    long hold_ms;
    float base;
    float floor_fps;
    float hold;
    float pace;
    float steady;

    if(ab_breakbeat_state.last_hit_ms <= 0 || ab_breakbeat_state.burst_fps <= 0.01f)
        return fps;

    age = now - ab_breakbeat_state.last_hit_ms;
    hold_ms = ab_breakbeat_ms_from_beats(snap,
                                         ab_breakbeat_policy.recent_floor_hold_beats,
                                         160L,
                                         1200L);

    if(age < 0 || age > hold_ms)
        return fps;

    base = ab_breakbeat_base_fps(v);
    hold = 1.0f - ((float)age / (float)hold_ms);
    hold = ab_breakbeat_clampf(hold);
    pace = ab_breakbeat_pace_drive(snap);
    steady = ab_breakbeat_steady_drive();

    floor_fps = ab_breakbeat_state.burst_fps *
                (0.18f + hold * 0.18f +
                 ab_breakbeat_state.tempo_drive * 0.06f +
                 pace * 0.04f);

    if(floor_fps < base * (0.22f + ab_breakbeat_state.tempo_drive * 0.12f + pace * 0.10f))
        floor_fps = base * (0.22f + ab_breakbeat_state.tempo_drive * 0.12f + pace * 0.10f);

    if(steady > 0.0f)
    {
        float cap = base * (0.72f + ab_breakbeat_state.rhythm_density * 0.18f +
                            pace * 0.06f + ab_breakbeat_state.tempo_drive * 0.04f);

        cap += base * (1.0f - steady) * 0.16f;

        if(floor_fps > cap)
            floor_fps = cap;
    }

    floor_fps = ab_breakbeat_clamp_effect_fps(v, floor_fps);

    return fps < floor_fps ? floor_fps : fps;
}

static inline long ab_breakbeat_repeat_window_ms(vj_audio_beat_shared_t *s, const vj_audio_beat_snapshot_t *snap)
{
    long gate_ms = ab_breakbeat_param_ms(s, &s->gate_ms, 10, 1000);
    float climax = ab_breakbeat_climax_drive();
    long window = gate_ms;

    if(snap && snap->hit_kind == AB_HIT_SCRATCH)
    {
        float amount = snap->beat_density;
        float burst = snap->transient;
        float drive = ab_breakbeat_clampf(amount * 0.62f + burst * 0.38f);

        window = ab_breakbeat_policy.scratch_repeat_min_ms +
                 (long)((double)(ab_breakbeat_policy.scratch_repeat_max_ms -
                                 ab_breakbeat_policy.scratch_repeat_min_ms) *
                        (double)drive + 0.5);
        return window;
    }

    if(snap)
    {
        float beats = ab_breakbeat_policy.repeat_body_beats +
                      climax * ab_breakbeat_policy.repeat_climax_extra_beats;
        long musical = ab_breakbeat_ms_from_beats(snap, beats, gate_ms, ab_breakbeat_policy.repeat_max_ms);

        if(window < musical)
            window = musical;
    }

    if(window > ab_breakbeat_policy.repeat_max_ms)
        window = ab_breakbeat_policy.repeat_max_ms;

    return window;
}

static inline int ab_breakbeat_max_repeats(vj_audio_beat_shared_t *s)
{
    long gate_ms = ab_breakbeat_param_ms(s, &s->gate_ms, 10, 1000);
    float climax = ab_breakbeat_climax_drive();
    int repeats = 3 + (int)(gate_ms / 150L) + (int)(climax * 7.0f);

    if(repeats < 3)
        repeats = 3;
    else if(repeats > 16)
        repeats = 16;

    return repeats;
}

static inline int ab_breakbeat_seek_projected(veejay_t *v, long long frame, int *dir)
{
    int park = 0;

    frame = ab_breakbeat_project_frame(v, frame, dir, &park);

    if(frame > LONG_MAX)
        frame = LONG_MAX;

    veejay_set_frame(v, (long)frame);

    return park;
}

static inline void ab_breakbeat_seek(veejay_t *v, long long frame)
{
    (void)ab_breakbeat_seek_projected(v, frame, NULL);
}

static void ab_breakbeat_release_transport(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int user_parked = 0;

    if(!ab_breakbeat_state.active)
        return;

    if(v && v->settings && s)
        user_parked = (v->settings->current_playback_speed == 0 &&
                       !ab_load_i(&s->paused_by_beat));

    ab_breakbeat_restore_fps(v);

    if(!user_parked && ab_breakbeat_state.saved_speed != 0)
        ab_set_speed_from_beat(v, ab_breakbeat_state.saved_speed, 0);

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_breakbeat_reset_state();
}

int vj_audio_beat_release_transport(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int action;
    int was_paused;
    int was_breakbeat_active;
    long hold_until;
    int changed = 0;

    if(!s)
        return 0;

    action = ab_load_i(&s->action_mode);
    was_paused = ab_load_i(&s->paused_by_beat);
    was_breakbeat_active = ab_breakbeat_state.active;
    hold_until = ab_load_l(&s->hold_until_ms);

    if(was_breakbeat_active)
    {
        ab_breakbeat_release_transport(v, s);
        changed = 1;
    }
    else if(was_paused)
    {
        ab_resume_from_consumer(v, s);
        changed = 1;
    }
    else if(hold_until > 0)
    {
        ab_store_l(&s->hold_until_ms, 0);
        changed = 1;
    }

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->consumed_seq, ab_load_i(&s->hit_seq));

    if(ab_action_is_breakbeat(action) && changed)
        ab_store_l(&ab_breakbeat_user_override_until_ms, ab_now_ms() + 350L);

    if(changed)
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-BEAT] released beat transport ownership action=%s(%d) paused=%d break_active=%d hold=%ld",
                   ab_action_name(action),
                   action,
                   was_paused,
                   was_breakbeat_active,
                   hold_until);

    return changed;
}

int vj_audio_beat_disable_for_transport(veejay_t *v, vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    vj_audio_beat_release_transport(v, s);
    return vj_audio_beat_disable(s);
}

void vj_audio_beat_user_transport_override(veejay_t *v, vj_audio_beat_shared_t *s,
                                           int requested_speed)
{
    int action;
    int was_paused;
    int was_breakbeat_active;
    int manual_pause_active;
    int hit_seq;
    long hold_until;
    int force_manual_pause;

    if(!s)
        return;

    action = ab_load_i(&s->action_mode);
    was_paused = ab_load_i(&s->paused_by_beat);
    was_breakbeat_active = ab_breakbeat_state.active;
    manual_pause_active = ab_breakbeat_user_pause_override_active();
    hit_seq = ab_load_i(&s->hit_seq);
    hold_until = ab_load_l(&s->hold_until_ms);
    force_manual_pause = requested_speed == 0;

    if(!was_paused &&
       !was_breakbeat_active &&
       !manual_pause_active &&
       hold_until <= 0 &&
       !force_manual_pause)
        return;

    if(was_breakbeat_active)
    {
        ab_breakbeat_restore_fps(v);
        ab_breakbeat_reset_state();
    }
    else if(ab_action_is_breakbeat(action))
    {
        ab_breakbeat_reset_state();
    }

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->resume_speed, requested_speed);
    ab_store_i(&s->consumed_seq, hit_seq);

    if(force_manual_pause)
        ab_clear_published_control(s);

    if(ab_action_is_breakbeat(action))
    {
        if(force_manual_pause)
            ab_store_l(&ab_breakbeat_user_override_until_ms, AB_BREAKBEAT_USER_PAUSE_OVERRIDE);
        else
            ab_store_l(&ab_breakbeat_user_override_until_ms, ab_now_ms() + 350L);
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[AUDIO-BEAT] user transport override req=%d action=%s(%d) paused=%d break_active=%d hold=%ld manual_pause=%d",
               requested_speed,
               ab_action_name(action),
               action,
               was_paused,
               was_breakbeat_active,
               hold_until,
               force_manual_pause);
}

static int ab_breakbeat_fallback_if_due(veejay_t *v, vj_audio_beat_shared_t *s, long now)
{
    vj_audio_beat_snapshot_t snap;
    long hold_until;
    float fb_fps;
    float base_fps;

    if(!ab_breakbeat_state.active || !ab_breakbeat_state.anchor_valid)
        return 0;

    hold_until = ab_load_l(&s->hold_until_ms);

    if(hold_until <= 0)
        return 0;

    if(now < hold_until)
    {
        if(ab_breakbeat_state.burst_fps > 0.01f &&
           (ab_breakbeat_state.burst_until_ms <= 0 || now <= ab_breakbeat_state.burst_until_ms))
            ab_breakbeat_apply_fps(v, ab_breakbeat_state.burst_fps);

        ab_breakbeat_clear_local_loop();
        ab_breakbeat_state.repeat_count = 0;
        ab_breakbeat_state.direction = 1;
        ab_breakbeat_state.fallback_dir = 1;
        ab_store_i(&s->resume_speed, 1);
        ab_breakbeat_apply_transport(v, 1);
        return 1;
    }

    if(!vj_audio_beat_get_snapshot(s, &snap))
        memset(&snap, 0, sizeof(snap));

    ab_breakbeat_update_music_state(&snap, now);
    ab_breakbeat_clear_local_loop();
    ab_breakbeat_state.repeat_count = 0;
    ab_breakbeat_state.fallback_active = 0;
    ab_breakbeat_state.direction = 1;
    ab_breakbeat_state.fallback_dir = 1;
    ab_store_l(&s->hold_until_ms, now + 35);
    ab_store_i(&s->resume_speed, 1);

    fb_fps = ab_breakbeat_fallback_fps(v, &snap);
    fb_fps = ab_breakbeat_recent_fallback_floor(v, fb_fps, now, &snap);

    base_fps = ab_breakbeat_base_fps(v);
    if(base_fps < 1.0f)
        base_fps = 25.0f;

    if(fb_fps < base_fps * 0.46f)
        fb_fps = base_fps * 0.46f;
    if(fb_fps > base_fps)
        fb_fps = base_fps;

    ab_breakbeat_apply_fps(v, fb_fps);
    ab_breakbeat_apply_transport(v, 1);

    return 1;
}



#if VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE
static void ab_breakbeat_trace_consume(vj_audio_beat_shared_t *s,
                                       const char *path,
                                       const char *block_reason,
                                       const vj_audio_beat_snapshot_t *snap,
                                       long now,
                                       long event_ms,
                                       long publish_ms,
                                       long block_ms,
                                       long stale_ms,
                                       long open_ms,
                                       long repeat_ms,
                                       int repeated,
                                       int tonal,
                                       int allowed,
                                       int base_speed,
                                       int run_speed,
                                       int slice_frames,
                                       int max_repeats,
                                       long long cur,
                                       long long target,
                                       float target_fps)
{
    static long last_trace_ms = 0;
    static int last_seq = -1;
    long event_age_ms;
    long publish_age_ms;
    long consume_age_ms;
    long center_age_ms;
    int seq;
    int qread;
    int qwrite;
    int qdepth;
    float scratch_visual_env;
    int scratch_visual_dir;
    float scratch_visual_dir_signal;
    float scratch_visual_signed;
    long scratch_visual_last_ms;
    long scratch_visual_hold_until_ms;
    long scratch_visual_decay_until_ms;
    long scratch_visual_age_ms;
    long scratch_visual_hold_ms;
    long scratch_visual_decay_ms;
    long output_latency_ms;
    long heard_latency_ms;
    long monitor_latency_ms;
    long speaker_offset_ms;
    float latency_frames;
    float speaker_offset_frames;
    float effective_late_frames;

    if(!s || !ab_action_is_breakbeat(ab_load_i(&s->action_mode)))
        return;

    seq = snap ? snap->hit_seq : 0;
    if(seq == last_seq && last_trace_ms > 0 &&
       (now - last_trace_ms) < VEEJAY_AUDIO_BEAT_BREAKBEAT_TRACE_INTERVAL_MS)
        return;

    last_seq = seq;
    last_trace_ms = now;

    event_age_ms = event_ms > 0 && now >= event_ms ? now - event_ms : 0;
    if(publish_ms <= 0 || publish_ms < event_ms)
        publish_ms = event_ms;
    publish_age_ms = publish_ms >= event_ms ? publish_ms - event_ms : 0;
    consume_age_ms = now >= publish_ms ? now - publish_ms : 0;
    center_age_ms = block_ms > 0 ? event_age_ms + (block_ms / 2L) : event_age_ms;
    latency_frames = ab_breakbeat_latency_frames_from_age(event_age_ms);
    output_latency_ms = atomic_load_int(&ab_output_latency_ms);
    heard_latency_ms = atomic_load_int(&ab_heard_latency_ms);
    monitor_latency_ms = atomic_load_int(&ab_monitor_latency_ms);
    if(heard_latency_ms < 0)
        speaker_offset_ms = 0;
    else
        speaker_offset_ms = event_age_ms - heard_latency_ms;
    speaker_offset_frames = heard_latency_ms < 0 ? 0.0f : ab_breakbeat_signed_frames_from_ms(speaker_offset_ms);
    effective_late_frames = ab_breakbeat_effective_late_frames(event_age_ms);
    qread = ab_breakbeat_hit_queue_read;
    qwrite = ab_breakbeat_hit_queue_write;
    qdepth = qwrite >= qread ? qwrite - qread : AB_BREAKBEAT_HIT_QUEUE_SIZE - qread + qwrite;

    scratch_visual_env = ab_scratch_visual_env_now(now);
    scratch_visual_dir = ab_scratch_visual_dir_now(now);
    scratch_visual_dir_signal = ab_scratch_visual_direction_signal(now);
    scratch_visual_signed = ab_scratch_visual_signed_signal(now);
    scratch_visual_last_ms = ab_load_l(&ab_scratch_visual_last_ms);
    scratch_visual_hold_until_ms = ab_load_l(&ab_scratch_visual_hold_until_ms);
    scratch_visual_decay_until_ms = ab_load_l(&ab_scratch_visual_decay_until_ms);
    scratch_visual_age_ms = scratch_visual_last_ms > 0 && now >= scratch_visual_last_ms ? now - scratch_visual_last_ms : 0;
    scratch_visual_hold_ms = scratch_visual_hold_until_ms > now ? scratch_visual_hold_until_ms - now : 0;
    scratch_visual_decay_ms = scratch_visual_decay_until_ms > now ? scratch_visual_decay_until_ms - now : 0;

    AB_BREAK_TRACE(
        "consume path=%s seq=%d kind=%s age=%ld latblk=%ld latpub=%ld latcons=%ld latcenter=%ld latframes=%.2f audq=%ld moncfg=%ld hearq=%ld spkoff=%ld spkframes=%.2f effframes=%.2f stale=%ld open=%ld repeat=%ld rep=%d tonal=%d allow=%d block=%s active=%d dir=%d repcnt=%d loop=%d base=%d run=%d slice=%d maxrep=%d cur=%lld target=%lld fps=%.2f bpm=%.1f lvl=%.3f env=%.3f flux=%.3f tr=%.3f k=%.3f sn=%.3f hat=%.3f bass=%.3f mid=%.3f high=%.3f pulse=%.3f gate=%.3f dens=%.3f senv=%.3f sdir=%d sdirsig=%.3f ssigned=%.3f shold=%ld sdecay=%ld sage=%ld rdens=%.3f reg=%.3f accent=%.3f accel=%.3f tempo=%.3f groove=%.3f phrase=%.3f climax=%.3f q=%d/%d/%d",
        path ? path : "event",
        seq,
        ab_hit_kind_name(snap ? snap->hit_kind : AB_HIT_NONE),
        event_age_ms,
        block_ms,
        publish_age_ms,
        consume_age_ms,
        center_age_ms,
        latency_frames,
        output_latency_ms,
        monitor_latency_ms,
        heard_latency_ms,
        speaker_offset_ms,
        speaker_offset_frames,
        effective_late_frames,
        stale_ms,
        open_ms,
        repeat_ms,
        repeated,
        tonal,
        allowed,
        block_reason ? block_reason : "none",
        ab_breakbeat_state.active,
        ab_breakbeat_state.direction,
        ab_breakbeat_state.repeat_count,
        ab_breakbeat_state.local_loop_active,
        base_speed,
        run_speed,
        slice_frames,
        max_repeats,
        cur,
        target,
        target_fps,
        snap ? snap->bpm : 0.0f,
        snap ? snap->level : 0.0f,
        snap ? snap->envelope : 0.0f,
        snap ? snap->flux : 0.0f,
        snap ? snap->transient : 0.0f,
        snap ? snap->kick : 0.0f,
        snap ? snap->snare : 0.0f,
        snap ? snap->hat : 0.0f,
        snap ? snap->bass : 0.0f,
        snap ? snap->mid : 0.0f,
        snap ? snap->high : 0.0f,
        snap ? snap->beat_pulse : 0.0f,
        snap ? snap->beat_gate : 0.0f,
        snap ? snap->beat_density : 0.0f,
        scratch_visual_env,
        scratch_visual_dir,
        scratch_visual_dir_signal,
        scratch_visual_signed,
        scratch_visual_hold_ms,
        scratch_visual_decay_ms,
        scratch_visual_age_ms,
        ab_breakbeat_state.rhythm_density,
        ab_breakbeat_state.rhythm_regularity,
        ab_breakbeat_state.rhythm_accent,
        ab_breakbeat_state.rhythm_accel,
        ab_breakbeat_state.tempo_drive,
        ab_breakbeat_state.music_groove,
        ab_breakbeat_state.music_phrase,
        ab_breakbeat_state.music_climax,
        qread,
        qwrite,
        qdepth);
}
#else
#define ab_breakbeat_trace_consume(s, path, block_reason, snap, now, event_ms, publish_ms, block_ms, stale_ms, open_ms, repeat_ms, repeated, tonal, allowed, base_speed, run_speed, slice_frames, max_repeats, cur, target, target_fps) do { } while(0)
#endif

#if VEEJAY_AUDIO_BEAT_BREAKBEAT_FRAME_DEBUG
static inline const char *ab_breakbeat_debug_hit_name(int hit_kind)
{
    return ab_hit_kind_name(hit_kind);
}

static inline float ab_breakbeat_debug_body_drive(const vj_audio_beat_snapshot_t *snap)
{
    float percussive;
    float motion;
    float tonal;
    float groove;
    float body;

    if(!snap)
        return 0.0f;

    percussive = ab_breakbeat_percussive_drive(snap, snap->hit_kind);
    motion = snap->transient * 0.28f + snap->flux * 0.18f +
             snap->beat_pulse * 0.22f + snap->beat_density * 0.10f;
    tonal = ab_breakbeat_tonal_bias(snap) * 0.06f;
    groove = ab_breakbeat_state.rhythm_accent * 0.08f +
             ab_breakbeat_state.rhythm_accel * 0.08f +
             ab_breakbeat_state.tempo_drive * 0.06f;

    body = percussive * 0.38f + motion + tonal + groove;
    return ab_breakbeat_clampf(body);
}

static inline float ab_breakbeat_debug_body_gate(const vj_audio_beat_snapshot_t *snap, int hit_kind)
{
    float gate = 0.34f;

    if(ab_is_body_hit(hit_kind))
        gate -= 0.045f;
    if(snap && snap->beat_gate > 0.5f)
        gate -= 0.030f;
    if(snap)
        gate -= ab_breakbeat_clampf(snap->beat_density) * 0.030f;
    gate -= ab_breakbeat_state.rhythm_accent * 0.030f;
    gate -= ab_breakbeat_state.rhythm_accel * 0.025f;
    gate += ab_breakbeat_regular_drive() * 0.020f;

    if(gate < 0.20f)
        gate = 0.20f;
    else if(gate > 0.42f)
        gate = 0.42f;

    return gate;
}

static inline float ab_breakbeat_debug_section_energy(const vj_audio_beat_snapshot_t *snap)
{
    float body;
    float percussive;
    float tone;
    float rhythm;
    float state;

    if(!snap)
        return 0.0f;

    body = ab_breakbeat_debug_body_drive(snap);
    percussive = ab_breakbeat_percussive_drive(snap, snap->hit_kind);
    tone = ab_breakbeat_tonal_bias(snap);
    rhythm = ab_breakbeat_state.rhythm_density * 0.26f +
             ab_breakbeat_state.rhythm_regularity * 0.14f +
             ab_breakbeat_state.rhythm_accent * 0.18f +
             ab_breakbeat_state.rhythm_accel * 0.20f +
             ab_breakbeat_state.tempo_drive * 0.12f;
    state = ab_breakbeat_state.music_groove * 0.14f +
            ab_breakbeat_state.music_phrase * 0.16f +
            ab_breakbeat_state.music_climax * 0.24f;

    return ab_breakbeat_clampf(body * 0.44f + percussive * 0.20f +
                               snap->beat_pulse * 0.16f + rhythm + state +
                               tone * 0.04f);
}

static inline float ab_breakbeat_debug_section_gate(const vj_audio_beat_snapshot_t *snap)
{
    float regular = ab_breakbeat_regular_drive();
    float accel = ab_breakbeat_accel_drive();
    float density = ab_breakbeat_clampf(ab_breakbeat_state.rhythm_density);
    float gate = 0.30f + regular * 0.04f - accel * 0.035f - density * 0.025f;

    if(snap && snap->beat_gate > 0.5f)
        gate -= 0.020f;
    if(gate < 0.22f)
        gate = 0.22f;
    else if(gate > 0.38f)
        gate = 0.38f;

    return gate;
}

static inline float ab_breakbeat_debug_forward_drive(const vj_audio_beat_snapshot_t *snap)
{
    float body;
    float section;
    float velocity;
    float expression;

    if(!snap)
        return 0.0f;

    body = ab_breakbeat_debug_body_drive(snap);
    section = ab_breakbeat_debug_section_energy(snap);
    velocity = ab_breakbeat_velocity(snap);
    expression = ab_breakbeat_expression_drive();

    return ab_breakbeat_clampf(body * 0.30f + section * 0.34f +
                               velocity * 0.16f + expression * 0.12f +
                               ab_breakbeat_state.tempo_drive * 0.08f);
}

static inline float ab_breakbeat_debug_forward_gate(const vj_audio_beat_snapshot_t *snap)
{
    float regular = ab_breakbeat_regular_drive();
    float expression = ab_breakbeat_expression_drive();
    float gate = 0.54f + regular * 0.10f - expression * 0.06f;

    if(snap && snap->bpm > 210.0f)
        gate += 0.035f;
    if(gate < 0.46f)
        gate = 0.46f;
    else if(gate > 0.68f)
        gate = 0.68f;

    return gate;
}

typedef struct
{
    const char *path;
    int event_valid;
    int hit_kind;
    int repeated;
    int tonal;
    int forward;
    int allowed;
    int base_speed;
    int run_speed;
    int slice_frames;
    int max_repeats;
    long open_ms;
    long repeat_ms;
    long stale_ms;
    long event_age_ms;
    long long cur = 0;
    long long target = 0;
    float target_fps;
    float body_drive;
    float body_gate;
    float section_energy;
    float section_gate;
    float forward_drive;
    float forward_gate;
    int section_hot;
} ab_breakbeat_frame_debug_t;

static ab_breakbeat_frame_debug_t ab_breakbeat_frame_debug;

static inline void ab_breakbeat_debug_reset_frame(void)
{
    memset(&ab_breakbeat_frame_debug, 0, sizeof(ab_breakbeat_frame_debug));
    ab_breakbeat_frame_debug.path = "idle";
    ab_breakbeat_frame_debug.hit_kind = AB_HIT_NONE;
    ab_breakbeat_frame_debug.allowed = 1;
}

static inline void ab_breakbeat_debug_note_simple(const char *path)
{
    ab_breakbeat_frame_debug.path = path ? path : "idle";
}

static inline void ab_breakbeat_debug_note_event(const char *path,
                                                 const vj_audio_beat_snapshot_t *snap,
                                                 int repeated,
                                                 int tonal,
                                                 int forward,
                                                 int allowed,
                                                 int base_speed,
                                                 int run_speed,
                                                 int slice_frames,
                                                 int max_repeats,
                                                 long open_ms,
                                                 long repeat_ms,
                                                 long stale_ms,
                                                 long event_age_ms,
                                                 long long cur,
                                                 long long target,
                                                 float target_fps)
{
    ab_breakbeat_frame_debug.path = path ? path : "event";
    ab_breakbeat_frame_debug.event_valid = 1;
    ab_breakbeat_frame_debug.hit_kind = snap ? snap->hit_kind : AB_HIT_NONE;
    ab_breakbeat_frame_debug.repeated = repeated;
    ab_breakbeat_frame_debug.tonal = tonal;
    ab_breakbeat_frame_debug.forward = forward;
    ab_breakbeat_frame_debug.allowed = allowed;
    ab_breakbeat_frame_debug.base_speed = base_speed;
    ab_breakbeat_frame_debug.run_speed = run_speed;
    ab_breakbeat_frame_debug.slice_frames = slice_frames;
    ab_breakbeat_frame_debug.max_repeats = max_repeats;
    ab_breakbeat_frame_debug.open_ms = open_ms;
    ab_breakbeat_frame_debug.repeat_ms = repeat_ms;
    ab_breakbeat_frame_debug.stale_ms = stale_ms;
    ab_breakbeat_frame_debug.event_age_ms = event_age_ms;
    ab_breakbeat_frame_debug.cur = cur;
    ab_breakbeat_frame_debug.target = target;
    ab_breakbeat_frame_debug.target_fps = target_fps;

    if(snap)
    {
        ab_breakbeat_frame_debug.body_drive = ab_breakbeat_debug_body_drive(snap);
        ab_breakbeat_frame_debug.body_gate = ab_breakbeat_debug_body_gate(snap, snap->hit_kind);
        ab_breakbeat_frame_debug.section_energy = ab_breakbeat_debug_section_energy(snap);
        ab_breakbeat_frame_debug.section_gate = ab_breakbeat_debug_section_gate(snap);
        ab_breakbeat_frame_debug.forward_drive = ab_breakbeat_debug_forward_drive(snap);
        ab_breakbeat_frame_debug.forward_gate = ab_breakbeat_debug_forward_gate(snap);
        ab_breakbeat_frame_debug.section_hot =
            ab_breakbeat_frame_debug.section_energy >= ab_breakbeat_frame_debug.section_gate;
    }
}

static void ab_breakbeat_debug_trace_frame(veejay_t *v,
                                           vj_audio_beat_shared_t *s,
                                           long now,
                                           int result,
                                           int hit_seq_before,
                                           int consumed_seq_before)
{
    vj_audio_beat_snapshot_t snap;
    long long frame = 0;
    long hold_until = 0;
    long hold_left = 0;
    long hit_age = -1L;
    long action_age = -1L;
    long burst_left = 0L;
    int speed = 0;
    int resume_speed = 0;
    int paused = 0;
    int hit_seq_after = 0;
    int consumed_seq_after = 0;
    int action = 0;
    int qread;
    int qwrite;
    int qdepth;
    int dbg_kind;
    float runtime_fps = 0.0f;
    float base_fps = 0.0f;
    float current_fps = 0.0f;
    float target_fps = 0.0f;
    float burst_fps = 0.0f;
    float effective = 0.0f;
    float target_effective = 0.0f;
    float body;
    float body_gate;
    float section;
    float section_gate;
    float forward_drive;
    float forward_gate;
    float fallback_fps;
    float recent_fps;
    double transport_period;
    double transport_bpm;

    memset(&snap, 0, sizeof(snap));

    if(v && v->settings)
    {
        frame = ab_breakbeat_current_frame(v);
        speed = v->settings->current_playback_speed;
    }

    if(s)
    {
        vj_audio_beat_get_snapshot(s, &snap);
        hold_until = ab_load_l(&s->hold_until_ms);
        hold_left = hold_until > now ? hold_until - now : 0;
        resume_speed = ab_load_i(&s->resume_speed);
        paused = ab_load_i(&s->paused_by_beat);
        hit_seq_after = ab_load_i(&s->hit_seq);
        consumed_seq_after = ab_load_i(&s->consumed_seq);
        action = ab_load_i(&s->action_mode);
    }

    if(ab_breakbeat_state.last_hit_ms > 0)
        hit_age = now - ab_breakbeat_state.last_hit_ms;
    if(ab_breakbeat_state.last_transport_action_ms > 0)
        action_age = now - ab_breakbeat_state.last_transport_action_ms;
    if(ab_breakbeat_state.burst_until_ms > now)
        burst_left = ab_breakbeat_state.burst_until_ms - now;

    qread = ab_breakbeat_hit_queue_read;
    qwrite = ab_breakbeat_hit_queue_write;
    qdepth = qwrite >= qread ? qwrite - qread : AB_BREAKBEAT_HIT_QUEUE_SIZE - qread + qwrite;

    runtime_fps = ab_breakbeat_runtime_fps(v);
    base_fps = ab_breakbeat_base_fps(v);
    current_fps = ab_breakbeat_state.current_fps;
    target_fps = ab_breakbeat_state.target_fps;
    burst_fps = ab_breakbeat_state.burst_fps;

    if(current_fps <= 0.01f)
        current_fps = runtime_fps;
    if(target_fps <= 0.01f)
        target_fps = current_fps;

    effective = (float)(speed < 0 ? -speed : speed) * runtime_fps;
    target_effective = (float)(resume_speed < 0 ? -resume_speed : resume_speed) * target_fps;

    dbg_kind = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.hit_kind : snap.hit_kind;
    body = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.body_drive : ab_breakbeat_debug_body_drive(&snap);
    body_gate = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.body_gate : ab_breakbeat_debug_body_gate(&snap, dbg_kind);
    section = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.section_energy : ab_breakbeat_debug_section_energy(&snap);
    section_gate = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.section_gate : ab_breakbeat_debug_section_gate(&snap);
    forward_drive = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.forward_drive : ab_breakbeat_debug_forward_drive(&snap);
    forward_gate = ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.forward_gate : ab_breakbeat_debug_forward_gate(&snap);

    transport_period = ab_breakbeat_transport_period_ms(&snap);
    transport_bpm = transport_period > 1.0 ? 60000.0 / transport_period : 0.0;
    fallback_fps = ab_breakbeat_fallback_fps(v, &snap);
    recent_fps = ab_breakbeat_recent_fallback_floor(v, fallback_fps, now, &snap);

    AB_BREAK_FRAME_DBG(
        "t=%ld frame=%lld rc=%d path=%s action=%s(%d) hs=%d->%d cs=%d->%d spd=%d resume=%d paused=%d hold=%ld active=%d dir=%d repcnt=%d loop=%d fb=%d fps=%.2f fps_cur=%.2f fps_tgt=%.2f fps_burst=%.2f fps_base=%.2f eff=%.2f teff=%.2f kind=%s ev=%d age=%ld stale=%ld open=%ld repeat=%ld rep=%d tonal=%d fw=%d allow=%d base_spd=%d run=%d slice=%d maxrep=%d ev_cur=%lld ev_target=%lld bpm=%.1f tbpm=%.1f tper=%.1f rawper=%.1f ri=%.1f prevint=%ld hitage=%ld actage=%ld burstleft=%ld q=%d/%d/%d lvl=%.3f env=%.3f flux=%.3f tr=%.3f k=%.3f sn=%.3f hat=%.3f bass=%.3f mid=%.3f high=%.3f pulse=%.3f gate=%.3f dens=%.3f rhythm_dens=%.3f reg=%.3f accent=%.3f accel=%.3f tempo=%.3f groove=%.3f phrase=%.3f climax=%.3f body=%.3f bgate=%.3f sect=%.3f sectgate=%.3f shot=%d fdrive=%.3f fgate=%.3f fb_fps=%.2f recent_fps=%.2f override=%ld",
        now,
        frame,
        result,
        ab_breakbeat_frame_debug.path ? ab_breakbeat_frame_debug.path : "idle",
        ab_action_name(action),
        action,
        hit_seq_before,
        hit_seq_after,
        consumed_seq_before,
        consumed_seq_after,
        speed,
        resume_speed,
        paused,
        hold_left,
        ab_breakbeat_state.active,
        ab_breakbeat_state.direction,
        ab_breakbeat_state.repeat_count,
        ab_breakbeat_state.local_loop_active,
        ab_breakbeat_state.fallback_active,
        runtime_fps,
        current_fps,
        target_fps,
        burst_fps,
        base_fps,
        effective,
        target_effective,
        ab_breakbeat_debug_hit_name(dbg_kind),
        ab_breakbeat_frame_debug.event_valid,
        ab_breakbeat_frame_debug.event_valid ? ab_breakbeat_frame_debug.event_age_ms : (long)snap.beat_age_ms,
        ab_breakbeat_frame_debug.stale_ms,
        ab_breakbeat_frame_debug.open_ms,
        ab_breakbeat_frame_debug.repeat_ms,
        ab_breakbeat_frame_debug.repeated,
        ab_breakbeat_frame_debug.tonal,
        ab_breakbeat_frame_debug.forward,
        ab_breakbeat_frame_debug.allowed,
        ab_breakbeat_frame_debug.base_speed,
        ab_breakbeat_frame_debug.run_speed,
        ab_breakbeat_frame_debug.slice_frames,
        ab_breakbeat_frame_debug.max_repeats,
        ab_breakbeat_frame_debug.cur,
        ab_breakbeat_frame_debug.target,
        snap.bpm,
        transport_bpm,
        transport_period,
        snap.bpm > 1.0f ? 60000.0f / snap.bpm : 0.0f,
        ab_breakbeat_state.rhythm_interval_ema_ms,
        ab_breakbeat_state.tempo_prev_interval_ms,
        hit_age,
        action_age,
        burst_left,
        qread,
        qwrite,
        qdepth,
        snap.level,
        snap.envelope,
        snap.flux,
        snap.transient,
        snap.kick,
        snap.snare,
        snap.hat,
        snap.bass,
        snap.mid,
        snap.high,
        snap.beat_pulse,
        snap.beat_gate,
        snap.beat_density,
        ab_breakbeat_state.rhythm_density,
        ab_breakbeat_state.rhythm_regularity,
        ab_breakbeat_state.rhythm_accent,
        ab_breakbeat_state.rhythm_accel,
        ab_breakbeat_state.tempo_drive,
        ab_breakbeat_state.music_groove,
        ab_breakbeat_state.music_phrase,
        ab_breakbeat_state.music_climax,
        body,
        body_gate,
        section,
        section_gate,
        section >= section_gate,
        forward_drive,
        forward_gate,
        fallback_fps,
        recent_fps,
        ab_load_l(&ab_breakbeat_user_override_until_ms));
}
#else
#define ab_breakbeat_debug_reset_frame() do { } while(0)
#define ab_breakbeat_debug_note_simple(path) do { } while(0)
#define ab_breakbeat_debug_note_event(path, snap, repeated, tonal, forward, allowed, base_speed, run_speed, slice_frames, max_repeats, open_ms, repeat_ms, stale_ms, event_age_ms, cur, target, target_fps) do { } while(0)
#define ab_breakbeat_debug_trace_frame(v, s, now, result, hit_seq_before, consumed_seq_before) do { } while(0)
#endif

static int ab_breakbeat_consume(veejay_t *v, vj_audio_beat_shared_t *s,
                                long now, int hit_seq, int consumed_seq)
{
    ab_breakbeat_hit_event_t ev;
    vj_audio_beat_snapshot_t snap;
    long open_ms;
    long repeat_ms;
    long event_ms;
    long event_publish_ms;
    long event_block_ms;
    long stale_ms;
    long event_age_ms;
    long long cur = 0;
    long long target = 0;
    int hit_kind;
    int base_speed;
    int run_speed;
    int slice_frames;
    int repeated;
    int cur_scene_id;
    int max_repeats;
    int scratch_dir;
    int scratch_turn_reversal;
    int scratch_block_escape;
    int transport_allowed;
    int tonal_transport_hit;
    float scratch_amount;
    float scratch_velocity;
    float scratch_burst;
    float target_fps;
    float effective_late_frames;
    float late_drive;
    int very_late_hit;
    const char *consume_path;
    const char *block_reason;
    memset(&ev, 0, sizeof(ev));

    if(!ab_breakbeat_hit_queue_pop_after(consumed_seq, &ev))
    {
        if(hit_seq == consumed_seq)
        {
            ab_breakbeat_debug_note_simple("fallback");
            return ab_breakbeat_fallback_if_due(v, s, now);
        }

        if(!vj_audio_beat_get_snapshot(s, &snap))
            memset(&snap, 0, sizeof(snap));

        ev.valid = 1;
        ev.seq = hit_seq;
        ev.hit_ms = snap.last_hit_ms > 0 ? snap.last_hit_ms : now;
        ev.publish_ms = now;
        ev.block_ms = 0;
        ev.snap = snap;
    }
    else
    {
        snap = ev.snap;
        hit_seq = ev.seq;
    }

    if(hit_seq <= consumed_seq)
    {
        ab_breakbeat_debug_note_simple("late-event");
        return ab_breakbeat_fallback_if_due(v, s, now);
    }

    event_ms = ev.hit_ms > 0 ? ev.hit_ms : now;
    event_publish_ms = ev.publish_ms > 0 ? ev.publish_ms : event_ms;
    if(event_publish_ms < event_ms)
        event_publish_ms = event_ms;
    event_block_ms = ev.block_ms > 0 ? ev.block_ms : 0;
    event_age_ms = now >= event_ms ? now - event_ms : 0;
    effective_late_frames = ab_breakbeat_effective_late_frames(event_age_ms);
    late_drive = ab_breakbeat_late_hit_drive(effective_late_frames);
    very_late_hit = effective_late_frames >= AB_BREAKBEAT_VERY_LATE_HIT_FRAMES;
    consume_path = late_drive > 0.0f ? (very_late_hit ? "hit-vlate" : "hit-late") : "hit";
    block_reason = "none";

    if(snap.hit_seq <= 0)
        snap.hit_seq = hit_seq;
    snap.hit_seq = hit_seq;
    snap.last_hit_ms = event_ms;
    snap.beat_age_ms = event_age_ms;

    if(snap.hit_kind == AB_HIT_SCRATCH)
    {
        if(ev.scratch_amount <= 0.0f)
            ev.scratch_amount = ab_from_q15(ab_load_i(&ab_scratch_q15));
        if(ev.scratch_velocity <= 0.0f)
            ev.scratch_velocity = ab_from_q15(ab_load_i(&ab_scratch_velocity_q15));
        if(ev.scratch_burst <= 0.0f)
            ev.scratch_burst = ab_from_q15(ab_load_i(&ab_scratch_burst_q15));
        if(ev.scratch_dir == 0)
            ev.scratch_dir = ab_load_i(&ab_scratch_dir) < 0 ? -1 : 1;

        snap.beat_density = ev.scratch_amount;
        snap.flux = ev.scratch_velocity;
        snap.transient = ev.scratch_burst;
        snap.beat_pulse = ev.scratch_burst;
        snap.beat_gate = ev.scratch_amount > 0.18f ? 1.0f : 0.0f;
    }

    stale_ms = ab_breakbeat_ms_from_beats(&snap,
                                          ab_breakbeat_policy.stale_event_beats,
                                          ab_breakbeat_policy.stale_min_ms,
                                          ab_breakbeat_policy.stale_max_ms);

    if(snap.hit_kind == AB_HIT_SCRATCH)
    {
        float scratch_drive = ab_breakbeat_clampf(ev.scratch_amount * 0.62f +
                                                  ev.scratch_burst * 0.38f);
        long scratch_stale = ab_breakbeat_policy.scratch_repeat_min_ms +
                             (long)((double)(ab_breakbeat_policy.scratch_repeat_max_ms -
                                             ab_breakbeat_policy.scratch_repeat_min_ms) *
                                    (double)scratch_drive + 0.5);

        if(stale_ms > scratch_stale)
            stale_ms = scratch_stale;
    }

    if(event_age_ms > stale_ms)
    {
        ab_store_i(&s->consumed_seq, hit_seq);
        ab_breakbeat_trace_consume(s, "stale", "stale", &snap, now, event_ms, event_publish_ms, event_block_ms, stale_ms, 0L, 0L, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f);
        ab_breakbeat_debug_note_event("stale", &snap, 0, 0, 0, 0, 0, 0, 0, 0, 0L, 0L, stale_ms, event_age_ms, 0, 0, 0.0f);
        return ab_breakbeat_fallback_if_due(v, s, now);
    }

    ab_breakbeat_update_music_state(&snap, event_ms);

    hit_kind = snap.hit_kind;
    tonal_transport_hit = ab_breakbeat_tonal_transport_hit(&snap, hit_kind);
    scratch_amount = ev.scratch_amount;
    scratch_velocity = ev.scratch_velocity;
    scratch_burst = ev.scratch_burst;
    scratch_dir = ev.scratch_dir < 0 ? -1 : 1;
    open_ms = ab_breakbeat_open_ms(s, &snap, hit_kind);
    repeat_ms = ab_breakbeat_repeat_window_ms(s, &snap);
    cur = ab_breakbeat_frame_from_hit_time(v, s, event_ms, now);
    cur = ab_breakbeat_scene_refine_frame(v, cur, &snap);
    cur_scene_id = ab_breakbeat_scene_id_for_frame(v, cur);
    base_speed = ab_breakbeat_base_speed(v, s);
    slice_frames = ab_breakbeat_slice_frames(v, s, &snap);
    slice_frames = ab_breakbeat_bound_slice_frames(v, s, slice_frames, &snap);
    slice_frames = ab_breakbeat_quantize_slice_frames(v, slice_frames, &snap);
    slice_frames = ab_breakbeat_bound_slice_frames(v, s, slice_frames, &snap);
    max_repeats = ab_breakbeat_max_repeats(s);

    if(late_drive > 0.0f)
    {
        long open_reduce = (long)((double)open_ms * (0.18 + 0.22 * (double)late_drive) + 0.5);
        long repeat_reduce = (long)((double)repeat_ms * (0.24 + 0.36 * (double)late_drive) + 0.5);

        open_ms -= open_reduce;
        repeat_ms -= repeat_reduce;

        if(open_ms < 32L)
            open_ms = 32L;
        if(repeat_ms < 40L)
            repeat_ms = 40L;

        if(max_repeats > 1)
            max_repeats = 1;
        if(very_late_hit)
            max_repeats = 0;
    }

    if(!ab_is_body_hit(hit_kind) && hit_kind != AB_HIT_SCRATCH)
    {
        float state_fps = ab_breakbeat_fallback_fps(v, &snap);
        float base_fps = ab_breakbeat_base_fps(v);

        if(base_fps < 1.0f)
            base_fps = 25.0f;

        state_fps = ab_breakbeat_recent_fallback_floor(v, state_fps, now, &snap);
        if(state_fps < base_fps * 0.46f)
            state_fps = base_fps * 0.46f;
        if(state_fps > base_fps)
            state_fps = base_fps;

        ab_store_i(&s->consumed_seq, hit_seq);
        ab_breakbeat_clear_local_loop();
        ab_breakbeat_state.repeat_count = 0;
        ab_breakbeat_state.direction = 1;
        ab_breakbeat_state.fallback_dir = 1;
        ab_breakbeat_state.fallback_active = 0;

        if(ab_breakbeat_state.active)
        {
            ab_breakbeat_state.burst_fps = state_fps;
            ab_breakbeat_state.burst_until_ms = now + 40L;
            ab_breakbeat_apply_fps(v, state_fps);
            ab_store_i(&s->resume_speed, 1);
            ab_store_l(&s->hold_until_ms, now + 40L);
            ab_breakbeat_apply_transport(v, 1);
        }

        ab_breakbeat_trace_consume(s, "state-only", "state-only", &snap, now, event_ms, event_publish_ms, event_block_ms, stale_ms, open_ms, repeat_ms, 0, tonal_transport_hit, 0, base_speed, 0, slice_frames, max_repeats, cur, cur, state_fps);
        ab_breakbeat_debug_note_event("state-only", &snap, 0, tonal_transport_hit, 0, 0, base_speed, 0, slice_frames, max_repeats, open_ms, repeat_ms, stale_ms, event_age_ms, cur, cur, state_fps);
        return 1;
    }

    repeated = ab_breakbeat_state.anchor_valid &&
               ab_breakbeat_state.last_hit_ms > 0 &&
               event_ms >= ab_breakbeat_state.last_hit_ms &&
               (event_ms - ab_breakbeat_state.last_hit_ms) <= repeat_ms &&
               ab_breakbeat_state.repeat_count < max_repeats;

    if(repeated &&
       ab_breakbeat_state.anchor_scene_id > 0 &&
       cur_scene_id > 0 &&
       cur_scene_id != ab_breakbeat_state.anchor_scene_id)
        repeated = 0;

    scratch_turn_reversal = 0;
    scratch_block_escape = 0;
    if(hit_kind == AB_HIT_SCRATCH &&
       ab_breakbeat_scratch_transport_reversal_allowed(&snap, event_ms, scratch_dir))
    {
        scratch_turn_reversal = 1;
        repeated = 0;
    }

    transport_allowed = ab_breakbeat_transport_hit_allowed(&snap, event_ms, hit_kind, repeated);
    if(!transport_allowed)
        block_reason = ab_breakbeat_transport_block_reason(&snap,
                                                           event_ms,
                                                           hit_kind,
                                                           repeated,
                                                           repeat_ms,
                                                           max_repeats,
                                                           scratch_dir);
    if(!transport_allowed && scratch_turn_reversal)
    {
        ab_breakbeat_state.last_transport_action_ms = event_ms;
        transport_allowed = 1;
        block_reason = "scratch-turn";
    }

    if(!transport_allowed &&
       hit_kind == AB_HIT_SCRATCH &&
       ab_breakbeat_scratch_block_escape_allowed(&snap,
                                                 event_age_ms,
                                                 effective_late_frames,
                                                 scratch_dir,
                                                 scratch_amount,
                                                 scratch_velocity,
                                                 scratch_burst))
    {
        float escape_drive = ab_breakbeat_scratch_block_escape_drive(scratch_amount,
                                                                     scratch_velocity,
                                                                     scratch_burst);
        long escape_open = 42L + (long)(escape_drive * 44.0f + 0.5f);
        long escape_repeat = 70L + (long)(escape_drive * 110.0f + 0.5f);

        scratch_block_escape = 1;
        repeated = 0;
        transport_allowed = 1;
        consume_path = "scratch-escape";
        block_reason = "scratch-escape";
        ab_breakbeat_state.last_transport_action_ms = event_ms;

        if(open_ms > escape_open)
            open_ms = escape_open;
        if(repeat_ms > escape_repeat)
            repeat_ms = escape_repeat;
        if(max_repeats > 1)
            max_repeats = 1;
    }

    if(very_late_hit && hit_kind == AB_HIT_SCRATCH)
    {
        float visual_late_drive = late_drive > 0.0f ? late_drive : 1.0f;
        float base_fps = ab_breakbeat_base_fps(v);
        float safe_fps = ab_breakbeat_recent_fallback_floor(v, base_fps * 0.72f, now, &snap);

        if(safe_fps < base_fps * 0.46f)
            safe_fps = base_fps * 0.46f;
        if(safe_fps > base_fps)
            safe_fps = base_fps;

        ab_store_i(&s->consumed_seq, hit_seq);
        ab_breakbeat_state.scratch_transport_dir = scratch_dir;
        ab_breakbeat_state.scratch_transport_ms = now;
        ab_scratch_visual_pulse(now, scratch_dir,
                                scratch_amount,
                                scratch_velocity,
                                scratch_burst,
                                visual_late_drive);
        ab_breakbeat_clear_local_loop();

        if(ab_breakbeat_state.active)
        {
            ab_breakbeat_state.burst_fps = safe_fps;
            ab_breakbeat_state.burst_until_ms = now + 32L;
            ab_breakbeat_apply_fps(v, safe_fps);
            ab_store_i(&s->resume_speed, 1);
            ab_store_l(&s->hold_until_ms, now + 32L);
            ab_breakbeat_apply_transport(v, 1);
        }

        ab_breakbeat_trace_consume(s, "scratch-vlate-visual", "vlate-visual", &snap, now, event_ms, event_publish_ms, event_block_ms, stale_ms, open_ms, repeat_ms, repeated, tonal_transport_hit, 1, base_speed, ab_breakbeat_state.active ? 1 : 0, slice_frames, max_repeats, cur, cur, safe_fps);
        ab_breakbeat_debug_note_event("scratch-vlate-visual", &snap, repeated, tonal_transport_hit, 0, 1, base_speed, ab_breakbeat_state.active ? 1 : 0, slice_frames, max_repeats, open_ms, repeat_ms, stale_ms, event_age_ms, cur, cur, safe_fps);
        return 1;
    }

    if(!transport_allowed)
    {
        ab_store_i(&s->consumed_seq, hit_seq);
        ab_breakbeat_trace_consume(s, "blocked", block_reason, &snap, now, event_ms, event_publish_ms, event_block_ms, stale_ms, open_ms, repeat_ms, repeated, tonal_transport_hit, 0, base_speed, 0, slice_frames, max_repeats, cur, cur, 0.0f);
        ab_breakbeat_debug_note_event("blocked", &snap, repeated, tonal_transport_hit, 0, 0, base_speed, 0, slice_frames, max_repeats, open_ms, repeat_ms, stale_ms, event_age_ms, cur, cur, 0.0f);
        return ab_breakbeat_fallback_if_due(v, s, now);
    }

    if(!ab_breakbeat_state.active)
    {
        ab_breakbeat_state.saved_speed = base_speed;
        ab_breakbeat_state.saved_fps = ab_breakbeat_runtime_fps(v);
        ab_breakbeat_state.base_fps = (float)ab_breakbeat_video_fps();
        if(ab_breakbeat_state.base_fps <= 0.01f || ab_breakbeat_state.base_fps > 120.0f)
            ab_breakbeat_state.base_fps = ab_breakbeat_state.saved_fps;
        if(ab_breakbeat_state.base_fps > 60.0f)
            ab_breakbeat_state.base_fps = 60.0f;
        if(ab_breakbeat_state.saved_fps > ab_breakbeat_max_effect_fps(v))
            ab_breakbeat_state.saved_fps = ab_breakbeat_state.base_fps;
        ab_breakbeat_state.current_fps = ab_breakbeat_state.saved_fps;
        if(ab_breakbeat_state.current_fps > ab_breakbeat_max_effect_fps(v))
            ab_breakbeat_state.current_fps = ab_breakbeat_state.base_fps;
        if(ab_breakbeat_state.current_fps < 5.0f)
            ab_breakbeat_state.current_fps = 5.0f;
        ab_breakbeat_state.target_fps = ab_breakbeat_state.current_fps;
        ab_breakbeat_state.fps_last_ms = now;
        ab_breakbeat_state.fps_write_last_ms = now;
        ab_breakbeat_state.tempo_drive = 0.0f;
        ab_breakbeat_state.tempo_last_hit_ms = 0;
        ab_breakbeat_state.tempo_prev_interval_ms = 0;
    }

    if(!repeated)
    {
        ab_breakbeat_state.anchor_valid = 1;
        ab_breakbeat_state.anchor_frame = cur;
        ab_breakbeat_state.anchor_scene_id = cur_scene_id;
        ab_breakbeat_state.repeat_count = 0;
        ab_breakbeat_state.direction = 1;
        ab_breakbeat_state.fallback_dir = 1;
    }
    else
    {
        ab_breakbeat_state.repeat_count++;
    }

    ab_breakbeat_state.direction = 1;
    ab_breakbeat_state.fallback_dir = 1;

    target = ab_breakbeat_state.anchor_frame;
    run_speed = 1;
    target_fps = ab_breakbeat_hit_fps(v, &snap, repeated, hit_kind);
    if(ab_breakbeat_state.tempo_drive > 0.60f && !tonal_transport_hit &&
       hit_kind != AB_HIT_HAT && hit_kind != AB_HIT_SCRATCH)
    {
        float pace = ab_breakbeat_pace_drive(&snap);
        float phrase = ab_breakbeat_phrase_drive();
        float climax = ab_breakbeat_climax_drive();
        float velocity = ab_breakbeat_velocity(&snap);
        float expression = ab_breakbeat_expression_drive();
        float accel = ab_breakbeat_accel_drive();

        if(accel > 0.32f || expression > 0.46f ||
           (phrase > 0.76f && climax > 0.78f && velocity > 0.74f))
        {
            float accel_floor = ab_breakbeat_base_fps(v) *
                                (0.66f + ab_breakbeat_state.tempo_drive * 0.24f +
                                 pace * 0.08f + expression * 0.20f + accel * 0.22f);
            if(target_fps < accel_floor)
                target_fps = ab_breakbeat_clamp_effect_fps(v, accel_floor);
        }
    }
    if(hit_kind == AB_HIT_SCRATCH)
    {
        int scrub = 1 + (int)(scratch_velocity * 5.0f) + (int)(scratch_burst * 4.0f) + (repeated ? 1 : 0);
        float base = ab_breakbeat_base_fps(v);
        float scratch_fps = base * (0.72f + scratch_amount * 0.42f + scratch_velocity * 0.52f + scratch_burst * 0.28f);

        if(late_drive > 0.0f)
            scratch_fps += base * (0.08f + late_drive * 0.14f);
        if(scratch_block_escape)
            scratch_fps += base * 0.10f;

        if(scrub < 1)
            scrub = 1;
        else if(scrub > 12)
            scrub = 12;

        scrub = ab_breakbeat_quantize_slice_frames(v, scrub, &snap);
        if(scrub > 12)
            scrub = 12;

        ab_breakbeat_state.direction = scratch_dir;
        ab_breakbeat_state.fallback_dir = scratch_dir;
        target = cur + (long long)scratch_dir * (long long)scrub;
        run_speed = scratch_dir;

        if(scratch_fps > target_fps)
            target_fps = ab_breakbeat_clamp_effect_fps(v, scratch_fps);
    }
    else if(ab_breakbeat_state.direction < 0)
    {
        target += (long long)slice_frames;
        run_speed = -run_speed;
    }
    else
    {
        target = ab_breakbeat_state.anchor_frame;
    }

    target = ab_breakbeat_scene_guard_target(v, cur, target);

    ab_store_i(&s->consumed_seq, hit_seq);
    ab_breakbeat_state.active = 1;
    ab_breakbeat_state.last_hit_seq = hit_seq;
    ab_breakbeat_state.last_hit_ms = event_ms;
    ab_breakbeat_state.saved_speed = base_speed;

    if(hit_kind == AB_HIT_SCRATCH)
    {
        ab_breakbeat_state.scratch_transport_dir = scratch_dir;
        ab_breakbeat_state.scratch_transport_ms = event_ms;
        ab_scratch_visual_pulse(now, scratch_dir,
                                scratch_amount,
                                scratch_velocity,
                                scratch_burst,
                                late_drive);
    }

    {
        int parked = 0;
        long long projected_target = ab_breakbeat_project_frame(v, target, &ab_breakbeat_state.direction, &parked);
        int radius = ab_breakbeat_local_radius(slice_frames, &snap);

        projected_target = ab_breakbeat_scene_refine_frame(v, projected_target, &snap);

        if(projected_target > LONG_MAX)
            projected_target = LONG_MAX;

        veejay_set_frame(v, (long)projected_target);

        if(ab_breakbeat_should_use_local_loop(&snap, repeated))
        {
            ab_breakbeat_set_local_loop(v, projected_target, radius);
            if(hit_kind == AB_HIT_SCRATCH)
                ab_breakbeat_state.local_loop_until_ms = now + open_ms + 24L +
                                                       (long)(scratch_amount * 90.0f);
            else
                ab_breakbeat_state.local_loop_until_ms = now + open_ms + 90L +
                                                       (long)(ab_breakbeat_climax_drive() * 420.0f);
        }
        else
        {
            ab_breakbeat_clear_local_loop();
        }

        if(parked)
            run_speed = 1;
        else if(run_speed == 0)
            run_speed = 1;
    }

    target_fps = ab_breakbeat_clamp_effect_fps(v, target_fps);

    ab_breakbeat_state.burst_fps = target_fps;
    ab_breakbeat_state.burst_until_ms = now + open_ms;

    ab_breakbeat_apply_fps(v, target_fps);

    ab_breakbeat_state.fallback_active = 0;
    ab_breakbeat_state.fallback_dir = ab_breakbeat_state.direction;
    ab_store_i(&s->resume_speed, run_speed);
    ab_store_l(&s->hold_until_ms, now + open_ms);
    ab_breakbeat_apply_transport(v, run_speed);

    ab_breakbeat_trace_consume(s, consume_path, block_reason, &snap, now, event_ms, event_publish_ms, event_block_ms, stale_ms, open_ms, repeat_ms, repeated, tonal_transport_hit, 1, base_speed, run_speed, slice_frames, max_repeats, cur, target, target_fps);
    ab_breakbeat_debug_note_event(consume_path, &snap, repeated, tonal_transport_hit, 0, 1, base_speed, run_speed, slice_frames, max_repeats, open_ms, repeat_ms, stale_ms, event_age_ms, cur, target, target_fps);

    return 1;
}

int vj_audio_beat_resume_if_due(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int enabled;
    int action;
    long now;

    if(!v || !v->settings || !s || !ab_load_i(&s->initialized))
        return 0;

    now = ab_now_ms();
    enabled = ab_load_i(&s->enabled);
    action = ab_load_i(&s->action_mode);

    if(ab_breakbeat_state.active && (!enabled || !ab_action_is_breakbeat(action)))
    {
        ab_breakbeat_release_transport(v, s);
        return 1;
    }

    if(!ab_load_i(&s->paused_by_beat) && !ab_action_is_breakbeat(action))
        return 0;

    if(!enabled || !ab_action_is_breakbeat(action))
    {
        if(ab_breakbeat_state.active)
            ab_breakbeat_release_transport(v, s);
        else
            ab_resume_from_consumer(v, s);
        return 1;
    }

    {
        int cur_hit_seq = ab_load_i(&s->hit_seq);

        if(ab_breakbeat_resume_owned_pause(v, s, now, cur_hit_seq))
            return 1;

        if(ab_breakbeat_respect_user_pause(v, s, cur_hit_seq))
            return 0;

        if(ab_breakbeat_detect_user_resume(v, s, now, cur_hit_seq))
            return 0;

        if(ab_breakbeat_user_pause_override_active())
            return 0;

        return ab_breakbeat_fallback_if_due(v, s, now);
    }
}

static int ab_breakbeat_source_loss_pause(veejay_t *v,
                                          vj_audio_beat_shared_t *s,
                                          long now,
                                          int hit_seq)
{
    int speed;
    const char *reason;

    if(!v || !v->settings || !s)
        return 0;

    if(!atomic_load_int(&ab_source_loss_pause))
        return 0;

    if(!ab_source_loss_is_active(s, now))
        return 0;

    if(atomic_load_int(&ab_source_loss_paused))
        return 0;

    atomic_store_int(&ab_source_loss_paused, 1);

    speed = v->settings->current_playback_speed;
    if(speed == 0)
    {
        ab_store_i(&s->consumed_seq, hit_seq >= 0 ? hit_seq : ab_load_i(&s->hit_seq));
        ab_store_l(&ab_breakbeat_user_override_until_ms, AB_BREAKBEAT_USER_PAUSE_OVERRIDE);
        return 0;
    }

    ab_store_i(&s->resume_speed, speed);
    v->settings->previous_playback_speed = speed;

    if(ab_breakbeat_state.active)
        ab_breakbeat_restore_fps(v);

    ab_breakbeat_reset_state();
    ab_breakbeat_hit_queue_clear();

    ab_store_i(&s->paused_by_beat, 0);
    ab_store_l(&s->hold_until_ms, 0);
    ab_store_i(&s->consumed_seq, hit_seq >= 0 ? hit_seq : ab_load_i(&s->hit_seq));
    ab_store_l(&ab_breakbeat_user_override_until_ms, AB_BREAKBEAT_USER_PAUSE_OVERRIDE);

    ab_set_speed_from_beat(v, 0, 0);

    reason = !ab_load_i(&s->open) ? "closed" :
             ((now - atomic_load_long(&ab_source_last_block_ms)) >= AB_SOURCE_LOSS_NO_BLOCK_MS ? "stale" : "silent");

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT][BREAK] audio source %s; transport parked at speed 0",
               reason);

    return 1;
}

static int ab_breakbeat_source_loss_hold_active(veejay_t *v,
                                                vj_audio_beat_shared_t *s,
                                                long now)
{
    if(!v || !v->settings || !s)
        return 0;

    if(!atomic_load_int(&ab_source_loss_pause))
        return 0;

    if(!atomic_load_int(&ab_source_loss_paused))
        return 0;

    if(!ab_source_loss_is_active(s, now))
    {
        atomic_store_int(&ab_source_loss_paused, 0);
        return 0;
    }

    return v->settings->current_playback_speed == 0;
}

int vj_audio_beat_consume(veejay_t *v, vj_audio_beat_shared_t *s)
{
    int enabled;
    int action;
    int hit_seq;
    int consumed_seq;
    long now;

    if(!v || !v->settings || !s || !ab_load_i(&s->initialized))
        return 0;

    now = ab_now_ms();
    enabled = ab_load_i(&s->enabled);
    action = ab_load_i(&s->action_mode);

    if(ab_breakbeat_state.active && !ab_action_is_breakbeat(action))
    {
        ab_breakbeat_release_transport(v, s);
        return 1;
    }

    if(ab_load_i(&s->paused_by_beat) && !ab_action_is_breakbeat(action))
        vj_audio_beat_resume_if_due(v, s);

    if(!enabled)
    {
        if(ab_breakbeat_state.active)
            ab_breakbeat_release_transport(v, s);
        return 0;
    }

    hit_seq = ab_load_i(&s->hit_seq);
    consumed_seq = ab_load_i(&s->consumed_seq);

    if(action == VJ_AUDIO_BEAT_ACTION_AUTO_FX)
    {
        if(hit_seq != consumed_seq)
            ab_store_i(&s->consumed_seq, hit_seq);

        return 0;
    }

    if(ab_action_is_breakbeat(action))
    {
        int break_result = 0;

        ab_breakbeat_debug_reset_frame();

        if(ab_breakbeat_source_loss_pause(v, s, now, hit_seq))
        {
            ab_breakbeat_debug_note_simple("source-loss-pause");
            break_result = 1;
        }
        else if(ab_breakbeat_source_loss_hold_active(v, s, now))
        {
            if(hit_seq != consumed_seq)
                ab_store_i(&s->consumed_seq, hit_seq);
            ab_breakbeat_hit_queue_clear();
            ab_breakbeat_debug_note_simple("source-loss-hold");
            break_result = 0;
        }
        else if(ab_breakbeat_resume_owned_pause(v, s, now, hit_seq))
        {
            ab_breakbeat_debug_note_simple("resume-owned");
            break_result = 1;
        }
        else if(ab_breakbeat_respect_user_pause(v, s, hit_seq))
        {
            ab_breakbeat_debug_note_simple("respect-pause");
            break_result = 0;
        }
        else if(ab_breakbeat_detect_user_resume(v, s, now, hit_seq))
        {
            ab_breakbeat_debug_note_simple("detect-resume");
            break_result = 0;
        }
        else if(ab_breakbeat_user_pause_override_active())
        {
            if(hit_seq != consumed_seq)
                ab_store_i(&s->consumed_seq, hit_seq);
            ab_breakbeat_hit_queue_clear();
            ab_breakbeat_debug_note_simple("override");
            break_result = 0;
        }
        else
        {
            break_result = ab_breakbeat_consume(v, s, now, hit_seq, consumed_seq);
        }

        ab_breakbeat_debug_trace_frame(v, s, now, break_result, hit_seq, consumed_seq);
        return break_result;
    }

    return 0;
}

int vj_audio_beat_is_enabled(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    return ab_load_i(&s->enabled);
}

int vj_audio_beat_is_running(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    return ab_load_i(&s->running);
}

int vj_audio_beat_is_open(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    return ab_load_i(&s->open);
}

int vj_audio_beat_is_paused_by_beat(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    return ab_load_i(&s->paused_by_beat);
}

void vj_audio_beat_set_output_latency_ms(vj_audio_beat_shared_t *s, int ms)
{
    (void)s;

    if(ms < 0)
        ms = -1;
    else if(ms > 5000)
        ms = 5000;

    atomic_store_int(&ab_output_latency_ms, ms);
    atomic_store_int(&ab_heard_latency_ms, ms);
}

int vj_audio_beat_get_output_latency_ms(vj_audio_beat_shared_t *s)
{
    (void)s;
    return atomic_load_int(&ab_output_latency_ms);
}

void vj_audio_beat_set_heard_latency_ms(vj_audio_beat_shared_t *s, int ms)
{
    (void)s;

    if(ms < 0)
        ms = -1;
    else if(ms > 5000)
        ms = 5000;

    atomic_store_int(&ab_heard_latency_ms, ms);
}

int vj_audio_beat_get_heard_latency_ms(vj_audio_beat_shared_t *s)
{
    (void)s;
    return atomic_load_int(&ab_heard_latency_ms);
}

void vj_audio_beat_set_monitor_latency_ms(vj_audio_beat_shared_t *s, int ms)
{
    (void)s;

    if(ms < 0)
        ms = -1;
    else if(ms > 64)
        ms = 64;

    atomic_store_int(&ab_monitor_latency_ms, ms);
}

int vj_audio_beat_get_monitor_latency_ms(vj_audio_beat_shared_t *s)
{
    (void)s;
    return atomic_load_int(&ab_monitor_latency_ms);
}

int vj_audio_beat_get_effective_latency_ms(vj_audio_beat_shared_t *s)
{
    (void)s;
    return atomic_load_int(&ab_heard_latency_ms);
}

void vj_audio_beat_set_freeze_ms(vj_audio_beat_shared_t *s, int ms)
{
    if(!s)
        return;

    if(ms < VEEJAY_AUDIO_BEAT_WINDOW_MIN_MS)
        ms = VEEJAY_AUDIO_BEAT_WINDOW_MIN_MS;
    else if(ms > VEEJAY_AUDIO_BEAT_WINDOW_MAX_MS)
        ms = VEEJAY_AUDIO_BEAT_WINDOW_MAX_MS;

    ab_store_i(&s->freeze_ms, ms);
}

void vj_audio_beat_set_cooldown_ms(vj_audio_beat_shared_t *s, int ms)
{
    if(!s)
        return;

    if(ms < VEEJAY_AUDIO_BEAT_COOLDOWN_MIN_MS)
        ms = VEEJAY_AUDIO_BEAT_COOLDOWN_MIN_MS;
    else if(ms > VEEJAY_AUDIO_BEAT_COOLDOWN_MAX_MS)
        ms = VEEJAY_AUDIO_BEAT_COOLDOWN_MAX_MS;

    ab_store_i(&s->cooldown_ms, ms);
}

void vj_audio_beat_set_threshold(vj_audio_beat_shared_t *s, int threshold)
{
    if(!s)
        return;

    if(threshold < VEEJAY_AUDIO_BEAT_THRESHOLD_MIN)
        threshold = VEEJAY_AUDIO_BEAT_THRESHOLD_MIN;
    else if(threshold > VEEJAY_AUDIO_BEAT_THRESHOLD_MAX)
        threshold = VEEJAY_AUDIO_BEAT_THRESHOLD_MAX;

    ab_store_i(&s->threshold, threshold);
}

void vj_audio_beat_set_scratch_sensitivity(vj_audio_beat_shared_t *s, int sensitivity)
{
    (void)s;

    if(sensitivity < 0)
        sensitivity = 0;
    else if(sensitivity > 100)
        sensitivity = 100;

    atomic_store_int(&ab_scratch_sensitivity, sensitivity);
}

void vj_audio_beat_set_source_loss_pause(vj_audio_beat_shared_t *s, int enabled)
{
    (void)s;

    atomic_store_int(&ab_source_loss_pause, enabled ? 1 : 0);

    if(!enabled)
        atomic_store_int(&ab_source_loss_paused, 0);
}

int vj_audio_beat_get_freeze_ms(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 90;

    return ab_load_i(&s->freeze_ms);
}

int vj_audio_beat_get_cooldown_ms(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 240;

    return ab_load_i(&s->cooldown_ms);
}

int vj_audio_beat_get_threshold(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 145;

    return ab_load_i(&s->threshold);
}

int vj_audio_beat_get_input_channels(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 2;

    return ab_load_i(&s->input_channels_request);
}

int vj_audio_beat_get_pulse_ms(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 180;

    return ab_load_i(&s->pulse_ms);
}

int vj_audio_beat_get_gate_ms(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 90;

    return ab_load_i(&s->gate_ms);
}

int vj_audio_beat_get_auto_mode(vj_audio_beat_shared_t *s)
{
    (void)s;
    return ab_load_i(&ab_auto_mode);
}

int vj_audio_beat_get_auto_amount(vj_audio_beat_shared_t *s)
{
    (void)s;
    return ab_load_i(&ab_auto_amount);
}

int vj_audio_beat_get_scratch_sensitivity(vj_audio_beat_shared_t *s)
{
    int v;

    (void)s;

    v = atomic_load_int(&ab_scratch_sensitivity);
    if(v < 0)
        v = 0;
    else if(v > 100)
        v = 100;

    return v;
}

int vj_audio_beat_get_source_loss_pause(vj_audio_beat_shared_t *s)
{
    (void)s;
    return atomic_load_int(&ab_source_loss_pause) ? 1 : 0;
}

void vj_audio_beat_set_input_channels(vj_audio_beat_shared_t *s, int channels)
{
    int old_channels;

    if(!s)
        return;

    if(channels < 1)
        channels = 1;
    else if(channels > 2)
        channels = 2;

    old_channels = ab_load_i(&s->input_channels_request);

    if(old_channels != channels)
    {
        ab_store_i(&s->input_channels_request, channels);

        if(s->sync) {
            int src = s->sync->source;

            /* Channel changes are a JACK-capture concern.  Do not turn
             * original-media PUSH or explicit WAV analysis back into JACK.
             */
            if(src == VJ_AUDIO_SYNC_SOURCE_NONE ||
               src == VJ_AUDIO_SYNC_SOURCE_JACK)
            {
                vj_audio_sync_set_source_jack(s->sync, channels);
            }
            else if(src == VJ_AUDIO_SYNC_SOURCE_PUSH)
            {
                vj_audio_sync_reset_beat_reader(s->sync);
            }
        }

        ab_add_i(&s->reset_seq, 1);
    }
}

static inline int ab_normalize_action(int action)
{
    switch(action)
    {
        case VJ_AUDIO_BEAT_ACTION_NONE:
        case VJ_AUDIO_BEAT_ACTION_AUTO_FX:
        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX:
        case VJ_AUDIO_BEAT_ACTION_BREAK_BEAT:
            return action;
        default:
            return VJ_AUDIO_BEAT_ACTION_NONE;
    }
}

static void ab_log_action_transport_mode(vj_audio_beat_shared_t *s, int action, const char *reason)
{
    const char *mode = "none";

    (void)s;

    if(action == VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX)
    {
        mode = "break-beat+auto-fx";
    }
    else if(ab_action_is_breakbeat(action))
    {
        mode = "break-beat-transport";
    }
    else if(action == VJ_AUDIO_BEAT_ACTION_AUTO_FX)
    {
        mode = "auto-fx-render";
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-BEAT] Action mode set to %s(%d), transport=%s%s%s",
               ab_action_name(action),
               action,
               mode,
               reason ? " reason=" : "",
               reason ? reason : "");
}

void vj_audio_beat_set_action(vj_audio_beat_shared_t *s, int action)
{
    int old_action;
    int hit_seq;

    if(!s)
        return;

    action = ab_normalize_action(action);
    old_action = ab_load_i(&s->action_mode);
    hit_seq = ab_load_i(&s->hit_seq);

    if(old_action == action)
        return;

    ab_store_i(&s->action_mode, action);
    ab_store_i(&s->consumed_seq, hit_seq);

    if(ab_action_is_breakbeat(old_action) &&
       !ab_action_is_breakbeat(action) &&
       ab_breakbeat_state.active)
    {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[AUDIO-BEAT] action change preserves active break-beat transport until release old=%s(%d) new=%s(%d)",
                   ab_action_name(old_action), old_action,
                   ab_action_name(action), action);
    }
    else if(ab_action_is_breakbeat(old_action) ||
            ab_action_is_breakbeat(action))
    {
        ab_breakbeat_reset_state();
    }

    if(!ab_action_is_breakbeat(action))
        ab_store_l(&ab_breakbeat_user_override_until_ms, 0);

    ab_log_action_transport_mode(s, action, "set-action");
}

void vj_audio_beat_set_action_for_transport(veejay_t *v,
                                            vj_audio_beat_shared_t *s,
                                            int action)
{
    int old_action;
    int new_action;

    if(!s)
        return;

    new_action = ab_normalize_action(action);
    old_action = ab_load_i(&s->action_mode);

    if(old_action != new_action)
        vj_audio_beat_release_transport(v, s);

    vj_audio_beat_set_action(s, new_action);
}

void vj_audio_beat_set_pulse_ms(vj_audio_beat_shared_t *s, int ms)
{
    if(!s)
        return;

    if(ms < VEEJAY_AUDIO_BEAT_PULSE_MIN_MS)
        ms = VEEJAY_AUDIO_BEAT_PULSE_MIN_MS;
    else if(ms > VEEJAY_AUDIO_BEAT_PULSE_MAX_MS)
        ms = VEEJAY_AUDIO_BEAT_PULSE_MAX_MS;

    ab_store_i(&s->pulse_ms, ms);
}

void vj_audio_beat_set_gate_ms(vj_audio_beat_shared_t *s, int ms)
{
    if(!s)
        return;

    if(ms < VEEJAY_AUDIO_BEAT_GATE_MIN_MS)
        ms = VEEJAY_AUDIO_BEAT_GATE_MIN_MS;
    else if(ms > VEEJAY_AUDIO_BEAT_GATE_MAX_MS)
        ms = VEEJAY_AUDIO_BEAT_GATE_MAX_MS;

    ab_store_i(&s->gate_ms, ms);

}

float vj_audio_beat_get_level(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0.0f;

    return ab_from_q15(ab_load_i(&s->level_q15));
}

float vj_audio_beat_get_transient(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0.0f;

    return ab_from_q15(ab_load_i(&s->transient_norm_q15));
}

long vj_audio_beat_get_hits(vj_audio_beat_shared_t *s)
{
    if(!s)
        return 0;

    return ab_load_l(&s->hits);
}

int vj_audio_beat_get_snapshot(vj_audio_beat_shared_t *s, vj_audio_beat_snapshot_t *dst)
{
    long now;
    long last;
    long age;
    int pulse_ms;
    int gate_ms;
    int hit_kind;
    float bpm;
    float pulse;

    if(!s || !dst)
        return 0;

    memset(dst, 0, sizeof(*dst));

    now = ab_now_ms();
    last = ab_load_l(&s->last_hit_ms);
    age = last > 0 ? now - last : 2147483647L;

    bpm = (float)ab_load_i(&s->bpm_q8) * (1.0f / 256.0f);
    hit_kind = ab_load_i(&ab_last_hit_kind);

    pulse_ms = (int)ab_effective_snapshot_window_ms(ab_load_i(&s->pulse_ms), bpm, hit_kind, 0);
    gate_ms = (int)ab_effective_snapshot_window_ms(ab_load_i(&s->gate_ms), bpm, hit_kind, 1);

    pulse = age >= 0 && age < pulse_ms
        ? 1.0f - ((float)age / (float)pulse_ms)
        : 0.0f;

    dst->enabled = ab_load_i(&s->enabled);
    dst->open = ab_load_i(&s->open);
    dst->channels = ab_load_i(&s->channels);
    dst->sample_rate = ab_load_i(&s->sample_rate);
    dst->hit_seq = ab_load_i(&s->hit_seq);

    dst->hits = ab_load_l(&s->hits);
    dst->last_hit_ms = last;
    dst->beat_age_ms = age;

    dst->level = ab_from_q15(ab_load_i(&s->level_q15));
    dst->envelope = ab_from_q15(ab_load_i(&s->envelope_q15));
    dst->transient = ab_from_q15(ab_load_i(&s->transient_norm_q15));
    dst->flux = ab_from_q15(ab_load_i(&s->flux_q15));
    dst->bass = ab_from_q15(ab_load_i(&ab_band_low_q15));
    dst->mid = ab_from_q15(ab_load_i(&ab_band_mid_q15));
    dst->high = ab_from_q15(ab_load_i(&ab_band_high_q15));
    dst->band_balance = ab_from_q15(ab_load_i(&ab_band_balance_q15));
    dst->beat_pulse = pulse;
    dst->beat_gate = age >= 0 && age < gate_ms ? 1.0f : 0.0f;
    dst->beat_toggle = ab_from_q15(ab_load_i(&s->beat_toggle_q15));
    dst->bpm = bpm;
    dst->kick = ab_from_q15(ab_load_i(&ab_kick_q15));
    dst->snare = ab_from_q15(ab_load_i(&ab_snare_q15));
    dst->hat = ab_from_q15(ab_load_i(&ab_hat_q15));
    dst->hit_kind = hit_kind;

    {
        double beat_ms = dst->bpm > 1.0f ? 60000.0 / (double)dst->bpm : (double)(pulse_ms * 4);
        double trail_window;
        double trail;
        double density;

        if(beat_ms < 180.0)
            beat_ms = 180.0;
        else if(beat_ms > 1800.0)
            beat_ms = 1800.0;

        trail_window = beat_ms * 4.0;

        if(trail_window < 500.0)
            trail_window = 500.0;
        else if(trail_window > 4200.0)
            trail_window = 4200.0;

        trail = (last > 0 && age >= 0 && (double)age < trail_window)
            ? 1.0 - ((double)age / trail_window)
            : 0.0;

        if(dst->bpm > 1.0f)
            density = ((double)dst->bpm - 45.0) / 155.0;
        else
            density = 0.0;

        density = ab_clampd(density, 0.0, 1.0);
        density = density * 0.42 +
                  (double)dst->beat_gate * 0.16 +
                  (double)dst->beat_pulse * 0.16 +
                  (double)(dst->kick + dst->snare) * 0.10 +
                  (double)dst->flux * 0.08 +
                  (double)dst->envelope * 0.08;

        dst->beat_trail_length = (float)ab_clampd(trail, 0.0, 1.0);
        dst->beat_density = (float)ab_clampd(density, 0.0, 1.0);
    }

    if(ab_action_is_breakbeat(ab_load_i(&s->action_mode)))
    {
        float scratch_env = ab_scratch_visual_env_now(now);

        if(scratch_env > 0.001f)
        {
            float scratch_density = scratch_env * 0.82f;

            if(dst->beat_pulse < scratch_env)
                dst->beat_pulse = scratch_env;
            if(dst->beat_trail_length < scratch_env)
                dst->beat_trail_length = scratch_env;
            if(dst->beat_density < scratch_density)
                dst->beat_density = scratch_density;
            if(scratch_env > 0.055f)
                dst->beat_gate = 1.0f;
        }
    }

    return 1;
}

float vj_audio_beat_get_signal(vj_audio_beat_shared_t *s, int signal)
{
    vj_audio_beat_snapshot_t snap;

    if(!vj_audio_beat_get_snapshot(s, &snap))
        return 0.0f;

    switch(signal)
    {
        case VJ_AUDIO_CTRL_LEVEL:
            return snap.level;

        case VJ_AUDIO_CTRL_ENVELOPE:
            return snap.envelope;

        case VJ_AUDIO_CTRL_TRANSIENT:
            return snap.transient;

        case VJ_AUDIO_CTRL_FLUX:
            return snap.flux;

        case VJ_AUDIO_CTRL_BEAT_PULSE:
            return snap.beat_pulse;

        case VJ_AUDIO_CTRL_BEAT_GATE:
            return snap.beat_gate;

        case VJ_AUDIO_CTRL_BEAT_TOGGLE:
            return snap.beat_toggle;

        case VJ_AUDIO_CTRL_BPM:
            return snap.bpm > 0.0f ? (float)ab_clampd((double)(snap.bpm / 240.0f), 0.0, 1.0) : 0.0f;

        case VJ_AUDIO_CTRL_BASS:
            return snap.bass;

        case VJ_AUDIO_CTRL_MID:
            return snap.mid;

        case VJ_AUDIO_CTRL_HIGH:
            return snap.high;

        case VJ_AUDIO_CTRL_BAND_BALANCE:
            return snap.band_balance;

        case VJ_AUDIO_CTRL_TRAIL_LENGTH:
            return snap.beat_trail_length;

        case VJ_AUDIO_CTRL_DENSITY:
            return snap.beat_density;

        case VJ_AUDIO_CTRL_KICK:
            return snap.kick;

        case VJ_AUDIO_CTRL_SNARE:
            return snap.snare;

        case VJ_AUDIO_CTRL_HAT:
            return snap.hat;

        case VJ_AUDIO_CTRL_SCRATCH_ENVELOPE:
            return ab_scratch_visual_env_now(ab_now_ms());

        case VJ_AUDIO_CTRL_SCRATCH_DIRECTION:
            return ab_scratch_visual_direction_signal(ab_now_ms());

        case VJ_AUDIO_CTRL_SCRATCH_SIGNED:
            return ab_scratch_visual_signed_signal(ab_now_ms());

        default:
            return 0.0f;
    }
}

int vj_audio_beat_map_signal(vj_audio_beat_shared_t *s, int signal, int min_value, int max_value, int invert)
{
    float v;
    int span;
    float mapped;

    v = vj_audio_beat_get_signal(s, signal);

    if(v < 0.0f)
        v = 0.0f;
    else if(v > 1.0f)
        v = 1.0f;

    if(invert)
        v = 1.0f - v;

    span = max_value - min_value;

    if(span == 0)
        return min_value;

    mapped = (float)min_value + ((float)span * v);

    return (int)(mapped >= 0.0f ? mapped + 0.5f : mapped - 0.5f);
}


static int ab_contains_lc(const char *s, const char *needle)
{
    size_t nlen;

    if(!s || !needle)
        return 0;

    nlen = strlen(needle);

    if(nlen == 0)
        return 1;

    for(; *s; s++)
    {
        size_t i;

        for(i = 0; i < nlen; i++)
        {
            unsigned char a = (unsigned char)s[i];
            unsigned char b = (unsigned char)needle[i];

            if(a == 0)
                return 0;

            if((char)tolower(a) != (char)tolower(b))
                break;
        }

        if(i == nlen)
            return 1;
    }

    return 0;
}

static int ab_is_word_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

static int ab_contains_token_lc(const char *s, const char *token)
{
    size_t nlen;

    if(!s || !token)
        return 0;

    nlen = strlen(token);

    if(nlen == 0)
        return 1;

    for(const char *p = s; *p; p++)
    {
        size_t i;
        unsigned char before;
        unsigned char after;

        for(i = 0; i < nlen; i++)
        {
            unsigned char a = (unsigned char)p[i];
            unsigned char b = (unsigned char)token[i];

            if(a == 0)
                return 0;

            if((char)tolower(a) != (char)tolower(b))
                break;
        }

        if(i != nlen)
            continue;

        before = p == s ? 0 : (unsigned char)p[-1];
        after = (unsigned char)p[nlen];

        if((before == 0 || !ab_is_word_char(before)) &&
           (after == 0 || !ab_is_word_char(after)))
            return 1;
    }

    return 0;
}

static int ab_contains_axis_token_lc(const char *name)
{
    if(!name)
        return 0;

    if(ab_contains_token_lc(name, "ax") ||
       ab_contains_token_lc(name, "ay") ||
       ab_contains_token_lc(name, "bx") ||
       ab_contains_token_lc(name, "by"))
        return 1;

    if(ab_contains_lc(name, "axis x") ||
       ab_contains_lc(name, "axis y") ||
       ab_contains_lc(name, "point ax") ||
       ab_contains_lc(name, "point ay") ||
       ab_contains_lc(name, "point bx") ||
       ab_contains_lc(name, "point by"))
        return 1;

    return 0;
}

static int ab_auto_is_hard_reject(const char *name)
{
    if(!name)
        return 1;

    if(ab_contains_lc(name, "reset"))
        return 1;
    if(ab_contains_lc(name, "high quality"))
        return 1;
    if(ab_contains_lc(name, "operator"))
        return 1;
    if(ab_contains_lc(name, "update alpha"))
        return 1;
    if(ab_contains_lc(name, "lock"))
        return 1;
    if(ab_contains_lc(name, "enable"))
        return 1;
    if(ab_contains_lc(name, "take background"))
        return 1;
    if(ab_contains_lc(name, "clear alpha"))
        return 1;
    if(ab_contains_lc(name, "opacity"))
        return 1;

    return 0;
}

static int ab_auto_is_structural(const char *name)
{
    if(!name)
        return 1;

    /* Keep these under explicit user control. Do not drive them even in chaos mode. */
    if(ab_contains_lc(name, "render mode"))
        return 1;
    if(ab_contains_lc(name, "color mode"))
        return 1;
    if(ab_contains_lc(name, "view mode"))
        return 1;
    if(ab_contains_lc(name, "output mode"))
        return 1;
    if(ab_contains_lc(name, "blend mode"))
        return 1;
    if(ab_contains_lc(name, "mirror mode"))
        return 1;
    if(ab_contains_lc(name, "direction mode"))
        return 1;
    if(ab_contains_lc(name, "mode"))
        return 1;
    if(ab_contains_lc(name, "shape"))
        return 1;

    /* Allow color-phase/palette-feel parameters, reject palette selectors. */
    if(ab_contains_lc(name, "palette phase") ||
       ab_contains_lc(name, "color phase") ||
       ab_contains_lc(name, "nebula palette") ||
       ab_contains_lc(name, "pastel palette"))
        return 0;

    if(ab_contains_lc(name, "palette"))
        return 1;
    if(ab_contains_lc(name, "operator"))
        return 1;
    if(ab_contains_lc(name, "automatic"))
        return 1;
    if(ab_contains_lc(name, "clear background"))
        return 1;
    if(ab_contains_lc(name, "black background"))
        return 1;
    if(ab_contains_lc(name, "keep original"))
        return 1;
    if(ab_contains_lc(name, "lock update"))
        return 1;
    if(ab_contains_lc(name, "clear alpha"))
        return 1;
    if(ab_contains_lc(name, "to alpha"))
        return 1;
    if(ab_contains_lc(name, "channel"))
        return 1;
    if(ab_contains_lc(name, "invert"))
        return 1;
    if(ab_contains_lc(name, "reverse"))
        return 1;
    if(ab_contains_lc(name, "swap"))
        return 1;
    if(ab_contains_lc(name, "pingpong"))
        return 1;
    if(ab_contains_lc(name, "ping pong"))
        return 1;
    if(ab_contains_lc(name, "horizontal"))
        return 1;
    if(ab_contains_lc(name, "vertical"))
        return 1;
    if(ab_contains_lc(name, "red"))
        return 1;
    if(ab_contains_lc(name, "green"))
        return 1;
    if(ab_contains_lc(name, "blue"))
        return 1;
    if(ab_contains_lc(name, "old cb"))
        return 1;
    if(ab_contains_lc(name, "old cr"))
        return 1;
    if(ab_contains_lc(name, "new cb"))
        return 1;
    if(ab_contains_lc(name, "new cr"))
        return 1;
    if(ab_contains_lc(name, "center x"))
        return 1;
    if(ab_contains_lc(name, "center y"))
        return 1;
    if(ab_contains_lc(name, "x center"))
        return 1;
    if(ab_contains_lc(name, "y center"))
        return 1;
    if(ab_contains_lc(name, "target x"))
        return 1;
    if(ab_contains_lc(name, "target y"))
        return 1;
    if(ab_contains_lc(name, "point "))
        return 1;
    if(ab_contains_lc(name, "x offset") || ab_contains_lc(name, "y offset"))
        return 1;
    if(ab_contains_lc(name, "offset x") || ab_contains_lc(name, "offset y"))
        return 1;
    if(ab_contains_lc(name, "x displacement") || ab_contains_lc(name, "y displacement"))
        return 1;
    if(ab_contains_axis_token_lc(name))
        return 1;

    return 0;
}

static int ab_auto_span_is_frame_like(int lo, int hi)
{
    int span = hi - lo;

    return span >= 8 && hi >= 8;
}

static int ab_auto_span_is_percent_like(int lo, int hi)
{
    return lo <= 0 && hi >= 80 && hi <= 255;
}

static int ab_auto_is_beat_time_name(const char *name, int lo, int hi)
{
    if(!name)
        return 0;

    if(ab_contains_lc(name, "history in frames"))
        return 1;
    if(ab_contains_lc(name, "history"))
        return ab_auto_span_is_frame_like(lo, hi);
    if(ab_contains_lc(name, "tempo"))
        return 1;
    if(ab_contains_lc(name, "framespeed"))
        return 1;
    if(ab_contains_lc(name, "frame speed"))
        return 1;
    if(ab_contains_lc(name, "frame delay"))
        return 1;
    if(ab_contains_lc(name, "frame length"))
        return 1;
    if(ab_contains_lc(name, "frame freq"))
        return 1;
    if(ab_contains_lc(name, "frame frequency"))
        return 1;
    if(ab_contains_lc(name, "frametime"))
        return 1;
    if(ab_contains_lc(name, "frame time"))
        return 1;
    if(ab_contains_lc(name, "refresh frames"))
        return 1;
    if(ab_contains_lc(name, "refresh frequency"))
        return 1;
    if(ab_contains_lc(name, "hold frame frequency"))
        return 1;
    if(ab_contains_lc(name, "hold mask frequency"))
        return 1;
    if(ab_contains_lc(name, "stop duration"))
        return 1;
    if(ab_contains_lc(name, "duration"))
        return 1;
    if(ab_contains_lc(name, "interval"))
        return 1;
    if(ab_contains_lc(name, "delay"))
        return 1;
    if(ab_contains_lc(name, "slice period"))
        return 1;
    if(ab_contains_lc(name, "period"))
        return 1;
    if(ab_contains_lc(name, "buffer length"))
        return 1;
    if(ab_contains_lc(name, "length") &&
       !ab_contains_lc(name, "bone length") &&
       !ab_contains_lc(name, "rib length") &&
       !ab_contains_lc(name, "tail length") &&
       !ab_contains_lc(name, "head size") &&
       !ab_contains_lc(name, "petal length") &&
       !ab_contains_lc(name, "strip width"))
        return ab_auto_span_is_frame_like(lo, hi);
    if(ab_contains_lc(name, "scratch frames"))
        return 1;
    if(ab_contains_lc(name, "scratch buffer"))
        return 1;
    if(ab_contains_lc(name, "lifespan"))
        return 1;
    if(ab_contains_lc(name, "temporal taps"))
        return 1;
    if(ab_contains_lc(name, "frames") && !ab_contains_lc(name, "interpolate frames"))
        return ab_auto_span_is_frame_like(lo, hi);
    if(ab_contains_lc(name, "time depth") || ab_contains_lc(name, "time slip") || ab_contains_lc(name, "time amount"))
        return ab_auto_span_is_frame_like(lo, hi);

    return 0;
}

static int ab_auto_is_memory_name(const char *name)
{
    if(!name)
        return 0;

    if(ab_contains_lc(name, "trail memory"))
        return 1;
    if(ab_contains_lc(name, "swirl memory"))
        return 1;
    if(ab_contains_lc(name, "echo memory"))
        return 1;
    if(ab_contains_lc(name, "residue memory"))
        return 1;
    if(ab_contains_lc(name, "terrain memory"))
        return 1;
    if(ab_contains_lc(name, "memory"))
        return 1;
    if(ab_contains_lc(name, "persistence"))
        return 1;
    if(ab_contains_lc(name, "feedback"))
        return 1;
    if(ab_contains_lc(name, "decay"))
        return 1;
    if(ab_contains_lc(name, "trail"))
        return 1;
    if(ab_contains_lc(name, "temporal smooth"))
        return 1;
    if(ab_contains_lc(name, "smoothing"))
        return 1;
    if(ab_contains_lc(name, "smoothness"))
        return 1;
    if(ab_contains_lc(name, "damping"))
        return 1;
    if(ab_contains_lc(name, "dampening"))
        return 1;
    if(ab_contains_lc(name, "mechanical inertia"))
        return 1;
    if(ab_contains_lc(name, "mechanical lag"))
        return 1;
    if(ab_contains_lc(name, "chroma persistence"))
        return 1;

    return 0;
}


#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
static const char *ab_auto_hint_class_name(int klass)
{
#ifdef VJ_BEAT_F_REJECT
    switch(klass)
    {
        case VJ_BEAT_OFF:                return "off";
        case VJ_BEAT_TRIGGER:            return "trigger";
        case VJ_BEAT_FLOW:               return "flow";
        case VJ_BEAT_DRIFT:              return "drift";
        case VJ_BEAT_WARP:               return "warp";
        case VJ_BEAT_MOTION_REACT:       return "motion-react";
        case VJ_BEAT_GEOMETRY_AMPLITUDE: return "geometry-amplitude";
        case VJ_BEAT_GEOMETRY_FREQUENCY: return "geometry-frequency";
        case VJ_BEAT_GEOMETRY_PHASE:     return "geometry-phase";
        case VJ_BEAT_GRID_SIZE:          return "grid-size";
        case VJ_BEAT_WINDOW_RADIUS:      return "window-radius";
        case VJ_BEAT_SPEED:              return "speed";
        case VJ_BEAT_SIGNED_SPEED:       return "signed-speed";
        case VJ_BEAT_SIGNED_CURVE:       return "signed-curve";
        case VJ_BEAT_MEMORY:             return "memory";
        case VJ_BEAT_INERTIA:            return "inertia";
        case VJ_BEAT_SOURCE_MIX:         return "source-mix";
        case VJ_BEAT_COLOR_AMOUNT:       return "color-amount";
        case VJ_BEAT_COLOR_PHASE:        return "color-phase";
        case VJ_BEAT_DETAIL:             return "detail";
        case VJ_BEAT_GLOW:               return "glow";
        case VJ_BEAT_INTENSITY:          return "intensity";
        case VJ_BEAT_CONTRAST:           return "contrast";
        case VJ_BEAT_TURBULENCE:         return "turbulence";
        case VJ_BEAT_KICK:               return "kick";
        case VJ_BEAT_SNARE:              return "snare";
        case VJ_BEAT_HAT:                return "hat";
#ifdef VJ_BEAT_SOURCE
        case VJ_BEAT_SOURCE:             return "source";
#endif
        case VJ_BEAT_SELECTOR:           return "selector";
        case VJ_BEAT_RESET:              return "reset";
        case VJ_BEAT_ALPHA_OR_OPACITY:   return "alpha-opacity";
        default:                         return "unknown";
    }
#else
    (void)klass;
    return "none";
#endif
}
#endif


#ifdef VJ_BEAT_F_REJECT
static inline int ab_auto_hint_class_is_binary_impulse(int klass)
{
    return klass == VJ_BEAT_TRIGGER ||
           klass == VJ_BEAT_SELECTOR ||
           klass == VJ_BEAT_RESET ||
           klass == VJ_BEAT_OFF;
}

static inline int ab_auto_target_is_binary_impulse(const ab_auto_target_t *t)
{
    if(!t)
        return 0;

    if(t->has_hint)
        return ab_auto_hint_class_is_binary_impulse(t->hint_class);

    return t->role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
}

static inline int ab_auto_target_is_continuous_impulse(const ab_auto_target_t *t)
{
    return t &&
           t->impulse &&
           t->has_hint &&
           !ab_auto_hint_class_is_binary_impulse(t->hint_class);
}

static inline int ab_auto_target_is_wrap(const ab_auto_target_t *t)
{
#if defined(VJ_BEAT_F_WRAP)
    return t && t->has_hint && (t->hint_flags & VJ_BEAT_F_WRAP);
#else
    (void)t;
    return 0;
#endif
}

static inline int ab_auto_target_is_discrete(const ab_auto_target_t *t)
{
#if defined(VJ_BEAT_F_DISCRETE)
    return t && t->has_hint && (t->hint_flags & VJ_BEAT_F_DISCRETE);
#else
    (void)t;
    return 0;
#endif
}
#endif

static int ab_auto_hint_to_role(
    int klass,
    unsigned int flags,
    int *role,
    int *invert,
    int *amount_pct,
    int *impulse,
    int *base_score
)
{
#ifdef VJ_BEAT_F_REJECT
    int r = VJ_AUDIO_BEAT_AUTO_ROLE_NONE;
    int inv = 0;
    int amt = 45;
    int imp = 0;
    int score = 0;

    if(!role || !invert || !amount_pct || !impulse || !base_score)
        return 0;

    if(flags & VJ_BEAT_F_REJECT)
        return 0;

    switch(klass)
    {
        case VJ_BEAT_TRIGGER:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
            amt = 100;
            imp = 1;
            score = 230;
            break;

        case VJ_BEAT_FLOW:
        case VJ_BEAT_DRIFT:
        case VJ_BEAT_WARP:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_FLOW;
            amt = klass == VJ_BEAT_WARP ? 58 : 54;
            score = 185;
            break;

        case VJ_BEAT_MOTION_REACT:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_MOTION;
            amt = 68;
            score = 210;
            break;

        case VJ_BEAT_GEOMETRY_AMPLITUDE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL;
            amt = 72;
            score = 150;
            break;

        case VJ_BEAT_GEOMETRY_FREQUENCY:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY;
            amt = 22;
            score = 118;
            break;

        case VJ_BEAT_GEOMETRY_PHASE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY;
            amt = 28;
            score = 128;
            break;

        case VJ_BEAT_GRID_SIZE:
        case VJ_BEAT_WINDOW_RADIUS:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL;
            amt = 48;
            score = 105;
            break;

        case VJ_BEAT_SPEED:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPEED;
            amt = 70;
            score = 175;
            break;

        case VJ_BEAT_SIGNED_SPEED:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPEED;
            amt = 58;
            score = 165;
            break;

        case VJ_BEAT_SIGNED_CURVE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY;
            amt = 20;
            score = 120;
            break;

        case VJ_BEAT_MEMORY:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY;
            amt = 58;
            inv = 1;
            score = 155;
            break;

        case VJ_BEAT_INERTIA:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY;
            amt = 50;
            score = 145;
            break;

        case VJ_BEAT_SOURCE_MIX:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE;
            amt = 24;
            score = 138;
            break;

        case VJ_BEAT_COLOR_AMOUNT:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_COLOR;
            amt = 42;
            score = 148;
            break;

        case VJ_BEAT_COLOR_PHASE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_COLOR;
            amt = 36;
            score = 152;
            break;

        case VJ_BEAT_DETAIL:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
            amt = 48;
            score = 162;
            break;

        case VJ_BEAT_GLOW:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
            amt = 54;
            score = 166;
            break;

        case VJ_BEAT_INTENSITY:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
            amt = 58;
            score = 172;
            break;

        case VJ_BEAT_CONTRAST:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST;
            amt = 50;
            score = 168;
            break;

        case VJ_BEAT_TURBULENCE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
            amt = 56;
            score = 170;
            break;

        case VJ_BEAT_KICK:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
            amt = 76;
            score = 222;
            break;

        case VJ_BEAT_SNARE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST;
            amt = 68;
            score = 214;
            break;

        case VJ_BEAT_HAT:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
            amt = 48;
            score = 198;
            break;
#ifdef VJ_BEAT_SOURCE
        case VJ_BEAT_SOURCE:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE;
            amt = 30;
            score = 140;
            break;
#endif
        case VJ_BEAT_ALPHA_OR_OPACITY:
            r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
            amt = 56;
            score = 170;
            break;

        case VJ_BEAT_SELECTOR:
        case VJ_BEAT_RESET:
        case VJ_BEAT_OFF:
        default:
            return 0;
    }

    if(flags & VJ_BEAT_F_IMPULSE)
    {
        imp = 1;
        score += 40;

        if(ab_auto_hint_class_is_binary_impulse(klass))
        {
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
            amt = 100;
        }
        else if(amt < 84)
        {
            amt = 84;
        }
    }

    if(flags & VJ_BEAT_F_PHRASE_ONLY)
        score -= 8;

#ifdef VJ_BEAT_F_CLIMAX_ONLY
    if(flags & VJ_BEAT_F_CLIMAX_ONLY)
        score -= 18;
#endif

    if(flags & VJ_BEAT_F_REBUILDS_STATE)
    {
        if(amt > 24)
            amt = 24;
        score -= 32;
    }

#ifdef VJ_BEAT_F_INVERTED
    if(flags & VJ_BEAT_F_INVERTED)
        inv = 1;
#endif

    *role = r;
    *invert = inv;
    *amount_pct = amt;
    *impulse = imp;
    *base_score = score;
    return 1;
#else
    (void)klass;
    (void)flags;
    (void)role;
    (void)invert;
    (void)amount_pct;
    (void)impulse;
    (void)base_score;
    return 0;
#endif
}

static int ab_auto_score_param_name(const char *name, int chaos, int lo, int hi, int *role, int *invert, int *amount_pct)
{
    int score = 0;
    int r = VJ_AUDIO_BEAT_AUTO_ROLE_NONE;
    int inv = 0;
    int amt = 45;
    int structural;
    int span = hi - lo;

    if(!name || !role || !invert || !amount_pct)
        return 0;

    if(ab_auto_is_hard_reject(name))
        return 0;

    structural = ab_auto_is_structural(name);

    if(structural)
        return 0;

    if(ab_contains_lc(name, "trigger") ||
       ab_contains_lc(name, "pulse") ||
       ab_contains_lc(name, "storm") ||
       ab_contains_lc(name, "ignition") ||
       ab_contains_lc(name, "discharge") ||
       ab_contains_lc(name, "break"))
    {
        score += 165;
        r = VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
        amt = 100;
    }

    if(ab_auto_is_beat_time_name(name, lo, hi))
    {
        score += 150;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE || r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME;
        amt = 100;
    }

    if(ab_contains_lc(name, "contrast") ||
       ab_contains_lc(name, "black level") ||
       ab_contains_lc(name, "white level") ||
       ab_contains_lc(name, "side wall") ||
       ab_contains_lc(name, "edge power") ||
       ab_contains_lc(name, "edge contrast"))
    {
        score += 118;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE || r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST;
        if(amt < 50)
            amt = 50;
    }

    if(ab_contains_lc(name, "mosh amount"))
    {
        score += 145;
        r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
        amt = 70;
    }

    if(ab_contains_lc(name, "amount") ||
       ab_contains_lc(name, "intensity") ||
       ab_contains_lc(name, "strength") ||
       ab_contains_lc(name, "impact") ||
       ab_contains_lc(name, "gain") ||
       ab_contains_lc(name, "exposure") ||
       ab_contains_lc(name, "charge") ||
       ab_contains_lc(name, "density") ||
       ab_contains_lc(name, "glow") ||
       ab_contains_lc(name, "white forge") ||
       ab_contains_lc(name, "fissure") ||
       ab_contains_lc(name, "power") ||
       ab_contains_lc(name, "amplification") ||
       ab_contains_lc(name, "brightness") ||
       ab_contains_lc(name, "contrast"))
    {
        score += 105;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT;
        if(amt < 55)
            amt = 55;
    }

    if(ab_contains_lc(name, "motion react") ||
       ab_contains_lc(name, "motion reactivity") ||
       ab_contains_lc(name, "motion gain") ||
       ab_contains_lc(name, "motion boost") ||
       ab_contains_lc(name, "motion sensitivity") ||
       ab_contains_lc(name, "motion pull") ||
       ab_contains_lc(name, "motion launch") ||
       ab_contains_lc(name, "motion ageing") ||
       ab_contains_lc(name, "maximum motion energy"))
    {
        score += 130;
        r = VJ_AUDIO_BEAT_AUTO_ROLE_MOTION;
        if(amt < 65)
            amt = 65;
    }

    if(ab_contains_lc(name, "build speed") ||
       ab_contains_lc(name, "cycle speed") ||
       ab_contains_lc(name, "flip speed") ||
       ab_contains_lc(name, "rotation speed") ||
       ab_contains_lc(name, "rot speed") ||
       ab_contains_lc(name, "spin speed") ||
       ab_contains_lc(name, "motion speed") ||
       ab_contains_lc(name, "flight speed") ||
       ab_contains_lc(name, "move speed") ||
       ab_contains_lc(name, "attack speed") ||
       ab_contains_lc(name, "release speed") ||
       ab_contains_lc(name, "flux speed") ||
       ab_contains_lc(name, "wavespeed") ||
       ab_contains_lc(name, "speed"))
    {
        score += 90;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE || r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPEED;
        if(amt < 45)
            amt = 45;
    }

    if(ab_contains_lc(name, "flow") ||
       ab_contains_lc(name, "swirl") ||
       ab_contains_lc(name, "warp") ||
       ab_contains_lc(name, "drift") ||
       ab_contains_lc(name, "turbulence") ||
       ab_contains_lc(name, "jitter") ||
       ab_contains_lc(name, "tear") ||
       ab_contains_lc(name, "curl") ||
       ab_contains_lc(name, "current") ||
       ab_contains_lc(name, "gravity") ||
       ab_contains_lc(name, "erosion") ||
       ab_contains_lc(name, "sediment") ||
       ab_contains_lc(name, "distortion") ||
       ab_contains_lc(name, "shatter") ||
       ab_contains_lc(name, "propagate") ||
       ab_contains_lc(name, "conductivity") ||
       ab_contains_lc(name, "accretion"))
    {
        score += 100;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE || r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_FLOW;
        if(amt < 55)
            amt = 55;
    }

    if(!ab_auto_is_beat_time_name(name, lo, hi) && ab_auto_is_memory_name(name))
    {
        score += 105;
        r = VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY;
        if(amt < 45)
            amt = 45;
        if(ab_contains_lc(name, "decay") || ab_contains_lc(name, "damping") || ab_contains_lc(name, "dampening"))
            inv = 1;
    }

    if(ab_contains_lc(name, "threshold") ||
       ab_contains_lc(name, "sensitivity") ||
       ab_contains_lc(name, "edge sens") ||
       ab_contains_lc(name, "tolerance") ||
       ab_contains_lc(name, "softness") ||
       ab_contains_lc(name, "sharpness") ||
       ab_contains_lc(name, "clip black") ||
       ab_contains_lc(name, "clip white") ||
       ab_contains_lc(name, "luma min") ||
       ab_contains_lc(name, "luma max") ||
       ab_contains_lc(name, "matte min") ||
       ab_contains_lc(name, "matte max") ||
       ab_contains_lc(name, "black threshold") ||
       ab_contains_lc(name, "white threshold") ||
       ab_contains_lc(name, "key reach"))
    {
        score += 60;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD;
        inv = 1;
        if(amt < 28)
            amt = 28;
    }

    if(ab_contains_lc(name, "radius") ||
       ab_contains_lc(name, "size") ||
       ab_contains_lc(name, "width") ||
       ab_contains_lc(name, "height") ||
       ab_contains_lc(name, "depth") ||
       ab_contains_lc(name, "scale") ||
       ab_contains_lc(name, "zoom") ||
       ab_contains_lc(name, "slice count") ||
       ab_contains_lc(name, "segment count") ||
       ab_contains_lc(name, "brush size") ||
       ab_contains_lc(name, "line size") ||
       ab_contains_lc(name, "stroke size") ||
       ab_contains_lc(name, "kernel size") ||
       ab_contains_lc(name, "step size") ||
       ab_contains_lc(name, "bone length") ||
       ab_contains_lc(name, "rib length") ||
       ab_contains_lc(name, "tail length") ||
       ab_contains_lc(name, "head size") ||
       ab_contains_lc(name, "view distance") ||
       ab_contains_lc(name, "cell size") ||
       ab_contains_lc(name, "pixel size") ||
       ab_contains_lc(name, "block size") ||
       ab_contains_lc(name, "tile size") ||
       ab_contains_lc(name, "strip width") ||
       ab_contains_lc(name, "spread") ||
       ab_contains_lc(name, "stride") ||
       ab_contains_lc(name, "spacing"))
    {
        score += 70;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL;
        if(amt < 35)
            amt = 35;
    }

    if(ab_contains_lc(name, "source feed") ||
       ab_contains_lc(name, "source bleed") ||
       ab_contains_lc(name, "source mix") ||
       ab_contains_lc(name, "source presence") ||
       ab_contains_lc(name, "source detail") ||
       ab_contains_lc(name, "source deposit") ||
       ab_contains_lc(name, "mix progress") ||
       ab_contains_lc(name, "mix weight"))
    {
        score += 75;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE || r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE;
        if(amt < 40)
            amt = 40;
    }

    if(ab_contains_lc(name, "hue") ||
       ab_contains_lc(name, "chroma") ||
       ab_contains_lc(name, "color") ||
       ab_contains_lc(name, "palette phase") ||
       ab_contains_lc(name, "color phase") ||
       ab_contains_lc(name, "pastel") ||
       ab_contains_lc(name, "nebula") ||
       ab_contains_lc(name, "rainbow") ||
       ab_contains_lc(name, "saturation") ||
       ab_contains_lc(name, "vibrance") ||
       ab_contains_lc(name, "temperature"))
    {
        score += 45;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_COLOR;
        if(amt < 30)
            amt = 30;
    }

    if(ab_contains_lc(name, "noise") ||
       ab_contains_lc(name, "grain") ||
       ab_contains_lc(name, "dust") ||
       ab_contains_lc(name, "flicker") ||
       ab_contains_lc(name, "scratch intensity") ||
       ab_contains_lc(name, "detail") ||
       ab_contains_lc(name, "chaos") ||
       ab_contains_lc(name, "randomness") ||
       ab_contains_lc(name, "character") ||
       ab_contains_lc(name, "shimmer"))
    {
        score += 55;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
        if(amt < 35)
            amt = 35;
    }

    if(ab_contains_lc(name, "angle") ||
       ab_contains_lc(name, "rotation") ||
       ab_contains_lc(name, "rotate") ||
       ab_contains_lc(name, "twist") ||
       ab_contains_lc(name, "pitch") ||
       ab_contains_lc(name, "yaw") ||
       ab_contains_lc(name, "camera") ||
       ab_contains_lc(name, "fov") ||
       ab_contains_lc(name, "perspective") ||
       ab_contains_lc(name, "curve") ||
       ab_contains_lc(name, "phase") ||
       ab_contains_lc(name, "offset angle") ||
       ab_contains_lc(name, "degrees"))
    {
        score += 24;
        if(r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
            r = VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY;
        if(amt < 14)
            amt = 14;
    }

    if(chaos && score <= 0 && span >= 8)
    {
        if(span >= 96)
            score += 44;
        else if(span >= 32)
            score += 30;
        else
            score += 16;

        r = VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
        amt = span >= 96 ? 36 : 28;
    }

    if(ab_contains_lc(name, "min "))
        score -= 25;
    if(ab_contains_lc(name, "max "))
        score -= 15;
    if(ab_contains_lc(name, "frequency") && r != VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME)
        score -= 8;
    if(ab_contains_lc(name, "smooth") && r != VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY)
        score -= 8;
    if(structural)
        score -= chaos ? 45 : 0;
    if(!ab_auto_span_is_percent_like(lo, hi) && r == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT)
        score += 8;
    if(r == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY)
        score -= 18;

    if(score <= 0 || r == VJ_AUDIO_BEAT_AUTO_ROLE_NONE)
        return 0;

    *role = r;
    *invert = inv;
    *amount_pct = amt;

    return score;
}

static int ab_auto_role_allowed(int mode, int role)
{
    if(role == VJ_AUDIO_BEAT_AUTO_ROLE_NONE ||
       role == VJ_AUDIO_BEAT_AUTO_ROLE_STRUCTURAL)
        return 0;

    if(mode == VJ_AUDIO_BEAT_AUTO_PRIMARY)
        return role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_SPEED ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_MOTION ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE;

    if(mode == VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION)
        return role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_SPEED ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_MOTION ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_FLOW ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_COLOR ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY ||
               role == VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD;

    if(mode == VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION_MEMORY ||
       mode == VJ_AUDIO_BEAT_AUTO_CHAOS)
        return 1;

    return 0;
}

static int ab_auto_role_quota(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
            return 3;

        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:
            return 2;

        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
            return 1;

        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD:
        case VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME:
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:
            return 1;

        default:
            return 0;
    }
}

static int ab_auto_role_priority(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:    return 1000;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:     return 960;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:     return 940;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:       return 920;
        case VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME:  return 900;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:     return 860;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:      return 840;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:     return 800;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:    return 760;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:    return 720;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:   return 700;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:      return 680;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:   return 500;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD:  return 560;
        default:                                 return 0;
    }
}

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
static const char *ab_auto_role_name(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return "trigger";
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return "amount";
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return "motion";
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return "flow";
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return "memory";
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return "speed";
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return "source";
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return "spatial";
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return "texture";
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return "contrast";
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return "color";
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return "geometry";
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return "threshold";
        case VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME: return "beat-time";
        default:                                return "unknown";
    }
}
#endif

static int ab_auto_sanitize_soft_range_values(
    int hard_min,
    int hard_max,
    int *soft_min,
    int *soft_max,
    int fx_id,
    int param_nr,
    const char *where
);

int vj_audio_beat_auto_build_table(void)
{
    int last_id;
    int built_fx = 0;
    int built_params = 0;
    int interesting = 0;
    int hinted = 0;
    int hinted_rejected = 0;

    if(ab_auto_fx_table_ready)
        return 1;

    if(ab_auto_fx_table_building)
        return 0;

    ab_auto_fx_table_building = 1;

    last_id = vje_get_last_id();

    if(last_id <= 0)
        last_id = vje_max_effects();

    if(last_id <= 0)
    {
        ab_auto_fx_table_building = 0;
        return 0;
    }

    ab_auto_fx_table = (ab_auto_fx_meta_t *)vj_calloc(sizeof(ab_auto_fx_meta_t) * (size_t)(last_id + 1));

    if(!ab_auto_fx_table)
    {
        ab_auto_fx_table_building = 0;
        veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO-BEAT] auto-fx unable to allocate FX metadata table");
        return 0;
    }

    ab_auto_fx_table_len = last_id + 1;

    for(int fx_id = 0; fx_id <= last_id; fx_id++)
    {
        int n_params;

#ifdef VJ_PLUGIN
        if(fx_id >= VJ_PLUGIN)
            continue;
#endif

        if(!vje_is_valid(fx_id))
            continue;

        n_params = vje_get_num_params(fx_id);

        if(n_params <= 0)
            continue;

        ab_auto_fx_table[fx_id].params = (ab_auto_param_meta_t *)vj_calloc(sizeof(ab_auto_param_meta_t) * (size_t)n_params);

        if(!ab_auto_fx_table[fx_id].params)
            continue;

        ab_auto_fx_table[fx_id].valid = 1;
        ab_auto_fx_table[fx_id].num_params = n_params;
        built_fx++;

        for(int p = 0; p < n_params; p++)
        {
            const char *name = vje_get_param_description(fx_id, p);
            int role = VJ_AUDIO_BEAT_AUTO_ROLE_NONE;
            int invert = 0;
            int amount_pct = 45;
            int impulse = 0;
            int score = 0;
            int lo;
            int hi;
            ab_auto_param_meta_t *m = &ab_auto_fx_table[fx_id].params[p];

            built_params++;

            lo = vje_get_param_min_limit(fx_id, p);
            hi = vje_get_param_max_limit(fx_id, p);

            if(hi <= lo)
                continue;

            m->has_range = 1;
            m->min_value = lo;
            m->max_value = hi;
            m->soft_min = VJ_BEAT_SOFT_UNSET;
            m->soft_max = VJ_BEAT_SOFT_UNSET;

#ifdef VJ_BEAT_F_REJECT
            {
                const vj_beat_param_hint_t *hint = vje_get_beat_hint(fx_id, p);

                if(hint && hint->klass != VJ_BEAT_OFF)
                {
                    int hint_score = 0;

                    m->has_hint = 1;
                    m->hint_class = hint->klass;
                    m->hint_flags = hint->flags;
                    m->soft_min = hint->soft_min;
                    m->soft_max = hint->soft_max;
                    ab_auto_sanitize_soft_range_values(lo, hi, &m->soft_min, &m->soft_max, fx_id, p, "hint-table");
                    m->normal_depth_pct = hint->normal_depth_pct;
                    m->climax_depth_pct = hint->climax_depth_pct;
                    m->attack_ms = hint->attack_ms;
                    m->release_ms = hint->release_ms;
                    m->hold_ms = hint->hold_ms;
                    m->priority = hint->priority;
                    hinted++;

                    if(hint->flags & VJ_BEAT_F_REJECT)
                    {
                        hinted_rejected++;
                        continue;
                    }

                    if(!ab_auto_hint_to_role(hint->klass, hint->flags, &role, &invert, &amount_pct, &impulse, &hint_score))
                    {
                        hinted_rejected++;
                        continue;
                    }

                    score = hint_score + hint->priority;

                    if(score <= 0)
                        continue;
                }
            }
#endif

            if(!m->has_hint)
            {
                if(!name)
                    continue;

                score = ab_auto_score_param_name(name, 0, lo, hi, &role, &invert, &amount_pct);

                if(score <= 0)
                {
                    score = ab_auto_score_param_name(name, 1, lo, hi, &role, &invert, &amount_pct);

                    if(score <= 0)
                        continue;
                }

                impulse = role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
            }

            m->valid = 1;
            m->score = score;
            m->role = role;
            m->invert = invert;
            m->amount_pct = amount_pct;
            m->impulse = impulse;
            m->min_value = lo;
            m->max_value = hi;
            interesting++;
        }
    }

    ab_auto_fx_table_ready = 1;
    ab_auto_fx_table_building = 0;

    return 1;
}

static const ab_auto_param_meta_t *ab_auto_meta_for(int fx_id, int param_nr)
{
    if(!ab_auto_fx_table_ready)
        return NULL;

    if(fx_id < 0 || fx_id >= ab_auto_fx_table_len)
        return NULL;

    if(!ab_auto_fx_table[fx_id].valid || !ab_auto_fx_table[fx_id].params)
        return NULL;

    if(param_nr < 0 || param_nr >= ab_auto_fx_table[fx_id].num_params)
        return NULL;

    if(!ab_auto_fx_table[fx_id].params[param_nr].valid)
        return NULL;

    return &ab_auto_fx_table[fx_id].params[param_nr];
}

static int ab_auto_hint_class_curve_mix_compatible(int klass)
{
    switch(klass)
    {
        case VJ_BEAT_FLOW:
        case VJ_BEAT_DRIFT:
        case VJ_BEAT_WARP:
        case VJ_BEAT_MOTION_REACT:
        case VJ_BEAT_GEOMETRY_AMPLITUDE:
        case VJ_BEAT_GEOMETRY_FREQUENCY:
        case VJ_BEAT_MEMORY:
        case VJ_BEAT_INERTIA:
        case VJ_BEAT_COLOR_AMOUNT:
        case VJ_BEAT_DETAIL:
        case VJ_BEAT_GLOW:
        case VJ_BEAT_ALPHA_OR_OPACITY:
        case VJ_BEAT_TRAIL_LENGTH:
        case VJ_BEAT_DENSITY:
        case VJ_BEAT_CONTRAST:
        case VJ_BEAT_INTENSITY:
        case VJ_BEAT_TURBULENCE:
        case VJ_BEAT_KICK:
        case VJ_BEAT_SNARE:
        case VJ_BEAT_HAT:
            return 1;

        default:
            return 0;
    }
}

static int ab_auto_role_curve_mix_compatible(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:
            return 1;

        default:
            return 0;
    }
}

static int ab_auto_param_meta_curve_mixable(const ab_auto_param_meta_t *m)
{
    unsigned int reject_flags;

    if(!m || !m->valid)
        return 0;

    reject_flags = VJ_BEAT_F_REJECT |
                   VJ_BEAT_F_STRUCTURAL |
                   VJ_BEAT_F_REBUILDS_STATE |
                   VJ_BEAT_F_IMPULSE |
                   VJ_BEAT_F_DISCRETE;

    if(m->hint_flags & reject_flags)
        return 0;

    if(m->has_hint)
        return ab_auto_hint_class_curve_mix_compatible(m->hint_class);

    return ab_auto_role_curve_mix_compatible(m->role);
}

static int ab_auto_entry_param_curve_enabled(sample_eff_chain *entry, int param_nr)
{
    char key[40];
    int status = 0;

    if(!entry || !entry->kf || !entry->kf_status)
        return 0;

    if(param_nr < 0 || param_nr >= SAMPLE_MAX_PARAMETERS)
        return 0;

    snprintf(key, sizeof(key), "status_p%d", param_nr);

    if(vevo_property_get(entry->kf, key, 0, &status) != 0)
        return 0;

    return status ? 1 : 0;
}

static int ab_auto_entry_param_curve_value(sample_eff_chain *entry, int param_nr, long long n_frame, int *value)
{
    if(!value)
        return 0;

    if(!ab_auto_entry_param_curve_enabled(entry, param_nr))
        return 0;

    return get_keyframe_value(entry->kf, n_frame, param_nr, value) ? 1 : 0;
}

static int ab_auto_target_curve_owned_now(const ab_auto_target_t *t)
{
    if(!t || !t->entry_ptr)
        return 0;

    return ab_auto_entry_param_curve_enabled(t->entry_ptr, t->param_nr);
}

static int ab_auto_chain_signature(void *ctx, int chain_len, vj_audio_beat_get_fx_id_func get_fx_id)
{
    int sig = 5381;

    if(!ctx || !get_fx_id)
        return 0;

    for(int i = 0; i < chain_len; i++)
    {
        int fx_id = get_fx_id(ctx, i);

        if(fx_id <= 0 || !vje_is_valid(fx_id))
            continue;

        sig = ((sig << 5) + sig) ^ ((fx_id & 0xffff) + (i << 16));
        sig = ((sig << 5) + sig) ^ (vje_get_num_params(fx_id) & 0xff);
    }

    return sig;
}

static int ab_auto_insert_candidate(ab_auto_target_t *dst, int *count, int max_count, const ab_auto_target_t *cand)
{
    int same_role = 0;
    int weakest_same_role = -1;
    int weakest_global = -1;
    int quota;
    int cand_rank;

    if(!dst || !count || !cand || !cand->valid || max_count <= 0)
        return 0;

    quota = ab_auto_role_quota(cand->role);

    if(quota <= 0)
        return 0;

    for(int i = 0; i < *count; i++)
    {
        int rank_i = dst[i].score + ab_auto_role_priority(dst[i].role);

        if(dst[i].effect_id == cand->effect_id &&
           dst[i].chain_pos == cand->chain_pos &&
           dst[i].param_nr == cand->param_nr)
            return 0;

        if(weakest_global < 0 ||
           rank_i < (dst[weakest_global].score + ab_auto_role_priority(dst[weakest_global].role)))
            weakest_global = i;

        if(dst[i].role == cand->role)
        {
            same_role++;

            if(weakest_same_role < 0 || dst[i].score < dst[weakest_same_role].score)
                weakest_same_role = i;
        }
    }

    if(same_role >= quota)
    {
        if(weakest_same_role >= 0 && cand->score > dst[weakest_same_role].score)
        {
            dst[weakest_same_role] = *cand;
            return 1;
        }

        return 0;
    }

    if(*count < max_count)
    {
        dst[*count] = *cand;
        (*count)++;
        return 1;
    }

    cand_rank = cand->score + ab_auto_role_priority(cand->role);

    if(weakest_global >= 0 &&
       cand_rank > (dst[weakest_global].score + ab_auto_role_priority(dst[weakest_global].role)))
    {
        dst[weakest_global] = *cand;
        return 1;
    }

    return 0;
}

static int ab_auto_insert_seed_candidate(ab_auto_target_t *dst, int *count, int max_count, const ab_auto_target_t *cand)
{
    int weakest_global = -1;
    int cand_rank;

    if(!dst || !count || !cand || !cand->valid || max_count <= 0)
        return 0;

    for(int i = 0; i < *count; i++)
    {
        int rank_i = dst[i].score + ab_auto_role_priority(dst[i].role);

        if(dst[i].effect_id == cand->effect_id &&
           dst[i].chain_pos == cand->chain_pos &&
           dst[i].param_nr == cand->param_nr)
            return 0;

        if(weakest_global < 0 ||
           rank_i < (dst[weakest_global].score + ab_auto_role_priority(dst[weakest_global].role)))
            weakest_global = i;
    }

    if(*count < max_count)
    {
        dst[*count] = *cand;
        (*count)++;
        return 1;
    }

    cand_rank = cand->score + ab_auto_role_priority(cand->role);

    if(weakest_global >= 0 &&
       cand_rank > (dst[weakest_global].score + ab_auto_role_priority(dst[weakest_global].role)))
    {
        dst[weakest_global] = *cand;
        return 1;
    }

    return 0;
}

static int ab_auto_abs_i_sane(int v)
{
    if(v == INT_MIN)
        return INT_MAX;

    return v < 0 ? -v : v;
}

static int ab_auto_max_i3(int a, int b, int c)
{
    int m = a > b ? a : b;

    return m > c ? m : c;
}

static int ab_auto_soft_value_sane(int v, int hard_min, int hard_max)
{
    int span;
    int hard_abs;
    int limit;

    if(v == VJ_BEAT_SOFT_UNSET)
        return 1;

    span = hard_max - hard_min;

    if(span < 0)
        span = -span;

    hard_abs = ab_auto_max_i3(ab_auto_abs_i_sane(hard_min),
                              ab_auto_abs_i_sane(hard_max),
                              span);

    if(hard_abs < 256)
        hard_abs = 256;

    limit = hard_abs * 16;

    if(limit < 4096 || limit < 0)
        limit = 4096;

    return ab_auto_abs_i_sane(v) <= limit;
}

static int ab_auto_sanitize_soft_range_values(
    int hard_min,
    int hard_max,
    int *soft_min,
    int *soft_max,
    int fx_id,
    int param_nr,
    const char *where
)
{
    int sm;
    int sx;
    int changed = 0;

    if(!soft_min || !soft_max)
        return 0;

    sm = *soft_min;
    sx = *soft_max;

    if(sm == VJ_BEAT_SOFT_UNSET && sx == VJ_BEAT_SOFT_UNSET)
        return 0;

    if(!ab_auto_soft_value_sane(sm, hard_min, hard_max) ||
       !ab_auto_soft_value_sane(sx, hard_min, hard_max))
    {
        *soft_min = VJ_BEAT_SOFT_UNSET;
        *soft_max = VJ_BEAT_SOFT_UNSET;
        return 1;
    }

    if(sm != VJ_BEAT_SOFT_UNSET && sx != VJ_BEAT_SOFT_UNSET)
    {
        if(sx < sm)
        {
            int tmp = sm;
            sm = sx;
            sx = tmp;
            changed = 1;
        }

        if(sx == sm || sx < hard_min || sm > hard_max)
        {
            *soft_min = VJ_BEAT_SOFT_UNSET;
            *soft_max = VJ_BEAT_SOFT_UNSET;
            return 1;
        }

        if(sm < hard_min)
        {
            sm = hard_min;
            changed = 1;
        }

        if(sx > hard_max)
        {
            sx = hard_max;
            changed = 1;
        }
    }

    if(sm != VJ_BEAT_SOFT_UNSET && sx == VJ_BEAT_SOFT_UNSET && sm > hard_max)
    {
        *soft_min = VJ_BEAT_SOFT_UNSET;
        return 1;
    }

    if(sx != VJ_BEAT_SOFT_UNSET && sm == VJ_BEAT_SOFT_UNSET && sx < hard_min)
    {
        *soft_max = VJ_BEAT_SOFT_UNSET;
        return 1;
    }

    if(changed)
    {
        *soft_min = sm;
        *soft_max = sx;
    }

    return changed;
}

static void ab_auto_apply_mechanical_pixels_profile(ab_auto_target_t *cand)
{
    if(!cand || cand->effect_id != 12)
        return;

    switch(cand->param_nr)
    {
        case 1: /* Pixel Size: visible percussive grid pulse, but still bounded. */
            cand->role = VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL;
            cand->invert = 1;
            cand->amount_pct = 62;
            cand->impulse = 0;
            cand->soft_min = 14;
            cand->soft_max = 42;
            cand->normal_depth_pct = 22;
            cand->climax_depth_pct = 64;
            cand->attack_ms = 70;
            cand->release_ms = 360;
            cand->hold_ms = 135;
#ifdef VJ_BEAT_F_REJECT
            cand->hint_flags &= ~VJ_BEAT_F_PHRASE_ONLY;
#endif
            cand->score += 18;
            break;

        case 2: /* 3D Depth: keep it moving, but do not collapse the relief. */
            cand->role = VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL;
            cand->invert = 0;
            cand->amount_pct = 76;
            cand->impulse = 0;
            cand->soft_min = 58;
            cand->soft_max = 100;
            cand->normal_depth_pct = 34;
            cand->climax_depth_pct = 70;
            cand->attack_ms = 55;
            cand->release_ms = 520;
            cand->hold_ms = 0;
            cand->score += 12;
            break;

        case 3: /* Cycle Speed is the main "this is alive" parameter. */
            cand->role = VJ_AUDIO_BEAT_AUTO_ROLE_SPEED;
            cand->invert = 0;
            cand->amount_pct = 92;
            cand->impulse = 0;
            cand->soft_min = 10;
            cand->soft_max = 86;
            cand->normal_depth_pct = 42;
            cand->climax_depth_pct = 88;
            cand->attack_ms = 35;
            cand->release_ms = 420;
            cand->hold_ms = 0;
            cand->score += 32;
            break;

        case 4: /* Trigger is really a threshold/sensitivity control: lower means more reactive. */
            cand->role = VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER;
            cand->invert = 1;
            cand->amount_pct = 100;
            cand->impulse = 1;
            cand->soft_min = 8;
            cand->soft_max = 46;
            cand->normal_depth_pct = 100;
            cand->climax_depth_pct = 100;
            cand->attack_ms = 15;
            cand->release_ms = 160;
            cand->hold_ms = 0;
            cand->score += 46;
            break;

        case 6: /* Mechanical Inertia: lower on drum hits so actuators snap, release back slowly. */
            cand->role = VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY;
            cand->invert = 1;
            cand->amount_pct = 86;
            cand->impulse = 0;
            cand->soft_min = 24;
            cand->soft_max = 92;
            cand->normal_depth_pct = 42;
            cand->climax_depth_pct = 82;
            cand->attack_ms = 45;
            cand->release_ms = 620;
            cand->hold_ms = 115;
#ifdef VJ_BEAT_F_REJECT
            cand->hint_flags &= ~VJ_BEAT_F_PHRASE_ONLY;
#endif
            cand->score += 24;
            break;

        default:
            break;
    }

    ab_auto_sanitize_soft_range_values(cand->min_value, cand->max_value,
                                       &cand->soft_min, &cand->soft_max,
                                       cand->effect_id, cand->param_nr,
                                       "mechanical-profile");
}


#ifdef VJ_BEAT_F_REJECT
static void ab_auto_normalize_continuous_impulse_base(ab_auto_target_t *cand)
{
    int span;

    if(!ab_auto_target_is_continuous_impulse(cand))
        return;

    if(cand->invert)
        return;

    span = cand->max_value - cand->min_value;

    if(span <= 0)
        return;

    if(cand->base_value >= cand->max_value)
    {
        cand->base_value = cand->min_value;
        cand->last_value = cand->base_value;
    }
}
#endif

static void ab_auto_copy_meta_to_target(ab_auto_target_t *cand, const ab_auto_param_meta_t *m)
{
    if(!cand || !m)
        return;

    cand->has_hint = m->has_hint;
    cand->hint_class = m->hint_class;
    cand->hint_flags = m->hint_flags;
    cand->soft_min = m->soft_min;
    cand->soft_max = m->soft_max;
    ab_auto_sanitize_soft_range_values(cand->min_value, cand->max_value, &cand->soft_min, &cand->soft_max, cand->effect_id, cand->param_nr, "target-copy");
    cand->normal_depth_pct = m->normal_depth_pct;
    cand->climax_depth_pct = m->climax_depth_pct;
    cand->attack_ms = m->attack_ms;
    cand->release_ms = m->release_ms;
    cand->hold_ms = m->hold_ms;
#ifdef VJ_BEAT_F_REJECT
    if(cand->has_hint)
    {
        if(cand->hold_ms < 0)
            cand->hold_ms = 0;
        else if(cand->hold_ms > 10000)
            cand->hold_ms = 10000;
    }
#endif
    cand->priority = m->priority;

    ab_auto_apply_mechanical_pixels_profile(cand);

#ifdef VJ_BEAT_F_REJECT
    ab_auto_normalize_continuous_impulse_base(cand);
#endif
}

static int ab_auto_insert_numeric_fallback(
    ab_auto_target_t *dst,
    int *count,
    int max_count,
    void *ctx,
    int chain_len,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_get_fx_entry_func get_entry,
    int *scanned_fx,
    int *scanned_params
)
{
    int inserted = 0;

    if(!dst || !count || !ctx || !get_fx_id || !get_arg || chain_len <= 0)
        return 0;

    for(int chain_pos = 0; chain_pos < chain_len; chain_pos++)
    {
        ab_auto_target_t best[3];
        int best_rank[3] = { -2147483647, -2147483647, -2147483647 };
        int fx_id = get_fx_id(ctx, chain_pos);
        sample_eff_chain *entry = get_entry ? get_entry(ctx, chain_pos) : NULL;
        int n_params;

        memset(best, 0, sizeof(best));

        if(fx_id <= 0 || !vje_is_valid(fx_id))
            continue;

        if(fx_id >= ab_auto_fx_table_len || !ab_auto_fx_table[fx_id].valid)
            continue;

        n_params = ab_auto_fx_table[fx_id].num_params;

        if(scanned_fx)
            (*scanned_fx)++;

        for(int p = 0; p < n_params; p++)
        {
            ab_auto_param_meta_t *m = &ab_auto_fx_table[fx_id].params[p];
            const char *name = vje_get_param_description(fx_id, p);
            ab_auto_target_t cand;
            int span;
            int base;
            int rank;

            if(scanned_params)
                (*scanned_params)++;

            if(!m || !m->has_range)
                continue;

            if(m->has_hint)
                continue;

            if(name && (ab_auto_is_hard_reject(name) || ab_auto_is_structural(name)))
                continue;

            span = m->max_value - m->min_value;

            if(span <= 3)
                continue;

            rank = span;

            if(span >= 96)
                rank += 260;
            else if(span >= 64)
                rank += 210;
            else if(span >= 24)
                rank += 90;
            else if(span >= 8)
                rank += 25;
            else
                rank -= 160;

            if(p > 0)
                rank += 25;
            else if(span <= 32)
                rank -= 90;

            if(m->valid)
                rank -= 60;

            if(rank <= 0)
                continue;

            if(ab_auto_entry_param_curve_enabled(entry, p))
                continue;

            base = get_arg(ctx, chain_pos, p);

            if(base < m->min_value)
                base = m->min_value;
            else if(base > m->max_value)
                base = m->max_value;

            memset(&cand, 0, sizeof(cand));
            cand.valid = 1;
            cand.effect_id = fx_id;
            cand.chain_pos = chain_pos;
            cand.param_nr = p;
            cand.entry_ptr = entry;
            cand.curve_owned = ab_auto_entry_param_curve_enabled(entry, p);
            cand.curve_mixable = cand.curve_owned ? ab_auto_param_meta_curve_mixable(m) : 0;
            cand.base_value = base;
            cand.min_value = m->min_value;
            cand.max_value = m->max_value;
            cand.last_value = base;
            cand.score = rank;
            cand.role = span >= 64 ? VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT : VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE;
            cand.invert = 0;
            cand.amount_pct = span >= 64 ? 42 : 34;
            cand.impulse = 0;
            ab_auto_copy_meta_to_target(&cand, m);

            for(int k = 0; k < 3; k++)
            {
                if(rank > best_rank[k])
                {
                    for(int mpos = 2; mpos > k; mpos--)
                    {
                        best[mpos] = best[mpos - 1];
                        best_rank[mpos] = best_rank[mpos - 1];
                    }

                    best[k] = cand;
                    best_rank[k] = rank;
                    break;
                }
            }
        }

        for(int k = 0; k < 3; k++)
        {
            if(best[k].valid)
                inserted += ab_auto_insert_candidate(dst, count, max_count, &best[k]);
        }
    }

    return inserted;
}


static void ab_auto_trim_macro_state_on_map_change(void)
{
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    float old_groove = ab_auto_groove_level;
    float old_phrase = ab_auto_phrase_level;
    float old_climax = ab_auto_climax_level;
#endif

    ab_auto_groove_level *= 0.64f;
    ab_auto_phrase_level *= 0.58f;
    ab_auto_climax_level *= 0.44f;

    if(ab_auto_groove_level > 0.52f)
        ab_auto_groove_level = 0.52f;
    if(ab_auto_phrase_level > 0.56f)
        ab_auto_phrase_level = 0.56f;
    if(ab_auto_climax_level > 0.42f)
        ab_auto_climax_level = 0.42f;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    AB_AUTO_DBG("macro-trim map-change groove=%.3f->%.3f phrase=%.3f->%.3f climax=%.3f->%.3f",
                old_groove,
                ab_auto_groove_level,
                old_phrase,
                ab_auto_phrase_level,
                old_climax,
                ab_auto_climax_level);
#endif
}

static int ab_auto_rebuild_map(void *ctx, int chain_len, vj_audio_beat_get_fx_id_func get_fx_id, vj_audio_beat_get_fx_arg_func get_arg, vj_audio_beat_get_fx_entry_func get_entry)
{
    ab_auto_target_t chosen[VJ_AUDIO_BEAT_AUTO_MAX_TARGETS];
    int count = 0;
    int mode;
    int scanned_fx = 0;
    int scanned_params = 0;
    int seeded_targets = 0;
    int fallback_fx = 0;
    int fallback_params = 0;

    if(!ctx || !get_fx_id || !get_arg || chain_len <= 0)
        return 0;

    if(!ab_auto_fx_table_ready)
    {
        static int warned = 0;

        if(!warned)
        {
            warned = 1;
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[AUDIO-BEAT] auto-fx metadata table is not built; call vj_audio_beat_auto_build_table() after vje_init() and before audio beat thread creation");
        }

        return 0;
    }

    memset(chosen, 0, sizeof(chosen));

    mode = ab_load_i(&ab_auto_mode);

    if(mode <= VJ_AUDIO_BEAT_AUTO_OFF)
        return 0;

    if(mode > VJ_AUDIO_BEAT_AUTO_CHAOS)
        mode = VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION;

    for(int chain_pos = 0; chain_pos < chain_len; chain_pos++)
    {
        ab_auto_target_t best_seed;
        int best_seed_rank = -2147483647;
        int fx_id = get_fx_id(ctx, chain_pos);
        sample_eff_chain *entry = get_entry ? get_entry(ctx, chain_pos) : NULL;
        int n_params;

        memset(&best_seed, 0, sizeof(best_seed));

        if(fx_id <= 0 || !vje_is_valid(fx_id))
            continue;

        if(fx_id >= ab_auto_fx_table_len || !ab_auto_fx_table[fx_id].valid)
            continue;

        n_params = ab_auto_fx_table[fx_id].num_params;
        scanned_fx++;

        for(int p = 0; p < n_params; p++)
        {
            const ab_auto_param_meta_t *m = ab_auto_meta_for(fx_id, p);
            ab_auto_target_t cand;
            int base;
            int rank;

            scanned_params++;

            if(!m)
                continue;

            if(!ab_auto_role_allowed(mode, m->role))
                continue;

            if(ab_auto_entry_param_curve_enabled(entry, p))
                continue;

            base = get_arg(ctx, chain_pos, p);

            if(base < m->min_value)
                base = m->min_value;
            else if(base > m->max_value)
                base = m->max_value;

            memset(&cand, 0, sizeof(cand));

            cand.valid = 1;
            cand.effect_id = fx_id;
            cand.chain_pos = chain_pos;
            cand.param_nr = p;
            cand.entry_ptr = entry;
            cand.curve_owned = ab_auto_entry_param_curve_enabled(entry, p);
            cand.curve_mixable = cand.curve_owned ? ab_auto_param_meta_curve_mixable(m) : 0;
            cand.base_value = base;
            cand.min_value = m->min_value;
            cand.max_value = m->max_value;
            cand.last_value = base;
            cand.score = m->score;
            cand.role = m->role;
            cand.invert = m->invert;
            cand.amount_pct = m->amount_pct;
            cand.impulse = m->impulse;
            ab_auto_copy_meta_to_target(&cand, m);

            rank = cand.score + ab_auto_role_priority(cand.role);

            if(rank > best_seed_rank)
            {
                best_seed = cand;
                best_seed_rank = rank;
            }

            ab_auto_insert_candidate(chosen, &count, VJ_AUDIO_BEAT_AUTO_MAX_TARGETS, &cand);
        }

        if(best_seed.valid)
            seeded_targets += ab_auto_insert_seed_candidate(chosen, &count, VJ_AUDIO_BEAT_AUTO_MAX_TARGETS, &best_seed);
    }

    if(count < VJ_AUDIO_BEAT_AUTO_MAX_TARGETS)
    {
        (void) ab_auto_insert_numeric_fallback(chosen, &count, VJ_AUDIO_BEAT_AUTO_MAX_TARGETS,
                                               ctx, chain_len, get_fx_id, get_arg, get_entry,
                                               &fallback_fx, &fallback_params);
    }

    memset(ab_auto_targets, 0, sizeof(ab_auto_targets));

    for(int i = 0; i < count; i++)
        ab_auto_targets[i] = chosen[i];

    ab_auto_target_count = count;
    ab_auto_active = 0;
    ab_store_i(&ab_auto_dirty, 0);
    
    return count;
}

static float ab_auto_mix3(float a, float b, float c)
{
    float v = a + b + c;

    if(v < 0.0f)
        return 0.0f;

    if(v > 1.0f)
        return 1.0f;

    return v;
}


static float ab_auto_activity_gate(const vj_audio_beat_snapshot_t *snap)
{
    float body;
    float v;

    if(!snap)
        return 0.0f;

    body = snap->level * 3.20f +
           snap->envelope * 2.60f +
           snap->transient * 0.90f +
           snap->flux * 0.80f +
           snap->kick * 1.10f +
           snap->snare * 0.95f +
           snap->hat * 0.70f +
           snap->beat_pulse * 1.10f +
           snap->beat_gate * 1.20f;

    v = (body - 0.050f) * 1.42f;

    if(v <= 0.0f)
        return 0.0f;
    if(v >= 1.0f)
        return 1.0f;

    return v * v * (3.0f - 2.0f * v);
}

static float ab_auto_low_activity_keep(int role, float groove, float phrase, float climax, float activity, long beat_age_ms)
{
    float keep = 0.0f;
    float age_scale = 1.0f;

    if(activity < 0.035f && beat_age_ms > 360)
        return 0.0f;

    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
            keep = groove * 0.040f + phrase * 0.055f + climax * 0.030f;
            break;

        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
            keep = groove * 0.026f + phrase * 0.030f + climax * 0.020f;
            break;

        default:
            keep = groove * 0.018f + phrase * 0.020f;
            break;
    }

    if(beat_age_ms > 1200)
        age_scale = 0.12f;
    else if(beat_age_ms > 720)
        age_scale = 0.26f;
    else if(beat_age_ms > 420)
        age_scale = 0.48f;

    keep *= age_scale;

    if(activity < 0.080f)
        keep *= activity * 12.5f;
    else if(activity > 0.450f)
        keep *= 0.35f;

    if(keep < 0.0f)
        keep = 0.0f;
    else if(keep > 0.16f)
        keep = 0.16f;

    return keep;
}

static float ab_auto_beat_ms_from_snapshot(const vj_audio_beat_snapshot_t *snap)
{
    float beat_ms = 600.0f;

    if(snap && snap->bpm > 1.0f)
    {
        beat_ms = 60000.0f / snap->bpm;

        if(beat_ms < 180.0f)
            beat_ms = 180.0f;
        else if(beat_ms > 1800.0f)
            beat_ms = 1800.0f;
    }

    return beat_ms;
}

static float ab_auto_beat_phase(const vj_audio_beat_snapshot_t *snap)
{
    float beat_ms;
    float age;
    int loops;

    if(!snap || snap->bpm <= 1.0f || snap->beat_age_ms < 0)
        return 0.0f;

    beat_ms = ab_auto_beat_ms_from_snapshot(snap);
    age = (float)snap->beat_age_ms;

    if(age < 0.0f)
        age = 0.0f;

    loops = (int)(age / beat_ms);
    age -= (float)loops * beat_ms;

    if(age < 0.0f)
        age = 0.0f;
    else if(age >= beat_ms)
        age = beat_ms - 0.001f;

    return age / beat_ms;
}

static float ab_auto_phase_hit(float phase, float center, float width)
{
    float d;
    float v;

    d = phase - center;

    if(d < 0.0f)
        d = -d;

    if(d > 0.5f)
        d = 1.0f - d;

    if(width <= 0.0001f)
        width = 0.0001f;

    v = 1.0f - (d / width);

    if(v <= 0.0f)
        return 0.0f;
    if(v >= 1.0f)
        return 1.0f;

    return v * v * (3.0f - 2.0f * v);
}

static float ab_auto_musical_pulse(const vj_audio_beat_snapshot_t *snap)
{
    float phase;
    float down;
    float off;
    float pulse;

    if(!snap)
        return 0.0f;

    phase = ab_auto_beat_phase(snap);
    down = ab_auto_phase_hit(phase, 0.0f, 0.34f);
    off = ab_auto_phase_hit(phase, 0.50f, 0.20f);
    pulse = snap->beat_pulse;

    if(down * 0.62f > pulse)
        pulse = down * 0.62f;

    if(off * 0.22f > pulse)
        pulse = off * 0.22f;

    if(pulse > 1.0f)
        pulse = 1.0f;

    return pulse;
}

static float ab_auto_subdivision_pulse(const vj_audio_beat_snapshot_t *snap)
{
    float phase;
    float v;

    if(!snap || snap->bpm <= 1.0f)
        return 0.0f;

    phase = ab_auto_beat_phase(snap);

    v = ab_auto_phase_hit(phase, 0.25f, 0.16f);

    {
        float b = ab_auto_phase_hit(phase, 0.50f, 0.18f);
        float c = ab_auto_phase_hit(phase, 0.75f, 0.16f);

        if(b > v) v = b;
        if(c > v) v = c;
    }

    return v;
}

typedef struct
{
    float groove;
    float phrase;
    float musical;
    float subdiv;
    float hit;
    float trigger;
    float drum;
    float body;
    float density;
    float tempo_fast;
    float tempo_slow;
    float restraint;
    float activity;
} ab_auto_frame_signal_t;

static void ab_auto_build_frame_signal(const vj_audio_beat_snapshot_t *snap,
                                       float climax,
                                       ab_auto_frame_signal_t *fs)
{
    if(!fs)
        return;

    memset(fs, 0, sizeof(*fs));

    if(!snap)
        return;

    fs->groove = ab_auto_groove_level;
    fs->phrase = ab_auto_phrase_level;
    fs->musical = ab_auto_musical_pulse(snap);
    fs->subdiv = ab_auto_subdivision_pulse(snap);

    fs->trigger = snap->beat_gate > snap->beat_pulse ? snap->beat_gate : snap->beat_pulse;
    fs->trigger = fs->trigger < 0.0f ? 0.0f : (fs->trigger > 1.0f ? 1.0f : fs->trigger);

    fs->hit = fs->trigger;
    fs->hit = (snap->transient * 0.92f) > fs->hit ? snap->transient * 0.92f : fs->hit;
    fs->hit = (snap->flux * 0.64f) > fs->hit ? snap->flux * 0.64f : fs->hit;
    fs->hit = fs->hit < 0.0f ? 0.0f : (fs->hit > 1.0f ? 1.0f : fs->hit);

    fs->drum = fs->trigger * 0.42f +
               snap->transient * 0.30f +
               snap->flux * 0.24f +
               snap->kick * 0.34f +
               snap->snare * 0.26f +
               snap->hat * 0.14f +
               snap->envelope * 0.16f +
               snap->level * 0.14f;

    if(snap->bass > 0.72f)
        fs->drum += 0.10f * (0.35f + snap->flux * 0.65f);
    if(snap->mid > 0.72f)
        fs->drum += 0.08f * (0.35f + snap->flux * 0.65f);
    if(snap->high > 0.72f)
        fs->drum += 0.08f * (0.35f + snap->flux * 0.65f);

    fs->drum = fs->drum < 0.0f ? 0.0f : (fs->drum > 1.0f ? 1.0f : fs->drum);

    fs->body = snap->level * 0.28f +
               snap->envelope * 0.30f +
               snap->bass * 0.14f +
               snap->mid * 0.16f +
               snap->high * 0.12f +
               snap->flux * 0.14f +
               fs->drum * 0.10f;

    if(fs->body < 0.0f)
        fs->body = 0.0f;
    else if(fs->body > 1.0f)
        fs->body = 1.0f;

    fs->density = fs->body * 0.46f +
                  snap->beat_density * 0.20f +
                  fs->groove * 0.16f +
                  fs->phrase * 0.08f +
                  fs->musical * 0.10f +
                  climax * 0.08f;

    if(fs->density < 0.0f)
        fs->density = 0.0f;
    else if(fs->density > 1.0f)
        fs->density = 1.0f;

    if(snap->bpm > 1.0f)
    {
        fs->tempo_fast = (snap->bpm - 150.0f) * (1.0f / 110.0f);
        fs->tempo_slow = (105.0f - snap->bpm) * (1.0f / 70.0f);

        if(fs->tempo_fast < 0.0f)
            fs->tempo_fast = 0.0f;
        else if(fs->tempo_fast > 1.0f)
            fs->tempo_fast = 1.0f;

        if(fs->tempo_slow < 0.0f)
            fs->tempo_slow = 0.0f;
        else if(fs->tempo_slow > 1.0f)
            fs->tempo_slow = 1.0f;
    }

    fs->restraint = fs->tempo_fast * (0.26f + fs->density * 0.50f);

    if(fs->restraint > 1.0f)
        fs->restraint = 1.0f;

    fs->subdiv *= 1.0f - fs->restraint * 0.62f;
    fs->hit = fs->hit * (1.0f - fs->restraint * 0.16f) + fs->body * fs->restraint * 0.06f;

    if(fs->subdiv < 0.0f)
        fs->subdiv = 0.0f;
    else if(fs->subdiv > 1.0f)
        fs->subdiv = 1.0f;

    if(fs->hit < 0.0f)
        fs->hit = 0.0f;
    else if(fs->hit > 1.0f)
        fs->hit = 1.0f;

    fs->activity = ab_auto_activity_gate(snap);

    if(fs->density > fs->activity)
        fs->activity = fs->activity * 0.70f + fs->density * 0.30f;

    if(fs->activity > 1.0f)
        fs->activity = 1.0f;
}

static float ab_auto_signal_for_role(const vj_audio_beat_snapshot_t *snap,
                                     const ab_auto_frame_signal_t *fs,
                                     int role)
{
    const float groove = fs ? fs->groove : 0.0f;
    const float phrase = fs ? fs->phrase : 0.0f;
    const float musical = fs ? fs->musical : 0.0f;
    const float subdiv = fs ? fs->subdiv : 0.0f;
    const float hit = fs ? fs->hit : 0.0f;
    const float trigger = fs ? fs->trigger : 0.0f;
    const float body = fs ? fs->body : 0.0f;
    const float density = fs ? fs->density : 0.0f;

    if(!snap)
        return 0.0f;

    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:
            return trigger;

        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:
            return ab_auto_mix3(snap->beat_pulse * 0.22f + musical * 0.14f + snap->kick * 0.18f,
                                snap->bass * 0.20f + snap->transient * 0.12f + body * 0.14f,
                                groove * 0.18f + phrase * 0.08f + density * 0.14f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
            return ab_auto_mix3(musical * 0.14f + subdiv * 0.04f + hit * 0.10f,
                                snap->mid * 0.10f + body * 0.08f,
                                snap->transient * 0.06f + groove * 0.16f + phrase * 0.08f + density * 0.06f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
            return ab_auto_mix3(snap->mid * 0.20f + snap->snare * 0.13f + body * 0.12f,
                                snap->high * 0.12f + snap->transient * 0.18f + snap->kick * 0.10f,
                                groove * 0.16f + musical * 0.10f + density * 0.12f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
            return ab_auto_mix3(snap->flux * 0.16f + snap->mid * 0.18f + body * 0.12f,
                                groove * 0.28f + subdiv * 0.06f + phrase * 0.06f,
                                snap->bass * 0.08f + musical * 0.08f + density * 0.10f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
            return ab_auto_mix3(snap->envelope * 0.28f + snap->bass * 0.20f,
                                groove * 0.30f,
                                phrase * 0.18f + musical * 0.08f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD:
            return ab_auto_mix3(snap->transient * 0.22f,
                                snap->high * 0.14f,
                                groove * 0.08f + musical * 0.05f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
            return ab_auto_mix3(snap->bass * 0.18f + snap->envelope * 0.16f + hit * 0.20f,
                                groove * 0.24f,
                                musical * 0.12f + phrase * 0.08f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:
            return ab_auto_mix3(snap->envelope * 0.20f + snap->mid * 0.16f + body * 0.14f,
                                snap->high * 0.18f + snap->flux * 0.16f,
                                groove * 0.12f + musical * 0.06f + density * 0.12f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
            return ab_auto_mix3(snap->high * 0.24f + snap->mid * 0.12f,
                                snap->beat_toggle * 0.10f + musical * 0.12f,
                                groove * 0.16f + phrase * 0.18f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
            return ab_auto_mix3(snap->envelope * 0.28f,
                                snap->bass * 0.18f + groove * 0.28f,
                                phrase * 0.12f + musical * 0.08f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
            return ab_auto_mix3(snap->high * 0.23f + snap->hat * 0.20f + density * 0.10f,
                                snap->transient * 0.16f + snap->snare * 0.10f + subdiv * 0.12f,
                                groove * 0.10f + musical * 0.08f + body * 0.08f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
            return ab_auto_mix3(snap->bass * 0.08f + snap->mid * 0.10f + body * 0.08f,
                                snap->flux * 0.08f + musical * 0.10f + density * 0.08f,
                                groove * 0.10f + phrase * 0.06f + snap->envelope * 0.06f);

        case VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME:
            return snap->bpm > 1.0f ? 1.0f : 0.0f;

        default:
            return 0.0f;
    }
}


#ifdef VJ_BEAT_F_REJECT
static float ab_auto_impulse_hint_signal(const vj_audio_beat_snapshot_t *snap,
                                         int hint_class,
                                         float trigger_hit)
{
    float gate;
    float v;

    if(!snap)
        return 0.0f;

    gate = snap->beat_gate;

    if(gate <= 0.0f)
        return 0.0f;

    if(gate > 1.0f)
        gate = 1.0f;

    switch(hint_class)
    {
        case VJ_BEAT_KICK:
            if(snap->hit_kind != AB_HIT_KICK &&
               snap->hit_kind != AB_HIT_FULL)
                return 0.0f;

            v = snap->kick;

            if(snap->bass > v)
                v = snap->bass;

            if((snap->transient * 0.72f) > v)
                v = snap->transient * 0.72f;
            break;

        case VJ_BEAT_SNARE:
            if(snap->hit_kind != AB_HIT_SNARE &&
               snap->hit_kind != AB_HIT_FULL)
                return 0.0f;

            v = snap->snare;

            if((snap->mid * 0.80f) > v)
                v = snap->mid * 0.80f;

            if((snap->high * 0.72f) > v)
                v = snap->high * 0.72f;

            if((snap->flux * 0.68f) > v)
                v = snap->flux * 0.68f;
            break;

        case VJ_BEAT_HAT:
            if(snap->hit_kind != AB_HIT_HAT)
                return 0.0f;

            v = snap->hat;

            if((snap->high * 0.86f) > v)
                v = snap->high * 0.86f;

            if((snap->flux * 0.62f) > v)
                v = snap->flux * 0.62f;
            break;

        default:
            v = trigger_hit;

            if(gate > v)
                v = gate;
            break;
    }

    if(v < 0.0f)
        v = 0.0f;
    else if(v > 1.0f)
        v = 1.0f;

    if(ab_auto_hint_class_is_binary_impulse(hint_class))
        v = gate * (0.68f + v * 0.32f);
    else
        v = gate * v;

    return v > 1.0f ? 1.0f : v;
}
#endif

static float ab_auto_signal_for_target(const vj_audio_beat_snapshot_t *snap,
                                       const ab_auto_target_t *t,
                                       float climax,
                                       const ab_auto_frame_signal_t *fs)
{
    float v;
    const float groove = fs ? fs->groove : 0.0f;
    const float phrase = fs ? fs->phrase : 0.0f;
    const float musical = fs ? fs->musical : 0.0f;
    const float subdiv = fs ? fs->subdiv : 0.0f;
    const float hit = fs ? fs->hit : 0.0f;
    const float trigger_hit = fs ? fs->trigger : 0.0f;
    const float drum = fs ? fs->drum : 0.0f;
    const float body = fs ? fs->body : 0.0f;
    const float density = fs ? fs->density : 0.0f;
    const float restraint = fs ? fs->restraint : 0.0f;
    const float tempo_slow = fs ? fs->tempo_slow : 0.0f;

    if(!snap || !t)
        return 0.0f;

    climax = climax < 0.0f ? 0.0f : (climax > 1.0f ? 1.0f : climax);
    v = ab_auto_signal_for_role(snap, fs, t->role);

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        switch(t->hint_class)
        {
            case VJ_BEAT_TRIGGER:
                v = trigger_hit;
                break;

            case VJ_BEAT_FLOW:
            case VJ_BEAT_DRIFT:
            case VJ_BEAT_WARP:
                v = ab_auto_mix3(snap->flux * 0.18f + snap->mid * 0.16f + body * 0.10f,
                                  groove * 0.28f + phrase * 0.16f + density * 0.12f,
                                  musical * 0.10f + subdiv * 0.06f);
                if(t->hint_class == VJ_BEAT_WARP)
                    v = v * 0.80f + climax * 0.08f + density * 0.14f;
                break;

            case VJ_BEAT_MOTION_REACT:
                v = ab_auto_mix3(snap->transient * 0.30f + snap->flux * 0.22f + drum * 0.20f,
                                  snap->mid * 0.14f + snap->bass * 0.10f,
                                  groove * 0.12f + musical * 0.12f);
                break;

            case VJ_BEAT_GEOMETRY_AMPLITUDE:
                v = ab_auto_mix3(drum * 0.36f + hit * 0.18f + body * 0.10f,
                                  snap->bass * 0.10f + snap->envelope * 0.10f + snap->transient * 0.10f,
                                  groove * 0.10f + phrase * 0.08f + musical * 0.08f + density * 0.10f);
                break;

            case VJ_BEAT_GEOMETRY_FREQUENCY:
            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                v = ab_auto_mix3(hit * 0.36f + drum * 0.22f + musical * 0.12f,
                                  subdiv * 0.12f + groove * 0.12f,
                                  phrase * 0.12f + snap->envelope * 0.08f + climax * 0.06f);
                break;

            case VJ_BEAT_GEOMETRY_PHASE:
                v = ab_auto_mix3(groove * 0.34f + phrase * 0.22f,
                                  musical * 0.14f + subdiv * 0.04f,
                                  snap->flux * 0.08f + tempo_slow * 0.08f);
                break;

            case VJ_BEAT_SPEED:
            case VJ_BEAT_SIGNED_SPEED:
                v = ab_auto_mix3(hit * 0.16f + drum * 0.12f + subdiv * 0.04f + musical * 0.12f,
                                  snap->mid * 0.08f + snap->transient * 0.06f + snap->flux * 0.05f,
                                  groove * 0.18f + phrase * 0.10f + density * 0.05f);
                break;

            case VJ_BEAT_SIGNED_CURVE:
                v = ab_auto_mix3(phrase * 0.34f,
                                  groove * 0.22f,
                                  snap->flux * 0.10f + climax * 0.12f);
                break;

            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
                v = ab_auto_mix3(hit * 0.22f + drum * 0.14f + snap->envelope * 0.20f,
                                  phrase * 0.18f + groove * 0.14f,
                                  climax * 0.08f + musical * 0.06f);
                break;

            case VJ_BEAT_SOURCE_MIX:
                v = ab_auto_mix3(snap->envelope * 0.22f,
                                  groove * 0.24f,
                                  phrase * 0.14f + musical * 0.06f);
                break;

            case VJ_BEAT_COLOR_AMOUNT:
            case VJ_BEAT_COLOR_PHASE:
                v = ab_auto_mix3(snap->high * 0.22f + snap->mid * 0.14f,
                                  groove * 0.18f + phrase * 0.18f,
                                  musical * 0.12f);
                break;

            case VJ_BEAT_DETAIL:
                v = ab_auto_mix3(snap->high * 0.30f,
                                  snap->flux * 0.16f + subdiv * 0.08f,
                                  groove * 0.16f + musical * 0.08f);
                break;

            case VJ_BEAT_GLOW:
            case VJ_BEAT_ALPHA_OR_OPACITY:
                v = ab_auto_mix3(snap->envelope * 0.30f + snap->bass * 0.18f,
                                  groove * 0.20f,
                                  phrase * 0.12f + musical * 0.08f);
                break;

            case VJ_BEAT_INTENSITY:
                v = ab_auto_mix3(snap->beat_pulse * 0.20f + musical * 0.14f + body * 0.18f,
                                  snap->bass * 0.18f + snap->mid * 0.12f + snap->high * 0.10f + snap->transient * 0.12f,
                                  groove * 0.16f + phrase * 0.08f + density * 0.18f);
                break;

            case VJ_BEAT_CONTRAST:
                v = ab_auto_mix3(snap->envelope * 0.20f + snap->mid * 0.16f + body * 0.14f,
                                  snap->high * 0.18f + snap->flux * 0.16f,
                                  groove * 0.12f + musical * 0.06f + density * 0.12f);
                break;

            case VJ_BEAT_TURBULENCE:
                v = ab_auto_mix3(snap->high * 0.28f,
                                  snap->transient * 0.18f + subdiv * 0.08f,
                                  groove * 0.18f + musical * 0.08f + density * 0.08f);
                break;

            case VJ_BEAT_KICK:
                v = ab_auto_mix3(snap->kick * 0.84f,
                                  snap->bass * 0.10f + snap->beat_pulse * 0.08f,
                                  snap->transient * 0.08f + drum * 0.06f);
                break;

            case VJ_BEAT_SNARE:
                v = ab_auto_mix3(snap->snare * 0.82f,
                                  snap->mid * 0.10f + snap->high * 0.10f,
                                  snap->flux * 0.10f + snap->transient * 0.08f);
                break;

            case VJ_BEAT_HAT:
                v = ab_auto_mix3(snap->hat * 0.84f,
                                  snap->high * 0.14f + snap->flux * 0.10f,
                                  subdiv * 0.06f);
                break;
#ifdef VJ_BEAT_SOURCE
            case VJ_BEAT_SOURCE:
                v = ab_auto_mix3(snap->envelope * 0.22f,
                                  groove * 0.24f,
                                  phrase * 0.14f + musical * 0.06f);
                break;
#endif

            default:
                break;
        }

        if(t->hint_flags & VJ_BEAT_F_IMPULSE)
        {
            v = ab_auto_impulse_hint_signal(snap, t->hint_class, trigger_hit);
        }
        else if(t->hint_flags & VJ_BEAT_F_PHRASE_ONLY)
        {
            float phrase_gate = phrase * 0.62f + groove * 0.20f + climax * 0.12f + density * 0.06f;
            float phrase_floor = phrase_gate * 0.34f;

            if(phrase_gate < 0.0f)
                phrase_gate = 0.0f;
            else if(phrase_gate > 1.0f)
                phrase_gate = 1.0f;

            v *= 0.28f + phrase_gate * 0.72f;

            if(v < phrase_floor)
                v = phrase_floor;
        }

#ifdef VJ_BEAT_F_CLIMAX_ONLY
        if(t->hint_flags & VJ_BEAT_F_CLIMAX_ONLY)
        {
            v *= climax;
        }
#endif

        if(restraint > 0.0f)
        {
            float damp = 1.0f;

            switch(t->role)
            {
                case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
                    damp = 1.0f - restraint * 0.48f;
                    break;

                case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
                case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
                case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
                case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
                    damp = 1.0f - restraint * 0.34f;
                    break;

                case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
                    damp = 1.0f - restraint * 0.18f;
                    break;

                default:
                    damp = 1.0f - restraint * 0.12f;
                    break;
            }

            if(t->hint_class == VJ_BEAT_HAT &&
               (t->role == VJ_AUDIO_BEAT_AUTO_ROLE_SPEED ||
                t->role == VJ_AUDIO_BEAT_AUTO_ROLE_MOTION ||
                t->role == VJ_AUDIO_BEAT_AUTO_ROLE_FLOW ||
                t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY))
            {
                damp *= 0.74f;
                v = v * 0.78f + groove * 0.12f + phrase * 0.06f;
            }

            if(damp < 0.32f)
                damp = 0.32f;

            v *= damp;
        }
    }
#endif

    if(t->role != VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME)
    {
        float activity;
        float keep;
        float gate;

        if(snap->hit_seq <= 0)
            v = 0.0f;

        activity = fs ? fs->activity : ab_auto_activity_gate(snap);
        keep = ab_auto_low_activity_keep(t->role, groove, phrase, climax, activity, snap->beat_age_ms);
        gate = activity + keep;

        if(gate > 1.0f)
            gate = 1.0f;

        v *= gate;
    }

    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;

    return v;
}

void vj_audio_beat_set_video_fps(vj_audio_beat_shared_t *s, double fps)
{
    int q16;

    (void)s;

    if(fps < 1.0)
        fps = 1.0;
    else if(fps > 240.0)
        fps = 240.0;

    q16 = (int)(fps * 65536.0 + 0.5);

    if(q16 < 1)
        q16 = (25 << 16);

    ab_store_i(&ab_auto_video_fps_q16, q16);
}

static double ab_auto_get_video_fps(void)
{
    int q16 = ab_load_i(&ab_auto_video_fps_q16);

    if(q16 <= 0)
        return 25.0;

    return (double)q16 * (1.0 / 65536.0);
}

static int ab_auto_compute_beat_time_value(const ab_auto_target_t *t, const vj_audio_beat_snapshot_t *snap)
{
    double fps;
    double frames_d;
    int value;

    if(!t || !t->valid || !snap || snap->bpm <= 1.0f)
        return t ? t->base_value : 0;

    fps = ab_auto_get_video_fps();
    frames_d = (60.0 / (double)snap->bpm) * fps;

    if(frames_d < 1.0)
        frames_d = 1.0;
    else if(frames_d > 100000.0)
        frames_d = 100000.0;

    value = (int)(frames_d + 0.5);

    if(value < t->min_value)
        value = t->min_value;
    else if(value > t->max_value)
        value = t->max_value;


    return value;
}

static float ab_auto_role_comfort_depth(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 0.34f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 0.26f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 0.32f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.30f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.28f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.22f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 0.42f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.24f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.36f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.24f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.18f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.09f;
        default:                                return 0.30f;
    }
}

static float ab_auto_role_peak_depth(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 0.86f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.96f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.94f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.86f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.84f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.90f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.82f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.56f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.38f;
        default:                                return 0.86f;
    }
}

static int ab_auto_role_should_hold(int role)
{
    return role == VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY ||
           role == VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE ||
           role == VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL ||
           role == VJ_AUDIO_BEAT_AUTO_ROLE_COLOR ||
           role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY;
}

static float ab_auto_role_floor(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 0.020f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 0.018f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 0.025f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 0.020f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.022f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.020f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.030f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.018f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.020f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 0.025f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.025f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.055f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.050f;
        default:                                return 0.024f;
    }
}

static float ab_auto_role_gain(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 1.30f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 1.16f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 1.08f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 1.14f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 1.12f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 1.18f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 1.16f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 1.15f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 1.08f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 1.02f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 1.04f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.86f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.72f;
        default:                                return 1.08f;
    }
}

static float ab_auto_role_attack(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 1.00f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.72f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 0.68f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 0.62f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.52f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.58f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 0.52f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.34f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.32f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.36f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.22f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.24f;
        default:                                return 0.50f;
    }
}

static float ab_auto_role_release(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 0.45f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.26f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 0.20f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 0.075f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 0.18f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.16f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.13f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 0.075f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.065f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.055f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.045f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.032f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.055f;
        default:                                return 0.12f;
    }
}

static float ab_auto_expand_signal(float v, int role)
{
    float floor;
    float gain;

    if(v < 0.0f)
        v = 0.0f;
    else if(v > 1.0f)
        v = 1.0f;

    floor = ab_auto_role_floor(role);

    if(v <= floor)
        return 0.0f;

    v = (v - floor) / (1.0f - floor);
    gain = ab_auto_role_gain(role);
    v *= gain;

    if(v > 1.0f)
        v = 1.0f;

    if(role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER)
        return v * (2.0f - v);

    if(role == VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD)
        return v * v;

    return v * v * (3.0f - 2.0f * v);
}

static float ab_auto_role_signal_deadband(int role)
{
    switch(role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER:   return 0.006f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:    return 0.010f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST:  return 0.018f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:    return 0.012f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:      return 0.014f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:   return 0.012f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:     return 0.016f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:   return 0.020f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:    return 0.022f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:    return 0.022f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:     return 0.024f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:  return 0.040f;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD: return 0.030f;
        default:                                return 0.016f;
    }
}

static float ab_auto_slew_signal(ab_auto_target_t *t, float raw, long now)
{
    float current;
    float alpha;
    float base_alpha;
    float diff;
    float dt;

    if(!t)
        return 0.0f;

    if(raw < 0.0f)
        raw = 0.0f;
    else if(raw > 1.0f)
        raw = 1.0f;

    raw = ab_auto_expand_signal(raw, t->role);
    t->raw_value = raw;

#ifdef VJ_BEAT_F_REJECT
    if(ab_auto_target_is_binary_impulse(t))
#else
    if(t->impulse || t->role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER)
#endif
    {
        t->mod_initialized = 1;
        t->mod_value = raw;
        t->last_slew_ms = now;
        return raw;
    }

    if(now <= 0)
        now = ab_now_ms();

    if(!t->mod_initialized)
    {
        t->mod_initialized = 1;
        t->mod_value = raw;
        t->last_slew_ms = now;
        return raw;
    }

    current = t->mod_value;
    diff = raw - current;

    if(diff < 0.0f)
        diff = -diff;

    if(diff < ab_auto_role_signal_deadband(t->role))
    {
        t->last_slew_ms = now;
        return current;
    }

    if(t->last_slew_ms <= 0)
        dt = 1.0f / 25.0f;
    else
        dt = (float)(now - t->last_slew_ms) * 0.001f;

    if(dt < 0.0f)
        dt = 0.0f;
    else if(dt > 0.25f)
        dt = 0.25f;

    t->last_slew_ms = now;

    if(t->has_hint && (t->attack_ms > 0 || t->release_ms > 0))
    {
        int tau_ms = raw > current ? t->attack_ms : t->release_ms;

        if(tau_ms <= 0)
            tau_ms = raw > current ? 600 : 1400;

        if(tau_ms < 20)
            tau_ms = 20;

        alpha = 1.0f - expf(-(dt * 1000.0f) / (float)tau_ms);
    }
    else
    {
        base_alpha = raw > current ? ab_auto_role_attack(t->role) : ab_auto_role_release(t->role);

        if(base_alpha < 0.001f)
            base_alpha = 0.001f;
        else if(base_alpha > 0.999f)
            base_alpha = 0.999f;

        alpha = 1.0f - powf(1.0f - base_alpha, dt * 25.0f);
    }

    if(alpha < 0.0f)
        alpha = 0.0f;
    else if(alpha > 1.0f)
        alpha = 1.0f;

    current += (raw - current) * alpha;

    if(raw > current && raw > 0.18f)
    {
        float lift;

        switch(t->role)
        {
            case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
                lift = raw * 0.34f;
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
            case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
                lift = raw * 0.42f;
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:
            case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
                lift = raw * 0.56f;
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
            case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
                lift = raw * 0.38f;
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
            case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
            case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
                lift = raw * 0.32f;
                break;

            default:
                lift = raw * 0.46f;
                break;
        }

        if(t->has_hint && t->attack_ms > 480)
        {
            float slow_factor = 480.0f / (float)t->attack_ms;

            if(slow_factor < 0.34f)
                slow_factor = 0.34f;
            else if(slow_factor > 1.0f)
                slow_factor = 1.0f;

            lift *= slow_factor;
        }

        if(current < lift)
            current = lift;
    }
    else if(raw <= 0.002f && current > 0.0f)
    {
        current *= 0.42f;
    }

    if(current < 0.035f)
        current = 0.0f;
    else if(current > 1.0f)
        current = 1.0f;

    t->mod_value = current;
    return current;
}

static int ab_auto_value_deadband(const ab_auto_target_t *t)
{
    int span;
    int dead;

    if(!t)
        return 1;

    span = t->max_value - t->min_value;

    if(span <= 0)
        return 1;

#ifdef VJ_BEAT_F_REJECT
    if(ab_auto_target_is_discrete(t))
    {
        dead = span / 32;

        if(dead < 1)
            dead = 1;
        else if(dead > 16)
            dead = 16;

        return dead;
    }

    if(t->has_hint)
    {
        switch(t->hint_class)
        {
            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                dead = span / 48;
                break;
            case VJ_BEAT_GEOMETRY_FREQUENCY:
            case VJ_BEAT_GEOMETRY_PHASE:
            case VJ_BEAT_SIGNED_CURVE:
            case VJ_BEAT_HAT:
                dead = span / 220;
                break;
            case VJ_BEAT_KICK:
            case VJ_BEAT_SNARE:
                dead = span / 160;
                break;
            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
            case VJ_BEAT_SOURCE_MIX:
                dead = span / 96;
                break;
            default:
                dead = span / 180;
                break;
        }

        if(dead < 1)
            dead = 1;
        else if(dead > 10)
            dead = 10;

        return dead;
    }
#endif

    switch(t->role)
    {
        case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
        case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
        case VJ_AUDIO_BEAT_AUTO_ROLE_COLOR:
            dead = span / 128;
            break;
        case VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY:
            dead = span / 256;
            break;
        case VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD:
            dead = span / 80;
            break;
        default:
            dead = span / 160;
            break;
    }

    if(dead < 1)
        dead = 1;
    else if(dead > 8)
        dead = 8;

    return dead;
}

static int ab_auto_absi(int v)
{
    return v < 0 ? -v : v;
}

static int ab_auto_quantize_discrete_value(const ab_auto_target_t *t, int value, int lo, int hi)
{
#ifdef VJ_BEAT_F_REJECT
    int span;
    int step;
    int delta;
    int sign;
    int abs_delta;
    int q_delta;

    if(!ab_auto_target_is_discrete(t))
        return value;

    span = hi - lo;

    if(span <= 0)
        return value;

    step = span / 32;

    if(step < 1)
        step = 1;
    else if(step > 16)
        step = 16;

    delta = value - t->base_value;

    if(delta == 0)
        return t->base_value;

    sign = delta < 0 ? -1 : 1;
    abs_delta = delta < 0 ? -delta : delta;
    q_delta = ((abs_delta + (step >> 1)) / step) * step;

    if(q_delta < step)
        q_delta = step;

    value = t->base_value + sign * q_delta;

    if(value < lo)
        value = lo;
    else if(value > hi)
        value = hi;

    if(value < t->min_value)
        value = t->min_value;
    else if(value > t->max_value)
        value = t->max_value;
#else
    (void)t;
    (void)lo;
    (void)hi;
#endif

    return value;
}

static int ab_auto_apply_global_amount_value(const ab_auto_target_t *t, int value, int global_amount, int lo, int hi)
{
    long delta;
    long scaled;

    if(!t || !t->valid)
        return value;

    if(global_amount <= 0)
        return t->base_value;

    if(global_amount >= 100 || value == t->base_value)
        return value;

    delta = (long)value - (long)t->base_value;
    scaled = delta * (long)global_amount;

    if(scaled >= 0)
        scaled = (scaled + 50L) / 100L;
    else
        scaled = (scaled - 50L) / 100L;

    if(scaled == 0)
        return t->base_value;

    value = t->base_value + (int)scaled;

    if(value < lo)
        value = lo;
    else if(value > hi)
        value = hi;

    if(value < t->min_value)
        value = t->min_value;
    else if(value > t->max_value)
        value = t->max_value;

    return ab_auto_quantize_discrete_value(t, value, lo, hi);
}

static float ab_auto_smoothstepf(float v)
{
    if(v <= 0.0f)
        return 0.0f;
    if(v >= 1.0f)
        return 1.0f;

    return v * v * (3.0f - 2.0f * v);
}

static float ab_auto_maxf(float a, float b)
{
    return a > b ? a : b;
}


static long ab_auto_pause_gap_threshold_ms(void)
{
    int fps_q16 = ab_load_i(&ab_auto_video_fps_q16);
    double fps = (double)fps_q16 / 65536.0;
    long frame_ms;
    long threshold;

    if(fps < 1.0)
        fps = 25.0;

    frame_ms = (long)(1000.0 / fps + 0.5);

    if(frame_ms < 1)
        frame_ms = 1;

    threshold = 900L;

    if((frame_ms * 4L + 250L) > threshold)
        threshold = frame_ms * 4L + 250L;

    if(threshold > 2500L)
        threshold = 2500L;

    return threshold;
}

static long ab_auto_resume_guard_ms(void)
{
    int fps_q16 = ab_load_i(&ab_auto_video_fps_q16);
    double fps = (double)fps_q16 / 65536.0;
    long frame_ms;
    long guard;

    if(fps < 1.0)
        fps = 25.0;

    frame_ms = (long)(1000.0 / fps + 0.5);

    if(frame_ms < 1)
        frame_ms = 1;

    guard = frame_ms * 3L;

    if(guard < 140L)
        guard = 140L;
    else if(guard > 420L)
        guard = 420L;

    return guard;
}

static int ab_auto_release_targets_to_base(
    void *ctx,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg
)
{
    int changed = 0;

    if(!ctx || !get_fx_id || !get_arg || !set_arg)
        return 0;

    for(int i = 0; i < ab_auto_target_count; i++)
    {
        ab_auto_target_t *t = &ab_auto_targets[i];
        int current;

        if(!t->valid)
            continue;

        if(get_fx_id(ctx, t->chain_pos) != t->effect_id)
            continue;

        current = get_arg(ctx, t->chain_pos, t->param_nr);

        if(current != t->base_value)
        {
            if(vje_is_param_value_valid(t->effect_id, t->param_nr, t->base_value))
            {
                if(set_arg(ctx, t->chain_pos, t->param_nr, t->base_value))
                    changed++;
            }
        }

        t->last_value = t->base_value;
        t->active = 0;
        t->mod_value = 0.0f;
        t->raw_value = 0.0f;
        t->climax_value = 0.0f;
        t->mod_initialized = 0;
        t->last_slew_ms = 0;
    }

    return changed;
}

static void ab_auto_clear_macro_state_for_resume(const vj_audio_beat_snapshot_t *snap, long now)
{
    ab_auto_climax_level = 0.0f;
    ab_auto_climax_last_ms = now;
    ab_auto_climax_last_hit_seq = snap ? snap->hit_seq : 0;
    ab_auto_groove_level = 0.0f;
    ab_auto_phrase_level = 0.0f;
    ab_auto_groove_last_ms = now;
    ab_auto_groove_last_hit_seq = snap ? snap->hit_seq : 0;
    ab_auto_resume_guard_until_ms = now + ab_auto_resume_guard_ms();
    ab_auto_resume_guard_active = 1;
}

static float ab_auto_update_groove(const vj_audio_beat_snapshot_t *snap)
{
    long now;
    float dt;
    float musical;
    float subdiv;
    float body;
    float target;
    float alpha;

    if(!snap)
        return 0.0f;

    now = ab_now_ms();

    if(ab_auto_groove_last_ms <= 0)
    {
        ab_auto_groove_last_ms = now;
        ab_auto_groove_last_hit_seq = snap->hit_seq;
    }

    dt = (float)(now - ab_auto_groove_last_ms) * 0.001f;

    if(dt < 0.0f)
        dt = 0.0f;
    else if(dt > 0.25f)
        dt = 0.25f;

    ab_auto_groove_last_ms = now;

    musical = ab_auto_musical_pulse(snap);
    subdiv = ab_auto_subdivision_pulse(snap);

    body = snap->envelope * 0.26f +
           snap->level * 0.12f +
           snap->bass * 0.20f +
           snap->mid * 0.18f +
           snap->high * 0.10f +
           snap->flux * 0.08f +
           snap->transient * 0.10f +
           musical * 0.20f +
           subdiv * 0.08f;

    target = (body - 0.075f) * (1.0f / 0.74f);

    if(target < 0.0f)
        target = 0.0f;
    else if(target > 1.0f)
        target = 1.0f;

    target = target * (2.0f - target);

    alpha = target > ab_auto_groove_level ? (1.95f * dt) : (0.42f * dt);

    if(alpha > 0.34f)
        alpha = 0.34f;

    ab_auto_groove_level += (target - ab_auto_groove_level) * alpha;

    if(snap->hit_seq != ab_auto_groove_last_hit_seq)
    {
        float impact = snap->bass * 0.38f + snap->transient * 0.30f + musical * 0.28f + snap->mid * 0.12f;
        float add = 0.018f + impact * 0.090f;

        if((snap->hit_seq & 3) == 1)
            add += 0.030f;

        ab_auto_phrase_level += add;
        ab_auto_groove_last_hit_seq = snap->hit_seq;
    }

    {
        float phrase_target = ab_auto_groove_level * 0.48f + snap->envelope * 0.16f;
        float p_alpha = phrase_target > ab_auto_phrase_level ? (0.28f * dt) : (0.075f * dt);

        if(p_alpha > 0.12f)
            p_alpha = 0.12f;

        ab_auto_phrase_level += (phrase_target - ab_auto_phrase_level) * p_alpha;
        ab_auto_phrase_level -= 0.020f * dt;
    }

    if(ab_auto_groove_level < 0.0f)
        ab_auto_groove_level = 0.0f;
    else if(ab_auto_groove_level > 1.0f)
        ab_auto_groove_level = 1.0f;

    if(ab_auto_phrase_level < 0.0f)
        ab_auto_phrase_level = 0.0f;
    else if(ab_auto_phrase_level > 1.0f)
        ab_auto_phrase_level = 1.0f;

    return ab_auto_smoothstepf(ab_auto_groove_level);
}

static float ab_auto_update_climax(const vj_audio_beat_snapshot_t *snap)
{
    long now;
    float dt;
    float bass;
    float mid;
    float high;
    float transient;
    float groove;
    float phrase;
    float body;
    float target;
    float alpha;
    float age_penalty = 0.0f;
    float beat_ms = 600.0f;

    if(!snap)
        return 0.0f;

    now = ab_now_ms();

    if(ab_auto_climax_last_ms <= 0)
    {
        ab_auto_climax_last_ms = now;
        ab_auto_climax_last_hit_seq = snap->hit_seq;
    }

    dt = (float)(now - ab_auto_climax_last_ms) * 0.001f;

    if(dt < 0.0f)
        dt = 0.0f;
    else if(dt > 0.25f)
        dt = 0.25f;

    ab_auto_climax_last_ms = now;

    bass = snap->bass;
    mid = snap->mid;
    high = snap->high;
    transient = snap->transient;
    groove = ab_auto_groove_level;
    phrase = ab_auto_phrase_level;

    if(bass > 1.0f) bass = 1.0f;
    if(mid > 1.0f) mid = 1.0f;
    if(high > 1.0f) high = 1.0f;
    if(transient > 1.0f) transient = 1.0f;

    body = bass * 0.26f + mid * 0.18f + high * 0.12f + snap->level * 0.10f + snap->envelope * 0.20f + snap->flux * 0.10f + transient * 0.14f + groove * 0.18f + phrase * 0.16f;

    target = (body - 0.30f) * (1.0f / 0.70f);

    if(target < 0.0f)
        target = 0.0f;
    else if(target > 1.0f)
        target = 1.0f;

    target *= 0.70f;

    if(snap->hit_seq != ab_auto_climax_last_hit_seq)
    {
        float impact = ab_auto_maxf(transient, ab_auto_maxf(bass, snap->beat_pulse));
        float add = 0.030f + 0.105f * impact + 0.035f * phrase;

        if(snap->beat_age_ms >= 0 && snap->beat_age_ms < 80)
            add *= 1.08f;

        ab_auto_climax_level += add;
        ab_auto_climax_last_hit_seq = snap->hit_seq;
    }

    if(snap->bpm > 1.0f)
    {
        beat_ms = 60000.0f / snap->bpm;

        if(beat_ms < 180.0f)
            beat_ms = 180.0f;
        else if(beat_ms > 1600.0f)
            beat_ms = 1600.0f;
    }

    if(snap->beat_age_ms > (long)(beat_ms * 1.35f))
        age_penalty = 0.20f;
    if(snap->beat_age_ms > (long)(beat_ms * 2.25f))
        age_penalty = 0.48f;

    alpha = target > ab_auto_climax_level ? (0.55f * dt) : (0.16f * dt);

    if(alpha > 0.22f)
        alpha = 0.22f;

    ab_auto_climax_level += (target - ab_auto_climax_level) * alpha;
    ab_auto_climax_level -= age_penalty * dt;

    if(ab_auto_climax_level < 0.0f)
        ab_auto_climax_level = 0.0f;
    else if(ab_auto_climax_level > 1.0f)
        ab_auto_climax_level = 1.0f;

    return ab_auto_smoothstepf(ab_auto_climax_level);
}


static int ab_auto_min_visible_delta(const ab_auto_target_t *t, int span, float signal, float macro_open)
{
    int n;

    if(!t || span <= 0 || signal < 0.105f)
        return 0;

    n = span / 42;

    if(n < 1)
        n = 1;

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        switch(t->hint_class)
        {
            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                n = signal > 0.12f ? 1 : 0;
                if(signal > 0.30f && span > 22)
                    n = 2;
                if(signal > 0.62f && span > 34)
                    n = 3;
                break;

            case VJ_BEAT_SPEED:
            case VJ_BEAT_SIGNED_SPEED:
            case VJ_BEAT_MOTION_REACT:
            case VJ_BEAT_GEOMETRY_AMPLITUDE:
            case VJ_BEAT_KICK:
            case VJ_BEAT_SNARE:
                n = 2 + (int)(signal * 5.0f) + (int)(macro_open * 2.0f);
                break;

            case VJ_BEAT_DETAIL:
            case VJ_BEAT_GLOW:
            case VJ_BEAT_ALPHA_OR_OPACITY:
            case VJ_BEAT_INTENSITY:
            case VJ_BEAT_TURBULENCE:
            case VJ_BEAT_CONTRAST:
                n = 3 + (int)(signal * 8.0f) + (int)(macro_open * 3.0f);
                break;

            case VJ_BEAT_HAT:
                n = 1 + (int)(signal * 4.0f) + (int)(macro_open * 1.0f);
                break;

            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
            case VJ_BEAT_SOURCE_MIX:
#ifdef VJ_BEAT_SOURCE
            case VJ_BEAT_SOURCE:
#endif
                n = 2 + (int)(signal * 5.0f) + (int)(macro_open * 2.0f);
                break;

            default:
                n = 1 + (int)(signal * 5.0f) + (int)(macro_open * 2.0f);
                break;
        }
    }
    else
#endif
    {
        switch(t->role)
        {
            case VJ_AUDIO_BEAT_AUTO_ROLE_SPEED:
            case VJ_AUDIO_BEAT_AUTO_ROLE_MOTION:
            case VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT:
            case VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE:
            case VJ_AUDIO_BEAT_AUTO_ROLE_FLOW:
                n = 2 + (int)(signal * 5.0f) + (int)(macro_open * 2.0f);
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY:
            case VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE:
                n = 1 + (int)(signal * 3.0f) + (int)(macro_open * 2.0f);
                break;

            case VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL:
                n = signal > 0.20f ? 1 : 0;
                break;

            default:
                n = 1 + (int)(signal * 4.0f);
                break;
        }
    }

    if(n < 0)
        n = 0;

    if(span <= 12 && n > 1)
        n = 1;
    else if(span <= 32 && n > 2)
        n = 2;
    else if(span <= 80 && n > 5)
        n = 5;
    else if(span <= 160 && n > 9)
        n = 9;
    else if(n > 14)
        n = 14;

    return n;
}

static int ab_auto_compute_value(const ab_auto_target_t *t, float signal, int global_amount, float climax)
{
    int lo;
    int hi;
    int span;
    int direction;
    int capacity;
    int positive_capacity;
    int negative_capacity;
    int min_capacity;
    float global;
    float depth;
    float amount_gate;
    float macro_open;
    float comfort_depth;
    float peak_depth;
    float drive;
    float delta;
    int value;

    if(!t || !t->valid)
        return 0;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    ab_auto_calc_debug_reset();
    ab_auto_calc_dbg.reason = "start";
#endif

    if(global_amount < 0)
        global_amount = 0;
    else if(global_amount > 100)
        global_amount = 100;

    global = (float)global_amount * 0.01f;

    if(signal < 0.0f)
        signal = 0.0f;
    else if(signal > 1.0f)
        signal = 1.0f;

    if(climax < 0.0f)
        climax = 0.0f;
    else if(climax > 1.0f)
        climax = 1.0f;

    macro_open = ab_auto_smoothstepf(climax);

    lo = t->min_value;
    hi = t->max_value;

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        if(t->soft_min != VJ_BEAT_SOFT_UNSET && t->soft_min > lo)
            lo = t->soft_min;
        if(t->soft_max != VJ_BEAT_SOFT_UNSET && t->soft_max < hi)
            hi = t->soft_max;

        {
            int lock_sign = 0;
#ifdef VJ_BEAT_F_NO_ZERO_CROSS
            if(t->hint_flags & VJ_BEAT_F_NO_ZERO_CROSS)
                lock_sign = 1;
#endif
#ifdef VJ_BEAT_F_SIGN_LOCK
            if((t->hint_flags & VJ_BEAT_F_SIGN_LOCK) && macro_open < 0.72f)
                lock_sign = 1;
#endif
            if(lock_sign)
            {
                if(t->base_value >= 0)
                {
                    if(lo < 0)
                        lo = 0;
                }
                else
                {
                    if(hi > 0)
                        hi = 0;
                }
            }
        }

#ifdef VJ_BEAT_F_CLIMAX_ONLY
        if((t->hint_flags & VJ_BEAT_F_CLIMAX_ONLY) && macro_open < 0.10f)
            return t->base_value;
#endif
    }
#endif

    if(hi < lo)
    {
        int tmp = lo;
        lo = hi;
        hi = tmp;
    }

    span = hi - lo;

    if(span <= 0)
    {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.reason = "empty-soft-range";
#endif
        return t->base_value;
    }

    if(t->base_value < lo)
        lo = t->base_value;
    else if(t->base_value > hi)
        hi = t->base_value;

    span = hi - lo;

    if(span <= 0)
    {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.reason = "empty-expanded-range";
#endif
        return t->base_value;
    }

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    ab_auto_calc_dbg.lo = lo;
    ab_auto_calc_dbg.hi = hi;
    ab_auto_calc_dbg.span = span;
#endif

#ifdef VJ_BEAT_F_REJECT
    if(ab_auto_target_is_continuous_impulse(t))
    {
        int impulse_direction = t->invert ? -1 : 1;
        int impulse_capacity;
        float normal_hint = 1.0f;
        float climax_hint = 1.0f;
        float impulse_depth;
        float impulse_amount;
        float impulse_delta;
        int impulse_value;

        if(signal <= 0.002f)
        {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            ab_auto_calc_dbg.drive = signal;
            ab_auto_calc_dbg.depth = 0.0f;
            ab_auto_calc_dbg.capacity = 0;
            ab_auto_calc_dbg.direction = impulse_direction;
            ab_auto_calc_dbg.delta = 0.0f;
            ab_auto_calc_dbg.reason = "impulse-idle";
#endif
            return t->base_value;
        }

        if(t->normal_depth_pct > 0 || t->climax_depth_pct > 0)
        {
            normal_hint = (float)t->normal_depth_pct * 0.01f;
            climax_hint = (float)t->climax_depth_pct * 0.01f;

            if(normal_hint < 0.0f)
                normal_hint = 0.0f;
            else if(normal_hint > 1.0f)
                normal_hint = 1.0f;

            if(climax_hint < normal_hint)
                climax_hint = normal_hint;
            else if(climax_hint > 1.0f)
                climax_hint = 1.0f;
        }

        impulse_depth = normal_hint + (climax_hint - normal_hint) * macro_open;
        impulse_amount = 0.62f + global * 0.38f;
        impulse_depth *= impulse_amount;

        if(impulse_depth < 0.0f)
            impulse_depth = 0.0f;
        else if(impulse_depth > 1.0f)
            impulse_depth = 1.0f;

        impulse_capacity = impulse_direction > 0
            ? hi - t->base_value
            : t->base_value - lo;

        if(impulse_capacity <= 0)
        {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            ab_auto_calc_dbg.drive = signal;
            ab_auto_calc_dbg.depth = impulse_depth;
            ab_auto_calc_dbg.capacity = impulse_capacity;
            ab_auto_calc_dbg.direction = impulse_direction;
            ab_auto_calc_dbg.delta = 0.0f;
            ab_auto_calc_dbg.reason = "impulse-no-capacity";
#endif
            return t->base_value;
        }

        impulse_delta = (float)impulse_capacity * impulse_depth * signal;

        if(impulse_direction < 0)
            impulse_delta = -impulse_delta;

        impulse_value = t->base_value +
            (int)(impulse_delta >= 0.0f ? impulse_delta + 0.5f : impulse_delta - 0.5f);

        if(impulse_value < lo)
            impulse_value = lo;
        else if(impulse_value > hi)
            impulse_value = hi;

        if(impulse_value < t->min_value)
            impulse_value = t->min_value;
        else if(impulse_value > t->max_value)
            impulse_value = t->max_value;

        impulse_value = ab_auto_quantize_discrete_value(t, impulse_value, lo, hi);

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.drive = signal;
        ab_auto_calc_dbg.depth = impulse_depth;
        ab_auto_calc_dbg.capacity = impulse_capacity;
        ab_auto_calc_dbg.direction = impulse_direction;
        ab_auto_calc_dbg.delta = impulse_delta;
        ab_auto_calc_dbg.reason = "impulse-continuous";
#endif
        return ab_auto_apply_global_amount_value(t, impulse_value, global_amount, lo, hi);
    }
#endif

#ifdef VJ_BEAT_F_REJECT
    if(ab_auto_target_is_binary_impulse(t))
#else
    if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER)
#endif
    {
        float trigger_threshold = 0.55f;
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.drive = signal;
        ab_auto_calc_dbg.depth = 1.0f;
        ab_auto_calc_dbg.capacity = span;
        ab_auto_calc_dbg.direction = t->invert ? -1 : 1;
        ab_auto_calc_dbg.delta = signal >= trigger_threshold ? (float)span : 0.0f;
        ab_auto_calc_dbg.reason = signal >= trigger_threshold ? "trigger-hit" : "trigger-idle";
#endif
        if(signal >= trigger_threshold)
            return ab_auto_apply_global_amount_value(t, t->invert ? lo : hi, global_amount, lo, hi);

        return t->base_value;
    }

    if(t->role != VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME)
    {
        float lane_bias = (float)((t->param_nr % 5) - 2) * 0.004f;
        signal = signal + lane_bias;

        if(signal < 0.0f)
            signal = 0.0f;
        else if(signal > 1.0f)
            signal = 1.0f;
    }

    amount_gate = (float)t->amount_pct * 0.01f;

    if(amount_gate < 0.0f)
        amount_gate = 0.0f;
    else if(amount_gate > 1.0f)
        amount_gate = 1.0f;

    amount_gate = amount_gate + (1.0f - amount_gate) * macro_open;

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint && (t->normal_depth_pct > 0 || t->climax_depth_pct > 0))
    {
        float normal_hint = (float)t->normal_depth_pct * 0.01f;
        float climax_hint = (float)t->climax_depth_pct * 0.01f;

        if(normal_hint < 0.0f)
            normal_hint = 0.0f;
        else if(normal_hint > 1.0f)
            normal_hint = 1.0f;

        if(climax_hint < normal_hint)
            climax_hint = normal_hint;
        else if(climax_hint > 1.0f)
            climax_hint = 1.0f;

        {
            float hint_amount = 0.58f + amount_gate * 0.42f;
            float normal_scale = 0.54f + global * 0.72f;
            float peak_scale = 0.72f + global * 0.42f;

            if(normal_scale > 1.18f)
                normal_scale = 1.18f;
            if(peak_scale > 1.18f)
                peak_scale = 1.18f;

            comfort_depth = normal_hint * normal_scale * hint_amount;
            peak_depth = climax_hint * peak_scale * hint_amount;
        }
    }
    else
#endif
    {
        comfort_depth = ab_auto_role_comfort_depth(t->role) * (0.18f + global * 0.58f) * amount_gate;
        peak_depth = ab_auto_role_peak_depth(t->role) * amount_gate;
    }

    if(peak_depth < comfort_depth)
        peak_depth = comfort_depth;

    depth = comfort_depth + (peak_depth - comfort_depth) * macro_open;

    if(depth > 1.0f)
        depth = 1.0f;
    else if(depth < 0.0f)
        depth = 0.0f;

    drive = ab_auto_smoothstepf(signal);

    if(t->role != VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD)
    {
        float linear_mix = t->has_hint ? 0.58f : 0.48f;
        float linear_drive = signal;

        drive = drive * (1.0f - linear_mix) + linear_drive * linear_mix;
    }

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        float floor_drive = signal;

        switch(t->hint_class)
        {
            case VJ_BEAT_DETAIL:
            case VJ_BEAT_GLOW:
            case VJ_BEAT_ALPHA_OR_OPACITY:
            case VJ_BEAT_INTENSITY:
            case VJ_BEAT_TURBULENCE:
            case VJ_BEAT_CONTRAST:
            case VJ_BEAT_SPEED:
            case VJ_BEAT_SIGNED_SPEED:
            case VJ_BEAT_MOTION_REACT:
            case VJ_BEAT_GEOMETRY_AMPLITUDE:
            case VJ_BEAT_KICK:
            case VJ_BEAT_SNARE:
                floor_drive *= 0.72f;
                break;

            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                floor_drive *= 0.55f;
                break;

            case VJ_BEAT_HAT:
                floor_drive *= 0.60f;
                break;

            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
            case VJ_BEAT_SOURCE_MIX:
                floor_drive *= 0.58f;
                break;

            default:
                floor_drive *= 0.50f;
                break;
        }

        if(drive < floor_drive)
            drive = floor_drive;
    }
#endif

    if(drive < 0.0f)
        drive = 0.0f;
    else if(drive > 1.0f)
        drive = 1.0f;

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        if(t->hint_flags & VJ_BEAT_F_SQUARED)
        {
            drive *= drive;
        }

#ifdef VJ_BEAT_F_LOG
        if(t->hint_flags & VJ_BEAT_F_LOG)
        {
            drive *= 0.78f;
            depth *= 0.76f;
        }
#endif

        if(t->hint_flags & VJ_BEAT_F_REBUILDS_STATE)
        {
            float rebuild_depth = 0.08f + global * 0.06f + macro_open * 0.16f;
            float rebuild_drive = 0.38f + macro_open * 0.22f;

            if(depth > rebuild_depth)
                depth = rebuild_depth;
            if(drive > rebuild_drive)
                drive = rebuild_drive;
        }

        if(t->hint_flags & VJ_BEAT_F_PHRASE_ONLY)
        {
            float phrase_depth = 0.16f + global * 0.08f + macro_open * 0.18f;

            if(depth > phrase_depth)
                depth = phrase_depth;
        }

        switch(t->hint_class)
        {
            case VJ_BEAT_GEOMETRY_FREQUENCY:
            case VJ_BEAT_SIGNED_CURVE:
                if(depth > 0.16f + macro_open * 0.30f)
                    depth = 0.16f + macro_open * 0.30f;
                if(drive > 0.52f + macro_open * 0.26f)
                    drive = 0.52f + macro_open * 0.26f;
                break;

            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                if(depth > 0.24f + macro_open * 0.34f)
                    depth = 0.24f + macro_open * 0.34f;
                if(drive > 0.72f + macro_open * 0.20f)
                    drive = 0.72f + macro_open * 0.20f;
                break;

            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
            case VJ_BEAT_SOURCE_MIX:
#ifdef VJ_BEAT_SOURCE
            case VJ_BEAT_SOURCE:
#endif
                if(depth > 0.24f + macro_open * 0.34f)
                    depth = 0.24f + macro_open * 0.34f;
                break;

            case VJ_BEAT_CONTRAST:
                if(depth > 0.22f + macro_open * 0.34f)
                    depth = 0.22f + macro_open * 0.34f;
                break;

            case VJ_BEAT_KICK:
                if(depth > 0.36f + macro_open * 0.44f)
                    depth = 0.36f + macro_open * 0.44f;
                break;

            case VJ_BEAT_SNARE:
                if(depth > 0.30f + macro_open * 0.38f)
                    depth = 0.30f + macro_open * 0.38f;
                break;

            case VJ_BEAT_HAT:
                if(depth > 0.22f + macro_open * 0.28f)
                    depth = 0.22f + macro_open * 0.28f;
                if(drive > 0.74f + macro_open * 0.16f)
                    drive = 0.74f + macro_open * 0.16f;
                break;

            case VJ_BEAT_GEOMETRY_PHASE:
            case VJ_BEAT_SIGNED_SPEED:
                if(depth > 0.34f + macro_open * 0.38f)
                    depth = 0.34f + macro_open * 0.38f;
                break;

            default:
                break;
        }
    }
#endif

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint && signal > 0.075f)
    {
        float min_depth = 0.0f;

        switch(t->hint_class)
        {
            case VJ_BEAT_DETAIL:
            case VJ_BEAT_GLOW:
            case VJ_BEAT_ALPHA_OR_OPACITY:
            case VJ_BEAT_INTENSITY:
            case VJ_BEAT_TURBULENCE:
            case VJ_BEAT_CONTRAST:
                min_depth = 0.16f + global * 0.16f + macro_open * 0.18f;
                break;

            case VJ_BEAT_SPEED:
            case VJ_BEAT_SIGNED_SPEED:
            case VJ_BEAT_MOTION_REACT:
            case VJ_BEAT_GEOMETRY_AMPLITUDE:
            case VJ_BEAT_KICK:
            case VJ_BEAT_SNARE:
                min_depth = 0.15f + global * 0.14f + macro_open * 0.18f;
                break;

            case VJ_BEAT_GRID_SIZE:
            case VJ_BEAT_WINDOW_RADIUS:
                min_depth = 0.10f + global * 0.08f + macro_open * 0.12f;
                break;

            case VJ_BEAT_HAT:
                min_depth = 0.09f + global * 0.08f + macro_open * 0.10f;
                break;

            case VJ_BEAT_MEMORY:
            case VJ_BEAT_INERTIA:
            case VJ_BEAT_SOURCE_MIX:
#ifdef VJ_BEAT_SOURCE
            case VJ_BEAT_SOURCE:
#endif
                min_depth = 0.11f + global * 0.10f + macro_open * 0.14f;
                break;

            default:
                min_depth = 0.10f + global * 0.10f + macro_open * 0.14f;
                break;
        }

        if(depth < min_depth)
            depth = min_depth;
    }
#endif

    if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY)
    {
        float geo_ceiling = 0.24f + global * 0.09f + macro_open * 0.24f;
        float geo_depth_ceiling = 0.14f + global * 0.07f + macro_open * 0.30f;

        if(geo_ceiling > 0.64f)
            geo_ceiling = 0.64f;
        if(drive > geo_ceiling)
            drive = geo_ceiling;

        if(depth > geo_depth_ceiling)
            depth = geo_depth_ceiling;
    }

    if(macro_open < 0.35f)
    {
        float ceiling = 0.72f + global * 0.16f;

        if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD)
            ceiling = 0.38f;
        else if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST)
            ceiling = 0.58f;
        else if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_SPEED)
            ceiling = 0.62f;
        else if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY ||
                t->role == VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE)
            ceiling = 0.54f;
        else if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY)
            ceiling = 0.32f + global * 0.08f;

        if(drive > ceiling)
            drive = ceiling;
    }

    direction = t->invert ? -1 : 1;

    if(!t->invert &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER &&
       t->role != VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME)
    {
        if(((t->chain_pos + t->param_nr + t->effect_id) & 1) != 0)
            direction = -1;
    }

#ifdef VJ_BEAT_F_REJECT
    if(t->has_hint)
    {
        if(t->hint_class == VJ_BEAT_GEOMETRY_PHASE ||
           t->hint_class == VJ_BEAT_COLOR_PHASE ||
           t->hint_class == VJ_BEAT_FLOW ||
           t->hint_class == VJ_BEAT_DRIFT ||
           t->hint_class == VJ_BEAT_WARP)
        {
            if(((t->chain_pos * 3 + t->param_nr + t->effect_id) & 1) != 0)
                direction = -direction;
        }

#ifdef VJ_BEAT_F_SIGN_LOCK
        if((t->hint_flags & VJ_BEAT_F_SIGN_LOCK) && t->base_value < 0)
            direction = -1;
#endif
    }
#endif

#ifdef VJ_BEAT_F_REJECT
    if(ab_auto_target_is_wrap(t))
    {
        int cycle = span + 1;
        int origin = vje_is_param_value_valid(t->effect_id, t->param_nr, t->last_value)
            ? t->last_value
            : t->base_value;
        int phase = origin - lo;
        int step = (int)((float)span * depth * drive + 0.5f);
        int min_visible = ab_auto_min_visible_delta(t, span, signal, macro_open);

        if(cycle <= 1)
            return t->base_value;

        if(phase < 0)
            phase = 0;
        else if(phase >= cycle)
            phase = cycle - 1;

        if(step < min_visible)
            step = min_visible;

        if(step <= 0)
        {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            ab_auto_calc_dbg.direction = direction;
            ab_auto_calc_dbg.capacity = span;
            ab_auto_calc_dbg.depth = depth;
            ab_auto_calc_dbg.drive = drive;
            ab_auto_calc_dbg.delta = 0.0f;
            ab_auto_calc_dbg.reason = "wrap-idle";
#endif
            return origin;
        }

        if(direction < 0)
            step = -step;

        phase += step;
        phase %= cycle;

        if(phase < 0)
            phase += cycle;

        value = lo + phase;

        if(value < t->min_value)
            value = t->min_value;
        else if(value > t->max_value)
            value = t->max_value;

        value = ab_auto_quantize_discrete_value(t, value, lo, hi);

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.direction = direction;
        ab_auto_calc_dbg.capacity = span;
        ab_auto_calc_dbg.depth = depth;
        ab_auto_calc_dbg.drive = drive;
        ab_auto_calc_dbg.delta = (float)step;
        ab_auto_calc_dbg.reason = "wrap";
#endif
        return ab_auto_apply_global_amount_value(t, value, global_amount, lo, hi);
    }
#endif

    positive_capacity = hi - t->base_value;
    negative_capacity = t->base_value - lo;
    min_capacity = span / 4;

    if(min_capacity < 1)
        min_capacity = 1;

    if(direction > 0)
    {
        if(positive_capacity <= 0 ||
           (positive_capacity < min_capacity && negative_capacity > positive_capacity) ||
           (negative_capacity > positive_capacity * 2 && positive_capacity < (span * 34) / 100))
            direction = -1;
    }
    else
    {
        if(negative_capacity <= 0 ||
           (negative_capacity < min_capacity && positive_capacity > negative_capacity) ||
           (positive_capacity > negative_capacity * 2 && negative_capacity < (span * 34) / 100))
            direction = 1;
    }

    capacity = direction > 0 ? positive_capacity : negative_capacity;

    if(capacity <= 0)
    {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.direction = direction;
        ab_auto_calc_dbg.capacity = capacity;
        ab_auto_calc_dbg.depth = depth;
        ab_auto_calc_dbg.drive = drive;
        ab_auto_calc_dbg.reason = "no-capacity";
#endif
        return t->base_value;
    }

    delta = (float)capacity * depth * drive;

    if(direction < 0)
        delta = -delta;

    value = t->base_value + (int)(delta >= 0.0f ? delta + 0.5f : delta - 0.5f);

    if(value < lo)
        value = lo;
    else if(value > hi)
        value = hi;

    if(value < t->min_value)
        value = t->min_value;
    else if(value > t->max_value)
        value = t->max_value;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    ab_auto_calc_dbg.direction = direction;
    ab_auto_calc_dbg.capacity = capacity;
    ab_auto_calc_dbg.depth = depth;
    ab_auto_calc_dbg.drive = drive;
    ab_auto_calc_dbg.delta = delta;
    ab_auto_calc_dbg.reason = "continuous";
#endif

    {
        int min_visible = ab_auto_min_visible_delta(t, span, signal, macro_open);
        int actual_delta = value - t->base_value;
        int actual_abs = actual_delta < 0 ? -actual_delta : actual_delta;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.min_delta = min_visible;
#endif

        if(min_visible > 0 && actual_abs < min_visible && capacity >= min_visible)
        {
            value = t->base_value + (direction > 0 ? min_visible : -min_visible);

            if(value < lo)
                value = lo;
            else if(value > hi)
                value = hi;

            if(value < t->min_value)
                value = t->min_value;
            else if(value > t->max_value)
                value = t->max_value;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            ab_auto_calc_dbg.reason = "visible-floor";
#endif
        }
    }

    if(value == t->base_value && signal > 0.18f && capacity > 0)
    {
        int nudge = 1;

#ifdef VJ_BEAT_F_REJECT
        if(t->has_hint)
        {
            switch(t->hint_class)
            {
                case VJ_BEAT_GEOMETRY_AMPLITUDE:
                case VJ_BEAT_SPEED:
                case VJ_BEAT_SIGNED_SPEED:
                    nudge = signal > 0.52f ? 3 : 2;
                    break;

                case VJ_BEAT_GRID_SIZE:
                case VJ_BEAT_WINDOW_RADIUS:
                    nudge = signal > 0.44f ? 2 : 1;
                    break;

                case VJ_BEAT_MEMORY:
                case VJ_BEAT_INERTIA:
                    nudge = signal > 0.58f ? 2 : 1;
                    break;

                default:
                    nudge = signal > 0.62f ? 2 : 1;
                    break;
            }
        }
#endif

        value = t->base_value + (direction > 0 ? nudge : -nudge);

        if(value < lo)
            value = lo;
        else if(value > hi)
            value = hi;

        if(value < t->min_value)
            value = t->min_value;
        else if(value > t->max_value)
            value = t->max_value;

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        ab_auto_calc_dbg.min_delta = nudge;
        ab_auto_calc_dbg.reason = "fallback-nudge";
#endif
    }

    value = ab_auto_quantize_discrete_value(t, value, lo, hi);

    return ab_auto_apply_global_amount_value(t, value, global_amount, lo, hi);
}

static void ab_auto_clear_runtime_state(int mark_dirty)
{
    ab_auto_signature = 0;
    ab_auto_target_count = 0;
    ab_auto_active = 0;

    ab_auto_climax_level = 0.0f;
    ab_auto_climax_last_ms = 0;
    ab_auto_climax_last_hit_seq = 0;

    ab_auto_groove_level = 0.0f;
    ab_auto_phrase_level = 0.0f;
    ab_auto_groove_last_ms = 0;
    ab_auto_groove_last_hit_seq = 0;

    ab_auto_last_apply_ms = 0;
    ab_auto_signature_last_check_ms = 0;
    ab_auto_signature_last_chain_len = -1;
    ab_auto_resume_guard_until_ms = 0;
    ab_auto_resume_guard_active = 0;
    ab_auto_debug_last_apply_ms = 0;

    memset(ab_auto_targets, 0, sizeof(ab_auto_targets));

    if(mark_dirty)
        ab_store_i(&ab_auto_dirty, 1);
}

static int ab_auto_release_and_clear(
    void *ctx,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg
)
{
    int released = 0;

    if(ab_auto_target_count > 0)
        released = ab_auto_release_targets_to_base(ctx, get_fx_id, get_arg, set_arg);

    ab_auto_clear_runtime_state(1);
    return released;
}

void vj_audio_beat_set_auto_mode(vj_audio_beat_shared_t *s, int mode)
{
    (void)s;

    if(mode < VJ_AUDIO_BEAT_AUTO_OFF)
        mode = VJ_AUDIO_BEAT_AUTO_OFF;
    else if(mode > VJ_AUDIO_BEAT_AUTO_CHAOS)
        mode = VJ_AUDIO_BEAT_AUTO_CHAOS;

    ab_store_i(&ab_auto_mode, mode);
    ab_store_i(&ab_auto_dirty, 1);
}

void vj_audio_beat_set_auto_amount(vj_audio_beat_shared_t *s, int amount)
{
    (void)s;

    if(amount < 0)
        amount = 0;
    else if(amount > 100)
        amount = 100;

    ab_store_i(&ab_auto_amount, amount);
}

void vj_audio_beat_auto_reset(vj_audio_beat_shared_t *s)
{
    (void)s;
    ab_auto_clear_runtime_state(1);
}

int vj_audio_beat_auto_apply_chain_ex(
    vj_audio_beat_shared_t *s,
    void *ctx,
    int chain_len,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg,
    vj_audio_beat_get_fx_entry_func get_entry
)
{
    vj_audio_beat_snapshot_t snap;
    int action;
    int mode;
    int amount;
    int dirty;
    int paused;
    int need_dirty_store = 0;
    float climax;
    int sig;
    long now;
    ab_auto_frame_signal_t frame_sig;
    int changed = 0;
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    long auto_dbg_now = 0;
    int auto_dbg_emit = 0;
#endif

    if(!s || !ctx || !get_fx_id || !get_arg || !set_arg || chain_len <= 0)
        return 0;

    now = ab_now_ms();

    if(!ab_load_i(&s->initialized))
        return 0;

    if(!ab_load_i(&s->enabled))
        return ab_auto_release_and_clear(ctx, get_fx_id, get_arg, set_arg);

    action = ab_load_i(&s->action_mode);
    paused = ab_load_i(&s->paused_by_beat);

    if(!ab_action_uses_auto_fx(action))
        return ab_auto_release_and_clear(ctx, get_fx_id, get_arg, set_arg);

    mode = ab_load_i(&ab_auto_mode);

    if(mode <= VJ_AUDIO_BEAT_AUTO_OFF)
        return ab_auto_release_and_clear(ctx, get_fx_id, get_arg, set_arg);

    if(!vj_audio_beat_get_snapshot(s, &snap))
        return 0;

    {
        const int transport_modulation = ab_action_is_breakbeat(action);

        if(paused && !transport_modulation)
        {
            int released = ab_auto_release_targets_to_base(ctx, get_fx_id, get_arg, set_arg);

            ab_auto_clear_macro_state_for_resume(&snap, now);
            return released;
        }

        {
            long gap = ab_auto_last_apply_ms > 0 ? now - ab_auto_last_apply_ms : 0;
            long gap_threshold = ab_auto_pause_gap_threshold_ms();

            ab_auto_last_apply_ms = now;

            if(!transport_modulation && gap > gap_threshold)
            {
                int released = ab_auto_release_targets_to_base(ctx, get_fx_id, get_arg, set_arg);

                ab_auto_clear_macro_state_for_resume(&snap, now);
                return released;
            }

            if(!transport_modulation && ab_auto_resume_guard_active && now < ab_auto_resume_guard_until_ms)
                return 0;

            if(ab_auto_resume_guard_active && now >= ab_auto_resume_guard_until_ms)
            {
                ab_auto_resume_guard_active = 0;
                ab_auto_climax_last_hit_seq = snap.hit_seq;
            }
        }
    }

    dirty = ab_load_i(&ab_auto_dirty);
    sig = ab_auto_signature;

    if(dirty ||
       sig == 0 ||
       ab_auto_signature_last_chain_len != chain_len ||
       ab_auto_signature_last_check_ms == 0 ||
       (now - ab_auto_signature_last_check_ms) >= VEEJAY_AUDIO_BEAT_AUTO_SIG_CHECK_MS)
    {
        sig = ab_auto_chain_signature(ctx, chain_len, get_fx_id);
        ab_auto_signature_last_check_ms = now;
        ab_auto_signature_last_chain_len = chain_len;
    }

    if(sig != ab_auto_signature || dirty)
    {
        int signature_changed = sig != ab_auto_signature;

        if(signature_changed && ab_auto_signature != 0)
            ab_auto_trim_macro_state_on_map_change();

        ab_auto_signature = sig;
        ab_auto_rebuild_map(ctx, chain_len, get_fx_id, get_arg, get_entry);
    }

    amount = ab_load_i(&ab_auto_amount);
    ab_auto_update_groove(&snap);
    climax = ab_auto_update_climax(&snap);
    ab_auto_build_frame_signal(&snap, climax, &frame_sig);

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
    auto_dbg_now = now;
    if(ab_auto_debug_last_apply_ms == 0 ||
       (auto_dbg_now - ab_auto_debug_last_apply_ms) >= VEEJAY_AUDIO_BEAT_AUTO_DEBUG_INTERVAL_MS)
    {
        auto_dbg_emit = 1;
        ab_auto_debug_last_apply_ms = auto_dbg_now;
        AB_AUTO_DBG("snapshot mapper=v32 hit_seq=%d hits=%ld age=%ldms bpm=%.2f kind=%d level=%.3f env=%.3f trans=%.3f flux=%.3f bass=%.3f mid=%.3f high=%.3f kick=%.3f snare=%.3f hat=%.3f body=%.3f density=%.3f fast=%.3f slow=%.3f rest=%.3f activity=%.3f groove=%.3f phrase=%.3f climax=%.3f amount=%d targets=%d",
                    snap.hit_seq,
                    snap.hits,
                    snap.beat_age_ms,
                    snap.bpm,
                    snap.hit_kind,
                    snap.level,
                    snap.envelope,
                    snap.transient,
                    snap.flux,
                    snap.bass,
                    snap.mid,
                    snap.high,
                    snap.kick,
                    snap.snare,
                    snap.hat,
                    frame_sig.body,
                    frame_sig.density,
                    frame_sig.tempo_fast,
                    frame_sig.tempo_slow,
                    frame_sig.restraint,
                    frame_sig.activity,
                    ab_auto_groove_level,
                    ab_auto_phrase_level,
                    climax,
                    amount,
                    ab_auto_target_count);
    }
#endif

    for(int i = 0; i < ab_auto_target_count; i++)
    {
        ab_auto_target_t *t = &ab_auto_targets[i];
        float signal;
        int current;
        int value;

        if(!t->valid)
            continue;

        if(get_fx_id(ctx, t->chain_pos) != t->effect_id)
        {
            need_dirty_store = 1;
            continue;
        }

        if(ab_auto_target_curve_owned_now(t))
        {
            t->last_value = t->base_value;
            t->active = 0;
            t->mod_value = 0.0f;
            t->raw_value = 0.0f;
            t->climax_value = 0.0f;
            t->mod_initialized = 0;
            t->last_slew_ms = 0;
            t->last_change_ms = 0;
            need_dirty_store = 1;
            continue;
        }

        current = get_arg(ctx, t->chain_pos, t->param_nr);

        if(!t->impulse &&
           t->role != VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER &&
           current != t->last_value &&
           current != t->base_value &&
           vje_is_param_value_valid(t->effect_id, t->param_nr, current))
        {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            if(auto_dbg_emit)
                AB_AUTO_DBG("target-rebase chain=%d fx=%d param=%d old_base=%d old_last=%d current=%d role=%s hint=%s",
                            t->chain_pos,
                            t->effect_id,
                            t->param_nr,
                            t->base_value,
                            t->last_value,
                            current,
                            ab_auto_role_name(t->role),
                            t->has_hint ? ab_auto_hint_class_name(t->hint_class) : "fallback");
#endif
            t->base_value = current;
            t->last_value = current;
            t->active = 0;
            t->mod_value = 0.0f;
            t->raw_value = 0.0f;
            t->climax_value = 0.0f;
            t->mod_initialized = 0;
            t->last_slew_ms = 0;
            t->last_change_ms = 0;
        }

        if(t->role == VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME)
        {
            value = ab_auto_compute_beat_time_value(t, &snap);

            if(value != current)
            {
                if(vje_is_param_value_valid(t->effect_id, t->param_nr, value))
                {
                    if(set_arg(ctx, t->chain_pos, t->param_nr, value))
                    {
                        changed++;
                        current = value;
                        t->last_change_ms = now;
                    }
                }
            }

            t->last_value = current;
            t->active = current != t->base_value ? 1 : 0;
            continue;
        }

        signal = ab_auto_signal_for_target(&snap, t, climax, &frame_sig);
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
        {
            float role_signal = signal;
            signal = ab_auto_slew_signal(t, signal, now);
            value = ab_auto_compute_value(t, signal, amount, climax);

            if(auto_dbg_emit && (t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY || t->has_hint))
            {
                float pos = 0.0f;
                int span = t->max_value - t->min_value;

                if(span > 0)
                    pos = (float)(value - t->min_value) / (float)span;

                float activity = frame_sig.activity;
                float keep = ab_auto_low_activity_keep(t->role, frame_sig.groove, frame_sig.phrase, climax, activity, snap.beat_age_ms);
                float gate_dbg = activity + keep;

                gate_dbg = gate_dbg > 1.0f ? 1.0f : gate_dbg;

                AB_AUTO_DBG("target=%d chain=%d fx=%d param=%d role=%s hint=%s flags=0x%x base=%d current=%d value=%d pos=%.3f range=%d..%d soft=%d..%d role_signal=%.3f activity=%.3f keep=%.3f gate=%.3f slewed=%.3f expanded=%.3f climax=%.3f amount_pct=%d invert=%d hold=%dms calc_drive=%.3f depth=%.3f delta=%.2f cap=%d dir=%d floor=%d why=%s edge=%s",
                            i,
                            t->chain_pos,
                            t->effect_id,
                            t->param_nr,
                            ab_auto_role_name(t->role),
                            t->has_hint ? ab_auto_hint_class_name(t->hint_class) : "fallback",
                            t->hint_flags,
                            t->base_value,
                            current,
                            value,
                            pos,
                            t->min_value,
                            t->max_value,
                            t->soft_min,
                            t->soft_max,
                            role_signal,
                            activity,
                            keep,
                            gate_dbg,
                            signal,
                            t->raw_value,
                            climax,
                            t->amount_pct,
                            t->invert,
                            t->hold_ms,
                            ab_auto_calc_dbg.drive,
                            ab_auto_calc_dbg.depth,
                            ab_auto_calc_dbg.delta,
                            ab_auto_calc_dbg.capacity,
                            ab_auto_calc_dbg.direction,
                            ab_auto_calc_dbg.min_delta,
                            ab_auto_calc_dbg.reason ? ab_auto_calc_dbg.reason : "?",
                            value >= t->max_value ? "MAX" : (value <= t->min_value ? "MIN" : "ok"));
            }
        }
#else
        signal = ab_auto_slew_signal(t, signal, now);
        value = ab_auto_compute_value(t, signal, amount, climax);
#endif

        if(signal <= 0.002f && !ab_auto_role_should_hold(t->role))
            value = t->base_value;

        if(value != current)
        {
            int deadband = ab_auto_value_deadband(t);
            int hold_ok = 1;
            int diff = ab_auto_absi(value - current);
            int deadband_ok;
            int valid_value;
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            int written = 0;
            const char *skip_reason = "none";
#endif

            if(t->hold_ms > 0 &&
               t->last_change_ms > 0 &&
               (now - t->last_change_ms) < (long)t->hold_ms)
            {
                int cur_dist = ab_auto_absi(current - t->base_value);
                int new_dist = ab_auto_absi(value - t->base_value);
                int release_toward_base = new_dist < cur_dist;
                int fresh_hit_override = snap.beat_age_ms >= 0 && snap.beat_age_ms <= 120L && frame_sig.activity >= 0.45f;

                hold_ok = 0;

                if(release_toward_base && (frame_sig.activity < 0.18f || snap.beat_age_ms > 260L))
                    hold_ok = 1;
                else if(fresh_hit_override && diff >= deadband)
                    hold_ok = 1;
            }

            deadband_ok = value == t->base_value || current == t->base_value || diff >= deadband;
            valid_value = vje_is_param_value_valid(t->effect_id, t->param_nr, value);

            if(hold_ok && deadband_ok && valid_value)
            {
                if(set_arg(ctx, t->chain_pos, t->param_nr, value))
                {
                    changed++;
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                    if(auto_dbg_emit && (t->has_hint || t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY))
                        AB_AUTO_DBG("target-write chain=%d fx=%d param=%d old=%d new=%d base=%d calc=%s",
                                    t->chain_pos,
                                    t->effect_id,
                                    t->param_nr,
                                    current,
                                    value,
                                    t->base_value,
                                    ab_auto_calc_dbg.reason ? ab_auto_calc_dbg.reason : "?");
#endif
                    current = value;
                    t->last_change_ms = now;
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                    written = 1;
#endif
                }
                else
                {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                    skip_reason = "set-arg-failed";
#endif
                }
            }
            else if(!hold_ok)
            {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                skip_reason = "hold";
#endif
            }
            else if(!deadband_ok)
            {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                skip_reason = "deadband";
#endif
            }
            else if(!valid_value)
            {
#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
                skip_reason = "invalid-value";
#endif
            }

#ifdef VEEJAY_AUDIO_BEAT_AUTO_DEBUG
            if(auto_dbg_emit && !written && (t->has_hint || t->role == VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY))
            {
                AB_AUTO_DBG("target-skip chain=%d fx=%d param=%d base=%d current=%d wanted=%d diff=%d deadband=%d hold_ok=%d valid=%d reason=%s calc=%s",
                            t->chain_pos,
                            t->effect_id,
                            t->param_nr,
                            t->base_value,
                            current,
                            value,
                            diff,
                            deadband,
                            hold_ok,
                            valid_value,
                            skip_reason,
                            ab_auto_calc_dbg.reason ? ab_auto_calc_dbg.reason : "?");
            }
#endif
        }

        t->last_value = current;
        t->active = signal > 0.002f ? 1 : 0;
    }

    if(need_dirty_store)
        ab_store_i(&ab_auto_dirty, 1);

    ab_auto_active = changed > 0 ? 1 : 0;

    return changed;
}

int vj_audio_beat_auto_apply_chain(
    vj_audio_beat_shared_t *s,
    void *ctx,
    int chain_len,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg
)
{
    return vj_audio_beat_auto_apply_chain_ex(s,
                                             ctx,
                                             chain_len,
                                             get_fx_id,
                                             get_arg,
                                             set_arg,
                                             NULL);
}

int vj_audio_beat_auto_modulate_args(
    vj_audio_beat_shared_t *s,
    sample_eff_chain *entry,
    int effect_id,
    int *args,
    int n_params,
    long long n_frame
)
{
    vj_audio_beat_snapshot_t snap;
    ab_auto_frame_signal_t frame_sig;
    int action;
    int mode;
    int amount;
    int changed = 0;
    long now;

    if(!s || !entry || !args || effect_id <= 0 || n_params <= 0)
        return 0;

    if(!ab_load_i(&s->initialized) || !ab_load_i(&s->enabled))
        return 0;

    action = ab_load_i(&s->action_mode);
    if(!ab_action_uses_auto_fx(action))
        return 0;

    mode = ab_load_i(&ab_auto_mode);
    if(mode <= VJ_AUDIO_BEAT_AUTO_OFF)
        return 0;

    if(ab_auto_target_count <= 0)
        return 0;

    if(!vj_audio_beat_get_snapshot(s, &snap))
        return 0;

    amount = ab_load_i(&ab_auto_amount);
    now = ab_now_ms();

    ab_auto_build_frame_signal(&snap, ab_auto_climax_level, &frame_sig);

    if(n_params > SAMPLE_MAX_PARAMETERS)
        n_params = SAMPLE_MAX_PARAMETERS;

    for(int i = 0; i < ab_auto_target_count; i++)
    {
        ab_auto_target_t *t = &ab_auto_targets[i];
        float signal;
        int current_base;
        int curve_value;
        int value;
        int old_base;

        if(!t->valid)
            continue;

        if(t->entry_ptr != entry)
            continue;

        if(t->effect_id != effect_id)
            continue;

        if(t->param_nr < 0 || t->param_nr >= n_params)
            continue;

        if(ab_auto_entry_param_curve_enabled(entry, t->param_nr))
        {
            t->last_value = args[t->param_nr];
            t->active = 0;
            t->mod_value = 0.0f;
            t->raw_value = 0.0f;
            t->climax_value = 0.0f;
            t->mod_initialized = 0;
            t->last_slew_ms = 0;
            t->last_change_ms = 0;
            continue;
        }

        if(!t->curve_owned || !t->curve_mixable)
            continue;

        if(!ab_auto_entry_param_curve_value(entry, t->param_nr, n_frame, &curve_value))
            continue;

        current_base = curve_value;

        if(current_base < t->min_value)
            current_base = t->min_value;
        else if(current_base > t->max_value)
            current_base = t->max_value;

        signal = ab_auto_signal_for_target(&snap, t, ab_auto_climax_level, &frame_sig);
        signal = ab_auto_slew_signal(t, signal, now);

        old_base = t->base_value;
        t->base_value = current_base;
        value = ab_auto_compute_value(t, signal, amount, ab_auto_climax_level);
        t->base_value = old_base;

        if(signal <= 0.002f && !ab_auto_role_should_hold(t->role))
            value = current_base;

        if(value < t->min_value)
            value = t->min_value;
        else if(value > t->max_value)
            value = t->max_value;

        if(value != args[t->param_nr] &&
           vje_is_param_value_valid(effect_id, t->param_nr, value))
        {
            args[t->param_nr] = value;
            changed++;
        }

        t->last_value = args[t->param_nr];
        t->active = signal > 0.002f ? 1 : 0;
        t->last_change_ms = now;
    }

    if(changed > 0)
        ab_auto_active = 1;

    return changed;
}

int vj_audio_beat_get_action(vj_audio_beat_shared_t *s)
{
    if(!s)
        return VJ_AUDIO_BEAT_ACTION_NONE;

    return ab_load_i(&s->action_mode);
}

#endif
