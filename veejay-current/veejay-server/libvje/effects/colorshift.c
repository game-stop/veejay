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
#include "colorshift.h"

vj_effect *colorshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 9;   ve->defaults[0] = 5;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 235;

    ve->description = "Shift pixel values YCbCr";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Value");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Luma (OR)",
        "Chroma Blue (OR)",
        "Chroma Red (OR)",
        "Chroma Blue and Red (OR)",
        "All Channels (OR)",
        "All Channels (AND)",
        "Luma (AND)",
        "Chroma Blue (AND)",
        "Chroma Red (AND)",
        "Chroma Blue and Red (AND)"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 32,                 255,                4,  16, 2600, 7600, 1800, 24
    );

    return ve;
}

static void colorshift_or_plane(uint8_t *restrict p, int len, uint8_t value)
{
    if(!p || len <= 0)
        return;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        p[i] = (uint8_t)(p[i] | value);
}

static void colorshift_and_plane(uint8_t *restrict p, int len, uint8_t value)
{
    if(!p || len <= 0)
        return;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        p[i] = (uint8_t)(p[i] & value);
}

static void colorshift_or_2planes(uint8_t *restrict p0, uint8_t *restrict p1, int len, uint8_t value)
{
    if(!p0 || !p1 || len <= 0)
        return;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        p0[i] = (uint8_t)(p0[i] | value);
        p1[i] = (uint8_t)(p1[i] | value);
    }
}

static void colorshift_and_2planes(uint8_t *restrict p0, uint8_t *restrict p1, int len, uint8_t value)
{
    if(!p0 || !p1 || len <= 0)
        return;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        p0[i] = (uint8_t)(p0[i] & value);
        p1[i] = (uint8_t)(p1[i] & value);
    }
}

static void colorshift_or_ycbcr(VJFrame *frame, uint8_t value)
{
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if(!Y || !Cb || !Cr || len <= 0)
        return;

    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = (uint8_t)(Y[i] | value);

        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb[i] = (uint8_t)(Cb[i] | value);
            Cr[i] = (uint8_t)(Cr[i] | value);
        }
    }
}

static void colorshift_and_ycbcr(VJFrame *frame, uint8_t value)
{
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if(!Y || !Cb || !Cr || len <= 0)
        return;

    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = (uint8_t)(Y[i] & value);

        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb[i] = (uint8_t)(Cb[i] & value);
            Cr[i] = (uint8_t)(Cr[i] & value);
        }
    }
}

void colorshift_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int type = args[0];
    const uint8_t value = args[1];
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel num_threads(n_threads)
    {
        switch(type) {
            case 0:
                colorshift_or_plane(Y, len, value);
                break;
            case 1:
                colorshift_or_plane(Cb, uv_len, value);
                break;
            case 2:
                colorshift_or_plane(Cr, uv_len, value);
                break;
            case 3:
                colorshift_or_2planes(Cb, Cr, uv_len, value);
                break;
            case 4:
                colorshift_or_ycbcr(frame, value);
                break;
            case 5:
                colorshift_and_ycbcr(frame, value);
                break;
            case 6:
                colorshift_and_plane(Y, len, value);
                break;
            case 7:
                colorshift_and_plane(Cb, uv_len, value);
                break;
            case 8:
                colorshift_and_plane(Cr, uv_len, value);
                break;
            case 9:
                colorshift_and_2planes(Cb, Cr, uv_len, value);
                break;
        }
    }
}

