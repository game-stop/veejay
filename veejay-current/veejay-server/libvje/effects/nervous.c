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

/*
    Nervous is loosly based on Kentaro's Nervous effect, found
    in EffecTV ( http://effectv.sf.net ).
*/

#include "common.h"
#include <veejaycore/vjmem.h>
#include "nervous.h"

#define N_MAX 100

typedef struct {
    uint8_t *nervous_buf[3];
    int frames_elapsed;
} nervous_t;

vj_effect *nervous_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = N_MAX;
    ve->defaults[0] = N_MAX;

    ve->description = "Nervous";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Buffer length");
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_MEMORY, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,
        4, 72, 6, 22, 1800, 4200, 900, 30 /* Buffer length */
    );

    (void) w;
    (void) h;

    return ve;
}

void *nervous_malloc(int w, int h)
{
    nervous_t *n = (nervous_t*) vj_calloc(sizeof(nervous_t));
    if(!n)
        return NULL;

    const size_t len = (size_t) w * (size_t) h;
    const size_t total_len = len * (size_t) N_MAX * 3u;

    n->nervous_buf[0] = (uint8_t*) vj_malloc(total_len);
    if(!n->nervous_buf[0]) {
        free(n);
        return NULL;
    }

    n->nervous_buf[1] = n->nervous_buf[0] + (len * N_MAX);
    n->nervous_buf[2] = n->nervous_buf[1] + (len * N_MAX);

    n->frames_elapsed = 0;

    vj_frame_clear1(n->nervous_buf[0], pixel_Y_lo_, len * N_MAX);
    vj_frame_clear1(n->nervous_buf[1], 128,         len * N_MAX);
    vj_frame_clear1(n->nervous_buf[2], 128,         len * N_MAX);

    return (void*) n;
}

void nervous_free(void *ptr)
{
    nervous_t *n = (nervous_t*) ptr;
    if(!n)
        return;

    if(n->nervous_buf[0])
        free(n->nervous_buf[0]);

    free(n);
}

static inline int nervous_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void nervous_apply(void *ptr, VJFrame *frame, int *args)
{
    nervous_t *n = (nervous_t*) ptr;
    if(!n || !frame || !args)
        return;

    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    int length = nervous_clampi(args[0], 1, N_MAX);

    if(n->frames_elapsed < 0 || n->frames_elapsed >= length)
        n->frames_elapsed = 0;

    uint8_t *dstY  = n->nervous_buf[0] + ((size_t)len * n->frames_elapsed);
    uint8_t *dstCb = n->nervous_buf[1] + ((size_t)len * n->frames_elapsed);
    uint8_t *dstCr = n->nervous_buf[2] + ((size_t)len * n->frames_elapsed);

    veejay_memcpy(dstY,  frame->data[0], len);
    veejay_memcpy(dstCb, frame->data[1], uv_len);
    veejay_memcpy(dstCr, frame->data[2], uv_len);

    if(n->frames_elapsed > 0) {
        const unsigned int index = (unsigned int)(((double)n->frames_elapsed * rand()) / (RAND_MAX + 1.0));

        uint8_t *srcY  = n->nervous_buf[0] + ((size_t)len * index);
        uint8_t *srcCb = n->nervous_buf[1] + ((size_t)len * index);
        uint8_t *srcCr = n->nervous_buf[2] + ((size_t)len * index);

        veejay_memcpy(frame->data[0], srcY,  len);
        veejay_memcpy(frame->data[1], srcCb, uv_len);
        veejay_memcpy(frame->data[2], srcCr, uv_len);
    }

    n->frames_elapsed++;

    if(n->frames_elapsed >= length)
        n->frames_elapsed = 0;
}