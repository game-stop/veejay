/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "whiteframe.h"

typedef struct {
  int n_threads;
} whiteframe_t;

vj_effect *whiteframe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;    ve->limits[1][0] = 255; ve->defaults[0] = 220;
    ve->limits[0][1] = 1;    ve->limits[1][1] = 128; ve->defaults[1] = 24;
    ve->description = "Replace White";
    ve->sub_format  = 1;
    ve->extra_frame = 1;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Softness"
    );

    return ve;
}

void *whiteframe_malloc(int w, int h) {
    whiteframe_t *wf = (whiteframe_t*) vj_calloc(sizeof(whiteframe_t));
    if(!wf)
        return NULL;
    wf->n_threads = vje_advise_num_threads(w*h);
    return (void*) wf;
}

void whiteframe_free(void *ptr) {
    whiteframe_t *wf = (whiteframe_t*) ptr;
    if(wf) {
        free(wf);
    }
}

static inline uint8_t blend_u8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((a * (255 - t) + b * t) >> 8);
}

void whiteframe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    whiteframe_t *wf = (whiteframe_t*) ptr;
    const int threshold = args[0];
    const int softness  = args[1];
    const int len       = frame->len;
    const int n_threads = wf->n_threads;

    const int full = threshold - softness;
    const int edge = threshold + softness;
    const int denom = edge - full;

    const int max_chroma = 128;
    float k = (float)full / max_chroma;
    if(k < 0.5f) k = 0.5f;
    if(k > 2.5f) k = 2.5f;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        int y  = Y[i];
        int cb = Cb[i];
        int cr = Cr[i];

        int cbd = cb - 128;
        int crd = cr - 128;
        int abs_cb = (cbd ^ (cbd >> 31)) - (cbd >> 31);
        int abs_cr = (crd ^ (crd >> 31)) - (crd >> 31);

        int k = 2;
        int light = y - (int)(k * (abs_cb + abs_cr));

        int t = ((light - full) * 255) / denom;
        if(t < 0) t = 0;
        if(t > 255) t = 255;

        Y[i]  = blend_u8(Y[i],  Y2[i], t);
        Cb[i] = blend_u8(Cb[i], Cb2[i], t);
        Cr[i] = blend_u8(Cr[i], Cr2[i], t);
    }
}