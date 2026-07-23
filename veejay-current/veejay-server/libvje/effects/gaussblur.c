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

#include "common.h"
#include <libswscale/swscale.h>
#include "gaussblur.h"

extern int yuv_sws_get_cpu_flags();

typedef struct {
    float radius;
    float strength;
    float quality;
    struct SwsContext *filter_context;
} FilterParam;

typedef struct {
    uint8_t *temp;
    FilterParam gaussfilter;
    int last_radius;
    int last_strength;
    int last_quality;
} gaussblur_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *gaussblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 3;
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

    ve->defaults[0] = 100;
    ve->defaults[1] = 100;
    ve->defaults[2] = 300;

    ve->limits[0][0] = 1;    ve->limits[1][0] = 500;
    ve->limits[0][1] = -100; ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 300;

    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Strength", "Quality");

    ve->has_user = 0;
    ve->description = "Alpha: Choke Matte";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->rgb_conv = 0;
    ve->parallel = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 220, 68, 92, 20, 720, 0, 2, 900, VJ_BEAT_COST_MODERATE, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -60, 90, 54, 82, 10, 720, 0, 2, 1000, VJ_BEAT_COST_EXPENSIVE, 52, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static int alloc_sws_context(FilterParam *f, int width, int height, unsigned int flags)
{
    SwsVector *vec = sws_getGaussianVec(f->radius, f->quality);

    if(!vec)
        return 0;

    sws_scaleVec(vec, f->strength);

    vec->coeff[vec->length >> 1] += 1.0 - f->strength;

    SwsFilter sws_filter;
    sws_filter.lumH = vec;
    sws_filter.lumV = vec;
    sws_filter.chrH = NULL;
    sws_filter.chrV = NULL;

    f->filter_context = sws_getCachedContext(NULL,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             flags, &sws_filter, NULL, NULL);

    sws_freeVec(vec);

    return f->filter_context ? 1 : 0;
}

void *gaussblur_malloc(int w, int h)
{
    gaussblur_t *g = (gaussblur_t*) vj_calloc(sizeof(gaussblur_t));

    if(!g)
        return NULL;

    g->temp = (uint8_t*) vj_malloc((size_t)w * (size_t)h);

    if(!g->temp) {
        free(g);
        return NULL;
    }

    g->last_radius = -1;
    g->last_strength = 1000000;
    g->last_quality = -1;

    return (void*) g;
}

void gaussblur_free(void *ptr)
{
    gaussblur_t *g = (gaussblur_t*) ptr;

    if(g->gaussfilter.filter_context)
        sws_freeContext(g->gaussfilter.filter_context);

    free(g->temp);
    free(g);
}

static int gaussfilter_init(gaussblur_t *g, int w, int h, int radius, int strength, int quality)
{
    FilterParam *f = &g->gaussfilter;

    f->radius = (float)radius * 0.01f;
    f->strength = (float)strength * 0.01f;
    f->quality = (float)quality * 0.01f;

    if(f->quality <= 0.0f)
        f->quality = 0.01f;

    return alloc_sws_context(f, w, h, yuv_sws_get_cpu_flags());
}

static void gaussblur_scale(uint8_t *dst,
                            int dst_linesize,
                            const uint8_t *src,
                            int src_linesize,
                            int w,
                            int h,
                            struct SwsContext *filter_context)
{
    const uint8_t *const src_array[4] = { src, NULL, NULL, NULL };
    uint8_t *dst_array[4] = { dst, NULL, NULL, NULL };
    int src_linesize_array[4] = { src_linesize, 0, 0, 0 };
    int dst_linesize_array[4] = { dst_linesize, 0, 0, 0 };

    sws_scale(filter_context, src_array, src_linesize_array, 0, h, dst_array, dst_linesize_array);
}

void gaussblur_apply(void *ptr, VJFrame *frame, int *args)
{
    gaussblur_t *g = (gaussblur_t*) ptr;

    const int radius = args[0];
    const int strength = args[1];
    const int quality = args[2];

    uint8_t *A = frame->data[3];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(g->last_radius != radius || g->last_strength != strength || g->last_quality != quality) {
        if(g->gaussfilter.filter_context) {
            sws_freeContext(g->gaussfilter.filter_context);
            g->gaussfilter.filter_context = NULL;
        }

        if(gaussfilter_init(g, width, height, radius, strength, quality) == 0)
            return;

        g->last_radius = radius;
        g->last_strength = strength;
        g->last_quality = quality;
    }

    veejay_memcpy(g->temp, A, len);
    gaussblur_scale(A, width, g->temp, width, width, height, g->gaussfilter.filter_context);
}
