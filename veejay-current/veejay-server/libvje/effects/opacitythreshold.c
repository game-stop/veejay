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
#include "opacitythreshold.h"

vj_effect *opacitythreshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 180;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 50;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 255;

    ve->description = "Soft Luma Key (edge smoothing)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Min Threshold",
        "Max Threshold"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Opacity */
        VJ_BEAT_DETAIL,           VJ_BEAT_F_PHRASE_ONLY,  8,                  120,                6, 22, 1600, 3400, 700, 35,    /* Min Threshold */
        VJ_BEAT_DETAIL,           VJ_BEAT_F_PHRASE_ONLY,  135,                245,                6, 22, 1600, 3400, 700, 35     /* Max Threshold */
    );

    (void) w;
    (void) h;

    return ve;
}

typedef struct {
    uint16_t *hblur;
    int n_threads;
} op_thres_t;

void *opacitythreshold_malloc(int w, int h)
{
    op_thres_t *opt = (op_thres_t*) vj_calloc(sizeof(op_thres_t));
    if(!opt)
        return NULL;

    opt->hblur = (uint16_t*) vj_calloc(sizeof(uint16_t) * w * h);
    if(!opt->hblur) {
        free(opt);
        return NULL;
    }

    opt->n_threads = vje_advise_num_threads(w * h);
    if(opt->n_threads < 1)
        opt->n_threads = 1;

    return (void*) opt;
}

void opacitythreshold_free(void *ptr)
{
    op_thres_t *opt = (op_thres_t*) ptr;
    if(opt) {
        if(opt->hblur)
            free(opt->hblur);
        free(opt);
    }
}

void opacitythreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    op_thres_t *opt = (op_thres_t*) ptr;
    if(!opt || !frame || !frame2 || !args)
        return;

    int opacity = args[0];
    int tmin = args[1];
    int tmax = args[2];

    if(opacity < 0)
        opacity = 0;
    else if(opacity > 255)
        opacity = 255;

    if(tmax < tmin) {
        int tmp_t = tmin;
        tmin = tmax;
        tmax = tmp_t;
    }

    const int w = frame->width;
    const int h = frame->height;

    if(w < 2 || h < 3)
        return;

    uint8_t *restrict Y   = frame->data[0];
    uint8_t *restrict Cb  = frame->data[1];
    uint8_t *restrict Cr  = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    uint16_t *restrict tmp = opt->hblur;

    const int t_diff = (tmax > tmin) ? (tmax - tmin) : 1;

#pragma omp parallel num_threads(opt->n_threads)
    {
#pragma omp for schedule(static)
        for(int y = 0; y < h; y++) {
            const int row = y * w;

            tmp[row] = (uint16_t)((Y[row] + (Y[row] << 1) + Y[row + 1]) >> 2);

            for(int x = 1; x < w - 1; x++) {
                const int idx = row + x;
                tmp[idx] = (uint16_t)((Y[idx - 1] + (Y[idx] << 1) + Y[idx + 1]) >> 2);
            }

            tmp[row + w - 1] = (uint16_t)((Y[row + w - 2] + (Y[row + w - 1] << 1) + Y[row + w - 1]) >> 2);
        }

#pragma omp for schedule(static)
        for(int y = 1; y < h - 1; y++) {
            const int row = y * w;
            const int up  = row - w;
            const int dn  = row + w;

            for(int x = 1; x < w - 1; x++) {
                const int idx = row + x;

                const int blur = (tmp[up + x] + (tmp[idx] << 1) + tmp[dn + x]) >> 2;

                int mask;
                if(blur <= tmin)
                    mask = 0;
                else if(blur >= tmax)
                    mask = 256;
                else
                    mask = ((blur - tmin) * 256) / t_diff;

                mask = (mask * opacity) >> 8;

                if(mask > 0) {
                    const int inv = 256 - mask;

                    Y[idx]  = (uint8_t)((inv * Y[idx]  + mask * Y2[idx]  + 128) >> 8);
                    Cb[idx] = (uint8_t)((inv * Cb[idx] + mask * Cb2[idx] + 128) >> 8);
                    Cr[idx] = (uint8_t)((inv * Cr[idx] + mask * Cr2[idx] + 128) >> 8);
                }
            }
        }
    }
}