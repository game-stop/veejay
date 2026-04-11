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

typedef struct
{
    int n_threads;
} isolate_t;

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

vj_effect *isolate_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500; /* Hue Angle */
    ve->defaults[1] = 0;    /* Red */
    ve->defaults[2] = 255;  /* Green */
    ve->defaults[3] = 0;    /* Blue */
    ve->defaults[4] = 40;   /* Threshold */
    ve->defaults[5] = 160;  /* Solidity */
    ve->defaults[6] = 128;  /* Background Level (Luminance) */

    ve->limits[0][0] = 500;  ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 255;

    ve->description = "Isolate by Color Key (Advanced)";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Bg Level");

    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    return ve;
}

void *isolate_malloc(int w, int h) {
    isolate_t *iso = (isolate_t*) vj_malloc(sizeof(isolate_t));
    if(!iso) return NULL;
    iso->n_threads = vje_advise_num_threads(w * h);
    return (void*) iso;
}

void isolate_free(void *ptr) {
    if(ptr) free(ptr);
}

void isolate_apply(void *ptr, VJFrame *frame, int *args) {
    isolate_t *iso = (isolate_t*) ptr;
    int iy, iu, iv;

    _rgb2yuv(args[1], args[2], args[3], iy, iu, iv);

    const int SCALE = 4096;
    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;

    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);
    if (mag_f < 1.0f) mag_f = 1.0f;

    const int mag_fp   = (int)(mag_f * SCALE);
    const int cos_q_fp = (int)((ut_f / mag_f) * SCALE);
    const int sin_q_fp = (int)((vt_f / mag_f) * SCALE);

    const float angle_rad = ((float)args[0] / 100.0f) * (M_PI / 180.0f);
    const int inv_wedge_slope_fp = (int)((1.0f / tanf(angle_rad)) * SCALE);

    const float diff = (float)args[5] - (float)args[4];
    const int inv_range_fp = (int)((255.0f / (diff < 1.0f ? 1.0f : diff)) * (1 << 8));
    const int black_clip_fp = (int)(args[4] * SCALE);

    const uint8_t bg_level = (uint8_t)args[6];

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const int len = frame->len;

    #pragma omp parallel num_threads(iso->n_threads)
    {
        #pragma omp for schedule(static)
        for (int pos = 0; pos < len; pos++) {
            int uc = (int)Cb[pos] - 128;
            int vc = (int)Cr[pos] - 128;

            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
            int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
            int abs_yy = (yy < 0) ? -yy : yy;

            int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
            int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;

            if (alpha == 0) {
                Y[pos] = bg_level;
            } else if (alpha < 255) {

                Y[pos] = DIV255((int)Y[pos] * alpha + (int)bg_level * (255 - alpha));
            }

            Cb[pos] = 128;
            Cr[pos] = 128;
        }
    }
}