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

#define VIDEOPLAY_PARAMS 7
#define P_PHOTOS       0
#define P_WATERFALL    1
#define P_MODE         2
#define P_BEAT_CAPTURE 3
#define P_BEAT_SLIDE   4
#define P_BEAT_PUSH    5
#define P_BEAT_SMOOTH  6

typedef struct {
    picture_t **video_list;
    int num_videos;
    int grid;
    int frame_counter;
    int frame_delay;
    int *rt;
    int last_mode;

    int beat_env_q8;
    int beat_kick_q8;
    int beat_phase;
    int beat_cooldown;
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

    if(m < 2)
        return 2;

    p = max_power(m);

    if(p < 2)
        p = 2;

    if(p > m)
        p = m;

    return p;
}

static inline int videoplay_beat_shape_q8(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int q = (beat_push * beat_push * 255 + 500000) / 1000000;
    return clampi(q, 0, 255);
}

static inline int videoplay_tri_signed_q8(int phase)
{
    int p = phase & 1023;
    int tri = (p < 512) ? p : (1023 - p);

    return tri - 256;
}

static void videoplay_update_beat(videowall_t *vw, int beat_push, int beat_smooth)
{
    if(!vw)
        return;

    beat_smooth = clampi(beat_smooth, 0, 1000);

    const int target = videoplay_beat_shape_q8(beat_push);
    const int prev = vw->beat_env_q8;

    const int attack = 42 + ((1000 - beat_smooth) * 116) / 1000;
    const int release = 5 + ((1000 - beat_smooth) * 28) / 1000;

    if(target > vw->beat_env_q8)
        vw->beat_env_q8 += ((target - vw->beat_env_q8) * attack + 127) >> 8;
    else
        vw->beat_env_q8 += ((target - vw->beat_env_q8) * release + 127) >> 8;

    vw->beat_env_q8 = clampi(vw->beat_env_q8, 0, 255);

    if(target > prev + 18) {
        int k = target - (prev >> 1);
        if(k > 255)
            k = 255;
        if(k > vw->beat_kick_q8)
            vw->beat_kick_q8 = k;
    } else {
        const int hold = 184 + (beat_smooth * 42) / 1000;
        vw->beat_kick_q8 = (vw->beat_kick_q8 * hold) >> 8;
    }

    if(vw->beat_kick_q8 < 2)
        vw->beat_kick_q8 = 0;

    if(vw->beat_cooldown > 0)
        vw->beat_cooldown--;
}

vj_effect *videoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = VIDEOPLAY_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int max_grid = videoplay_max_grid(w, h);
    int max_mode = get_matrix_func_n();

    if(max_mode < 1)
        max_mode = 1;

    ve->limits[0][P_PHOTOS] = 2;
    ve->limits[1][P_PHOTOS] = max_grid;
    ve->defaults[P_PHOTOS] = DEFAULT_NUM_PHOTOS;

    ve->limits[0][P_WATERFALL] = 1;
    ve->limits[1][P_WATERFALL] = 250;
    ve->defaults[P_WATERFALL] = 1;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = max_mode;
    ve->defaults[P_MODE] = 2;

    ve->limits[0][P_BEAT_CAPTURE] = 0;
    ve->limits[1][P_BEAT_CAPTURE] = 1000;
    ve->defaults[P_BEAT_CAPTURE] = 320;

    ve->limits[0][P_BEAT_SLIDE] = 0;
    ve->limits[1][P_BEAT_SLIDE] = 1000;
    ve->defaults[P_BEAT_SLIDE] = 180;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Videoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photos",
        "Waterfall",
        "Mode",
        "Beat Capture",
        "Beat Slide",
        "Beat Push",
        "Beat Smooth"
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

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 2,                  max_grid > 16 ? 16 : max_grid, 6, 22, 2200, 5200, 1800, 25,    /* Photos */
        VJ_BEAT_SPEED,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                          1,                  96,                         6, 22, 1800, 4200, 900,  30,    /* Waterfall */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,          0, 0,  0,    0,    0,    -1000, /* Mode */
        VJ_BEAT_SPEED,     VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,          0, 0,  0,    0,    0,    -1000, /* Beat Capture */
        VJ_BEAT_DRIFT,     VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,          0, 0,  0,    0,    0,    -1000, /* Beat Slide */
        VJ_BEAT_KICK,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                             0,                  780,                        18, 76, 80,   740,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY,                                                260,                850,                        5, 18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    return ve;
}

static void release_filmstrip(videowall_t *vw)
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

static void destroy_filmstrip(videowall_t *vw)
{
    if(!vw)
        return;

    release_filmstrip(vw);
    free(vw);
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
        release_filmstrip(vw);
        return NULL;
    }

    vw->rt = (int*) vj_calloc(sizeof(int) * film_length);
    if(!vw->rt) {
        release_filmstrip(vw);
        return NULL;
    }

    vw->num_videos = film_length;
    vw->grid = grid;

    for(int i = 0; i < film_length; i++) {
        vw->video_list[i] = (picture_t*) vj_calloc(sizeof(picture_t));
        if(!vw->video_list[i]) {
            release_filmstrip(vw);
            return NULL;
        }

        vw->video_list[i]->w = box_w;
        vw->video_list[i]->h = box_h;

        for(int j = 0; j < 3; j++) {
            vw->video_list[i]->data[j] = (uint8_t*) vj_malloc((size_t)box_w * (size_t)box_h);
            if(!vw->video_list[i]->data[j]) {
                release_filmstrip(vw);
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

    vw->beat_env_q8 = 0;
    vw->beat_kick_q8 = 0;
    vw->beat_phase = 0;
    vw->beat_cooldown = 0;

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
                            int y,
                            int boost_y)
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
    if(!vw || !dst || index < 0 || index >= vw->num_videos || !vw->video_list[index])
        return;

    const int box_w = vw->video_list[index]->w;
    const int box_h = vw->video_list[index]->h;
    const int x = matrix.w + off_x;
    const int y = matrix.h + off_y;

    for(int p = 0; p < 3; p++) {
        if(dst->data[p] && vw->video_list[index]->data[p]) {
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
    const int grid = clampi(args[P_PHOTOS], 2, max_grid);
    const int delay = clampi(args[P_WATERFALL], 1, 250);

    int max_mode = get_matrix_func_n();
    if(max_mode < 1)
        max_mode = 1;

    const int mode = clampi(args[P_MODE], 0, max_mode);
    const int beat_capture = clampi(args[P_BEAT_CAPTURE], 0, 1000);
    const int beat_slide = clampi(args[P_BEAT_SLIDE], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);
    const int wanted_videos = grid * grid;

    videoplay_update_beat(vw, beat_push, beat_smooth);

    if(wanted_videos != vw->num_videos || vw->num_videos <= 0 || vw->grid != grid) {
        release_filmstrip(vw);

        if(!prepare_filmstrip(vw, grid, width, height))
            return;

        videoplay_rebuild_order(vw, mode);
    }

    if(vw->last_mode != mode)
        videoplay_rebuild_order(vw, mode);

    const int beat_q8 = clampi(vw->beat_env_q8 + (vw->beat_kick_q8 >> 1), 0, 255);
    const int capture_q8 = (beat_capture * beat_q8 + 500) / 1000;
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

    if(beat_capture > 0 &&
       vw->beat_cooldown <= 0 &&
       vw->beat_kick_q8 > 78)
    {
        capture_now = 1;
        vw->beat_cooldown = 2 + ((delay * (1000 - beat_capture)) / 1000);
        if(vw->beat_cooldown < 2)
            vw->beat_cooldown = 2;
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

    if(!matrix_placement)
        return;

    const int box_w = (vw->num_videos > 0 && vw->video_list[0]) ? vw->video_list[0]->w : (width / grid);
    const int box_h = (vw->num_videos > 0 && vw->video_list[0]) ? vw->video_list[0]->h : (height / grid);

    const int phase_speed = 3 + ((beat_slide * (32 + beat_q8)) / 8000);
    vw->beat_phase += phase_speed;

    const int wave_x = videoplay_tri_signed_q8(vw->beat_phase);
    const int wave_y = videoplay_tri_signed_q8(vw->beat_phase + 256);
    const int amp_x = (box_w * beat_slide * beat_q8) / (1000 * 640);
    const int amp_y = (box_h * beat_slide * beat_q8) / (1000 * 640);
    const int global_x = (amp_x * wave_x) >> 8;
    const int global_y = (amp_y * wave_y) >> 8;
    const int boost_y = (beat_q8 * 18) >> 8;

    for(int i = 0; i < vw->num_videos; i++) {
        const int slot = vw->rt ? vw->rt[i] : i;
        matrix_t m = matrix_placement(slot, grid, width, height);

        const int row = grid > 0 ? (slot / grid) : 0;
        const int col = grid > 0 ? (slot % grid) : 0;
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
