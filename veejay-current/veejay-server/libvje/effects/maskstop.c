/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
 *
 * vvMaskStop - ported from vvFFPP_basic
 * Copyright(C)2005 Maciek Szczesniak <maciek@visualvinyl.net>
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
#include "maskstop.h"

#define MASKSTOP_PARAMS 4

#define P_NEGATE_MASK 0
#define P_SWAP_MASK   1
#define P_FRAME_FREQ  2
#define P_MASK_FREQ   3

typedef struct {
    uint8_t *vvmaskstop_buffer[6];
    int frq_frame;
    int frq_mask;
    int n_threads;
} vvmask_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t maskstop_div255(int v)
{
    return (uint8_t)(((v + 1) + (v >> 8)) >> 8);
}

static inline uint8_t maskstop_blend255(int a, int b, int w)
{
    return maskstop_div255(a * w + b * (255 - w));
}

vj_effect *maskstop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MASKSTOP_PARAMS;
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

    ve->defaults[P_NEGATE_MASK] = 0;
    ve->defaults[P_SWAP_MASK] = 0;
    ve->defaults[P_FRAME_FREQ] = 80;
    ve->defaults[P_MASK_FREQ] = 20;

    ve->limits[0][P_NEGATE_MASK] = 0; ve->limits[1][P_NEGATE_MASK] = 1;
    ve->limits[0][P_SWAP_MASK] = 0;   ve->limits[1][P_SWAP_MASK] = 1;
    ve->limits[0][P_FRAME_FREQ] = 0;  ve->limits[1][P_FRAME_FREQ] = 255;
    ve->limits[0][P_MASK_FREQ] = 0;   ve->limits[1][P_MASK_FREQ] = 255;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Negate Mask",
        "Swap Mask/Frame",
        "Hold Frame Frequency",
        "Hold Mask Frequency"
    );

    ve->description = "vvMaskStop";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_NEGATE_MASK], P_NEGATE_MASK, "Normal", "Negate");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_SWAP_MASK], P_SWAP_MASK, "Mask Drives Held Frame", "Mask Drives Current Frame");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MEMORY,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 6,                  150,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_MEMORY,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 4,                  110,                12, 46, 1000, 3600, 0,    66
    );

    return ve;
}

void *maskstop_malloc(int width, int height)
{
    vvmask_t *v = (vvmask_t*) vj_calloc(sizeof(vvmask_t));

    if(!v)
        return NULL;

    const int len = width * height;

    v->vvmaskstop_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 6);

    if(!v->vvmaskstop_buffer[0]) {
        free(v);
        return NULL;
    }

    for(int i = 1; i < 6; i++)
        v->vvmaskstop_buffer[i] = v->vvmaskstop_buffer[i - 1] + len;

    veejay_memset(v->vvmaskstop_buffer[0], pixel_Y_lo_, len);
    veejay_memset(v->vvmaskstop_buffer[1], 128, len);
    veejay_memset(v->vvmaskstop_buffer[2], 128, len);
    veejay_memset(v->vvmaskstop_buffer[3], pixel_Y_lo_, len);
    veejay_memset(v->vvmaskstop_buffer[4], 128, len);
    veejay_memset(v->vvmaskstop_buffer[5], 128, len);

    v->frq_frame = 256;
    v->frq_mask = 256;
    v->n_threads = vje_advise_num_threads(len);

    return (void*) v;
}

void maskstop_free(void *ptr)
{
    vvmask_t *v = (vvmask_t*) ptr;

    free(v->vvmaskstop_buffer[0]);
    free(v);
}

static void maskstop_blend(vvmask_t *v, VJFrame *frame, int swapmask, int negmask)
{
    const int len = frame->len;

    const uint8_t *restrict Yframe = v->vvmaskstop_buffer[0];
    const uint8_t *restrict Uframe = v->vvmaskstop_buffer[1];
    const uint8_t *restrict Vframe = v->vvmaskstop_buffer[2];
    const uint8_t *restrict Ymask = v->vvmaskstop_buffer[3];
    const uint8_t *restrict Umask = v->vvmaskstop_buffer[4];
    const uint8_t *restrict Vmask = v->vvmaskstop_buffer[5];

    uint8_t *restrict Ydest = frame->data[0];
    uint8_t *restrict Udest = frame->data[1];
    uint8_t *restrict Vdest = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(v->n_threads)
    for(int i = 0; i < len; i++) {
        if(swapmask) {
            if(negmask) {
                Ydest[i] = maskstop_blend255(Yframe[i], 255 - Ydest[i], Ymask[i]);
                Udest[i] = maskstop_blend255(Uframe[i], 255 - Udest[i], Umask[i]);
                Vdest[i] = maskstop_blend255(Vframe[i], 255 - Vdest[i], Vmask[i]);
            } else {
                Ydest[i] = maskstop_blend255(Ydest[i], Yframe[i], Ymask[i]);
                Udest[i] = maskstop_blend255(Udest[i], Uframe[i], Umask[i]);
                Vdest[i] = maskstop_blend255(Vdest[i], Vframe[i], Vmask[i]);
            }
        } else {
            if(negmask) {
                Ydest[i] = maskstop_blend255(Ymask[i], 255 - Ydest[i], Yframe[i]);
                Udest[i] = maskstop_blend255(Umask[i], 255 - Udest[i], Uframe[i]);
                Vdest[i] = maskstop_blend255(Vmask[i], 255 - Vdest[i], Vframe[i]);
            } else {
                Ydest[i] = maskstop_blend255(Ydest[i], Ymask[i], Yframe[i]);
                Udest[i] = maskstop_blend255(Udest[i], Umask[i], Uframe[i]);
                Vdest[i] = maskstop_blend255(Vdest[i], Vmask[i], Vframe[i]);
            }
        }
    }
}

void maskstop_apply(void *ptr, VJFrame *frame, int *args)
{
    vvmask_t *v = (vvmask_t*) ptr;

    const int negmask = args[P_NEGATE_MASK];
    const int swapmask = args[P_SWAP_MASK];
    const int framefreq = args[P_FRAME_FREQ];
    const int maskfreq = args[P_MASK_FREQ];
    const int len = frame->len;

    uint8_t *restrict Ydest = frame->data[0];
    uint8_t *restrict Udest = frame->data[1];
    uint8_t *restrict Vdest = frame->data[2];

    v->frq_frame += framefreq;
    v->frq_mask += maskfreq;

    if(v->frq_frame > 255) {
        veejay_memcpy(v->vvmaskstop_buffer[0], Ydest, len);
        veejay_memcpy(v->vvmaskstop_buffer[1], Udest, len);
        veejay_memcpy(v->vvmaskstop_buffer[2], Vdest, len);
        v->frq_frame = 0;
    }

    if(v->frq_mask > 255) {
        veejay_memcpy(v->vvmaskstop_buffer[3], Ydest, len);
        veejay_memcpy(v->vvmaskstop_buffer[4], Udest, len);
        veejay_memcpy(v->vvmaskstop_buffer[5], Vdest, len);
        v->frq_mask = 0;
    }

    maskstop_blend(v, frame, swapmask, negmask);
}
