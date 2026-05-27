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
#include "warppers.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LUT_SIZE 3600

typedef struct {
    uint8_t *buf[3];
    double *lut;
    double *cos_lut;
    double *sin_lut;
    int n_threads;
    int w;
    int h;
} warppers_t;

static inline int warppers_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int warppers_wrap_lut(int v)
{
    v %= LUT_SIZE;

    if(v < 0)
        v += LUT_SIZE;

    return v;
}

static inline int warppers_wrap_double(double v, int max)
{
    if(max <= 1 || !isfinite(v))
        return 0;

    v = fmod(v, (double)max);

    if(v < 0.0)
        v += (double)max;

    if(v >= (double)max)
        return max - 1;

    return (int)v;
}

static void warppers_init_trig_lut(warppers_t *f)
{
    for(int i = 0; i < LUT_SIZE; i++) {
        const double a = ((double)i * 0.1) * (M_PI / 180.0);

        f->cos_lut[i] = cos(a);
        f->sin_lut[i] = sin(a);
    }
}

vj_effect *warppers_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;    ve->limits[1][0] = 3600; ve->defaults[0] = 15;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 3600; ve->defaults[1] = 0;
    ve->limits[0][2] = 1;    ve->limits[1][2] = 1000; ve->defaults[2] = 100;
    ve->limits[0][3] = 0;    ve->limits[1][3] = w;    ve->defaults[3] = w / 2;
    ve->limits[0][4] = 0;    ve->limits[1][4] = h;    ve->defaults[4] = h / 2;
    ve->limits[0][5] = 0;    ve->limits[1][5] = 1000; ve->defaults[5] = 0;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 1000; ve->defaults[6] = 0;

    ve->description = "Warp Perspective";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "X Angle",
        "Y Angle",
        "Zoom",
        "X Center",
        "Y Center",
        "Distance Falloff",
        "Perspective Strength"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, 0, 3600, 8, 30, 1200, 3000, 0, 55, /* X Angle */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, 0, 3600, 8, 30, 1200, 3000, 0, 55, /* Y Angle */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                  20, 260, 8, 30, 1200, 3000, 0, 48, /* Zoom */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS,                  0, w,    8, 30, 1200, 3000, 0, 35, /* X Center */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS,                  0, h,    8, 30, 1200, 3000, 0, 35, /* Y Center */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                  0, 420,  8, 30, 1200, 3000, 0, 50, /* Distance Falloff */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                  0, 420,  8, 30, 1200, 3000, 0, 50  /* Perspective Strength */
    );

    return ve;
}

void *warppers_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    warppers_t *s = (warppers_t*) vj_calloc(sizeof(warppers_t));
    if(!s)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;

    s->buf[0] = (uint8_t*) vj_malloc(len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->lut = (double*) vj_malloc(sizeof(double) * (size_t)LUT_SIZE * 2u);
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->sin_lut = s->lut;
    s->cos_lut = s->sin_lut + LUT_SIZE;

    s->w = w;
    s->h = h;

    s->n_threads = vje_advise_num_threads((int)len);

    warppers_init_trig_lut(s);

    return (void*) s;
}

void warppers_free(void *ptr)
{
    warppers_t *s = (warppers_t*) ptr;

    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    if(s->lut)
        free(s->lut);

    free(s);
}

void warppers_apply(void *ptr, VJFrame *frame, int *args)
{
    warppers_t *warp = (warppers_t*) ptr;

    if(!warp || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    if(w != warp->w || h != warp->h)
        return;

    const int x_angle = warppers_wrap_lut(warppers_clampi(args[0], 0, 3600));
    const int y_angle = warppers_wrap_lut(warppers_clampi(args[1], 0, 3600));
    const int zoom_arg = warppers_clampi(args[2], 1, 1000);
    const int x_center = warppers_clampi(args[3], 0, w - 1);
    const int y_center = warppers_clampi(args[4], 0, h - 1);
    const int falloff_arg = warppers_clampi(args[5], 0, 1000);
    const int strength_arg = warppers_clampi(args[6], 0, 1000);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = warp->buf[0];
    uint8_t *restrict srcU = warp->buf[1];
    uint8_t *restrict srcV = warp->buf[2];

    const size_t plane_size = (size_t)w * (size_t)h;

    veejay_memcpy(srcY, dstY, plane_size);
    veejay_memcpy(srcU, dstU, plane_size);
    veejay_memcpy(srcV, dstV, plane_size);

    const double zoom = (double)zoom_arg * 0.01;
    double falloff = (double)falloff_arg * 0.01;
    const double strength = (double)strength_arg * 0.01;

    falloff *= falloff;

    const double strength_factor = 1.0 - strength;
    const double cos_val = warp->cos_lut[x_angle];
    const double sin_val = warp->sin_lut[y_angle];

    int64_t half_w = w >> 1;
    int64_t half_h = h >> 1;
    int64_t max_dist_i = half_w * half_w + half_h * half_h;

    if(max_dist_i <= 0)
        max_dist_i = 1;

    const double inv_max_dist = 1.0 / (double)max_dist_i;

#pragma omp parallel for schedule(static) num_threads(warp->n_threads)
    for(int y_pos = 0; y_pos < h; y_pos++) {
        const int row = y_pos * w;

        for(int x_pos = 0; x_pos < w; x_pos++) {
            const int idx = row + x_pos;

            const int dx = x_pos - x_center;
            const int dy = y_pos - y_center;

            const int64_t dist = (int64_t)dx * (int64_t)dx + (int64_t)dy * (int64_t)dy;
            const double dmd = (double)dist * inv_max_dist;

            const double factor =
                (1.0 - falloff * dmd) *
                (strength_factor + strength * dmd);

            const double sx =
                (double)x_center +
                (zoom * factor * ((cos_val * (double)dx) - (sin_val * (double)dy)));

            const double sy =
                (double)y_center +
                (zoom * factor * ((sin_val * (double)dx) + (cos_val * (double)dy)));

            const int x = warppers_wrap_double(sx, w);
            const int y = warppers_wrap_double(sy, h);
            const int src = y * w + x;

            dstY[idx] = srcY[src];
            dstU[idx] = srcU[src];
            dstV[idx] = srcV[src];
        }
    }
}