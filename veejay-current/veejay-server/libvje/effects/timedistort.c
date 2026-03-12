/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * TimeDistortionTV - scratch the surface and playback old images.
 * Copyright (C) 2005 Ryo-ta
 *
 * Ported and arranged by Kentaro Fukuchi

 * Ported and modified by Niels Elburg 
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
#include <veejaycore/vjmem.h>
#include "softblur.h"
#include "timedistort.h"
#include <libvje/internal.h>
#include <libvje/effects/motionmap.h>


static int PLANES = 256;

vj_effect *timedistort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 40;
    ve->description = "TimeDistortionTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Value");
    return ve;
}

typedef struct {
    int n__;
    int N__;
    uint8_t	*nonmap;
    uint8_t *planes[4];
    uint8_t *planetableY[512];
    uint8_t *planetableU[512];
    uint8_t *planetableV[512];
    uint8_t *warptime[2];
    int state;
    int plane;
    int warptimeFrame;
    int have_bg;
    void *motionmap;
	int n_threads;
	int plane_populated;
} timedistort_t;

void *timedistort_malloc(int w, int h)
{
    unsigned int i;
    timedistort_t *td = (timedistort_t*) vj_calloc(sizeof(timedistort_t));
    if(!td) return NULL;

    td->nonmap = vj_calloc((2 * w * h + 2 * w) * sizeof(uint8_t));
    if(!td->nonmap) {
        free(td);
        return NULL;
    }

	td->n_threads = vje_advise_num_threads(w*h);

    // try allocations: 256 -> 128 -> 64 planes
    int try_planes[] = { 256, 128, 64 };
    int success = 0;
    for(int t = 0; t < 3; t++) {
        PLANES = try_planes[t];
        td->planes[0] = vj_malloc((size_t)PLANES * 3 * w * h * sizeof(uint8_t));
        if(td->planes[0]) {
            success = 1;
            break;
        }
    }

    if(!success) {
        free(td->nonmap);
        free(td);
        return NULL;
    }

    td->planes[1] = td->planes[0] + PLANES * w * h;
    td->planes[2] = td->planes[1] + PLANES * w * h;

    veejay_memset(td->planes[0], 0, PLANES * w * h);
    veejay_memset(td->planes[1], 128, PLANES * w * h);
    veejay_memset(td->planes[2], 128, PLANES * w * h);

    td->have_bg = 0;
    td->n__ = 0;
    td->N__ = 0;

    for(i = 0; i < PLANES; i++) {
        td->planetableY[i] = &(td->planes[0][(w * h) * i]);
        td->planetableU[i] = &(td->planes[1][(w * h) * i]);
        td->planetableV[i] = &(td->planes[2][(w * h) * i]);
    }

    td->warptime[0] = (uint8_t*) vj_calloc(w * h);
    if(!td->warptime[0]) {
        free(td->nonmap);
        free(td->planes[0]);
        free(td);
        return NULL;
    }
    td->warptime[1] = (uint8_t*) vj_calloc(w * h);
    if(!td->warptime[1]) {
        free(td->nonmap);
        free(td->planes[0]);
        free(td->warptime[0]);
        free(td);
        return NULL;
    }

    td->plane = 0;
    td->state = 1;

    return (void*)td;
}

void	timedistort_free(void *ptr)
{
    timedistort_t *td = (timedistort_t*) ptr;

	if(td->nonmap)
		free(td->nonmap);
	if( td->planes[0])
		free(td->planes[0]);
	if( td->warptime[0] )
		free(td->warptime[0]);
	if( td->warptime[1] )
		free(td->warptime[1] );
    free(td);
}

int timedistort_request_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP;
}

void timedistort_set_motionmap(void *ptr, void *priv)
{
    timedistort_t *t = (timedistort_t*) ptr;
    t->motionmap = priv;
}

void timedistort_apply(void *ptr, VJFrame *frame, int *args)
{
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int interpolate = 1;
    int motion = 0;
    int tmp1, tmp2;
    int val = args[0];

    timedistort_t *td = (timedistort_t*) ptr;
    uint8_t *restrict diff = td->nonmap;
    uint8_t *restrict prev = td->nonmap + len;

    if (motionmap_active(td->motionmap)) {
        motionmap_scale_to(td->motionmap, 255, 255, 1, 1, &tmp1, &tmp2, &(td->n__), &(td->N__));
        diff = motionmap_bgmap(td->motionmap);
        motion = 1;
    } else {
        td->n__ = 0;
        td->N__ = 0;

        if (!td->have_bg) {

            vj_frame_copy1(Y, prev, len);
            veejay_memcpy(td->planetableY[0], Y, len);
            veejay_memcpy(td->planetableU[0], Cb, len);
            veejay_memcpy(td->planetableV[0], Cr, len);

            VJFrame smooth;
            veejay_memcpy(&smooth, frame, sizeof(VJFrame));
            smooth.data[0] = prev;
            softblur_apply_internal(&smooth);

            veejay_memset(diff, 0, len);
            td->have_bg = 1;
            td->plane = 1;
            td->plane_populated = 1;
            return;
        } else {
            vje_diff_plane(prev, Y, diff, val, len);
            vj_frame_copy1(Y, prev, len);
            VJFrame smooth;
            veejay_memcpy(&smooth, frame, sizeof(VJFrame));
            smooth.data[0] = prev;
            softblur_apply_internal(&smooth);
        }
    }

    if (td->n__ == td->N__ || td->n__ == 0)
        interpolate = 0;

    uint8_t *planeTables[4] = {
        td->planetableY[td->plane],
        td->planetableU[td->plane],
        td->planetableV[td->plane],
        NULL
    };
    int strides[4] = { len, len, len, 0 };
    vj_frame_copy(frame->data, planeTables, strides);

    if (td->plane_populated < PLANES)
        td->plane_populated++;

    uint8_t *restrict warptime0 = td->warptime[td->warptimeFrame];
    uint8_t *restrict warptime1 = td->warptime[td->warptimeFrame ^ 1];
    int nthreads = td->n_threads > 0 ? td->n_threads : omp_get_max_threads();
    const int width_i = (int) width;
    const int height_i = (int) height;

    #pragma omp parallel for num_threads(nthreads) schedule(static)
    for (int y = 0; y < height_i; y++) {
        const int row_off = y * width_i;
        for (int x = 0; x < width_i; x++) {
            const int idx = row_off + x;
            int tmp = 0;

            if (y > 0) tmp += warptime0[idx - width_i];
            if (y < height_i - 1) tmp += warptime0[idx + width_i];
            if (x > 0) tmp += warptime0[idx - 1];
            if (x < width_i - 1) tmp += warptime0[idx + 1];

            if (tmp > 3) tmp -= 3;
            warptime1[idx] = (uint8_t)(tmp >> 2);
        }
    }

    uint8_t *restrict q = td->warptime[td->warptimeFrame ^ 1];
    const int planes_mask = PLANES - 1;
    const int plane_now = td->plane;
    const int populated = td->plane_populated;

    #pragma omp parallel for simd num_threads(td->n_threads) schedule(static)
    for (int i = 0; i < len; i++) {
        int age = q[i];
        if (populated < PLANES && age >= populated)
            age = populated - 1;
        if (diff[i])
            q[i] = PLANES - 1;
        int n_plane = (plane_now - age + PLANES) & planes_mask;

        Y[i] = td->planetableY[n_plane][i];
        Cb[i] = td->planetableU[n_plane][i];
        Cr[i] = td->planetableV[n_plane][i];
    }

    td->plane = (td->plane + 1) & (PLANES - 1);
    td->warptimeFrame ^= 1;

    if (interpolate)
        motionmap_interpolate_frame(td->motionmap, frame, td->N__, td->n__);
    if (motion)
        motionmap_store_frame(td->motionmap, frame);
}