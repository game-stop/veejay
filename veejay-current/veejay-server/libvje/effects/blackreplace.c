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
#include "blackreplace.h"

vj_effect *blackreplace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 512;
    ve->defaults[0]  = 120;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 128;
    ve->defaults[1]  = 24;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2]  = 150;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3]  = 120;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->defaults[4]  = 250;

    ve->description = "Replace Black with Color (Darkness Key)";
    ve->sub_format  = 1;
    ve->extra_frame = 0;
    ve->parallel    = 0;
    ve->has_user    = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Softness",
        "Red",
        "Green",
        "Blue"
    );

    return ve;
}

typedef struct {
  int n_threads;
} blackreplace_t;

void *blackreplace_malloc(int w, int h) {
    blackreplace_t *br = (blackreplace_t*) vj_calloc(sizeof(blackreplace_t));
    if(!br)
        return NULL;
    br->n_threads = vje_advise_num_threads(w*h);
    return (void*) br;
}

void blackreplace_free(void *ptr ) {
    blackreplace_t *br = (blackreplace_t*) ptr;
    if(br) {
        free(br);
    }
}

static inline uint8_t blend_u8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((a * (255 - t) + b * t) >> 8);
}

void blackreplace_apply(void *ptr, VJFrame *frame, int *args)
{
    blackreplace_t *br = (blackreplace_t*) ptr;
    const int threshold = args[0];
    const int softness  = args[1];
    const int red   = args[2];
    const int green = args[3];
    const int blue  = args[4];
    const int len = frame->len;
    const int n_threads = br->n_threads;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    int colorY = 0, colorCb = 128, colorCr = 128;
    _rgb2yuv(red, green, blue, colorY, colorCb, colorCr);

    const int full = threshold - softness;
    const int edge = threshold + softness;
    const int denom = edge - full;
    const int mul = (255 << 16) / denom;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; ++i) {
        int y  = Y[i];
        int cb = Cb[i];
        int cr = Cr[i];

        int cbd = cb - 128;
        int crd = cr - 128;
        int abs_cb = (cbd ^ (cbd >> 31)) - (cbd >> 31);
        int abs_cr = (crd ^ (crd >> 31)) - (crd >> 31);

        int dark = y + abs_cb + abs_cr;
        int diff = edge - dark;
        int raw_t = (int)(((int64_t)diff * (int64_t)mul) >> 16);
        raw_t = raw_t & ~(raw_t >> 31);

        
        int tmp = 255 - raw_t;
        int gt = tmp >> 31;
        
        raw_t = raw_t + (gt & (255 - raw_t));
        
        int mask_edge = (dark - edge) >> 31;
        int mask_full = (dark - full) >> 31;
        int chosen_t = (mask_full & 255) | (~mask_full & raw_t);
        int t = mask_edge & chosen_t;

        Y[i]  = blend_u8((uint8_t)Y[i],  (uint8_t)colorY,  t);
        Cb[i] = blend_u8((uint8_t)Cb[i], (uint8_t)colorCb, t);
        Cr[i] = blend_u8((uint8_t)Cr[i], (uint8_t)colorCr, t);
    }
}