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
#include <math.h>
#include <stdint.h>
#include "ripple.h"

#define RIPPLE_DEGREES 360
#define RIPPLE_PI 3.14159265358979323846

#define RIPPLE_PARAMS 10

#define P_WAVES             0
#define P_AMPLITUDE         1
#define P_ATTENUATION       2
#define P_MIX               3
#define P_CHROMA_AMOUNT     4
#define P_PHASE             5
#define P_WAVES_DRIVE       6
#define P_AMPLITUDE_DRIVE   7
#define P_ATTENUATION_DRIVE 8
#define P_PHASE_DRIVE       9

typedef struct {
    uint8_t *block;
    int *ripple_table;
    uint8_t *ripple_data[3];
    float *ripple_sin;
    float *ripple_cos;

    int ripple_waves;
    int ripple_ampli;
    int ripple_attn;
    int ripple_phase;

    float sm_waves;
    float sm_ampli;
    float sm_attn;
    float sm_mix;
    float sm_chroma;
    float sm_phase;
    float sm_waves_drive;
    float sm_ampli_drive;
    float sm_attn_drive;
    float sm_phase_drive;

    int have_smooth;
    int n_threads;
} ripple_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int ripple_wrapi(int v, int max)
{
    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline uint8_t ripple_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}



static inline float ripple_smooth_value(float old_v, float target, float amount)
{
    return old_v + (target - old_v) * amount;
}

static inline int ripple_roundi(float v)
{
    return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

vj_effect *ripple_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RIPPLE_PARAMS;
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
        return NULL;
    }

    ve->limits[0][P_WAVES] = 1;             ve->limits[1][P_WAVES] = 3600;              ve->defaults[P_WAVES] = 132;
    ve->limits[0][P_AMPLITUDE] = 1;         ve->limits[1][P_AMPLITUDE] = 80;            ve->defaults[P_AMPLITUDE] = 47;
    ve->limits[0][P_ATTENUATION] = 1;       ve->limits[1][P_ATTENUATION] = 360;         ve->defaults[P_ATTENUATION] = 7;
    ve->limits[0][P_MIX] = 0;               ve->limits[1][P_MIX] = 1000;                ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_CHROMA_AMOUNT] = 0;     ve->limits[1][P_CHROMA_AMOUNT] = 1000;      ve->defaults[P_CHROMA_AMOUNT] = 1000;
    ve->limits[0][P_PHASE] = 0;             ve->limits[1][P_PHASE] = 1000;              ve->defaults[P_PHASE] = 0;
    ve->limits[0][P_WAVES_DRIVE] = 0;       ve->limits[1][P_WAVES_DRIVE] = 1000;        ve->defaults[P_WAVES_DRIVE] = 0;
    ve->limits[0][P_AMPLITUDE_DRIVE] = 0;   ve->limits[1][P_AMPLITUDE_DRIVE] = 1000;    ve->defaults[P_AMPLITUDE_DRIVE] = 0;
    ve->limits[0][P_ATTENUATION_DRIVE] = 0; ve->limits[1][P_ATTENUATION_DRIVE] = 1000;  ve->defaults[P_ATTENUATION_DRIVE] = 0;
    ve->limits[0][P_PHASE_DRIVE] = 0;       ve->limits[1][P_PHASE_DRIVE] = 1000;        ve->defaults[P_PHASE_DRIVE] = 0;

    ve->description = "Ripple";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Waves",
        "Amplitude",
        "Attenuation",
        "Mix",
        "Chroma Amount",
        "Phase",
        "Waves Drive",
        "Amplitude Drive",
        "Attenuation Drive",
        "Phase Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WARP,             VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 72,  980,  4,  14, 3000, 8200, 2200, 26,
        VJ_BEAT_WINDOW_RADIUS,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,  7,   58,   12, 46, 1000, 3600, 0,    76,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,  2,   96,   12, 46, 1000, 3600, 0,    70,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                                       520, 1000, 12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                                       520, 1000, 12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_GEOMETRY_PHASE,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                                                      0,   1000, 12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_WARP,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS,                             120, 1000, 16, 62, 700,  2800, 0,    88,
        VJ_BEAT_INTENSITY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS,                             160, 1000, 16, 62, 700,  2800, 0,    92,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS,                             160, 1000, 14, 54, 800,  3200, 0,    78,
        VJ_BEAT_GEOMETRY_PHASE,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                                                      0,   1000, 16, 62, 700,  2800, 0,    90
    );
    return ve;
}

void *ripple_malloc(int width, int height)
{
    ripple_t *r = (ripple_t*)vj_calloc(sizeof(ripple_t));

    if(!r)
        return NULL;

    const int len = width * height;
    const size_t table_bytes = sizeof(int) * (size_t)len;
    const size_t data_bytes = (size_t)len * 3u;
    const size_t sin_bytes = sizeof(float) * RIPPLE_DEGREES;
    const size_t cos_bytes = sizeof(float) * RIPPLE_DEGREES;
    const size_t total = table_bytes + data_bytes + sin_bytes + cos_bytes + 64u;

    r->block = (uint8_t*)vj_malloc(total);

    if(!r->block) {
        free(r);
        return NULL;
    }

    uint8_t *p = r->block;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->ripple_table = (int*)p;
    p += table_bytes;

    r->ripple_data[0] = p;
    p += (size_t)len;
    r->ripple_data[1] = p;
    p += (size_t)len;
    r->ripple_data[2] = p;
    p += (size_t)len;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->ripple_sin = (float*)p;
    p += sin_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    r->ripple_cos = (float*)p;

    for(int i = 0; i < RIPPLE_DEGREES; i++) {
        const float rad = (float)((2.0 * RIPPLE_PI * (double)i) / (double)RIPPLE_DEGREES);

        r->ripple_sin[i] = sinf(rad);
        r->ripple_cos[i] = cosf(rad);
    }

    for(int i = 0; i < len; i++)
        r->ripple_table[i] = i;

    veejay_memset(r->ripple_data[0], pixel_Y_lo_, len);
    veejay_memset(r->ripple_data[1], 128, len);
    veejay_memset(r->ripple_data[2], 128, len);

    r->ripple_waves = -1;
    r->ripple_ampli = -1;
    r->ripple_attn = -1;
    r->ripple_phase = -1;

    r->sm_waves = 132.0f;
    r->sm_ampli = 47.0f;
    r->sm_attn = 7.0f;
    r->sm_mix = 1000.0f;
    r->sm_chroma = 1000.0f;
    r->sm_phase = 0.0f;
    r->sm_waves_drive = 0.0f;
    r->sm_ampli_drive = 0.0f;
    r->sm_attn_drive = 0.0f;
    r->sm_phase_drive = 0.0f;
    r->have_smooth = 0;
    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void ripple_free(void *ptr)
{
    ripple_t *r = (ripple_t*)ptr;

    free(r->block);
    free(r);
}

static void ripple_build_table(ripple_t *r,
                               int width,
                               int height,
                               int waves_arg,
                               int amplitude_arg,
                               int attenuation_arg,
                               int phase_arg)
{
    const float cx = ((float)width - 1.0f) * 0.5f;
    const float cy = ((float)height - 1.0f) * 0.5f;
    const float maxradius = sqrtf(cx * cx + cy * cy);
    const float waves = (float)waves_arg * 0.1f;
    const float ampli = (float)amplitude_arg * 0.1f;
    const float attenuation = (float)attenuation_arg * 0.1f;
    const float frequency = ((float)RIPPLE_DEGREES * waves) / maxradius;
    const float amplitude = maxradius / ampli;

    int *restrict table = r->ripple_table;
    float *restrict sin_lut = r->ripple_sin;
    float *restrict cos_lut = r->ripple_cos;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const float dy = (float)y - cy;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const float dx = (float)x - cx;
            const float dist2 = dx * dx + dy * dy;

            if(dist2 <= 0.000001f) {
                table[idx] = idx;
                continue;
            }

            const float radius = sqrtf(dist2);
            int angle = (int)((atan2f(dy, dx) * 180.0f) / (float)RIPPLE_PI);

            if(angle < 0)
                angle += RIPPLE_DEGREES;

            const int wave_index = ripple_wrapi((int)(frequency * radius) + phase_arg, RIPPLE_DEGREES);
            const float denom = powf(radius, attenuation);
            const float z = (amplitude / denom) * sin_lut[wave_index];

            int sx = (int)((float)x + z * cos_lut[angle]);
            int sy = (int)((float)y + z * sin_lut[angle]);

            sx = clampi(sx, 0, width - 1);
            sy = clampi(sy, 0, height - 1);

            table[idx] = sy * width + sx;
        }
    }

#pragma omp single
    {
        r->ripple_waves = waves_arg;
        r->ripple_ampli = amplitude_arg;
        r->ripple_attn = attenuation_arg;
        r->ripple_phase = phase_arg;
    }
}



void ripple_apply(void *ptr, VJFrame *frame, int *args)
{
    ripple_t *r = (ripple_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int waves_arg = args[P_WAVES];
    const int amplitude_arg = args[P_AMPLITUDE];
    const int atten_arg = args[P_ATTENUATION];
    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA_AMOUNT];
    const int phase_arg = args[P_PHASE];
    const int waves_drive = args[P_WAVES_DRIVE];
    const int ampli_drive = args[P_AMPLITUDE_DRIVE];
    const int attn_drive = args[P_ATTENUATION_DRIVE];
    const int phase_drive = args[P_PHASE_DRIVE];

    const float slow = 0.22f;
    const float fast = 0.34f;

    if(!r->have_smooth) {
        r->sm_waves = (float)waves_arg;
        r->sm_ampli = (float)amplitude_arg;
        r->sm_attn = (float)atten_arg;
        r->sm_mix = (float)mix_arg;
        r->sm_chroma = (float)chroma_arg;
        r->sm_phase = (float)phase_arg;
        r->sm_waves_drive = (float)waves_drive;
        r->sm_ampli_drive = (float)ampli_drive;
        r->sm_attn_drive = (float)attn_drive;
        r->sm_phase_drive = (float)phase_drive;
        r->have_smooth = 1;
    }
    else {
        r->sm_waves = ripple_smooth_value(r->sm_waves, (float)waves_arg, slow);
        r->sm_ampli = ripple_smooth_value(r->sm_ampli, (float)amplitude_arg, slow);
        r->sm_attn = ripple_smooth_value(r->sm_attn, (float)atten_arg, slow);
        r->sm_mix = ripple_smooth_value(r->sm_mix, (float)mix_arg, fast);
        r->sm_chroma = ripple_smooth_value(r->sm_chroma, (float)chroma_arg, fast);
        r->sm_phase = ripple_smooth_value(r->sm_phase, (float)phase_arg, fast);
        r->sm_waves_drive = ripple_smooth_value(r->sm_waves_drive, (float)waves_drive, fast);
        r->sm_ampli_drive = ripple_smooth_value(r->sm_ampli_drive, (float)ampli_drive, fast);
        r->sm_attn_drive = ripple_smooth_value(r->sm_attn_drive, (float)attn_drive, fast);
        r->sm_phase_drive = ripple_smooth_value(r->sm_phase_drive, (float)phase_drive, fast);
    }

    const int base_waves = clampi(ripple_roundi(r->sm_waves), 1, 3600);
    const int base_ampli = clampi(ripple_roundi(r->sm_ampli), 1, 80);
    const int base_attn = clampi(ripple_roundi(r->sm_attn), 1, 360);

    const int bw = clampi(ripple_roundi(r->sm_waves_drive), 0, 1000);
    const int ba = clampi(ripple_roundi(r->sm_ampli_drive), 0, 1000);
    const int bt = clampi(ripple_roundi(r->sm_attn_drive), 0, 1000);
    const int bp = clampi(ripple_roundi(r->sm_phase_drive), 0, 1000);

    int effective_waves = base_waves + (bw * 2600 + 500) / 1000;
    effective_waves = clampi(effective_waves, 1, 3600);

    int effective_ampli = base_ampli + (ba * 62 + 500) / 1000;
    effective_ampli = clampi(effective_ampli, 1, 80);

    int effective_attn = base_attn;
    effective_attn -= (bt * (base_attn - 1) + 500) / 1000;
    effective_attn = clampi(effective_attn, 1, 360);

    int effective_phase = ((clampi(ripple_roundi(r->sm_phase), 0, 1000) * RIPPLE_DEGREES + 500) / 1000);
    effective_phase += (bp * RIPPLE_DEGREES + 500) / 1000;
    effective_phase = ripple_wrapi(effective_phase, RIPPLE_DEGREES);

    int mix_q8 = (clampi(ripple_roundi(r->sm_mix), 0, 1000) * 256 + 500) / 1000;
    int chroma_q8 = (mix_q8 * clampi(ripple_roundi(r->sm_chroma), 0, 1000) + 500) / 1000;

    const int drive = clampi(((bw * 260) + (ba * 360) + (bt * 180) + (bp * 200) + 500) / 1000, 0, 1000);

    if(drive > 0) {
        mix_q8 += ((256 - mix_q8) * drive + 500) / 1000;
        chroma_q8 += ((256 - chroma_q8) * drive + 750) / 1500;
    }

    mix_q8 = clampi(mix_q8, 0, 256);
    chroma_q8 = clampi(chroma_q8, 0, 256);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    veejay_memcpy(r->ripple_data[0], Y, len);
    veejay_memcpy(r->ripple_data[1], Cb, len);
    veejay_memcpy(r->ripple_data[2], Cr, len);

    const int rebuild =
        r->ripple_waves != effective_waves ||
        r->ripple_ampli != effective_ampli ||
        r->ripple_attn != effective_attn ||
        r->ripple_phase != effective_phase;

    int *restrict table = r->ripple_table;
    uint8_t *restrict srcY = r->ripple_data[0];
    uint8_t *restrict srcCb = r->ripple_data[1];
    uint8_t *restrict srcCr = r->ripple_data[2];

#pragma omp parallel num_threads(r->n_threads)
    {
        if(rebuild)
            ripple_build_table(r, width, height, effective_waves, effective_ampli, effective_attn, effective_phase);

        if(mix_q8 >= 256 && chroma_q8 >= 256) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int src = table[i];

                Y[i] = srcY[src];
                Cb[i] = srcCb[src];
                Cr[i] = srcCr[src];
            }
        }
        else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int src = table[i];

                Y[i] = ripple_mix_u8(srcY[i], srcY[src], mix_q8);
                Cb[i] = ripple_mix_u8(srcCb[i], srcCb[src], chroma_q8);
                Cr[i] = ripple_mix_u8(srcCr[i], srcCr[src], chroma_q8);
            }
        }
    }
}

