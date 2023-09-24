/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
 *
 * vvMaskStop - ported from vvFFPP_basic
 * Copyright(C)2005 Maciek Szczesniak <maciek@visualvinyl.net>
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
#include "maskstop.h"

typedef struct {
    uint8_t *vvmaskstop_buffer[6];
    unsigned int frq_frame;
    unsigned int frq_mask;
} vvmask_t;

vj_effect *maskstop_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0; // negate mask
    ve->defaults[1] = 0; // swap mask/frame
    ve->defaults[2] = 80;   // hold frame freq
    ve->defaults[3] = 20;   // hold mask freq

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

	ve->param_description = vje_build_param_list( ve->num_params, "Negate Mask", "Swap Mask/Frame", "Hold Frame Frequency", "Hold Mask Frequency");

    ve->description = "vvMaskStop";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;

    return ve;
}

void *maskstop_malloc(int width, int height)
{
    vvmask_t *v = (vvmask_t*) vj_calloc(sizeof(vvmask_t));
    if(!v) {
        return NULL;
    }

	int i;
	v->vvmaskstop_buffer[0] =  (uint8_t*) vj_malloc( sizeof(uint8_t)  * RUP8(width * height)  * 6 );
    if(!v->vvmaskstop_buffer[0]) {
        free(v);
        return NULL;
    }

	for( i = 1; i < 6; i ++ )
		v->vvmaskstop_buffer[i] = v->vvmaskstop_buffer[(i-1)] + (width * height);
	
    veejay_memset( v->vvmaskstop_buffer[1], 128, (width*height));
	veejay_memset( v->vvmaskstop_buffer[2], 128, (width*height));
    veejay_memset( v->vvmaskstop_buffer[4], 128, (width*height));
	veejay_memset( v->vvmaskstop_buffer[5], 128, (width*height));
    veejay_memset( v->vvmaskstop_buffer[0], pixel_Y_lo_, (width*height));
    veejay_memset( v->vvmaskstop_buffer[3], pixel_Y_lo_, (width*height));
	
	v->frq_frame = 256;
	v->frq_mask = 256;

	return (void*) v;
}

void maskstop_free(void *ptr) {

    vvmask_t *v = (vvmask_t*) ptr;
    free(v->vvmaskstop_buffer[0]);
    free(v);
}


void maskstop_apply( void *ptr, VJFrame *frame, int *args ) {
    int negmask = args[0];
    int swapmask = args[1];
    int framefreq = args[2];
    int maskfreq = args[3];

    vvmask_t *v = (vvmask_t*) ptr;

	int i=0;
	const int len = frame->len;
 
	uint8_t *Yframe = v->vvmaskstop_buffer[0];
	uint8_t *Uframe = v->vvmaskstop_buffer[1];
	uint8_t *Vframe = v->vvmaskstop_buffer[2];
	uint8_t *Ymask  = v->vvmaskstop_buffer[3];
	uint8_t *Umask  = v->vvmaskstop_buffer[4];
	uint8_t *Vmask  = v->vvmaskstop_buffer[5];
	uint8_t *Ydest  = frame->data[0];
	uint8_t *Udest  = frame->data[1];
	uint8_t *Vdest  = frame->data[2];
	
	v->frq_frame = v->frq_frame + framefreq;
	v->frq_mask = v->frq_mask + maskfreq;
	
	if (v->frq_frame > 255) {
		veejay_memcpy(Yframe, Ydest, len);
		veejay_memcpy(Uframe, Udest, len);
		veejay_memcpy(Vframe, Vdest, len);
		v->frq_frame = 0;
	}

	if (v->frq_mask > 255) {
		veejay_memcpy(Ymask, Ydest, len);
		veejay_memcpy(Umask, Udest, len);
		veejay_memcpy(Vmask, Vdest, len);
		v->frq_mask = 0;
	}

	// negmask acts like transparency mask:  new p = ((p0 * a) + (p1 * (255-a))) / 255 
	if(swapmask && negmask)
	{
		for( i = 0; i < len; i ++ )
		{
			Ydest[i] = ((Yframe[i] * Ymask[i]) + ((0xff-Ydest[i]) * (0xff-Ymask[i])))>>8; 
			Udest[i] = ((Uframe[i] * Umask[i]) + ((0xff-Udest[i]) * (0xff-Umask[i])))>>8;
			Vdest[i] = ((Vframe[i] * Vmask[i]) + ((0xff-Vdest[i]) * (0xff-Vmask[i])))>>8;
		}
	}
	if(swapmask && !negmask)
	{
		for( i = 0; i < len ; i ++ )
		{
			Ydest[i] = ((Yframe[i] * (0xff-Ymask[i]) + (Ydest[i] * Ymask[i])))>>8;
			Udest[i] = ((Uframe[i] * (0xff-Umask[i]) + (Udest[i] * Umask[i])))>>8;
			Vdest[i] = ((Vframe[i] * (0xff-Vmask[i]) + (Vdest[i] * Vmask[i])))>>8;
		}
	}
	if(!swapmask && negmask)
	{
		for( i = 0; i < len; i ++ )
		{
			Ydest[i] = ((Ymask[i] * Yframe[i]) + ( (0xff-Ydest[i]) * (0xff-Yframe[i])) ) >> 8;
			Udest[i] = ((Umask[i] * Uframe[i]) + ( (0xff-Udest[i]) * (0xff-Uframe[i])) ) >> 8;
			Vdest[i] = ((Vmask[i] * Vframe[i]) + ( (0xff-Vdest[i]) * (0xff-Vframe[i])) ) >> 8;
		}
	}
	if(!swapmask && !negmask)
	{
		for( i = 0; i < len; i ++ )
		{
			Ydest[i] = ((Ymask[i] * (0xff-Yframe[i])) + ( Ydest[i] * Yframe[i]) ) >> 8;
			Udest[i] = ((Umask[i] * (0xff-Uframe[i])) + ( Udest[i] * Uframe[i]) ) >> 8;
			Vdest[i] = ((Vmask[i] * (0xff-Vframe[i])) + ( Vdest[i] * Vframe[i]) ) >> 8;
		}
	
	}


/*
	for (i = 0; i < len; i++) {
		if (swapmask) {

			if (negmask) {
				val = (float)Ymask[i]/255;
				valN = (float)1 - val;
			} else {
				valN = (float)Ymask[i]/255;
				val = (float)1 - valN;
			}

			Ydest[i] = (uint8_t) Yframe[i] * val + Ydest[i] * valN;
			Udest[i] = (uint8_t) Uframe[i] * val + Udest[i] * valN;
			Vdest[i] = (uint8_t) Vframe[i] * val + Vdest[i] * valN;
			
		} else {

			if (negmask) {
				val = (float)Yframe[i]/255;
				valN = (float)1 - val;
			} else {
				valN = (float)Yframe[i]/255;
				val = (float)1 - valN;
			}
			
			Ydest[i] = (uint8_t) Ymask[i] * val + Ydest[i] * valN;
			Udest[i] = (uint8_t) Umask[i] * val + Udest[i] * valN;
			Vdest[i] = (uint8_t) Vmask[i] * val + Vdest[i] * valN;
			
		}

	}
	*/
}

