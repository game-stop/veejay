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
#include "posterize.h"

#define POSTERIZE_PARAMS 3

#define P_LEVELS 0
#define P_TMIN   1
#define P_TMAX   2

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *posterize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = POSTERIZE_PARAMS;
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

    ve->defaults[P_LEVELS] = 4;
    ve->defaults[P_TMIN] = 16;
    ve->defaults[P_TMAX] = 235;

    ve->limits[0][P_LEVELS] = 1; ve->limits[1][P_LEVELS] = 256;
    ve->limits[0][P_TMIN] = 0;   ve->limits[1][P_TMIN] = 256;
    ve->limits[0][P_TMAX] = 0;   ve->limits[1][P_TMAX] = 256;

    ve->description = "Posterize (Threshold Range)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Posterize",
        "Min Threshold",
        "Max Threshold"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 3,   42,  4,  14, 3000, 8200, 2200, 24,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,                         8,   118, 12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                               132, 248, 12, 46, 1000, 3600, 0,    68
    );

    return ve;	
}

static inline void posterize_build_lut(uint8_t *restrict lut, int levels, int threshold_min, int threshold_max)
{
    const int factor = 256 / levels;

    for(int i = 0; i < 256; i++) {
        const int q = (i / factor) * factor;

        if(q < threshold_min)
            lut[i] = pixel_Y_lo_;
        else if(q > threshold_max)
            lut[i] = pixel_Y_hi_;
        else
            lut[i] = (uint8_t)q;
    }
}

void posterize_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    int levels = args[P_LEVELS];
    int tmin = args[P_TMIN];
    int tmax = args[P_TMAX];

    if(tmax < tmin) {
        const int tmp = tmin;
        tmin = tmax;
        tmax = tmp;
    }

    uint8_t lut[256];

    posterize_build_lut(lut, levels, tmin, tmax);

    uint8_t *restrict Y = frame->data[0];
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = lut[Y[i]];
}
