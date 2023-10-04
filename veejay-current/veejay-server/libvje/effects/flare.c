/*
 * VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include "flare.h"

vj_effect *flare_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults =  (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 0;
	ve->defaults[1] = 25;
	ve->defaults[2] = 15;
	ve->description = "Flare (Glow)";
	ve->limits[0][0] = 0; /* p0 = type, p2 = opacity, p3 = radius */
	ve->limits[1][0] = 3;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 255;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 100;
	ve->extra_frame = 0;
	ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params , "Mode", "Opacity", "Radius" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints,ve->limits[1][0],0, "Simple", "Exclusive", "Darken", "Lighten" );

	return ve;
}

typedef struct {
    uint8_t *flare_buf[4];
} flare_t;

void *flare_malloc(int w, int h)
{
    flare_t *f = (flare_t*) vj_calloc(sizeof(flare_t));
    if(!f) {
        return NULL;
    }

    const int len = w*h;
    const int total_len = len * 3;

    f->flare_buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * total_len );
    if(!f->flare_buf[0]) {
        free(f);
        return NULL;
    }

    f->flare_buf[1] = f->flare_buf[0] + len;
    f->flare_buf[2] = f->flare_buf[1] + len;
	
    return (void*) f;
}

void	flare_free(void *ptr)
{
    flare_t *f = (flare_t*) ptr;
    free(f->flare_buf[0]);
    free(f);
}
static void flare_exclusive(VJFrame *frame, VJFrame *frame2, int width, int height, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    int a, b, c;
    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        a = Y[i];
        b = Y2[i];

        a = (a * o1) >> 8;
        b = (b * o2) >> 8;
        Y[i] = a + b - ((a * b) >> 8);

        a = Cb[i] - 128;
        b = Cb2[i] - 128;
        c = a + b - ((a * b) >> 8);
        c += 128;
        Cb[i] = CLAMP_UV(c);

        a = Cr[i] - 128;
        b = Cr2[i] - 128;
        c = a + b - ((a * b) >> 8);
        c += 128;
        Cr[i] = CLAMP_UV(c);
    }
}

static void flare_darken(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
{

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for(i=0; i < len; i++)
	{
		if(Y[i] > Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		} 
	}
}

static void	flare_simple( VJFrame *frame, VJFrame *frame2, int w, int h, int op_a )
{
	unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	const uint8_t solid = 255 - op_a;
	uint8_t premul;
	for (i = 0; i < len; i++)
	{
		premul = ((Y2[i] * op_a) + (solid * 0xff )) >> 8; 
		Y[i] = ( (Y[i] >> 1) + (premul >> 1 ) );
    }

}

static void flare_lighten(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
{

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for(i=0; i < len; i++)
	{
		if(Y[i] < Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		} 
	}
}

void flare_apply(void *ptr, VJFrame *frame, int *args) {
    int type = args[0];
    int op_a = args[1];
    int radius = args[2];

    flare_t *f = (flare_t*) ptr;

	int y,x;
	int plane = 0;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	/* clone frame */
	VJFrame frame2;
	veejay_memcpy( &frame2, frame, sizeof(VJFrame));
	/* data is local */
	frame2.data[0] = f->flare_buf[0];
	frame2.data[1] = f->flare_buf[1];
	frame2.data[2] = f->flare_buf[2];
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, frame2.data,strides );

	/* apply blur on Image, horizontal and vertical
	   (blur2 is from xine, see radial blur */
	
	for( plane = 0; plane < 2; plane ++ )
	{
		for( y = 0; y < height; y ++ ) 
			blur2( frame->data[plane] + (y * width),
				   frame2.data[plane] + (y * width),
					width,
					radius,
					1,
					1,	
					1 );
	}

	for( plane = 0; plane <2; plane ++ )
	{
		for( x = 0; x < width; x ++ )
			blur2( frame->data[plane] + x ,
				frame2.data[plane] + x,
				height,
				radius,
				1,
				width,
				width );

	}
	/* overlay original on top of blurred image */
	switch( type )
	{
    	case 1:
			flare_exclusive(frame,&frame2,width,height,op_a);
			break;
		case 2:
			flare_darken(frame,&frame2,width,height,op_a);
			break;
		case 3:
			flare_lighten( frame, &frame2, width, height, op_a );
			break;

		default:
			flare_simple( frame, &frame2, width,height, op_a );
			break;

	}
}


