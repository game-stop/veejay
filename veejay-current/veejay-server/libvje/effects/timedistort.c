/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * TimeDistortionTV - scratch the surface and playback old images.
 * Copyright (C) 2005 Ryo-ta
 *
 * Ported and arranged by Kentaro Fukuchi
 * Ported and modified by Niels Elburg 
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
#include "timedistort.h"
#include <libvje/internal.h>
#include <libvje/effects/motionmap.h>
#include <stdint.h>

#define TD_MAX_PLANES 256

#define TD_PARAMS 5
#define P_VALUE       0
#define P_TIME_DEPTH  1
#define P_SCRATCH     2
#define P_TRAIL_HOLD  3
#define P_DEPTH_DRIVE 4

typedef struct {
    int n__;
    int N__;

    uint8_t *region;
    uint8_t *maps;
    uint8_t *diff;
    uint8_t *prev;
    uint8_t *blur;

    uint8_t *planes[3];
    uint8_t *planetableY[TD_MAX_PLANES];
    uint8_t *planetableU[TD_MAX_PLANES];
    uint8_t *planetableV[TD_MAX_PLANES];

    uint8_t *warptime[2];

    int plane;
    int warptimeFrame;
    int have_bg;
    int plane_populated;
    int n_planes;
    int plane_mask;
    int n_threads;
    int len;

    float eff_value;
    float eff_time_depth;
    float eff_scratch;
    float eff_trail_hold;
    float eff_depth_drive;
    int eff_initialized;

    void *motionmap;
} timedistort_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}





static inline int td_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float coef = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * coef;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}


vj_effect *timedistort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TD_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_VALUE] = 5;
    ve->limits[1][P_VALUE] = 100;
    ve->defaults[P_VALUE] = 40;

    ve->limits[0][P_TIME_DEPTH] = 0;
    ve->limits[1][P_TIME_DEPTH] = 1000;
    ve->defaults[P_TIME_DEPTH] = 760;

    ve->limits[0][P_SCRATCH] = 0;
    ve->limits[1][P_SCRATCH] = 1000;
    ve->defaults[P_SCRATCH] = 0;

    ve->limits[0][P_TRAIL_HOLD] = 0;
    ve->limits[1][P_TRAIL_HOLD] = 1000;
    ve->defaults[P_TRAIL_HOLD] = 1000;

    ve->limits[0][P_DEPTH_DRIVE] = 0;
    ve->limits[1][P_DEPTH_DRIVE] = 1000;
    ve->defaults[P_DEPTH_DRIVE] = 220;

    ve->description = "TimeDistortionTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Value",
        "Time Depth",
        "Scratch Gain",
        "Trail Hold",
        "Depth Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 5,   100,  12, 46,  900, 3200, 0,  74,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         180, 1000, 18, 68,  800, 4200, 0,  88,
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS,                                                   120, 1000, 16, 62,  700, 2800, 0,  82,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         420, 1000, 12, 48, 1200, 5200, 0,  78,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         160, 1000, 18, 72,  600, 2600, 0,  94
    );

    (void) w;
    (void) h;

    return ve;
}

static void timedistort_init_plane_tables(timedistort_t *td, int len)
{
    for(int i = 0; i < td->n_planes; i++) {
        td->planetableY[i] = td->planes[0] + ((size_t)len * (size_t)i);
        td->planetableU[i] = td->planes[1] + ((size_t)len * (size_t)i);
        td->planetableV[i] = td->planes[2] + ((size_t)len * (size_t)i);
    }
}

void *timedistort_malloc(int w, int h)
{
    timedistort_t *td;
    const int try_planes[] = { 256, 128, 64 };
    const int len = w * h;

    td = (timedistort_t*) vj_calloc(sizeof(timedistort_t));
    if(!td)
        return NULL;

    td->len = len;

    for(int t = 0; t < 3; t++) {
        const int planes = try_planes[t];
        const size_t total = ((size_t)len * 3u) +
                             ((size_t)planes * 3u * (size_t)len) +
                             ((size_t)len * 2u);

        td->region = (uint8_t*) vj_malloc(total);
        if(td->region) {
            td->n_planes = planes;
            td->plane_mask = planes - 1;
            break;
        }
    }

    if(!td->region) {
        free(td);
        return NULL;
    }

    td->maps = td->region;
    td->diff = td->maps;
    td->prev = td->diff + len;
    td->blur = td->prev + len;

    td->planes[0] = td->maps + ((size_t)len * 3u);
    td->planes[1] = td->planes[0] + ((size_t)td->n_planes * (size_t)len);
    td->planes[2] = td->planes[1] + ((size_t)td->n_planes * (size_t)len);

    td->warptime[0] = td->planes[2] + ((size_t)td->n_planes * (size_t)len);
    td->warptime[1] = td->warptime[0] + len;

    veejay_memset(td->maps, 0, (size_t)len * 3u);
    veejay_memset(td->planes[0], 0,   (size_t)td->n_planes * (size_t)len);
    veejay_memset(td->planes[1], 128, (size_t)td->n_planes * (size_t)len);
    veejay_memset(td->planes[2], 128, (size_t)td->n_planes * (size_t)len);
    veejay_memset(td->warptime[0], 0, (size_t)len * 2u);

    timedistort_init_plane_tables(td, len);

    td->plane = 0;
    td->warptimeFrame = 0;
    td->have_bg = 0;
    td->plane_populated = 0;
    td->n__ = 0;
    td->N__ = 0;
    td->motionmap = NULL;

    td->eff_value = 40.0f;
    td->eff_time_depth = 760.0f;
    td->eff_scratch = 0.0f;
    td->eff_trail_hold = 1000.0f;
    td->eff_depth_drive = 220.0f;
    td->eff_initialized = 0;

    td->n_threads = vje_advise_num_threads(len);

    return (void*) td;
}

void timedistort_free(void *ptr)
{
    timedistort_t *td = (timedistort_t*) ptr;

    free(td->region);
    free(td);
}

int timedistort_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP;
}

void timedistort_set_motionmap(void *ptr, void *priv)
{
    timedistort_t *td = (timedistort_t*) ptr;

    td->motionmap = priv;
}


static void timedistort_soft_bg_seed(timedistort_t *td, const uint8_t *restrict src, int w, int h)
{
    uint8_t *restrict dst = td->prev;

    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = src + ym * w;
        const uint8_t *restrict r1 = src + y  * w;
        const uint8_t *restrict r2 = src + yp * w;

        uint8_t *restrict out = dst + y * w;

        for(int x = 0; x < w; x++) {
            const int xm = (x > 0) ? x - 1 : x;
            const int xp = (x < w - 1) ? x + 1 : x;

            const int sum =
                (int)r0[xm] + (int)r0[x] + (int)r0[xp] +
                (int)r1[xm] + (int)r1[x] + (int)r1[xp] +
                (int)r2[xm] + (int)r2[x] + (int)r2[xp];

            out[x] = (uint8_t)((sum + 4) / 9);
        }
    }
}

static void timedistort_soft_bg(timedistort_t *td, const uint8_t *restrict src, int w, int h)
{
    uint8_t *restrict dst = td->prev;

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = src + ym * w;
        const uint8_t *restrict r1 = src + y  * w;
        const uint8_t *restrict r2 = src + yp * w;

        uint8_t *restrict out = dst + y * w;

        for(int x = 0; x < w; x++) {
            const int xm = (x > 0) ? x - 1 : x;
            const int xp = (x < w - 1) ? x + 1 : x;

            const int sum =
                (int)r0[xm] + (int)r0[x] + (int)r0[xp] +
                (int)r1[xm] + (int)r1[x] + (int)r1[xp] +
                (int)r2[xm] + (int)r2[x] + (int)r2[xp];

            out[x] = (uint8_t)((sum + 4) / 9);
        }
    }
}

static void timedistort_build_diff(timedistort_t *td,
                                   const uint8_t *restrict current,
                                   int threshold,
                                   int gain_q8,
                                   int len)
{
    uint8_t *restrict diff = td->diff;
    const uint8_t *restrict prev = td->prev;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        int d = (int)current[i] - (int)prev[i];

        if(d < 0)
            d = -d;

        d -= threshold;

        if(d <= 0) {
            diff[i] = 0;
        } else {
            int v = (d * gain_q8 + 128) >> 8;

            if(v > 255)
                v = 255;

            diff[i] = (uint8_t)v;
        }
    }
}

void timedistort_apply(void *ptr, VJFrame *frame, int *args)
{
    timedistort_t *td = (timedistort_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int value_arg = args[P_VALUE];
    const int time_depth_arg = args[P_TIME_DEPTH];
    const int scratch_arg = args[P_SCRATCH];
    const int trail_hold_arg = args[P_TRAIL_HOLD];
    const int depth_drive_arg = args[P_DEPTH_DRIVE];

    if(!td->eff_initialized) {
        td->eff_value = (float)value_arg;
        td->eff_time_depth = (float)time_depth_arg;
        td->eff_scratch = (float)scratch_arg;
        td->eff_trail_hold = (float)trail_hold_arg;
        td->eff_depth_drive = (float)depth_drive_arg;
        td->eff_initialized = 1;
    }

    const float fast = 0.26f;
    const float slow = 0.080f;

    const int value_s = td_smooth_i(&td->eff_value, value_arg, fast, slow);
    const int time_depth_s = td_smooth_i(&td->eff_time_depth, time_depth_arg, fast * 0.68f, slow);
    const int scratch_s = td_smooth_i(&td->eff_scratch, scratch_arg, fast * 0.90f, slow);
    const int trail_hold_s = td_smooth_i(&td->eff_trail_hold, trail_hold_arg, fast * 0.58f, slow);
    const int depth_drive_s = td_smooth_i(&td->eff_depth_drive, depth_drive_arg, fast * 1.08f, slow);

    const int musical_depth = clampi(depth_drive_s, 0, 1000);
    const int threshold_drop = ((scratch_s * 26) + (musical_depth * 34) + 500) / 1000;
    const int value = clampi(value_s - threshold_drop, 1, 100);
    const int diff_gain_q8 = clampi(256 + ((scratch_s * 560) / 1000) + ((musical_depth * 540) / 1000), 256, 1536);
    int depth_q = time_depth_s + ((musical_depth * 360 + 500) / 1000);

    depth_q = clampi(depth_q, 0, 1000);

    int hold_q = trail_hold_s + ((musical_depth * 130 + 500) / 1000);

    hold_q = clampi(hold_q, 0, 1000);

    int interpolate = 0;
    int motion = 0;
    int update_internal_diff = 0;

    uint8_t *restrict diff = td->diff;

    if(td->motionmap && motionmap_active(td->motionmap)) {
        int tmp1 = value;
        int tmp2 = value;

        motionmap_scale_to(
            td->motionmap,
            255,
            255,
            1,
            1,
            &tmp1,
            &tmp2,
            &(td->n__),
            &(td->N__)
        );

        uint8_t *mm = motionmap_bgmap(td->motionmap);
        if(mm) {
            diff = mm;
            motion = 1;
        } else {
            td->n__ = 0;
            td->N__ = 0;
        }
    } else {
        td->n__ = 0;
        td->N__ = 0;

        if(!td->have_bg) {
            timedistort_soft_bg_seed(td, Y, width, height);

            veejay_memcpy(td->planetableY[0], Y,  len);
            veejay_memcpy(td->planetableU[0], Cb, len);
            veejay_memcpy(td->planetableV[0], Cr, len);

            veejay_memset(td->diff, 0, len);
            veejay_memset(td->warptime[0], 0, len);
            veejay_memset(td->warptime[1], 0, len);

            td->have_bg = 1;
            td->plane = 1 & td->plane_mask;
            td->plane_populated = 1;
            return;
        }

        update_internal_diff = 1;
        diff = td->diff;
    }

    if(!(td->n__ == td->N__ || td->n__ == 0))
        interpolate = 1;

    veejay_memcpy(td->planetableY[td->plane], Y,  len);
    veejay_memcpy(td->planetableU[td->plane], Cb, len);
    veejay_memcpy(td->planetableV[td->plane], Cr, len);

    if(td->plane_populated < td->n_planes)
        td->plane_populated++;

    uint8_t *restrict wt_old = td->warptime[td->warptimeFrame];
    uint8_t *restrict wt_new = td->warptime[td->warptimeFrame ^ 1];

#pragma omp parallel num_threads(td->n_threads)
    {
        if(update_internal_diff) {
            timedistort_build_diff(td, Y, value, diff_gain_q8, len);
            timedistort_soft_bg(td, Y, width, height);
        }

#pragma omp for schedule(static)
        for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            int sum = 0;
            int count = 0;

            if(y > 0) {
                sum += wt_old[idx - width];
                count++;
            }

            if(y < height - 1) {
                sum += wt_old[idx + width];
                count++;
            }

            if(x > 0) {
                sum += wt_old[idx - 1];
                count++;
            }

            if(x < width - 1) {
                sum += wt_old[idx + 1];
                count++;
            }

            if(count > 0)
                sum = (sum + (count >> 1)) / count;

            if(sum > 0) {
                const int decay_step = 1 + (((1000 - hold_q) * 5 + 500) / 1000);

                sum -= decay_step;

                if(sum < 0)
                    sum = 0;
            }

            wt_new[idx] = (uint8_t)sum;
        }
    }

    const int n_planes = td->n_planes;
    const int plane_mask = td->plane_mask;
    const int plane_now = td->plane;
    const int populated = td->plane_populated;

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
        int age = wt_new[i];

        if(diff[i]) {
            const int max_age = 1 + (((n_planes - 2) * depth_q + 500) / 1000);
            int inject_age = (max_age * (int)diff[i] + 127) / 255;

            if(inject_age < 1)
                inject_age = 1;

            if(inject_age > age)
                age = inject_age;

            wt_new[i] = (uint8_t)age;
        }

        if(populated < n_planes && age >= populated)
            age = populated - 1;

        if(age < 0)
            age = 0;

        const int n_plane = (plane_now - age + n_planes) & plane_mask;

        Y[i]  = td->planetableY[n_plane][i];
        Cb[i] = td->planetableU[n_plane][i];
            Cr[i] = td->planetableV[n_plane][i];
        }
    }

    td->plane = (td->plane + 1) & td->plane_mask;
    td->warptimeFrame ^= 1;

    if(interpolate)
        motionmap_interpolate_frame(td->motionmap, frame, td->N__, td->n__);

    if(motion)
        motionmap_store_frame(td->motionmap, frame);
}
