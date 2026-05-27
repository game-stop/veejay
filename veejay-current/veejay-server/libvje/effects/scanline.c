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
#include "scanline.h"

#define DEFAULT_STOP_DURATION 25

#define SCANLINE_PARAMS 8

#define P_DIRECTION    0
#define P_SPEED        1
#define P_STOP         2
#define P_BEAM_WIDTH   3
#define P_TRAIL_HOLD   4
#define P_BEAT_PUSH    5
#define P_BEAT_SMOOTH  6
#define P_BEAT_GLOW    7

typedef struct
{
    uint8_t *buf[3];
    int prevRow;
    int prevCol;
    int stopCount;
    float beat_env;
    int n_threads;
} scanline_t;

static inline int scanline_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t scanline_clamp_y(int v)
{
    return (uint8_t)((v < pixel_Y_lo_) ? pixel_Y_lo_ : ((v > pixel_Y_hi_) ? pixel_Y_hi_ : v));
}

static inline uint8_t scanline_clamp_uv(int v)
{
    return (uint8_t)((v < pixel_U_lo_) ? pixel_U_lo_ : ((v > pixel_U_hi_) ? pixel_U_hi_ : v));
}

static inline int scanline_beat_shape(int beat_push)
{
    beat_push = scanline_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return scanline_clampi((beat_push * 40 + sq * 60 + 50) / 100, 0, 1000);
}

vj_effect *scanline_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = SCANLINE_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_DIRECTION]   = 0;   ve->limits[1][P_DIRECTION]   = 3;    ve->defaults[P_DIRECTION]   = 0;
    ve->limits[0][P_SPEED]       = 1;   ve->limits[1][P_SPEED]       = 500;  ve->defaults[P_SPEED]       = 1;
    ve->limits[0][P_STOP]        = 0;   ve->limits[1][P_STOP]        = 500;  ve->defaults[P_STOP]        = DEFAULT_STOP_DURATION;
    ve->limits[0][P_BEAM_WIDTH]  = 1;   ve->limits[1][P_BEAM_WIDTH]  = 96;   ve->defaults[P_BEAM_WIDTH]  = 3;
    ve->limits[0][P_TRAIL_HOLD]  = 0;   ve->limits[1][P_TRAIL_HOLD]  = 255;  ve->defaults[P_TRAIL_HOLD]  = 255;
    ve->limits[0][P_BEAT_PUSH]   = 0;   ve->limits[1][P_BEAT_PUSH]   = 1000; ve->defaults[P_BEAT_PUSH]   = 0;
    ve->limits[0][P_BEAT_SMOOTH] = 0;   ve->limits[1][P_BEAT_SMOOTH] = 1000; ve->defaults[P_BEAT_SMOOTH] = 520;
    ve->limits[0][P_BEAT_GLOW]   = 0;   ve->limits[1][P_BEAT_GLOW]   = 255;  ve->defaults[P_BEAT_GLOW]   = 96;

    ve->description = "Scanline";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Direction",
        "Speed",
        "Stop Duration",
        "Beam Width",
        "Trail Hold",
        "Beat Push",
        "Beat Smooth",
        "Beat Glow"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Direction */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                             1,                  160,                10, 42, 900,  2400, 0,    62,    /* Speed */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,       0,                  180,                6,  22, 1800, 4200, 900,  28,    /* Stop Duration */
        VJ_BEAT_DETAIL,    VJ_BEAT_F_CONTINUOUS,                             1,                  36,                 8,  30, 1000, 2600, 0,    36,    /* Beam Width */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY,                            190,                255,                5,  18, 2200, 5200, 1200, 18,    /* Trail Hold */
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS,                             0,                  820,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY,                            260,                850,                5,  18, 2200, 5200, 1200, 18,    /* Beat Smooth */
        VJ_BEAT_GLOW,      VJ_BEAT_F_CONTINUOUS,                             0,                  180,                8,  32, 900,  2400, 0,    45     /* Beat Glow */
    );

    (void) w;
    (void) h;

    return ve;
}

void *scanline_malloc(int w, int h)
{
    scanline_t *s = (scanline_t*) vj_calloc(sizeof(scanline_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 3u);
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
    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void scanline_free(void *ptr)
{
    scanline_t *s = (scanline_t*) ptr;

    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

static void scanline_fade_buffer(scanline_t *s,
                                 int len,
                                 int hold)
{
    if(hold >= 255)
        return;

    hold = scanline_clampi(hold, 0, 255);
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

    if(y0 < 0) y0 = 0;
    if(y1 >= height) y1 = height - 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int row = y0; row <= y1; row++) {
        int d = row - center;
        if(d < 0) d = -d;

        int a = glow - ((glow * d) / (beam + 1));
        if(a <= 0)
            continue;

        uint8_t *restrict p = y + row * width;
        for(int x = 0; x < width; x++)
            p[x] = scanline_clamp_y((int)p[x] + a);
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

    if(x0 < 0) x0 = 0;
    if(x1 >= width) x1 = width - 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int row = 0; row < height; row++) {
        uint8_t *restrict p = y + row * width;
        for(int x = x0; x <= x1; x++) {
            int d = x - center;
            if(d < 0) d = -d;

            int a = glow - ((glow * d) / (beam + 1));
            if(a > 0)
                p[x] = scanline_clamp_y((int)p[x] + a);
        }
    }
}

void scanline_apply(void *ptr, VJFrame *frame, int *args)
{
    scanline_t *s = (scanline_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int mode     = scanline_clampi(args[P_DIRECTION], 0, 3);
    const int speed    = scanline_clampi(args[P_SPEED], 1, 500);
    const int duration = scanline_clampi(args[P_STOP], 0, 500);
    const int beam_arg = scanline_clampi(args[P_BEAM_WIDTH], 1, 96);
    const int hold_arg = scanline_clampi(args[P_TRAIL_HOLD], 0, 255);
    const int beat_arg = scanline_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int smooth   = scanline_clampi(args[P_BEAT_SMOOTH], 0, 1000);
    const int glow_arg = scanline_clampi(args[P_BEAT_GLOW], 0, 255);

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = width * height;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    const int beat_shaped = scanline_beat_shape(beat_arg);
    const float target = (float)beat_shaped * 0.001f;
    const float st = (float)smooth * 0.001f;
    const float attack = 0.16f + (1.0f - st) * 0.34f;
    const float release = 0.025f + (1.0f - st) * 0.085f;

    if(target > s->beat_env)
        s->beat_env += (target - s->beat_env) * attack;
    else
        s->beat_env += (target - s->beat_env) * release;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    const int beat_q = scanline_clampi((int)(s->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int long_axis = (mode < 2) ? height : width;
    const int beat_extra = ((beat_q * (long_axis / 10 + 2)) + 500) / 1000;
    const int eff_speed = scanline_clampi(speed + beat_extra, 1, long_axis > 0 ? long_axis : 1);
    const int eff_beam = scanline_clampi(beam_arg + ((beat_q * 18 + 500) / 1000), 1, 128);
    const int eff_glow = scanline_clampi(((glow_arg * beat_q) + 500) / 1000, 0, 255);
    const int eff_hold = scanline_clampi(hold_arg + ((beat_q * (255 - hold_arg) + 500) / 1000), 0, 255);

    if(s->stopCount > 0) {
        const int skip = 1 + ((beat_q * 5 + 500) / 1000);
        s->stopCount -= skip;

        if(s->stopCount <= 0) {
            s->prevRow = 0;
            s->prevCol = 0;
            s->stopCount = 0;

            veejay_memset(bufY, pixel_Y_lo_, len);
            veejay_memset(bufU, 128, len);
            veejay_memset(bufV, 128, len);
        }

        veejay_memcpy(dstY, bufY, len);
        veejay_memcpy(dstU, bufU, len);
        veejay_memcpy(dstV, bufV, len);
        return;
    }

    scanline_fade_buffer(s, len, eff_hold);

    int head = 0;
    int horizontal = (mode < 2);

    switch(mode)
    {
        case 0:
        {
            int start = s->prevRow;
            int stop  = start + eff_speed;
            if(stop > height) stop = height;

            for(int row = start; row < stop; ++row) {
                const int offset = row * width;
                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            head = (stop > 0) ? (stop - 1) : start;

            if(stop == height)
                s->stopCount = (duration * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevRow = (start + eff_speed) % height;
            break;
        }

        case 1:
        {
            int start = height - 1 - s->prevRow;
            int stop  = height - s->prevRow - eff_speed;
            if(stop < 0) stop = 0;

            for(int row = start; row >= stop; --row) {
                const int offset = row * width;
                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            head = stop;

            if(stop == 0)
                s->stopCount = (duration * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevRow = (s->prevRow + eff_speed) % height;
            break;
        }

        case 2:
        {
            int start = s->prevCol;
            int stop  = start + eff_speed;
            if(stop > width) stop = width;

            for(int row = 0; row < height; ++row) {
                const int base = row * width;
                for(int col = start; col < stop; ++col) {
                    const int idx = base + col;
                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            head = (stop > 0) ? (stop - 1) : start;
            horizontal = 0;

            if(stop == width)
                s->stopCount = (duration * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevCol = (start + eff_speed) % width;
            break;
        }

        case 3:
        default:
        {
            int start = width - 1 - s->prevCol;
            int stop  = width - s->prevCol - eff_speed;
            if(stop < 0) stop = 0;

            for(int row = 0; row < height; ++row) {
                const int base = row * width;
                for(int col = start; col >= stop; --col) {
                    const int idx = base + col;
                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            head = stop;
            horizontal = 0;

            if(stop == 0)
                s->stopCount = (duration * (1000 - (beat_q >> 1)) + 500) / 1000;

            s->prevCol = (s->prevCol + eff_speed) % width;
            break;
        }
    }

    veejay_memcpy(dstY, bufY, len);
    veejay_memcpy(dstU, bufU, len);
    veejay_memcpy(dstV, bufV, len);

    if(horizontal)
        scanline_overlay_horizontal(dstY, width, height, head, eff_beam, eff_glow, s->n_threads);
    else
        scanline_overlay_vertical(dstY, width, height, head, eff_beam, eff_glow, s->n_threads);
}
