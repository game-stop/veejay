/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "histomatch.h"

vj_effect *histomatch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;

    ve->description = "Histogram Matching";
    ve->sub_format = -1;
    ve->extra_frame = 1;
	ve->parallel = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity"); 

    return ve;
}

#define HIST_SIZE 256

typedef struct {
	int *hist;   //Y histogram
	int *hist2;  //Y2 histogram

	int *histu1; //U
	int *histu2; //U2

	int *histv1; //V
	int *histv2; //V2

	float *cdf;
	float *cdf1; // cummulative distribution
	float *cdf2;

	float *cdfu1;
	float *cdfu2;

	float *cdfv1;
	float *cdfv2;

	uint8_t *lutY;
    uint8_t *lutU;
    uint8_t *lutV;

} histomatch_t;

void *histomatch_malloc(int wid, int hei)
{
    histomatch_t *h = (histomatch_t*) vj_calloc(sizeof(histomatch_t));
    if(!h) return NULL;

    h->hist = (int*) vj_malloc(sizeof(int) * HIST_SIZE * 6);
    h->hist2 = h->hist + HIST_SIZE;
    h->histu1 = h->hist2 + HIST_SIZE;
    h->histu2 = h->histu1 + HIST_SIZE;
    h->histv1 = h->histu2 + HIST_SIZE;
    h->histv2 = h->histv1 + HIST_SIZE;

    h->cdf = (float*) vj_malloc(sizeof(float) * HIST_SIZE * 6);
    h->cdf1 = h->cdf;
    h->cdf2 = h->cdf1 + HIST_SIZE;
    h->cdfu1 = h->cdf2 + HIST_SIZE;
    h->cdfu2 = h->cdfu1 + HIST_SIZE;
    h->cdfv1 = h->cdfu2 + HIST_SIZE;
    h->cdfv2 = h->cdfv1 + HIST_SIZE;

    h->lutY = (uint8_t*) vj_malloc(HIST_SIZE * 3);
    h->lutU = h->lutY + HIST_SIZE;
    h->lutV = h->lutU + HIST_SIZE;

    if(!h->hist || !h->cdf || !h->lutY) {
        histomatch_free(h);
        return NULL;
    }

    return (void*) h;
}

void histomatch_free(void *ptr)
{
    histomatch_t *h = (histomatch_t*) ptr;
    if (h) {
        if (h->hist) free(h->hist);
        if (h->cdf)  free(h->cdf);
        if (h->lutY) free(h->lutY);
        free(h);
    }
}

static void histomatch_reset(histomatch_t *h)
{
    veejay_memset(h->hist, 0, sizeof(int) * HIST_SIZE * 6);
    veejay_memset(h->cdf,  0, sizeof(float) * HIST_SIZE * 6);
}

static void histomatch_calc_histogram(uint8_t *restrict data, const int len, int *restrict hist) 
{
    int bank0[HIST_SIZE] = {0};
    int bank1[HIST_SIZE] = {0};
    int bank2[HIST_SIZE] = {0};
    int bank3[HIST_SIZE] = {0};

    int i = 0;
    
    for(; i <= len - 4; i += 4) {
        bank0[data[i + 0]]++;
        bank1[data[i + 1]]++;
        bank2[data[i + 2]]++;
        bank3[data[i + 3]]++;
    }

    for(; i < len; i++) {
        bank0[data[i]]++;
    }

#pragma GCC ivdep
    for(int j = 0; j < HIST_SIZE; j++) {
        hist[j] = bank0[j] + bank1[j] + bank2[j] + bank3[j];
    }
}

static void histomatch_calc_distribution(int *restrict hist, float *restrict cdf) {
    int total = 0;
    
#pragma GCC ivdep
	for (int i = 0; i < HIST_SIZE; i++) {
        total += hist[i];
    }

    float invTotal = (total > 0) ? 1.0f / (float)total : 0.0f;
    
    double acc = 0.0;
    for (int i = 0; i < HIST_SIZE; i++) {
        acc += (double)hist[i];
        cdf[i] = (float)(acc * (double)invTotal);
    }
    
    if (total > 0) cdf[255] = 1.0f;
}

static void histomatch_map_and_blend(float *restrict c1, float *restrict c2, uint8_t *restrict lut, int opacity) {
    int j = 0;
    uint8_t map[HIST_SIZE];
    const uint32_t inv_op = (uint32_t)(255 - opacity);
    const uint32_t op = (uint32_t)opacity;

    for (int i = 0; i < HIST_SIZE; i++) {
        float target = c1[i];
        while (j < 255 && c2[j+1] < target) {
            j++;
        }
        
        int next_j = (j < 255) ? j + 1 : j;
        float dist_curr = target - c2[j];
        float dist_next = c2[next_j] - target;
        
        map[i] = (dist_next < dist_curr) ? next_j : j;
    }

    #pragma GCC ivdep
    for (int i = 0; i < HIST_SIZE; i++) {
        uint32_t val = (uint32_t)i * inv_op + (uint32_t)map[i] * op;
        lut[i] = (uint8_t)((val * 32897) >> 23);
    }
}

void histomatch_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    histomatch_t *h = (histomatch_t*) ptr;
    const int opacity = args[0];

    histomatch_reset(h);

    histomatch_calc_histogram(frame->data[0], frame->len, h->hist);
    histomatch_calc_histogram(frame2->data[0], frame->len, h->hist2);
    histomatch_calc_histogram(frame->data[1], frame->uv_len, h->histu1);
    histomatch_calc_histogram(frame2->data[1], frame->uv_len, h->histu2);
    histomatch_calc_histogram(frame->data[2], frame->uv_len, h->histv1);
    histomatch_calc_histogram(frame2->data[2], frame->uv_len, h->histv2);


    histomatch_calc_distribution(h->hist,   h->cdf1);
    histomatch_calc_distribution(h->hist2,  h->cdf2);
    histomatch_calc_distribution(h->histu1, h->cdfu1);
    histomatch_calc_distribution(h->histu2, h->cdfu2);
    histomatch_calc_distribution(h->histv1, h->cdfv1);
    histomatch_calc_distribution(h->histv2, h->cdfv2);

    histomatch_map_and_blend(h->cdf1,  h->cdf2,  h->lutY, opacity);
    histomatch_map_and_blend(h->cdfu1, h->cdfu2, h->lutU, opacity);
    histomatch_map_and_blend(h->cdfv1, h->cdfv2, h->lutV, opacity);

    uint8_t *restrict Y = (uint8_t*) frame->data[0];
    uint8_t *restrict U = (uint8_t*) frame->data[1];
    uint8_t *restrict V = (uint8_t*) frame->data[2];
    uint8_t *restrict lY = (uint8_t*) h->lutY;
    uint8_t *restrict lU = (uint8_t*) h->lutU;
    uint8_t *restrict lV = (uint8_t*) h->lutV;

    #pragma GCC ivdep
    for(int i = 0; i < frame->len; i++) {
        Y[i] = lY[Y[i]];
    }

    #pragma GCC ivdep
    for(int i = 0; i < frame->uv_len; i++) {
        U[i] = lU[U[i]];
        V[i] = lV[V[i]];
    }
}
