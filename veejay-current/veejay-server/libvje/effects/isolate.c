/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>

#define ISOLATE_PARAMS 7

#define P_HUE_ANGLE 0
#define P_RED       1
#define P_GREEN     2
#define P_BLUE      3
#define P_THRESHOLD 4
#define P_SOLIDITY  5
#define P_BG_LEVEL  6

#define ISOLATE_SCALE 4096
#define ISOLATE_PI    3.14159265358979323846f

typedef struct {
    int n_threads;
    int last[ISOLATE_PARAMS];
    int mag_fp;
    int cos_q_fp;
    int sin_q_fp;
    int inv_wedge_slope_fp;
    int inv_range_fp;
    int black_clip_fp;
    uint8_t bg_level;
} isolate_t;

static inline int isolate_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int isolate_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t isolate_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * opacity + (int)b * inv;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *isolate_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = ISOLATE_PARAMS;
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

    ve->defaults[P_HUE_ANGLE] = 4500;
    ve->defaults[P_RED] = 0;
    ve->defaults[P_GREEN] = 255;
    ve->defaults[P_BLUE] = 0;
    ve->defaults[P_THRESHOLD] = 40;
    ve->defaults[P_SOLIDITY] = 160;
    ve->defaults[P_BG_LEVEL] = 128;

    ve->limits[0][P_HUE_ANGLE] = 500; ve->limits[1][P_HUE_ANGLE] = 8500;
    ve->limits[0][P_RED] = 0;         ve->limits[1][P_RED] = 255;
    ve->limits[0][P_GREEN] = 0;       ve->limits[1][P_GREEN] = 255;
    ve->limits[0][P_BLUE] = 0;        ve->limits[1][P_BLUE] = 255;
    ve->limits[0][P_THRESHOLD] = 0;   ve->limits[1][P_THRESHOLD] = 255;
    ve->limits[0][P_SOLIDITY] = 1;    ve->limits[1][P_SOLIDITY] = 255;
    ve->limits[0][P_BG_LEVEL] = 0;    ve->limits[1][P_BG_LEVEL] = 255;

    ve->description = "Isolate by Color Key (Advanced)";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Hue Angle",
        "Red",
        "Green",
        "Blue",
        "Threshold",
        "Solidity",
        "Bg Level"
    );

    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    return ve;
}

void *isolate_malloc(int w, int h)
{
    isolate_t *s = (isolate_t*) vj_malloc(sizeof(isolate_t));

    if(!s)
        return NULL;

    for(int i = 0; i < ISOLATE_PARAMS; i++)
        s->last[i] = -1000000;

    s->mag_fp = ISOLATE_SCALE;
    s->cos_q_fp = ISOLATE_SCALE;
    s->sin_q_fp = 0;
    s->inv_wedge_slope_fp = ISOLATE_SCALE;
    s->inv_range_fp = 255 << 8;
    s->black_clip_fp = 0;
    s->bg_level = 128;
    s->n_threads = vje_advise_num_threads(w * h);

    return (void*) s;
}

void isolate_free(void *ptr)
{
    free(ptr);
}

static void isolate_update_cache(isolate_t *s, const int *args)
{
    int changed = 0;

    for(int i = 0; i < ISOLATE_PARAMS; i++) {
        if(args[i] != s->last[i]) {
            changed = 1;
            break;
        }
    }

    if(!changed)
        return;

    const int angle = isolate_clampi(args[P_HUE_ANGLE], 500, 8500);
    const int red = isolate_clampi(args[P_RED], 0, 255);
    const int green = isolate_clampi(args[P_GREEN], 0, 255);
    const int blue = isolate_clampi(args[P_BLUE], 0, 255);
    const int threshold = isolate_clampi(args[P_THRESHOLD], 0, 255);
    const int solidity = isolate_clampi(args[P_SOLIDITY], 1, 255);
    const int range = solidity > threshold ? solidity - threshold : 1;

    int iy = 0;
    int iu = 128;
    int iv = 128;

    _rgb2yuv(red, green, blue, iy, iu, iv);

    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);

    if(mag_f < 1.0f)
        mag_f = 1.0f;

    const float angle_rad = ((float)angle * 0.01f) * (ISOLATE_PI / 180.0f);
    const float t = tanf(angle_rad);

    s->mag_fp = (int)(mag_f * (float)ISOLATE_SCALE + 0.5f);
    s->cos_q_fp = (int)((ut_f / mag_f) * (float)ISOLATE_SCALE + (ut_f >= 0.0f ? 0.5f : -0.5f));
    s->sin_q_fp = (int)((vt_f / mag_f) * (float)ISOLATE_SCALE + (vt_f >= 0.0f ? 0.5f : -0.5f));
    s->inv_wedge_slope_fp = (int)((1.0f / t) * (float)ISOLATE_SCALE + 0.5f);
    s->inv_range_fp = (int)((255.0f / (float)range) * 256.0f + 0.5f);
    s->black_clip_fp = threshold * ISOLATE_SCALE;
    s->bg_level = (uint8_t)isolate_clampi(args[P_BG_LEVEL], 0, 255);

    for(int i = 0; i < ISOLATE_PARAMS; i++)
        s->last[i] = args[i];
}

void isolate_apply(void *ptr, VJFrame *frame, int *args)
{
    isolate_t *s = (isolate_t*) ptr;

    isolate_update_cache(s, args);

    const int mag_fp = s->mag_fp;
    const int cos_q_fp = s->cos_q_fp;
    const int sin_q_fp = s->sin_q_fp;
    const int inv_wedge_slope_fp = s->inv_wedge_slope_fp;
    const int inv_range_fp = s->inv_range_fp;
    const int black_clip_fp = s->black_clip_fp;
    const uint8_t bg_level = s->bg_level;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int pos = 0; pos < len; pos++) {
        const int uc = (int)Cb[pos] - 128;
        const int vc = (int)Cr[pos] - 128;

        const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        const int abs_yy = isolate_absi(yy);

        const int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
        int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

        alpha = isolate_clampi(alpha, 0, 255);

        if(alpha == 0)
            Y[pos] = bg_level;
        else if(alpha < 255)
            Y[pos] = isolate_blend255(Y[pos], bg_level, alpha);

        Cb[pos] = 128;
        Cr[pos] = 128;
    }
}
