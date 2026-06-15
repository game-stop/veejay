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
#include "videoplay.h"

#define DEFAULT_NUM_PHOTOS 2

#define VIDEOPLAY_PARAMS 5
#define P_PHOTOS        0
#define P_WATERFALL     1
#define P_MODE          2
#define P_CAPTURE_DRIVE 3
#define P_SLIDE_DRIVE   4

typedef struct {
    picture_t **video_list;
    picture_t *pictures;
    uint8_t *frame_region;
    int num_videos;
    int grid;
    int frame_counter;
    int frame_delay;
    int *rt;
    int last_mode;

    int slide_phase;
    int delay_env_q8;
    int capture_env_q8;
    int slide_env_q8;
} videowall_t;

static void destroy_filmstrip(videowall_t *vw);

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t clamp_u8(int v)
{
    if((unsigned int)v > 255U)
        return (v < 0) ? 0 : 255;

    return (uint8_t)v;
}

static inline int min_i(int a, int b)
{
    return a < b ? a : b;
}

static int videoplay_max_grid(int w, int h)
{
    int m = min_i(w, h);
    int p;

    p = max_power(m);

    return p;
}



static inline int videoplay_param_to_q8(int v)
{
    return clampi((clampi(v, 0, 1000) * 255 + 500) / 1000, 0, 255);
}

static inline int videoplay_smooth_i(int oldv, int target, int attack, int release)
{
    const int delta = target - oldv;
    const int a = delta >= 0 ? attack : release;
    const int bias = delta >= 0 ? 128 : -128;

    return oldv + ((delta * a + bias) >> 8);
}

static inline int videoplay_tri_signed_q8(int phase)
{
    int p = phase & 1023;
    int tri = (p < 512) ? p : (1023 - p);

    return tri - 256;
}



vj_effect *videoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = VIDEOPLAY_PARAMS;

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

    const int max_grid = videoplay_max_grid(w, h);
    const int max_mode = get_matrix_func_n();

    ve->limits[0][P_PHOTOS] = 2;
    ve->limits[1][P_PHOTOS] = max_grid;
    ve->defaults[P_PHOTOS] = DEFAULT_NUM_PHOTOS;

    ve->limits[0][P_WATERFALL] = 1;
    ve->limits[1][P_WATERFALL] = 250;
    ve->defaults[P_WATERFALL] = 1;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = max_mode;
    ve->defaults[P_MODE] = 2;

    ve->limits[0][P_CAPTURE_DRIVE] = 0;
    ve->limits[1][P_CAPTURE_DRIVE] = 1000;
    ve->defaults[P_CAPTURE_DRIVE] = 0;

    ve->limits[0][P_SLIDE_DRIVE] = 0;
    ve->limits[1][P_SLIDE_DRIVE] = 1000;
    ve->defaults[P_SLIDE_DRIVE] = 0;

    ve->description = "Videoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photos",
        "Waterfall",
        "Mode",
        "Capture Drive",
        "Slide Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
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
        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,   0,    0,   -1000,
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    1,                  250,                8,  34, 320, 1600, 0,   54,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,   0,    0,   -1000,
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    120,                1000,               14, 64, 90,  620,  0,   94,
        VJ_BEAT_DRIFT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    120,                1000,               12, 58, 80,  760,  0,   88
    );

    return ve;
}

static void release_filmstrip(videowall_t *vw)
{
    free(vw->frame_region);
    free(vw->pictures);
    free(vw->video_list);
    free(vw->rt);

    vw->frame_region = NULL;
    vw->pictures = NULL;
    vw->video_list = NULL;
    vw->rt = NULL;
    vw->num_videos = 0;
    vw->grid = 0;
    vw->frame_counter = 0;
    vw->frame_delay = 0;
    vw->last_mode = -1;
}

static void destroy_filmstrip(videowall_t *vw)
{
    release_filmstrip(vw);
    free(vw);
}

static void videoplay_rebuild_order(videowall_t *vw, int mode)
{
    for(int i = 0; i < vw->num_videos; i++)
        vw->rt[i] = i;

    if(mode == 0)
        fx_shuffle_int_array(vw->rt, vw->num_videos);

    vw->last_mode = mode;
}

static void *prepare_filmstrip(videowall_t *vw, int grid, int w, int h)
{
    const int box_w = w / grid;
    const int box_h = h / grid;
    const int film_length = grid * grid;
    const size_t plane_len = (size_t)box_w * (size_t)box_h;
    const size_t frame_len = plane_len * 3u;
    uint8_t *planes;

    vw->video_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1));
    if(!vw->video_list) {
        release_filmstrip(vw);
        return NULL;
    }

    vw->pictures = (picture_t*) vj_calloc(sizeof(picture_t) * film_length);
    if(!vw->pictures) {
        release_filmstrip(vw);
        return NULL;
    }

    vw->frame_region = (uint8_t*) vj_malloc(frame_len * (size_t)film_length);
    if(!vw->frame_region) {
        release_filmstrip(vw);
        return NULL;
    }

    vw->rt = (int*) vj_calloc(sizeof(int) * film_length);
    if(!vw->rt) {
        release_filmstrip(vw);
        return NULL;
    }

    veejay_memset(vw->frame_region, 0, frame_len * (size_t)film_length);

    vw->num_videos = film_length;
    vw->grid = grid;
    planes = vw->frame_region;

    for(int i = 0; i < film_length; i++) {
        picture_t *pic = vw->pictures + i;

        vw->video_list[i] = pic;
        pic->w = box_w;
        pic->h = box_h;
        pic->data[0] = planes;
        pic->data[1] = planes + plane_len;
        pic->data[2] = planes + plane_len + plane_len;

        veejay_memset(pic->data[0], pixel_Y_lo_, plane_len);
        veejay_memset(pic->data[1], 128,         plane_len);
        veejay_memset(pic->data[2], 128,         plane_len);

        planes += frame_len;
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

    vw->slide_phase = 0;
    vw->delay_env_q8 = 0;
    vw->capture_env_q8 = 0;
    vw->slide_env_q8 = 0;

    if(!prepare_filmstrip(vw, DEFAULT_NUM_PHOTOS, w, h)) {
        free(vw);
        return NULL;
    }

    return (void*) vw;
}

void videoplay_free(void *ptr)
{
    destroy_filmstrip((videowall_t*) ptr);
}

static void take_video_plane(const uint8_t *restrict src,
                             uint8_t *restrict dst,
                             int src_w,
                             int src_h,
                             int box_w,
                             int box_h)
{
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
    const int box_w = vw->video_list[index]->w;
    const int box_h = vw->video_list[index]->h;

    for(int p = 0; p < 3; p++) {
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

static void put_video_plane(uint8_t *restrict dst,
                            const uint8_t *restrict src,
                            int dst_w,
                            int dst_h,
                            int box_w,
                            int box_h,
                            int x,
                            int y,
                            int boost_y)
{
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

    if(boost_y <= 0) {
        for(int yy = 0; yy < copy_h; yy++) {
            veejay_memcpy(
                dst + (y + yy) * dst_w + x,
                src + (sy0 + yy) * box_w + sx0,
                copy_w
            );
        }
    } else {
        for(int yy = 0; yy < copy_h; yy++) {
            uint8_t *restrict d = dst + (y + yy) * dst_w + x;
            const uint8_t *restrict s = src + (sy0 + yy) * box_w + sx0;

            for(int xx = 0; xx < copy_w; xx++)
                d[xx] = clamp_u8((int)s[xx] + boost_y);
        }
    }
}

static void put_video(videowall_t *vw,
                      VJFrame *dst,
                      int index,
                      matrix_t matrix,
                      int off_x,
                      int off_y,
                      int boost_y)
{
    const int box_w = vw->video_list[index]->w;
    const int box_h = vw->video_list[index]->h;
    const int x = matrix.w + off_x;
    const int y = matrix.h + off_y;

    for(int p = 0; p < 3; p++) {
        put_video_plane(
            dst->data[p],
            vw->video_list[index]->data[p],
            dst->width,
            dst->height,
            box_w,
            box_h,
            x,
            y,
            (p == 0) ? boost_y : 0
        );
    }
}

void videoplay_apply(void *ptr, VJFrame *frame, VJFrame *B, int *args)
{
    videowall_t *vw = (videowall_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int max_grid = videoplay_max_grid(width, height);
    const int grid = args[P_PHOTOS];
    const int delay_arg = args[P_WATERFALL];
    const int max_mode = get_matrix_func_n();
    const int mode = args[P_MODE];
    const int capture_drive = args[P_CAPTURE_DRIVE];
    const int slide_drive = args[P_SLIDE_DRIVE];
    const int wanted_videos = grid * grid;

    if(vw->delay_env_q8 <= 0)
        vw->delay_env_q8 = delay_arg << 8;

    vw->delay_env_q8 = videoplay_smooth_i(vw->delay_env_q8, delay_arg << 8, 96, 44);
    vw->capture_env_q8 = videoplay_smooth_i(vw->capture_env_q8, videoplay_param_to_q8(capture_drive), 92, 46);
    vw->slide_env_q8 = videoplay_smooth_i(vw->slide_env_q8, videoplay_param_to_q8(slide_drive), 86, 42);

    const int delay = clampi((vw->delay_env_q8 + 128) >> 8, 1, 250);

    if(wanted_videos != vw->num_videos || vw->num_videos <= 0 || vw->grid != grid) {
        release_filmstrip(vw);

        if(!prepare_filmstrip(vw, grid, width, height))
            return;

        videoplay_rebuild_order(vw, mode);
    }

    if(vw->last_mode != mode)
        videoplay_rebuild_order(vw, mode);

    const int capture_q8 = clampi(vw->capture_env_q8, 0, 255);
    int effective_delay = delay - ((delay - 1) * capture_q8) / 255;

    effective_delay = clampi(effective_delay, 1, delay);

    if(vw->frame_delay > effective_delay)
        vw->frame_delay = effective_delay;

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
        vw->frame_delay = effective_delay;
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

    const int box_w = vw->video_list[0]->w;
    const int box_h = vw->video_list[0]->h;

    const int slide_q8 = clampi(vw->slide_env_q8, 0, 255);

    const int phase_speed = 2 + (slide_q8 >> 4);
    vw->slide_phase += phase_speed;

    const int wave_x = videoplay_tri_signed_q8(vw->slide_phase);
    const int wave_y = videoplay_tri_signed_q8(vw->slide_phase + 256);
    int amp_x = (box_w * slide_q8) / 540;
    int amp_y = (box_h * slide_q8) / 540;

    if(amp_x > (box_w >> 1))
        amp_x = box_w >> 1;
    if(amp_y > (box_h >> 1))
        amp_y = box_h >> 1;

    const int global_x = (amp_x * wave_x) >> 8;
    const int global_y = (amp_y * wave_y) >> 8;
    const int boost_y = (slide_q8 * 10) >> 8;

    for(int i = 0; i < vw->num_videos; i++) {
        const int slot = vw->rt[i];
        matrix_t m = matrix_placement(slot, grid, width, height);

        const int row = slot / grid;
        const int col = slot % grid;
        const int stagger_x = ((row ^ col) & 1) ? (amp_x >> 1) : -(amp_x >> 1);
        const int stagger_y = (row & 1) ? (amp_y >> 1) : -(amp_y >> 1);

        put_video(
            vw,
            frame,
            i,
            m,
            global_x + stagger_x,
            global_y + stagger_y,
            boost_y
        );
    }
}
