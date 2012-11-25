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
#include <config.h>
#include "binaryoverlays.h"
#include <libvje/effects/common.h>

vj_effect *binaryoverlay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->description = "Binary Overlays";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 10;
    ve->parallel = 1;
    ve->extra_frame = 1;
    ve->sub_format = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode");
    return ve;
}

/* rename methods in lumamagick and chromamagick */

static void _binary_not_and( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) & ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + (~(Cb[i]-128) & ~(Cb2[i]-128));
		Cr[i] = 128 + (~(Cr[i]-128) & ~(Cr2[i]-128));
	}
}
static void _binary_not_xor( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) ^ ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + (~(Cb[i]-128) ^ ~(Cb2[i]-128));
		Cr[i] = 128 + (~(Cr[i]-128) ^ ~(Cr2[i]-128));
	}
}

static void _binary_not_or( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) | ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + ( ~(Cb[i]-128) | ~(Cb2[i]-128) );
		Cr[i] = 128 + (~(Cr[i]-128) | ~(Cr2[i]-128) );
	}
}

// this is also sub, sub = A & ~(B)
static void _binary_not_and_lh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	int i;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	for(i=0; i < len; i++)
	{
		Y[i] = Y[i] & ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + (Cb[i]-128 & ~(Cb2[i]));
		Cr[i] = 128 + (Cr[i] & ~(Cr2[i]));
	}
}
static void _binary_not_xor_lh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = Y[i] ^ ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 +  ( Cb[i]-128 ^ ~(Cb2[i]-128 ));
		Cr[i] = 128 +  ( Cr[i]-128 ^ ~(Cr2[i]-128));
	}
}

static void _binary_not_or_lh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = Y[i] | ~(Y2[i]);
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + ( Cb[i]-128 | ~(Cb2[i]-128));
		Cr[i] = 128 + ( Cr[i]-128 | ~(Cr2[i]-128));
	}
}
static void _binary_not_and_rh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) & Y2[i];
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + ( ~(Cb[i]-128) & Cb2[i]-128);
		Cr[i] = 128 + ( ~(Cr[i]-128) & Cr2[i]-128);
	}
}
static void _binary_not_xor_rh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) ^ Y2[i];
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + ( ~(Cb[i]-128) ^ Cb2[i]-128);
		Cr[i] = 128 + (~(Cr[i]-128) ^ Cr2[i]-128);
	}
}

static void _binary_not_or_rh( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int i;
	for(i=0; i < len; i++)
	{
		Y[i] = ~(Y[i]) | Y2[i];
	}
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = 128 + ( ~(Cb[i]-128) | Cb2[i]-128);
		Cr[i] = 128 + ( ~(Cr[i]-128) | Cr2[i]-128);
	}
}




static void _binary_or( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];


	int i;
	for(i=0; i < len; i++)
		Y[i] = Y[i] | Y2[i];
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = Cb[i] | Cb2[i];
		Cr[i] = Cr[i] | Cr2[i];
	}
}
static void _binary_and( VJFrame *frame, VJFrame *frame2, int w, int h )
{
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];


	int i;
	for(i=0; i < len; i++)
		Y[i] = Y[i] & Y2[i];
	for(i=0; i < uv_len; i++)
	{
		Cb[i] = Cb[i] & Cb2[i];
		Cr[i] = Cr[i] & Cr2[i];
	}
}



void binaryoverlay_apply( VJFrame *frame, VJFrame *frame2, int w, int h, int mode )
{
	switch(mode)
	{
		case 0:	_binary_not_and( frame,frame2,w,h ); break;// not a and not b
		case 1: _binary_not_or( frame,frame2,w,h); break; // not a or not b
		case 2: _binary_not_xor( frame,frame2,w,h); break; // not a xor not b
		case 3:	_binary_not_and_lh( frame,frame2,w,h ); break; // a and not b 
		case 4: _binary_not_or_lh( frame,frame2,w,h); break; // a or not b 
		case 5: _binary_not_xor_lh( frame,frame2,w,h); break; // a xor not b 
		case 6: _binary_not_and_rh (frame,frame2,w,h); break; // a and not b
		case 7: _binary_not_or_rh( frame,frame2,w,h); break; // a or not b
		case 8: _binary_or( frame,frame2,w,h); break; // a or b
		case 9: _binary_and( frame,frame2,w,h); break; // a and b
		case 10: _binary_not_xor_rh(frame,frame2,w,h); break; 
	}
}

void binaryoverlay_free() {}
