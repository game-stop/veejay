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

/* Copyright (C) 2002-2003 W.P. van Paassen - peter@paassen.tmfweb.nl

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "common.h"
#include "blob.h"

typedef struct {
    short x;
    short y;
} blob_t;

#define DEFAULT_RADIUS 16
#define DEFAULT_NUM 50

#define BLOB_RECT 0
#define BLOB_CIRCLE 1

typedef struct {
    blob_t *blobs_;
    uint8_t **blob_;
    uint8_t *blob_data_;
    uint8_t *blob_image_;
    int blob_ready_;
    int blob_radius_;
    int blob_dradius_;
    int blob_sradius_;
    int blob_num_;
    int blob_type_;
    int n_threads;
} blobs_t;

static inline int blob_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *blob_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = 360; ve->defaults[0] = DEFAULT_RADIUS;
    ve->limits[0][1] = 1; ve->limits[1][1] = 100; ve->defaults[1] = DEFAULT_NUM;
    ve->limits[0][2] = 1; ve->limits[1][2] = 100; ve->defaults[2] = 50;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;   ve->defaults[3] = 1;

    ve->description = "Video Blobs";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Blobs", "Speed", "Shape");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 100, 88, 100, 24, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void blob_init_(blobs_t *g, blob_t *b, int w, int h)
{
    b->x = (short)((w >> 1) - g->blob_radius_);
    b->y = (short)((h >> 1) - g->blob_radius_);
}

static void blob_release_shape(blobs_t *b)
{
    if(b->blob_) {
        free(b->blob_);
        b->blob_ = NULL;
    }

    if(b->blob_data_) {
        free(b->blob_data_);
        b->blob_data_ = NULL;
    }

    if(b->blobs_) {
        free(b->blobs_);
        b->blobs_ = NULL;
    }
}

static int blob_reinit(blobs_t *b, int radius, int num, int w, int h)
{
    int i;
    int j;

    radius = blob_clampi(radius, 1, 360);
    num = blob_clampi(num, 1, 100);

    blob_release_shape(b);

    b->blob_radius_ = radius;
    b->blob_num_ = num;
    b->blob_dradius_ = b->blob_radius_ * 2;
    b->blob_sradius_ = b->blob_radius_ * b->blob_radius_;

    b->blob_ = (uint8_t **) vj_calloc(sizeof(uint8_t *) * b->blob_dradius_);
    b->blob_data_ = (uint8_t *) vj_calloc(sizeof(uint8_t) * b->blob_dradius_ * b->blob_dradius_);
    b->blobs_ = (blob_t *) vj_calloc(sizeof(blob_t) * b->blob_num_);

    if(!b->blob_ || !b->blob_data_ || !b->blobs_) {
        blob_release_shape(b);
        return 0;
    }

    for(i = 0; i < b->blob_dradius_; i++)
        b->blob_[i] = b->blob_data_ + i * b->blob_dradius_;

    for(i = -b->blob_radius_; i < b->blob_radius_; i++)
    {
        for(j = -b->blob_radius_; j < b->blob_radius_; j++)
        {
            const int dist_sqrt = i * i + j * j;
            b->blob_[i + b->blob_radius_][j + b->blob_radius_] = dist_sqrt < b->blob_sradius_ ? 0xff : 0x0;
        }
    }

    for(i = 0; i < b->blob_num_; i++)
        blob_init_(b, b->blobs_ + i, w, h);

    return 1;
}

void *blob_malloc(int w, int h)
{
    blobs_t *b = (blobs_t *) vj_calloc(sizeof(blobs_t));

    if(!b)
        return NULL;

    if(!blob_reinit(b, DEFAULT_RADIUS, DEFAULT_NUM, w, h)) {
        blob_free(b);
        return NULL;
    }

    b->blob_type_ = BLOB_CIRCLE;
    b->blob_image_ = (uint8_t *) vj_malloc(sizeof(uint8_t) * w * h);

    if(!b->blob_image_) {
        blob_free(b);
        return NULL;
    }

    veejay_memset(b->blob_image_, 0, w * h);
    b->blob_ready_ = 1;
    b->n_threads = vje_advise_num_threads(w * h);

    return b;
}

void blob_free(void *ptr)
{
    blobs_t *b = (blobs_t *) ptr;

    if(!b)
        return;

    blob_release_shape(b);

    if(b->blob_image_)
        free(b->blob_image_);

    free(b);
}

typedef void (*blob_func)(blobs_t *b, int s, int width);

static void blob_render_circle(blobs_t *b, int s, int width)
{
    for(int i = 0; i < b->blob_dradius_; i++)
    {
        uint8_t *restrict dst = b->blob_image_ + s;
        uint8_t *restrict src = b->blob_[i];

        for(int j = 0; j < b->blob_dradius_; j++)
        {
            const int v = (int)dst[j] + (int)src[j];
            dst[j] = v > 255 ? 255 : (uint8_t)v;
        }

        s += width;
    }
}

static void blob_render_rect(blobs_t *b, int s, int width)
{
    for(int i = 0; i < b->blob_dradius_; i++)
    {
        veejay_memset(b->blob_image_ + s, 0xff, b->blob_dradius_);
        s += width;
    }
}

static blob_func blob_render(blobs_t *b)
{
    return b->blob_type_ == BLOB_RECT ? &blob_render_rect : &blob_render_circle;
}

void blob_apply(void *ptr, VJFrame *frame, int *args)
{
    blobs_t *b = (blobs_t *) ptr;

    int radius = args[0];
    int num = args[1];
    int speed = args[2];
    int shape = args[3];

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcCb = frame->data[1];
    uint8_t *restrict srcCr = frame->data[2];

    b->blob_type_ = shape;

    if(radius != b->blob_radius_ || num != b->blob_num_) {
        if(!blob_reinit(b, radius, num, frame->width, frame->height))
            return;
    }

    const int step = blob_clampi((speed + 9) / 10, 1, 10);
    blob_func f = blob_render(b);

    for(int i = 0; i < b->blob_num_; i++)
    {
        const int span = step * 2 + 1;
        b->blobs_[i].x += (short)((rand() % span) - step);
        b->blobs_[i].y += (short)((rand() % span) - step);
    }

    for(int k = 0; k < b->blob_num_; k++)
    {
        if((b->blobs_[k].x > 0) &&
           (b->blobs_[k].x < (int)(width - b->blob_dradius_)) &&
           (b->blobs_[k].y > 0) &&
           (b->blobs_[k].y < (int)(height - b->blob_dradius_)))
        {
            const int s = b->blobs_[k].x + b->blobs_[k].y * (int)width;
            f(b, s, (int)width);
        }
        else
        {
            blob_init_(b, b->blobs_ + k, (int)width, (int)height);
        }
    }

    #pragma omp parallel for num_threads(b->n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        if(b->blob_image_[i] == 0x0)
        {
            srcY[i] = pixel_Y_lo_;
            srcCb[i] = 128;
            srcCr[i] = 128;
        }

        b->blob_image_[i] = 0x0;
    }
}
