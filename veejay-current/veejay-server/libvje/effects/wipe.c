/* veejay - Linux VeeJay
 *       (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
 *
 * Beat-ready transition wipe variant.
 */

#include "common.h"
#include <stdint.h>
#include <stdlib.h>
#include <veejaycore/vjmem.h>
#include "wipe.h"

#define WIPE_PARAMS 4

#define P_SPEED       0
#define P_RESTART     1
#define P_EDGE_WIDTH  2
#define P_EDGE_GLOW   3

typedef struct {
    int wipe_position;
    int last_restart;
    int n_threads;
    int initialized;
    float speed_env;
    float edge_env;
    float glow_env;
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
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline float wipe_follow(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

int wipe_ready(void *ptr, int width, int height);

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
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int max_speed = w;
    int max_edge = w / 3;
    if(max_edge > 512)
        max_edge = 512;

    ve->defaults[P_SPEED]      = 1;
    ve->defaults[P_RESTART]    = 1;
    ve->defaults[P_EDGE_WIDTH] = 16;
    ve->defaults[P_EDGE_GLOW]  = 24;

    ve->limits[0][P_SPEED]      = 0; ve->limits[1][P_SPEED]      = max_speed;
    ve->limits[0][P_RESTART]    = 0; ve->limits[1][P_RESTART]    = 1;
    ve->limits[0][P_EDGE_WIDTH] = 0; ve->limits[1][P_EDGE_WIDTH] = max_edge;
    ve->limits[0][P_EDGE_GLOW]  = 0; ve->limits[1][P_EDGE_GLOW]  = 255;

    ve->description = "Transition Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Restart",
        "Edge Width",
        "Edge Glow"
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
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,      1,                  max_speed,          26, 88, 18,  520, 0,  110,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,   0,   0,  -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE,           0,                  max_edge,           24, 82, 45,  760, 0,  86,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,      24,                 255,                36, 96, 6,   340, 20, 112
    );

    ve->is_transition_ready_func = wipe_ready;

    return ve;
}

int wipe_ready(void *ptr, int width, int height)
{
    wipe_t *w = (wipe_t*) ptr;

    (void) height;

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
    prv->initialized = 0;
    prv->speed_env = 0.0f;
    prv->edge_env = 0.0f;
    prv->glow_env = 0.0f;

    prv->n_threads = vje_advise_num_threads(w * h);

    return prv;
}

void wipe_free(void *ptr)
{
    free(ptr);
}

void wipe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int speed_arg = args[P_SPEED];
    const int restart = args[P_RESTART] ? 1 : 0;
    const int edge_arg = args[P_EDGE_WIDTH];
    const int glow_arg = args[P_EDGE_GLOW];

    if(!wipe->initialized) {
        wipe->speed_env = (float)speed_arg;
        wipe->edge_env = (float)edge_arg;
        wipe->glow_env = (float)glow_arg;
        wipe->initialized = 1;
    }

    if(restart && !wipe->last_restart)
        wipe->wipe_position = 0;

    wipe->last_restart = restart;

    wipe->speed_env = wipe_follow(wipe->speed_env, (float)speed_arg, 0.34f, 0.115f);
    wipe->edge_env = wipe_follow(wipe->edge_env, (float)edge_arg, 0.30f, 0.105f);
    wipe->glow_env = wipe_follow(wipe->glow_env, (float)glow_arg, 0.38f, 0.130f);

    int effective_speed = (int)(wipe->speed_env + 0.5f);
    if(effective_speed < 0)
        effective_speed = 0;
    if(effective_speed > width)
        effective_speed = width;

    wipe->wipe_position += effective_speed;

    if(wipe->wipe_position > width)
        wipe->wipe_position = width;

    const int copy_w = wipe_clampi(wipe->wipe_position, 0, width);

    int effective_edge = (int)(wipe->edge_env + 0.5f);
    if(effective_edge < 0)
        effective_edge = 0;
    if(effective_edge > width)
        effective_edge = width;

    int effective_glow = (int)(wipe->glow_env + 0.5f);
    if(effective_glow < 0)
        effective_glow = 0;
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
