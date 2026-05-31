/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "mirror.h"

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MIRROR_PARAMS       10

#define P_CENTER_X          0
#define P_CENTER_Y          1
#define P_ANGLE             2
#define P_SPIN_SPEED        3
#define P_REFLECT_MIX       4
#define P_AXIS_WIDTH        5
#define P_AXIS_GLOW         6
#define P_BEAT_SPIN         7
#define P_BEAT_PUSH         8
#define P_BEAT_SMOOTH       9

typedef struct {
    uint8_t *buf[3];
    int w;
    int h;
    int n_threads;

    float spin_phase;
    float beat_env;
    float beat_prev;
} mirror_t;

static inline int mirror_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t mirror_y(int v)
{
    return (uint8_t)((v < pixel_Y_lo_) ? pixel_Y_lo_ : ((v > pixel_Y_hi_) ? pixel_Y_hi_ : v));
}

static inline uint8_t mirror_uv(int v)
{
    return (uint8_t)((v < pixel_U_lo_) ? pixel_U_lo_ : ((v > pixel_U_hi_) ? pixel_U_hi_ : v));
}

static inline int mirror_abs_i(int v)
{
    return v < 0 ? -v : v;
}

static inline int mirror_reflect_coord(int v, int max)
{
    if(v < 0)
        v = -v;

    if(v >= max) {
        const int q = v / max;
        v -= q * max;
        if(q & 1)
            v = (max - 1) - v;
    }

    return v;
}

static inline uint8_t mirror_mix_y_u8(uint8_t a, uint8_t b, int q8)
{
    return mirror_y((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t mirror_mix_uv_u8(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return mirror_uv(v);
}

static inline int mirror_beat_shape_1000(int beat_push)
{
    beat_push = mirror_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return mirror_clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

vj_effect *mirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = MIRROR_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_CENTER_X] = 0;
    ve->limits[1][P_CENTER_X] = w;
    ve->defaults[P_CENTER_X] = w / 2;

    ve->limits[0][P_CENTER_Y] = 0;
    ve->limits[1][P_CENTER_Y] = h;
    ve->defaults[P_CENTER_Y] = h / 2;

    ve->limits[0][P_ANGLE] = 0;
    ve->limits[1][P_ANGLE] = 360;
    ve->defaults[P_ANGLE] = 0;

    ve->limits[0][P_SPIN_SPEED] = -100;
    ve->limits[1][P_SPIN_SPEED] = 100;
    ve->defaults[P_SPIN_SPEED] = 0;

    ve->limits[0][P_REFLECT_MIX] = 0;
    ve->limits[1][P_REFLECT_MIX] = 1000;
    ve->defaults[P_REFLECT_MIX] = 1000;

    ve->limits[0][P_AXIS_WIDTH] = 0;
    ve->limits[1][P_AXIS_WIDTH] = 256;
    ve->defaults[P_AXIS_WIDTH] = 18;

    ve->limits[0][P_AXIS_GLOW] = 0;
    ve->limits[1][P_AXIS_GLOW] = 1000;
    ve->defaults[P_AXIS_GLOW] = 0;

    ve->limits[0][P_BEAT_SPIN] = 0;
    ve->limits[1][P_BEAT_SPIN] = 1000;
    ve->defaults[P_BEAT_SPIN] = 420;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Axis Mirror Folding";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Center X",
        "Center Y",
        "Angle",
        "Spin Speed",
        "Reflection Mix",
        "Axis Width",
        "Axis Glow",
        "Beat Spin",
        "Beat Push",
        "Beat Smooth"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DRIFT,          VJ_BEAT_F_PHRASE_ONLY,                                      w / 4,              (w * 3) / 4,        6,  22, 1800, 4200, 900,  22,    /* Center X */
        VJ_BEAT_DRIFT,          VJ_BEAT_F_PHRASE_ONLY,                                      h / 4,              (h * 3) / 4,        6,  22, 1800, 4200, 900,  22,    /* Center Y */
        VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Angle */
        VJ_BEAT_SIGNED_SPEED,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, -72, 72,   10, 42, 900,  2400, 0,    65,    /* Spin Speed */
        VJ_BEAT_SOURCE_MIX,     VJ_BEAT_F_CONTINUOUS,                                       620,                1000,               8,  30, 1000, 2600, 0,    46,    /* Reflection Mix */
        VJ_BEAT_WINDOW_RADIUS,  VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                 4,                  96,                 5,  18, 1800, 4200, 900,  22,    /* Axis Width */
        VJ_BEAT_GLOW,           VJ_BEAT_F_CONTINUOUS,                                       0,                  720,                10, 42, 900,  2600, 0,    52,    /* Axis Glow */
        VJ_BEAT_SPEED,          VJ_BEAT_F_REJECT,                                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Spin */
        VJ_BEAT_KICK,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                   0,                  860,                22, 88, 60,   360,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,         VJ_BEAT_F_PHRASE_ONLY,                                      280,                860,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    return ve;
}

void *mirror_malloc(int w, int h)
{
    mirror_t *m = (mirror_t*) vj_calloc(sizeof(mirror_t));
    if(!m)
        return NULL;

    const int len = w * h;

    m->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!m->buf[0]) {
        free(m);
        return NULL;
    }

    m->buf[1] = m->buf[0] + len;
    m->buf[2] = m->buf[1] + len;

    m->w = w;
    m->h = h;
    m->spin_phase = 0.0f;
    m->beat_env = 0.0f;
    m->beat_prev = 0.0f;

    m->n_threads = vje_advise_num_threads(len);
    if(m->n_threads < 1)
        m->n_threads = 1;

    return (void*) m;
}

void mirror_free(void *ptr)
{
    mirror_t *m = (mirror_t*) ptr;

    if(m) {
        if(m->buf[0])
            free(m->buf[0]);

        free(m);
    }
}

void mirror_apply(void *ptr, VJFrame *frame, int *args)
{
    mirror_t *m = (mirror_t*) ptr;

    if(!m || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 1 || height <= 1 || len <= 0)
        return;

    if(m->w != width || m->h != height) {
        m->w = width;
        m->h = height;
        m->n_threads = vje_advise_num_threads(width * height);
        if(m->n_threads < 1)
            m->n_threads = 1;
    }

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = m->buf[0];
    uint8_t *restrict srcU = m->buf[1];
    uint8_t *restrict srcV = m->buf[2];

    veejay_memcpy(srcY, dstY, len);
    veejay_memcpy(srcU, dstU, len);
    veejay_memcpy(srcV, dstV, len);

    const int center_x_i = mirror_clampi(args[P_CENTER_X], 0, width);
    const int center_y_i = mirror_clampi(args[P_CENTER_Y], 0, height);
    const int angle_i = mirror_clampi(args[P_ANGLE], 0, 360);
    const int spin_i = mirror_clampi(args[P_SPIN_SPEED], -100, 100);
    const int mix_i = mirror_clampi(args[P_REFLECT_MIX], 0, 1000);
    const int axis_width_i = mirror_clampi(args[P_AXIS_WIDTH], 0, 256);
    const int axis_glow_i = mirror_clampi(args[P_AXIS_GLOW], 0, 1000);
    const int beat_spin_i = mirror_clampi(args[P_BEAT_SPIN], 0, 1000);
    const int beat_push_i = mirror_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth_i = mirror_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int beat_shaped = mirror_beat_shape_1000(beat_push_i);
    const float beat_target = (float)beat_shaped * 0.001f;
    const float smooth = (float)beat_smooth_i * 0.001f;

    const float attack = 0.18f + (1.0f - smooth) * 0.34f;
    const float release = 0.030f + (1.0f - smooth) * 0.100f;

    if(beat_target > m->beat_env)
        m->beat_env += (beat_target - m->beat_env) * attack;
    else
        m->beat_env += (beat_target - m->beat_env) * release;

    if(m->beat_env < 0.0001f)
        m->beat_env = 0.0f;
    else if(m->beat_env > 1.0f)
        m->beat_env = 1.0f;

    const float kick = (beat_target > m->beat_prev) ? (beat_target - m->beat_prev) : 0.0f;
    m->beat_prev += (beat_target - m->beat_prev) * 0.35f;

    const float beat_env2 = m->beat_env * m->beat_env;

    const float spin_step =
        ((float)spin_i * 0.00275f) +
        (beat_env2 * ((float)beat_spin_i * 0.000080f)) +
        (kick * ((float)beat_spin_i * 0.000045f));

    m->spin_phase += spin_step;

    if(m->spin_phase >= 360.0f || m->spin_phase <= -360.0f)
        m->spin_phase = fmodf(m->spin_phase, 360.0f);

    const float cx = (float)center_x_i;
    const float cy = (float)center_y_i;
    const float angle_deg = (float)angle_i + m->spin_phase;

    const float rad = angle_deg * ((float)M_PI / 180.0f);
    const float nx = cosf(rad);
    const float ny = sinf(rad);

    int mix_q8 = (mix_i * 256 + 500) / 1000;
    if(beat_push_i > 0) {
        const int beat_q = (int)(m->beat_env * 1000.0f + 0.5f);
        const int lift = (beat_q * (256 - mix_q8) + 2000) / 4000;
        mix_q8 = mirror_clampi(mix_q8 + lift, 0, 256);
    }

    const float axis_width =
        (float)axis_width_i +
        (float)(axis_width_i + 8) * (0.55f * beat_env2);

    const int glow_amount =
        mirror_clampi(axis_glow_i + (int)(beat_env2 * 520.0f + kick * 240.0f + 0.5f), 0, 1000);

    const int glow_enabled = glow_amount > 0 && axis_width > 0.1f;
    const float inv_axis_width = glow_enabled ? (1.0f / axis_width) : 0.0f;
    const int glow_max = (glow_amount * 70 + 500) / 1000;

    const float rx_step = 1.0f - 2.0f * nx * nx;
    const float ry_step = -2.0f * nx * ny;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (int y = 0; y < height; y++) {
        const int row = y * width;
        const float dy = (float)y - cy;

        float dot = (0.0f - cx) * nx + dy * ny;
        float rx = 0.0f - 2.0f * dot * nx;
        float ry = (float)y - 2.0f * dot * ny;

        for (int x = 0; x < width; x++) {
            const int idx = row + x;
            const float abs_dot = (dot < 0.0f) ? -dot : dot;

            if(dot > 0.0f) {
                int mx = (int)(rx + 0.5f);
                int my = (int)(ry + 0.5f);

                mx = mirror_reflect_coord(mx, width);
                my = mirror_reflect_coord(my, height);

                const int s_idx = my * width + mx;

                if(mix_q8 >= 256) {
                    dstY[idx] = srcY[s_idx];
                    dstU[idx] = srcU[s_idx];
                    dstV[idx] = srcV[s_idx];
                }
                else if(mix_q8 > 0) {
                    dstY[idx] = mirror_mix_y_u8(srcY[idx], srcY[s_idx], mix_q8);
                    dstU[idx] = mirror_mix_uv_u8(srcU[idx], srcU[s_idx], mix_q8);
                    dstV[idx] = mirror_mix_uv_u8(srcV[idx], srcV[s_idx], mix_q8);
                }
            }

            if(glow_enabled && abs_dot < axis_width) {
                const float falloff = 1.0f - abs_dot * inv_axis_width;
                const int glow = (int)(falloff * falloff * (float)glow_max + 0.5f);

                dstY[idx] = mirror_y((int)dstY[idx] + glow);
            }

            dot += nx;
            rx += rx_step;
            ry += ry_step;
        }
    }
}
