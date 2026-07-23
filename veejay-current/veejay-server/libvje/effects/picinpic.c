/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include "picinpic.h"

#define PICINPIC_PARAMS 4

#define P_WIDTH  0
#define P_HEIGHT 1
#define P_X      2
#define P_Y      3

typedef struct {
    void *scaler;
    VJFrame *frame;
    sws_template template;
    uint8_t *private_data[3];
    int w;
    int h;
    int pixfmt;
} pic_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int pip_floor8(int v)
{
    return v & ~7;
}

static inline int pip_size8(int v)
{
    v = pip_floor8(v);
    return v < 8 ? 8 : v;
}

static inline int pip_pos8(int v)
{
    v = pip_floor8(v);
    return v < 0 ? 0 : v;
}

static void picinpic_release_frame(pic_t *picture)
{
    free(picture->private_data[0]);
    picture->private_data[0] = NULL;
    picture->private_data[1] = NULL;
    picture->private_data[2] = NULL;

    if(picture->frame) {
        picture->frame->data[0] = NULL;
        picture->frame->data[1] = NULL;
        picture->frame->data[2] = NULL;
        free(picture->frame);
        picture->frame = NULL;
    }

    if(picture->scaler) {
        yuv_free_swscaler(picture->scaler);
        picture->scaler = NULL;
    }

    picture->w = 0;
    picture->h = 0;
    picture->pixfmt = 0;
}

static int picinpic_alloc_frame(pic_t *picture, int view_width, int view_height, int pixfmt)
{
    const size_t plane_len = (size_t)view_width * (size_t)view_height;

    picture->frame = yuv_yuv_template(NULL, NULL, NULL, view_width, view_height, pixfmt);

    if(!picture->frame)
        return 0;

    picture->private_data[0] = (uint8_t*) vj_malloc(plane_len * 3u);

    if(!picture->private_data[0]) {
        free(picture->frame);
        picture->frame = NULL;
        return 0;
    }

    picture->private_data[1] = picture->private_data[0] + plane_len;
    picture->private_data[2] = picture->private_data[1] + plane_len;

    picture->frame->data[0] = picture->private_data[0];
    picture->frame->data[1] = picture->private_data[1];
    picture->frame->data[2] = picture->private_data[2];

    veejay_memset(picture->frame->data[0], pixel_Y_lo_, plane_len);
    veejay_memset(picture->frame->data[1], 128, plane_len);
    veejay_memset(picture->frame->data[2], 128, plane_len);

    return 1;
}

vj_effect *picinpic_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PICINPIC_PARAMS;
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

    ve->limits[0][P_WIDTH] = 8;  ve->limits[1][P_WIDTH] = width;  ve->defaults[P_WIDTH] = width / 8;
    ve->limits[0][P_HEIGHT] = 8; ve->limits[1][P_HEIGHT] = height; ve->defaults[P_HEIGHT] = height / 8;
    ve->limits[0][P_X] = 0;      ve->limits[1][P_X] = width;       ve->defaults[P_X] = width / 2;
    ve->limits[0][P_Y] = 0;      ve->limits[1][P_Y] = height;      ve->defaults[P_Y] = height / 2;

    if(ve->defaults[P_WIDTH] < 8)
        ve->defaults[P_WIDTH] = 8;
    if(ve->defaults[P_HEIGHT] < 8)
        ve->defaults[P_HEIGHT] = 8;

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

    int w_hi = width >> 1;
    int h_hi = height >> 1;

    if(w_hi < 8)
        w_hi = 8;
    if(h_hi < 8)
        h_hi = 8;
    if(w_hi > 360)
        w_hi = 360;
    if(h_hi > 288)
        h_hi = 288;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, 0, width, 92, 100, 6, 380, 0, 8, 80, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, height, 84, 100, 0, 360, 0, 8, 80, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *picinpic_malloc(int w, int h)
{
    pic_t *picture = (pic_t*) vj_calloc(sizeof(pic_t));

    if(!picture)
        return NULL;

    picture->template.flags = 1;

    (void)w;
    (void)h;

    return (void*) picture;
}

void picinpic_free(void *ptr)
{
    pic_t *picture = (pic_t*) ptr;

    picinpic_release_frame(picture);
    free(picture);
}

static int picinpic_rebuild(pic_t *picture, VJFrame *src, int view_width, int view_height, int pixfmt)
{
    picinpic_release_frame(picture);

    picture->w = view_width;
    picture->h = view_height;
    picture->pixfmt = pixfmt;

    if(!picinpic_alloc_frame(picture, view_width, view_height, pixfmt)) {
        picture->w = 0;
        picture->h = 0;
        picture->pixfmt = 0;
        return 0;
    }

    picture->scaler = yuv_init_swscaler(
        src,
        picture->frame,
        &(picture->template),
        yuv_sws_get_cpu_flags()
    );

    if(!picture->scaler) {
        picinpic_release_frame(picture);
        return 0;
    }

    return 1;
}

void picinpic_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    pic_t *picture = (pic_t*) ptr;
    const int width = frame->width;
    const int height = frame->height;

    int view_width = pip_size8(clampi(args[P_WIDTH], 8, width));
    int view_height = pip_size8(clampi(args[P_HEIGHT], 8, height));
    int dx = pip_pos8(clampi(args[P_X], 0, width - 1));
    int dy = pip_pos8(clampi(args[P_Y], 0, height - 1));

    if(view_width > width)
        view_width = pip_floor8(width);
    if(view_height > height)
        view_height = pip_floor8(height);

    if(dx + view_width > width)
        dx = pip_pos8(width - view_width);
    if(dy + view_height > height)
        dy = pip_pos8(height - view_height);

    const int pixfmt = frame->format == AV_PIX_FMT_YUVJ422P ? AV_PIX_FMT_YUVJ444P : AV_PIX_FMT_YUV444P;

    VJFrame src;
    veejay_memcpy(&src, frame2, sizeof(VJFrame));
    src.format = pixfmt;
    src.stride[1] = src.width;
    src.stride[2] = src.width;

    if(picture->w != view_width || picture->h != view_height || picture->pixfmt != pixfmt || !picture->frame || !picture->scaler) {
        if(!picinpic_rebuild(picture, &src, view_width, view_height, pixfmt))
            return;
    }

    yuv_convert_and_scale(picture->scaler, &src, picture->frame);

    const uint8_t *restrict sY = picture->frame->data[0];
    const uint8_t *restrict sCb = picture->frame->data[1];
    const uint8_t *restrict sCr = picture->frame->data[2];

    uint8_t *restrict dY = frame->data[0];
    uint8_t *restrict dCb = frame->data[1];
    uint8_t *restrict dCr = frame->data[2];

    for(int y = 0; y < view_height; y++) {
        const int dst_off = (dy + y) * width + dx;
        const int src_off = y * view_width;

        veejay_memcpy(dY + dst_off, sY + src_off, view_width);
        veejay_memcpy(dCb + dst_off, sCb + src_off, view_width);
        veejay_memcpy(dCr + dst_off, sCr + src_off, view_width);
    }
}
