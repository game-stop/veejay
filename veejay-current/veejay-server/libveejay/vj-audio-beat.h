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
#ifndef VJ_AUDIO_BEAT_H
#define VJ_AUDIO_BEAT_H

#include <config.h>
#include <stdint.h>

#ifdef HAVE_JACK

#ifndef VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS
#define VJ_AUDIO_BEAT_MONITOR_LATENCY_DEFAULT_MS -1
#endif

typedef struct vj_audio_beat_shared_t vj_audio_beat_shared_t;
typedef struct vj_audio_sync_shared_t vj_audio_sync_shared_t;
typedef struct veejay_t veejay_t;
typedef struct sample_eff_t sample_eff_chain;

typedef enum
{
    VJ_AUDIO_BEAT_ACTION_NONE = 0,
    VJ_AUDIO_BEAT_ACTION_AUTO_FX = 2,
    VJ_AUDIO_BEAT_ACTION_BREAK_BEAT_AUTO_FX = 3,
    VJ_AUDIO_BEAT_ACTION_BREAK_BEAT = 4
} vj_audio_beat_action_t;

typedef enum
{
    VJ_AUDIO_CTRL_LEVEL = 0,
    VJ_AUDIO_CTRL_ENVELOPE = 1,
    VJ_AUDIO_CTRL_TRANSIENT = 2,
    VJ_AUDIO_CTRL_FLUX = 3,
    VJ_AUDIO_CTRL_BEAT_PULSE = 4,
    VJ_AUDIO_CTRL_BEAT_GATE = 5,
    VJ_AUDIO_CTRL_BEAT_TOGGLE = 6,
    VJ_AUDIO_CTRL_BPM = 7,
    VJ_AUDIO_CTRL_BASS = 8,
    VJ_AUDIO_CTRL_MID = 9,
    VJ_AUDIO_CTRL_HIGH = 10,
    VJ_AUDIO_CTRL_BAND_BALANCE = 11,
    VJ_AUDIO_CTRL_TRAIL_LENGTH = 12,
    VJ_AUDIO_CTRL_DENSITY = 13,
    VJ_AUDIO_CTRL_KICK = 14,
    VJ_AUDIO_CTRL_SNARE = 15,
    VJ_AUDIO_CTRL_HAT = 16,
    VJ_AUDIO_CTRL_SCRATCH_ENVELOPE = 17,
    VJ_AUDIO_CTRL_SCRATCH_DIRECTION = 18,
    VJ_AUDIO_CTRL_SCRATCH_SIGNED = 19
} vj_audio_beat_signal_t;

#define VJ_AUDIO_BEAT_AUTO_MAX_TARGETS 24
#define VJ_AUDIO_BEAT_AUTO_AMOUNT_UI_MAX 100
#define VJ_AUDIO_BEAT_AUTO_AMOUNT_MAX 300

typedef enum
{
    VJ_AUDIO_BEAT_AUTO_OFF = 0,
    VJ_AUDIO_BEAT_AUTO_PRIMARY = 1,
    VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION = 2,
    VJ_AUDIO_BEAT_AUTO_PRIMARY_MOTION_MEMORY = 3,
    VJ_AUDIO_BEAT_AUTO_CHAOS = 4
} vj_audio_beat_auto_mode_t;

typedef enum
{
    VJ_AUDIO_BEAT_AUTO_ROLE_NONE = 0,
    VJ_AUDIO_BEAT_AUTO_ROLE_TRIGGER = 1,
    VJ_AUDIO_BEAT_AUTO_ROLE_AMOUNT = 2,
    VJ_AUDIO_BEAT_AUTO_ROLE_BEAT_TIME = 3,
    VJ_AUDIO_BEAT_AUTO_ROLE_SPEED = 4,
    VJ_AUDIO_BEAT_AUTO_ROLE_MOTION = 5,
    VJ_AUDIO_BEAT_AUTO_ROLE_FLOW = 6,
    VJ_AUDIO_BEAT_AUTO_ROLE_MEMORY = 7,
    VJ_AUDIO_BEAT_AUTO_ROLE_THRESHOLD = 8,
    VJ_AUDIO_BEAT_AUTO_ROLE_SPATIAL = 9,
    VJ_AUDIO_BEAT_AUTO_ROLE_COLOR = 10,
    VJ_AUDIO_BEAT_AUTO_ROLE_SOURCE = 11,
    VJ_AUDIO_BEAT_AUTO_ROLE_TEXTURE = 12,
    VJ_AUDIO_BEAT_AUTO_ROLE_GEOMETRY = 13,
    VJ_AUDIO_BEAT_AUTO_ROLE_STRUCTURAL = 14,
    VJ_AUDIO_BEAT_AUTO_ROLE_CONTRAST = 15
} vj_audio_beat_auto_role_t;

typedef struct
{
    int enabled;
    int open;
    int channels;
    int sample_rate;
    int hit_seq;

    long hits;
    long last_hit_ms;
    long beat_age_ms;

    float level;
    float envelope;
    float transient;
    float flux;
    float beat_pulse;
    float beat_gate;
    float beat_toggle;
    float bpm;

    float bass;
    float mid;
    float high;
    float band_balance;
    float beat_trail_length;
    float beat_density;

    float kick;
    float snare;
    float hat;
    int hit_kind;

} vj_audio_beat_snapshot_t;

typedef int (*vj_audio_beat_get_fx_id_func)(void *ctx, int chain_pos);
typedef int (*vj_audio_beat_get_fx_arg_func)(void *ctx, int chain_pos, int param_nr);
typedef int (*vj_audio_beat_set_fx_arg_func)(void *ctx, int chain_pos, int param_nr, int value);
typedef sample_eff_chain *(*vj_audio_beat_get_fx_entry_func)(void *ctx, int chain_pos);

void vj_audio_beat_init(vj_audio_beat_shared_t *s, int input_channels);
void vj_audio_beat_bind_sync(vj_audio_beat_shared_t *s, vj_audio_sync_shared_t *sync);
void vj_audio_beat_request_stop(vj_audio_beat_shared_t *s);
void *vj_audio_beat_thread(void *arg);

int vj_audio_beat_enable(vj_audio_beat_shared_t *s);
int vj_audio_beat_disable(vj_audio_beat_shared_t *s);
int vj_audio_beat_toggle(vj_audio_beat_shared_t *s);
int vj_audio_beat_resume_if_due(veejay_t *v, vj_audio_beat_shared_t *s);
int vj_audio_beat_consume(veejay_t *v, vj_audio_beat_shared_t *s);

int vj_audio_beat_is_enabled(vj_audio_beat_shared_t *s);
int vj_audio_beat_is_running(vj_audio_beat_shared_t *s);
int vj_audio_beat_is_open(vj_audio_beat_shared_t *s);
int vj_audio_beat_is_paused_by_beat(vj_audio_beat_shared_t *s);
int vj_audio_beat_transport_is_internal(vj_audio_beat_shared_t *s);
void vj_audio_beat_user_transport_override(veejay_t *v, vj_audio_beat_shared_t *s,
                                           int requested_speed);

void vj_audio_beat_set_freeze_ms(vj_audio_beat_shared_t *s, int ms);
void vj_audio_beat_set_cooldown_ms(vj_audio_beat_shared_t *s, int ms);
void vj_audio_beat_set_threshold(vj_audio_beat_shared_t *s, int threshold);
void vj_audio_beat_set_input_channels(vj_audio_beat_shared_t *s, int channels);
void vj_audio_beat_set_scratch_sensitivity(vj_audio_beat_shared_t *s, int sensitivity);
void vj_audio_beat_set_source_loss_pause(vj_audio_beat_shared_t *s, int enabled);
void vj_audio_beat_set_output_latency_ms(vj_audio_beat_shared_t *s, int ms);
int vj_audio_beat_get_output_latency_ms(vj_audio_beat_shared_t *s);
void vj_audio_beat_set_heard_latency_ms(vj_audio_beat_shared_t *s, int ms);
int vj_audio_beat_get_heard_latency_ms(vj_audio_beat_shared_t *s);
void vj_audio_beat_set_monitor_latency_ms(vj_audio_beat_shared_t *s, int ms);
int vj_audio_beat_get_monitor_latency_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_effective_latency_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_freeze_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_cooldown_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_threshold(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_input_channels(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_pulse_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_gate_ms(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_auto_mode(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_auto_amount(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_scratch_sensitivity(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_source_loss_pause(vj_audio_beat_shared_t *s);

void vj_audio_beat_set_action(vj_audio_beat_shared_t *s, int action);
void vj_audio_beat_set_pulse_ms(vj_audio_beat_shared_t *s, int ms);
void vj_audio_beat_set_gate_ms(vj_audio_beat_shared_t *s, int ms);

void vj_audio_beat_set_auto_mode(vj_audio_beat_shared_t *s, int mode);
void vj_audio_beat_set_auto_amount(vj_audio_beat_shared_t *s, int amount);
void vj_audio_beat_set_video_fps(vj_audio_beat_shared_t *s, double fps);
void vj_audio_beat_auto_reset(vj_audio_beat_shared_t *s);
void vj_audio_beat_auto_mark_dirty(vj_audio_beat_shared_t *s);
int vj_audio_beat_auto_build_table(void);
int vj_audio_beat_auto_apply_chain(
    vj_audio_beat_shared_t *s,
    void *ctx,
    int chain_len,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg
);
int vj_audio_beat_auto_apply_chain_ex(
    vj_audio_beat_shared_t *s,
    void *ctx,
    int chain_len,
    vj_audio_beat_get_fx_id_func get_fx_id,
    vj_audio_beat_get_fx_arg_func get_arg,
    vj_audio_beat_set_fx_arg_func set_arg,
    vj_audio_beat_get_fx_entry_func get_entry
);
int vj_audio_beat_auto_modulate_args(
    vj_audio_beat_shared_t *s,
    sample_eff_chain *entry,
    int effect_id,
    int *args,
    int n_params,
    long long n_frame
);

int vj_audio_beat_copy_record_audio(
    vj_audio_beat_shared_t *s,
    uint8_t *dst,
    int dst_frames,
    int dst_frame_bytes,
    int dst_channels,
    int dst_sample_rate
);

float vj_audio_beat_get_level(vj_audio_beat_shared_t *s);
float vj_audio_beat_get_transient(vj_audio_beat_shared_t *s);
long vj_audio_beat_get_hits(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_action(vj_audio_beat_shared_t *s);
int vj_audio_beat_get_snapshot(vj_audio_beat_shared_t *s, vj_audio_beat_snapshot_t *dst);
float vj_audio_beat_get_signal(vj_audio_beat_shared_t *s, int signal);
int vj_audio_beat_map_signal(vj_audio_beat_shared_t *s, int signal, int min_value, int max_value, int invert);

int vj_audio_beat_disable_for_transport(veejay_t *v, vj_audio_beat_shared_t *s);
int vj_audio_beat_release_transport(veejay_t *v, vj_audio_beat_shared_t *s);
void vj_audio_beat_set_action_for_transport(veejay_t *v,
                                            vj_audio_beat_shared_t *s,
                                            int action);

#endif

#endif
