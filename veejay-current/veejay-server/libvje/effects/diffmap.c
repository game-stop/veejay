/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "softblur.h"
#include "diffmap.h"

typedef struct {
    uint8_t *binary_img;
    int n_threads;
} diffmap_t;

vj_effect *differencemap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 40;
    ve->limits[0][1] = 0; ve->limits[1][1] = 1;   ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;   ve->defaults[2] = 1;

    ve->description = "Map B to A (bitmask)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Reverse", "Show");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,                  150,                12, 46,  900, 3200, 0,    76,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *differencemap_malloc(int w, int h)
{
    diffmap_t *d = (diffmap_t*) vj_calloc(sizeof(diffmap_t));

    if(!d)
        return NULL;

    const int len = w * h;

    if(len <= 0) {
        free(d);
        return NULL;
    }

    d->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * ((len * 2) + (w * 2)));

    if(!d->binary_img) {
        free(d);
        return NULL;
    }

    d->n_threads = vje_advise_num_threads(len);

    return d;
}

void differencemap_free(void *ptr)
{
    diffmap_t *d = (diffmap_t*) ptr;

    if(!d)
        return;

    if(d->binary_img)
        free(d->binary_img);

    free(d);
}

void differencemap_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    diffmap_t *d = (diffmap_t*) ptr;

    const int threshold = args[0];
    const int reverse = args[1];
    const int show = args[2];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    uint8_t *restrict binary_img = d->binary_img;
    uint8_t *restrict previous_img = binary_img + len;

    vj_frame_copy1(Y, previous_img, len);

    VJFrame tmp;
    veejay_memcpy(&tmp, frame, sizeof(VJFrame));
    tmp.data[0] = previous_img;

    softblur_apply_internal(&tmp);
    binarify_1src(binary_img, previous_img, threshold, reverse, width, height);

    if(show)
    {
        vj_frame_copy1(binary_img, Y, len);
        vj_frame_clear1(Cb, 128, uv_len);
        vj_frame_clear1(Cr, 128, uv_len);
        return;
    }

    veejay_memset(Y, pixel_Y_lo_, len);
    veejay_memset(Cb, 128, uv_len);
    veejay_memset(Cr, 128, uv_len);

    if(height < 3 || width < 3)
        return;

    #pragma omp parallel for schedule(static) num_threads(d->n_threads)
    for(int y = 1; y < height - 1; y++)
    {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++)
        {
            const int i = row + x;

            if(binary_img[i])
            {
                Y[i] = Y2[i];
                Cb[i] = Cb2[i];
                Cr[i] = Cr2[i];
            }
        }
    }
}
