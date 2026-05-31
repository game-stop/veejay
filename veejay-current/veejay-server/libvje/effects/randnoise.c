/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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

/*
 * Add pseudo random noise to image
 */

#include "common.h"
#include "randnoise.h"

vj_effect *randnoise_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = -255;
    ve->limits[1][0] = 255;
    ve->defaults[0] = -16;

    ve->limits[0][1] = -255;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 16;

    ve->description = "Randnoise";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Min",
        "Max"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS, -96,  -4,  10, 38, 1000, 2600, 0, 60, /* Min */
        VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS,   4, 128,  10, 38, 1000, 2600, 0, 60  /* Max */
    );

    (void) w;
    (void) h;

    return ve;
}

static inline int randnoise_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t randnoise_u8(int v)
{
    return (uint8_t)randnoise_clampi(v, 0, 255);
}

static inline unsigned int randnoise_hash(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void randnoise_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0])
        return;

    int minv = randnoise_clampi(args[0], -255, 255);
    int maxv = randnoise_clampi(args[1], -255, 255);

    if(maxv < minv) {
        int t = minv;
        minv = maxv;
        maxv = t;
    }

    const int len = frame->len;
    if(len <= 0)
        return;

    const int range = maxv - minv;

    if(range == 0) {
        uint8_t *restrict Y = frame->data[0];
        const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int i = 0; i < len; i++) {
            Y[i] = randnoise_u8((int)Y[i] + minv);
        }

        return;
    }

    uint8_t *restrict Y = frame->data[0];

    const int n_threads = vje_advise_num_threads(len);
    const unsigned int seed =
        ((unsigned int)Y[0] << 24) ^
        ((unsigned int)Y[len >> 1] << 12) ^
        ((unsigned int)Y[len - 1]) ^
        ((unsigned int)len * 2654435761U);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        unsigned int h = randnoise_hash((unsigned int)i ^ seed);
        int rv = (int)(h % (unsigned int)(range + 1)) + minv;

        Y[i] = randnoise_u8((int)Y[i] + rv);
    }
}