/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "solarize.h"

#define SOLARIZE_PARAMS 7

#define P_THRESHOLD   0
#define P_MODE        1
#define P_SOFTNESS    2
#define P_CONTRAST    3
#define P_CHROMA      4
#define P_DEPTH_DRIVE 5
#define P_COLOR_DRIVE 6

typedef struct {
    float threshold;
    float softness;
    float contrast;
    float chroma;
    float depth_drive;
    float color_drive;
    int initialized;
} solarize_t;

static inline int solarize_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t solarize_u8(int v)
{
    return (uint8_t)solarize_clampi(v, 0, 255);
}

static inline uint8_t solarize_uv(int v)
{
    return (uint8_t)CLAMP_UV(v);
}

static inline int solarize_abs_i(int v)
{
    const int m = v >> 31;
    return (v ^ m) - m;
}


static inline int solarize_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline int solarize_mix_q8(int a, int b, int q8)
{
    return a + (((b - a) * q8 + 128) >> 8);
}

static inline int solarize_contrast(int y, int contrast)
{
    int out = (((y - 128) * contrast) >> 8) + 128;
    return solarize_clampi(out, 0, 255);
}

vj_effect *solarize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SOLARIZE_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_THRESHOLD]   = 16;
    ve->defaults[P_MODE]        = 0;
    ve->defaults[P_SOFTNESS]    = 32;
    ve->defaults[P_CONTRAST]    = 250;
    ve->defaults[P_CHROMA]      = 1000;
    ve->defaults[P_DEPTH_DRIVE] = 0;
    ve->defaults[P_COLOR_DRIVE] = 0;

    ve->limits[0][P_THRESHOLD]   = 1;    ve->limits[1][P_THRESHOLD]   = 255;
    ve->limits[0][P_MODE]        = 0;    ve->limits[1][P_MODE]        = 2;
    ve->limits[0][P_SOFTNESS]    = 1;    ve->limits[1][P_SOFTNESS]    = 128;
    ve->limits[0][P_CONTRAST]    = 0;    ve->limits[1][P_CONTRAST]    = 1000;
    ve->limits[0][P_CHROMA]      = 0;    ve->limits[1][P_CHROMA]      = 1000;
    ve->limits[0][P_DEPTH_DRIVE] = 0;    ve->limits[1][P_DEPTH_DRIVE] = 1000;
    ve->limits[0][P_COLOR_DRIVE] = 0;    ve->limits[1][P_COLOR_DRIVE] = 1000;

    ve->description = "Solarize (Sabattier)";
    ve->sub_format = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Mode",
        "Softness",
        "Contrast",
        "Chroma Amount",
        "Depth Drive",
        "Color Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Desaturated",
        "Luma Only",
        "Color Sabattier"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 220, 86, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 112, 90, 100, 6, 460, 24, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 180, 1000, 88, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 240, 1000, 72, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 84, 100, 4, 480, 24, 5, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void) w;
    (void) h;

    return ve;
}

void *solarize_malloc(int w, int h)
{
    solarize_t *s = (solarize_t*) vj_calloc(sizeof(solarize_t));
    if(!s)
        return NULL;

    s->threshold = 16.0f;
    s->softness = 32.0f;
    s->contrast = 250.0f;
    s->chroma = 1000.0f;
    s->depth_drive = 0.0f;
    s->color_drive = 0.0f;
    s->initialized = 0;

    (void) w;
    (void) h;

    return (void*) s;
}

void solarize_free(void *ptr)
{
    free(ptr);
}

static void solarize_process(void *ptr, VJFrame *frame, int *args, int force_mode)
{
    solarize_t *s = (solarize_t*) ptr;

    const int len = frame->len;

    int threshold = solarize_clampi(args[P_THRESHOLD], 1, 255);
    int softness = solarize_clampi(args[P_SOFTNESS], 1, 128);
    int contrast_arg = solarize_clampi(args[P_CONTRAST], 0, 1000);
    int chroma_arg = solarize_clampi(args[P_CHROMA], 0, 1000);
    int depth_drive = solarize_clampi(args[P_DEPTH_DRIVE], 0, 1000);
    int color_drive = solarize_clampi(args[P_COLOR_DRIVE], 0, 1000);

    const float param_attack = 0.30f;
    const float param_release = 0.085f;

    if(!s->initialized) {
        s->threshold = (float)threshold;
        s->softness = (float)softness;
        s->contrast = (float)contrast_arg;
        s->chroma = (float)chroma_arg;
        s->depth_drive = (float)depth_drive;
        s->color_drive = (float)color_drive;
        s->initialized = 1;
    } else {
        threshold = solarize_smooth_i(&s->threshold, threshold, param_attack, param_release);
        softness = solarize_smooth_i(&s->softness, softness, param_attack, param_release);
        contrast_arg = solarize_smooth_i(&s->contrast, contrast_arg, param_attack, param_release);
        chroma_arg = solarize_smooth_i(&s->chroma, chroma_arg, param_attack, param_release);
        depth_drive = solarize_smooth_i(&s->depth_drive, depth_drive, param_attack, param_release);
        color_drive = solarize_smooth_i(&s->color_drive, color_drive, param_attack, param_release);
    }

    threshold = solarize_clampi(threshold, 1, 255);
    softness = solarize_clampi(softness, 1, 128);
    contrast_arg = solarize_clampi(contrast_arg, 0, 1000);
    chroma_arg = solarize_clampi(chroma_arg, 0, 1000);
    depth_drive = solarize_clampi(depth_drive, 0, 1000);
    color_drive = solarize_clampi(color_drive, 0, 1000);

    threshold += (depth_drive * 96 + 500) / 1000;
    threshold = solarize_clampi(threshold, 1, 255);

    softness += (depth_drive * 48 + 500) / 1000;
    softness = solarize_clampi(softness, 1, 128);

    contrast_arg += (depth_drive * 320 + 500) / 1000;
    contrast_arg = solarize_clampi(contrast_arg, 0, 1000);

    chroma_arg += ((1000 - chroma_arg) * color_drive + 500) / 1000;
    chroma_arg = solarize_clampi(chroma_arg, 0, 1000);

    const int contrast_q8 = 256 + ((contrast_arg * 256 + 500) / 1000);
    const int color_q8 = solarize_clampi((color_drive * 224 + 500) / 1000, 0, 256);
    const int inv_softness = (256 + (softness >> 1)) / softness;
    const int chroma_q = chroma_arg;
    const int mode = solarize_clampi(force_mode, 0, 2);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++) {
        const int y = Y[i];
        const int d = y - threshold;
        int t = d * inv_softness;

        if(t < -256)
            t = -256;
        else if(t > 256)
            t = 256;

        const int blend = (t + 256) >> 1;
        const int inv = 255 - y;
        int out = ((256 - blend) * y + blend * inv) >> 8;

        out = solarize_contrast(out, contrast_q8);
        Y[i] = solarize_u8(out);

        if(mode == 1 && color_q8 <= 0)
            continue;

        int cb = Cb[i];
        int cr = Cr[i];

        if(mode == 2) {
            int chroma_blend = (blend * chroma_q + 2000) / 4000;
            chroma_blend += (blend * color_q8 + 32768) >> 16;
            chroma_blend = solarize_clampi(chroma_blend, 0, 256);

            cb = solarize_mix_q8(cb, 255 - cb, chroma_blend);
            cr = solarize_mix_q8(cr, 255 - cr, chroma_blend);
        } else if(mode == 0) {
            const int dist = solarize_abs_i(d);
            int sat = 256 - (((dist > softness) ? softness : dist) * inv_softness);
            sat = (sat * chroma_q + 500) / 1000;
            sat = solarize_clampi(sat, 0, 256);

            cb = 128 + ((((int)cb - 128) * sat) >> 8);
            cr = 128 + ((((int)cr - 128) * sat) >> 8);
        }

        if(color_q8 > 0) {
            const int cb0 = cb;
            const int cr0 = cr;
            const int phase_q8 = (blend * color_q8 + 128) >> 8;
            const int twist_q8 = color_q8 >> 1;

            cb = solarize_mix_q8(cb0, 255 - cr0, phase_q8);
            cr = solarize_mix_q8(cr0, cb0, twist_q8);
        }

        Cb[i] = solarize_uv(cb);
        Cr[i] = solarize_uv(cr);
    }
}

void solarize_apply1(void *ptr, VJFrame *frame, int *args)
{
    solarize_process(ptr, frame, args, 0);
}

void solarize_apply_luma(void *ptr, VJFrame *frame, int *args)
{
    solarize_process(ptr, frame, args, 1);
}

void solarize_apply_color(void *ptr, VJFrame *frame, int *args)
{
    solarize_process(ptr, frame, args, 2);
}

void solarize_apply(void *ptr, VJFrame *frame, int *args)
{
    const int mode = args[P_MODE];

    switch(mode) {
        case 1:
            solarize_apply_luma(ptr, frame, args);
            break;
        case 2:
            solarize_apply_color(ptr, frame, args);
            break;
        default:
            solarize_apply1(ptr, frame, args);
            break;
    }
}
