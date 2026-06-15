/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "opacitythreshold.h"

#define OPACITYTHRESHOLD_PARAMS 3

#define P_OPACITY 0
#define P_MIN_T   1
#define P_MAX_T   2

typedef struct {
    uint16_t *hblur;
    int n_threads;
} op_thres_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t opacitythreshold_div255(int v)
{
    return (uint8_t)(((v + 128) + ((v + 128) >> 8)) >> 8);
}

vj_effect *opacitythreshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = OPACITYTHRESHOLD_PARAMS;
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

    ve->limits[0][P_OPACITY] = 0; ve->limits[1][P_OPACITY] = 255; ve->defaults[P_OPACITY] = 180;
    ve->limits[0][P_MIN_T] = 0;   ve->limits[1][P_MIN_T] = 255;   ve->defaults[P_MIN_T] = 50;
    ve->limits[0][P_MAX_T] = 0;   ve->limits[1][P_MAX_T] = 255;   ve->defaults[P_MAX_T] = 255;

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
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     18,                 245,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_DETAIL,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,                  118,                12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_CONTRAST,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 118,                245,                12, 46, 1000, 3600, 0,    68
    );

    return ve;
}

void opacitythreshold_free(void *ptr)
{
    op_thres_t *opt = (op_thres_t*) ptr;

    free(opt->hblur);
    free(opt);
}

void *opacitythreshold_malloc(int w, int h)
{
    op_thres_t *opt = (op_thres_t*) vj_calloc(sizeof(op_thres_t));

    if(!opt)
        return NULL;

    opt->hblur = (uint16_t*) vj_calloc(sizeof(uint16_t) * (size_t)w * (size_t)h);

    if(!opt->hblur) {
        free(opt);
        return NULL;
    }

    opt->n_threads = vje_advise_num_threads(w * h);

    return (void*) opt;
}

void opacitythreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    op_thres_t *opt = (op_thres_t*) ptr;

    const int opacity = args[P_OPACITY];
    int tmin = args[P_MIN_T];
    int tmax = args[P_MAX_T];

    if(tmax < tmin) {
        const int tmp_t = tmin;
        tmin = tmax;
        tmax = tmp_t;
    }

    if(opacity <= 0)
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int t_diff = tmax > tmin ? tmax - tmin : 1;
    const int n_threads = opt->n_threads;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    uint16_t *restrict tmp = opt->hblur;

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for(int y = 0; y < h; y++) {
            const int row = y * w;
            const int last = row + w - 1;

            tmp[row] = (uint16_t)((Y[row] + (Y[row] << 1) + Y[row + 1]) >> 2);

            for(int x = 1; x < w - 1; x++) {
                const int idx = row + x;

                tmp[idx] = (uint16_t)((Y[idx - 1] + (Y[idx] << 1) + Y[idx + 1]) >> 2);
            }

            tmp[last] = (uint16_t)((Y[last - 1] + (Y[last] << 1) + Y[last]) >> 2);
        }

#pragma omp for schedule(static)
        for(int y = 1; y < h - 1; y++) {
            const int row = y * w;
            const int up = row - w;
            const int dn = row + w;

            for(int x = 1; x < w - 1; x++) {
                const int idx = row + x;
                const int blur = (tmp[up + x] + (tmp[idx] << 1) + tmp[dn + x]) >> 2;
                int mask;

                if(blur <= tmin)
                    mask = 0;
                else if(blur >= tmax)
                    mask = 255;
                else
                    mask = ((blur - tmin) * 255 + (t_diff >> 1)) / t_diff;

                const int w2 = opacitythreshold_div255(mask * opacity);

                if(w2 > 0) {
                    const int w1 = 255 - w2;

                    Y[idx] = opacitythreshold_div255(w1 * Y[idx] + w2 * Y2[idx]);
                    Cb[idx] = opacitythreshold_div255(w1 * Cb[idx] + w2 * Cb2[idx]);
                    Cr[idx] = opacitythreshold_div255(w1 * Cr[idx] + w2 * Cr2[idx]);
                }
            }
        }
    }
}
