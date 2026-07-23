/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>

#include <libavutil/avutil.h>
#include "../effects/common.h"
#include "../libel/pixbuf.h"
#include <veejaycore/avcommon.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "shapewipe.h"

#define MAX_NUMBER_OF_SHAPES 10000

#define SHAPEWIPE_PARAMS 8

#define P_SHAPE       0
#define P_THRESHOLD   1
#define P_DIRECTION   2
#define P_AUTOMATIC   3
#define P_SOFTNESS    4
#define P_EDGE_GLOW   5
#define P_WIPE_DRIVE  6
#define P_MIX_DRIVE   7

typedef struct {
    char **shapelist;
    int shapeidx;
    int currentshape;
    void *selected_shape;
    int shape_min;
    int shape_max;
    int shape_completed;

    int n_threads;
    int have_smooth;
    float sm_threshold;
    float sm_softness;
    float sm_glow;
    float sm_wipe_drive;
    float sm_mix_drive;
} shape_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t shapewipe_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}



static inline void shapewipe_smooth_to(float *v, float target, float a)
{
    *v += (target - *v) * a;
}

static int is_img(const char *file)
{
    if(!file)
        return 0;

    if(strstr(file, ".png") || strstr(file, ".PNG"))
        return 1;

    if(strstr(file, ".pgm") || strstr(file, ".PGM"))
        return 1;

    if(strstr(file, ".tif") || strstr(file, ".TIF") ||
       strstr(file, ".tiff") || strstr(file, ".TIFF"))
        return 1;

    return 0;
}

static int find_shape_file(char *path, char **shapelist, int *shapeidx, int maxshapes)
{
    if(!path || !shapelist || !shapeidx)
        return 0;

    struct stat l;

    veejay_memset(&l, 0, sizeof(struct stat));

    if(lstat(path, &l) < 0)
        return 0;

    if(S_ISLNK(l.st_mode)) {
        veejay_memset(&l, 0, sizeof(struct stat));

        if(stat(path, &l) < 0)
            return 0;
    }

    if(S_ISDIR(l.st_mode))
        return 1;

    if(S_ISREG(l.st_mode) && is_img(path) && *shapeidx < maxshapes) {
        shapelist[*shapeidx] = strdup(path);

        if(shapelist[*shapeidx])
            *shapeidx = *shapeidx + 1;
    }

    return 0;
}

static int find_shapes(char *path, char **shapelist, int *shapeidx, int maxshapes)
{
    if(!path || !shapelist || !shapeidx || *shapeidx >= maxshapes)
        return 0;

    struct dirent **files = NULL;
    int n = scandir(path, &files, NULL, alphasort);

    if(n < 0)
        return 0;

    char tmp[2048];

    while(n--) {
        if(strcmp(files[n]->d_name, ".") != 0 &&
           strcmp(files[n]->d_name, "..") != 0)
        {
            snprintf(tmp, sizeof(tmp), "%s/%s", path, files[n]->d_name);

            if(find_shape_file(tmp, shapelist, shapeidx, maxshapes) && *shapeidx < maxshapes)
                find_shapes(tmp, shapelist, shapeidx, maxshapes);
        }

        free(files[n]);
    }

    free(files);

    return 1;
}

static void load_shapes(char **shapelist, int *shapeidx, int maxshapes)
{
    char *home = getenv("HOME");
    char path[2048];

    if(home) {
        snprintf(path, sizeof(path), "%s/.veejay/shapes", home);
        find_shapes(path, shapelist, shapeidx, maxshapes);
    }

    find_shapes("/usr/local/share/veejay/shapes", shapelist, shapeidx, maxshapes);
    find_shapes("/usr/share/veejay/shapes", shapelist, shapeidx, maxshapes);
}

static shape_t *init_shape_loader(void)
{
    shape_t *s = (shape_t*)vj_calloc(sizeof(shape_t));

    if(!s)
        return NULL;

    s->shapelist = (char**)vj_calloc(sizeof(char*) * MAX_NUMBER_OF_SHAPES);

    if(!s->shapelist) {
        free(s);
        return NULL;
    }

    s->currentshape = -1;
    s->shape_min = 0;
    s->shape_max = 255;
    s->shape_completed = 0;
    s->n_threads = 1;
    s->have_smooth = 0;

    load_shapes(s->shapelist, &(s->shapeidx), MAX_NUMBER_OF_SHAPES);

    return s;
}

static void free_shape_loader(shape_t *s)
{
    if(!s)
        return;

    if(s->shapelist) {
        for(int i = 0; i < MAX_NUMBER_OF_SHAPES; i++) {
            if(s->shapelist[i])
                free(s->shapelist[i]);
        }

        free(s->shapelist);
    }

    free(s);
}

static void *change_shape(shape_t *s, void *oldshape, int shape, int w, int h)
{
    if(shape < 0 || shape >= s->shapeidx || !s->shapelist[shape])
        return oldshape;

    void *newshape = vj_picture_open(s->shapelist[shape], w, h, PIX_FMT_GRAY8);

    if(!newshape)
        return oldshape;

    if(oldshape)
        vj_picture_cleanup(oldshape);

    return newshape;
}

static char **get_shapelist_hints(char **shapelist, int count)
{
    if(count <= 0)
        return NULL;

    char **result = (char**)vj_calloc(sizeof(char*) * (count + 1));

    if(!result)
        return NULL;

    for(int i = 0; i < count; i++) {
        char *copy = strdup(shapelist[i] ? shapelist[i] : "");
        char *ptr = basename(copy);

        result[i] = strdup(ptr ? ptr : "");

        free(copy);

        if(!result[i])
            result[i] = strdup("");
    }

    result[count] = NULL;

    return result;
}

static void free_shapelist_hints(char **hints, int count)
{
    if(!hints)
        return;

    for(int i = 0; i < count; i++) {
        if(hints[i])
            free(hints[i]);
    }

    free(hints);
}

static void shape_find_min_max(uint8_t *restrict data, const int len, int *min, int *max)
{
    int a = 256;
    int b = 0;

    if(!data || len <= 0) {
        *min = 0;
        *max = 255;
        return;
    }

    for(int i = 0; i < len; i++) {
        if(data[i] > b)
            b = data[i];
        if(data[i] < a)
            a = data[i];
    }

    if(a > b) {
        a = 0;
        b = 255;
    }

    *min = a;
    *max = b;
}

static void shape_wipe_1(uint8_t *dst[4], uint8_t *src[4], uint8_t *pattern, const int len, const int threshold)
{
    uint8_t *restrict d0 = dst[0];
    uint8_t *restrict d1 = dst[1];
    uint8_t *restrict d2 = dst[2];

    uint8_t *restrict s0 = src[0];
    uint8_t *restrict s1 = src[1];
    uint8_t *restrict s2 = src[2];

    for(int i = 0; i < len; i++) {
        if(pattern[i] < threshold) {
            d0[i] = s0[i];
            d1[i] = s1[i];
            d2[i] = s2[i];
        }
    }
}

static void shape_wipe_2(uint8_t *dst[4], uint8_t *src[4], uint8_t *pattern, const int len, const int threshold)
{
    uint8_t *restrict d0 = dst[0];
    uint8_t *restrict d1 = dst[1];
    uint8_t *restrict d2 = dst[2];

    uint8_t *restrict s0 = src[0];
    uint8_t *restrict s1 = src[1];
    uint8_t *restrict s2 = src[2];

    for(int i = 0; i < len; i++) {
        if(pattern[i] > threshold) {
            d0[i] = s0[i];
            d1[i] = s1[i];
            d2[i] = s2[i];
        }
    }
}

static void shape_wipe_musical(shape_t *s,
                               uint8_t *dst[4],
                               uint8_t *src[4],
                               const uint8_t *restrict pattern,
                               int len,
                               int threshold,
                               int direction,
                               int softness,
                               int edge_glow,
                               int beat_mix_q8)
{
    uint8_t *restrict d0 = dst[0];
    uint8_t *restrict d1 = dst[1];
    uint8_t *restrict d2 = dst[2];

    const uint8_t *restrict s0 = src[0];
    const uint8_t *restrict s1 = src[1];
    const uint8_t *restrict s2 = src[2];

    threshold = clampi(threshold, 0, 256);
    softness = clampi(softness, 0, 128);
    edge_glow = clampi(edge_glow, 0, 255);
    beat_mix_q8 = clampi(beat_mix_q8, 0, 256);

    const int edge_span = softness > 0 ? softness : (edge_glow > 0 ? 8 : 1);
    const int denom = edge_span << 1;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        const int p = pattern[i];
        const int edge = direction ? (threshold - p) : (p - threshold);
        int q8;

        if(softness <= 0) {
            q8 = edge > 0 ? 256 : 0;
        }
        else {
            q8 = ((edge + edge_span) * 256 + (denom >> 1)) / denom;
            q8 = clampi(q8, 0, 256);
        }

        if(beat_mix_q8 > 0)
            q8 += ((256 - q8) * beat_mix_q8 + 128) >> 8;

        if(q8 > 0) {
            d0[i] = (uint8_t)((((int)d0[i] * (256 - q8)) + ((int)s0[i] * q8) + 128) >> 8);
            d1[i] = (uint8_t)((((int)d1[i] * (256 - q8)) + ((int)s1[i] * q8) + 128) >> 8);
            d2[i] = (uint8_t)((((int)d2[i] * (256 - q8)) + ((int)s2[i] * q8) + 128) >> 8);
        }

        if(edge_glow > 0) {
            const int ae = edge < 0 ? -edge : edge;

            if(ae < edge_span) {
                const int g = (edge_glow * (edge_span - ae) + (edge_span >> 1)) / edge_span;

                d0[i] = shapewipe_u8((int)d0[i] + g);
            }
        }
    }
}

int shapewipe_get_num_shapes(void *ptr)
{
    shape_t *s = (shape_t*)ptr;

    if(!s || s->shapeidx <= 0)
        return 0;

    return s->shapeidx - 1;
}

int shapewipe_ready(void *ptr, int w, int h)
{
    shape_t *s = (shape_t*)ptr;

    (void)w;
    (void)h;

    return s ? s->shape_completed : 0;
}

vj_effect *shapewipe_init(int w, int h)
{
    shape_t *s = init_shape_loader();

    if(!s)
        return NULL;

    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve) {
        free_shape_loader(s);
        return NULL;
    }

    ve->num_params = SHAPEWIPE_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        free_shape_loader(s);
        return NULL;
    }

    ve->limits[0][P_SHAPE] = 0;       ve->limits[1][P_SHAPE] = s->shapeidx > 0 ? s->shapeidx - 1 : 0; ve->defaults[P_SHAPE] = 0;
    ve->limits[0][P_THRESHOLD] = 0;   ve->limits[1][P_THRESHOLD] = 256;                               ve->defaults[P_THRESHOLD] = 0;
    ve->limits[0][P_DIRECTION] = 0;   ve->limits[1][P_DIRECTION] = 1;                                 ve->defaults[P_DIRECTION] = 0;
    ve->limits[0][P_AUTOMATIC] = 0;   ve->limits[1][P_AUTOMATIC] = 1;                                 ve->defaults[P_AUTOMATIC] = 1;
    ve->limits[0][P_SOFTNESS] = 0;    ve->limits[1][P_SOFTNESS] = 128;                                ve->defaults[P_SOFTNESS] = 0;
    ve->limits[0][P_EDGE_GLOW] = 0;   ve->limits[1][P_EDGE_GLOW] = 255;                               ve->defaults[P_EDGE_GLOW] = 0;
    ve->limits[0][P_WIPE_DRIVE] = 0;  ve->limits[1][P_WIPE_DRIVE] = 1000;                             ve->defaults[P_WIPE_DRIVE] = 0;
    ve->limits[0][P_MIX_DRIVE] = 0;   ve->limits[1][P_MIX_DRIVE] = 1000;                              ve->defaults[P_MIX_DRIVE] = 0;

    ve->description = "Shape Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shape",
        "Threshold",
        "Direction",
        "Automatic",
        "Softness",
        "Edge Glow",
        "Wipe Drive",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    if(s->shapeidx > 0) {
        char **hints = get_shapelist_hints(s->shapelist, s->shapeidx);

        if(hints) {
            vje_build_value_hint_list_array(ve->hints, s->shapeidx - 1, P_SHAPE, hints);
            free_shapelist_hints(hints, s->shapeidx);
        }
    }

    vje_build_value_hint_list(ve->hints, ve->limits[1][P_DIRECTION], P_DIRECTION, "White to Black", "Black to White");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_AUTOMATIC], P_AUTOMATIC, "Manual", "Automatic");


    char *home = getenv("HOME");

    if(home)
        veejay_msg(VEEJAY_MSG_DEBUG, "Put your shape transition files (png, pgm, tiff) in %s/.veejay/shapes", home);
    else
        veejay_msg(VEEJAY_MSG_DEBUG, "Put your shape transition files (png, pgm, tiff) in ~/.veejay/shapes");


    ve->is_transition_ready_func = shapewipe_ready;

    veejay_msg(VEEJAY_MSG_DEBUG, "Loaded %d shape transitions from storage", s->shapeidx);

    free_shape_loader(s);

    (void)w;
    (void)h;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 256, 76, 100, 0, 320, 0, 1, 0, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 96, 60, 92, 220, 1400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 82, 100, 4, 420, 20, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 86, 100, 6, 480, 20, 5, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *shapewipe_malloc(int w, int h)
{
    shape_t *s = init_shape_loader();

    if(!s)
        return NULL;

    s->n_threads = vje_advise_num_threads(w * h);

    return s;
}

void shapewipe_free(void *ptr)
{
    shape_t *s = (shape_t*)ptr;

    if(s->selected_shape)
        vj_picture_cleanup(s->selected_shape);

    free_shape_loader(s);
}

static int shapewipe_apply1(void *ptr,
                            VJFrame *frame,
                            VJFrame *frame2,
                            double timecode,
                            int shape,
                            int threshold,
                            int direction,
                            int automatic,
                            int softness,
                            int edge_glow,
                            int wipe_drive,
                            int mix_drive)
{
    shape_t *s = (shape_t*)ptr;

    if(s->shapeidx <= 0)
        return 0;

    shape = clampi(shape, 0, s->shapeidx - 1);
    threshold = clampi(threshold, 0, 256);
    direction = direction ? 1 : 0;
    automatic = automatic ? 1 : 0;
    softness = clampi(softness, 0, 128);
    edge_glow = clampi(edge_glow, 0, 255);
    wipe_drive = clampi(wipe_drive, 0, 1000);
    mix_drive = clampi(mix_drive, 0, 1000);

    if(shape != s->currentshape) {
        void *newshape = change_shape(s, s->selected_shape, shape, frame->width, frame->height);

        if(newshape == s->selected_shape && s->currentshape != shape) {
            veejay_msg(0, "Unable to read %s", s->shapelist[shape]);
            return 0;
        }

        s->selected_shape = newshape;
        s->currentshape = shape;
        s->have_smooth = 0;

        VJFrame *tmp = vj_picture_get(s->selected_shape);

        if(!tmp || !tmp->data[0])
            return 0;

        shape_find_min_max(tmp->data[0], tmp->len, &(s->shape_min), &(s->shape_max));
    }

    VJFrame *src = vj_picture_get(s->selected_shape);

    if(!src || !src->data[0])
        return 0;

    int base_threshold = threshold;
    int done_threshold = threshold;
    int range = s->shape_max - s->shape_min;

    if(range <= 0)
        range = 1;

    if(timecode < 0.0)
        timecode = 0.0;
    else if(timecode > 1.0)
        timecode = 1.0;

    if(direction) {
        if(automatic) {
            done_threshold = (int)(timecode * (double)range) + s->shape_min;
            base_threshold = done_threshold + threshold;
        }
    }
    else {
        if(automatic) {
            done_threshold = (int)((1.0 - timecode) * (double)range) + s->shape_min;
            base_threshold = done_threshold - threshold;
        }
    }

    base_threshold = clampi(base_threshold, 0, 256);
    done_threshold = clampi(done_threshold, 0, 256);

    const int musical_active = softness > 0 || edge_glow > 0 ||
                               wipe_drive > 0 || mix_drive > 0 ||
                               threshold > 0;

    if(!musical_active) {
        if(direction) {
            shape_wipe_1(frame->data, frame2->data, src->data[0], frame->len, done_threshold);

            return done_threshold >= s->shape_max;
        }

        shape_wipe_2(frame->data, frame2->data, src->data[0], frame->len, done_threshold);

        return done_threshold <= s->shape_min;
    }

    const float lane_a = 0.26f;

    if(!s->have_smooth) {
        s->sm_threshold = (float)base_threshold;
        s->sm_softness = (float)softness;
        s->sm_glow = (float)edge_glow;
        s->sm_wipe_drive = (float)wipe_drive;
        s->sm_mix_drive = (float)mix_drive;
        s->have_smooth = 1;
    }

    shapewipe_smooth_to(&s->sm_threshold, (float)base_threshold, lane_a);
    shapewipe_smooth_to(&s->sm_softness, (float)softness, lane_a);
    shapewipe_smooth_to(&s->sm_glow, (float)edge_glow, lane_a);
    shapewipe_smooth_to(&s->sm_wipe_drive, (float)wipe_drive, lane_a);
    shapewipe_smooth_to(&s->sm_mix_drive, (float)mix_drive, lane_a);

    const int wipe_q = clampi((int)(s->sm_wipe_drive + 0.5f), 0, 1000);
    const int advance = ((wipe_q * range) + 500) / 1000;

    int effective_threshold = clampi((int)(s->sm_threshold + 0.5f), 0, 256);

    effective_threshold += direction ? advance : -advance;
    effective_threshold = clampi(effective_threshold, 0, 256);

    int effective_softness = clampi((int)(s->sm_softness + 0.5f), 0, 128);

    effective_softness += ((wipe_q * 32) + 500) / 1000;
    effective_softness = clampi(effective_softness, 0, 128);

    int effective_glow = clampi((int)(s->sm_glow + 0.5f), 0, 255);

    effective_glow += ((wipe_q * 96) + 500) / 1000;
    effective_glow = clampi(effective_glow, 0, 255);

    int effective_mix = clampi((int)(s->sm_mix_drive + 0.5f), 0, 1000);

    effective_mix += ((wipe_q * 180) + 500) / 1000;
    effective_mix = clampi(effective_mix, 0, 1000);

    const int mix_q8 = (effective_mix * 256 + 500) / 1000;

    shape_wipe_musical(
        s,
        frame->data,
        frame2->data,
        src->data[0],
        frame->len,
        effective_threshold,
        direction,
        effective_softness,
        effective_glow,
        mix_q8
    );

    if(direction)
        return done_threshold >= s->shape_max;

    return done_threshold <= s->shape_min;
}

void shapewipe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    shape_t *s = (shape_t*)ptr;

    s->shape_completed = shapewipe_apply1(
        ptr,
        frame,
        frame2,
        frame->timecode,
        args[P_SHAPE],
        args[P_THRESHOLD],
        args[P_DIRECTION],
        args[P_AUTOMATIC],
        args[P_SOFTNESS],
        args[P_EDGE_GLOW],
        args[P_WIPE_DRIVE],
        args[P_MIX_DRIVE]
    );
}

int shapewipe_process(void *ptr,
                      VJFrame *frame,
                      VJFrame *frame2,
                      double timecode,
                      int shape,
                      int threshold,
                      int direction,
                      int automatic)
{
    shape_t *s = (shape_t*)ptr;

    s->shape_completed = shapewipe_apply1(
        ptr,
        frame,
        frame2,
        timecode,
        shape,
        threshold,
        direction,
        automatic,
        0,
        0,
        0,
        0
    );

    return s->shape_completed;
}
