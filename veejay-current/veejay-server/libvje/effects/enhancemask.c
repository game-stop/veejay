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
#include "enhancemask.h"

typedef struct {
    int n_threads;
    uint8_t *buf;
} enhancemask_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *enhancemask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->limits[0] || !ve->limits[1] || !ve->defaults)
    {
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        if(ve->defaults) free(ve->defaults);
        free(ve);
        return NULL;
    }

    ve->limits[0][0] = 0; ve->limits[1][0] = 4096; ve->defaults[0] = 120;
    ve->limits[0][1] = 0; ve->limits[1][1] = 64;   ve->defaults[1] = 8;
    ve->limits[0][2] = 0; ve->limits[1][2] = 128;  ve->defaults[2] = 50;

    ve->description = "Sharpen";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Strength", "Grain Threshold", "Halo Clamp");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 80, 2200, 92, 100, 10, 520, 0, 10, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 2, 24, 68, 94, 100, 1000, 0, 1, 0, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 18, 120, 90, 100, 4, 520, 24, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *enhancemask_malloc(int w, int h)
{
    enhancemask_t *e = (enhancemask_t*) vj_calloc(sizeof(enhancemask_t));

    if(!e)
        return NULL;

    const int len = w * h;

    e->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len);

    if(!e->buf) {
        free(e);
        return NULL;
    }

    e->n_threads = vje_advise_num_threads(len);

    return e;
}

void enhancemask_free(void *ptr)
{
    enhancemask_t *e = (enhancemask_t*) ptr;

    free(e->buf);
    free(e);
}

void enhancemask_apply(void *ptr, VJFrame *frame, int *args)
{
    enhancemask_t *e = (enhancemask_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int amount = args[0];
    const int threshold = args[1];
    const int limit = args[2];

    if(amount <= 0 || limit <= 0)
        return;

    uint8_t *restrict dst = frame->data[0];
    uint8_t *restrict src = e->buf;

    veejay_memcpy(src, dst, len);

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for(int y = 1; y < height - 1; y++)
    {
        const uint8_t *restrict p_prev = src + (y - 1) * width;
        const uint8_t *restrict p_curr = src + y * width;
        const uint8_t *restrict p_next = src + (y + 1) * width;
        uint8_t *restrict p_out = dst + y * width;

#pragma omp simd
        for(int x = 1; x < width - 1; x++)
        {
            const int blur =
                (p_prev[x - 1] + (p_prev[x] << 1) + p_prev[x + 1] +
                (p_curr[x - 1] << 1) + (p_curr[x] << 2) + (p_curr[x + 1] << 1) +
                p_next[x - 1] + (p_next[x] << 1) + p_next[x + 1]) >> 4;
            const int detail = (int)p_curr[x] - blur;
            const int abs_d = (detail ^ (detail >> 31)) - (detail >> 31);
            const int active = -(abs_d >= threshold);
            int boost = ((detail & active) * amount) >> 7;

            boost = clampi(boost, -limit, limit);
            p_out[x] = (uint8_t)clampi((int)p_curr[x] + boost, 0, 255);
        }
    }
}
