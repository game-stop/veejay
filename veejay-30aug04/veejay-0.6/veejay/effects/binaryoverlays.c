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
#include "binaryoverlays.h"
#include <stdlib.h>
#include "common.h"

vj_effect *binaryoverlay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->description = "Binary Overlays";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->extra_frame = 1;
    ve->sub_format = 0;
    ve->has_internal_data = 0;
    return ve;
}

/* rename methods in lumamagick and chromamagick */

static void _binary_not_and( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) & ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) & ~(yuv2[1][i]);
		yuv1[2][i] = ~(yuv1[2][i]) & ~(yuv2[2][i]);
	}
}
static void _binary_not_xor( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) ^ ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) ^ ~(yuv2[1][i]);
		yuv1[2][i] = ~(yuv1[2][i]) ^ ~(yuv2[2][i]);
	}
}

static void _binary_not_or( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) | ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) | ~(yuv2[1][i]);
		yuv1[2][i] = ~(yuv1[2][i]) | ~(yuv2[2][i]);
	}
}

// this is also sub, sub = A & ~(B)
static void _binary_not_and_lh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = yuv1[0][i] & ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = yuv1[1][i] & ~(yuv2[1][i]);
		yuv1[2][i] = yuv1[2][i] & ~(yuv2[2][i]);
	}
}
static void _binary_not_xor_lh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = yuv1[0][i] ^ ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = yuv1[1][i] ^ ~(yuv2[1][i]);
		yuv1[2][i] = yuv1[2][i] ^ ~(yuv2[2][i]);
	}
}

static void _binary_not_or_lh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = yuv1[0][i] | ~(yuv2[0][i]);
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = yuv1[1][i] | ~(yuv2[1][i]);
		yuv1[2][i] = yuv1[2][i] | ~(yuv2[2][i]);
	}
}
static void _binary_not_and_rh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) & yuv2[0][i];
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) & yuv2[1][i];
		yuv1[2][i] = ~(yuv1[2][i]) & yuv2[2][i];
	}
}
static void _binary_not_xor_rh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) ^ yuv2[0][i];
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) ^ yuv2[1][i];
		yuv1[2][i] = ~(yuv1[2][i]) ^ yuv2[2][i];
	}
}

static void _binary_not_or_rh( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
	{
		yuv1[0][i] = ~(yuv1[0][i]) | yuv2[0][i];
	}
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = ~(yuv1[1][i]) | yuv2[1][i];
		yuv1[2][i] = ~(yuv1[2][i]) | yuv2[2][i];
	}
}




static void _binary_or( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
		yuv1[0][i] = yuv1[0][i] | yuv2[0][i];
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = yuv1[1][i] | yuv2[1][i];
		yuv1[2][i] = yuv1[2][i] | yuv2[2][i];
	}
}
static void _binary_and( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 4;
	int i;
	for(i=0; i < len; i++)
		yuv1[0][i] = yuv1[0][i] & yuv2[0][i];
	for(i=0; i < uv_len; i++)
	{
		yuv1[1][i] = yuv1[1][i] & yuv2[1][i];
		yuv1[2][i] = yuv1[2][i] & yuv2[2][i];
	}
}



void binaryoverlay_apply( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int mode )
{
	switch(mode)
	{
		case 0:	_binary_not_and( yuv1,yuv2,w,h ); break;// not a and not b
		case 1: _binary_not_or( yuv1,yuv2,w,h); break; // not a or not b
		case 2: _binary_not_xor( yuv1,yuv2,w,h); break; // not a xor not b
		case 3:	_binary_not_and_lh( yuv1,yuv2,w,h ); break; // a and not b 
		case 4: _binary_not_or_lh( yuv1,yuv2,w,h); break; // a or not b 
		case 5: _binary_not_xor_lh( yuv1,yuv2,w,h); break; // a xor not b 
		case 6: _binary_not_and_rh (yuv1,yuv2,w,h); break; // a and not b
		case 7: _binary_not_or_rh( yuv1,yuv2,w,h); break; // a or not b
		case 8: _binary_or( yuv1,yuv2,w,h); break; // a or b
		case 9: _binary_and( yuv1,yuv2,w,h); break; // a and b
	}
}

void binaryoverlay_free() {}
