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
} rgbkey_t;

vj_effect *rgbkeysmooth_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4500;
    ve->defaults[1] = 0;
    ve->defaults[2] = 255;
    ve->defaults[3] = 0;
    ve->defaults[4] = 40;
    ve->defaults[5] = 160;
    ve->defaults[6] = 200;
    ve->defaults[7] = 0;
    
    ve->limits[0][0] = 500;  ve->limits[1][0] = 8500;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;    ve->limits[1][4] = 255;
    ve->limits[0][5] = 1;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 255;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 1;

    ve->description = "Advanced Chroma Key";
    
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Hue Angle", "Red", "Green", "Blue", "Threshold", "Solidity", "Spill Kill", "Mode");

    ve->has_user = 0;
    ve->extra_frame = 1;
    ve->sub_format = 1; 
	ve->rgb_conv = 1;
    return ve;
}

void *rgbkeysmooth_malloc(int w, int h) {
    rgbkey_t *r = (rgbkey_t*) vj_malloc(sizeof(rgbkey_t));
    if(!r) return NULL;
    r->n_threads = vje_advise_num_threads(w * h);
    return (void*) r;
}

void rgbkeysmooth_free(void *ptr) {
    if(ptr) free(ptr);
}

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

void rgbkeysmooth_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    rgbkey_t *rgbkey = (rgbkey_t*) ptr;
    int iy, iu, iv;

    _rgb2yuv(args[1], args[2], args[3], iy, iu, iv);

    const int SCALE = 4096;
    
    float ut_f = (float)iu - 128.0f;
    float vt_f = (float)iv - 128.0f;
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);
    if (mag_f < 1.0f) mag_f = 1.0f;

    int mag_fp   = (int)(mag_f * SCALE);
    int cos_q_fp = (int)((ut_f / mag_f) * SCALE);
    int sin_q_fp = (int)((vt_f / mag_f) * SCALE);
    
    float angle_rad = ((float)args[0] / 100.0f) * (3.14159265f / 180.0f);
    int inv_wedge_slope_fp = (int)((1.0f / tanf(angle_rad)) * SCALE);

    float diff = (float)args[5] - (float)args[4];

    int inv_range_fp = (int)((255.0f / (diff < 1.0f ? 1.0f : diff)) * (1 << 8));
    int black_clip_fp = (int)(args[4] * SCALE);
    int spill_factor_fp = (int)((((float)args[6] / 255.0f) / mag_f) * SCALE);
    
    int ut = (int)ut_f;
    int vt = (int)vt_f;
    int mode = args[7];

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int len = frame->len;

    #pragma omp parallel num_threads(rgbkey->n_threads)

    if (mode != 0) {
        #pragma omp for schedule(static)
        for (int pos = 0; pos < len; pos++) {
            int uc = (int)Cb[pos] - 128;
            int vc = (int)Cr[pos] - 128;
            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
            int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
            int abs_yy = (yy < 0) ? -yy : yy;
            
            int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
            int a = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;
            
            Y[pos] = (a < 0) ? 0 : (a > 255 ? 255 : (uint8_t)a);
            Cb[pos] = 128; Cr[pos] = 128;
        }
    } else {
        #pragma omp for schedule(static)
        for (int pos = 0; pos < len; pos++) {
            int uc = (int)Cb[pos] - 128;
            int vc = (int)Cr[pos] - 128;

            int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
            int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;

            int abs_yy = (yy < 0) ? -yy : yy;
            int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
            
            int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;
            alpha = (alpha < 0) ? 0 : (alpha > 255 ? 255 : alpha);
            int invA = 255 - alpha;

            int cb_c = Cb[pos];
            int cr_c = Cr[pos];

            if (xx > 0) {
                int suppress_fp = (xx * spill_factor_fp);
                if (suppress_fp > SCALE) suppress_fp = SCALE;

                cb_c -= (suppress_fp * ut) >> 12;
                cr_c -= (suppress_fp * vt) >> 12;
                
                cb_c = (cb_c < 0) ? 0 : (cb_c > 255 ? 255 : cb_c);
                cr_c = (cr_c < 0) ? 0 : (cr_c > 255 ? 255 : cr_c);
            }

            Y[pos]  = DIV255(Y[pos]  * alpha + Y2[pos]  * invA);
            Cb[pos] = DIV255(cb_c    * alpha + Cb2[pos] * invA);
            Cr[pos] = DIV255(cr_c    * alpha + Cr2[pos] * invA);
        }
    }
}
