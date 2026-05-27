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
#include "rawman.h"

#define RAWMAN_ADDITIVE    0
#define RAWMAN_SUBTRACTIVE 1
#define RAWMAN_MULTIPLY    2
#define RAWMAN_DIVIDE      3
#define RAWMAN_LIGHTEN     4
#define RAWMAN_HARDLIGHT   5

vj_effect *rawman_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = RAWMAN_ADDITIVE;
    ve->defaults[1] = 15;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 5;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;

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
        ve->limits[1][0],
        0,
        "Additive",
        "Subtractive",
        "Multiply",
        "Divide",
        "Lighten",
        "Hardlight"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS,                    4,                  96,                 10, 38, 1000, 2600, 0,   60     /* Value */
    );

    (void) w;
    (void) h;

    return ve;
}

static inline int rawman_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t rawman_u8(int v)
{
    return (uint8_t) rawman_clampi(v, 0, 255);
}

void rawman_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0])
        return;

    int mode = rawman_clampi(args[0], 0, 5);
    int value = rawman_clampi(args[1], 1, 255);

    const int len = frame->len;
    if(len <= 0)
        return;

    uint8_t *restrict Y = frame->data[0];

    const int n_threads = vje_advise_num_threads(len);

    switch(mode) {
        case RAWMAN_SUBTRACTIVE:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                Y[i] = rawman_u8((int)Y[i] - value);
            }
            break;

        case RAWMAN_MULTIPLY:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                Y[i] = rawman_u8(((int)Y[i] * value + 127) / 128);
            }
            break;

        case RAWMAN_DIVIDE:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                Y[i] = rawman_u8(((int)Y[i] * 128 + (value >> 1)) / value);
            }
            break;

        case RAWMAN_LIGHTEN:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                Y[i] = ((int)Y[i] < value) ? (uint8_t)value : Y[i];
            }
            break;

        case RAWMAN_HARDLIGHT:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                const int y = Y[i];
                Y[i] = (y < value)
                    ? rawman_u8((y * value + 127) / 128)
                    : rawman_u8(255 - (((255 - y) * (255 - value) + 127) / 128));
            }
            break;

        case RAWMAN_ADDITIVE:
        default:
#pragma omp parallel for schedule(static) num_threads(n_threads)
            for(int i = 0; i < len; i++) {
                Y[i] = rawman_u8((int)Y[i] + value);
            }
            break;
    }
}