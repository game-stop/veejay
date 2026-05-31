/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "slice.h"
#include "motionmap.h"

typedef struct {
    uint8_t *slice_frame[3];
    int *slice_xshift;
    int *slice_yshift;
    int frame_periods;
    int current_period;
    int current_slices;
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} slice_t;

static inline int slice_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int slice_rand_range(int lo, int hi)
{
    if(hi <= lo)
        return lo;

    return lo + (rand() % (hi - lo + 1));
}

static void slice_recalc(slice_t *s, int width, int height, int val)
{
    val = slice_clampi(val, 2, 128);

    int *restrict slice_xshift = s->slice_xshift;
    int *restrict slice_yshift = s->slice_yshift;

    const int half = val >> 1;
    const int min_shift = -half;
    const int max_shift = half;
    const int min_run = 8;
    const int max_run = 8 + (half > 1 ? half - 1 : 1);

    int run = 0;
    int shift = 0;

    for(int x = 0; x < width; x++) {
        if(run <= 0) {
            shift = slice_rand_range(min_shift, max_shift);
            run = slice_rand_range(min_run, max_run);
        } else {
            run--;
        }

        slice_yshift[x] = shift;
    }

    run = 0;
    shift = 0;

    for(int y = 0; y < height; y++) {
        if(run <= 0) {
            shift = slice_rand_range(min_shift, max_shift);
            run = slice_rand_range(min_run, max_run);
        } else {
            run--;
        }

        slice_xshift[y] = shift;
    }

    s->current_slices = val;
}

vj_effect *slice_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 2;
    ve->limits[1][0] = 128;
    ve->defaults[0] = 63;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 8 * 30;
    ve->defaults[1] = 0;

    ve->description = "Slice Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->motion = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Slices",
        "Slice Period"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,  VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 4,  96,  6, 22, 1800, 4200, 900, 30, /* Slices */
        VJ_BEAT_SPEED, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                            0,  180, 6, 22, 1800, 4200, 900, 30  /* Slice Period */
    );

    (void) width;
    (void) height;

    return ve;
}

void *slice_malloc(int width, int height)
{
    slice_t *s = (slice_t*) vj_calloc(sizeof(slice_t));
    if(!s)
        return NULL;

    const int len = width * height;

    s->slice_frame[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->slice_frame[0]) {
        free(s);
        return NULL;
    }

    s->slice_frame[1] = s->slice_frame[0] + len;
    s->slice_frame[2] = s->slice_frame[1] + len;

    s->slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    if(!s->slice_xshift) {
        free(s->slice_frame[0]);
        free(s);
        return NULL;
    }

    s->slice_yshift = (int*) vj_malloc(sizeof(int) * width);
    if(!s->slice_yshift) {
        free(s->slice_xshift);
        free(s->slice_frame[0]);
        free(s);
        return NULL;
    }

    s->frame_periods = 0;
    s->current_period = 0;
    s->current_slices = -1;
    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    slice_recalc(s, width, height, 63);

    return (void*) s;
}

void slice_free(void *ptr)
{
    slice_t *s = (slice_t*) ptr;
    if(!s)
        return;

    if(s->slice_frame[0])
        free(s->slice_frame[0]);
    if(s->slice_xshift)
        free(s->slice_xshift);
    if(s->slice_yshift)
        free(s->slice_yshift);

    free(s);
}

void slice_apply(void *ptr, VJFrame *frame, int *args)
{
    slice_t *s = (slice_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int val = slice_clampi(args[0], 2, 128);
    int re_init = slice_clampi(args[1], 0, 8 * 30);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t **slice_frame = s->slice_frame;
    int *restrict slice_xshift = s->slice_xshift;
    int *restrict slice_yshift = s->slice_yshift;

    int interpolate = 0;
    int motion = 0;
    int tmp1 = val;
    int tmp2 = re_init;

    if(s->frame_periods != re_init) {
        s->frame_periods = re_init;
        s->current_period = re_init;
    }

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            128,
            1,
            2,
            0,
            &tmp1,
            &tmp2,
            &(s->n__),
            &(s->N__)
        );

        tmp1 = slice_clampi(tmp1, 2, 128);

        if(tmp1 >= 64) {
            tmp2 = ((rand() % 25) == 0) ? 1 : 0;
        } else {
            tmp2 = 1;
        }

        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    } else {
        s->n__ = 0;
        s->N__ = 0;
    }

    if(motion) {
        if(tmp2 == 1)
            slice_recalc(s, width, height, tmp1);
    } else {
        if(re_init > 0) {
            s->current_period--;

            if(s->current_period <= 0) {
                slice_recalc(s, width, height, val);
                s->current_period = s->frame_periods;
            }
        } else if(val != s->current_slices) {
            slice_recalc(s, width, height, val);
        }
    }

    veejay_memcpy(slice_frame[0], Y,  len);
    veejay_memcpy(slice_frame[1], Cb, len);
    veejay_memcpy(slice_frame[2], Cr, len);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int y_shift = slice_xshift[y];

        for(int x = 0; x < width; x++) {
            const int dx = x + y_shift;
            const int dy = y + slice_yshift[x];

            if((unsigned)dx < (unsigned)width && (unsigned)dy < (unsigned)height) {
                const int dst = row + x;
                const int src = dy * width + dx;

                Y[dst]  = slice_frame[0][src];
                Cb[dst] = slice_frame[1][src];
                Cr[dst] = slice_frame[2][src];
            }
        }
    }

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}

int slice_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void slice_set_motionmap(void *ptr, void *priv)
{
    slice_t *s = (slice_t*) ptr;

    if(s)
        s->motionmap = priv;
}