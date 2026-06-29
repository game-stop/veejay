/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "morphologymixer.h"

#define MORPHOLOGYMIXER_PARAMS 5

#define P_PROGRESS  0
#define P_WARP      1
#define P_MODE      2
#define P_RESPONSE  3
#define P_STABILITY 4

#define MM_MODE_GRADIENT   0
#define MM_MODE_GRID       1
#define MM_MODE_PERSISTENT 2

#define FLOW_SHIFT 4
#define FLOW_SIZE  (1 << FLOW_SHIFT)
#define FLOW_MASK  (FLOW_SIZE - 1)

typedef struct {
    uint8_t *tmpY;
    uint8_t *tmpCb;
    uint8_t *tmpCr;

    int *pix_x;
    int *pix_y;

    int *grid_x1;
    int *grid_y1;
    int *grid_x2;
    int *grid_y2;

    int grid_w;
    int grid_h;
    int n_threads;
} morph_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t mm_blend255(uint8_t a, uint8_t b, int q)
{
    const int x = (int)a * (255 - q) + (int)b * q;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *morphologymixer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MORPHOLOGYMIXER_PARAMS;
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

    ve->limits[0][P_PROGRESS] = 0;  ve->limits[1][P_PROGRESS] = 255; ve->defaults[P_PROGRESS] = 128;
    ve->limits[0][P_WARP] = 0;      ve->limits[1][P_WARP] = 255;     ve->defaults[P_WARP] = 64;
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 2;       ve->defaults[P_MODE] = MM_MODE_GRID;
    ve->limits[0][P_RESPONSE] = 0;  ve->limits[1][P_RESPONSE] = 255; ve->defaults[P_RESPONSE] = 160;
    ve->limits[0][P_STABILITY] = 0; ve->limits[1][P_STABILITY] = 255; ve->defaults[P_STABILITY] = 180;

    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->description = "Displacement Morphology";

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mix Progress",
        "Warp Intensity",
        "Mode",
        "Response",
        "Stability"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Gradient Warp", "Grid Flow", "Persistent Flow");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    28,                 235,                16, 62,  700, 2800, 0,    84,
        VJ_BEAT_WARP,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    18,                 245,                18, 68,  650, 2600, 0,    92,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    72,                 245,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_INERTIA,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 54,                 230,                12, 46, 1000, 3600, 0,    58
    );

    return ve;
}

void morphologymixer_free(void *ptr)
{
    morph_t *m = (morph_t*) ptr;

    free(m->tmpY);
    free(m->pix_x);
    free(m);
}

void *morphologymixer_malloc(int w, int h)
{
    morph_t *m = (morph_t*) vj_calloc(sizeof(morph_t));

    if(!m)
        return NULL;

    const int size = w * h;

    m->grid_w = (w >> FLOW_SHIFT) + 2;
    m->grid_h = (h >> FLOW_SHIFT) + 2;

    const int grid_cells = m->grid_w * m->grid_h;
    const size_t ints = (size_t)size * 2u + (size_t)grid_cells * 4u;

    m->tmpY = (uint8_t*) vj_malloc((size_t)size * 3u);
    m->pix_x = (int*) vj_calloc(sizeof(int) * ints);

    if(!m->tmpY || !m->pix_x) {
        morphologymixer_free(m);
        return NULL;
    }

    m->tmpCb = m->tmpY + size;
    m->tmpCr = m->tmpCb + size;

    m->pix_y = m->pix_x + size;
    m->grid_x1 = m->pix_y + size;
    m->grid_y1 = m->grid_x1 + grid_cells;
    m->grid_x2 = m->grid_y1 + grid_cells;
    m->grid_y2 = m->grid_x2 + grid_cells;

    m->n_threads = vje_advise_num_threads(size);

    return (void*) m;
}

static void morphologymixer_blend_plain(morph_t *m,
                                        VJFrame *frame,
                                        VJFrame *frame2,
                                        const uint8_t *restrict sY,
                                        const uint8_t *restrict sCb,
                                        const uint8_t *restrict sCr,
                                        int progress)
{
    const int size = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp for schedule(static)
    for(int i = 0; i < size; i++) {
        Y[i] = mm_blend255(sY[i], Y2[i], progress);
        Cb[i] = mm_blend255(sCb[i], Cb2[i], progress);
        Cr[i] = mm_blend255(sCr[i], Cr2[i], progress);
    }
}

static void morphologymixer_mode_gradient(morph_t *m,
                                          VJFrame *frame,
                                          VJFrame *frame2,
                                          const uint8_t *restrict sY,
                                          const uint8_t *restrict sCb,
                                          const uint8_t *restrict sCr,
                                          int progress,
                                          int warp_amt,
                                          int response)
{
    const int w = frame->width;
    const int h = frame->height;
    const int inv_progress = 255 - progress;
    const int gain = response + 32;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const int up = y > 0 ? row - w : row;
        const int dn = y < h - 1 ? row + w : row;

        for(int x = 0; x < w; x++) {
            const int idx = row + x;
            const int lx = x > 0 ? x - 1 : x;
            const int rx = x < w - 1 ? x + 1 : x;

            const int dx1 = Y2[row + rx] - Y2[row + lx];
            const int dy1 = Y2[dn + x] - Y2[up + x];
            const int dx2 = sY[row + rx] - sY[row + lx];
            const int dy2 = sY[dn + x] - sY[up + x];

            const int sx1 = clampi(x + ((dx1 * warp_amt * gain * progress) >> 24), 0, w - 1);
            const int sy1 = clampi(y + ((dy1 * warp_amt * gain * progress) >> 24), 0, h - 1);
            const int sx2 = clampi(x - ((dx2 * warp_amt * gain * inv_progress) >> 24), 0, w - 1);
            const int sy2 = clampi(y - ((dy2 * warp_amt * gain * inv_progress) >> 24), 0, h - 1);

            const int a = sy1 * w + sx1;
            const int b = sy2 * w + sx2;

            Y[idx] = mm_blend255(sY[a], Y2[b], progress);
            Cb[idx] = mm_blend255(sCb[a], Cb2[b], progress);
            Cr[idx] = mm_blend255(sCr[a], Cr2[b], progress);
        }
    }
}

static void morphologymixer_mode_grid(morph_t *m,
                                      VJFrame *frame,
                                      VJFrame *frame2,
                                      const uint8_t *restrict sY,
                                      const uint8_t *restrict sCb,
                                      const uint8_t *restrict sCr,
                                      int progress,
                                      int warp_amt,
                                      int response,
                                      int stability)
{
    const int w = frame->width;
    const int h = frame->height;
    const int inv_progress = 255 - progress;
    const int envelope = (progress * inv_progress) >> 6;
    const int gain = warp_amt + ((warp_amt * response) >> 8);
    const int soften = 256 - (stability >> 1);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp for schedule(static)
    for(int gy = 0; gy < m->grid_h; gy++) {
        int y = gy << FLOW_SHIFT;

        if(y >= h)
            y = h - 1;

        const int up = y >= FLOW_SIZE ? y - FLOW_SIZE : 0;
        const int dn = y < h - FLOW_SIZE ? y + FLOW_SIZE : h - 1;

        for(int gx = 0; gx < m->grid_w; gx++) {
            int x = gx << FLOW_SHIFT;

            if(x >= w)
                x = w - 1;

            const int lx = x >= FLOW_SIZE ? x - FLOW_SIZE : 0;
            const int rx = x < w - FLOW_SIZE ? x + FLOW_SIZE : w - 1;
            const int gi = gy * m->grid_w + gx;

            const int dx1 = Y2[y * w + rx] - Y2[y * w + lx];
            const int dy1 = Y2[dn * w + x] - Y2[up * w + x];
            const int dx2 = sY[y * w + rx] - sY[y * w + lx];
            const int dy2 = sY[dn * w + x] - sY[up * w + x];

            m->grid_x1[gi] = (dx1 * gain * envelope) >> 16;
            m->grid_y1[gi] = (dy1 * gain * envelope) >> 16;
            m->grid_x2[gi] = (dx2 * gain * envelope) >> 16;
            m->grid_y2[gi] = (dy2 * gain * envelope) >> 16;
        }
    }

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int gy = y >> FLOW_SHIFT;
        const int yf = y & FLOW_MASK;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int gx = x >> FLOW_SHIFT;
            const int xf = x & FLOW_MASK;
            const int gi = gy * m->grid_w + gx;

            const int topx1 = m->grid_x1[gi] + (((m->grid_x1[gi + 1] - m->grid_x1[gi]) * xf) >> FLOW_SHIFT);
            const int botx1 = m->grid_x1[gi + m->grid_w] + (((m->grid_x1[gi + m->grid_w + 1] - m->grid_x1[gi + m->grid_w]) * xf) >> FLOW_SHIFT);
            const int topy1 = m->grid_y1[gi] + (((m->grid_y1[gi + 1] - m->grid_y1[gi]) * xf) >> FLOW_SHIFT);
            const int boty1 = m->grid_y1[gi + m->grid_w] + (((m->grid_y1[gi + m->grid_w + 1] - m->grid_y1[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

            const int topx2 = m->grid_x2[gi] + (((m->grid_x2[gi + 1] - m->grid_x2[gi]) * xf) >> FLOW_SHIFT);
            const int botx2 = m->grid_x2[gi + m->grid_w] + (((m->grid_x2[gi + m->grid_w + 1] - m->grid_x2[gi + m->grid_w]) * xf) >> FLOW_SHIFT);
            const int topy2 = m->grid_y2[gi] + (((m->grid_y2[gi + 1] - m->grid_y2[gi]) * xf) >> FLOW_SHIFT);
            const int boty2 = m->grid_y2[gi + m->grid_w] + (((m->grid_y2[gi + m->grid_w + 1] - m->grid_y2[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

            int wx1 = topx1 + (((botx1 - topx1) * yf) >> FLOW_SHIFT);
            int wy1 = topy1 + (((boty1 - topy1) * yf) >> FLOW_SHIFT);
            int wx2 = topx2 + (((botx2 - topx2) * yf) >> FLOW_SHIFT);
            int wy2 = topy2 + (((boty2 - topy2) * yf) >> FLOW_SHIFT);

            wx1 = (wx1 * soften) >> 8;
            wy1 = (wy1 * soften) >> 8;
            wx2 = (wx2 * soften) >> 8;
            wy2 = (wy2 * soften) >> 8;

            const int sx1 = clampi(x + wx1, 0, w - 1);
            const int sy1 = clampi(y + wy1, 0, h - 1);
            const int sx2 = clampi(x - wx2, 0, w - 1);
            const int sy2 = clampi(y - wy2, 0, h - 1);

            const int idx = row + x;
            const int a = sy1 * w + sx1;
            const int b = sy2 * w + sx2;

            Y[idx] = mm_blend255(sY[a], Y2[b], progress);
            Cb[idx] = mm_blend255(sCb[a], Cb2[b], progress);
            Cr[idx] = mm_blend255(sCr[a], Cr2[b], progress);
        }
    }
}

static void morphologymixer_mode_persistent(morph_t *m,
                                            VJFrame *frame,
                                            VJFrame *frame2,
                                            const uint8_t *restrict sY,
                                            const uint8_t *restrict sCb,
                                            const uint8_t *restrict sCr,
                                            int progress,
                                            int warp_amt,
                                            int response,
                                            int stability)
{
    const int w = frame->width;
    const int h = frame->height;
    const int inv_progress = 255 - progress;
    const int env = (progress * inv_progress) >> 7;
    const int impact = (warp_amt * (response + 32)) >> 8;
    const int persistence = 96 + ((stability * 156) >> 8);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp for schedule(static)
    for(int y = 1; y < h - 1; y++) {
        const int row = y * w;

        for(int x = 1; x < w - 1; x++) {
            const int idx = row + x;
            const int fx = ((sY[idx + 1] - sY[idx - 1]) + (Y2[idx + 1] - Y2[idx - 1])) << 4;
            const int fy = ((sY[idx + w] - sY[idx - w]) + (Y2[idx + w] - Y2[idx - w])) << 4;

            int vx = (m->pix_x[idx] * persistence + fx * impact) >> 8;
            int vy = (m->pix_y[idx] * persistence + fy * impact) >> 8;

            vx = clampi(vx, -32000, 32000);
            vy = clampi(vy, -32000, 32000);

            m->pix_x[idx] = vx;
            m->pix_y[idx] = vy;

            const int shift_x = (vx * env) >> 9;
            const int shift_y = (vy * env) >> 9;

            const int ax = clampi(x + shift_x, 0, w - 1);
            const int ay = clampi(y + shift_y, 0, h - 1);
            const int bx = clampi(x - shift_x, 0, w - 1);
            const int by = clampi(y - shift_y, 0, h - 1);

            const int a = ay * w + ax;
            const int b = by * w + bx;

            Y[idx] = mm_blend255(sY[a], Y2[b], progress);
            Cb[idx] = mm_blend255(sCb[a], Cb2[b], progress);
            Cr[idx] = mm_blend255(sCr[a], Cr2[b], progress);
        }
    }
}
void morphologymixer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    morph_t *m = (morph_t*) ptr;

    const int size = frame->len;
    const int progress = clampi(args[P_PROGRESS], 0, 255);
    const int warp_amt = clampi(args[P_WARP], 0, 255);
    const int mode = clampi(args[P_MODE], 0, 2);
    const int response = clampi(args[P_RESPONSE], 0, 255);
    const int stability = clampi(args[P_STABILITY], 0, 255);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    veejay_memcpy(m->tmpY, Y, (size_t)size);
    veejay_memcpy(m->tmpCb, Cb, (size_t)size);
    veejay_memcpy(m->tmpCr, Cr, (size_t)size);

#pragma omp parallel num_threads(m->n_threads)
    {
        if(warp_amt == 0) {
            morphologymixer_blend_plain(m, frame, frame2, m->tmpY, m->tmpCb, m->tmpCr, progress);
        }
        else if(mode == MM_MODE_GRADIENT) {
            morphologymixer_mode_gradient(m, frame, frame2, m->tmpY, m->tmpCb, m->tmpCr, progress, warp_amt, response);
        }
        else if(mode == MM_MODE_GRID) {
            morphologymixer_mode_grid(m, frame, frame2, m->tmpY, m->tmpCb, m->tmpCr, progress, warp_amt, response, stability);
        }
        else {
            morphologymixer_mode_persistent(m, frame, frame2, m->tmpY, m->tmpCb, m->tmpCr, progress, warp_amt, response, stability);
        }
    }
}
