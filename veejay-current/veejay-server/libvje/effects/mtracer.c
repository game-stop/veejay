/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "mtracer.h"
#include "magicoverlays.h"
#include "internal.h"

#define MTRACER_PARAMS 7

#define P_MODE           0
#define P_STRENGTH       1
#define P_CLASSIC        2
#define P_CHARACTER      3
#define P_DECAY          4
#define P_MOTION_ONLY    5
#define P_FRAME2_OPACITY 6

#define MTRACER_MAX_MODE 34

typedef struct {
    uint8_t *mtrace_buffer[4];
    uint8_t *mode_buffer;
    int started;
    int mode_transition;
    int mode_transition_len;
    int prev_mode;
    int n_threads;
} m_tracer_t;

static inline int mtracer_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int mtracer_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t mtracer_y(int v)
{
    return (uint8_t)CLAMP_Y(v);
}

static inline uint8_t mtracer_u8(int v)
{
    return (uint8_t)mtracer_clampi(v, 0, 255);
}

static inline uint8_t mtracer_div255(int v)
{
    return (uint8_t)(((v + 128) + ((v + 128) >> 8)) >> 8);
}

static inline uint8_t mtracer_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    return mtracer_div255((int)a * inv + (int)b * opacity);
}

static inline uint8_t mtracer_screen(int a, int b)
{
    return (uint8_t)(255 - mtracer_div255((255 - a) * (255 - b)));
}

static inline uint8_t mtracer_multiply(int a, int b)
{
    return mtracer_div255(a * b);
}

static inline uint8_t mtracer_overlay_pixel(int mode, int a, int b)
{
    int c;

    switch(mode) {
        case VJ_EFFECT_BLEND_ADDITIVE:
            return mtracer_u8(a + b);

        case VJ_EFFECT_BLEND_SUBSTRACTIVE:
            return mtracer_y(a - b);

        case VJ_EFFECT_BLEND_MULTIPLY:
            return mtracer_multiply(a, b);

        case VJ_EFFECT_BLEND_DIVIDE:
            return b > pixel_Y_lo_ ? (uint8_t)(a / b) : (uint8_t)a;

        case VJ_EFFECT_BLEND_LIGHTEN:
            return (uint8_t)(a > b ? a : b);

        case VJ_EFFECT_BLEND_DIFFERENCE:
            return (uint8_t)mtracer_absi(a - b);

        case VJ_EFFECT_BLEND_DIFFNEGATE:
            return (uint8_t)(255 - mtracer_absi((255 - a) - b));

        case VJ_EFFECT_BLEND_EXCLUSIVE:
            return mtracer_u8(a + b - ((2 * a * b) / 255));

        case VJ_EFFECT_BLEND_BASECOLOR:
            c = (a * b) >> 8;
            return mtracer_y(c + ((a * ((255 - (((255 - a) * (255 - b)) >> 8)) - c)) >> 8));

        case VJ_EFFECT_BLEND_FREEZE:
            return b > pixel_Y_lo_ ? mtracer_y(255 - (((255 - a) * (255 - a)) / b)) : (uint8_t)a;

        case VJ_EFFECT_BLEND_UNFREEZE:
            return a > pixel_Y_lo_ ? mtracer_y(255 - (((255 - b) * (255 - b)) / a)) : (uint8_t)a;

        case VJ_EFFECT_BLEND_RELADD:
            return (uint8_t)((a >> 1) + (b >> 1));

        case VJ_EFFECT_BLEND_RELSUB:
            return (uint8_t)((a - b + 255) >> 1);

        case VJ_EFFECT_BLEND_RELADDLUM:
            return mtracer_y((a >> 1) + (b >> 1));

        case VJ_EFFECT_BLEND_RELSUBLUM:
            return (uint8_t)((a - b + 255) >> 1);

        case VJ_EFFECT_BLEND_MAXSEL:
            return (uint8_t)(b > a ? b : a);

        case VJ_EFFECT_BLEND_MINSEL:
            return (uint8_t)(b < a ? b : a);

        case VJ_EFFECT_BLEND_MINSUBSEL:
            return (uint8_t)((b < a) ? ((b - a + 255) >> 1) : ((a - b + 255) >> 1));

        case VJ_EFFECT_BLEND_MAXSUBSEL:
            return (uint8_t)((b > a) ? ((b - a + 255) >> 1) : ((a - b + 255) >> 1));

        case VJ_EFFECT_BLEND_ADDSUBSEL:
            return (uint8_t)((b < a) ? ((a + b) >> 1) : a);

        case VJ_EFFECT_BLEND_ADDAVG:
            return (uint8_t)((a + b) >> 1);

        case VJ_EFFECT_BLEND_ADDTEST2:
            return mtracer_y(a + (((2 * b) - 255) >> 1));

        case VJ_EFFECT_BLEND_ADDTEST3:
            return mtracer_y(a + (2 * b) - 255);

        case VJ_EFFECT_BLEND_ADDTEST4:
            b -= 255;
            return b <= pixel_Y_lo_ ? (uint8_t)a : (uint8_t)((a * a) / b);

        case VJ_EFFECT_BLEND_MULSUB:
            b = 255 - b;
            return b > pixel_Y_lo_ ? (uint8_t)(a / b) : (uint8_t)a;

        case VJ_EFFECT_BLEND_SOFTBURN:
            return b == 0 ? 0 : mtracer_u8(255 - (((255 - a) << 8) / b));

        case VJ_EFFECT_BLEND_INVERSEBURN:
            return a <= pixel_Y_lo_ ? (uint8_t)pixel_Y_lo_ : mtracer_y(255 - (((255 - b) >> 8) / a));

        case VJ_EFFECT_BLEND_COLORDODGE:
            return b == 255 ? 255 : mtracer_u8((a << 8) / (255 - b));

        case VJ_EFFECT_BLEND_ADDDISTORT:
            return mtracer_y(a + b);

        case VJ_EFFECT_BLEND_SUBDISTORT:
            return mtracer_y(a - b);

        case VJ_EFFECT_BLEND_ADDTEST5:
            if(a <= pixel_Y_lo_)
                c = pixel_Y_lo_;
            else
                c = 255 - ((256 - a) * (256 - a)) / a;
            if(c <= pixel_Y_lo_)
                c = pixel_Y_lo_;
            if(b <= pixel_Y_lo_)
                return (uint8_t)pixel_Y_lo_;
            return mtracer_y(255 - ((256 - c) * (256 - b)) / b);

        case VJ_EFFECT_BLEND_NEGDIV:
            return mtracer_screen(a, b);

        case VJ_EFFECT_BLEND_SCREEN:
            return mtracer_screen(a, b);

        case VJ_EFFECT_BLEND_HARDLIGHT:
            return (uint8_t)(b < 128 ? ((2 * a * b) / 255) : (255 - ((2 * (255 - a) * (255 - b)) / 255)));

        default:
            return (uint8_t)(a < 128 ? ((2 * a * b) / 255) : (255 - ((2 * (255 - a) * (255 - b)) / 255)));
    }
}

static void overlaymagic1_apply_n(VJFrame *frame, VJFrame *frame2, int mode, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = mtracer_overlay_pixel(mode, Y[i], Y2[i]);
}

void overlaymagic1_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int n)
{
    (void)ptr;

    overlaymagic1_apply_n(frame, frame2, n, vje_advise_num_threads(frame->len));
}

vj_effect *mtracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MTRACER_PARAMS;
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

    ve->limits[0][P_MODE] = 0;           ve->limits[1][P_MODE] = MTRACER_MAX_MODE; ve->defaults[P_MODE] = 0;
    ve->limits[0][P_STRENGTH] = 1;       ve->limits[1][P_STRENGTH] = 255;          ve->defaults[P_STRENGTH] = 200;
    ve->limits[0][P_CLASSIC] = 0;        ve->limits[1][P_CLASSIC] = 1;             ve->defaults[P_CLASSIC] = 0;
    ve->limits[0][P_CHARACTER] = 0;      ve->limits[1][P_CHARACTER] = 255;         ve->defaults[P_CHARACTER] = 128;
    ve->limits[0][P_DECAY] = 1;          ve->limits[1][P_DECAY] = 255;             ve->defaults[P_DECAY] = 11;
    ve->limits[0][P_MOTION_ONLY] = 0;    ve->limits[1][P_MOTION_ONLY] = 1;         ve->defaults[P_MOTION_ONLY] = 0;
    ve->limits[0][P_FRAME2_OPACITY] = 0; ve->limits[1][P_FRAME2_OPACITY] = 255;    ve->defaults[P_FRAME2_OPACITY] = 128;

    ve->description = "Magic Tracer";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Strength",
        "Use Classic Blend",
        "Character",
        "Decay Strength",
        "Motion Only",
        "Frame2 Opacity"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE,
        "Additive","Subtractive","Multiply","Divide","Lighten","Hardlight",
        "Difference","Difference Negate","Exclusive","Base","Freeze",
        "Unfreeze","Relative Add","Relative Subtract","Max select","Min select",
        "Relative Luma Add","Relative Luma Subtract","Min Subselect","Max Subselect",
        "Add Subselect","Add Average","Experimental 1","Experimental 2","Experimental 3",
        "Multisub","Softburn","Inverse Burn","Dodge","Distorted Add","Distorted Subtract",
        "Experimental 4","Negation Divide","Screen","Overlay"
    );
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_CLASSIC], P_CLASSIC, "Off", "On");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MOTION_ONLY], P_MOTION_ONLY, "Off", "On");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_INTENSITY,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       48,                 238,                18, 68,  650, 2600, 0,    90,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_CONTRAST,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       56,                 235,                14, 54,  800, 3000, 0,    78,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       18,                 230,                18, 68,  700, 3000, 0,    88,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       28,                 235,                14, 54,  800, 3000, 0,    76
    );

    return ve;
}

void mtracer_free(void *ptr)
{
    m_tracer_t *m = (m_tracer_t*) ptr;

    free(m->mtrace_buffer[0]);
    free(m);
}

void *mtracer_malloc(int w, int h)
{
    const size_t buflen = (size_t)w * (size_t)h;

    m_tracer_t *m = (m_tracer_t*) vj_calloc(sizeof(m_tracer_t));

    if(!m)
        return NULL;

    uint8_t *block = (uint8_t*) vj_malloc(buflen * 4u);

    if(!block) {
        free(m);
        return NULL;
    }

    m->mtrace_buffer[0] = block;
    m->mtrace_buffer[1] = block + buflen;
    m->mtrace_buffer[2] = block + buflen * 2u;
    m->mtrace_buffer[3] = block + buflen * 3u;
    m->mode_buffer = m->mtrace_buffer[3];

    veejay_memset(block, pixel_Y_lo_, buflen * 4u);

    m->mode_transition = 0;
    m->mode_transition_len = 12;
    m->prev_mode = 0;
    m->n_threads = vje_advise_num_threads(w * h);

    return (void*) m;
}

static void mtracer_motion_mask(const uint8_t *restrict cur,
                                const uint8_t *restrict prev,
                                uint8_t *restrict out,
                                int len,
                                int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        out[i] = (uint8_t)mtracer_absi((int)cur[i] - (int)prev[i]);
}

void mtracer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    m_tracer_t *m = (m_tracer_t*) ptr;

    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int n_threads = m->n_threads;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int mode = args[P_MODE];
    const int strength = args[P_STRENGTH];
    const int classic = args[P_CLASSIC];
    const int character = args[P_CHARACTER];
    const int decay_val = args[P_DECAY];
    const int motion_only = args[P_MOTION_ONLY];
    const int frame2_opacity = args[P_FRAME2_OPACITY];

    uint8_t *restrict feedback_buf = m->mtrace_buffer[0];
    uint8_t *restrict blended_result = m->mtrace_buffer[1];
    uint8_t *restrict prev_frame = m->mtrace_buffer[2];

    VJFrame tmp_frame;

    veejay_memcpy(&tmp_frame, frame, sizeof(VJFrame));
    tmp_frame.data[0] = blended_result;

    if(!m->started) {
        veejay_memcpy(feedback_buf, Y, len);
        veejay_memcpy(prev_frame, Y, len);
        m->prev_mode = mode;
        m->mode_transition = 0;
        m->started = 1;
    }

    if(mode != m->prev_mode) {
        veejay_memcpy(m->mode_buffer, feedback_buf, len);
        m->mode_transition = m->mode_transition_len;
        m->prev_mode = mode;
    }

    veejay_memcpy(blended_result, Y, len);
    overlaymagic1_apply_n(&tmp_frame, frame2, mode, n_threads);

    if(frame2_opacity < 255) {
#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int i = 0; i < len; i++)
            blended_result[i] = mtracer_blend255(Y[i], blended_result[i], frame2_opacity);
    }

    if(m->mode_transition > 0) {
        const int t = m->mode_transition_len - m->mode_transition;
        const int x = (t << 8) / m->mode_transition_len;
        const int alpha = (x * x * (768 - (x << 1))) >> 16;
        uint8_t *restrict mode_buf = m->mode_buffer;

#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int i = 0; i < len; i++)
            blended_result[i] = mtracer_blend255(mode_buf[i], blended_result[i], alpha);

        m->mode_transition--;
    }

    if(motion_only)
        mtracer_motion_mask(blended_result, prev_frame, blended_result, len, n_threads);

    const int combined_scale = mtracer_clampi((strength * character + 127) / 255, 1, 255);
    const int decay = 256 - (256 / decay_val);
    const int inject = 256 - decay;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int f = feedback_buf[i];
        const int b = blended_result[i];
        const int accum = ((f * decay + 128) >> 8) + ((b * combined_scale * inject + 32768) >> 16);

        feedback_buf[i] = mtracer_u8(accum);
    }

    veejay_memcpy(prev_frame, Y, len);

    if(classic) {
        tmp_frame.data[0] = feedback_buf;
        overlaymagic1_apply_n(frame, &tmp_frame, mode, n_threads);
    }
    else {
        veejay_memcpy(Y, feedback_buf, len);
    }

    veejay_memset(U, 128, uv_len);
    veejay_memset(V, 128, uv_len);
}
