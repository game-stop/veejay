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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "magicoverlays.h"
#include <libvje/internal.h>
#include "common.h"  
#include <veejay/vj-task.h>
vj_effect *overlaymagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->defaults[0] = 0;
    ve->description = "Overlay Magic";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 33;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1; // clear chroma or keep
    ve->extra_frame = 1;
    ve->sub_format = 0;
    ve->has_user = 0;
    ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Keep or clear color" );
    return ve;
}

/* rename methods in lumamagick and chromamagick */


void _overlaymagic_adddistorted(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	
	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + b;
	Y[i] = CLAMP_Y(c);
    }

}

void _overlaymagic_add_distorted(VJFrame *frame, VJFrame *frame2,
				 int width, int height)
{

    unsigned int i;
    uint8_t y1, y2;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	Y[i] = CLAMP_Y(y1 + y2);
    }

}

void _overlaymagic_subdistorted(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    uint8_t y1, y2;
    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	y1 -= y2;
	Y[i] = CLAMP_Y(y1);
    }
}

void _overlaymagic_sub_distorted(VJFrame *frame, VJFrame *frame2,
				 int width, int height)
{

    unsigned int i ;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    uint8_t y1, y2;
    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	Y[i] = y1 - y2;
    }
}

void _overlaymagic_multiply(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];
    for (i = 0; i < len; i++) 
	Y[i] = (Y[i] * Y2[i]) >> 8;
    
}
void _overlaymagic_simpledivide(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];
    for (i = 0; i < len; i++) {
	if(Y2[i] > pixel_Y_lo_ )
		Y[i] = Y[i] / Y2[i];
    }
}

void _overlaymagic_divide(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    int a, b, c;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
    for (i = 0; i < len; i++) {
	b = Y[i] * Y[i];
	c = 255 - Y2[i];
	if (c == 0)
	    c = 1;
	a = b / c;
	Y[i] = a;
    }
}

void _overlaymagic_additive(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a;
	while(len--) { 
		a = Y[len] + (2 * Y2[len]) - 255;
		Y[len] = CLAMP_Y(a);
	}
}


void _overlaymagic_substractive(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{

    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) 
	Y[i] = CLAMP_Y( Y[i] - Y2[i] );
}

void _overlaymagic_softburn(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];

	if ( (a + b) <= pixel_Y_hi_) {
	    if (a == pixel_Y_hi_)
		c = a;
	    else
		c = (b >> 7) / (256 - a);
	} else {
	    if (b <= pixel_Y_lo_) {
		b = 255;
	   }
	   c = 255 - (((255 - a) >> 7) / b);
	}
	Y[i] = c;
    }
}

void _overlaymagic_inverseburn(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a <= pixel_Y_lo_)
	    c = pixel_Y_lo_;
	else
	    c = 255 - (((255 - b) >> 8) / a);
	Y[i] = CLAMP_Y(c);
    }
}


void _overlaymagic_colordodge(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a >= pixel_Y_hi_)

	    c = pixel_Y_hi_;
	else
	    c = (b >> 8) / (256 - a);

	if (c >= pixel_Y_hi_)
	    c = pixel_Y_hi_;
	Y[i] = c;
    }
}

void _overlaymagic_mulsub(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = 255 - Y2[i];
	if (b > pixel_Y_lo_)
	    Y[i] = a / b;
    }
}

void _overlaymagic_lighten(VJFrame *frame, VJFrame *frame2, int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a > b)
	    c = a;
	else
	    c = b;
	Y[i] = c;
    }
}

void _overlaymagic_difference(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	Y[i] = abs(a - b);
    }
}

void _overlaymagic_diffnegate(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
		a = (255 - Y[i]);
		b = Y2[i];
		Y[i] = 255 - abs(a - b);
    }
}

void _overlaymagic_exclusive(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int c;
    for (i = 0; i < len; i++) {
	c = Y[i] + (2 * Y2[i]) - 255;
	Y[i] = CLAMP_Y(c - (( Y[i] * Y2[i] ) >> 8 ));	
    }
}

void _overlaymagic_basecolor(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    int a, b, c, d;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a * b >> 8;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);	//8
	Y[i] = CLAMP_Y(d);
    }
}

void _overlaymagic_freeze(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if ( b > pixel_Y_lo_ )
		Y[i] = CLAMP_Y(255 - ((( 255 - a) * ( 255 - a )) / b));
    }
}

void _overlaymagic_unfreeze(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if( a > pixel_Y_lo_ )
		Y[i] = CLAMP_Y( 255 - ((( 255 - b ) * ( 255 - b )) / a));
    }
}

void _overlaymagic_hardlight(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];

		if (b < 128)
		    c = (a * b) >> 7;
		else
		    c = 255 - ((255 - b) * (255 - a) >> 7);
		Y[i] = c;
    }
}
void _overlaymagic_relativeaddlum(VJFrame *frame, VJFrame *frame2,
				  int width, int height)
{
    unsigned int i;
    int a, b, c, d;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i];
	c = a >> 1;
	b = Y2[i];
	d = b >> 1;
	Y[i] = CLAMP_Y(c + d);
    }
}

void _overlaymagic_relativesublum(VJFrame *frame, VJFrame *frame2,
				  int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		Y[i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_relativeadd(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c, d;
    for (i = 0; i < len; i++) {
	a = Y[i];
	c = a >> 1;
	b = Y2[i];
	d = b >> 1;
	Y[i] = c + d;
    }
}

void _overlaymagic_relativesub(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	Y[i] = (a - b + 255) >> 1;
    }

}
void _overlaymagic_minsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b < a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_maxsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b > a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}



void _overlaymagic_addsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];

	if (b < a) {
	    c = (a + b) >> 1;
	    Y[i] = c;
	}
    }
}


void _overlaymagic_maxselect(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b > a)
	    Y[i] = b;
    }
}

void _overlaymagic_minselect(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b < a)
	    Y[i] = b;
    }
}

void _overlaymagic_addtest(VJFrame *frame, VJFrame *frame2, int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + ((2 * b) - 255)>>1;
	Y[i] = CLAMP_Y(c);
    }
}
void _overlaymagic_addtest2(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + (2 * b) - 255;
	Y[i] = CLAMP_Y(c);
    }


}
void _overlaymagic_addtest4(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	b = b - 255;
	if (b <= pixel_Y_lo_)
	    Y[i] = a;
	else
	    Y[i] = (a * a) / b;
    }

}

void _overlaymagic_try
    (VJFrame *frame, VJFrame *frame2, int width, int height) {
    unsigned int i;
    unsigned int len = width * height;
    int a, b, p, q;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	/* calc p */
	a = Y[i];
	b = Y[i];

	if (b <= pixel_Y_lo_)
	    p = pixel_Y_lo_;
	else
	    p = 255 - ((256 - a) * (256 - a)) / b;
	if (p <= pixel_Y_lo_)
	    p = pixel_Y_lo_;

	/* calc q */
	a = Y2[i];
	b = Y2[i];
	if (b <= pixel_Y_lo_)
	    q = pixel_Y_lo_;
	else
	    q = 255 - ((256 - a) * (256 - a)) / b;
	if (b <= pixel_Y_lo_)
	    q = pixel_Y_lo_;

	/* calc pixel */
	if (q <= pixel_Y_lo_)
	    q = pixel_Y_lo_;
	else
	    q = 255 - ((256 - p) * (256 - a)) / q;

	Y[i] = q;
    }
}

void overlaymagic_apply(VJFrame *frame, VJFrame *frame2, int width,
			int height, int n, int clearchroma)
{
    switch (n) {
    case VJ_EFFECT_BLEND_ADDITIVE:
	_overlaymagic_additive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBSTRACTIVE:
	_overlaymagic_substractive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULTIPLY:
	_overlaymagic_multiply(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIVIDE:
	_overlaymagic_simpledivide(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_LIGHTEN:
	_overlaymagic_lighten(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFERENCE:
	_overlaymagic_difference(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFNEGATE:
	_overlaymagic_diffnegate(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_EXCLUSIVE:
	_overlaymagic_exclusive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_BASECOLOR:
	_overlaymagic_basecolor(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_FREEZE:
	_overlaymagic_freeze(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_UNFREEZE:
	_overlaymagic_unfreeze(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADD:
	_overlaymagic_relativeadd(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUB:
	_overlaymagic_relativesub(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADDLUM:
	_overlaymagic_relativeaddlum(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUBLUM:
	_overlaymagic_relativesublum(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSEL:
	_overlaymagic_maxselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSEL:
	_overlaymagic_minselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSUBSEL:
	_overlaymagic_minsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSUBSEL:
	_overlaymagic_maxsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDSUBSEL:
	_overlaymagic_addsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDAVG:
	_overlaymagic_add_distorted(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST2:
	_overlaymagic_addtest(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST3:
	_overlaymagic_addtest2(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST4:
	_overlaymagic_addtest4(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULSUB:
	_overlaymagic_mulsub(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SOFTBURN:
	_overlaymagic_softburn(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_INVERSEBURN:
	_overlaymagic_inverseburn(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_COLORDODGE:
	_overlaymagic_colordodge(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDDISTORT:
	_overlaymagic_adddistorted(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBDISTORT:
	_overlaymagic_subdistorted(frame, frame2, width, height);
	break;
    case 32:
	_overlaymagic_try(frame, frame2, width, height);
	break;
    case 33:
 	_overlaymagic_divide(frame,frame2,width,height);
	break;

    }
    if(clearchroma) {
	veejay_memset( frame->data[1], 128, frame->uv_len );
	veejay_memset( frame->data[2], 128, frame->uv_len );
    }
}


void overlaymagic_free(){}
