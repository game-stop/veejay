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

	/* Color information is transferred from a reference image based on the similarity
	 * of their histograms. It works in any colorspace
	 */


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

	float *cdf1; // cummulative distribution
	float *cdf2;

	float *cdfu1;
	float *cdfu2;

	float *cdfv1;
	float *cdfv2;

} histomatch_t;

void *histomatch_malloc(int wid, int hei)
{
	histomatch_t *h = (histomatch_t*) vj_malloc(sizeof(histomatch_t));
	if(!h) return NULL;

	h->hist = (int*) vj_malloc(sizeof(int) * HIST_SIZE * 6 );
	if(!h->hist) {
		free(h);
		return NULL;
	}
	h->hist2 = h->hist + HIST_SIZE;
	h->histu1 = h->hist2 + HIST_SIZE;
	h->histu2 = h->histu1 + HIST_SIZE;
	h->histv1 = h->histu2 + HIST_SIZE;
	h->histv2 = h->histv1 + HIST_SIZE;

	h->cdf1 = (float*) vj_malloc(sizeof(float) * HIST_SIZE * 6 );
	if(!h->cdf1) {
		free(h->hist);
		free(h);
		return NULL;
	}

	h->cdf2 = h->cdf1 + HIST_SIZE;
	h->cdfu1 = h->cdf2 + HIST_SIZE;
	h->cdfu2 = h->cdfu1 + HIST_SIZE;
	h->cdfv1 = h->cdfu2 + HIST_SIZE;
	h->cdfv2 = h->cdfv1 + HIST_SIZE;

	return (void*) h;
}

void histomatch_free(void *ptr)
{
	histomatch_t *h = (histomatch_t*) ptr;
	free(h->hist);
	free(h->cdf1);
	free(h);
}

static void histomatch_reset( int *hist, float *cdf )
{
	veejay_memset( hist, 0, sizeof(int) * HIST_SIZE * 6 );
	veejay_memset( cdf,  0, sizeof(float) * HIST_SIZE * 6 );
}


// Calculating the histogram itself is the most costly part ... 
static void histomatch_calc_histogram(uint8_t *data, const int len, int *hist) {
	for( int i = 0; i < len; i ++ ) {
		hist[ data[i] ] ++;
	}
}

static void histomatch_calc_distribution(int *hist, const int len, float *cdf) {
    const float invLen = 1.0f / len;
    float sum = 0.0f;
    for (int i = 0; i < HIST_SIZE; i++) {
        sum += hist[i];
        cdf[i] = sum * invLen;
    }
}

static void histomatch_map_table(float *cdf1, float *cdf2, int *mapping_table)
{
    const float epsilon = 1e-12;

    for (int i = 0; i < HIST_SIZE; i++) {
        const float target = cdf1[i];
        int closest_match = 0;
		
		float minDiffSquared = (target - cdf2[0]) * (target - cdf2[0]);

        for (int j = 1; j < HIST_SIZE; j++) {
            float diff = target - cdf2[j];
            float diffSquared = diff * diff;

            if (diffSquared < minDiffSquared - epsilon) {
                minDiffSquared = diffSquared;
                closest_match = j;
            }
        }
        mapping_table[i] = closest_match;
    }
}

void histomatch_apply( void *ptr,  VJFrame *frame, VJFrame *frame2, int *args )
{
	histomatch_t *h = (histomatch_t*) ptr;
	int mapping_tableY[HIST_SIZE];
	int mapping_tableU[HIST_SIZE];
	int mapping_tableV[HIST_SIZE];
	
	const int len = frame->len;
	const int uv_len = frame->uv_len;
	const int opacity = args[0];
	const int op1 = 0xff - opacity;

	int i;

	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict U = frame->data[1];
	uint8_t *restrict V = frame->data[2];

	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict U2 = frame2->data[1];
	uint8_t *restrict V2 = frame2->data[2];

	float *restrict cdf1 = h->cdf1;
	float *restrict cdf2 = h->cdf2;

	float *restrict cdfu1 = h->cdfu1;
	float *restrict cdfv1 = h->cdfv1;

	float *restrict cdfu2 = h->cdfu2;
	float *restrict cdfv2 = h->cdfv2;

	histomatch_reset( h->hist, h->cdf1 );

	histomatch_calc_histogram( Y, len, h->hist );
	histomatch_calc_histogram( Y2, len, h->hist2 );

	histomatch_calc_histogram( U, uv_len, h->histu1 );
	histomatch_calc_histogram( U2, uv_len, h->histu2 );

	histomatch_calc_histogram( V, uv_len, h->histv1 );
	histomatch_calc_histogram( V2, uv_len, h->histv2 );

	histomatch_calc_distribution( h->hist,  len, cdf1 );
	histomatch_calc_distribution( h->hist2, len, cdf2 );

	histomatch_calc_distribution( h->histu1, uv_len, cdfu1 );
	histomatch_calc_distribution( h->histu2, uv_len, cdfu2 );

	histomatch_calc_distribution( h->histv1, uv_len, cdfv1 );
	histomatch_calc_distribution( h->histv2, uv_len, cdfv2 );

	histomatch_map_table( cdf1,  cdf2,  mapping_tableY );
	histomatch_map_table( cdfu1, cdfu2, mapping_tableU );
	histomatch_map_table( cdfv1, cdfv2, mapping_tableV );

	for( i = 0; i < len; i ++ ) {
		Y[i] = (Y[i] * op1 + mapping_tableY[ Y[i] ] * opacity) >> 8;
	}

	for( i = 0; i < uv_len; i ++ ) {
		U[i] = (U[i] * op1 + mapping_tableU[ U[i] ] * opacity) >> 8;
		V[i] = (V[i] * op1 + mapping_tableV[ V[i] ] * opacity) >> 8;
	}

}

