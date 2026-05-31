/* veejay - Linux VeeJay
 *       (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
 *
 * Beat-ready transition wipe variant.
 */

#include "common.h"
#include "wipe.h"

#define WIPE_PARAMS 6

#define P_SPEED       0
#define P_RESTART     1
#define P_EDGE_WIDTH  2
#define P_EDGE_GLOW   3
#define P_BEAT_PUSH   4
#define P_BEAT_SMOOTH 5

typedef struct {
    int wipe_position;
    int last_restart;
    int n_threads;
    float beat_env;
} wipe_t;

static inline int wipe_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t wipe_clamp_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint8_t wipe_blend_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = wipe_clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int wipe_beat_shape(int beat_push)
{
    beat_push = wipe_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return wipe_clampi((beat_push * 30 + sq * 70 + 50) / 100, 0, 1000);
}

vj_effect *wipe_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WIPE_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int max_speed = (w > h ? w : h);
    if(max_speed < 1)
        max_speed = 1;

    int max_edge = w / 3;
    if(max_edge < 1)
        max_edge = 1;
    if(max_edge > 512)
        max_edge = 512;

    ve->defaults[P_SPEED]       = 1;
    ve->defaults[P_RESTART]     = 1;
    ve->defaults[P_EDGE_WIDTH]  = 16;
    ve->defaults[P_EDGE_GLOW]   = 24;
    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->limits[0][P_SPEED]       = 0;    ve->limits[1][P_SPEED]       = max_speed;
    ve->limits[0][P_RESTART]     = 0;    ve->limits[1][P_RESTART]     = 1;
    ve->limits[0][P_EDGE_WIDTH]  = 0;    ve->limits[1][P_EDGE_WIDTH]  = max_edge;
    ve->limits[0][P_EDGE_GLOW]   = 0;    ve->limits[1][P_EDGE_GLOW]   = 255;
    ve->limits[0][P_BEAT_PUSH]   = 0;    ve->limits[1][P_BEAT_PUSH]   = 1000;
    ve->limits[0][P_BEAT_SMOOTH] = 0;    ve->limits[1][P_BEAT_SMOOTH] = 1000;

    ve->description = "Transition Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Restart",
        "Edge Width",
        "Edge Glow",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_RESTART],
        P_RESTART,
        "Run",
        "Restart"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS,
        0, max_speed > 240 ? 240 : max_speed,
        8, 30, 1200, 3000, 0, 42, /* Speed */

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Restart */

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,
        0, max_edge,
        5, 18, 1800, 4200, 900, 20, /* Edge Width */

        VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS,
        0, 160,
        8, 32, 900, 2400, 0, 46, /* Edge Glow */

        VJ_BEAT_KICK, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,
        0, 760,
        18, 68, 80, 760, 0, 100, /* Beat Push */

        VJ_BEAT_MEMORY, VJ_BEAT_F_PHRASE_ONLY,
        260, 840,
        5, 18, 2200, 5200, 1200, 18 /* Beat Smooth */
    );

    ve->is_transition_ready_func = wipe_ready;

    return ve;
}

int wipe_ready(void *ptr, int width, int height)
{
    wipe_t *w = (wipe_t*) ptr;

    (void) height;

    if(!w || width <= 0)
        return TRANSITION_COMPLETED;

    return (w->wipe_position >= width)
        ? TRANSITION_COMPLETED
        : TRANSITION_RUNNING;
}

void *wipe_malloc(int w, int h)
{
    wipe_t *prv = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!prv)
        return NULL;

    prv->wipe_position = 0;
    prv->last_restart = 1;
    prv->beat_env = 0.0f;

    prv->n_threads = vje_advise_num_threads(w * h);
    if(prv->n_threads < 1)
        prv->n_threads = 1;

    return prv;
}

void wipe_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

void wipe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    if(!wipe || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int speed_arg  = wipe_clampi(args[P_SPEED], 0, width);
    const int restart    = args[P_RESTART] ? 1 : 0;
    const int edge_arg   = wipe_clampi(args[P_EDGE_WIDTH], 0, width);
    const int glow_arg   = wipe_clampi(args[P_EDGE_GLOW], 0, 255);
    const int beat_push  = wipe_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int smooth_arg = wipe_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    if(restart && !wipe->last_restart) {
        wipe->wipe_position = 0;
        wipe->beat_env = 0.0f;
    }

    wipe->last_restart = restart;

    const int shaped = wipe_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)smooth_arg * 0.001f;
    const float attack = 0.20f + (1.0f - smooth_t) * 0.38f;
    const float release = 0.035f + (1.0f - smooth_t) * 0.105f;

    if(target > wipe->beat_env)
        wipe->beat_env += (target - wipe->beat_env) * attack;
    else
        wipe->beat_env += (target - wipe->beat_env) * release;

    if(wipe->beat_env < 0.0001f)
        wipe->beat_env = 0.0f;
    else if(wipe->beat_env > 1.0f)
        wipe->beat_env = 1.0f;

    const int beat_q = wipe_clampi((int)(wipe->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int beat_speed_headroom = width / 7;
    const int speed_boost = (beat_q * (beat_speed_headroom > 1 ? beat_speed_headroom : 1) + 500) / 1000;

    int effective_speed = speed_arg + speed_boost;
    if(effective_speed < 0)
        effective_speed = 0;
    if(effective_speed > width)
        effective_speed = width;

    wipe->wipe_position += effective_speed;

    if(wipe->wipe_position > width)
        wipe->wipe_position = width;

    const int copy_w = wipe_clampi(wipe->wipe_position, 0, width);

    int effective_edge = edge_arg + ((beat_q * 48 + 500) / 1000);
    if(effective_edge > width)
        effective_edge = width;

    int effective_glow = glow_arg + ((beat_q * 96 + 500) / 1000);
    if(effective_glow > 255)
        effective_glow = 255;

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        if(copy_w > 0) {
            veejay_memcpy(frame->data[0] + row, frame2->data[0] + row, copy_w);
            veejay_memcpy(frame->data[1] + row, frame2->data[1] + row, copy_w);
            veejay_memcpy(frame->data[2] + row, frame2->data[2] + row, copy_w);
        }

        if(effective_edge > 0 && copy_w < width) {
            int edge_end = copy_w + effective_edge;
            if(edge_end > width)
                edge_end = width;

            for(int x = copy_w; x < edge_end; x++) {
                const int rel = x - copy_w;
                const int q8 = ((effective_edge - rel) * 256 + (effective_edge >> 1)) / effective_edge;
                const int idx = row + x;

                frame->data[0][idx] = wipe_blend_u8(frame->data[0][idx], frame2->data[0][idx], q8);
                frame->data[1][idx] = wipe_blend_u8(frame->data[1][idx], frame2->data[1][idx], q8);
                frame->data[2][idx] = wipe_blend_u8(frame->data[2][idx], frame2->data[2][idx], q8);

                if(effective_glow > 0) {
                    const int glow = (effective_glow * q8 + 128) >> 8;
                    frame->data[0][idx] = wipe_clamp_u8((int)frame->data[0][idx] + glow);
                }
            }
        }
    }
}
