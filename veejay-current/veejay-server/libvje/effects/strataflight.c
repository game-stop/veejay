/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stdlib.h>
#include <math.h>

#define STRATAFLIGHT_PARAMS 11
#define SF_ZLUT_MAX 225

#define P_OPACITY    0
#define P_YAW        1
#define P_PITCH      2
#define P_DISTANCE   3
#define P_FORWARD    4
#define P_WORLDZOOM  5
#define P_HEIGHT     6
#define P_DEPOSIT    7
#define P_MEMORY     8
#define P_EROSION    9
#define P_CHROMA     10

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;

    int seeded;
    int frame;

    int cam_x_fp;
    int cam_y_fp;

    uint8_t *region;
    int *int_region;

    uint8_t *prev_y;

    uint8_t *height;
    uint8_t *height_next;

    uint8_t *mat_u;
    uint8_t *mat_v;

    int *ray_x;
    int *ray_y;

    int *row_z;
    int *row_fog;
    int *row_haze;
    int *row_bright;

    int *z_lut;
    int *z_fog_lut;
} strataflight_t;

static inline int sf_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t sf_u8(int v)
{
    return (uint8_t) sf_clampi(v, 0, 255);
}

static inline int sf_abs_i(int v)
{
    return v < 0 ? -v : v;
}

static inline int sf_blend(int oldv, int newv, int alpha)
{
    return (oldv * (256 - alpha) + newv * alpha) >> 8;
}

static inline int sf_wrap_i_fast(int v, int max)
{
    if (max <= 0)
        return 0;

    if ((unsigned int) v < (unsigned int) max)
        return v;

    if (v >= max && v < (max << 1))
        return v - max;

    if (v < 0 && v >= -max)
        return v + max;

    v %= max;
    if (v < 0)
        v += max;

    return v;
}

static inline int sf_wrap_fp(int v, int max_fp)
{
    if (max_fp <= 0)
        return 0;

    v %= max_fp;
    if (v < 0)
        v += max_fp;

    return v;
}

static inline int sf_sample_u8_feather(
    const uint8_t *buf,
    int x,
    int y,
    int w,
    int h,
    int feather
) {
    int xx;
    int yy;
    int v;

    if (x >= feather && x < w - feather &&
        y >= feather && y < h - feather) {
        return buf[y * w + x];
    }

    xx = sf_wrap_i_fast(x, w);
    yy = sf_wrap_i_fast(y, h);

    v = buf[yy * w + xx];

    if (feather > 0) {
        if (xx < feather) {
            int ox = xx + w - feather;
            int ov = buf[yy * w + sf_wrap_i_fast(ox, w)];
            int a = ((feather - xx) * 256) / feather;

            v = ((v * (256 - a)) + (ov * a)) >> 8;
        } else if (xx >= w - feather) {
            int ox = xx - w + feather;
            int ov = buf[yy * w + sf_wrap_i_fast(ox, w)];
            int a = ((xx - (w - feather)) * 256) / feather;

            v = ((v * (256 - a)) + (ov * a)) >> 8;
        }

        if (yy < feather) {
            int oy = yy + h - feather;
            int ov = buf[sf_wrap_i_fast(oy, h) * w + xx];
            int a = ((feather - yy) * 256) / feather;

            v = ((v * (256 - a)) + (ov * a)) >> 8;
        } else if (yy >= h - feather) {
            int oy = yy - h + feather;
            int ov = buf[sf_wrap_i_fast(oy, h) * w + xx];
            int a = ((yy - (h - feather)) * 256) / feather;

            v = ((v * (256 - a)) + (ov * a)) >> 8;
        }
    }

    return v;
}

vj_effect *strataflight_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if (!ve)
        return NULL;

    ve->num_params = STRATAFLIGHT_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults)
            free(ve->defaults);
        if (ve->limits[0])
            free(ve->limits[0]);
        if (ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->description = "Luma Terrain";
    ve->sub_format = 1;

    ve->defaults[P_OPACITY]   = 100;
    ve->defaults[P_YAW]       = 500;
    ve->defaults[P_PITCH]     = 680;
    ve->defaults[P_DISTANCE]  = 520;
    ve->defaults[P_FORWARD]   = 320;
    ve->defaults[P_WORLDZOOM] = 360;
    ve->defaults[P_HEIGHT]    = 54;
    ve->defaults[P_DEPOSIT]   = 62;
    ve->defaults[P_MEMORY]    = 66;
    ve->defaults[P_EROSION]   = 22;
    ve->defaults[P_CHROMA]    = 72;

    ve->limits[0][P_OPACITY]   = 0;
    ve->limits[1][P_OPACITY]   = 100;

    ve->limits[0][P_YAW]       = 0;
    ve->limits[1][P_YAW]       = 1000;

    ve->limits[0][P_PITCH]     = 0;
    ve->limits[1][P_PITCH]     = 1000;

    ve->limits[0][P_DISTANCE]  = 0;
    ve->limits[1][P_DISTANCE]  = 1000;

    ve->limits[0][P_FORWARD]   = 0;
    ve->limits[1][P_FORWARD]   = 1000;

    ve->limits[0][P_WORLDZOOM] = 0;
    ve->limits[1][P_WORLDZOOM] = 1000;

    ve->limits[0][P_HEIGHT]    = 0;
    ve->limits[1][P_HEIGHT]    = 100;

    ve->limits[0][P_DEPOSIT]   = 0;
    ve->limits[1][P_DEPOSIT]   = 100;

    ve->limits[0][P_MEMORY]    = 0;
    ve->limits[1][P_MEMORY]    = 100;

    ve->limits[0][P_EROSION]   = 0;
    ve->limits[1][P_EROSION]   = 100;

    ve->limits[0][P_CHROMA]    = 0;
    ve->limits[1][P_CHROMA]    = 100;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Camera Yaw",
        "Camera Pitch",
        "Camera Distance",
        "Forward Drift",
        "World Zoom",
        "Height Scale",
        "Source Deposit",
        "Terrain Memory",
        "Erosion",
        "Material Chroma"
    );

    return ve;
}

void *strataflight_malloc(int w, int h)
{
    strataflight_t *c;
    uint8_t *p;
    int *ip;
    size_t len;
    size_t total_u8;
    size_t total_int;

    if (w <= 0 || h <= 0)
        return NULL;

    c = (strataflight_t *) vj_calloc(sizeof(strataflight_t));
    if (!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->n_threads = vje_advise_num_threads(c->len);

    c->seeded = 0;
    c->frame = 0;

    c->cam_x_fp = w << 7;
    c->cam_y_fp = h << 7;

    len = (size_t) c->len;


    total_u8 = len * 5;

    c->region = (uint8_t *) vj_malloc(total_u8);
    if (!c->region) {
        free(c);
        return NULL;
    }

    total_int = ((size_t) w * 2) + ((size_t) h * 4) + (SF_ZLUT_MAX * 2);

    c->int_region = (int *) vj_malloc(sizeof(int) * total_int);
    if (!c->int_region) {
        free(c->region);
        free(c);
        return NULL;
    }

    p = c->region;

    c->prev_y = p;      p += len;

    c->height = p;      p += len;
    c->height_next = p; p += len;

    c->mat_u = p;       p += len;
    c->mat_v = p;

    ip = c->int_region;

    c->ray_x = ip;      ip += w;
    c->ray_y = ip;      ip += w;

    c->row_z = ip;      ip += h;
    c->row_fog = ip;    ip += h;
    c->row_haze = ip;   ip += h;
    c->row_bright = ip; ip += h;

    c->z_lut = ip;      ip += SF_ZLUT_MAX;
    c->z_fog_lut = ip;

    for (int i = 0; i < c->len; i++) {
        c->prev_y[i] = 16;

        c->height[i] = 96;
        c->height_next[i] = 96;

        c->mat_u[i] = 128;
        c->mat_v[i] = 128;
    }

    for (int i = 0; i < total_int; i++)
        c->int_region[i] = 0;

    return (void *) c;
}

void strataflight_free(void *ptr)
{
    strataflight_t *c = (strataflight_t *) ptr;

    if (!c)
        return;

    if (c->region)
        free(c->region);

    if (c->int_region)
        free(c->int_region);

    free(c);
}

void strataflight_apply(void *ptr, VJFrame *frame, int *args)
{
    strataflight_t *c = (strataflight_t *) ptr;

    uint8_t *restrict Y;
    uint8_t *restrict U;
    uint8_t *restrict V;

    uint8_t *restrict py;
    uint8_t *restrict mu;
    uint8_t *restrict mv;

    int *restrict ray_x_lut;
    int *restrict ray_y_lut;
    int *restrict row_z_lut;
    int *restrict row_fog_lut;
    int *restrict row_haze_lut;
    int *restrict row_bright_lut;
    int *restrict z_lut;
    int *restrict z_fog_lut;

    int w;
    int h;
    int rows;
    int plen;

    int opacity;
    int yaw;
    int pitch;
    int distance;
    int forward;
    int worldzoom;
    int height_scale;
    int deposit;
    int memory;
    int erosion;
    int chroma;

    int alpha;
    int chroma_q;

    int smooth_gain;
    int feed;
    int mfeed;
    int settle;
    int ridge_gain;
    int upheaval_gain;

    int horizon;
    int horizon_above;
    int cam_height;
    int projection;
    int max_z;
    int steps;
    int fov_q;
    int dist_curve;
    int dist_far;
    int world_q;
    int seam_feather;
    int use_feather;

    int half_w;
    int side_start_q12;
    int side_step_q12;

    int height_shade_floor;
    int height_shade_column;
    int elev_scale;
    int elev_damp_q;

    float angle;
    int fwd_x_q;
    int fwd_y_q;
    int right_x_q;
    int right_y_q;

    int do_seed;

    if (!c || !frame || !frame->data[0] || !frame->data[1] || !frame->data[2] || !args)
        return;

    w = c->w;
    h = c->h;

    if (w <= 0 || h <= 0 || c->len <= 0)
        return;

    plen = frame->len;
    if (plen > c->len)
        plen = c->len;

    rows = plen / w;
    if (rows <= 0)
        return;

    if (rows > h)
        rows = h;

    plen = rows * w;
    if (plen <= 0)
        return;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    py = c->prev_y;

    mu = c->mat_u;
    mv = c->mat_v;

    ray_x_lut = c->ray_x;
    ray_y_lut = c->ray_y;
    row_z_lut = c->row_z;
    row_fog_lut = c->row_fog;
    row_haze_lut = c->row_haze;
    row_bright_lut = c->row_bright;
    z_lut = c->z_lut;
    z_fog_lut = c->z_fog_lut;

    opacity      = sf_clampi(args[P_OPACITY],   0, 100);
    yaw          = sf_clampi(args[P_YAW],       0, 1000);
    pitch        = sf_clampi(args[P_PITCH],     0, 1000);
    distance     = sf_clampi(args[P_DISTANCE],  0, 1000);
    forward      = sf_clampi(args[P_FORWARD],   0, 1000);
    worldzoom    = sf_clampi(args[P_WORLDZOOM], 0, 1000);
    height_scale = sf_clampi(args[P_HEIGHT],    0, 100);
    deposit      = sf_clampi(args[P_DEPOSIT],   0, 100);
    memory       = sf_clampi(args[P_MEMORY],    0, 100);
    erosion      = sf_clampi(args[P_EROSION],   0, 100);
    chroma       = sf_clampi(args[P_CHROMA],    0, 100);

    alpha = (opacity * 256 + 50) / 100;
    chroma_q = (chroma * 256 + 50) / 100;

    ridge_gain = 48 + deposit;
    upheaval_gain = 32 + deposit;

    smooth_gain = 4 + ((erosion * 82) / 100);

    feed = 8 + ((deposit * (150 - (memory >> 1))) / 100);
    feed = sf_clampi(feed, 0, 180);

    mfeed = 4 + ((deposit * (120 - (memory >> 1))) / 100);
    if (mfeed > 140)
        mfeed = 140;

    settle = erosion >> 3;

    dist_curve = (distance * distance) / 1000;
    dist_far = (distance + dist_curve) >> 1;

    {
        int wz_curve = (worldzoom * worldzoom) / 1000;

        world_q = 192 +
                  ((worldzoom * 520) / 1000) +
                  ((wz_curve * 1336) / 1000);

        world_q = sf_clampi(world_q, 160, 2048);
    }

    use_feather = worldzoom >= 700;

    seam_feather = 4 + ((worldzoom * 20) / 1000);

    if (seam_feather > (w >> 4))
        seam_feather = w >> 4;

    if (seam_feather > (rows >> 4))
        seam_feather = rows >> 4;

    if (seam_feather < 2)
        seam_feather = 2;

    angle = (((float) yaw - 500.0f) / 1000.0f) * 6.28318530718f;

    fwd_x_q = (int) (cosf(angle) * 4096.0f);
    fwd_y_q = (int) (sinf(angle) * 4096.0f);

    right_x_q = -fwd_y_q;
    right_y_q = fwd_x_q;

    horizon_above = (rows * (8 + (((1000 - pitch) * 42) / 1000))) / 100;
    if (horizon_above < 8)
        horizon_above = 8;

    horizon = -horizon_above;

    cam_height = 34 + ((dist_far * 420) / 1000);
    projection = 64 + (rows / 8) + ((dist_far * rows) / 7600);

    max_z = 128 + ((dist_far * 2200) / 1000);

    steps = 80 + ((dist_far * 128) / 1000);
    if (steps < 56)
        steps = 56;
    if (steps > SF_ZLUT_MAX - 1)
        steps = SF_ZLUT_MAX - 1;

    fov_q = 3400 - ((dist_far * 1350) / 1000);
    fov_q = sf_clampi(fov_q, 1750, 3500);

    half_w = w >> 1;
    if (half_w <= 0)
        half_w = 1;

    side_start_q12 = -fov_q << 12;
    side_step_q12 = (fov_q << 12) / half_w;

    height_shade_floor = 24 + height_scale;
    height_shade_column = 32 + height_scale;
    elev_scale = 24 + (height_scale * 3);

    elev_damp_q = 256;
    if (worldzoom > 600) {
        elev_damp_q = 256 - ((worldzoom - 600) * 80) / 400;
        if (elev_damp_q < 128)
            elev_damp_q = 128;
    }

    do_seed = !c->seeded;

#pragma omp parallel num_threads(c->n_threads)
    {
        uint8_t *old_ht;
        uint8_t *new_ht;

        if (do_seed) {
#pragma omp for schedule(static)
            for (int i = 0; i < plen; i++) {
                py[i] = Y[i];

                c->height[i] = Y[i];
                c->height_next[i] = Y[i];

                mu[i] = U[i];
                mv[i] = V[i];
            }

#pragma omp single
            {
                c->seeded = 1;
            }
        }

        old_ht = c->height;
        new_ht = c->height_next;

#pragma omp for schedule(static)
        for (int y = 0; y < rows; y++) {
            int row = y * w;

            int yu = y > 0 ? y - 1 : rows - 1;
            int yd = y < rows - 1 ? y + 1 : 0;

            int row_u = yu * w;
            int row_d = yd * w;

            int sy_yu = y > 0 ? y - 1 : y;
            int sy_yd = y < rows - 1 ? y + 1 : y;

            int sy_row_u = sy_yu * w;
            int sy_row_d = sy_yd * w;

            for (int x = 0; x < w; x++) {
                int i = row + x;

                int hx_l = x > 0 ? x - 1 : w - 1;
                int hx_r = x < w - 1 ? x + 1 : 0;

                int sx_l = x > 0 ? x - 1 : x;
                int sx_r = x < w - 1 ? x + 1 : x;

                int old_h = old_ht[i];

                int avg_h =
                    ((int) old_ht[row + hx_l] +
                     (int) old_ht[row + hx_r] +
                     (int) old_ht[row_u + x] +
                     (int) old_ht[row_d + x]) >> 2;

                int edge =
                    sf_abs_i((int) Y[row + sx_r] - (int) Y[row + sx_l]) +
                    sf_abs_i((int) Y[sy_row_d + x] - (int) Y[sy_row_u + x]);

                int motion = sf_abs_i((int) Y[i] - (int) py[i]);

                int ridge;
                int upheaval;
                int target;
                int smoothed;
                int new_h_val;

                if (edge > 255)
                    edge = 255;

                ridge = (edge * ridge_gain) >> 7;
                upheaval = (motion * upheaval_gain) >> 7;

                target = (int) Y[i] + ridge + upheaval - 24;
                target = sf_clampi(target, 0, 255);

                smoothed = old_h + (((avg_h - old_h) * smooth_gain) >> 8);

                new_h_val = smoothed + (((target - smoothed) * feed) >> 8);

                if (settle > 0)
                    new_h_val += ((128 - new_h_val) * settle) >> 8;

                new_ht[i] = sf_u8(new_h_val);

                mu[i] = sf_u8((int) mu[i] + ((((int) U[i] - (int) mu[i]) * mfeed) >> 8));
                mv[i] = sf_u8((int) mv[i] + ((((int) V[i] - (int) mv[i]) * mfeed) >> 8));

                py[i] = Y[i];
            }
        }

#pragma omp single
        {
            uint8_t *tmp = c->height;

            c->height = c->height_next;
            c->height_next = tmp;
        }

#pragma omp for schedule(static)
        for (int x = 0; x < w; x++) {
            int side_q = (side_start_q12 + (x * side_step_q12)) >> 12;

            int rx = fwd_x_q + ((right_x_q * side_q) >> 12);
            int ry = fwd_y_q + ((right_y_q * side_q) >> 12);

            ray_x_lut[x] = (rx * world_q) >> 8;
            ray_y_lut[x] = (ry * world_q) >> 8;
        }

#pragma omp for schedule(static)
        for (int y = 0; y < rows; y++) {
            int denom = y - horizon + 1;
            int z;
            int fog;

            if (denom < 1)
                denom = 1;

            z = ((cam_height + 128) * projection) / denom;

            if (z < 3)
                z = 3;

            if (z > max_z)
                z = max_z;

            fog = (z * 138) / (max_z + 1);
            if (fog > 168)
                fog = 168;

            row_z_lut[y] = z;
            row_fog_lut[y] = fog;
            row_haze_lut[y] = 50 + ((y * 18) / rows);
            row_bright_lut[y] = (y * 28) / rows;
        }

#pragma omp for schedule(static)
        for (int s = 1; s <= steps; s++) {
            int z = 4 + ((s * s * max_z) / (steps * steps));
            int fog = (z * 146) / (max_z + 1);

            if (fog > 174)
                fog = 174;

            z_lut[s] = z;
            z_fog_lut[s] = fog;
        }

        {
            const uint8_t *render_ht = c->height;
            const uint8_t *render_mu = c->mat_u;
            const uint8_t *render_mv = c->mat_v;

            if (!use_feather) {
#pragma omp for schedule(static)
                for (int y = 0; y < rows; y++) {
                    int row = y * w;

                    int z = row_z_lut[y];
                    int fog = row_fog_lut[y];
                    int haze_y = row_haze_lut[y];
                    int row_bright = row_bright_lut[y];

                    int wx0 = c->cam_x_fp + ((ray_x_lut[0] * z) >> 4);
                    int wy0 = c->cam_y_fp + ((ray_y_lut[0] * z) >> 4);

                    int wx1 = c->cam_x_fp + ((ray_x_lut[w - 1] * z) >> 4);
                    int wy1 = c->cam_y_fp + ((ray_y_lut[w - 1] * z) >> 4);

                    int denom = w > 1 ? w - 1 : 1;

                    int wx_q16 = wx0 << 16;
                    int wy_q16 = wy0 << 16;

                    int wx_step_q16 = ((wx1 - wx0) << 16) / denom;
                    int wy_step_q16 = ((wy1 - wy0) << 16) / denom;

                    for (int x = 0; x < w; x++) {
                        int i = row + x;

                        int tx = sf_wrap_i_fast(wx_q16 >> 24, w);
                        int ty = sf_wrap_i_fast(wy_q16 >> 24, rows);

                        int xr = tx < w - 1 ? tx + 1 : 0;
                        int yd = ty < rows - 1 ? ty + 1 : 0;

                        int ti = ty * w + tx;

                        int h0 = render_ht[ti];
                        int hx1 = render_ht[ty * w + xr];
                        int hy1 = render_ht[yd * w + tx];

                        int shade = ((h0 - hx1) * 2 + (h0 - hy1)) * height_shade_floor;

                        int base_y;
                        int base_u;
                        int base_v;

                        shade >>= 8;

                        base_y = h0 + shade - 6 + row_bright;
                        base_y = sf_clampi(base_y, 0, 255);

                        base_u = 128 + ((((int) render_mu[ti] - 128) * chroma_q) >> 8);
                        base_v = 128 + ((((int) render_mv[ti] - 128) * chroma_q) >> 8);

                        base_u = sf_clampi(base_u, 0, 255);
                        base_v = sf_clampi(base_v, 0, 255);

                        base_y = ((base_y * (256 - fog)) + (haze_y * fog)) >> 8;
                        base_u = ((base_u * (256 - fog)) + (128 * fog)) >> 8;
                        base_v = ((base_v * (256 - fog)) + (128 * fog)) >> 8;

                        Y[i] = (uint8_t) base_y;
                        U[i] = (uint8_t) base_u;
                        V[i] = (uint8_t) base_v;

                        wx_q16 += wx_step_q16;
                        wy_q16 += wy_step_q16;
                    }
                }

#pragma omp for schedule(static)
                for (int x = 0; x < w; x++) {
                    int ray_x = ray_x_lut[x];
                    int ray_y = ray_y_lut[x];

                    int y_limit = rows;

                    for (int s = 1; s <= steps; s++) {
                        int z = z_lut[s];

                        int wx_fp = c->cam_x_fp + ((ray_x * z) >> 4);
                        int wy_fp = c->cam_y_fp + ((ray_y * z) >> 4);

                        int tx = sf_wrap_i_fast(wx_fp >> 8, w);
                        int ty = sf_wrap_i_fast(wy_fp >> 8, rows);

                        int ti = ty * w + tx;

                        int h0 = render_ht[ti];

                        int elev = ((h0 - 128) * elev_scale) >> 8;
                        int screen_y;

                        if (elev_damp_q != 256)
                            elev = (elev * elev_damp_q) >> 8;

                        screen_y = horizon + (((cam_height - elev) * projection) / z);

                        if (screen_y < y_limit) {
                            int xl = tx > 0 ? tx - 1 : w - 1;
                            int xr = tx < w - 1 ? tx + 1 : 0;
                            int yu = ty > 0 ? ty - 1 : rows - 1;
                            int yd = ty < rows - 1 ? ty + 1 : 0;

                            int hx0 = render_ht[ty * w + xl];
                            int hx1 = render_ht[ty * w + xr];
                            int hy0 = render_ht[yu * w + tx];
                            int hy1 = render_ht[yd * w + tx];

                            int shade = ((hx0 - hx1) * 2 + (hy0 - hy1)) * height_shade_column;

                            int base_y;
                            int base_u;
                            int base_v;

                            int fog = z_fog_lut[s];
                            int draw_from;
                            int draw_to;

                            shade >>= 8;

                            base_y = h0 + shade + 10;
                            base_y = sf_clampi(base_y, 0, 255);

                            base_u = 128 + ((((int) render_mu[ti] - 128) * chroma_q) >> 8);
                            base_v = 128 + ((((int) render_mv[ti] - 128) * chroma_q) >> 8);

                            base_u = sf_clampi(base_u, 0, 255);
                            base_v = sf_clampi(base_v, 0, 255);

                            base_y = ((base_y * (256 - fog)) + (56 * fog)) >> 8;
                            base_u = ((base_u * (256 - fog)) + (128 * fog)) >> 8;
                            base_v = ((base_v * (256 - fog)) + (128 * fog)) >> 8;

                            draw_from = screen_y;
                            draw_to = y_limit;

                            if (draw_from < 0)
                                draw_from = 0;

                            if (draw_to > rows)
                                draw_to = rows;

                            if (draw_from < draw_to) {
                                int span_len = draw_to - draw_from + 1;
                                int dark_acc = 0;
                                int dark_step = (42 << 8) / span_len;

                                if (alpha >= 256) {
                                    for (int yy = draw_from; yy < draw_to; yy++) {
                                        int oi = yy * w + x;

                                        int wall_dark = dark_acc >> 8;
                                        int ey = base_y - wall_dark;

                                        dark_acc += dark_step;

                                        Y[oi] = (uint8_t) sf_clampi(ey, 0, 255);
                                        U[oi] = (uint8_t) base_u;
                                        V[oi] = (uint8_t) base_v;
                                    }
                                } else if (alpha > 0) {
                                    for (int yy = draw_from; yy < draw_to; yy++) {
                                        int oi = yy * w + x;

                                        int wall_dark = dark_acc >> 8;
                                        int ey = base_y - wall_dark;

                                        dark_acc += dark_step;

                                        ey = sf_clampi(ey, 0, 255);

                                        Y[oi] = (uint8_t) sf_blend((int) Y[oi], ey, alpha);
                                        U[oi] = (uint8_t) sf_blend((int) U[oi], base_u, alpha);
                                        V[oi] = (uint8_t) sf_blend((int) V[oi], base_v, alpha);
                                    }
                                }
                            }

                            y_limit = screen_y;

                            if (y_limit <= 0)
                                break;
                        }
                    }
                }
            } else {
#pragma omp for schedule(static)
                for (int y = 0; y < rows; y++) {
                    int row = y * w;

                    int z = row_z_lut[y];
                    int fog = row_fog_lut[y];
                    int haze_y = row_haze_lut[y];
                    int row_bright = row_bright_lut[y];

                    int wx0 = c->cam_x_fp + ((ray_x_lut[0] * z) >> 4);
                    int wy0 = c->cam_y_fp + ((ray_y_lut[0] * z) >> 4);

                    int wx1 = c->cam_x_fp + ((ray_x_lut[w - 1] * z) >> 4);
                    int wy1 = c->cam_y_fp + ((ray_y_lut[w - 1] * z) >> 4);

                    int denom = w > 1 ? w - 1 : 1;

                    int wx_q16 = wx0 << 16;
                    int wy_q16 = wy0 << 16;

                    int wx_step_q16 = ((wx1 - wx0) << 16) / denom;
                    int wy_step_q16 = ((wy1 - wy0) << 16) / denom;

                    for (int x = 0; x < w; x++) {
                        int i = row + x;

                        int tx = sf_wrap_i_fast(wx_q16 >> 24, w);
                        int ty = sf_wrap_i_fast(wy_q16 >> 24, rows);

                        int h0 = sf_sample_u8_feather(render_ht, tx, ty, w, rows, seam_feather);

                        int hx1 = sf_sample_u8_feather(render_ht, tx + 1, ty, w, rows, seam_feather);
                        int hy1 = sf_sample_u8_feather(render_ht, tx, ty + 1, w, rows, seam_feather);

                        int shade = ((h0 - hx1) * 2 + (h0 - hy1)) * height_shade_floor;

                        int base_y;
                        int base_u;
                        int base_v;

                        shade >>= 8;

                        base_y = h0 + shade - 6 + row_bright;
                        base_y = sf_clampi(base_y, 0, 255);

                        base_u =
                            128 +
                            ((sf_sample_u8_feather(render_mu, tx, ty, w, rows, seam_feather) - 128) * chroma_q >> 8);

                        base_v =
                            128 +
                            ((sf_sample_u8_feather(render_mv, tx, ty, w, rows, seam_feather) - 128) * chroma_q >> 8);

                        base_u = sf_clampi(base_u, 0, 255);
                        base_v = sf_clampi(base_v, 0, 255);

                        base_y = ((base_y * (256 - fog)) + (haze_y * fog)) >> 8;
                        base_u = ((base_u * (256 - fog)) + (128 * fog)) >> 8;
                        base_v = ((base_v * (256 - fog)) + (128 * fog)) >> 8;

                        Y[i] = (uint8_t) base_y;
                        U[i] = (uint8_t) base_u;
                        V[i] = (uint8_t) base_v;

                        wx_q16 += wx_step_q16;
                        wy_q16 += wy_step_q16;
                    }
                }

#pragma omp for schedule(static)
                for (int x = 0; x < w; x++) {
                    int ray_x = ray_x_lut[x];
                    int ray_y = ray_y_lut[x];

                    int y_limit = rows;

                    for (int s = 1; s <= steps; s++) {
                        int z = z_lut[s];

                        int wx_fp = c->cam_x_fp + ((ray_x * z) >> 4);
                        int wy_fp = c->cam_y_fp + ((ray_y * z) >> 4);

                        int tx = sf_wrap_i_fast(wx_fp >> 8, w);
                        int ty = sf_wrap_i_fast(wy_fp >> 8, rows);

                        int h0 = sf_sample_u8_feather(render_ht, tx, ty, w, rows, seam_feather);

                        int elev = ((h0 - 128) * elev_scale) >> 8;
                        int screen_y;

                        if (elev_damp_q != 256)
                            elev = (elev * elev_damp_q) >> 8;

                        screen_y = horizon + (((cam_height - elev) * projection) / z);

                        if (screen_y < y_limit) {
                            int hx0 = sf_sample_u8_feather(render_ht, tx - 1, ty, w, rows, seam_feather);
                            int hx1 = sf_sample_u8_feather(render_ht, tx + 1, ty, w, rows, seam_feather);
                            int hy0 = sf_sample_u8_feather(render_ht, tx, ty - 1, w, rows, seam_feather);
                            int hy1 = sf_sample_u8_feather(render_ht, tx, ty + 1, w, rows, seam_feather);

                            int shade = ((hx0 - hx1) * 2 + (hy0 - hy1)) * height_shade_column;

                            int base_y;
                            int base_u;
                            int base_v;

                            int fog = z_fog_lut[s];
                            int draw_from;
                            int draw_to;

                            shade >>= 8;

                            base_y = h0 + shade + 10;
                            base_y = sf_clampi(base_y, 0, 255);

                            base_u =
                                128 +
                                ((sf_sample_u8_feather(render_mu, tx, ty, w, rows, seam_feather) - 128) * chroma_q >> 8);

                            base_v =
                                128 +
                                ((sf_sample_u8_feather(render_mv, tx, ty, w, rows, seam_feather) - 128) * chroma_q >> 8);

                            base_u = sf_clampi(base_u, 0, 255);
                            base_v = sf_clampi(base_v, 0, 255);

                            base_y = ((base_y * (256 - fog)) + (56 * fog)) >> 8;
                            base_u = ((base_u * (256 - fog)) + (128 * fog)) >> 8;
                            base_v = ((base_v * (256 - fog)) + (128 * fog)) >> 8;

                            draw_from = screen_y;
                            draw_to = y_limit;

                            if (draw_from < 0)
                                draw_from = 0;

                            if (draw_to > rows)
                                draw_to = rows;

                            if (draw_from < draw_to) {
                                int span_len = draw_to - draw_from + 1;
                                int dark_acc = 0;
                                int dark_step = (42 << 8) / span_len;

                                if (alpha >= 256) {
                                    for (int yy = draw_from; yy < draw_to; yy++) {
                                        int oi = yy * w + x;

                                        int wall_dark = dark_acc >> 8;
                                        int ey = base_y - wall_dark;

                                        dark_acc += dark_step;

                                        Y[oi] = (uint8_t) sf_clampi(ey, 0, 255);
                                        U[oi] = (uint8_t) base_u;
                                        V[oi] = (uint8_t) base_v;
                                    }
                                } else if (alpha > 0) {
                                    for (int yy = draw_from; yy < draw_to; yy++) {
                                        int oi = yy * w + x;

                                        int wall_dark = dark_acc >> 8;
                                        int ey = base_y - wall_dark;

                                        dark_acc += dark_step;

                                        ey = sf_clampi(ey, 0, 255);

                                        Y[oi] = (uint8_t) sf_blend((int) Y[oi], ey, alpha);
                                        U[oi] = (uint8_t) sf_blend((int) U[oi], base_u, alpha);
                                        V[oi] = (uint8_t) sf_blend((int) V[oi], base_v, alpha);
                                    }
                                }
                            }

                            y_limit = screen_y;

                            if (y_limit <= 0)
                                break;
                        }
                    }
                }
            }
        }
    }

    {
        int speed_fp = (forward * forward * 12) / 10000;

        speed_fp = (speed_fp * world_q) >> 8;

        c->cam_x_fp += (fwd_x_q * speed_fp) >> 12;
        c->cam_y_fp += (fwd_y_q * speed_fp) >> 12;

        c->cam_x_fp = sf_wrap_fp(c->cam_x_fp, w << 8);
        c->cam_y_fp = sf_wrap_fp(c->cam_y_fp, rows << 8);
    }

    c->frame++;
}