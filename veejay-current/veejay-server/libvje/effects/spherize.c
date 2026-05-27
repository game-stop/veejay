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
#include "spherize.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    uint8_t *buf[3];
    float *lut;
    float *atan2_lut;
    float *sin_lut;
    float *dist_lut;
    float *exp_lut;
    int last_cx;
    int last_cy;
    int last_radius;
    float last_angle;
    int n_threads;
} spherize_t;

static inline int spherize_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int spherize_wrapi(int v, int max)
{
    if(max <= 1)
        return 0;

    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline int spherize_reflecti(int v, int max)
{
    if(max <= 1)
        return 0;

    const int hi = max - 1;
    const int period = hi << 1;

    if(period <= 0)
        return 0;

    v %= period;

    if(v < 0)
        v += period;

    return (v <= hi) ? v : period - v;
}

vj_effect *spherize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int max_radius = (int)sqrtf(((float)w * (float)w * 0.25f) + ((float)h * (float)h * 0.25f));

    ve->limits[0][0] = 0;              ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;              ve->limits[1][1] = 360;
    ve->limits[0][2] = 1;              ve->limits[1][2] = max_radius > 1 ? max_radius : 1;
    ve->limits[0][3] = 10;             ve->limits[1][3] = 200;
    ve->limits[0][4] = 10;             ve->limits[1][4] = 200;
    ve->limits[0][5] = 0;              ve->limits[1][5] = w;
    ve->limits[0][6] = 0;              ve->limits[1][6] = h;
    ve->limits[0][7] = 0;              ve->limits[1][7] = 2;

    ve->defaults[0] = 33;
    ve->defaults[1] = 340;
    ve->defaults[2] = ve->limits[1][2] / 2;
    ve->defaults[3] = 100;
    ve->defaults[4] = 100;
    ve->defaults[5] = w / 2;
    ve->defaults[6] = h / 2;
    ve->defaults[7] = 2;

    ve->description = "Spherize";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Strength",
        "Angle",
        "Radius",
        "Ratio X",
        "Ratio Y",
        "Center X",
        "Center Y",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][7],
        7,
        "Clamp",
        "Wrap",
        "Reflect"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                       0,                  88,                 8, 30, 1200, 3000, 0,   55,    /* Strength */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,      0,                  360,                8, 30, 1200, 3000, 0,   45,    /* Angle */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE, 8,              ve->limits[1][2],   8, 30, 1200, 3000, 0,   45,    /* Radius */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                       50,                 160,                8, 30, 1200, 3000, 0,   42,    /* Ratio X */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                       50,                 160,                8, 30, 1200, 3000, 0,   42,    /* Ratio Y */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE, 0,              w,                  8, 30, 1200, 3000, 0,   35,    /* Center X */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE, 0,              h,                  8, 30, 1200, 3000, 0,   35,    /* Center Y */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    return ve;
}

static void spherize_rebuild_center_luts(spherize_t *s, int w, int h, int cx, int cy)
{
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < h; y++) {
        const float dy = (float)(y - cy);
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const float dx = (float)(x - cx);
            const int idx = row + x;

            s->atan2_lut[idx] = atan2f(dy, dx);
            s->dist_lut[idx] = sqrtf(dx * dx + dy * dy);
        }
    }

    s->last_cx = cx;
    s->last_cy = cy;
    s->last_angle = -999999.0f;
    s->last_radius = -1;
}

static void spherize_rebuild_sin_lut(spherize_t *s, int len, float angle)
{
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++)
        s->sin_lut[i] = sinf(s->atan2_lut[i] - angle);

    s->last_angle = angle;
}

static void spherize_rebuild_exp_lut(spherize_t *s, int len, int radius)
{
    const float r = (float)(radius > 0 ? radius : 1);
    const float inv_sigma = 1.0f / (2.0f * r * r);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        const float d = s->dist_lut[i];
        s->exp_lut[i] = expf(-(d * d) * inv_sigma);
    }

    s->last_radius = radius;
}

void *spherize_malloc(int w, int h)
{
    spherize_t *s = (spherize_t*) vj_calloc(sizeof(spherize_t));
    if(!s)
        return NULL;

    const int pixels = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)pixels * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + pixels;
    s->buf[2] = s->buf[1] + pixels;

    s->lut = (float*) vj_malloc(sizeof(float) * (size_t)pixels * 4u);
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan2_lut = s->lut;
    s->sin_lut   = s->atan2_lut + pixels;
    s->dist_lut  = s->sin_lut + pixels;
    s->exp_lut   = s->dist_lut + pixels;

    s->last_cx = INT_MIN;
    s->last_cy = INT_MIN;
    s->last_radius = -1;
    s->last_angle = -999999.0f;

    s->n_threads = vje_advise_num_threads(pixels);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void spherize_free(void *ptr)
{
    spherize_t *s = (spherize_t*) ptr;
    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    if(s->lut)
        free(s->lut);

    free(s);
}

void spherize_apply(void *ptr, VJFrame *frame, int *args)
{
    spherize_t *s = (spherize_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int max_radius = (int)sqrtf(((float)width * (float)width * 0.25f) + ((float)height * (float)height * 0.25f));

    const float strength = (float)spherize_clampi(args[0], 0, 100) * 0.01f;
    const float angle = (float)spherize_clampi(args[1], 0, 360) * ((float)M_PI / 180.0f);
    const int radius = spherize_clampi(args[2], 1, max_radius > 1 ? max_radius : 1);
    const float ratio_x = (float)spherize_clampi(args[3], 10, 200) * 0.01f;
    const float ratio_y = (float)spherize_clampi(args[4], 10, 200) * 0.01f;
    const int center_x = spherize_clampi(args[5], 0, width);
    const int center_y = spherize_clampi(args[6], 0, height);
    const int mode = spherize_clampi(args[7], 0, 2);

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if(s->last_cx != center_x || s->last_cy != center_y)
        spherize_rebuild_center_luts(s, width, height, center_x, center_y);

    if(s->last_angle != angle)
        spherize_rebuild_sin_lut(s, len, angle);

    if(s->last_radius != radius)
        spherize_rebuild_exp_lut(s, len, radius);

    const float *restrict sin_lut = s->sin_lut;
    const float *restrict exp_lut = s->exp_lut;

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = spherize_clampi(sx, 0, width - 1);
                sy = spherize_clampi(sy, 0, height - 1);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    } else if(mode == 1) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = spherize_wrapi(sx, width);
                sy = spherize_wrapi(sy, height);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = spherize_reflecti(sx, width);
                sy = spherize_reflecti(sy, height);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    }
}