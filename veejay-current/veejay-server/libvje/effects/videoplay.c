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
#include "videoplay.h"

#define DEFAULT_NUM_PHOTOS 2

typedef struct {
    picture_t **video_list;
    int num_videos;
    int grid;
    int frame_counter;
    int frame_delay;
    int *rt;
    int last_mode;
} videowall_t;

static void destroy_filmstrip(videowall_t *vw);

static inline int videoplay_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int videoplay_min_i(int a, int b)
{
    return a < b ? a : b;
}

static int videoplay_max_grid(int w, int h)
{
    int m = videoplay_min_i(w, h);
    int p;

    if(m < 2)
        return 2;

    p = max_power(m);

    if(p < 2)
        p = 2;

    if(p > m)
        p = m;

    return p;
}

vj_effect *videoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int max_grid = videoplay_max_grid(w, h);
    int max_mode = get_matrix_func_n();

    if(max_mode < 1)
        max_mode = 1;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_grid;
    ve->defaults[0] = DEFAULT_NUM_PHOTOS;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 250;
    ve->defaults[1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = max_mode;
    ve->defaults[2] = 2;

    ve->description = "Videoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photos",
        "Waterfall",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Random",
        "TopLeft to BottomRight : Horizontal",
        "TopLeft to BottomRight : Vertical",
        "BottomRight to TopLeft : Horizontal",
        "BottomRight to TopLeft : Vertical",
        "BottomLeft to TopRight : Horizontal",
        "TopRight to BottomLeft : Vertical",
        "TopRight to BottomLeft : Horizontal",
        "BottomLeft to TopRight : Vertical"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 2,                  max_grid > 16 ? 16 : max_grid, 6, 22, 2200, 5200, 1800, 25,    /* Photos */
        VJ_BEAT_SPEED,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          1,                  96,                         6, 22, 1800, 4200, 900,  30,    /* Waterfall */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,          0, 0,  0,    0,    0,    -1000  /* Mode */
    );

    return ve;
}

static void destroy_filmstrip(videowall_t *vw)
{
    if(!vw)
        return;

    if(vw->video_list) {
        for(int i = 0; i < vw->num_videos; i++) {
            if(vw->video_list[i]) {
                for(int j = 0; j < 3; j++) {
                    if(vw->video_list[i]->data[j])
                        free(vw->video_list[i]->data[j]);
                }

                free(vw->video_list[i]);
            }
        }

        free(vw->video_list);
    }

    if(vw->rt)
        free(vw->rt);

    vw->video_list = NULL;
    vw->rt = NULL;
    vw->num_videos = 0;
    vw->grid = 0;
    vw->frame_counter = 0;
    vw->frame_delay = 0;
    vw->last_mode = -1;
}

static void videoplay_rebuild_order(videowall_t *vw, int mode)
{
    if(!vw || !vw->rt || vw->num_videos <= 0)
        return;

    for(int i = 0; i < vw->num_videos; i++)
        vw->rt[i] = i;

    if(mode == 0)
        fx_shuffle_int_array(vw->rt, vw->num_videos);

    vw->last_mode = mode;
}

static void *prepare_filmstrip(videowall_t *vw, int grid, int w, int h)
{
    if(!vw || grid < 2 || w <= 0 || h <= 0)
        return NULL;

    const int box_w = w / grid;
    const int box_h = h / grid;
    const int film_length = grid * grid;

    if(box_w <= 0 || box_h <= 0 || film_length <= 0)
        return NULL;

    vw->video_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1));
    if(!vw->video_list) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->rt = (int*) vj_calloc(sizeof(int) * film_length);
    if(!vw->rt) {
        destroy_filmstrip(vw);
        return NULL;
    }

    vw->num_videos = film_length;
    vw->grid = grid;

    for(int i = 0; i < film_length; i++) {
        vw->video_list[i] = (picture_t*) vj_calloc(sizeof(picture_t));
        if(!vw->video_list[i]) {
            destroy_filmstrip(vw);
            return NULL;
        }

        vw->video_list[i]->w = box_w;
        vw->video_list[i]->h = box_h;

        for(int j = 0; j < 3; j++) {
            vw->video_list[i]->data[j] = (uint8_t*) vj_malloc((size_t)box_w * (size_t)box_h);
            if(!vw->video_list[i]->data[j]) {
                destroy_filmstrip(vw);
                return NULL;
            }

            veejay_memset(
                vw->video_list[i]->data[j],
                (j == 0) ? pixel_Y_lo_ : 128,
                box_w * box_h
            );
        }
    }

    vw->frame_counter = 0;
    vw->frame_delay = 0;
    vw->last_mode = -1;

    return (void*) vw;
}

void *videoplay_malloc(int w, int h)
{
    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    if(!vw)
        return NULL;

    if(!prepare_filmstrip(vw, DEFAULT_NUM_PHOTOS, w, h)) {
        free(vw);
        return NULL;
    }

    return (void*) vw;
}

void videoplay_free(void *ptr)
{
    videowall_t *vw = (videowall_t*) ptr;

    if(!vw)
        return;

    destroy_filmstrip(vw);
    free(vw);
}

static void take_video_plane(const uint8_t *restrict src,
                             uint8_t *restrict dst,
                             int src_w,
                             int src_h,
                             int box_w,
                             int box_h)
{
    if(!src || !dst || src_w <= 0 || src_h <= 0 || box_w <= 0 || box_h <= 0)
        return;

    const int step_x = (src_w << 16) / box_w;
    const int step_y = (src_h << 16) / box_h;

    for(int y = 0; y < box_h; y++) {
        int sy = (y * step_y) >> 16;

        if(sy >= src_h)
            sy = src_h - 1;

        const uint8_t *restrict src_row = src + sy * src_w;
        uint8_t *restrict dst_row = dst + y * box_w;

        for(int x = 0; x < box_w; x++) {
            int sx = (x * step_x) >> 16;

            if(sx >= src_w)
                sx = src_w - 1;

            dst_row[x] = src_row[sx];
        }
    }
}

static void take_video(videowall_t *vw, VJFrame *src, int index)
{
    if(!vw || !src || index < 0 || index >= vw->num_videos || !vw->video_list[index])
        return;

    const int box_w = vw->video_list[index]->w;
    const int box_h = vw->video_list[index]->h;

    for(int p = 0; p < 3; p++) {
        if(src->data[p] && vw->video_list[index]->data[p]) {
            take_video_plane(
                src->data[p],
                vw->video_list[index]->data[p],
                src->width,
                src->height,
                box_w,
                box_h
            );
        }
    }
}

static void put_video_plane(uint8_t *restrict dst,
                            const uint8_t *restrict src,
                            int dst_w,
                            int dst_h,
                            int box_w,
                            int box_h,
                            int x,
                            int y)
{
    if(!dst || !src || dst_w <= 0 || dst_h <= 0 || box_w <= 0 || box_h <= 0)
        return;

    int sx0 = 0;
    int sy0 = 0;
    int copy_w = box_w;
    int copy_h = box_h;

    if(x < 0) {
        sx0 = -x;
        copy_w += x;
        x = 0;
    }

    if(y < 0) {
        sy0 = -y;
        copy_h += y;
        y = 0;
    }

    if(x >= dst_w || y >= dst_h)
        return;

    if(x + copy_w > dst_w)
        copy_w = dst_w - x;

    if(y + copy_h > dst_h)
        copy_h = dst_h - y;

    if(copy_w <= 0 || copy_h <= 0)
        return;

    for(int yy = 0; yy < copy_h; yy++) {
        veejay_memcpy(
            dst + (y + yy) * dst_w + x,
            src + (sy0 + yy) * box_w + sx0,
            copy_w
        );
    }
}

static void put_video(videowall_t *vw,
                      VJFrame *dst,
                      int index,
                      matrix_t matrix)
{
    if(!vw || !dst || index < 0 || index >= vw->num_videos || !vw->video_list[index])
        return;

    const int box_w = vw->video_list[index]->w;
    const int box_h = vw->video_list[index]->h;

    for(int p = 0; p < 3; p++) {
        if(dst->data[p] && vw->video_list[index]->data[p]) {
            put_video_plane(
                dst->data[p],
                vw->video_list[index]->data[p],
                dst->width,
                dst->height,
                box_w,
                box_h,
                matrix.w,
                matrix.h
            );
        }
    }
}

void videoplay_apply(void *ptr, VJFrame *frame, VJFrame *B, int *args)
{
    videowall_t *vw = (videowall_t*) ptr;

    if(!vw || !frame || !B || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !B->data[0] || !B->data[1] || !B->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int max_grid = videoplay_max_grid(width, height);
    const int grid = videoplay_clampi(args[0], 2, max_grid);
    const int delay = videoplay_clampi(args[1], 1, 250);

    int max_mode = get_matrix_func_n();
    if(max_mode < 1)
        max_mode = 1;

    const int mode = videoplay_clampi(args[2], 0, max_mode);
    const int wanted_videos = grid * grid;

    if(wanted_videos != vw->num_videos || vw->num_videos <= 0 || vw->grid != grid) {
        destroy_filmstrip(vw);

        if(!prepare_filmstrip(vw, grid, width, height))
            return;

        videoplay_rebuild_order(vw, mode);
    }

    if(vw->last_mode != mode)
        videoplay_rebuild_order(vw, mode);

    if(vw->frame_delay > delay)
        vw->frame_delay = delay;

    int capture_now = 0;

    if(vw->frame_delay <= 0) {
        capture_now = 1;
    } else {
        vw->frame_delay--;

        if(vw->frame_delay <= 0)
            capture_now = 1;
    }

    if(capture_now) {
        const int a = vw->frame_counter % vw->num_videos;
        const int b = (vw->frame_counter + 1) % vw->num_videos;

        take_video(vw, B, a);
        take_video(vw, frame, b);

        vw->frame_counter += 2;
        vw->frame_delay = delay;
    } else if(vw->frame_counter > 0) {
        const int a = (vw->frame_counter - 1) % vw->num_videos;
        const int b = vw->frame_counter % vw->num_videos;

        take_video(vw, frame, a);
        take_video(vw, B, b);
    }

    matrix_f matrix_placement = NULL;

    if(mode == 0)
        matrix_placement = get_matrix_func(0);
    else
        matrix_placement = get_matrix_func(mode - 1);

    if(!matrix_placement)
        matrix_placement = get_matrix_func(0);

    if(!matrix_placement)
        return;

    for(int i = 0; i < vw->num_videos; i++) {
        const int slot = vw->rt ? vw->rt[i] : i;
        matrix_t m = matrix_placement(slot, grid, width, height);

        put_video(vw, frame, i, m);
    }
}