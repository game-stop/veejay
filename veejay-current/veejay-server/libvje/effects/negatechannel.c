/* 
 * Linux VeeJay
 *
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
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
#include "negatechannel.h"

#define NEGATECHANNEL_PARAMS 2

#define P_MODE  0
#define P_VALUE 1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *negatechannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NEGATECHANNEL_PARAMS;
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

    ve->limits[0][P_MODE] = 0;  ve->limits[1][P_MODE] = 3;   ve->defaults[P_MODE] = 0;
    ve->limits[0][P_VALUE] = 0; ve->limits[1][P_VALUE] = 255; ve->defaults[P_VALUE] = 255;

    ve->description = "Negate a channel";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Value");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Luminance", "Chroma Blue", "Chroma Red", "Chroma Red and Blue");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,           145,                255,                14, 54,  800, 3000, 0,    78
    );

    return ve;
}

static void negatechannel_plane(uint8_t *restrict p, int len, int val)
{
#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        p[i] = (uint8_t)(val - p[i]);
}

static void negatechannel_uv_planes(uint8_t *restrict cb,
                                    uint8_t *restrict cr,
                                    int len,
                                    int val)
{
#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        cb[i] = (uint8_t)(val - cb[i]);
        cr[i] = (uint8_t)(val - cr[i]);
    }
}

void negatechannel_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int mode = args[P_MODE];
    const int val = args[P_VALUE];
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel num_threads(n_threads)
    {
        switch(mode) {
            case 0:
                negatechannel_plane(frame->data[0], len, val);
                break;

            case 1:
                negatechannel_plane(frame->data[1], uv_len, val);
                break;

            case 2:
                negatechannel_plane(frame->data[2], uv_len, val);
                break;

            case 3:
                negatechannel_uv_planes(frame->data[1], frame->data[2], uv_len, val);
                break;
        }
    }
}
