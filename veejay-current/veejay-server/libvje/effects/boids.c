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
#include "boids.h"

typedef struct {
    short x;
    short y;
    double vx;
    double vy;
} blob_t;

#define DEFAULT_RADIUS 16
#define DEFAULT_NUM 100

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
    int blob_home_radius_;
    int n_threads;
} boids_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void boid_rule1_(boids_t *b, int boid_id, double v1[2]);
static void boid_rule2_(boids_t *b, int boid_id, double v1[2]);
static void boid_rule3_(boids_t *b, int boid_id, double v1[2]);
static void boid_rule4_(boids_t *b, int boid_id, int velocity_limit);

vj_effect *boids_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = w / 2; ve->defaults[0] = DEFAULT_RADIUS;
    ve->limits[0][1] = 2; ve->limits[1][1] = 256;   ve->defaults[1] = DEFAULT_NUM;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;     ve->defaults[2] = 1;
    ve->limits[0][3] = 0; ve->limits[1][3] = 100;   ve->defaults[3] = 12;
    ve->limits[0][4] = 0; ve->limits[1][4] = 100;   ve->defaults[4] = 18;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100;   ve->defaults[5] = 8;
    ve->limits[0][6] = 1; ve->limits[1][6] = 100;   ve->defaults[6] = 20;
    ve->limits[0][7] = 1; ve->limits[1][7] = 360;   ve->defaults[7] = w / 4;

    ve->description = "Video Boids";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Blobs", "Shape", "Cohesion", "Seperation", "Alignment", "Speed", "Home Radius");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 6,                  128,                4,  14, 3800, 9800, 2800, 18,
        VJ_BEAT_DENSITY,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 24,                 220,                4,  14, 3800, 9800, 2800, 20,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_FLOW,          VJ_BEAT_F_CONTINUOUS,                                                  8,                  78,                 10, 42, 1100, 3400, 0,    62,
        VJ_BEAT_MOTION_REACT,  VJ_BEAT_F_CONTINUOUS,                                                  6,                  86,                 12, 48,  900, 3000, 0,    72,
        VJ_BEAT_INERTIA,       VJ_BEAT_F_CONTINUOUS,                                                  0,                  82,                 8,  30, 1400, 4400, 0,    46,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       8,                  92,                 14, 58,  800, 2600, 0,    84,
        VJ_BEAT_GRID_SIZE,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,   VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

static void blob_home_position(boids_t *b, int blob_id, int w, int h, double v[2])
{
    const double theta = 360.0 / (double)b->blob_num_ * (double)blob_id;
    const double rad = (theta / 180.0) * M_PI;
    const double ratio = h > 0 ? ((double)w / (double)h) : 1.0;
    const double cx = (double)(w >> 1);
    const double cy = (double)(h >> 1) * ratio;

    v[0] = cx + a_cos(rad) * (double)b->blob_home_radius_;
    v[1] = cy + a_sin(rad) * (double)b->blob_home_radius_;
}

static void blob_init_(boids_t *g, blob_t *b, int blob_id, int w, int h)
{
    double v[2];

    blob_home_position(g, blob_id, w, h, v);

    b->x = (short)v[0];
    b->y = (short)v[1];
    b->vx = 0.01;
    b->vy = 0.01;
}

static void boids_release_shape(boids_t *b)
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

static int boids_reinit(boids_t *b, int radius, int num, int w, int h)
{
    radius = clampi(radius, 1, w > 1 ? w / 2 : 1);
    num = clampi(num, 2, 256);

    boids_release_shape(b);

    b->blob_radius_ = radius;
    b->blob_dradius_ = b->blob_radius_ * 2;
    b->blob_sradius_ = b->blob_radius_ * b->blob_radius_;
    b->blob_num_ = num;

    b->blob_ = (uint8_t **) vj_calloc(sizeof(uint8_t *) * b->blob_dradius_);
    b->blob_data_ = (uint8_t *) vj_calloc(sizeof(uint8_t) * b->blob_dradius_ * b->blob_dradius_);
    b->blobs_ = (blob_t *) vj_calloc(sizeof(blob_t) * b->blob_num_);

    if(!b->blob_ || !b->blob_data_ || !b->blobs_) {
        boids_release_shape(b);
        return 0;
    }

    for(int i = 0; i < b->blob_dradius_; i++)
        b->blob_[i] = b->blob_data_ + i * b->blob_dradius_;

    for(int i = -b->blob_radius_; i < b->blob_radius_; i++)
    {
        for(int j = -b->blob_radius_; j < b->blob_radius_; j++)
        {
            const int dist_sqrt = i * i + j * j;
            b->blob_[i + b->blob_radius_][j + b->blob_radius_] = dist_sqrt < b->blob_sradius_ ? 0xff : 0x0;
        }
    }

    for(int i = 0; i < b->blob_num_; i++)
        blob_init_(b, b->blobs_ + i, i, w, h);

    return 1;
}

void *boids_malloc(int w, int h)
{
    boids_t *b = (boids_t*) vj_calloc(sizeof(boids_t));

    if(!b)
        return NULL;

    b->blob_type_ = BLOB_CIRCLE;
    b->blob_home_radius_ = w / 4;

    if(!boids_reinit(b, DEFAULT_RADIUS, DEFAULT_NUM, w, h)) {
        boids_free(b);
        return NULL;
    }

    b->blob_image_ = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h);

    if(!b->blob_image_) {
        boids_free(b);
        return NULL;
    }

    veejay_memset(b->blob_image_, 0, w * h);
    b->blob_ready_ = 1;
    b->n_threads = vje_advise_num_threads(w * h);

    return b;
}

void boids_free(void *ptr)
{
    boids_t *b = (boids_t*) ptr;

    if(!b)
        return;

    boids_release_shape(b);

    if(b->blob_image_)
        free(b->blob_image_);

    free(b);
}

typedef void (*blob_func)(boids_t *b, int s, int width);

static void blob_render_circle(boids_t *b, int s, int width)
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

static void blob_render_rect(boids_t *b, int s, int width)
{
    for(int i = 0; i < b->blob_dradius_; i++)
    {
        veejay_memset(b->blob_image_ + s, 0xff, b->blob_dradius_);
        s += width;
    }
}

static blob_func blob_render(boids_t *b)
{
    return b->blob_type_ == BLOB_RECT ? blob_render_rect : blob_render_circle;
}

static void boid_rule1_(boids_t *b, int boid_id, double v1[2])
{
    double v[2] = { 0.0, 0.0 };

    for(int i = 0; i < b->blob_num_; i++)
    {
        if(i != boid_id)
        {
            v[0] += (double)b->blobs_[i].x;
            v[1] += (double)b->blobs_[i].y;
        }
    }

    v[0] /= (double)(b->blob_num_ - 1);
    v[1] /= (double)(b->blob_num_ - 1);

    v1[0] = (v[0] - (double)b->blobs_[boid_id].x) / 100.0;
    v1[1] = (v[1] - (double)b->blobs_[boid_id].y) / 100.0;
}

static void boid_rule2_(boids_t *b, int boid_id, double v1[2])
{
    double v[2] = { 0.0, 0.0 };

    for(int i = 0; i < b->blob_num_; i++)
    {
        if(i != boid_id)
        {
            const double dx = (double)b->blobs_[boid_id].x - (double)b->blobs_[i].x;
            const double dy = (double)b->blobs_[boid_id].y - (double)b->blobs_[i].y;
            const double d = dx * dx + dy * dy;

            if(d < (double)b->blob_sradius_)
            {
                v[0] += dx;
                v[1] += dy;
            }
        }
    }

    v1[0] = v[0];
    v1[1] = v[1];
}

static void boid_rule3_(boids_t *b, int boid_id, double v1[2])
{
    double v[2] = { 0.0, 0.0 };

    for(int i = 0; i < b->blob_num_; i++)
    {
        if(boid_id != i)
        {
            v[0] += b->blobs_[i].vx;
            v[1] += b->blobs_[i].vy;
        }
    }

    v[0] /= (double)(b->blob_num_ - 1);
    v[1] /= (double)(b->blob_num_ - 1);

    v1[0] = (v[0] - b->blobs_[boid_id].vx) / 8.0;
    v1[1] = (v[1] - b->blobs_[boid_id].vy) / 8.0;
}

static void boid_rule4_(boids_t *b, int boid_id, int vlim)
{
    const double vx = b->blobs_[boid_id].vx;
    const double vy = b->blobs_[boid_id].vy;
    const double v2 = vx * vx + vy * vy;
    const double lim = (double)vlim;

    if(lim > 0.0 && v2 > lim * lim)
    {
        const double s = lim / sqrt(v2);
        b->blobs_[boid_id].vx = vx * s;
        b->blobs_[boid_id].vy = vy * s;
    }
}

void boids_apply(void *ptr, VJFrame *frame, int *args)
{
    boids_t *b = (boids_t*) ptr;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    int radius = args[0];
    int num = args[1];
    int shape = args[2];
    int m1 = args[3];
    int m2 = args[4];
    int m3 = args[5];
    int speed = args[6];
    int home_radius = args[7];

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcCb = frame->data[1];
    uint8_t *restrict srcCr = frame->data[2];

    const double M1 = m1 == 0 ? 0.0 : (double)m1 / 100.0;
    const double M2 = m2 == 0 ? 0.0 : (double)m2 / 100.0;
    const double M3 = m3 == 0 ? 0.0 : (double)m3 / 1000.0;

    b->blob_type_ = shape;

    if(radius != b->blob_radius_ || num != b->blob_num_) {
        if(!boids_reinit(b, radius, num, frame->width, frame->height))
            return;
    }

    if(home_radius != b->blob_home_radius_)
    {
        b->blob_home_radius_ = home_radius;

        for(int i = 0; i < b->blob_num_; i++)
            blob_init_(b, b->blobs_ + i, i, width, height);
    }

    for(int i = 0; i < b->blob_num_; i++)
    {
        double v1[2];
        double v2[2];
        double v3[2];

        boid_rule1_(b, i, v1);
        boid_rule2_(b, i, v2);
        boid_rule3_(b, i, v3);

        b->blobs_[i].vx += v1[0] * M1 + v2[0] * M2 + v3[0] * M3;
        b->blobs_[i].vy += v1[1] * M1 + v2[1] * M2 + v3[1] * M3;

        boid_rule4_(b, i, speed);

        b->blobs_[i].x = (short)((double)b->blobs_[i].x + b->blobs_[i].vx);
        b->blobs_[i].y = (short)((double)b->blobs_[i].y + b->blobs_[i].vy);
    }

    blob_func f = blob_render(b);

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
            blob_init_(b, b->blobs_ + k, k, width, height);
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
