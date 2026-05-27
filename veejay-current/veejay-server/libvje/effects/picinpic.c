/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
    This effect uses libpostproc , it should be enabled at compile time
    (--with-swscaler) if you want to use this Effect.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include "picinpic.h"

extern void vj_get_yuv444_template(VJFrame *src, int w, int h);

typedef struct {
    void *scaler;
    VJFrame *frame;
    sws_template template;
    void *sampler;
    int cached;
    int w;
    int h;
} pic_t;

static int nearest_div(int val)
{
    if(val < 0)
        val = 0;

    return val - (val % 8);
}

static inline int pip_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void picinpic_release_frame(pic_t *picture)
{
    if(!picture)
        return;

    if(picture->frame) {
        for(int i = 0; i < 3; i++) {
            if(picture->frame->data[i]) {
                free(picture->frame->data[i]);
                picture->frame->data[i] = NULL;
            }
        }

        free(picture->frame);
        picture->frame = NULL;
    }

    if(picture->scaler) {
        yuv_free_swscaler(picture->scaler);
        picture->scaler = NULL;
    }

    picture->w = 0;
    picture->h = 0;
}

static int picinpic_alloc_frame(pic_t *picture, int view_width, int view_height, int pixfmt)
{
    const int plane_len = view_width * view_height;

    picture->frame = yuv_yuv_template(NULL, NULL, NULL, view_width, view_height, pixfmt);
    if(!picture->frame)
        return 0;

    for(int i = 0; i < 3; i++) {
        picture->frame->data[i] = (uint8_t*) vj_malloc(sizeof(uint8_t) * plane_len);
        if(!picture->frame->data[i]) {
            picinpic_release_frame(picture);
            return 0;
        }

        veejay_memset(picture->frame->data[i], (i == 0 ? pixel_Y_lo_ : 128), plane_len);
    }

    return 1;
}

vj_effect *picinpic_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = width / 8;
    ve->defaults[1] = height / 8;
    ve->defaults[2] = width / 2;
    ve->defaults[3] = height / 2;

    ve->limits[0][0] = 8;
    ve->limits[1][0] = width;

    ve->limits[0][1] = 8;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = width;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = height;

    ve->description = "Picture in picture";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Width",
        "Height",
        "X offset",
        "Y offset"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 32,                 width / 2,          6, 22, 1800, 4200, 900, 30, /* Width */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 32,                 height / 2,         6, 22, 1800, 4200, 900, 30, /* Height */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          0,                  width,              6, 22, 1800, 4200, 900, 25, /* X offset */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          0,                  height,             6, 22, 1800, 4200, 900, 25  /* Y offset */
    );

    return ve;
}

void *picinpic_malloc(int w, int h)
{
    pic_t *my = (pic_t*) vj_calloc(sizeof(pic_t));
    if(!my)
        return NULL;

    my->scaler = NULL;
    my->template.flags = 1;
    my->sampler = NULL;
    my->cached = 0;
    my->w = 0;
    my->h = 0;
    my->frame = NULL;

    (void) w;
    (void) h;

    return (void*) my;
}

void picinpic_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    pic_t *picture = (pic_t*) ptr;
    if(!picture || !frame || !frame2 || !args)
        return;

    const int width = frame->width;
    const int height = frame->height;

    if(width <= 0 || height <= 0)
        return;

    int twidth = pip_clampi(args[0], 8, width);
    int theight = pip_clampi(args[1], 8, height);
    int x1 = pip_clampi(args[2], 0, width - 1);
    int y1 = pip_clampi(args[3], 0, height - 1);

    int view_width = nearest_div(twidth);
    int view_height = nearest_div(theight);
    int dx = nearest_div(x1);
    int dy = nearest_div(y1);

    if(dx >= width || dy >= height)
        return;

    if((dx + view_width) > width)
        view_width = width - dx;

    if((dy + view_height) > height)
        view_height = height - dy;

    view_width = nearest_div(view_width);
    view_height = nearest_div(view_height);

    if(view_width < 8 || view_height < 8)
        return;

    const int pixfmt = (frame->format == AV_PIX_FMT_YUVJ422P)
        ? AV_PIX_FMT_YUVJ444P
        : AV_PIX_FMT_YUV444P;

    VJFrame src;
    veejay_memcpy(&src, frame2, sizeof(VJFrame));
    src.format = pixfmt;
    src.stride[1] = src.width;
    src.stride[2] = src.width;

    if(picture->w != view_width || picture->h != view_height || !picture->frame || !picture->scaler) {
        picinpic_release_frame(picture);

        picture->w = view_width;
        picture->h = view_height;

        if(!picinpic_alloc_frame(picture, view_width, view_height, pixfmt)) {
            picture->w = 0;
            picture->h = 0;
            return;
        }

        picture->scaler = yuv_init_swscaler(
            &src,
            picture->frame,
            &(picture->template),
            yuv_sws_get_cpu_flags()
        );

        if(!picture->scaler) {
            picinpic_release_frame(picture);
            return;
        }
    }

    yuv_convert_and_scale(picture->scaler, &src, picture->frame);

    uint8_t *sY  = picture->frame->data[0];
    uint8_t *sCb = picture->frame->data[1];
    uint8_t *sCr = picture->frame->data[2];

    uint8_t *dY  = frame->data[0];
    uint8_t *dCb = frame->data[1];
    uint8_t *dCr = frame->data[2];

    for(int y = 0; y < view_height; y++) {
        uint8_t *dst_y  = dY  + (dy + y) * width + dx;
        uint8_t *dst_cb = dCb + (dy + y) * width + dx;
        uint8_t *dst_cr = dCr + (dy + y) * width + dx;

        uint8_t *src_y  = sY  + y * view_width;
        uint8_t *src_cb = sCb + y * view_width;
        uint8_t *src_cr = sCr + y * view_width;

        veejay_memcpy(dst_y,  src_y,  view_width);
        veejay_memcpy(dst_cb, src_cb, view_width);
        veejay_memcpy(dst_cr, src_cr, view_width);
    }
}

void picinpic_free(void *d)
{
    if(!d)
        return;

    pic_t *my = (pic_t*) d;

    picinpic_release_frame(my);
    free(my);
}