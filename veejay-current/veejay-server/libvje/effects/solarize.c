
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
#include "solarize.h"

vj_effect *solarize_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 16;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;

    ve->defaults[1] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;

    ve->description = "Solarize (Sabattier)";
    ve->parallel = 0;
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params,
        "Threshold", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Desaturated",
        "Luma Only",
        "Color Sabattier"
    );

    return ve;
}

void solarize_apply1(void *ptr, VJFrame *frame, int *args)
{
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int T = args[0];

    const int softness = 32;
    const int inv_softness = 256 / softness;

    int n_threads = vje_advise_num_threads(len);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++) {

        int y = Y[i];
        int d = y - T;

        int t = d * inv_softness;

        if (t < -256) t = -256;
        if (t >  256) t =  256;

        int blend = (t + 256) >> 1;

        int inv = 255 - y;

        int out = ((256 - blend) * y + blend * inv) >> 8;

        out = ((out - 128) * 5 >> 2) + 128; // ~1.25x

        if (out < 0) out = 0;
        if (out > 255) out = 255;

        Y[i] = (uint8_t) out;

        int dist = d >= 0 ? d : -d;
        int sat = 256 - ((dist > softness ? softness : dist) * inv_softness);

        int cb = Cb[i] - 128;
        int cr = Cr[i] - 128;

        cb = (cb * sat) >> 8;
        cr = (cr * sat) >> 8;

        Cb[i] = (uint8_t)(cb + 128);
        Cr[i] = (uint8_t)(cr + 128);
    }
}

void solarize_apply_luma(void *ptr, VJFrame *frame, int *args)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];

    const int T = args[0];

    const int softness = 32;
    const int inv_softness = 256 / softness;

    int n_threads = vje_advise_num_threads(len);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++) {
        int y = Y[i];
        int d = y - T;

        int t = d * inv_softness;

        if (t < -256) t = -256;
        if (t >  256) t =  256;

        int blend = (t + 256) >> 1;
        int inv = 255 - y;

        int out = ((256 - blend) * y + blend * inv) >> 8;
        out = ((out - 128) * 5 >> 2) + 128;

        if (out < 0) out = 0;
        if (out > 255) out = 255;

        Y[i] = (uint8_t) out;
    }
}

void solarize_apply_color(void *ptr, VJFrame *frame, int *args)
{
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int T = args[0];

    const int softness = 32;
    const int inv_softness = 256 / softness;

    int n_threads = vje_advise_num_threads(len);

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++) {
        int y = Y[i];
        int d = y - T;

        int t = d * inv_softness;

        if (t < -256) t = -256;
        if (t >  256) t =  256;

        int blend = (t + 256) >> 1;
        int inv = 255 - y;

        int out = ((256 - blend) * y + blend * inv) >> 8;
        out = ((out - 128) * 5 >> 2) + 128;

        if (out < 0) out = 0;
        if (out > 255) out = 255;

        Y[i] = (uint8_t) out;

        int cb = Cb[i];
        int cr = Cr[i];

        int inv_cb = 255 - cb;
        int inv_cr = 255 - cr;

        int chroma_blend = blend >> 2;

        cb = ((256 - chroma_blend) * cb + chroma_blend * inv_cb) >> 8;
        cr = ((256 - chroma_blend) * cr + chroma_blend * inv_cr) >> 8;

        Cb[i] = (uint8_t)cb;
        Cr[i] = (uint8_t)cr;
    }
}

void solarize_apply(void *ptr, VJFrame *frame, int *args)
{
    int mode = args[1];

    switch(mode) {
        case 1:
            solarize_apply_luma(ptr, frame, args);
            break;
        case 2:
            solarize_apply_color(ptr, frame, args);
            break;
        default:
            solarize_apply1(ptr, frame, args);
            break;
    }
}