/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "levelcorrection.h"

#define LEVELCORRECTION_PARAMS 4

#define P_LEVEL_MIN  0
#define P_LEVEL_MAX  1
#define P_SHRINK_MIN 2
#define P_SHRINK_MAX 3

typedef struct {
    int n_threads;
} level_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *levelcorrection_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LEVELCORRECTION_PARAMS;
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

    ve->defaults[P_LEVEL_MIN] = 0;
    ve->defaults[P_LEVEL_MAX] = 255;
    ve->defaults[P_SHRINK_MIN] = 0;
    ve->defaults[P_SHRINK_MAX] = 255;

    for(int i = 0; i < ve->num_params; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Level Min",
        "Level Max",
        "Shrink Min",
        "Shrink Max"
    );

    ve->has_user = 0;
    ve->description = "Alpha: Level Correction";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->rgb_conv = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 80, 60, 90, 30, 800, 0, 1, 0, VJ_BEAT_COST_CHEAP, 68, 1, 0, VJ_BEAT_GROUP_ASCENDING, 8),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 175, 255, 64, 92, 30, 800, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 1, 1, VJ_BEAT_GROUP_ASCENDING, 8),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 96, 70, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 2, 0, VJ_BEAT_GROUP_ASCENDING, 8),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 160, 255, 72, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 2, 1, VJ_BEAT_GROUP_ASCENDING, 8)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *levelcorrection_malloc(int w, int h)
{
    level_t *t = (level_t*) vj_malloc(sizeof(level_t));

    if(!t)
        return NULL;

    t->n_threads = vje_advise_num_threads(w * h);

    return (void*) t;
}

void levelcorrection_free(void *ptr)
{
    free(ptr);
}

void levelcorrection_apply(void *ptr, VJFrame *frame, int *args)
{
    level_t *t = (level_t*) ptr;

    const int min = args[P_LEVEL_MIN];
    const int max = args[P_LEVEL_MAX];
    const int bmin = args[P_SHRINK_MIN];
    const int bmax = args[P_SHRINK_MAX];
    const int apply_levels = max > min;
    const int apply_shrink = bmax > bmin;

    if(!apply_levels && !apply_shrink)
        return;

    uint8_t lut[256];

    if(apply_levels && apply_shrink) {
        uint8_t lut1[256];
        uint8_t lut2[256];

        __init_lookup_table(lut1, 256, (float)min, (float)max, 0, 0xff);
        __init_lookup_table(lut2, 256, 0.0f, 255.0f, bmin, bmax);

#pragma GCC ivdep
        for(int i = 0; i < 256; i++)
            lut[i] = lut2[lut1[i]];
    }
    else if(apply_levels) {
        __init_lookup_table(lut, 256, (float)min, (float)max, 0, 0xff);
    }
    else {
        __init_lookup_table(lut, 256, 0.0f, 255.0f, bmin, bmax);
    }

    uint8_t *restrict A = frame->data[3];
    const int len = frame->len;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int pos = 0; pos < len; pos++)
        A[pos] = lut[A[pos]];
}
