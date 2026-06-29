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
#include "mirrors.h"
#include "motionmap.h"

#define MIRRORS_PARAMS 2

#define P_MODE   0
#define P_NUMBER 1

typedef struct {
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} mirrors_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int mirrors_max_factor_for_axis(int span)
{
    int max_factor = (span >> 1) - 1;

    if(max_factor < 0)
        max_factor = 0;

    return max_factor;
}

vj_effect *mirrors_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MIRRORS_PARAMS;
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

    int max_factor = mirrors_max_factor_for_axis(width < height ? width : height);
    int beat_hi = max_factor < 16 ? max_factor : 16;

    if(beat_hi < 1)
        beat_hi = 1;

    ve->defaults[P_MODE] = 1;
    ve->defaults[P_NUMBER] = 1;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 3;
    ve->limits[0][P_NUMBER] = 0;
    ve->limits[1][P_NUMBER] = max_factor;

    ve->sub_format = 1;
    ve->description = "Multi Mirrors";
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "H or V", "Number");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Right to Left", "Left to Right", "Bottom to Top", "Top to Bottom");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  beat_hi,            4,  14, 3000, 8200, 2200, 22
    );
    return ve;
}

static void mirrors_vertical(uint8_t *yuv[3], int width, int height, int factor, int swap)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];
    const int tiles = factor + 1;
    const int base_w = width / tiles;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        uint8_t *restrict ry = py + y * width;
        uint8_t *restrict ru = pu + y * width;
        uint8_t *restrict rv = pv + y * width;

        for(int t = 0; t < tiles; t++) {
            const int tile_off = t * base_w;
            const int tile_end = t == tiles - 1 ? width : tile_off + base_w;
            const int tile_w = tile_end - tile_off;
            const int half_tile = tile_w >> 1;

            for(int x = 0; x < half_tile; x++) {
                const int src_x = swap ? (tile_w - 1 - x) : x;
                const int dst_x = swap ? x : (tile_w - 1 - x);
                const int src = tile_off + src_x;
                const int dst = tile_off + dst_x;

                ry[dst] = ry[src];
                ru[dst] = ru[src];
                rv[dst] = rv[src];
            }
        }
    }
}

static void mirrors_horizontal(uint8_t *yuv[3], int width, int height, int factor, int swap)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];
    const int tiles = factor + 1;
    const int base_h = height / tiles;

#pragma omp for schedule(static)
    for(int t = 0; t < tiles; t++) {
        const int tile_start = t * base_h;
        const int tile_end = t == tiles - 1 ? height : tile_start + base_h;
        const int tile_h = tile_end - tile_start;
        const int half_tile = tile_h >> 1;

        for(int y = 0; y < half_tile; y++) {
            const int src_y_local = swap ? (tile_h - 1 - y) : y;
            const int dst_y_local = swap ? y : (tile_h - 1 - y);

            const uint8_t *restrict sY = py + (tile_start + src_y_local) * width;
            const uint8_t *restrict sU = pu + (tile_start + src_y_local) * width;
            const uint8_t *restrict sV = pv + (tile_start + src_y_local) * width;

            uint8_t *restrict dY = py + (tile_start + dst_y_local) * width;
            uint8_t *restrict dU = pu + (tile_start + dst_y_local) * width;
            uint8_t *restrict dV = pv + (tile_start + dst_y_local) * width;

            veejay_memcpy(dY, sY, width);
            veejay_memcpy(dU, sU, width);
            veejay_memcpy(dV, sV, width);
        }
    }
}

void *mirrors_malloc(int w, int h)
{
    mirrors_t *m = (mirrors_t*) vj_calloc(sizeof(mirrors_t));

    if(!m)
        return NULL;

    m->n_threads = vje_advise_num_threads(w * h);

    return m;
}

void mirrors_free(void *ptr)
{
    free(ptr);
}

int mirrors_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void mirrors_set_motionmap(void *ptr, void *priv)
{
    mirrors_t *m = (mirrors_t*) ptr;

    m->motionmap = priv;
}

void mirrors_apply(void *ptr, VJFrame *frame, int *args)
{
    mirrors_t *m = (mirrors_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int type = args[P_MODE];
    const int span = type < 2 ? width : height;
    const int max_factor = mirrors_max_factor_for_axis(span);
    int factor = clampi(args[P_NUMBER], 0, max_factor);
    int interpolate = 0;
    int motion = 0;

    if(motionmap_active(m->motionmap)) {
        int tmp1 = 0;
        int tmp2 = factor;

        motionmap_scale_to(m->motionmap, max_factor, max_factor, 0, 0, &tmp1, &tmp2, &(m->n__), &(m->N__));
        factor = clampi(tmp2, 0, max_factor);
        motion = 1;

        if(m->N__ != m->n__ && m->n__ != 0)
            interpolate = 1;
    }
    else {
        m->n__ = 0;
        m->N__ = 0;
    }

#pragma omp parallel num_threads(m->n_threads)
    {
        switch(type) {
            case 0:
                mirrors_vertical(frame->data, width, height, factor, 0);
                break;
            case 1:
                mirrors_vertical(frame->data, width, height, factor, 1);
                break;
            case 2:
                mirrors_horizontal(frame->data, width, height, factor, 0);
                break;
            case 3:
                mirrors_horizontal(frame->data, width, height, factor, 1);
                break;
        }
    }

    if(interpolate)
        motionmap_interpolate_frame(m->motionmap, frame, m->N__, m->n__);

    if(motion)
        motionmap_store_frame(m->motionmap, frame);
}
