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
    picture_t **photo_list;
    picture_t *pictures;
    uint8_t *photo_region;
    int *offset_region;
    int num_photos;
    int frame_counter;
    int *offset_table_x;
    int *offset_table_y;
    int box_w;
    int box_h;
    int n_threads;

    float slide_env;
    float slide_phase;
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

static inline uint8_t u8_add(uint8_t v, int add)
{
    const int r = (int)v + add;
    return (uint8_t)((r < 0) ? 0 : (r > 255 ? 255 : r));
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
    ve->defaults[P_SLIDE_DRIVE] = 420;

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
    free(vw->pictures);
    free(vw->photo_list);

    vw->photo_region = NULL;
    vw->offset_region = NULL;
    vw->pictures = NULL;
    vw->photo_list = NULL;
    vw->offset_table_x = NULL;
    vw->offset_table_y = NULL;
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

static void *prepare_filmstrip(int w, int h)
{
    const int g = videowall_gcd(w, h);
    const int picture_width = g;
    const int picture_height = g;
    const int film_length = videowall_num_pics(w, h);
    const size_t plane_len = (size_t)picture_width * (size_t)picture_height;
    const size_t frame_len = plane_len * 3u;
    uint8_t *planes;

    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    if(!vw)
        return NULL;

    vw->photo_list = (picture_t**) vj_calloc(sizeof(picture_t*) * film_length);
    if(!vw->photo_list) {
        destroy_filmstrip(vw);
        return NULL;
    }

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

    vw->offset_table_x = vw->offset_region;
    vw->offset_table_y = vw->offset_table_x + film_length;
    vw->num_photos = film_length;
    vw->box_w = picture_width;
    vw->box_h = picture_height;
    vw->frame_counter = 0;
    vw->slide_env = 420.0f;
    vw->slide_phase = 0.0f;
    vw->n_threads = vje_advise_num_threads(w * h);

    planes = vw->photo_region;

    for(int i = 0; i < vw->num_photos; i++) {
        picture_t *pic = vw->pictures + i;

        vw->photo_list[i] = pic;
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

static void take_photo_plane(const uint8_t *restrict src,
                             uint8_t *restrict dst,
                             int src_w,
                             int src_h,
                             int box_w,
                             int box_h,
                             int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < box_h; y++) {
        const int sy0 = (y * src_h) / box_h;
        int sy1 = ((y + 1) * src_h) / box_h;

        if(sy1 <= sy0)
            sy1 = sy0 + 1;

        if(sy1 > src_h)
            sy1 = src_h;

        for(int x = 0; x < box_w; x++) {
            const int sx0 = (x * src_w) / box_w;
            int sx1 = ((x + 1) * src_w) / box_w;

            if(sx1 <= sx0)
                sx1 = sx0 + 1;

            if(sx1 > src_w)
                sx1 = src_w;

            int sum = 0;
            int count = 0;

            for(int yy = sy0; yy < sy1; yy++) {
                const int row = yy * src_w;

                for(int xx = sx0; xx < sx1; xx++) {
                    sum += src[row + xx];
                    count++;
                }
            }

            dst[y * box_w + x] = (uint8_t)((sum + (count >> 1)) / count);
        }
    }
}

static void take_photo(videowall_t *vw, VJFrame *frame, int index)
{
    const int box_w = vw->photo_list[index]->w;
    const int box_h = vw->photo_list[index]->h;

    for(int p = 0; p < 3; p++) {
        take_photo_plane(
            frame->data[p],
            vw->photo_list[index]->data[p],
            frame->width,
            frame->height,
            box_w,
            box_h,
            vw->n_threads
        );
    }
}

static void put_photo_plane(uint8_t *restrict dst,
                            const uint8_t *restrict photo,
                            int dst_w,
                            int dst_h,
                            int box_w,
                            int box_h,
                            int x,
                            int y,
                            int luma_lift,
                            int n_threads)
{
    x = wrapi(x, dst_w);
    y = wrapi(y, dst_h);

    if(luma_lift <= 0) {
#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int yy = 0; yy < box_h; yy++) {
            const int dy = wrapi(y + yy, dst_h);
            const int dst_row = dy * dst_w;
            const int src_row = yy * box_w;

            if(x + box_w <= dst_w) {
                veejay_memcpy(dst + dst_row + x, photo + src_row, box_w);
            } else {
                const int left = dst_w - x;
                const int right = box_w - left;

                if(left > 0)
                    veejay_memcpy(dst + dst_row + x, photo + src_row, left);

                if(right > 0)
                    veejay_memcpy(dst + dst_row, photo + src_row + left, right);
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int yy = 0; yy < box_h; yy++) {
            const int dy = wrapi(y + yy, dst_h);
            const int dst_row = dy * dst_w;
            const int src_row = yy * box_w;

            for(int xx = 0; xx < box_w; xx++) {
                const int dx = wrapi(x + xx, dst_w);
                dst[dst_row + dx] = u8_add(photo[src_row + xx], luma_lift);
            }
        }
    }
}

static void put_photo(videowall_t *vw,
                      uint8_t *dst_plane,
                      const uint8_t *photo,
                      int dst_w,
                      int dst_h,
                      int index,
                      int global_x,
                      int global_y,
                      int stagger_x,
                      int stagger_y,
                      int luma_lift)
{
    const int n = vw->num_photos >> 1;
    const int per_row = n;
    const int box_w = vw->photo_list[index]->w;
    const int box_h = vw->photo_list[index]->h;

    const int dx = vw->offset_table_x[index];
    const int dy = vw->offset_table_y[index];

    const int row_group = (index < n) ? 0 : 1;
    const int alt_x = (index & 1) ? -stagger_x : stagger_x;
    const int alt_y = row_group ? -stagger_y : stagger_y;

    const int base_x = (box_w * (index % per_row)) + dx + global_x + alt_x;
    const int base_y = ((index < n) ? dy : (dst_h - box_h - dy)) + global_y + alt_y;

    put_photo_plane(
        dst_plane,
        photo,
        dst_w,
        dst_h,
        box_w,
        box_h,
        base_x,
        base_y,
        luma_lift,
        vw->n_threads
    );
}

void videowall_apply(void *ptr, VJFrame *frameA, VJFrame *frameB, int *args)
{
    videowall_t *vw = (videowall_t*) ptr;

    const int width = frameA->width;
    const int height = frameA->height;

    int slot = args[P_PHOTO_SLOT];
    int x_disp = args[P_X_DISPLACE];
    int y_disp = args[P_Y_DISPLACE];
    int lock_update = args[P_LOCK_UPDATE] ? 1 : 0;
    const int slide_drive = args[P_SLIDE_DRIVE];

    vw->slide_env += ((float)slide_drive - vw->slide_env) * 0.115f;

    if(vw->slide_env < 0.0f)
        vw->slide_env = 0.0f;
    else if(vw->slide_env > 1000.0f)
        vw->slide_env = 1000.0f;

    const int slide_q = clampi((int)(vw->slide_env + 0.5f), 0, 1000);
    const int slide_span = vw->box_w + vw->box_h;
    int max_slide_px = 1 + ((slide_span * slide_q + 500) / 1000);
    const int max_safe_slide = (width + height) >> 2;

    if(max_slide_px > max_safe_slide)
        max_slide_px = max_safe_slide;

    const int amp = max_slide_px;

    vw->slide_phase += 0.10f + ((float)slide_q * 0.0024f);
    if(vw->slide_phase > 8192.0f)
        vw->slide_phase -= 8192.0f;

    const int phase = ((int)vw->slide_phase) & 7;
    const int tri = (phase < 4) ? phase : (7 - phase);
    const int sign = (phase < 4) ? 1 : -1;

    const int global_x = (amp * sign * tri) / 3;
    const int global_y = (amp * (((phase + 2) & 4) ? -1 : 1)) / 3;
    const int stagger_x = amp;
    const int stagger_y = amp >> 1;
    const int luma_lift = (slide_q * 32 + 500) / 1000;

    if(!lock_update) {
        vw->offset_table_x[slot] = x_disp;
        vw->offset_table_y[slot] = y_disp;
    }

    const int index_a = vw->frame_counter % vw->num_photos;
    take_photo(vw, frameA, index_a);

    vw->frame_counter++;

    const int index_b = vw->frame_counter % vw->num_photos;
    take_photo(vw, frameB, index_b);

    for(int i = 0; i < vw->num_photos; i++) {
        put_photo(vw, frameA->data[0], vw->photo_list[i]->data[0], width, height, i,
                  global_x, global_y, stagger_x, stagger_y, luma_lift);
        put_photo(vw, frameA->data[1], vw->photo_list[i]->data[1], width, height, i,
                  global_x, global_y, stagger_x, stagger_y, 0);
        put_photo(vw, frameA->data[2], vw->photo_list[i]->data[2], width, height, i,
                  global_x, global_y, stagger_x, stagger_y, 0);
    }

    vw->frame_counter++;
}
