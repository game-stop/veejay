
/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "bwselect.h"
#include "common.h"
vj_effect *bwselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 16;
    ve->defaults[1] = 235;
	ve->defaults[2] = 400;
	ve->defaults[3] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    
	ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1000;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->description = "Black and White Mask by Threshold";
    
	ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user =0;
	ve->parallel = 1;

	ve->param_description = vje_build_param_list( ve->num_params, "Min Threshold", "Max Threshold", "Gamma", "Alpha" );
    return ve;
}

static int last_gamma = 0;
static uint8_t table[256];

static void gamma_setup(int width, int height, double gamma_value)
{
    int i;
    double val;

    for (i = 0; i < 256; i++) {
		val = i / 256.0;
		val = pow(val, gamma_value);
		val = 256.0 * val;
		table[i] = val;
    }
}

void bwselect_apply(VJFrame *frame, int width, int height, int min_threshold, int max_threshold, int gamma, int mode) {
	int r,c;
    const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	if( gamma == 0 ) 
	{
		if( mode == 0 ) {
			for(r=0; r < len; r+=width) {
				for(c=0; c < width; c++) {
					uint8_t p = Y[r+c];
					if( p > min_threshold && p < max_threshold) {
						Y[r+c] = pixel_Y_hi_;
					}
					else {
						Y[r+c] = pixel_Y_lo_;
					}
				}
			}
			veejay_memset(Cb, 128, (frame->ssm ? frame->len : frame->uv_len));
			veejay_memset(Cr, 128, (frame->ssm ? frame->len : frame->uv_len));
		}
		else {
			uint8_t *aA = frame->data[3];
			for(r=0; r < len; r+=width) {
				for(c=0; c < width; c++) {
				uint8_t p = Y[r+c];
					if( p > min_threshold && p < max_threshold) {
						aA[r+c] = 0xff;
					}
					else {
						aA[r+c] = 0;
					}
				}
			}
		}
	}
	else
	{
		if( gamma != last_gamma ) {
			gamma_setup( width,height,(double) gamma / 100.0 );
			last_gamma = gamma;
		}
	
		if( mode == 0 ) {
			for(r=0; r < len; r+=width) {
				for(c=0; c < width; c++) {
					uint8_t p = table[ Y[r+c] ];
					if( p > min_threshold && p < max_threshold) {
						Y[r+c] = pixel_Y_hi_;
					}
					else {
						Y[r+c] = pixel_Y_lo_;
					}
				}
			}
			veejay_memset(Cb, 128, (frame->ssm ? frame->len : frame->uv_len));
			veejay_memset(Cr, 128, (frame->ssm ? frame->len : frame->uv_len));
		}
		else {
			uint8_t *aA = frame->data[3];
			for(r=0; r < len; r+=width) {
				for(c=0; c < width; c++) {
				uint8_t p = table[ Y[r+c] ];
					if( p > min_threshold && p < max_threshold) {
						aA[r+c] = 0xff;
					}
					else {
						aA[r+c] = 0;
					}
				}
			}
		}
	}
}

void bwselect_free(){}
