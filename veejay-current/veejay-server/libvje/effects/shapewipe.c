/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
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

    if(S_ISREG(l.st_mode) && is_img(path)) {
        if(*shapeidx < maxshapes) {
            shapelist[*shapeidx] = strdup(path);
            if(shapelist[*shapeidx])
                *shapeidx = *shapeidx + 1;
        }
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

            if(find_shape_file(tmp, shapelist, shapeidx, maxshapes)) {
                if(*shapeidx < maxshapes)
                    find_shapes(tmp, shapelist, shapeidx, maxshapes);
            }
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

typedef struct {
    char **shapelist;
    int shapeidx;
    int currentshape;
    void *selected_shape;
    int shape_min;
    int shape_max;
    int shape_completed;
} shape_t;

static shape_t *init_shape_loader(void)
{
    shape_t *s = (shape_t*) vj_calloc(sizeof(shape_t));
    if(!s)
        return NULL;

    s->shapelist = (char**) vj_calloc(sizeof(char*) * MAX_NUMBER_OF_SHAPES);
    if(!s->shapelist) {
        free(s);
        return NULL;
    }

    s->currentshape = -1;
    s->shape_min = 0;
    s->shape_max = 255;
    s->shape_completed = 0;

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
    if(!s || shape < 0 || shape >= s->shapeidx || !s->shapelist[shape])
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

    char **result = (char**) vj_calloc(sizeof(char*) * (count + 1));
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

int shapewipe_get_num_shapes(void *ptr)
{
    shape_t *s = (shape_t*) ptr;
    if(!s || s->shapeidx <= 0)
        return 0;

    return s->shapeidx - 1;
}

int shapewipe_ready(void *ptr, int w, int h)
{
    shape_t *s = (shape_t*) ptr;

    (void) w;
    (void) h;

    return s ? s->shape_completed : 0;
}

vj_effect *shapewipe_init(int w, int h)
{
    shape_t *s = init_shape_loader();
    if(!s)
        return NULL;

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve) {
        free_shape_loader(s);
        return NULL;
    }

    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = s->shapeidx > 0 ? s->shapeidx - 1 : 0;
    ve->defaults[0] = 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->defaults[1] = 0;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[2] = 0;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[3] = 1;

    ve->description = "Shape Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shape",
        "Threshold",
        "Direction",
        "Automatic"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    if(s->shapeidx > 0) {
        char **hints = get_shapelist_hints(s->shapelist, s->shapeidx);

        if(hints) {
            vje_build_value_hint_list_array(
                ve->hints,
                s->shapeidx - 1,
                0,
                hints
            );

            free_shapelist_hints(hints, s->shapeidx);
        }
    }

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "White to Black",
        "Black to White"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Manual",
        "Automatic"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Shape */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY,                        0,                  256,                6, 22, 1600, 3400, 700, 35,    /* Threshold */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Direction */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Automatic */
    );

    char *home = getenv("HOME");
    if(home)
        veejay_msg(VEEJAY_MSG_DEBUG, "Put your shape transition files (png, pgm, tiff) in %s/.veejay/shapes", home);
    else
        veejay_msg(VEEJAY_MSG_DEBUG, "Put your shape transition files (png, pgm, tiff) in ~/.veejay/shapes");

    ve->is_transition_ready_func = shapewipe_ready;

    veejay_msg(VEEJAY_MSG_DEBUG, "Loaded %d shape transitions from storage", s->shapeidx);

    free_shape_loader(s);

    (void) w;
    (void) h;

    return ve;
}

void *shapewipe_malloc(int w, int h)
{
    (void) w;
    (void) h;

    return init_shape_loader();
}

void shapewipe_free(void *ptr)
{
    shape_t *s = (shape_t*) ptr;
    if(!s)
        return;

    if(s->selected_shape)
        vj_picture_cleanup(s->selected_shape);

    free_shape_loader(s);
}

static inline int shapewipe_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static int shapewipe_apply1(
    void *ptr,
    VJFrame *frame,
    VJFrame *frame2,
    double timecode,
    int shape,
    int threshold,
    int direction,
    int automatic
) {
    shape_t *s = (shape_t*) ptr;

    if(!s || !frame || !frame2 || !frame->data[0] || !frame2->data[0])
        return 0;

    if(s->shapeidx <= 0)
        return 0;

    shape = shapewipe_clampi(shape, 0, s->shapeidx - 1);
    threshold = shapewipe_clampi(threshold, 0, 256);
    direction = direction ? 1 : 0;
    automatic = automatic ? 1 : 0;

    if(shape != s->currentshape) {
        void *newshape = change_shape(s, s->selected_shape, shape, frame->width, frame->height);

        if(newshape == s->selected_shape && s->currentshape != shape) {
            veejay_msg(0, "Unable to read %s", s->shapelist[shape]);
            return 0;
        }

        s->selected_shape = newshape;
        s->currentshape = shape;

        VJFrame *tmp = vj_picture_get(s->selected_shape);
        if(!tmp || !tmp->data[0])
            return 0;

        shape_find_min_max(tmp->data[0], tmp->len, &(s->shape_min), &(s->shape_max));
    }

    VJFrame *src = vj_picture_get(s->selected_shape);
    if(!src || !src->data[0])
        return 0;

    int auto_threshold = threshold;
    int range = s->shape_max - s->shape_min;

    if(range <= 0)
        range = 1;

    if(timecode < 0.0)
        timecode = 0.0;
    else if(timecode > 1.0)
        timecode = 1.0;

    if(direction) {
        if(automatic)
            auto_threshold = (int)(timecode * (double)range) + s->shape_min;

        auto_threshold = shapewipe_clampi(auto_threshold, 0, 256);

        shape_wipe_1(frame->data, frame2->data, src->data[0], frame->len, auto_threshold);

        if(auto_threshold >= s->shape_max)
            return 1;
    } else {
        if(automatic)
            auto_threshold = (int)((1.0 - timecode) * (double)range) + s->shape_min;

        auto_threshold = shapewipe_clampi(auto_threshold, 0, 256);

        shape_wipe_2(frame->data, frame2->data, src->data[0], frame->len, auto_threshold);

        if(auto_threshold <= s->shape_min)
            return 1;
    }

    return 0;
}

void shapewipe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    if(!ptr || !frame || !frame2 || !args)
        return;

    shape_t *s = (shape_t*) ptr;

    s->shape_completed = shapewipe_apply1(
        ptr,
        frame,
        frame2,
        frame->timecode,
        args[0],
        args[1],
        args[2],
        args[3]
    );
}

int shapewipe_process(
    void *ptr,
    VJFrame *frame,
    VJFrame *frame2,
    double timecode,
    int shape,
    int threshold,
    int direction,
    int automatic
) {
    if(!ptr || !frame || !frame2)
        return 0;

    shape_t *s = (shape_t*) ptr;

    s->shape_completed = shapewipe_apply1(
        ptr,
        frame,
        frame2,
        timecode,
        shape,
        threshold,
        direction,
        automatic
    );

    return s->shape_completed;
}