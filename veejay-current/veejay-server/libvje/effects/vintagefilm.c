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
#include <libvje/internal.h>
#include "vintagefilm.h"

vj_effect *vintagefilm_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 5;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 10;
	ve->defaults[1] = 20;
	ve->defaults[2] = 5;
	ve->defaults[3] = 50;
	ve->defaults[4] = 0;
	ve->description = "Vintage Film";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 100;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 100;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 100;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 500;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 1;
	ve->extra_frame = 0;
	ve->sub_format = 1;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Scratch Intensity", "Dust Intensity", "Flicker Intensity", "Flicker Frequency", "True random" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	return ve;
}


void vintagefilm_apply(void *ptr, VJFrame *frame, int *args) {
    float scratchIntensity = args[0] * 0.01;
    float dustIntensity = args[1] * 0.01;
    float flickerIntensity = args[2] * 0.01;
    int flickerFrequency = args[3];
	int mode = args[4];

	// rand() does not run in constant time
	if( mode == 0 ) {
    	for (int i = 0; i < frame->height; ++i) {
        	for (int j = 0; j < frame->width; ++j) {
            	int index = i * frame->width + j;

            	if ((float)fastrand(index) / RAND_MAX < scratchIntensity) {
                	int noise = fastrand(index) % 40 - 20;
                	int newValue = frame->data[0][index] + noise;
                	frame->data[0][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
                	newValue = frame->data[1][index] + noise;
                	frame->data[1][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
                	newValue = frame->data[2][index] + noise;
                	frame->data[2][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
            	}

            	if ((float)fastrand(index) / RAND_MAX < dustIntensity) {
                	int noise = fastrand(index) % 30 - 15;
                	int newValue = frame->data[0][index] + noise;
                	frame->data[0][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
               		newValue = frame->data[1][index] + noise;
            	    frame->data[1][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
           	    	newValue = frame->data[2][index] + noise;
           	    	frame->data[2][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
            	}
        	}
    	}
	}
	else {
    	for (int i = 0; i < frame->height; ++i) {
        	for (int j = 0; j < frame->width; ++j) {
            	int index = i * frame->width + j;

            	if ((float)rand() / RAND_MAX < scratchIntensity) {
                	int noise = rand() % 40 - 20;
                	int newValue = frame->data[0][index] + noise;
                	frame->data[0][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
                	newValue = frame->data[1][index] + noise;
                	frame->data[1][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
                	newValue = frame->data[2][index] + noise;
                	frame->data[2][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
            	}

            	if ((float)rand() / RAND_MAX < dustIntensity) {
                	int noise = rand() % 30 - 15;
                	int newValue = frame->data[0][index] + noise;
                	frame->data[0][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
               		newValue = frame->data[1][index] + noise;
            	    frame->data[1][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
           	    	newValue = frame->data[2][index] + noise;
           	    	frame->data[2][index] = (uint8_t)(newValue < 0 ? 0 : (newValue > 255 ? 255 : newValue));
            	}
        	}
    	}
	}


    if (flickerFrequency == 0)
        return;

    if (rand() % flickerFrequency == 0) {
        float brightnessScale = 1.0 + flickerIntensity;
        for (int i = 0; i < frame->len; ++i) {
            int newValue = (int)(frame->data[0][i] * brightnessScale);
            frame->data[0][i] = (uint8_t)(newValue > 255 ? 255 : newValue);
        }
    }
}

/*
void vintagefilm_apply(void *ptr, VJFrame *frame, int *args ) {
    float scratchIntensity = args[0] * 0.01;
    float dustIntensity = args[1] * 0.01;
    float flickerIntensity = args[2] * 0.01;
	int flickerFrequency = args[3];

    for (int i = 0; i < frame->height; ++i) {
        for (int j = 0; j < frame->width; ++j) {
            int index = i * frame->width + j;

            if ((float)rand() / RAND_MAX < scratchIntensity) {
                int noise = rand() % 40 - 20; 
                frame->data[0][index] = fmin(fmax(frame->data[0][index] + noise, 0), 255);
                frame->data[1][index] = fmin(fmax(frame->data[1][index] + noise, 0), 255);
                frame->data[2][index] = fmin(fmax(frame->data[2][index] + noise, 0), 255);
            }

            if ((float)rand() / RAND_MAX < dustIntensity) {
                int noise = rand() % 30 - 15; 
                frame->data[0][index] = fmin(fmax(frame->data[0][index] + noise, 0), 255);
                frame->data[1][index] = fmin(fmax(frame->data[1][index] + noise, 0), 255);
                frame->data[2][index] = fmin(fmax(frame->data[2][index] + noise, 0), 255);
            }
        }
    }

	if( flickerFrequency == 0 )
			return;

	if( rand() % flickerFrequency == 0 ) {
		float brightnessScale = 1.0 + flickerIntensity;
		for (int i = 0; i < frame->len; ++i) {
			frame->data[0][i] = (uint8_t)fmin(frame->data[0][i] * brightnessScale, 255);
		}
	}


}
*/
