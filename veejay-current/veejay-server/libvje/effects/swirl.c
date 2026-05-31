/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include "swirl.h"
#include <math.h>

#define SWIRL_PARAMS 5

#define P_DEGREES     0
#define P_MODE        1
#define P_BEAT_SWIRL  2
#define P_BEAT_PUSH   3
#define P_BEAT_SMOOTH 4

typedef struct {
    double *polar_map;
    double *fish_angle;

    int *cached_coords;
    int *beat_coords;

    uint8_t *buf[3];

    int v;
    int mode;
    int beat_v;
    int beat_swirl;

    float beat_env;
    float beat_kick;

    int n_threads;
    int w;
    int h;
} swirl_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int swirl_beat_shape(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return clampi((beat_push * 30 + sq * 70 + 50) / 100, 0, 1000);
}

static inline int swirl_beat_degrees(int degrees, int beat_swirl)
{
    beat_swirl = clampi(beat_swirl, 0, 1000);

    /*
     * Lower degree values make the original swirl formula stronger:
     *     a + (r / degrees)
     *
     * Beat Swirl therefore prepares a second, stronger coordinate map.
     * Beat Push only crossfades toward that map; it does not rebuild maps.
     */
    int delta = (beat_swirl * 220 + 500) / 1000;

    if(delta < 1 && beat_swirl > 0)
        delta = 1;

    return clampi(degrees - delta, 1, 360);
}

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SWIRL_PARAMS;

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

    ve->limits[0][P_DEGREES] = 1;
    ve->limits[1][P_DEGREES] = 360;
    ve->defaults[P_DEGREES] = 250;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 1;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_BEAT_SWIRL] = 0;
    ve->limits[1][P_BEAT_SWIRL] = 1000;
    ve->defaults[P_BEAT_SWIRL] = 320;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Degrees",
        "Mode",
        "Beat Swirl",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Normal",
        "Mirrored"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 24,                 360,                6,  22, 1800, 4200, 900,  30,    /* Degrees */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Mode */
        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 0,                  760,                5,  18, 1800, 4200, 900,  24,    /* Beat Swirl */
        VJ_BEAT_KICK,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                               0,                  760,                18, 72, 70,   720,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,   VJ_BEAT_F_PHRASE_ONLY,                                                   260,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *swirl_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    swirl_t *s = (swirl_t*) vj_calloc(sizeof(swirl_t));
    if(!s)
        return NULL;

    const int len = w * h;
    const int w2 = w >> 1;
    const int h2 = h >> 1;

    s->polar_map = (double*) vj_malloc(sizeof(double) * (size_t)len);
    if(!s->polar_map) {
        free(s);
        return NULL;
    }

    s->fish_angle = (double*) vj_malloc(sizeof(double) * (size_t)len);
    if(!s->fish_angle) {
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->cached_coords = (int*) vj_malloc(sizeof(int) * (size_t)len * 2u);
    if(!s->cached_coords) {
        free(s->fish_angle);
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->beat_coords = s->cached_coords + len;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s->cached_coords);
        free(s->fish_angle);
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    for(int y = 0; y < h; y++) {
        const int dy = y - h2;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int dx = x - w2;
            const int i = row + x;

            s->polar_map[i] = sqrt((double)(dy * dy + dx * dx));
            s->fish_angle[i] = atan2((double)dy, (double)dx);
            s->cached_coords[i] = i;
            s->beat_coords[i] = i;
        }
    }

    s->v = -1;
    s->mode = -1;
    s->beat_v = -1;
    s->beat_swirl = -1;
    s->beat_env = 0.0f;
    s->beat_kick = 0.0f;
    s->w = w;
    s->h = h;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void swirl_free(void *ptr)
{
    swirl_t *s = (swirl_t*) ptr;

    if(!s)
        return;

    if(s->polar_map)
        free(s->polar_map);
    if(s->fish_angle)
        free(s->fish_angle);
    if(s->cached_coords)
        free(s->cached_coords);
    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

static void swirl_rebuild_map(swirl_t *s, int width, int height, int degrees, int mode, int *restrict coords)
{
    const int len = width * height;
    const int w2 = width >> 1;
    const int h2 = height >> 1;
    const double coeff = (double)clampi(degrees, 1, 360);

    double *restrict polar_map = s->polar_map;
    double *restrict fish_angle = s->fish_angle;

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            double co;
            double si;

            const double r = polar_map[i];
            const double a = fish_angle[i];

            sin_cos(co, si, a + (r / coeff));

            int px = (int)(r * co) + w2;
            int py = (int)(r * si) + h2;

            px = clampi(px, 0, width - 1);
            py = clampi(py, 0, height - 1);

            coords[i] = py * width + px;
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const int my = (y <= h2) ? y : (height - 1 - y);

            for(int x = 0; x < width; x++) {
                double co;
                double si;

                const int mx = (x <= w2) ? x : (width - 1 - x);
                const int qidx = my * width + mx;
                const int idx = row + x;

                const double r = polar_map[qidx];
                const double a = fish_angle[qidx];

                sin_cos(co, si, a + (r / coeff));

                int px = (int)(r * co) + w2;
                int py = (int)(r * si) + h2;

                px = clampi(px, 0, width - 1);
                py = clampi(py, 0, height - 1);

                coords[idx] = py * width + px;
            }
        }
    }
}

static void swirl_rebuild_caches(swirl_t *s,
                                 int width,
                                 int height,
                                 int degrees,
                                 int mode,
                                 int beat_degrees,
                                 int beat_swirl)
{
    if(s->v != degrees || s->mode != mode)
        swirl_rebuild_map(s, width, height, degrees, mode, s->cached_coords);

    if(s->beat_v != beat_degrees || s->mode != mode || s->beat_swirl != beat_swirl)
        swirl_rebuild_map(s, width, height, beat_degrees, mode, s->beat_coords);

    s->v = degrees;
    s->mode = mode;
    s->beat_v = beat_degrees;
    s->beat_swirl = beat_swirl;
}

void swirl_apply(void *ptr, VJFrame *frame, int *args)
{
    swirl_t *s = (swirl_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != s->w || height != s->h)
        return;

    const int degrees = clampi(args[P_DEGREES], 1, 360);
    const int mode = args[P_MODE] ? 1 : 0;
    const int beat_swirl = clampi(args[P_BEAT_SWIRL], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int beat_degrees = swirl_beat_degrees(degrees, beat_swirl);

    swirl_rebuild_caches(s, width, height, degrees, mode, beat_degrees, beat_swirl);

    const int beat_shaped = swirl_beat_shape(beat_push);
    const float beat_target = (float)beat_shaped * 0.001f;
    const float smooth = (float)beat_smooth * 0.001f;

    const float prev_env = s->beat_env;
    const float attack = 0.44f - smooth * 0.27f;
    const float release = 0.060f - smooth * 0.040f;

    if(beat_target > s->beat_env)
        s->beat_env += (beat_target - s->beat_env) * attack;
    else
        s->beat_env += (beat_target - s->beat_env) * release;

    if(beat_target > prev_env)
        s->beat_kick += (beat_target - prev_env) * 0.62f;

    s->beat_kick *= 0.54f + smooth * 0.34f;

    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;
    else if(s->beat_env > 1.0f)
        s->beat_env = 1.0f;

    if(s->beat_kick < 0.0001f)
        s->beat_kick = 0.0f;
    else if(s->beat_kick > 1.0f)
        s->beat_kick = 1.0f;

    float drive = s->beat_env * 0.70f + s->beat_kick * 0.30f;
    if(drive > 1.0f)
        drive = 1.0f;

    int beat_q8 = (int)(drive * 256.0f + 0.5f);
    beat_q8 = clampi(beat_q8, 0, 256);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = s->buf[0];
    uint8_t *restrict srcCb = s->buf[1];
    uint8_t *restrict srcCr = s->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

    int *restrict base_coords = s->cached_coords;
    int *restrict beat_coords = s->beat_coords;

    if(beat_q8 <= 0 || beat_degrees == degrees) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            const int idx = base_coords[i];

            Y[i]  = srcY[idx];
            Cb[i] = srcCb[idx];
            Cr[i] = srcCr[idx];
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            const int a = base_coords[i];
            const int b = beat_coords[i];

            Y[i]  = mix_u8(srcY[a],  srcY[b],  beat_q8);
            Cb[i] = mix_u8(srcCb[a], srcCb[b], beat_q8);
            Cr[i] = mix_u8(srcCr[a], srcCr[b], beat_q8);
        }
    }
}
