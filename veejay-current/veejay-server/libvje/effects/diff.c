/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <libavutil/avutil.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "softblur.h"
#include "diff.h"

typedef struct {
    uint8_t *static_bg;
    uint32_t *dt_map;
	uint8_t *data;
	uint8_t *current;
} diff_t;

vj_effect *diff_init(int width, int height)
{
	//int i,j;
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;	/* reverse */
	ve->limits[1][1] = 1;
	ve->limits[0][2] = 0;	/* show mask */
	ve->limits[1][2] = 2;
	ve->limits[0][3] = 1;	/* thinning */
	ve->limits[1][3] = 100;

	ve->defaults[0] = 30;
	ve->defaults[1] = 0;
	ve->defaults[2] = 2;
	ve->defaults[3] = 5;

	ve->description = "Map B to A (substract background mask)";
	ve->extra_frame = 1;
	ve->sub_format = 1;
	ve->has_user = 1;
    ve->static_bg = 1;

	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode", "Show mask/image", "Thinning" );
	ve->hints = vje_init_value_hint_list( ve->num_params );
	
	vje_build_value_hint_list( ve->hints, ve->limits[1][2],2, "Show Difference", "Show Distance Map", "Normal" );

	return ve;
}

void *diff_malloc(int width, int height)
{
	diff_t *d = (diff_t*) vj_calloc(sizeof(diff_t));
    if(!d) {
        return NULL;
    }

	d->data = (uint8_t*) vj_calloc( (sizeof(uint8_t) * width * height + width) );
    if(!d->data) {
        diff_free(d);
        return NULL;
    }
	d->static_bg = (uint8_t*) vj_calloc( sizeof(uint8_t) * ( width * height ) + (width * 2));
	if(!d->static_bg) {
        diff_free(d);
        return NULL;
    }

	d->dt_map = (uint32_t*) vj_calloc( sizeof(uint32_t) * (width * height) + (width * 2));
	if(!d->dt_map) {
        diff_free(d);
        return NULL;
    }

    return (void*) d;
}

void diff_free(void *ptr)
{
    diff_t *d = (diff_t*) ptr;
    if(d->data) {
        free(d->data);
    }
    if(d->static_bg) {
        free(d->static_bg);
    }
    if(d->dt_map) {
        free(d->dt_map);
    }
    free(d);
}

int diff_prepare(void *ptr, VJFrame *frame )
{
    diff_t *d = (diff_t*) ptr;
	veejay_memcpy( d->static_bg, frame->data[0], frame->len );
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = d->static_bg;
    tmp.len = frame->len;
    tmp.width = frame->width;
    tmp.height = frame->height;
	softblur_apply_internal( &tmp, 0);
	veejay_msg(2 , "Map B to A: Snapped background frame");

	return 1;
}


void diff_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int threshold = args[0];
    int reverse = args[1];
    int mode = args[2];
    int feather = args[3];

	unsigned int i;
	const int len = frame->len;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	diff_t *d = (diff_t*) ptr;

    uint32_t *dt_map = d->dt_map;
    uint8_t *static_bg = d->static_bg;

	//@ clear distance transform map
	vj_frame_clear1( (uint8_t*) dt_map, 0 , len * sizeof(uint32_t) );

	binarify( d->data, static_bg, frame->data[0], threshold, reverse,len );

	//@ calculate distance map
	veejay_distance_transform8( d->data, width, height, dt_map );
	
	if(mode==1)
	{
		//@ show difference image in grayscale
		vj_frame_copy1( d->data, Y, len );
		vj_frame_clear1( Cb, 128, len );
		vj_frame_clear1( Cr, 128, len );

		return;
	} 
	else if (mode == 2 )
	{
		//@ show dt map as grayscale image, intensity starts at 128
#pragma omp simd
		for( i = 0; i  < len ; i ++ )
		{
			if( dt_map[i] == feather )	
				Y[i] = 0xff; //@ border white
			else if( dt_map[i] > feather )	{
				Y[i] = 128 + (dt_map[i] % 128); //grayscale value
			} else if ( dt_map[i] == 1 ) {
				Y[i] = 0xff;
			} else {
				Y[i] = 0;
			}
			Cb[i] = 128;	
			Cr[i] = 128;
		}
		return;
	}

#pragma omp simd
	//@ process dt map
	for( i = 0; i < len ;i ++ )
	{
		if( dt_map[ i ] >= feather )
		{
			Y[i] = Y2[i];
			Cb[i] = Cb2[i];
			Cr[i] = Cr2[i];
		}
		else
		{
			Y[i] = pixel_Y_lo_;
			Cb[i] = 128;
			Cr[i] = 128;
		}
	}
}




