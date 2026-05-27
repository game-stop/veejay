/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "opacity.h"
#include "scratcher.h"

typedef struct {
    uint8_t *frame[3];
    int nframe;
    int direction;
    int last_pingpong;
    int last_n;
} scratcher_t;

vj_effect *scratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_SCRATCH_FRAMES - 1;
    ve->defaults[1] = 8;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[2] = 1;

    ve->description = "Overlay Scratcher";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Scratch buffer",
        "PingPong"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Loop",
        "PingPong"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Opacity */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,  2,                  32,                 6, 22, 1800, 4200, 900, 30,    /* Scratch buffer */
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* PingPong */
    );

    (void) w;
    (void) h;

    return ve;
}

void *scratcher_malloc(int w, int h)
{
    scratcher_t *s = (scratcher_t*) vj_calloc(sizeof(scratcher_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->frame[0] = (uint8_t*) vj_malloc((size_t)len * 3u * (size_t)MAX_SCRATCH_FRAMES);
    if(!s->frame[0]) {
        free(s);
        return NULL;
    }

    s->frame[1] = s->frame[0] + (len * MAX_SCRATCH_FRAMES);
    s->frame[2] = s->frame[1] + (len * MAX_SCRATCH_FRAMES);

    veejay_memset(s->frame[0], pixel_Y_lo_, len * MAX_SCRATCH_FRAMES);
    veejay_memset(s->frame[1], 128,         len * MAX_SCRATCH_FRAMES);
    veejay_memset(s->frame[2], 128,         len * MAX_SCRATCH_FRAMES);

    s->nframe = 0;
    s->direction = 1;
    s->last_pingpong = 1;
    s->last_n = 8;

    return (void*) s;
}

void scratcher_free(void *ptr)
{
    scratcher_t *s = (scratcher_t*) ptr;
    if(!s)
        return;

    if(s->frame[0])
        free(s->frame[0]);

    free(s);
}

static inline int scratcher_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void scratcher_advance(scratcher_t *s, int n, int pingpong)
{
    if(n <= 1) {
        s->nframe = 0;
        s->direction = 1;
        return;
    }

    if(pingpong) {
        s->nframe += s->direction;

        if(s->nframe >= n) {
            s->nframe = n - 1;
            s->direction = -1;
        } else if(s->nframe < 0) {
            s->nframe = 0;
            s->direction = 1;
        }
    } else {
        s->nframe++;
        if(s->nframe >= n)
            s->nframe = 0;

        s->direction = 1;
    }
}

static void scratcher_store_current(scratcher_t *s, VJFrame *src, int slot)
{
    const int len = src->len;
    const int uv_len = src->ssm ? len : src->uv_len;

    uint8_t *dest[4] = {
        s->frame[0] + (len * slot),
        s->frame[1] + (uv_len * slot),
        s->frame[2] + (uv_len * slot),
        NULL
    };

    int strides[4] = { len, uv_len, uv_len, 0 };

    vj_frame_copy(src->data, dest, strides);
}

void scratcher_apply(void *ptr, VJFrame *src, int *args)
{
    scratcher_t *s = (scratcher_t*) ptr;
    if(!s || !src || !args || !src->data[0] || !src->data[1] || !src->data[2])
        return;

    const int len = src->len;
    const int uv_len = src->ssm ? len : src->uv_len;

    if(len <= 0 || uv_len <= 0)
        return;

    int n = scratcher_clampi(args[1], 1, MAX_SCRATCH_FRAMES - 1);
    int pingpong = args[2] ? 1 : 0;

    if(n != s->last_n || pingpong != s->last_pingpong) {
        s->last_n = n;
        s->last_pingpong = pingpong;

        if(s->nframe >= n)
            s->nframe = n - 1;
        if(s->nframe < 0)
            s->nframe = 0;

        s->direction = 1;
    }

    const int slot = s->nframe;
    const int offset = len * slot;
    const int uv_offset = uv_len * slot;

    VJFrame tmp;
    veejay_memcpy(&tmp, src, sizeof(VJFrame));

    if(slot == 0) {
        tmp.data[0] = src->data[0];
        tmp.data[1] = src->data[1];
        tmp.data[2] = src->data[2];
    } else {
        tmp.data[0] = s->frame[0] + offset;
        tmp.data[1] = s->frame[1] + uv_offset;
        tmp.data[2] = s->frame[2] + uv_offset;
    }

    opacity_apply(NULL, src, &tmp, args);

    if(!pingpong || s->direction > 0)
        scratcher_store_current(s, src, slot);

    scratcher_advance(s, n, pingpong);
}