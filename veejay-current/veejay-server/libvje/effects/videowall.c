/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "videowall.h"

#define VIDEOWALL_PARAMS 5

#define P_PHOTO_SLOT  0
#define P_X_DISPLACE  1
#define P_Y_DISPLACE  2
#define P_LOCK_UPDATE 3
#define P_SLIDE_DRIVE 4

typedef struct {
    picture_t *pictures;
    uint8_t *photo_region;
    int *offset_region;
    int *sample_region;
    int num_photos;
    int frame_counter;
    int *offset_table_x;
    int *offset_table_y;
    int *sample_x0;
    int *sample_x1;
    int *sample_y0;
    int *sample_y1;
    int *sample_area;
    int box_w;
    int box_h;
    int n_threads;
    int slide_env_q8;
    int slide_phase;
    uint8_t lift_lut[33][256];
} videowall_t;

static void destroy_filmstrip(videowall_t *vw);

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int wrapi(int v, int max)
{
    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline int triwave12(int phase)
{
    phase &= 4095;

    if(phase < 1024)
        return phase;

    if(phase < 3072)
        return 2048 - phase;

    return phase - 4096;
}

static inline int scale_wave(int amp, int wave, int div)
{
    const int v = amp * wave;

    if(v >= 0)
        return (v + (div >> 1)) / div;

    return -(((-v) + (div >> 1)) / div);
}

static inline int smooth_q8(int current, int target)
{
    const int diff = target - current;

    if(diff >= 0)
        return current + ((diff * 29 + 128) >> 8);

    return current - (((-diff) * 29 + 128) >> 8);
}

static int videowall_gcd(int a, int b)
{
    while(b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }

    return a;
}

static int videowall_num_pics(int w, int h)
{
    const int g = videowall_gcd(w, h);

    return (w / g) * 2;
}

vj_effect *videowall_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = VIDEOWALL_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    const int photos = videowall_num_pics(w, h);
    const int max_photo = photos - 1;

    ve->limits[0][P_PHOTO_SLOT] = 0;
    ve->limits[1][P_PHOTO_SLOT] = max_photo;

    ve->limits[0][P_X_DISPLACE] = 0;
    ve->limits[1][P_X_DISPLACE] = w;

    ve->limits[0][P_Y_DISPLACE] = 0;
    ve->limits[1][P_Y_DISPLACE] = h;

    ve->limits[0][P_LOCK_UPDATE] = 0;
    ve->limits[1][P_LOCK_UPDATE] = 1;

    ve->limits[0][P_SLIDE_DRIVE] = 0;
    ve->limits[1][P_SLIDE_DRIVE] = 1000;

    ve->defaults[P_PHOTO_SLOT] = 0;
    ve->defaults[P_X_DISPLACE] = 1;
    ve->defaults[P_Y_DISPLACE] = 1;
    ve->defaults[P_LOCK_UPDATE] = 0;
    ve->defaults[P_SLIDE_DRIVE] = 0;

    ve->description = "VideoWall / Tile Placement";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photo Slot",
        "X Displacement",
        "Y Displacement",
        "Lock Update",
        "Slide Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_LOCK_UPDATE],
        P_LOCK_UPDATE,
        "Update Slot",
        "Locked"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,   -1000,
        VJ_BEAT_DRIFT,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,   -1000,
        VJ_BEAT_DRIFT,    VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,   -1000,
        VJ_BEAT_DRIFT,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               24, 72, 90,   1800, 0,   94
    );

    return ve;
}

static void release_filmstrip(videowall_t *vw)
{
    free(vw->photo_region);
    free(vw->offset_region);
    free(vw->sample_region);
    free(vw->pictures);

    vw->photo_region = NULL;
    vw->offset_region = NULL;
    vw->sample_region = NULL;
    vw->pictures = NULL;
    vw->offset_table_x = NULL;
    vw->offset_table_y = NULL;
    vw->sample_x0 = NULL;
    vw->sample_x1 = NULL;
    vw->sample_y0 = NULL;
    vw->sample_y1 = NULL;
    vw->sample_area = NULL;
    vw->num_photos = 0;
    vw->frame_counter = 0;
    vw->box_w = 0;
    vw->box_h = 0;
}

static void destroy_filmstrip(videowall_t *vw)
{
    release_filmstrip(vw);
    free(vw);
}

static void build_lift_lut(videowall_t *vw)
{
    for(int lift = 0; lift <= 32; lift++) {
        for(int v = 0; v < 256; v++) {
            const int r = v + lift;
            vw->lift_lut[lift][v] = (uint8_t)((r > 255) ? 255 : r);
        }
    }
}

static void build_sample_map(videowall_t *vw, int src_w, int src_h)
{
    const int box_w = vw->box_w;
    const int box_h = vw->box_h;

    for(int x = 0; x < box_w; x++) {
        const int x0 = (x * src_w) / box_w;
        int x1 = ((x + 1) * src_w) / box_w;

        if(x1 <= x0)
            x1 = x0 + 1;

        if(x1 > src_w)
            x1 = src_w;

        vw->sample_x0[x] = x0;
        vw->sample_x1[x] = x1;
    }

    for(int y = 0; y < box_h; y++) {
        const int y0 = (y * src_h) / box_h;
        int y1 = ((y + 1) * src_h) / box_h;

        if(y1 <= y0)
            y1 = y0 + 1;

        if(y1 > src_h)
            y1 = src_h;

        vw->sample_y0[y] = y0;
        vw->sample_y1[y] = y1;
    }

    for(int y = 0; y < box_h; y++) {
        const int ys = vw->sample_y1[y] - vw->sample_y0[y];
        int *restrict area = vw->sample_area + y * box_w;

        for(int x = 0; x < box_w; x++)
            area[x] = ys * (vw->sample_x1[x] - vw->sample_x0[x]);
    }
}

static void *prepare_filmstrip(int w, int h)
{
    const int g = videowall_gcd(w, h);
    const int picture_width = g;
    const int picture_height = g;
    const int film_length = videowall_num_pics(w, h);
    const size_t plane_len = (size_t)picture_width * (size_t)picture_height;
    const size_t frame_len = plane_len * 3u;
    const size_t sample_len = ((size_t)picture_width * 2u) + ((size_t)picture_height * 2u) + plane_len;
    uint8_t *planes;

    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    if(!vw)
        return NULL;

    vw->pictures = (picture_t*) vj_calloc(sizeof(picture_t) * film_length);
    if(!vw->pictures) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->photo_region = (uint8_t*) vj_malloc(frame_len * (size_t)film_length);
    if(!vw->photo_region) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->offset_region = (int*) vj_calloc(sizeof(int) * (size_t)film_length * 2u);
    if(!vw->offset_region) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->sample_region = (int*) vj_malloc(sizeof(int) * sample_len);
    if(!vw->sample_region) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->offset_table_x = vw->offset_region;
    vw->offset_table_y = vw->offset_table_x + film_length;
    vw->sample_x0 = vw->sample_region;
    vw->sample_x1 = vw->sample_x0 + picture_width;
    vw->sample_y0 = vw->sample_x1 + picture_width;
    vw->sample_y1 = vw->sample_y0 + picture_height;
    vw->sample_area = vw->sample_y1 + picture_height;
    vw->num_photos = film_length;
    vw->box_w = picture_width;
    vw->box_h = picture_height;
    vw->frame_counter = 0;
    vw->slide_env_q8 = 0;
    vw->slide_phase = 0;
    vw->n_threads = vje_advise_num_threads(w * h);

    build_lift_lut(vw);
    build_sample_map(vw, w, h);

    planes = vw->photo_region;

    for(int i = 0; i < vw->num_photos; i++) {
        picture_t *pic = vw->pictures + i;

        pic->w = picture_width;
        pic->h = picture_height;
        pic->data[0] = planes;
        pic->data[1] = planes + plane_len;
        pic->data[2] = planes + plane_len + plane_len;

        veejay_memset(pic->data[0], pixel_Y_lo_, plane_len);
        veejay_memset(pic->data[1], 128,         plane_len);
        veejay_memset(pic->data[2], 128,         plane_len);

        planes += frame_len;
    }

    return (void*) vw;
}

void *videowall_malloc(int w, int h)
{
    return prepare_filmstrip(w, h);
}

void videowall_free(void *ptr)
{
    destroy_filmstrip((videowall_t*)ptr);
}

static void take_photo(videowall_t *vw, VJFrame *frame, int index)
{
    picture_t *pic = vw->pictures + index;
    const int src_w = frame->width;
    const int box_w = vw->box_w;
    const int box_h = vw->box_h;
    const int rows = box_h * 3;
    const int *restrict sx0_tbl = vw->sample_x0;
    const int *restrict sx1_tbl = vw->sample_x1;
    const int *restrict sy0_tbl = vw->sample_y0;
    const int *restrict sy1_tbl = vw->sample_y1;
    const int *restrict area_tbl = vw->sample_area;

#pragma omp parallel for schedule(static) num_threads(vw->n_threads)
    for(int py = 0; py < rows; py++) {
        const int p = py / box_h;
        const int y = py - (p * box_h);
        const uint8_t *restrict src = frame->data[p];
        uint8_t *restrict dst = pic->data[p] + y * box_w;
        const int sy0 = sy0_tbl[y];
        const int sy1 = sy1_tbl[y];
        const int *restrict area = area_tbl + y * box_w;

        for(int x = 0; x < box_w; x++) {
            const int sx0 = sx0_tbl[x];
            const int sx1 = sx1_tbl[x];
            int sum = 0;

            for(int yy = sy0; yy < sy1; yy++) {
                const uint8_t *restrict s = src + yy * src_w + sx0;

                for(int xx = sx0; xx < sx1; xx++)
                    sum += *s++;
            }

            dst[x] = (uint8_t)((sum + (area[x] >> 1)) / area[x]);
        }
    }
}

static inline void copy_wrap_row(uint8_t *restrict dst,
                                 const uint8_t *restrict src,
                                 int dst_w,
                                 int box_w,
                                 int x)
{
    if(x + box_w <= dst_w) {
        veejay_memcpy(dst + x, src, box_w);
    } else {
        const int left = dst_w - x;
        const int right = box_w - left;

        veejay_memcpy(dst + x, src, left);
        veejay_memcpy(dst, src + left, right);
    }
}

static inline void lut_copy(uint8_t *restrict dst,
                            const uint8_t *restrict src,
                            const uint8_t *restrict lut,
                            int n)
{
    for(int i = 0; i < n; i++)
        dst[i] = lut[src[i]];
}

static inline void lut_wrap_row(uint8_t *restrict dst,
                                const uint8_t *restrict src,
                                const uint8_t *restrict lut,
                                int dst_w,
                                int box_w,
                                int x)
{
    if(x + box_w <= dst_w) {
        lut_copy(dst + x, src, lut, box_w);
    } else {
        const int left = dst_w - x;
        const int right = box_w - left;

        lut_copy(dst + x, src, lut, left);
        lut_copy(dst, src + left, lut, right);
    }
}

static void put_photo_plane_copy(uint8_t *restrict dst,
                                 const uint8_t *restrict photo,
                                 int dst_w,
                                 int dst_h,
                                 int box_w,
                                 int box_h,
                                 int x,
                                 int y)
{
    x = wrapi(x, dst_w);
    y = wrapi(y, dst_h);

    const int top = (y + box_h <= dst_h) ? box_h : (dst_h - y);

    for(int yy = 0; yy < top; yy++)
        copy_wrap_row(dst + (y + yy) * dst_w, photo + yy * box_w, dst_w, box_w, x);

    for(int yy = top; yy < box_h; yy++)
        copy_wrap_row(dst + (yy - top) * dst_w, photo + yy * box_w, dst_w, box_w, x);
}

static void put_photo_plane_lut(uint8_t *restrict dst,
                                const uint8_t *restrict photo,
                                const uint8_t *restrict lut,
                                int dst_w,
                                int dst_h,
                                int box_w,
                                int box_h,
                                int x,
                                int y)
{
    x = wrapi(x, dst_w);
    y = wrapi(y, dst_h);

    const int top = (y + box_h <= dst_h) ? box_h : (dst_h - y);

    for(int yy = 0; yy < top; yy++)
        lut_wrap_row(dst + (y + yy) * dst_w, photo + yy * box_w, lut, dst_w, box_w, x);

    for(int yy = top; yy < box_h; yy++)
        lut_wrap_row(dst + (yy - top) * dst_w, photo + yy * box_w, lut, dst_w, box_w, x);
}

static void put_photo(videowall_t *vw,
                      VJFrame *frame,
                      int index,
                      int global_x,
                      int global_y,
                      int stagger_x,
                      int stagger_y,
                      int luma_lift)
{
    const int n = vw->num_photos >> 1;
    const int box_w = vw->box_w;
    const int box_h = vw->box_h;
    const int width = frame->width;
    const int height = frame->height;
    const int dx = vw->offset_table_x[index];
    const int dy = vw->offset_table_y[index];
    const int row_group = (index < n) ? 0 : 1;
    const int alt_x = (index & 1) ? -stagger_x : stagger_x;
    const int alt_y = row_group ? -stagger_y : stagger_y;
    const int base_x = (box_w * (index % n)) + dx + global_x + alt_x;
    const int base_y = ((index < n) ? dy : (height - box_h - dy)) + global_y + alt_y;
    const picture_t *pic = vw->pictures + index;

    if(luma_lift > 0)
        put_photo_plane_lut(frame->data[0], pic->data[0], vw->lift_lut[luma_lift], width, height, box_w, box_h, base_x, base_y);
    else
        put_photo_plane_copy(frame->data[0], pic->data[0], width, height, box_w, box_h, base_x, base_y);

    put_photo_plane_copy(frame->data[1], pic->data[1], width, height, box_w, box_h, base_x, base_y);
    put_photo_plane_copy(frame->data[2], pic->data[2], width, height, box_w, box_h, base_x, base_y);
}

void videowall_apply(void *ptr, VJFrame *frameA, VJFrame *frameB, int *args)
{
    videowall_t *vw = (videowall_t*) ptr;

    const int width = frameA->width;
    const int height = frameA->height;
    const int slot = args[P_PHOTO_SLOT];
    const int x_disp = args[P_X_DISPLACE];
    const int y_disp = args[P_Y_DISPLACE];
    const int lock_update = args[P_LOCK_UPDATE] ? 1 : 0;
    const int slide_drive = args[P_SLIDE_DRIVE];

    vw->slide_env_q8 = smooth_q8(vw->slide_env_q8, slide_drive << 8);
    vw->slide_env_q8 = clampi(vw->slide_env_q8, 0, 1000 << 8);

    const int slide_q = (vw->slide_env_q8 + 128) >> 8;
    const int slide_span = vw->box_w + vw->box_h;
    int max_slide_px = (slide_span * slide_q + 500) / 1000;
    const int max_safe_slide = (width + height) >> 2;

    if(max_slide_px > max_safe_slide)
        max_slide_px = max_safe_slide;

    const int amp = max_slide_px;
    const int slide_step = 2 + ((slide_q * 46 + 500) / 1000);

    vw->slide_phase = (vw->slide_phase + slide_step) & 4095;

    const int phase = vw->slide_phase;
    const int phase3 = (phase * 3) & 4095;
    const int global_x = scale_wave(amp, triwave12(phase), 1024);
    const int global_y = scale_wave(amp, triwave12(phase3), 2048);
    const int stagger_x = scale_wave(amp, triwave12(phase + 1024), 2048);
    const int stagger_y = scale_wave(amp, triwave12(phase3 + 1024), 4096);
    const int luma_lift = (slide_q * 32 + 500) / 1000;

    if(!lock_update) {
        vw->offset_table_x[slot] = x_disp;
        vw->offset_table_y[slot] = y_disp;
    }

    int next = vw->frame_counter;

    take_photo(vw, frameA, next);
    next++;
    if(next == vw->num_photos)
        next = 0;

    take_photo(vw, frameB, next);
    next++;
    if(next == vw->num_photos)
        next = 0;

    vw->frame_counter = next;

    for(int i = 0; i < vw->num_photos; i++)
        put_photo(vw, frameA, i, global_x, global_y, stagger_x, stagger_y, luma_lift);
}
