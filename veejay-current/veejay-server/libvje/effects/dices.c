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
 *
 * This Effect was ported from:
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * dice.c: a 'dicing' effect
 *  copyright (c) 2001 Sam Mertens.  This code is subject to the provisions of
 *  the GNU Public License.
 *
 */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "dices.h"

static int g_map_width = 0;
static int g_map_height = 0;
static int g_cube_size = 0;
static int g_cube_bits = 0;
static uint8_t *g_dicemap=NULL;

void dice_create_map(int w, int h);

typedef enum _dice_dir {
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3,
} DiceDir;

vj_effect *dices_init(int width, int height)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 5;
    ve->description = "Dices (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Dice size" );
    return ve;
}

int	dices_malloc(int width, int height)
{
	g_dicemap = (uint8_t *) vj_malloc(sizeof(uint8_t) * width * height);
	if(!g_dicemap) return 0;
        dice_create_map(width, height);
	return 1;
}

void dices_free() {
   if(g_dicemap) 
	   free(g_dicemap);
   g_dicemap  = NULL;
}

unsigned int dices_fastrand_val;
/* effectv's rand val */
unsigned int dices_fastrand()
{
    return (dices_fastrand_val = dices_fastrand_val * 1103515245 + 12345);
}

void dice_create_map(int w, int h)
{
    int k,x, y, i = 0;
    int maplen = (w * h);
    g_map_height = h >> g_cube_bits;
    g_map_width = w >> g_cube_bits;
    g_cube_size = 1 << g_cube_bits;
    maplen = maplen / (g_map_height * g_map_width);
    for( k = 0; k < maplen;k++) {
    for (y = 0; y < g_map_height; y++) {
	for (x = 0; x < g_map_width; x++) {
	    //g_dicemap[i] = j++;
	    //if(j==3) j=0;
	    g_dicemap[i] = ((1 + (rand() * (i + y))) & 0x03);
		i++;
	}
    }
	}
//	fprintf(stderr, "created map of %d, %d did %d in dicemap, total is %d\n",
//		w,h,i,(w*h));
}

void dices_apply( void* data, VJFrame *frame, int width, int height,
		 int cube_bits)
{

    int i = 0, map_x, map_y, map_i = 0, base, dx, dy, di;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

    if (cube_bits != g_cube_bits) {
	g_cube_bits = cube_bits;
	dice_create_map(width, height);
    }
    //printf("g_cube_bits = %d, g_cube_size= %d range = %d, %d\n", g_cube_bits, g_cube_size, g_map_width,g_map_height);


    for (map_y = 0; map_y < g_map_height; map_y++) {
	for (map_x = 0; map_x < g_map_width; map_x++) {
	    base = (map_y << g_cube_bits) * width + (map_x << g_cube_bits);
	    switch (g_dicemap[map_i]) {
	    case Left:
		for (dy = 0; dy < g_cube_size; dy++) {
		    i = base + dy * width;
		    for (dx = 0; dx < g_cube_size; dx++) {
			di = base + (dx * width) + (g_cube_size - dy - 1);
			Y[di] = Y[i];
			Cb[di] = Cb[i];
			Cr[di] = Cr[i];
			i++;
		    }
		}
		break;
	    case Down:
		for (dy = 0; dy < g_cube_size; dy++) {
		    di = base + dy * width;
		    i = base + (g_cube_size - dy - 1) * width +
			g_cube_size;
		    for (dx = 0; dx < g_cube_size; dx++) {
			i--;
			Y[di] = Y[i];
			Cb[di] = Cb[i];
			Cr[di] = Cr[i];
			di++;
		    }
		}
		break;
	    case Right:
		for (dy = 0; dy < g_cube_size; dy++) {
		    i = base + (dy * width);
		    for (dx = 0; dx < g_cube_size; dx++) {
			di = base + dy + (g_cube_size - dx - 1) * width;
			Y[di] = Y[i];
			Cb[di] = Cb[i];
			Cr[di] = Cr[i];
			i++;
		    }
		}
		break;
	     case Up:
		for( dy = 0; dy < g_cube_size ; dy ++ )
		{
			i =  base + dy * width;
			for( dx  =  0; dx  < g_cube_size   ; dx ++ )
			{
				Y[di]  = Y[i]; Cb[di] =  Cb[i]; Cr[di]  =  Cr[i]; i ++;
			}
		}
		break;
	    }
	    map_i++;
	}
    }
}
