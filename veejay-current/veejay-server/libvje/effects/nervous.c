/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include "nervous.h"

#define N_MAX 100

#define NERVOUS_PARAMS 1

#define P_BUFFER_LENGTH 0

typedef struct {
    uint8_t *nervous_buf[3];
    int write_pos;
    int filled;
    uint32_t seed;
} nervous_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t nervous_next_u32(nervous_t *n)
{
    n->seed = n->seed * 1664525u + 1013904223u;
    return n->seed;
}

static inline int nervous_rand_bounded(nervous_t *n, int max)
{
    return (int)(((uint64_t)nervous_next_u32(n) * (uint64_t)max) >> 32);
}

vj_effect *nervous_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NERVOUS_PARAMS;
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

    ve->limits[0][P_BUFFER_LENGTH] = 1;
    ve->limits[1][P_BUFFER_LENGTH] = N_MAX;
    ve->defaults[P_BUFFER_LENGTH] = N_MAX;

    ve->description = "Nervous";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Buffer length");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MEMORY, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 4, 96, 4, 14, 3200, 8600, 2400, 22
    );

    return ve;
}

void nervous_free(void *ptr)
{
    nervous_t *n = (nervous_t*) ptr;

    free(n->nervous_buf[0]);
    free(n);
}

void *nervous_malloc(int w, int h)
{
    nervous_t *n = (nervous_t*) vj_calloc(sizeof(nervous_t));

    if(!n)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;
    const size_t plane_len = len * (size_t)N_MAX;

    n->nervous_buf[0] = (uint8_t*) vj_malloc(plane_len * 3u);

    if(!n->nervous_buf[0]) {
        free(n);
        return NULL;
    }

    n->nervous_buf[1] = n->nervous_buf[0] + plane_len;
    n->nervous_buf[2] = n->nervous_buf[1] + plane_len;

    n->write_pos = 0;
    n->filled = 0;
    n->seed = 0x5eedeed5u ^ (uint32_t)w ^ ((uint32_t)h << 16);

    veejay_memset(n->nervous_buf[0], pixel_Y_lo_, plane_len);
    veejay_memset(n->nervous_buf[1], 128, plane_len);
    veejay_memset(n->nervous_buf[2], 128, plane_len);

    return (void*) n;
}

void nervous_apply(void *ptr, VJFrame *frame, int *args)
{
    nervous_t *n = (nervous_t*) ptr;

    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int length = clampi(args[P_BUFFER_LENGTH], 1, N_MAX);

    if(n->write_pos >= length)
        n->write_pos = 0;

    if(n->filled > length)
        n->filled = length;

    const int slot = n->write_pos;
    const size_t offset = (size_t)len * (size_t)slot;

    uint8_t *dstY = n->nervous_buf[0] + offset;
    uint8_t *dstCb = n->nervous_buf[1] + offset;
    uint8_t *dstCr = n->nervous_buf[2] + offset;

    veejay_memcpy(dstY, frame->data[0], len);
    veejay_memcpy(dstCb, frame->data[1], uv_len);
    veejay_memcpy(dstCr, frame->data[2], uv_len);

    const int max_age = n->filled < length ? n->filled : length - 1;

    if(max_age > 0) {
        int src_slot = slot - 1 - nervous_rand_bounded(n, max_age);

        if(src_slot < 0)
            src_slot += length;

        const size_t src_offset = (size_t)len * (size_t)src_slot;
        const uint8_t *srcY = n->nervous_buf[0] + src_offset;
        const uint8_t *srcCb = n->nervous_buf[1] + src_offset;
        const uint8_t *srcCr = n->nervous_buf[2] + src_offset;

        veejay_memcpy(frame->data[0], srcY, len);
        veejay_memcpy(frame->data[1], srcCb, uv_len);
        veejay_memcpy(frame->data[2], srcCr, uv_len);
    }

    n->write_pos = slot + 1;

    if(n->write_pos >= length)
        n->write_pos = 0;

    if(n->filled < length)
        n->filled++;
}
