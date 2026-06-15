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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "transform.h"

#define TRANSFORM_PARAMS 4

#define P_CUBICS       0
#define P_PHASE        1
#define P_DRIFT_SPEED  2
#define P_SIZE_DRIVE   3

typedef struct {
    uint8_t *region;
    uint8_t *buf[3];
    int *xmap;
    int *ymap;
    int n_threads;
    int max_size;
    int w;
    int h;

    float size_state;
    float phase_state;
    float size_drive_state;
    float drift_phase;
} transform_t;

static inline int transform_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int transform_wrapi(int v, int max)
{
    v %= max;

    if(v < 0)
        v += max;

    return v;
}





vj_effect *transform_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRANSFORM_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int max_size = height / 16;
    if(max_size < 1)
        max_size = 1;

    ve->defaults[P_CUBICS]      = transform_clampi(5, 1, max_size);
    ve->defaults[P_PHASE]       = 0;
    ve->defaults[P_DRIFT_SPEED] = 0;
    ve->defaults[P_SIZE_DRIVE]  = 0;

    ve->limits[0][P_CUBICS]      = 1;     ve->limits[1][P_CUBICS]      = max_size;
    ve->limits[0][P_PHASE]       = 0;     ve->limits[1][P_PHASE]       = 1000;
    ve->limits[0][P_DRIFT_SPEED] = -1000; ve->limits[1][P_DRIFT_SPEED] = 1000;
    ve->limits[0][P_SIZE_DRIVE]  = 0;     ve->limits[1][P_SIZE_DRIVE]  = 1000;

    ve->description = "Transform Cubics";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Cubics",
        "Phase",
        "Drift Speed",
        "Size Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 1,     max_size, 12, 46, 700, 2800, 0, 76,
        VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS,                          0,     1000,     12, 46, 900, 3200, 0, 62,
        VJ_BEAT_SIGNED_SPEED,   VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,  -1000, 1000,     12, 46, 900, 3200, 0, 66,
        VJ_BEAT_WINDOW_RADIUS,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,   1000,     16, 62, 700, 2800, 0, 94
    );
    return ve;
}

void *transform_malloc(int w, int h)
{
    transform_t *t = (transform_t*) vj_calloc(sizeof(transform_t));
    if(!t)
        return NULL;

    const int len = w * h;
    const size_t frame_bytes = (size_t)len * 3u;
    const size_t map_bytes = sizeof(int) * (size_t)(w + h);
    const size_t total = frame_bytes + map_bytes + 32u;

    t->region = (uint8_t*) vj_malloc(total);
    if(!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *p = (uint8_t*)(((uintptr_t)t->region + 15u) & ~(uintptr_t)15u);

    t->buf[0] = p;
    t->buf[1] = t->buf[0] + len;
    t->buf[2] = t->buf[1] + len;

    p += frame_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    t->xmap = (int*)p;
    t->ymap = t->xmap + w;

    t->max_size = h / 16;
    if(t->max_size < 1)
        t->max_size = 1;

    t->w = w;
    t->h = h;
    t->size_state = 0.0f;
    t->phase_state = 0.0f;
    t->size_drive_state = 0.0f;
    t->drift_phase = 0.0f;

    t->n_threads = vje_advise_num_threads(len);

    return (void*) t;
}

void transform_free(void *ptr)
{
    transform_t *t = (transform_t*) ptr;

    free(t->region);
    free(t);
}

static void transform_build_map(int *restrict map, int n, int size, int phase)
{
    size = transform_clampi(size, 1, n);
    phase = transform_wrapi(phase, size * 4096);

    if(size <= 1) {
        for(int i = 0; i < n; i++)
            map[i] = i;
        return;
    }

    const int hsize = size >> 1;

    for(int i = 0; i < n; i++) {
        const int q = i + phase;
        const int off = q % size;
        const int band = q / size;
        int v;

        if((band & 1) != 0)
            v = q - off + hsize - phase;
        else
            v = q + off - hsize - phase;

        map[i] = transform_clampi(v, 0, n - 1);
    }
}

void transform_apply(void *ptr, VJFrame *frame, int *args)
{
    transform_t *t = (transform_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int base_size_arg = args[P_CUBICS];
    const int phase_arg = args[P_PHASE];
    const int drift_arg = args[P_DRIFT_SPEED];
    const int size_drive_arg = args[P_SIZE_DRIVE];

    const int base_size = transform_clampi(base_size_arg, 1, t->max_size);

    if(t->size_state < 1.0f)
        t->size_state = (float)base_size;

    const float size_coef = 0.142f;
    const float phase_coef = 0.170f;

    t->size_drive_state += ((float)size_drive_arg - t->size_drive_state) * size_coef;
    t->phase_state += ((float)phase_arg - t->phase_state) * phase_coef;

    const float size_lane = transform_clampi((int)(t->size_drive_state + 0.5f), 0, 1000) * 0.001f;
    const float headroom = (float)(t->max_size - base_size);

    float size_target = (float)base_size;
    size_target += headroom * size_lane;

    if(size_target < 1.0f)
        size_target = 1.0f;
    else if(size_target > (float)t->max_size)
        size_target = (float)t->max_size;

    t->size_state += (size_target - t->size_state) * size_coef;

    int size = transform_clampi((int)(t->size_state + 0.5f), 1, t->max_size);

    const float drift_step = (float)drift_arg * 0.020f;

    t->drift_phase += drift_step;

    if(t->drift_phase > 1048576.0f)
        t->drift_phase -= 1048576.0f;
    else if(t->drift_phase < -1048576.0f)
        t->drift_phase += 1048576.0f;

    const int manual_phase = (int)((t->phase_state * (float)(size * 2)) * 0.001f + 0.5f);
    const int drift_phase = (int)t->drift_phase;
    const int phase_x = manual_phase + drift_phase;
    const int phase_y = ((manual_phase * 3) >> 1) - ((drift_phase * 5) >> 2);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = t->buf[0];
    uint8_t *restrict srcCb = t->buf[1];
    uint8_t *restrict srcCr = t->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

    transform_build_map(t->xmap, width, size, phase_x);
    transform_build_map(t->ymap, height, size, phase_y);

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int sy = t->ymap[y] * width;

        for(int x = 0; x < width; x++) {
            const int dst = row + x;
            const int src = sy + t->xmap[x];

            Y[dst]  = srcY[src];
            Cb[dst] = srcCb[src];
            Cr[dst] = srcCr[src];
        }
    }
}
