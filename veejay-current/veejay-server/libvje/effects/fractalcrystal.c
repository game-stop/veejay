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
#include "fractalcrystal.h"

#define FRACTALCRYSTAL_PARAMS 10
#define FC_TWO_PI 6.28318530718f
#define FC_INV_TWO_PI 0.15915494309f
#define FC_TRIG_LUT_SIZE 1024
#define FC_TRIG_LUT_MASK 1023
#define FC_LUT_SCALE 162.974661726f
#define FC_BLUR_TILE 32

#define P_ZOOM        0
#define P_DENSITY     1
#define P_ITER        2
#define P_WARP        3
#define P_FACET       4
#define P_SILHOUETTE  5
#define P_MIX         6
#define P_CHROMA      7
#define P_PULSE       8
#define P_SPEED       9

typedef struct {
    float nx;
    float ny;
    float shift_x;
    float shift_y;
    float rot_c;
    float rot_s;
} fc_cell_t;

typedef struct {
    int w;
    int h;
    int len;
    int max_cell_cols;
    int max_cell_rows;
    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *blur;
    uint8_t *tmp;
    float *site_x;
    float *site_y;
    fc_cell_t *cells;
    float sin_lut[FC_TRIG_LUT_SIZE];
    float cos_lut[FC_TRIG_LUT_SIZE];
    float phase;
} fractalcrystal_t;

static inline int fc_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fc_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fc_absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float fc_smooth01(float x)
{
    x = fc_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline uint32_t fc_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float fc_hash01(int x, int y, uint32_t salt)
{
    uint32_t h = (uint32_t) x * 0x9e3779b1U ^ (uint32_t) y * 0x85ebca6bU ^ salt;
    return (float) (fc_hash_u32(h) & 0x00ffffffU) * (1.0f / 16777215.0f);
}

static inline float fc_wrap_2pi(float v)
{
    if (v >= FC_TWO_PI || v < 0.0f) {
        int k = (int) (v * FC_INV_TWO_PI);
        v -= (float) k * FC_TWO_PI;
        if (v < 0.0f)
            v += FC_TWO_PI;
        else if (v >= FC_TWO_PI)
            v -= FC_TWO_PI;
    }
    return v;
}

static inline float fc_lut_sin(const fractalcrystal_t *t, float phase)
{
    return t->sin_lut[((int) (phase * FC_LUT_SCALE)) & FC_TRIG_LUT_MASK];
}

static inline float fc_lut_cos(const fractalcrystal_t *t, float phase)
{
    return t->cos_lut[((int) (phase * FC_LUT_SCALE)) & FC_TRIG_LUT_MASK];
}

static inline float fc_pingpong(float phase)
{
    float p = fc_wrap_2pi(phase) * FC_INV_TWO_PI;
    float tri = 1.0f - fc_absf(2.0f * p - 1.0f);
    tri = tri * tri * (3.0f - 2.0f * tri);
    return tri * 2.0f - 1.0f;
}

static inline float fc_time_step(int speed)
{
    float u;
    float mag;

    if (speed == 0)
        return 0.0f;

    u = fc_clampf(fc_absf((float) speed) * 0.001f, 0.0f, 1.0f);
    mag = 0.00016f * (exp2f(10.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static void fc_box_blur(
    fractalcrystal_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst,
    int radius
) {
    const int w = t->w;
    const int h = t->h;
    const int window = radius * 2 + 1;
    const int recip = (65536 + window / 2) / window;
    uint8_t *restrict tmp = t->tmp;
    int y;

#pragma omp for schedule(static)
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + y * w;
        uint8_t *out = tmp + y * w;
        int sum = row[0] * (radius + 1);
        int x;

        for (x = 1; x <= radius; x++)
            sum += row[x < w ? x : w - 1];

        for (x = 0; x < w; x++) {
            int subx = x - radius;
            int addx = x + radius + 1;

            out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
            if (subx < 0)
                subx = 0;
            if (addx >= w)
                addx = w - 1;
            sum += row[addx] - row[subx];
        }
    }

#pragma omp for schedule(static)
    for (int xb = 0; xb < w; xb += FC_BLUR_TILE) {
        int sums[FC_BLUR_TILE];
        int count = w - xb;

        if (count > FC_BLUR_TILE)
            count = FC_BLUR_TILE;

        for (int k = 0; k < count; k++) {
            int x = xb + k;
            int sum = tmp[x] * (radius + 1);

            for (int yy = 1; yy <= radius; yy++) {
                int sy = yy < h ? yy : h - 1;
                sum += tmp[sy * w + x];
            }
            sums[k] = sum;
        }

        for (int yy = 0; yy < h; yy++) {
            int suby = yy > radius ? yy - radius : 0;
            int addy = yy + radius + 1;
            int outrow = yy * w + xb;
            int subrow;
            int addrow;

            if (addy >= h)
                addy = h - 1;
            subrow = suby * w + xb;
            addrow = addy * w + xb;

            for (int k = 0; k < count; k++) {
                dst[outrow + k] = (uint8_t) ((sums[k] * recip + 32768) >> 16);
                sums[k] += tmp[addrow + k] - tmp[subrow + k];
            }
        }
    }
}

static inline void fc_sample_bilinear_y(
    const uint8_t *restrict Y,
    float fx,
    float fy,
    int w,
    int h,
    int *oy
) {
    int x0;
    int y0;
    int x1;
    int y1;
    int wx;
    int wy;
    int p00;
    int p10;
    int p01;
    int p11;
    int a;
    int b;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);
    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    x0 = (int) fx;
    y0 = (int) fy;
    x1 = x0 + 1;
    y1 = y0 + 1;
    if (x1 >= w)
        x1 = w - 1;
    if (y1 >= h)
        y1 = h - 1;

    wx = (int) ((fx - (float) x0) * 256.0f);
    wy = (int) ((fy - (float) y0) * 256.0f);

    p00 = Y[y0 * w + x0];
    p10 = Y[y0 * w + x1];
    p01 = Y[y1 * w + x0];
    p11 = Y[y1 * w + x1];
    a = p00 * (256 - wx) + p10 * wx;
    b = p01 * (256 - wx) + p11 * wx;
    *oy = (a * (256 - wy) + b * wy + 32768) >> 16;
}

static inline int fc_nearest_index(
    float fx,
    float fy,
    int w,
    int h
) {
    int x = (int) (fx + 0.5f);
    int y = (int) (fy + 0.5f);

    x = fc_clampi(x, 0, w - 1);
    y = fc_clampi(y, 0, h - 1);
    return y * w + x;
}

static inline size_t fc_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

vj_effect *fractalcrystal_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = FRACTALCRYSTAL_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Fractal Crystal Shards";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_ZOOM]       = 48;
    ve->defaults[P_DENSITY]    = 62;
    ve->defaults[P_ITER]       = 58;
    ve->defaults[P_WARP]       = 52;
    ve->defaults[P_FACET]      = 72;
    ve->defaults[P_SILHOUETTE] = 54;
    ve->defaults[P_MIX]        = 76;
    ve->defaults[P_CHROMA]     = 42;
    ve->defaults[P_PULSE]      = 44;
    ve->defaults[P_SPEED]      = 180;

    ve->limits[0][P_ZOOM]       = 0;
    ve->limits[1][P_ZOOM]       = 100;
    ve->limits[0][P_DENSITY]    = 0;
    ve->limits[1][P_DENSITY]    = 100;
    ve->limits[0][P_ITER]       = 0;
    ve->limits[1][P_ITER]       = 100;
    ve->limits[0][P_WARP]       = 0;
    ve->limits[1][P_WARP]       = 100;
    ve->limits[0][P_FACET]      = 0;
    ve->limits[1][P_FACET]      = 100;
    ve->limits[0][P_SILHOUETTE] = 0;
    ve->limits[1][P_SILHOUETTE] = 100;
    ve->limits[0][P_MIX]        = 0;
    ve->limits[1][P_MIX]        = 100;
    ve->limits[0][P_CHROMA]     = 0;
    ve->limits[1][P_CHROMA]     = 100;
    ve->limits[0][P_PULSE]      = 0;
    ve->limits[1][P_PULSE]      = 100;
    ve->limits[0][P_SPEED]      = -1000;
    ve->limits[1][P_SPEED]      = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shard Scale",
        "Cell Irregularity",
        "Fracture",
        "Shard Displace",
        "Facet Angle",
        "Source Fracture",
        "Crystal Mix",
        "Prism Refraction",
        "Shard Motion",
        "Time Scale"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_KICK_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 40, 80, 28, 46, 20, 250, 0, 1, 0, VJ_BEAT_COST_CHEAP, 150, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_HAT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HAT_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 30, 76, 18, 34, 16, 120, 0, 1, 0, VJ_BEAT_COST_CHEAP, 115, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BEAT_GATE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 30, 78, 34, 54, 20, 200, 0, 1, 0, VJ_BEAT_COST_CHEAP, 180, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *fractalcrystal_malloc(int w, int h)
{
    fractalcrystal_t *t = NULL;
    unsigned char *base = NULL;
    size_t len = (size_t) w * (size_t) h;
    int max_cols = (w + 7) / 8 + 3;
    int max_rows = (h + 7) / 8 + 3;
    size_t pixel_bytes = len * 5;
    size_t cell_count = (size_t) max_cols * (size_t) max_rows;
    size_t site_x_off = fc_align_size(pixel_bytes, sizeof(float));
    size_t site_y_off = site_x_off + cell_count * sizeof(float);
    size_t cell_off = site_y_off + cell_count * sizeof(float);
    size_t total = cell_off + cell_count * sizeof(fc_cell_t);
    size_t off = 0;

    t = (fractalcrystal_t *) vj_calloc(sizeof(fractalcrystal_t));
    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->max_cell_cols = max_cols;
    t->max_cell_rows = max_rows;
    t->phase = 0.0f;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    base = (unsigned char *) t->region;
    t->src_y = (uint8_t *) (base + off); off += len;
    t->src_u = (uint8_t *) (base + off); off += len;
    t->src_v = (uint8_t *) (base + off); off += len;
    t->blur  = (uint8_t *) (base + off); off += len;
    t->tmp   = (uint8_t *) (base + off);
    t->site_x = (float *) (base + site_x_off);
    t->site_y = (float *) (base + site_y_off);
    t->cells = (fc_cell_t *) (base + cell_off);

    for (int i = 0; i < FC_TRIG_LUT_SIZE; i++) {
        float a = FC_TWO_PI * (float) i / (float) FC_TRIG_LUT_SIZE;
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    return (void *) t;
}


void fractalcrystal_free(void *ptr)
{
    fractalcrystal_t *t = (fractalcrystal_t *) ptr;

    if (!t)
        return;

    if (t->region)
        free(t->region);

    free(t);
}

void fractalcrystal_apply(void *ptr, VJFrame *frame, int *args)
{
    fractalcrystal_t *t = (fractalcrystal_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    uint8_t *restrict blur = t->blur;
    float *restrict site_x = t->site_x;
    float *restrict site_y = t->site_y;
    fc_cell_t *restrict cells = t->cells;
    const int w = t->w;
    const int h = t->h;
    const int len = t->len;
    int scale_i = args[P_ZOOM];
    int irregular_i = args[P_DENSITY];
    int fracture_i = args[P_ITER];
    int displace_i = args[P_WARP];
    int facet_i = args[P_FACET];
    int source_i = args[P_SILHOUETTE];
    int mix_i = args[P_MIX];
    int prism_i = args[P_CHROMA];
    int motion_i = args[P_PULSE];
    int speed_i = args[P_SPEED];
    float scale_t = (float) scale_i * 0.01f;
    float irregular = (float) irregular_i * 0.01f;
    float fracture = (float) fracture_i * 0.01f;
    float displace = (float) displace_i * 0.01f;
    float facet = (float) facet_i * 0.01f;
    float source_gain = (float) source_i * 0.01f;
    float mix = (float) mix_i * 0.01f;
    float prism = (float) prism_i * 0.01f;
    float motion = (float) motion_i * 0.01f;
    float cell_size = 8.0f * exp2f(scale_t * 4.30f);
    float jitter = irregular * 0.43f;
    float drift = motion * 0.28f;
    float crack_width = 0.008f + fracture * 0.105f;
    float inv_crack_width = 1.0f / crack_width;
    float displace_gain = displace * displace;
    float inv_cell_size = 1.0f / cell_size;
    float source_shape = 0.45f + source_gain * 2.25f;
    float facet_plane_gain = 0.65f + facet * 3.4f;
    float prism_base = prism * cell_size * (0.035f + 0.135f * facet);
    float facet_luma_gain = facet * 138.0f;
    float fracture_dark = fracture * (38.0f + 122.0f * mix);
    float source_highlight = facet * 30.0f;
    int cell_cols = (int) ceilf((float) w * inv_cell_size) + 3;
    int cell_rows = (int) ceilf((float) h * inv_cell_size) + 3;
    int blur_r = 4 + (int) (cell_size * 0.055f);
    int y;

#pragma omp single
    {
        veejay_memcpy(src_y, Y, len);
        veejay_memcpy(src_u, U, len);
        veejay_memcpy(src_v, V, len);
    }

    if (blur_r > 18)
        blur_r = 18;
    fc_box_blur(t, src_y, blur, blur_r);

#pragma omp single
    {
        t->phase = fc_wrap_2pi(t->phase + fc_time_step(speed_i));
    }

    {
        float travel = fc_pingpong(t->phase);

#pragma omp for schedule(static)
    for (int iy = 0; iy < cell_rows; iy++) {
        int cy = iy - 1;
        int ix;

        for (ix = 0; ix < cell_cols; ix++) {
            int cx = ix - 1;
            int ci = iy * cell_cols + ix;
            float h0 = fc_hash01(cx, cy, 0x7135U);
            float h1 = fc_hash01(cx, cy, 0xb529U);
            float h2 = fc_hash01(cx, cy, 0x51edU);
            float h3 = fc_hash01(cx, cy, 0xa531U);
            float hm = fc_hash01(cx, cy, 0x4a91U);
            float normal_angle = h0 * FC_TWO_PI;
            float motion_axis = h2 * FC_TWO_PI;
            float motion_sign = hm < 0.5f ? -1.0f : 1.0f;
            float local_travel = travel * motion_sign;
            float shard_angle = (h1 * 2.0f - 1.0f) * FC_TWO_PI
                              + local_travel * motion * (0.38f + 0.42f * h3);
            float shift = cell_size * displace_gain * (0.18f + 0.56f * h2);
            float rot = (h3 - 0.5f) * displace * 0.72f
                      + local_travel * motion * displace * (0.18f + 0.22f * h0);
            float local_drift = drift * (0.62f + 0.38f * h1);
            fc_cell_t *c = &cells[ci];

            site_x[ci] = (float) cx + 0.5f + (h0 - 0.5f) * 2.0f * jitter
                       + fc_lut_cos(t, motion_axis) * local_travel * local_drift;
            site_y[ci] = (float) cy + 0.5f + (h1 - 0.5f) * 2.0f * jitter
                       + fc_lut_sin(t, motion_axis) * local_travel * local_drift;
            c->nx = fc_lut_cos(t, normal_angle);
            c->ny = fc_lut_sin(t, normal_angle);
            c->shift_x = fc_lut_cos(t, shard_angle) * shift;
            c->shift_y = fc_lut_sin(t, shard_angle) * shift;
            c->rot_c = fc_lut_cos(t, rot);
            c->rot_s = fc_lut_sin(t, rot);
        }
    }
    }

#pragma omp for schedule(static)
    for (y = 0; y < h; y++) {
        int row = y * w;
        float gy = (float) y * inv_cell_size;
        int by = (int) gy;
        int x;

        for (x = 0; x < w; x++) {
            int i = row + x;
            float gx = (float) x * inv_cell_size;
            int bx = (int) gx;
            float best = 1.0e9f;
            float second = 1.0e9f;
            int best_ci = 0;
            float best_dx = 0.0f;
            float best_dy = 0.0f;
            int oy;
            int ox;

            for (oy = -1; oy <= 1; oy++) {
                int cy = by + oy;
                int iy = cy + 1;

                for (ox = -1; ox <= 1; ox++) {
                    int cx = bx + ox;
                    int ix = cx + 1;
                    int ci = iy * cell_cols + ix;
                    float dx0 = gx - site_x[ci];
                    float dy0 = gy - site_y[ci];
                    float d = dx0 * dx0 + dy0 * dy0;

                    if (d < best) {
                        second = best;
                        best = d;
                        best_ci = ci;
                        best_dx = dx0;
                        best_dy = dy0;
                    }
                    else if (d < second) {
                        second = d;
                    }
                }
            }

            {
                const fc_cell_t *c = &cells[best_ci];
                float gap = second - best;
                float crack = 1.0f - fc_smooth01(gap * inv_crack_width);
                int source_delta = (int) src_y[i] - (int) blur[i];
                float source_edge;

                if (source_delta < 0)
                    source_delta = -source_delta;
                source_edge = fc_clampf((float) source_delta * (1.0f / 38.0f), 0.0f, 1.0f);
                float source_crack = fc_smooth01(source_edge * source_shape) * source_gain;
                float crack_total = crack > source_crack ? crack : source_crack;
                float plane = best_dx * c->nx + best_dy * c->ny;
                float facet_light = fc_clampf(0.52f + plane * facet_plane_gain, 0.0f, 1.0f);
                float local_x = best_dx * cell_size;
                float local_y = best_dy * cell_size;
                float rx = local_x * c->rot_c - local_y * c->rot_s;
                float ry = local_x * c->rot_s + local_y * c->rot_c;
                float sx = (float) x + rx - local_x + c->shift_x;
                float sy = (float) y + ry - local_y + c->shift_y;
                float crystal_mix = mix * (0.42f + 0.58f * fc_smooth01(0.30f + facet_light * 0.70f));
                float prism_px = prism_base * (0.35f + 0.65f * crystal_mix);
                int sample_y;
                int base_u;
                int base_v;
                int prism_u;
                int prism_v;
                int outy;
                int outu;
                int outv;
                int center_i;

                fc_sample_bilinear_y(src_y, sx, sy, w, h, &sample_y);
                center_i = fc_nearest_index(sx, sy, w, h);
                base_u = src_u[center_i];
                base_v = src_v[center_i];
                prism_u = base_u;
                prism_v = base_v;

                if (prism_px > 0.05f) {
                    float px = c->nx * prism_px;
                    float py = c->ny * prism_px;
                    int plus_i = fc_nearest_index(sx + px, sy + py, w, h);
                    int minus_i = fc_nearest_index(sx - px, sy - py, w, h);

                    prism_u = src_u[minus_i];
                    prism_v = src_v[plus_i];
                }

                outy = (int) ((float) src_y[i] * (1.0f - mix) + (float) sample_y * mix + 0.5f);
                outy += (int) ((facet_light - 0.5f) * facet_luma_gain * crystal_mix);
                outy -= (int) (crack_total * fracture_dark);
                outy += (int) (source_crack * source_highlight);
                outy = fc_clampi(outy, 0, 255);

                outu = (int) ((float) src_u[i] * (1.0f - mix) + (float) base_u * mix + 0.5f);
                outv = (int) ((float) src_v[i] * (1.0f - mix) + (float) base_v * mix + 0.5f);
                outu += (int) ((float) (prism_u - base_u) * prism * crystal_mix);
                outv += (int) ((float) (prism_v - base_v) * prism * crystal_mix);
                outu = fc_clampi(outu, 0, 255);
                outv = fc_clampi(outv, 0, 255);

                Y[i] = (uint8_t) outy;
                U[i] = (uint8_t) outu;
                V[i] = (uint8_t) outv;
            }
        }
    }
}
