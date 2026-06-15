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
#include "opacity.h"

#define OPACITY_PARAMS 1

#define P_OPACITY 0

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t opacity_div255(int v)
{
    return (uint8_t)(((v + 128) + ((v + 128) >> 8)) >> 8);
}

static inline uint8_t opacity_blend_u8(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;

    return opacity_div255((int)a * inv + (int)b * opacity);
}

vj_effect *opacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = OPACITY_PARAMS;
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

    ve->limits[0][P_OPACITY] = 0;
    ve->limits[1][P_OPACITY] = 255;
    ve->defaults[P_OPACITY] = 150;

    ve->description = "Normal Overlay";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 18, 245, 16, 62, 700, 2800, 0, 86
    );
    return ve;
}

static void opacity_blend_yuv(uint8_t *restrict Y1,
                              uint8_t *restrict Cb1,
                              uint8_t *restrict Cr1,
                              const uint8_t *restrict Y2,
                              const uint8_t *restrict Cb2,
                              const uint8_t *restrict Cr2,
                              int len,
                              int uv_len,
                              int opacity)
{
    const int n_threads = vje_advise_num_threads(len);

    if(opacity <= 0)
        return;

    if(opacity >= 255) {
        veejay_memcpy(Y1, Y2, len);
        veejay_memcpy(Cb1, Cb2, uv_len);
        veejay_memcpy(Cr1, Cr2, uv_len);
        return;
    }

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y1[i] = opacity_blend_u8(Y1[i], Y2[i], opacity);

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb1[i] = opacity_blend_u8(Cb1[i], Cb2[i], opacity);
            Cr1[i] = opacity_blend_u8(Cr1[i], Cr2[i], opacity);
        }
    }
}

void opacity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void)ptr;

    const int opacity = clampi(args[P_OPACITY], 0, 255);
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    opacity_blend_yuv(
        frame->data[0],
        frame->data[1],
        frame->data[2],
        frame2->data[0],
        frame2->data[1],
        frame2->data[2],
        len,
        uv_len,
        opacity
    );
}

void opacity_blend_apply(uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int opacity)
{
    opacity_blend_yuv(
        src1[0],
        src1[1],
        src1[2],
        src2[0],
        src2[1],
        src2[2],
        len,
        uv_len,
        clampi(opacity, 0, 255)
    );
}
