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
#include "rawman.h"

#define RAWMAN_PARAMS 2

#define P_MODE  0
#define P_VALUE 1

#define RAWMAN_ADDITIVE    0
#define RAWMAN_SUBTRACTIVE 1
#define RAWMAN_MULTIPLY    2
#define RAWMAN_DIVIDE      3
#define RAWMAN_LIGHTEN     4
#define RAWMAN_HARDLIGHT   5

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t rawman_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

vj_effect *rawman_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RAWMAN_PARAMS;
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

    ve->defaults[P_MODE] = RAWMAN_ADDITIVE;
    ve->defaults[P_VALUE] = 15;

    ve->limits[0][P_MODE] = 0;  ve->limits[1][P_MODE] = 5;
    ve->limits[0][P_VALUE] = 1; ve->limits[1][P_VALUE] = 255;

    ve->sub_format = -1;
    ve->description = "Raw Data Manipulation";
    ve->has_user = 0;
    ve->extra_frame = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Value"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Additive",
        "Subtractive",
        "Multiply",
        "Divide",
        "Lighten",
        "Hardlight"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 10, 220, 90, 100, 6, 480, 24, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static inline void rawman_build_lut(uint8_t *restrict lut, int mode, int value)
{
    switch(mode) {
        case RAWMAN_SUBTRACTIVE:
            for(int i = 0; i < 256; i++)
                lut[i] = rawman_u8(i - value);
            break;

        case RAWMAN_MULTIPLY:
            for(int i = 0; i < 256; i++)
                lut[i] = rawman_u8(((i * value) + 127) >> 7);
            break;

        case RAWMAN_DIVIDE:
            for(int i = 0; i < 256; i++)
                lut[i] = rawman_u8(((i << 7) + (value >> 1)) / value);
            break;

        case RAWMAN_LIGHTEN:
            for(int i = 0; i < 256; i++)
                lut[i] = (uint8_t)(i < value ? value : i);
            break;

        case RAWMAN_HARDLIGHT:
            for(int i = 0; i < 256; i++) {
                lut[i] = i < value
                    ? rawman_u8(((i * value) + 127) >> 7)
                    : rawman_u8(255 - ((((255 - i) * (255 - value)) + 127) >> 7));
            }
            break;

        case RAWMAN_ADDITIVE:
        default:
            for(int i = 0; i < 256; i++)
                lut[i] = rawman_u8(i + value);
            break;
    }
}

void rawman_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int mode = args[P_MODE];
    const int value = args[P_VALUE];
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t lut[256];

    rawman_build_lut(lut, mode, value);

    uint8_t *restrict Y = frame->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = lut[Y[i]];
}
