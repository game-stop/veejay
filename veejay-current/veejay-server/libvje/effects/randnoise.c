/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include "randnoise.h"

#define RANDNOISE_PARAMS 2

#define P_MIN 0
#define P_MAX 1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t randnoise_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint32_t randnoise_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return x;
}

vj_effect *randnoise_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RANDNOISE_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_MIN] = -255; ve->limits[1][P_MIN] = 255; ve->defaults[P_MIN] = -16;
    ve->limits[0][P_MAX] = -255; ve->limits[1][P_MAX] = 255; ve->defaults[P_MAX] = 16;

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
        VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, -132, -4, 16, 62, 700, 2800, 0, 84,
        VJ_BEAT_TURBULENCE,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                           4,   132, 16, 62, 700, 2800, 0, 84
    );

    return ve;
}


void randnoise_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    int minv = args[P_MIN];
    int maxv = args[P_MAX];

    if(maxv < minv) {
        const int t = minv;
        minv = maxv;
        maxv = t;
    }

    const int len = frame->len;
    const int range = maxv - minv;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];

    const uint32_t seed =
        ((uint32_t)Y[0] << 24) ^
        ((uint32_t)Y[len >> 1] << 12) ^
        (uint32_t)Y[len - 1] ^
        ((uint32_t)len * 2654435761U);

    const uint32_t span = (uint32_t)(range + 1);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        if(range == 0) {
            Y[i] = randnoise_u8((int)Y[i] + minv);
        }
        else {
            const uint32_t h = randnoise_hash32((uint32_t)i ^ seed);
            const int n = (int)(h % span) + minv;

            Y[i] = randnoise_u8((int)Y[i] + n);
        }
    }
}
