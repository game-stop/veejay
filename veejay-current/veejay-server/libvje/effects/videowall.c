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
#include "videowall.h"

typedef struct {
    picture_t **photo_list;
    int num_photos;
    int frame_counter;
    int *offset_table_x;
    int *offset_table_y;
    int box_w;
    int box_h;
    int n_threads;
} videowall_t;

static void destroy_filmstrip(videowall_t *vw);

static inline int videowall_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int videowall_wrapi(int v, int max)
{
    if(max <= 0)
        return 0;

    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static int videowall_gcd(int a, int b)
{
    if(a < 0) a = -a;
    if(b < 0) b = -b;

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

    if(g <= 0 || w <= 0 || h <= 0)
        return 2;

    int n = (w / g) * 2;

    if(n < 2)
        n = 2;

    return n;
}

vj_effect *videowall_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int photos = videowall_num_pics(w, h);
    const int max_photo = photos > 0 ? photos - 1 : 0;
    const int max_w = w > 0 ? w : 1;
    const int max_h = h > 0 ? h : 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = max_photo;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = max_w;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = max_h;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->defaults[3] = 0;

    ve->description = "VideoWall / Tile Placement";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photo Slot",
        "X Displacement",
        "Y Displacement",
        "Lock Update"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Update Slot",
        "Locked"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Photo Slot */
        VJ_BEAT_DRIFT,    VJ_BEAT_F_CONTINUOUS,                    0,                  max_w,              8, 30, 1200, 3000, 0, 35,    /* X Displacement */
        VJ_BEAT_DRIFT,    VJ_BEAT_F_CONTINUOUS,                    0,                  max_h,              8, 30, 1200, 3000, 0, 35,    /* Y Displacement */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000  /* Lock Update */
    );

    return ve;
}

static void release_filmstrip(videowall_t *vw)
{
    if(!vw)
        return;

    if(vw->photo_list) {
        for(int i = 0; i < vw->num_photos; i++) {
            if(vw->photo_list[i]) {
                for(int j = 0; j < 3; j++) {
                    if(vw->photo_list[i]->data[j])
                        free(vw->photo_list[i]->data[j]);
                }

                free(vw->photo_list[i]);
            }
        }

        free(vw->photo_list);
    }

    if(vw->offset_table_x)
        free(vw->offset_table_x);

    if(vw->offset_table_y)
        free(vw->offset_table_y);

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
    if(!vw)
        return;

    release_filmstrip(vw);
    free(vw);
}

static void *prepare_filmstrip(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    const int g = videowall_gcd(w, h);
    if(g <= 0)
        return NULL;

    const int picture_width = g;
    const int picture_height = g;
    const int film_length = videowall_num_pics(w, h);

    if(picture_width <= 0 || picture_height <= 0 || film_length <= 0)
        return NULL;

    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    if(!vw)
        return NULL;

    vw->photo_list = (picture_t**) vj_calloc(sizeof(picture_t*) * film_length);
    if(!vw->photo_list) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->offset_table_x = (int*) vj_calloc(sizeof(int) * film_length);
    if(!vw->offset_table_x) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->offset_table_y = (int*) vj_calloc(sizeof(int) * film_length);
    if(!vw->offset_table_y) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->num_photos = film_length;
    vw->box_w = picture_width;
    vw->box_h = picture_height;
    vw->frame_counter = 0;

    vw->n_threads = vje_advise_num_threads(w * h);
    if(vw->n_threads < 1)
        vw->n_threads = 1;

    for(int i = 0; i < vw->num_photos; i++) {
        vw->photo_list[i] = (picture_t*) vj_calloc(sizeof(picture_t));
        if(!vw->photo_list[i]) {
            destroy_filmstrip(vw);
            return NULL;
        }

        vw->photo_list[i]->w = picture_width;
        vw->photo_list[i]->h = picture_height;

        for(int j = 0; j < 3; j++) {
            vw->photo_list[i]->data[j] = (uint8_t*) vj_malloc((size_t)picture_width * (size_t)picture_height);
            if(!vw->photo_list[i]->data[j]) {
                destroy_filmstrip(vw);
                return NULL;
            }

            veejay_memset(
                vw->photo_list[i]->data[j],
                j == 0 ? pixel_Y_lo_ : 128,
                picture_width * picture_height
            );
        }
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
    if(!src || !dst || src_w <= 0 || src_h <= 0 || box_w <= 0 || box_h <= 0)
        return;

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

            dst[y * box_w + x] = count > 0 ? (uint8_t)((sum + (count >> 1)) / count) : 0;
        }
    }
}

static void take_photo(videowall_t *vw, VJFrame *frame, int index)
{
    if(!vw || !frame || index < 0 || index >= vw->num_photos || !vw->photo_list[index])
        return;

    const int box_w = vw->photo_list[index]->w;
    const int box_h = vw->photo_list[index]->h;

    if(box_w <= 0 || box_h <= 0)
        return;

    for(int p = 0; p < 3; p++) {
        if(frame->data[p] && vw->photo_list[index]->data[p]) {
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
}

static void put_photo_plane(uint8_t *restrict dst,
                            const uint8_t *restrict photo,
                            int dst_w,
                            int dst_h,
                            int box_w,
                            int box_h,
                            int x,
                            int y,
                            int n_threads)
{
    if(!dst || !photo || dst_w <= 0 || dst_h <= 0 || box_w <= 0 || box_h <= 0)
        return;

    x = videowall_wrapi(x, dst_w);
    y = videowall_wrapi(y, dst_h);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int yy = 0; yy < box_h; yy++) {
        const int dy = videowall_wrapi(y + yy, dst_h);
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
}

static void put_photo(videowall_t *vw,
                      uint8_t *dst_plane,
                      const uint8_t *photo,
                      int dst_w,
                      int dst_h,
                      int index)
{
    if(!vw || !dst_plane || !photo || index < 0 || index >= vw->num_photos || !vw->photo_list[index])
        return;

    const int n = vw->num_photos >> 1;
    const int per_row = n > 0 ? n : 1;
    const int box_w = vw->photo_list[index]->w;
    const int box_h = vw->photo_list[index]->h;

    const int dx = vw->offset_table_x[index];
    const int dy = vw->offset_table_y[index];

    const int base_x = (box_w * (index % per_row)) + dx;
    const int base_y = (index < n) ? dy : (dst_h - box_h - dy);

    put_photo_plane(
        dst_plane,
        photo,
        dst_w,
        dst_h,
        box_w,
        box_h,
        base_x,
        base_y,
        vw->n_threads
    );
}

void videowall_apply(void *ptr, VJFrame *frameA, VJFrame *frameB, int *args)
{
    videowall_t *vw = (videowall_t*) ptr;

    if(!vw || !frameA || !frameB || !args ||
       !frameA->data[0] || !frameA->data[1] || !frameA->data[2] ||
       !frameB->data[0] || !frameB->data[1] || !frameB->data[2])
        return;

    const int width = frameA->width;
    const int height = frameA->height;
    const int len = frameA->len;

    if(width <= 0 || height <= 0 || len <= 0 || vw->num_photos <= 0)
        return;

    int slot = videowall_clampi(args[0], 0, vw->num_photos - 1);
    int x_disp = videowall_clampi(args[1], 0, width);
    int y_disp = videowall_clampi(args[2], 0, height);
    int lock_update = args[3] ? 1 : 0;

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
        put_photo(vw, frameA->data[0], vw->photo_list[i]->data[0], width, height, i);
        put_photo(vw, frameA->data[1], vw->photo_list[i]->data[1], width, height, i);
        put_photo(vw, frameA->data[2], vw->photo_list[i]->data[2], width, height, i);
    }

    vw->frame_counter++;
}