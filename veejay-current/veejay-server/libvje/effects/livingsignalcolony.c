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
#include "livingsignalcolony.h"

#define LSC_PARAMS 10

#define P_GROWTH      0
#define P_FEED        1
#define P_COUPLING    2
#define P_DIFFUSION   3
#define P_EXCITATION  4
#define P_SCALE       5
#define P_REGEN       6
#define P_MEMBRANE    7
#define P_PIGMENT     8
#define P_MIX         9

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int ping;
    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *life[2];
    uint8_t *nutrient[2];
    uint8_t *charge[2];
    uint8_t *pigment[2];
    int8_t hue_u[256];
    int8_t hue_v[256];
} livingsignalcolony_t;

static inline int lsc_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int lsc_abs(int v)
{
    return v < 0 ? -v : v;
}

static inline uint32_t lsc_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline void lsc_sample_yuv(
    const uint8_t *restrict Y,
    const uint8_t *restrict U,
    const uint8_t *restrict V,
    float fx,
    float fy,
    int w,
    int h,
    int *oy,
    int *ou,
    int *ov
) {
    int x0;
    int y0;
    int x1;
    int y1;
    int wx;
    int wy;
    int p00;
    int p10;
    int p01;
    int p11;
    int a;
    int b;
    int nx;
    int ny;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);

    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    x0 = (int) fx;
    y0 = (int) fy;
    x1 = x0 + 1;
    y1 = y0 + 1;

    if (x1 >= w)
        x1 = w - 1;
    if (y1 >= h)
        y1 = h - 1;

    wx = (int) ((fx - (float) x0) * 256.0f);
    wy = (int) ((fy - (float) y0) * 256.0f);

    p00 = y0 * w + x0;
    p10 = y0 * w + x1;
    p01 = y1 * w + x0;
    p11 = y1 * w + x1;

    a = (int) Y[p00] * (256 - wx) + (int) Y[p10] * wx;
    b = (int) Y[p01] * (256 - wx) + (int) Y[p11] * wx;
    *oy = (a * (256 - wy) + b * wy + 32768) >> 16;

    nx = (int) (fx + 0.5f);
    ny = (int) (fy + 0.5f);
    if (nx >= w)
        nx = w - 1;
    if (ny >= h)
        ny = h - 1;

    p00 = ny * w + nx;
    *ou = U[p00];
    *ov = V[p00];
}

static void lsc_seed(livingsignalcolony_t *t, VJFrame *frame, VJFrame *frame2)
{
    const uint8_t *restrict A = frame->data[0];
    const uint8_t *restrict B = frame2->data[0];
    const uint8_t *restrict BU = frame2->data[1];
    const uint8_t *restrict BV = frame2->data[2];
    uint8_t *restrict life = t->life[0];
    uint8_t *restrict nutrient = t->nutrient[0];
    uint8_t *restrict charge = t->charge[0];
    uint8_t *restrict pigment = t->pigment[0];
    const int w = t->w;
    const int h = t->h;

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        int row = y * w;
        int rowm = ym * w;
        int rowp = yp * w;

        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            int i = row + x;
            int gx = (int) B[row + xp] - (int) B[row + xm];
            int gy = (int) B[rowp + x] - (int) B[rowm + x];
            int edge = lsc_clampi(lsc_abs(gx) + lsc_abs(gy), 0, 255);
            int chroma = lsc_clampi(lsc_abs((int) BU[i] - 128) + lsc_abs((int) BV[i] - 128), 0, 255);
            int diff = lsc_abs((int) A[i] - (int) B[i]);
            int seed = lsc_clampi((edge * 3 + chroma + diff * 2 + ((int) B[i] >> 1)) >> 2, 0, 255);
            uint32_t h0 = lsc_hash32((uint32_t) ((x >> 2) + (y >> 2) * 4099));
            int colony_seed = ((h0 & 255U) < 74U) ? ((seed * (52 + (int) ((h0 >> 8) & 63U))) >> 7) : 0;

            life[i] = (uint8_t) lsc_clampi((seed >> 1) + colony_seed, 0, 255);
            nutrient[i] = (uint8_t) lsc_clampi(((int) B[i] * 3 + 255 - seed) >> 2, 0, 255);
            charge[i] = (uint8_t) lsc_clampi((edge + diff + chroma + (int) ((h0 >> 16) & 63U)) / 3, 0, 255);
            pigment[i] = (uint8_t) lsc_clampi(128 + (((int) BV[i] - (int) BU[i]) >> 1), 0, 255);
        }
    }

#pragma omp single
    {
        veejay_memcpy(t->life[1], t->life[0], t->len);
        veejay_memcpy(t->nutrient[1], t->nutrient[0], t->len);
        veejay_memcpy(t->charge[1], t->charge[0], t->len);
        veejay_memcpy(t->pigment[1], t->pigment[0], t->len);
        t->seeded = 1;
        t->ping = 0;
    }
}

vj_effect *livingsignalcolony_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = LSC_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Living Signal Colony";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->defaults[P_GROWTH]     = 62;
    ve->defaults[P_FEED]       = 66;
    ve->defaults[P_COUPLING]   = 72;
    ve->defaults[P_DIFFUSION]  = 46;
    ve->defaults[P_EXCITATION] = 64;
    ve->defaults[P_SCALE]      = 2;
    ve->defaults[P_REGEN]      = 58;
    ve->defaults[P_MEMBRANE]   = 76;
    ve->defaults[P_PIGMENT]    = 62;
    ve->defaults[P_MIX]        = 82;

    ve->limits[0][P_GROWTH]     = 0;   ve->limits[1][P_GROWTH]     = 100;
    ve->limits[0][P_FEED]       = 0;   ve->limits[1][P_FEED]       = 100;
    ve->limits[0][P_COUPLING]   = 0;   ve->limits[1][P_COUPLING]   = 100;
    ve->limits[0][P_DIFFUSION]  = 0;   ve->limits[1][P_DIFFUSION]  = 100;
    ve->limits[0][P_EXCITATION] = 0;   ve->limits[1][P_EXCITATION] = 100;
    ve->limits[0][P_SCALE]      = 1;   ve->limits[1][P_SCALE]      = 8;
    ve->limits[0][P_REGEN]      = 0;   ve->limits[1][P_REGEN]      = 100;
    ve->limits[0][P_MEMBRANE]   = 0;   ve->limits[1][P_MEMBRANE]   = 100;
    ve->limits[0][P_PIGMENT]    = 0;   ve->limits[1][P_PIGMENT]    = 100;
    ve->limits[0][P_MIX]        = 0;   ve->limits[1][P_MIX]        = 100;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Colony Growth",
        "Nutrient Feed",
        "Signal Coupling",
        "State Diffusion",
        "Excitation",
        "Colony Scale",
        "Regeneration",
        "Membrane",
        "Pigment",
        "Colony Mix"
    );

    return ve;
}

void *livingsignalcolony_malloc(int w, int h)
{
    livingsignalcolony_t *t = (livingsignalcolony_t *) vj_calloc(sizeof(livingsignalcolony_t));
    const size_t len = (size_t) w * (size_t) h;
    const size_t total = len * 11;
    uint8_t *base;
    size_t off = 0;

    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    base = (uint8_t *) t->region;
    t->src_y = base + off; off += len;
    t->src_u = base + off; off += len;
    t->src_v = base + off; off += len;

    for (int p = 0; p < 2; p++) {
        t->life[p] = base + off; off += len;
        t->nutrient[p] = base + off; off += len;
        t->charge[p] = base + off; off += len;
        t->pigment[p] = base + off; off += len;
    }

    for (int i = 0; i < 256; i++) {
        float a = 6.28318530718f * (float) i / 256.0f;
        t->hue_u[i] = (int8_t) (sinf(a) * 96.0f);
        t->hue_v[i] = (int8_t) (cosf(a) * 96.0f);
    }

    return t;
}

void livingsignalcolony_free(void *ptr)
{
    livingsignalcolony_t *t = (livingsignalcolony_t *) ptr;

    if (t) {
        free(t->region);
        free(t);
    }
}

void livingsignalcolony_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    livingsignalcolony_t *t = (livingsignalcolony_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    const int w = t->w;
    const int h = t->h;
    const int len = t->len;
    const int growth = args[P_GROWTH];
    const int feed = args[P_FEED];
    const int coupling = args[P_COUPLING];
    const int diffusion = args[P_DIFFUSION];
    const int excitation = args[P_EXCITATION];
    const int radius = args[P_SCALE];
    const int regen = args[P_REGEN];
    const int membrane_gain = args[P_MEMBRANE];
    const int pigment_gain = args[P_PIGMENT];
    const int mix = args[P_MIX];
    const int cur = t->ping;
    const int nxt = cur ^ 1;
    const uint8_t *restrict life = t->life[cur];
    const uint8_t *restrict nutrient = t->nutrient[cur];
    const uint8_t *restrict charge = t->charge[cur];
    const uint8_t *restrict pigment = t->pigment[cur];
    uint8_t *restrict life2 = t->life[nxt];
    uint8_t *restrict nutrient2 = t->nutrient[nxt];
    uint8_t *restrict charge2 = t->charge[nxt];
    uint8_t *restrict pigment2 = t->pigment[nxt];

#pragma omp single
    {
        veejay_memcpy(src_y, Y, len);
        veejay_memcpy(src_u, U, len);
        veejay_memcpy(src_v, V, len);
    }

    if (!t->seeded)
        lsc_seed(t, frame, frame2);

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int ym = y - radius;
        int yp = y + radius;
        int row = y * w;
        int rowm;
        int rowp;

        if (ym < 0)
            ym = 0;
        if (yp >= h)
            yp = h - 1;
        rowm = ym * w;
        rowp = yp * w;

        for (int x = 0; x < w; x++) {
            int xm = x - radius;
            int xp = x + radius;
            int i = row + x;
            int il;
            int ir;
            int iu;
            int id;
            int iul;
            int iur;
            int idl;
            int idr;
            int nl;
            int nn;
            int nc;
            int np;
            int gx;
            int gy;
            int edge;
            int chroma;
            int source_delta;
            int signal;
            int crowd;
            int life_v = life[i];
            int nutrient_v = nutrient[i];
            int charge_v = charge[i];
            int pigment_v = pigment[i];
            int feed_target;
            int dlife;
            int dnutrient;
            int dcharge;
            int pigment_target;
            int dpigment;

            if (xm < 0)
                xm = 0;
            if (xp >= w)
                xp = w - 1;

            il = row + xm;
            ir = row + xp;
            iu = rowm + x;
            id = rowp + x;
            iul = rowm + xm;
            iur = rowm + xp;
            idl = rowp + xm;
            idr = rowp + xp;

            nl = (life[il] + life[ir] + life[iu] + life[id] + life[iul] + life[iur] + life[idl] + life[idr] + 4) >> 3;
            nn = (nutrient[il] + nutrient[ir] + nutrient[iu] + nutrient[id] + nutrient[iul] + nutrient[iur] + nutrient[idl] + nutrient[idr] + 4) >> 3;
            nc = (charge[il] + charge[ir] + charge[iu] + charge[id] + charge[iul] + charge[iur] + charge[idl] + charge[idr] + 4) >> 3;
            np = (pigment[il] + pigment[ir] + pigment[iu] + pigment[id] + pigment[iul] + pigment[iur] + pigment[idl] + pigment[idr] + 4) >> 3;

            gx = (int) Y2[ir] - (int) Y2[il];
            gy = (int) Y2[id] - (int) Y2[iu];
            edge = lsc_clampi(lsc_abs(gx) + lsc_abs(gy), 0, 255);
            chroma = lsc_clampi(lsc_abs((int) U2[i] - 128) + lsc_abs((int) V2[i] - 128), 0, 255);
            source_delta = lsc_abs((int) src_y[i] - (int) Y2[i]);
            signal = lsc_clampi((edge * 3 + chroma + source_delta * 2 + ((int) Y2[i] >> 1)) >> 2, 0, 255);
            crowd = 255 - (lsc_abs(nl - 118) << 1);
            if (crowd < 0)
                crowd = 0;

            feed_target = lsc_clampi((((int) Y2[i] * 3 + signal * 2 + 255 - life_v) / 6), 0, 255);

            dlife = ((nl - life_v) * diffusion) / 190;
            dlife += (growth * crowd * nutrient_v) / 76000;
            dlife += (coupling * signal) / 720;
            dlife += (regen * nl * (255 - life_v)) / 250000;
            dlife -= (charge_v * (12 + excitation)) / 1800;
            dlife -= 1 + ((life_v * (12 + growth / 6)) >> 11);

            dnutrient = ((nn - nutrient_v) * diffusion) / 220;
            dnutrient += ((feed_target - nutrient_v) * feed) / 330;
            dnutrient -= (life_v * (18 + growth / 2)) >> 10;

            dcharge = ((nc - charge_v) * (diffusion + 28)) / 125;
            dcharge += (excitation * (life_v - charge_v)) / 190;
            dcharge += (coupling * signal) / 1100;
            dcharge -= 1;

            pigment_target = lsc_clampi(128 + (((int) V2[i] - (int) U2[i]) >> 1) + ((charge_v - 128) >> 2), 0, 255);
            dpigment = ((np - pigment_v) * diffusion) / 230;
            dpigment += ((pigment_target - pigment_v) * (12 + pigment_gain)) / 520;

            life2[i] = (uint8_t) lsc_clampi(life_v + dlife, 0, 255);
            nutrient2[i] = (uint8_t) lsc_clampi(nutrient_v + dnutrient, 0, 255);
            charge2[i] = (uint8_t) lsc_clampi(charge_v + dcharge, 0, 255);
            pigment2[i] = (uint8_t) lsc_clampi(pigment_v + dpigment, 0, 255);
        }
    }

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        int row = y * w;
        int rowm = ym * w;
        int rowp = yp * w;

        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            int i = row + x;
            int lv = life2[i];
            int nv = nutrient2[i];
            int cv = charge2[i];
            int pv = pigment2[i];
            int gx = (int) life2[row + xp] - (int) life2[row + xm];
            int gy = (int) life2[rowp + x] - (int) life2[rowm + x];
            int membrane = lsc_clampi(lsc_abs(gx) + lsc_abs(gy), 0, 255);
            int source_edge = lsc_clampi(lsc_abs((int) Y2[row + xp] - (int) Y2[row + xm]) + lsc_abs((int) Y2[rowp + x] - (int) Y2[rowm + x]), 0, 255);
            int reaction = lsc_abs(lv - cv);
            int colony = lsc_clampi((lv - 28) * 2 + reaction, 0, 255);
            int activity = lsc_clampi((colony * 3 + membrane * 4 + reaction * 2 + source_edge) / 11, 0, 255);
            float inv = 1.0f / (float) (lsc_abs(gx) + lsc_abs(gy) + 1);
            float tx = (float) (-gy) * inv;
            float ty = (float) gx * inv;
            float drift = ((float) (cv - 96) * (1.0f / 128.0f)) * (1.2f + 10.0f * ((float) mix * 0.01f)) * ((float) activity * (1.0f / 255.0f));
            float normal = ((float) (nv - 112) * (1.0f / 128.0f)) * 4.5f * ((float) colony * (1.0f / 255.0f));
            float sx = (float) x + tx * drift + (float) gx * inv * normal;
            float sy = (float) y + ty * drift + (float) gy * inv * normal;
            int syv;
            int suv;
            int svv;
            int colony_y;
            int colony_u;
            int colony_v;
            int emit;
            int hue_u;
            int hue_v;
            int mix_local;

            lsc_sample_yuv(src_y, src_u, src_v, sx, sy, w, h, &syv, &suv, &svv);

            emit = (membrane * membrane_gain) / 100;
            colony_y = syv + (((int) Y2[i] - syv) * colony * coupling) / 6502500;
            colony_y += ((255 - colony_y) * emit) >> 8;
            colony_y -= (colony * (100 - membrane_gain / 2)) >> 9;
            colony_y += ((cv - 96) * activity) >> 9;
            colony_y += ((lv - cv) * colony) >> 9;
            colony_y = lsc_clampi(colony_y, 0, 255);

            hue_u = t->hue_u[pv];
            hue_v = t->hue_v[pv];
            colony_u = suv + (((int) U2[i] - suv) * colony * coupling) / 6502500;
            colony_v = svv + (((int) V2[i] - svv) * colony * coupling) / 6502500;
            colony_u += (hue_u * activity * pigment_gain) / 25500;
            colony_v += (hue_v * activity * pigment_gain) / 25500;
            colony_u = lsc_clampi(colony_u, 0, 255);
            colony_v = lsc_clampi(colony_v, 0, 255);

            mix_local = (mix * (96 + activity * 159 / 255)) / 255;
            Y[i] = (uint8_t) (src_y[i] + ((mix_local * (colony_y - (int) src_y[i])) / 100));
            U[i] = (uint8_t) lsc_clampi((int) src_u[i] + ((mix_local * (colony_u - (int) src_u[i])) / 100), 0, 255);
            V[i] = (uint8_t) lsc_clampi((int) src_v[i] + ((mix_local * (colony_v - (int) src_v[i])) / 100), 0, 255);
        }
    }

#pragma omp single
    {
        t->ping = nxt;
    }
}
