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
#include <veejaycore/vjmem.h>
#include "colorshift.h"

vj_effect *colorshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 235;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->description = "Shift pixel values YCbCr";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );

	ve->hints = vje_init_value_hint_list( ve->num_params );
	
	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		"Luma (OR)", "Chroma Blue (OR)", "Chroma Red (OR)", "Chroma Blue and Red (OR)",
		"All Channels (OR)", "All Channels (AND)", "Luma (AND)", "Chroma Blue (AND)",
		"Chroma Red (AND)", "Chroma Blue and Red (AND)"
	);	

    return ve;
}

static void softmask2_apply(VJFrame *frame, int paramt)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Y[i] &= paramt;
    }
}

static void softmask2_applycb(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cb = frame->data[1];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Cb[i] &= paramt;
    }
}

static void softmask2_applycr(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cr = frame->data[2];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Cr[i] &= paramt;
    }
}

static void softmask2_applycbcr(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++) {
            Cb[i] &= paramt;
            Cr[i] &= paramt;
        }
    }
}

static void softmask2_applyycbcr(VJFrame *frame, int paramt)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int n_threads_y  = vje_advise_num_threads(len);
    const int n_threads_uv = vje_advise_num_threads(uv_len);

#pragma omp parallel for num_threads(n_threads_y) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Y[i] &= paramt;
    }

#pragma omp parallel for num_threads(n_threads_uv) schedule(static)
    for (int x = 0; x < uv_len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++) {
            Cb[i] &= paramt;
            Cr[i] &= paramt;
        }
    }
}

static void softmask_apply(VJFrame *frame, int paramt)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Y[i] |= paramt;
    }
}

static void softmask_applycb(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cb = frame->data[1];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Cb[i] |= paramt;
    }
}

static void softmask_applycr(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cr = frame->data[2];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Cr[i] |= paramt;
    }
}

static void softmask_applycbcr(VJFrame *frame, int paramt)
{
    const int len = (frame->ssm ? frame->len : frame->uv_len);
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++) {
            Cb[i] |= paramt;
            Cr[i] |= paramt;
        }
    }
}

static void softmask_applyycbcr(VJFrame *frame, int paramt)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int n_threads_y  = vje_advise_num_threads(len);
    const int n_threads_uv = vje_advise_num_threads(uv_len);

#pragma omp parallel for num_threads(n_threads_y) schedule(static)
    for (int x = 0; x < len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++)
            Y[i] |= paramt;
    }

#pragma omp parallel for num_threads(n_threads_uv) schedule(static)
    for (int x = 0; x < uv_len; x++) {
#pragma omp simd
        for (int i = x; i < x + 1; i++) {
            Cb[i] |= paramt;
            Cr[i] |= paramt;
        }
    }
}

void colorshift_apply(void *ptr, VJFrame *frame, int *args ) {
    int type = args[0];
    int value = args[1];

    switch (type) {
    case 0:
       softmask_apply(frame, value);
       break;
    case 1:
       softmask_applycb(frame, value);
       break;
    case 2:
       softmask_applycr(frame, value);
       break;
    case 3:
       softmask_applycbcr(frame, value);
       break;
    case 4:
       softmask_applyycbcr(frame, value);
       break;
    case 5:
       softmask2_apply(frame, value);
       break;
    case 6:
       softmask2_applycb(frame, value);
       break;
    case 7:
       softmask2_applycr(frame, value);
       break;
    case 8:
       softmask2_applycbcr(frame, value);
       break;
    case 9:
       softmask2_applyycbcr(frame, value);
       break;
	}
}