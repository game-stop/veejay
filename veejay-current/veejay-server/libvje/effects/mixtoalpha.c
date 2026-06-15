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
#include "mixtoalpha.h"

#define MIXTOALPHA_PARAMS 2

#define P_MODE  0
#define P_SCALE 1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t mixtoalpha_scale_full(uint8_t v)
{
    const int y = (int)v;

    if(y <= 16)
        return 0;

    if(y >= 235)
        return 255;

    return (uint8_t)(((y - 16) * 255 + 109) / 219);
}

vj_effect *mixtoalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MIXTOALPHA_PARAMS;
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

    ve->limits[0][P_MODE] = 0;  ve->limits[1][P_MODE] = 1;  ve->defaults[P_MODE] = 0;
    ve->limits[0][P_SCALE] = 0; ve->limits[1][P_SCALE] = 1; ve->defaults[P_SCALE] = !yuv_use_auto_ccir_jpeg();

    ve->description = "Alpha: Set from Mixing source";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Scale to full range");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Copy Luminance from B", "Copy Alpha from B");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_SCALE], P_SCALE, "Off", "On");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

    return ve;
}

void mixtoalpha_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void)ptr;

    const int mode = clampi(args[P_MODE], 0, 1);
    const int scale = clampi(args[P_SCALE], 0, 1);
    const int len = frame->len;

    uint8_t *restrict A = frame->data[3];
    const uint8_t *restrict src = mode == 0 ? frame2->data[0] : frame2->data[3];

    if(scale && frame->range == 0) {
        const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int i = 0; i < len; i++)
            A[i] = mixtoalpha_scale_full(src[i]);
    }
    else {
        veejay_memcpy(A, src, len);
    }
}
