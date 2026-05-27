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
#include "revtv.h"

vj_effect *revtv_init(int max_width, int max_height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 2;
    ve->defaults[1] = 42;
    ve->defaults[2] = 201;
    ve->defaults[3] = 6;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = max_height > 1 ? max_height : 1;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = max_width > 1 ? max_width : 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 7;

    ve->description = "RevTV (EffectTV)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Line spacing",
        "Vertical scale",
        "Luminance intensity",
        "Color range"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  16,                 6, 22, 2200, 5200, 1800, 25,    /* Line spacing */
        VJ_BEAT_WARP,      VJ_BEAT_F_CONTINUOUS,                               8,                  96,                 8, 30, 1200, 3000, 0,   45,    /* Vertical scale */
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS,                               48,                 255,                10,38, 1000, 2600, 0,   60,    /* Luminance intensity */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Color range */
    );

    return ve;
}

static inline int revtv_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void revtv_luma(uint8_t *restrict Y,
                       int width,
                       int height,
                       int linespace,
                       int vscale,
                       uint8_t color_y)
{
    for(int y = 0; y < height; y += linespace) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int lum = Y[row + x];
            const int yval = y - (lum / vscale);

            if(yval >= 0)
                Y[yval * width + x] = color_y;
        }
    }
}

static void revtv_chroma(uint8_t *restrict C,
                         int width,
                         int height,
                         int linespace,
                         int vscale,
                         uint8_t color_c)
{
    for(int y = 0; y < height; y += linespace) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int chroma_delta = C[row + x] - 128;
            const int mag = chroma_delta < 0 ? -chroma_delta : chroma_delta;
            const int yval = y - (mag / vscale);

            if(yval >= 0)
                C[yval * width + x] = color_c;
        }
    }
}

void revtv_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;

    if(width <= 0 || height <= 0)
        return;

    int linespace = revtv_clampi(args[0], 1, height);
    int vscale = revtv_clampi(args[1], 1, width);
    int color_y = revtv_clampi(args[2], 0, 255);
    int color_num = revtv_clampi(args[3], 0, 7);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t color_cb = (uint8_t)bl_pix_get_color_cb(color_num);
    const uint8_t color_cr = (uint8_t)bl_pix_get_color_cr(color_num);

    revtv_luma(Y, width, height, linespace, vscale, (uint8_t)color_y);

    if(color_num > 0) {
        const int uv_width = frame->ssm ? width : frame->uv_width;
        const int uv_height = frame->ssm ? height : frame->uv_height;

        if(uv_width > 0 && uv_height > 0) {
            int uv_linespace = linespace >> frame->shift_v;
            int uv_vscale = vscale >> frame->shift_v;

            if(uv_linespace < 1)
                uv_linespace = 1;
            if(uv_vscale < 1)
                uv_vscale = 1;

            revtv_chroma(Cb, uv_width, uv_height, uv_linespace, uv_vscale, color_cb);
            revtv_chroma(Cr, uv_width, uv_height, uv_linespace, uv_vscale, color_cr);
        }
    }
}