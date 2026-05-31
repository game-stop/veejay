/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "morphologymixer.h"

#define FLOW_SHIFT 4
#define FLOW_SIZE  (1 << FLOW_SHIFT)
#define FLOW_MASK  (FLOW_SIZE - 1)

static inline int mm_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

typedef struct {
    uint8_t *tmpY;
    uint8_t *tmpCb;
    uint8_t *tmpCr;

    int grid_w;
    int grid_h;

    /*
     * flow_x1/flow_y1 are full-frame sized because mode 2 uses them as
     * persistent per-pixel velocity fields.
     *
     * flow_x2/flow_y2 are grid-sized because mode 1 uses them for coarse
     * bilinear flow.
     */
    int *flow_x1;
    int *flow_y1;
    int *flow_x2;
    int *flow_y2;

    int n_threads;
} morph_t;

vj_effect *morphologymixer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 5;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0]  = 128;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1]  = 64;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 2;
    ve->defaults[2]  = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3]  = 160;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->defaults[4]  = 180;

    ve->sub_format  = 1;
    ve->extra_frame = 1;
    ve->has_user    = 0;

    ve->description = "Displacement Morphology";

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mix Progress",
        "Warp Intensity",
        "Mode",
        "Response",
        "Stability"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SOURCE_MIX,    VJ_BEAT_F_CONTINUOUS,                    12,                 235,                10, 38, 1000, 2600, 0,   60,    /* Mix Progress */
        VJ_BEAT_KICK,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE, 8,                  220,                22, 88, 60,   360,  0,   94,    /* Warp Intensity */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_MOTION_REACT,  VJ_BEAT_F_CONTINUOUS,                     48,                 230,                10, 38, 1000, 2600, 0,   60,    /* Response */
        VJ_BEAT_INERTIA,       VJ_BEAT_F_CONTINUOUS,                     40,                 245,                8,  32, 1200, 3200, 0,   50     /* Stability */
    );

    (void) w;
    (void) h;

    return ve;
}

void morphologymixer_free(void *ptr)
{
    morph_t *m = (morph_t*) ptr;

    if(!m)
        return;

    if(m->tmpY)
        free(m->tmpY);

    if(m->flow_x1)
        free(m->flow_x1);

    if(m->flow_x2)
        free(m->flow_x2);

    free(m);
}

void *morphologymixer_malloc(int w, int h)
{
    morph_t *m;
    int size;
    int grid_cells;

    if(w <= 0 || h <= 0)
        return NULL;

    m = (morph_t*) vj_calloc(sizeof(morph_t));
    if(!m)
        return NULL;

    size = w * h;

    m->tmpY = (uint8_t*) vj_calloc((size_t)size * 3u);
    if(!m->tmpY) {
        free(m);
        return NULL;
    }

    m->tmpCb = m->tmpY + size;
    m->tmpCr = m->tmpCb + size;

    m->grid_w = (w >> FLOW_SHIFT) + 2;
    m->grid_h = (h >> FLOW_SHIFT) + 2;

    grid_cells = m->grid_w * m->grid_h;

    m->flow_x2 = (int*) vj_calloc(sizeof(int) * (size_t)grid_cells * 2u);
    if(!m->flow_x2) {
        morphologymixer_free(m);
        return NULL;
    }

    m->flow_y2 = m->flow_x2 + grid_cells;

    m->flow_x1 = (int*) vj_calloc(sizeof(int) * (size_t)size * 2u);
    if(!m->flow_x1) {
        morphologymixer_free(m);
        return NULL;
    }

    m->flow_y1 = m->flow_x1 + size;

    m->n_threads = vje_advise_num_threads(size);
    if(m->n_threads <= 0)
        m->n_threads = 1;

    return (void*) m;
}

void morphologymixer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    morph_t *m = (morph_t*) ptr;

    if(!m || !frame || !frame2 || !args)
        return;

    const int progress  = mm_clampi(args[0], 0, 255);
    const int warp_amt  = mm_clampi(args[1], 0, 255);
    const int mode      = mm_clampi(args[2], 0, 2);
    const int response  = mm_clampi(args[3], 0, 255);
    const int stability = mm_clampi(args[4], 0, 255);

    const int w = frame->width;
    const int h = frame->height;
    const int size = w * h;

    uint8_t *restrict Y   = frame->data[0];
    uint8_t *restrict Cb  = frame->data[1];
    uint8_t *restrict Cr  = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    uint8_t *restrict sY;
    uint8_t *restrict sCb;
    uint8_t *restrict sCr;

    const int inv_progress = 255 - progress;

    if(!Y || !Cb || !Cr || !Y2 || !Cb2 || !Cr2)
        return;

    if(w <= 1 || h <= 1 || size <= 0)
        return;

    if(progress <= 0)
        return;

    if(progress >= 255) {
        veejay_memcpy(Y,  Y2,  (size_t)size);
        veejay_memcpy(Cb, Cb2, (size_t)size);
        veejay_memcpy(Cr, Cr2, (size_t)size);
        return;
    }

    veejay_memcpy(m->tmpY,  Y,  (size_t)size);
    veejay_memcpy(m->tmpCb, Cb, (size_t)size);
    veejay_memcpy(m->tmpCr, Cr, (size_t)size);

    sY  = m->tmpY;
    sCb = m->tmpCb;
    sCr = m->tmpCr;

    if(warp_amt <= 0) {
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int i = 0; i < size; i++) {
            Y[i]  = (uint8_t)((sY[i]  * inv_progress + Y2[i]  * progress) >> 8);
            Cb[i] = (uint8_t)((sCb[i] * inv_progress + Cb2[i] * progress) >> 8);
            Cr[i] = (uint8_t)((sCr[i] * inv_progress + Cr2[i] * progress) >> 8);
        }
        return;
    }

    /*
     * Mode 0: direct per-pixel luma-gradient warp.
     */
    if(mode == 0) {
        const int gain = response + 32;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 0; y < h; y++) {
            const int row = y * w;
            const int up  = (y > 0)     ? row - w : row;
            const int dn  = (y < h - 1) ? row + w : row;

            for(int x = 0; x < w; x++) {
                const int idx = row + x;
                const int lx  = (x > 0)     ? x - 1 : x;
                const int rx  = (x < w - 1) ? x + 1 : x;

                const int dx1 = Y2[row + rx] - Y2[row + lx];
                const int dy1 = Y2[dn + x]   - Y2[up + x];

                const int dx2 = sY[row + rx] - sY[row + lx];
                const int dy2 = sY[dn + x]   - sY[up + x];

                const int sx1 = mm_clampi(x + ((dx1 * warp_amt * gain * progress) >> 24), 0, w - 1);
                const int sy1 = mm_clampi(y + ((dy1 * warp_amt * gain * progress) >> 24), 0, h - 1);
                const int sx2 = mm_clampi(x - ((dx2 * warp_amt * gain * inv_progress) >> 24), 0, w - 1);
                const int sy2 = mm_clampi(y - ((dy2 * warp_amt * gain * inv_progress) >> 24), 0, h - 1);

                const int a = sy1 * w + sx1;
                const int b = sy2 * w + sx2;

                Y[idx]  = (uint8_t)((sY[a]  * inv_progress + Y2[b]  * progress) >> 8);
                Cb[idx] = (uint8_t)((sCb[a] * inv_progress + Cb2[b] * progress) >> 8);
                Cr[idx] = (uint8_t)((sCr[a] * inv_progress + Cr2[b] * progress) >> 8);
            }
        }
        return;
    }

    /*
     * Mode 1: coarse grid flow, bilinear interpolation.
     */
    if(mode == 1) {
        const int envelope = (progress * inv_progress) >> 6;
        const int gain = warp_amt + ((warp_amt * response) >> 8);
        const int soften = 256 - (stability >> 1);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int gy = 0; gy < m->grid_h; gy++) {
            int y = gy << FLOW_SHIFT;
            if(y >= h) y = h - 1;

            const int up = (y >= FLOW_SIZE) ? y - FLOW_SIZE : 0;
            const int dn = (y < h - FLOW_SIZE) ? y + FLOW_SIZE : h - 1;

            for(int gx = 0; gx < m->grid_w; gx++) {
                int x = gx << FLOW_SHIFT;
                if(x >= w) x = w - 1;

                const int lx = (x >= FLOW_SIZE) ? x - FLOW_SIZE : 0;
                const int rx = (x < w - FLOW_SIZE) ? x + FLOW_SIZE : w - 1;
                const int gi = gy * m->grid_w + gx;

                const int dx1 = Y2[y * w + rx] - Y2[y * w + lx];
                const int dy1 = Y2[dn * w + x] - Y2[up * w + x];

                const int dx2 = sY[y * w + rx] - sY[y * w + lx];
                const int dy2 = sY[dn * w + x] - sY[up * w + x];

                m->flow_x1[gi] = (dx1 * gain * envelope) >> 16;
                m->flow_y1[gi] = (dy1 * gain * envelope) >> 16;
                m->flow_x2[gi] = (dx2 * gain * envelope) >> 16;
                m->flow_y2[gi] = (dy2 * gain * envelope) >> 16;
            }
        }

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 0; y < h; y++) {
            const int gy  = y >> FLOW_SHIFT;
            const int yf  = y & FLOW_MASK;
            const int row = y * w;

            for(int x = 0; x < w; x++) {
                const int gx = x >> FLOW_SHIFT;
                const int xf = x & FLOW_MASK;
                const int gi = gy * m->grid_w + gx;

                const int topx1 = m->flow_x1[gi] +
                    (((m->flow_x1[gi + 1] - m->flow_x1[gi]) * xf) >> FLOW_SHIFT);
                const int botx1 = m->flow_x1[gi + m->grid_w] +
                    (((m->flow_x1[gi + m->grid_w + 1] - m->flow_x1[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

                const int topy1 = m->flow_y1[gi] +
                    (((m->flow_y1[gi + 1] - m->flow_y1[gi]) * xf) >> FLOW_SHIFT);
                const int boty1 = m->flow_y1[gi + m->grid_w] +
                    (((m->flow_y1[gi + m->grid_w + 1] - m->flow_y1[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

                const int topx2 = m->flow_x2[gi] +
                    (((m->flow_x2[gi + 1] - m->flow_x2[gi]) * xf) >> FLOW_SHIFT);
                const int botx2 = m->flow_x2[gi + m->grid_w] +
                    (((m->flow_x2[gi + m->grid_w + 1] - m->flow_x2[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

                const int topy2 = m->flow_y2[gi] +
                    (((m->flow_y2[gi + 1] - m->flow_y2[gi]) * xf) >> FLOW_SHIFT);
                const int boty2 = m->flow_y2[gi + m->grid_w] +
                    (((m->flow_y2[gi + m->grid_w + 1] - m->flow_y2[gi + m->grid_w]) * xf) >> FLOW_SHIFT);

                int wx1 = topx1 + (((botx1 - topx1) * yf) >> FLOW_SHIFT);
                int wy1 = topy1 + (((boty1 - topy1) * yf) >> FLOW_SHIFT);
                int wx2 = topx2 + (((botx2 - topx2) * yf) >> FLOW_SHIFT);
                int wy2 = topy2 + (((boty2 - topy2) * yf) >> FLOW_SHIFT);

                wx1 = (wx1 * soften) >> 8;
                wy1 = (wy1 * soften) >> 8;
                wx2 = (wx2 * soften) >> 8;
                wy2 = (wy2 * soften) >> 8;

                const int sx1 = mm_clampi(x + wx1, 0, w - 1);
                const int sy1 = mm_clampi(y + wy1, 0, h - 1);
                const int sx2 = mm_clampi(x - wx2, 0, w - 1);
                const int sy2 = mm_clampi(y - wy2, 0, h - 1);

                const int idx = row + x;
                const int a = sy1 * w + sx1;
                const int b = sy2 * w + sx2;

                Y[idx]  = (uint8_t)((sY[a]  * inv_progress + Y2[b]  * progress) >> 8);
                Cb[idx] = (uint8_t)((sCb[a] * inv_progress + Cb2[b] * progress) >> 8);
                Cr[idx] = (uint8_t)((sCr[a] * inv_progress + Cr2[b] * progress) >> 8);
            }
        }
        return;
    }

    /*
     * Mode 2: persistent flow field.
     * Border pixels remain from the original frame copy.
     */
    {
        const int env = (progress * inv_progress) >> 7;
        const int impact = ((warp_amt * (response + 32)) >> 8);
        const int persistence = 96 + ((stability * 156) >> 8);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 1; y < h - 1; y++) {
            const int row = y * w;

            for(int x = 1; x < w - 1; x++) {
                const int idx = row + x;

                const int fx =
                    ((sY[idx + 1] - sY[idx - 1]) +
                     (Y2[idx + 1] - Y2[idx - 1])) << 4;

                const int fy =
                    ((sY[idx + w] - sY[idx - w]) +
                     (Y2[idx + w] - Y2[idx - w])) << 4;

                int vx = (m->flow_x1[idx] * persistence + fx * impact) >> 8;
                int vy = (m->flow_y1[idx] * persistence + fy * impact) >> 8;

                vx = mm_clampi(vx, -32000, 32000);
                vy = mm_clampi(vy, -32000, 32000);

                m->flow_x1[idx] = vx;
                m->flow_y1[idx] = vy;

                const int shift_x = (vx * env) >> 9;
                const int shift_y = (vy * env) >> 9;

                const int ax = mm_clampi(x + shift_x, 0, w - 1);
                const int ay = mm_clampi(y + shift_y, 0, h - 1);
                const int bx = mm_clampi(x - shift_x, 0, w - 1);
                const int by = mm_clampi(y - shift_y, 0, h - 1);

                const int a = ay * w + ax;
                const int b = by * w + bx;

                Y[idx]  = (uint8_t)((sY[a]    * inv_progress + Y2[b]  * progress) >> 8);
                Cb[idx] = (uint8_t)((sCb[idx] * inv_progress + Cb2[b] * progress) >> 8);
                Cr[idx] = (uint8_t)((sCr[idx] * inv_progress + Cr2[b] * progress) >> 8);
            }
        }
    }
}