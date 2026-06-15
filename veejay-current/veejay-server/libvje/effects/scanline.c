/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include <veejaycore/vjmem.h>
#include "scanline.h"

#define DEFAULT_STOP_DURATION 25

#define SCANLINE_PARAMS 12

#define P_DIRECTION    0
#define P_SPEED        1
#define P_STOP         2
#define P_BEAM_WIDTH   3
#define P_TRAIL_HOLD   4
#define P_SCAN_MIX     5
#define P_BEAT_SPEED   6
#define P_BEAT_BEAM    7
#define P_BEAT_GLOW    8
#define P_BEAT_DECAY   9
#define P_BEAT_PUSH   10
#define P_BEAT_SMOOTH 11

typedef struct {
    uint8_t *buf[3];
    int prevRow;
    int prevCol;
    int stopCount;
    float beat_env;
    float beat_kick;
    float sm_speed;
    float sm_stop;
    float sm_beam;
    float sm_hold;
    float sm_mix;
    float sm_beat_speed;
    float sm_beat_beam;
    float sm_beat_glow;
    float sm_beat_decay;
    int sm_ready;
    int n_threads;
} scanline_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t scanline_clamp_y(int v)
{
    return (uint8_t)(v < pixel_Y_lo_ ? pixel_Y_lo_ : (v > pixel_Y_hi_ ? pixel_Y_hi_ : v));
}

static inline uint8_t scanline_clamp_uv(int v)
{
    return (uint8_t)(v < pixel_U_lo_ ? pixel_U_lo_ : (v > pixel_U_hi_ ? pixel_U_hi_ : v));
}

static inline uint8_t scanline_blend_y(uint8_t a, uint8_t b, int q8)
{
    return scanline_clamp_y((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t scanline_blend_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return scanline_clamp_uv(v);
}

static inline int scanline_beat_shape(int beat_push)
{
    const int sq = (beat_push * beat_push + 500) / 1000;

    return clampi((beat_push * 45 + sq * 55 + 50) / 100, 0, 1000);
}

static inline float scanline_smooth_to(float cur, float target, float a)
{
    return cur + (target - cur) * a;
}

vj_effect *scanline_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SCANLINE_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_DIRECTION] = 0;   ve->limits[1][P_DIRECTION] = 3;    ve->defaults[P_DIRECTION] = 0;
    ve->limits[0][P_SPEED] = 1;       ve->limits[1][P_SPEED] = 500;      ve->defaults[P_SPEED] = 1;
    ve->limits[0][P_STOP] = 0;        ve->limits[1][P_STOP] = 500;       ve->defaults[P_STOP] = DEFAULT_STOP_DURATION;
    ve->limits[0][P_BEAM_WIDTH] = 1;  ve->limits[1][P_BEAM_WIDTH] = 96;  ve->defaults[P_BEAM_WIDTH] = 3;
    ve->limits[0][P_TRAIL_HOLD] = 0;  ve->limits[1][P_TRAIL_HOLD] = 255; ve->defaults[P_TRAIL_HOLD] = 255;
    ve->limits[0][P_SCAN_MIX] = 0;    ve->limits[1][P_SCAN_MIX] = 1000;  ve->defaults[P_SCAN_MIX] = 1000;
    ve->limits[0][P_BEAT_SPEED] = 0;  ve->limits[1][P_BEAT_SPEED] = 1000;ve->defaults[P_BEAT_SPEED] = 0;
    ve->limits[0][P_BEAT_BEAM] = 0;   ve->limits[1][P_BEAT_BEAM] = 1000; ve->defaults[P_BEAT_BEAM] = 0;
    ve->limits[0][P_BEAT_GLOW] = 0;   ve->limits[1][P_BEAT_GLOW] = 255;  ve->defaults[P_BEAT_GLOW] = 0;
    ve->limits[0][P_BEAT_DECAY] = 0;  ve->limits[1][P_BEAT_DECAY] = 1000;ve->defaults[P_BEAT_DECAY] = 0;
    ve->limits[0][P_BEAT_PUSH] = 0;   ve->limits[1][P_BEAT_PUSH] = 1000; ve->defaults[P_BEAT_PUSH] = 0;
    ve->limits[0][P_BEAT_SMOOTH] = 0; ve->limits[1][P_BEAT_SMOOTH] = 1000;ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Scanline";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Direction",
        "Speed",
        "Stop Duration",
        "Beam Width",
        "Trail Hold",
        "Scan Mix",
        "Beat Speed",
        "Beat Beam",
        "Beat Glow",
        "Beat Decay",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_DIRECTION],
        P_DIRECTION,
        "Top to Bottom",
        "Bottom to Top",
        "Left to Right",
        "Right to Left"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SPEED,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                            4,                  360,                16, 62,  700, 2800, 0,    88,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED,                                  0,                  320,                10, 38, 1200, 4200, 0,    58,
        VJ_BEAT_WINDOW_RADIUS,VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                           2,                  72,                 14, 54,  800, 3000, 0,    76,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                             96,                 255,                12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                             420,                1000,               12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_SPEED,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                             120,                1000,               16, 62,  700, 2800, 0,    92,
        VJ_BEAT_WARP,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                             120,                1000,               16, 62,  700, 2800, 0,    86,
        VJ_BEAT_GLOW,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                             32,                 255,                16, 62,  600, 2400, 0,    90,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,         0,                  820,                12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_TRIGGER,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );
    return ve;
}

void *scanline_malloc(int w, int h)
{
    scanline_t *s = (scanline_t*)vj_calloc(sizeof(scanline_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    veejay_memset(s->buf[0], pixel_Y_lo_, len);
    veejay_memset(s->buf[1], 128, len);
    veejay_memset(s->buf[2], 128, len);

    s->prevRow = 0;
    s->prevCol = 0;
    s->stopCount = DEFAULT_STOP_DURATION;
    s->beat_env = 0.0f;
    s->beat_kick = 0.0f;
    s->sm_ready = 0;
    s->n_threads = vje_advise_num_threads(len);

    return (void*)s;
}

void scanline_free(void *ptr)
{
    scanline_t *s = (scanline_t*)ptr;

    free(s->buf[0]);
    free(s);
}

static void scanline_fade_buffer(scanline_t *s, int len, int hold)
{
    if(hold >= 255)
        return;

    const int inv = 255 - hold;

    uint8_t *restrict y = s->buf[0];
    uint8_t *restrict u = s->buf[1];
    uint8_t *restrict v = s->buf[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        y[i] = scanline_clamp_y((((int)y[i] * hold) + (pixel_Y_lo_ * inv) + 127) / 255);
        u[i] = scanline_clamp_uv((((int)u[i] * hold) + (128 * inv) + 127) / 255);
        v[i] = scanline_clamp_uv((((int)v[i] * hold) + (128 * inv) + 127) / 255);
    }
}

static void scanline_mix_output(uint8_t *restrict dstY,
                                uint8_t *restrict dstU,
                                uint8_t *restrict dstV,
                                const uint8_t *restrict bufY,
                                const uint8_t *restrict bufU,
                                const uint8_t *restrict bufV,
                                int len,
                                int mix_q8,
                                int n_threads)
{
    if(mix_q8 >= 256) {
        veejay_memcpy(dstY, bufY, len);
        veejay_memcpy(dstU, bufU, len);
        veejay_memcpy(dstV, bufV, len);
        return;
    }

    if(mix_q8 <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        dstY[i] = scanline_blend_y(dstY[i], bufY[i], mix_q8);
        dstU[i] = scanline_blend_uv(dstU[i], bufU[i], mix_q8);
        dstV[i] = scanline_blend_uv(dstV[i], bufV[i], mix_q8);
    }
}

static void scanline_overlay_horizontal(uint8_t *restrict y,
                                        int width,
                                        int height,
                                        int center,
                                        int beam,
                                        int glow,
                                        int n_threads)
{
    if(glow <= 0 || beam <= 0)
        return;

    int y0 = center - beam;
    int y1 = center + beam;

    if(y0 < 0)
        y0 = 0;
    if(y1 >= height)
        y1 = height - 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int row = y0; row <= y1; row++) {
        int d = row - center;

        if(d < 0)
            d = -d;

        const int a = glow - ((glow * d) / (beam + 1));

        if(a > 0) {
            uint8_t *restrict p = y + row * width;

            for(int x = 0; x < width; x++)
                p[x] = scanline_clamp_y((int)p[x] + a);
        }
    }
}

static void scanline_overlay_vertical(uint8_t *restrict y,
                                      int width,
                                      int height,
                                      int center,
                                      int beam,
                                      int glow,
                                      int n_threads)
{
    if(glow <= 0 || beam <= 0)
        return;

    int x0 = center - beam;
    int x1 = center + beam;

    if(x0 < 0)
        x0 = 0;
    if(x1 >= width)
        x1 = width - 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int row = 0; row < height; row++) {
        uint8_t *restrict p = y + row * width;

        for(int x = x0; x <= x1; x++) {
            int d = x - center;

            if(d < 0)
                d = -d;

            const int a = glow - ((glow * d) / (beam + 1));

            if(a > 0)
                p[x] = scanline_clamp_y((int)p[x] + a);
        }
    }
}

void scanline_apply(void *ptr, VJFrame *frame, int *args)
{
    scanline_t *s = (scanline_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = width * height;

    const int mode = args[P_DIRECTION];
    const int speed_arg = args[P_SPEED];
    const int stop_arg = args[P_STOP];
    const int beam_arg = args[P_BEAM_WIDTH];
    const int hold_arg = args[P_TRAIL_HOLD];
    const int mix_arg = args[P_SCAN_MIX];
    const int bs_arg = args[P_BEAT_SPEED];
    const int bb_arg = args[P_BEAT_BEAM];
    const int glow_arg = args[P_BEAT_GLOW];
    const int decay_arg = args[P_BEAT_DECAY];
    const int beat_arg = args[P_BEAT_PUSH];
    const int smooth_arg = args[P_BEAT_SMOOTH];

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    const float smooth = (float)smooth_arg * 0.001f;
    const float param_a = 0.46f - smooth * 0.32f;

    if(!s->sm_ready) {
        s->sm_speed = (float)speed_arg;
        s->sm_stop = (float)stop_arg;
        s->sm_beam = (float)beam_arg;
        s->sm_hold = (float)hold_arg;
        s->sm_mix = (float)mix_arg;
        s->sm_beat_speed = (float)bs_arg;
        s->sm_beat_beam = (float)bb_arg;
        s->sm_beat_glow = (float)glow_arg;
        s->sm_beat_decay = (float)decay_arg;
        s->sm_ready = 1;
    }
    else {
        s->sm_speed = scanline_smooth_to(s->sm_speed, (float)speed_arg, param_a);
        s->sm_stop = scanline_smooth_to(s->sm_stop, (float)stop_arg, param_a);
        s->sm_beam = scanline_smooth_to(s->sm_beam, (float)beam_arg, param_a);
        s->sm_hold = scanline_smooth_to(s->sm_hold, (float)hold_arg, param_a);
        s->sm_mix = scanline_smooth_to(s->sm_mix, (float)mix_arg, param_a);
        s->sm_beat_speed = scanline_smooth_to(s->sm_beat_speed, (float)bs_arg, param_a);
        s->sm_beat_beam = scanline_smooth_to(s->sm_beat_beam, (float)bb_arg, param_a);
        s->sm_beat_glow = scanline_smooth_to(s->sm_beat_glow, (float)glow_arg, param_a);
        s->sm_beat_decay = scanline_smooth_to(s->sm_beat_decay, (float)decay_arg, param_a);
    }

    const int beat_shaped = scanline_beat_shape(beat_arg);
    const float target = (float)beat_shaped * 0.001f;
    const float attack = 0.18f + (1.0f - smooth) * 0.38f;
    const float release = 0.028f + (1.0f - smooth) * 0.095f;
    const float old_env = s->beat_env;

    if(target > s->beat_env)
        s->beat_env += (target - s->beat_env) * attack;
    else
        s->beat_env += (target - s->beat_env) * release;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    const float rise = s->beat_env - old_env;

    if(rise > 0.0f)
        s->beat_kick += rise * 1.55f;

    s->beat_kick *= 0.50f + smooth * 0.30f;

    if(s->beat_kick > 1.0f)
        s->beat_kick = 1.0f;
    else if(s->beat_kick < 0.0001f)
        s->beat_kick = 0.0f;

    const int beat_q = clampi((int)(s->beat_env * 700.0f + s->beat_kick * 520.0f + 0.5f), 0, 1000);
    const int long_axis = mode < 2 ? height : width;
    const int speed_base = clampi((int)(s->sm_speed + 0.5f), 1, 500);
    const int stop_base = clampi((int)(s->sm_stop + 0.5f), 0, 500);
    const int beam_base = clampi((int)(s->sm_beam + 0.5f), 1, 96);
    const int hold_base = clampi((int)(s->sm_hold + 0.5f), 0, 255);
    const int mix_base = clampi((int)(s->sm_mix + 0.5f), 0, 1000);
    const int beat_speed = clampi((int)(s->sm_beat_speed + 0.5f), 0, 1000);
    const int beat_beam = clampi((int)(s->sm_beat_beam + 0.5f), 0, 1000);
    const int beat_glow = clampi((int)(s->sm_beat_glow + 0.5f), 0, 255);
    const int beat_decay = clampi((int)(s->sm_beat_decay + 0.5f), 0, 1000);
    const int speed_headroom = long_axis / 4 + 4;

    int eff_speed = speed_base;

    eff_speed += (beat_speed * speed_headroom + 500) / 1000;
    eff_speed += (beat_q * (long_axis / 8 + 3) + 500) / 1000;
    eff_speed = clampi(eff_speed, 1, long_axis);

    int eff_beam = beam_base;

    eff_beam += (beat_beam * 96 + 500) / 1000;
    eff_beam += (beat_q * 34 + 500) / 1000;
    eff_beam = clampi(eff_beam, 1, 160);

    int eff_glow = beat_glow + ((beat_q * 176 + 500) / 1000);

    eff_glow = clampi(eff_glow, 0, 255);

    int eff_hold = hold_base - ((beat_decay * hold_base + 500) / 1000);

    eff_hold += (beat_q * (255 - eff_hold) + 500) / 1000;
    eff_hold = clampi(eff_hold, 0, 255);

    int eff_mix = mix_base + ((beat_q * (1000 - mix_base) + 500) / 1000);

    eff_mix = clampi(eff_mix, 0, 1000);

    const int mix_q8 = (eff_mix * 256 + 500) / 1000;

    if(s->stopCount > 0) {
        const int skip = 1 + ((beat_speed * 6 + beat_q * 7 + 1000) / 2000);

        s->stopCount -= skip;

        if(s->stopCount <= 0) {
            s->prevRow = 0;
            s->prevCol = 0;
            s->stopCount = 0;

            veejay_memset(bufY, pixel_Y_lo_, len);
            veejay_memset(bufU, 128, len);
            veejay_memset(bufV, 128, len);
        }

        scanline_mix_output(dstY, dstU, dstV, bufY, bufU, bufV, len, mix_q8, s->n_threads);
        return;
    }

    scanline_fade_buffer(s, len, eff_hold);

    int head = 0;
    int horizontal = mode < 2;

    switch(mode) {
        case 0:
        {
            const int start = s->prevRow;
            int stop = start + eff_speed;

            if(stop > height)
                stop = height;

            for(int row = start; row < stop; row++) {
                const int offset = row * width;

                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            head = stop > 0 ? stop - 1 : start;

            if(stop == height)
                s->stopCount = (stop_base * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevRow = (start + eff_speed) % height;
            break;
        }

        case 1:
        {
            const int start = height - 1 - s->prevRow;
            int stop = height - s->prevRow - eff_speed;

            if(stop < 0)
                stop = 0;

            for(int row = start; row >= stop; row--) {
                const int offset = row * width;

                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            head = stop;

            if(stop == 0)
                s->stopCount = (stop_base * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevRow = (s->prevRow + eff_speed) % height;
            break;
        }

        case 2:
        {
            const int start = s->prevCol;
            int stop = start + eff_speed;

            if(stop > width)
                stop = width;

            for(int row = 0; row < height; row++) {
                const int base = row * width;

                for(int col = start; col < stop; col++) {
                    const int idx = base + col;

                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            head = stop > 0 ? stop - 1 : start;
            horizontal = 0;

            if(stop == width)
                s->stopCount = (stop_base * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevCol = (start + eff_speed) % width;
            break;
        }

        case 3:
        default:
        {
            const int start = width - 1 - s->prevCol;
            int stop = width - s->prevCol - eff_speed;

            if(stop < 0)
                stop = 0;

            for(int row = 0; row < height; row++) {
                const int base = row * width;

                for(int col = start; col >= stop; col--) {
                    const int idx = base + col;

                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            head = stop;
            horizontal = 0;

            if(stop == 0)
                s->stopCount = (stop_base * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevCol = (s->prevCol + eff_speed) % width;
            break;
        }
    }

    scanline_mix_output(dstY, dstU, dstV, bufY, bufU, bufV, len, mix_q8, s->n_threads);

    if(horizontal)
        scanline_overlay_horizontal(dstY, width, height, head, eff_beam, eff_glow, s->n_threads);
    else
        scanline_overlay_vertical(dstY, width, height, head, eff_beam, eff_glow, s->n_threads);
}
