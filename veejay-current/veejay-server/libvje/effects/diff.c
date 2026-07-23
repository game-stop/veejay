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
#include "softblur.h"
#include "diff.h"

typedef struct {
    uint8_t *static_bg;
    uint32_t *dt_map;
    uint8_t *data;
    uint8_t *current;
    int n_threads;
} diff_t;

static inline int diff_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *diff_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 30;
    ve->limits[0][1] = 0; ve->limits[1][1] = 1;   ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 2;   ve->defaults[2] = 2;
    ve->limits[0][3] = 1; ve->limits[1][3] = 100; ve->defaults[3] = 5;

    ve->description = "Map B to A (subtract background mask)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 1;
    ve->static_bg = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Mode", "Show mask/image", "Thinning");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][2], 2, "Show Difference", "Show Distance Map", "Normal");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 160, 78, 100, 15, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 24, 62, 92, 0, 620, 0, 1, 120, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *diff_malloc(int width, int height)
{
    diff_t *d = (diff_t*) vj_calloc(sizeof(diff_t));

    if(!d)
        return NULL;

    const int len = width * height;

    if(len <= 0) {
        free(d);
        return NULL;
    }

    d->data = (uint8_t*) vj_calloc(sizeof(uint8_t) * (len + width));
    d->static_bg = (uint8_t*) vj_calloc(sizeof(uint8_t) * (len + (width * 2)));
    d->dt_map = (uint32_t*) vj_calloc(sizeof(uint32_t) * (len + (width * 2)));
    d->n_threads = vje_advise_num_threads(len);

    if(d->n_threads < 1)
        d->n_threads = 1;

    if(!d->data || !d->static_bg || !d->dt_map) {
        diff_free(d);
        return NULL;
    }

    return d;
}

void diff_free(void *ptr)
{
    diff_t *d = (diff_t*) ptr;

    if(!d)
        return;

    if(d->data)
        free(d->data);

    if(d->static_bg)
        free(d->static_bg);

    if(d->dt_map)
        free(d->dt_map);

    free(d);
}

int diff_prepare(void *ptr, VJFrame *frame)
{
    diff_t *d = (diff_t*) ptr;

    if(!d || !frame || !frame->data[0] || !d->static_bg)
        return 0;

    veejay_memcpy(d->static_bg, frame->data[0], frame->len);

    VJFrame tmp;
    veejay_memset(&tmp, 0, sizeof(VJFrame));

    tmp.data[0] = d->static_bg;
    tmp.len = frame->len;
    tmp.width = frame->width;
    tmp.height = frame->height;

    softblur_apply_internal(&tmp);

    veejay_msg(2, "Map B to A: Snapped background frame");

    return 1;
}

static inline void diff_binarify_mask(uint8_t *restrict dst,
                                      const uint8_t *restrict frameA,
                                      const uint8_t *restrict frameB,
                                      int threshold,
                                      int reverse,
                                      int len)
{
#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
    {
        int d = (int)frameA[i] - (int)frameB[i];

        d = (d ^ (d >> 31)) - (d >> 31);

        uint8_t mask = (uint8_t)(-(d > threshold));

        dst[i] = reverse ? (uint8_t)~mask : mask;
    }
}

void diff_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    diff_t *d = (diff_t*) ptr;

    const int threshold = args[0];
    const int reverse = args[1];
    const int mode = args[2];
    const int feather = args[3];
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    uint8_t *restrict data = d->data;
    uint32_t *restrict dt_map = d->dt_map;

    veejay_memset(dt_map, 0, len * sizeof(uint32_t));

    #pragma omp parallel num_threads(d->n_threads)
    {
        diff_binarify_mask(data, Y, Y2, threshold, reverse, len);

#pragma omp single
        veejay_distance_transform8(data, frame->width, frame->height, dt_map);

        if(mode == 1)
        {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                Y[i] = data[i];

#pragma omp single
            {
                veejay_memset(Cb, 128, uv_len);
                veejay_memset(Cr, 128, uv_len);
            }
        }
        else if(mode == 2)
        {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
            {
                const uint32_t dt = dt_map[i];
                uint8_t val = 0;

                if(dt > (uint32_t)feather)
                    val = (uint8_t)(128 + (dt & 127u));

                if(dt == (uint32_t)feather || dt == 1u)
                    val = 0xff;

                Y[i] = val;
            }

#pragma omp single
            {
                veejay_memset(Cb, 128, uv_len);
                veejay_memset(Cr, 128, uv_len);
            }
        }
        else
        {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
            {
                const uint32_t mask = -(dt_map[i] >= (uint32_t)feather);

                Y[i] = (uint8_t)((Y2[i] & mask) | (pixel_Y_lo_ & ~mask));
                Cb[i] = (uint8_t)((Cb2[i] & mask) | (128 & ~mask));
                Cr[i] = (uint8_t)((Cr2[i] & mask) | (128 & ~mask));
            }
        }
    }
}
