/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail>
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

/* Radial Distortion Correction
 * http://local.wasp.uwa.edu.au/~pbourke/projection/lenscorrection/
 *
 */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "radcor.h"
#include "common.h"
vj_effect *radcor_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 1000;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 1000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = 10;
    ve->defaults[1] = 40;
    ve->defaults[2] = 0;
    ve->description = "Lens correction";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Alpha X", "Alpha Y", "Direction");
    return ve;
}

static uint8_t *badbuf = NULL;
static uint32_t *Map = NULL;
static int map_upd[3] = {0,0,0};

int	radcor_malloc( int width, int height )
{
	badbuf = (uint8_t*) vj_malloc( RUP8( width * height * 3 * sizeof(uint8_t)));
	if(!badbuf)
		return 0;
	Map    = (uint32_t*) vj_malloc( RUP8(width * height * sizeof(uint32_t)));
	veejay_memset( Map, 0, RUP8(width * height * sizeof(uint32_t)) );
	if(!Map)
		return 0;
	return 1;
}

void	radcor_free()
{
	free(badbuf);
	free(Map);
	badbuf = NULL;
	Map = NULL;
}

typedef struct
{
	uint32_t y;
	uint32_t v;
	uint32_t u;
} pixel_t;


void radcor_apply( VJFrame *frame, int width, int height, int alpaX, int alpaY, int dir)
{
	int i,j;
	int len = (width * height);
	int i2,j2;
	double x,y,x2,x3,y2,y3,r;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	pixel_t csum,c;
	int aa = 1;
	int nx = width;
	int ny = height;
	int nxout = nx;
	int nyout = ny;

	//@ copy source image to internal buffer 
	uint8_t *dest[4] = { badbuf, badbuf + len, badbuf + len + len,NULL };
	int strides[4] = { len, len, len, 0 };
	vj_frame_copy( frame->data, dest, strides );

	uint8_t *Yi = badbuf;
	uint8_t *Cbi = badbuf + len;
	uint8_t *Cri = badbuf + len + len;

	double alphax = alpaX / (double) 1000.0;
	double alphay = alpaY / (double) 1000.0;

	if(!dir)
	{
		alphax *= -1.0; // inward, outward, change sign
		alphay *= -1.0;
	}

	vj_frame_clear1( Y, 0, len );
	vj_frame_clear1( Cb, 128, len );
	vj_frame_clear1( Cr, 128, len );

	int update_map = 0;

	if( map_upd[0] != alpaX || map_upd[1] != alpaY || map_upd[2] != dir )
	{
		map_upd[0] = alpaX;
		map_upd[1] = alpaY;
		map_upd[2] = dir;
		update_map = 1;
	}

	if( update_map )
	{
		for( i = 0; i < nyout; i ++ ) 
		{
			for( j = 0; j < nxout; j ++ )
			{	
				x = ( 2 * j - nxout ) / (double) nxout;
				y = ( 2 * i - nyout ) / (double) nyout;

				r = x*x + y*y;
				x3 = x / (1 - alphax * r);
				y3 = y / (1 - alphay * r); 
				x2 = x / (1 - alphax * (x3*x3+y3*y3));
				y2 = y / (1 - alphay * (x3*x3+y3*y3));	
				i2 = (y2 + 1) * ny / 2;
				j2 = (x2 + 1) * nx / 2;
	
				if( i2 >= 0 && i2 < ny && j2 >= 0 && j2 < nx )
					Map[ i * nxout + j ] = i2 * nx + j2;
				else
					Map[ i * nxout + j ] = 0;

			}
		}

	}


	// process

	for( i = 0; i < height; i ++ )
	{
		for( j = 0; j < width ; j ++ )
		{
			Y[ i * width + j ] = Yi[ Map[i * width + j] ];
			Cb[ i * width + j ] = Cbi[ Map[i * width + j] ];
			Cr[ i * width + j ] = Cri[ Map[i * width + j] ];
		}
	}

}
