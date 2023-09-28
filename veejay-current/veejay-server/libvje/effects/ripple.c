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


/* This effect recalculates a pretty large table if 'waves' or 'amplitude'
   is changed. Results will be placed in ripple_table, a copy of the 
   frame is kept in ripple_data. So is the calculation of the first frame slow,
   the following frames will use the cached coordinates until the user changes
   the number of waves or the amplitude. 


*/

#include "common.h"
#include <veejaycore/vjmem.h>
#include "ripple.h"

#define RIPPLE_DEGREES 360
#define RIPPLE_VAL 180.0

typedef struct {
    double *ripple_table;
    uint8_t *ripple_data[4];
    double *ripple_sin;
    double *ripple_cos;
    int ripple_waves;
    int ripple_ampli;
    int ripple_attn;
} ripple_t;

vj_effect *ripple_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 3600;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 80;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 360;
    ve->defaults[0] = 132;
    ve->defaults[1] = 47;
    ve->defaults[2] = 7;
    ve->description = "Ripple";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Waves", "Amplitude", "Attenuation");
    return ve;
}

void *ripple_malloc(int width, int height)
{
    int i;
    ripple_t *r = (ripple_t*) vj_calloc(sizeof(ripple_t));
    if(!r) {
        return NULL;
    }

    
    r->ripple_table = (double*) vj_malloc(sizeof(double) * ((width * height) + width) );
    if(!r->ripple_table) {
        free(r);
        return NULL;
    }

    r->ripple_data[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * 3 * ( (width * height) + width) );
    if(!r->ripple_data[0]) {
        free(r->ripple_table);
        free(r);
        return NULL;
    }

    r->ripple_data[1] = r->ripple_data[0] +((width*height) + width);
    r->ripple_data[2] = r->ripple_data[1] +((width*height) + width);

	veejay_memset( r->ripple_data[1], 128, (width * height) + width );
	veejay_memset( r->ripple_data[2], 128, (width * height) + width );
    veejay_memset( r->ripple_data[0], pixel_Y_lo_, (width*height) + width);

    r->ripple_sin = (double*) vj_malloc(sizeof(double) * RIPPLE_DEGREES);
    if(!r->ripple_sin) {
        free(r->ripple_table);
        free(r->ripple_data);
        free(r);
        return NULL;
    }
    r->ripple_cos = (double*) vj_malloc(sizeof(double) * RIPPLE_DEGREES);
    if(!r->ripple_cos) {
        free(r->ripple_table);
        free(r->ripple_data);
        free(r->ripple_sin);
        free(r);
        return NULL;
    }
    
    for(i=0; i < RIPPLE_DEGREES; i++) {
 		fast_sin(r->ripple_sin[i], (M_PI * i) / RIPPLE_VAL);
		fast_sin(r->ripple_cos[i], (M_PI * i) / RIPPLE_VAL);
    }

    return (void*) r;

}

void ripple_free(void *ptr) {
	
    ripple_t *r = (ripple_t*) ptr;

    free(r->ripple_table);
    free(r->ripple_sin);
    free(r->ripple_cos);
    free(r->ripple_data[0]);
    free(r);
}


void ripple_apply(void *ptr, VJFrame *frame, int *args ) {

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	double wp2 = width * 0.5;
	double hp2 = height * 0.5;
	int x,y,dx,dy,a=0,sx=0,sy=0,angle=0;
	double r,z;
	double maxradius,frequency,amplitude;
    
    int _w = args[0];
    int _a = args[1];
    int _att = args[2];

	double waves = (_w/10.0);
	double ampli = (double) (_a/10.0);
	double attenuation = (_att/10.0);
	
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    ripple_t *ripple = (ripple_t*) ptr;

	fast_sqrt(maxradius, wp2 * wp2 + hp2 * hp2);
	
	frequency = 360.0 * waves / maxradius;
	amplitude = maxradius / ampli;

	int have_calc_data=0;

	if(ripple->ripple_waves != _w || ripple->ripple_ampli != _a || ripple->ripple_attn != _att) {
		ripple->ripple_waves = _w;
		ripple->ripple_ampli = _a;
		ripple->ripple_attn = _att;	
		have_calc_data=1;
	}
	
	int strides[4] = { len, len, len,0 };
	vj_frame_copy( frame->data, ripple->ripple_data , strides );

    double *ripple_table = ripple->ripple_table;
    uint8_t **ripple_data = ripple->ripple_data;
    double *ripple_sin = ripple->ripple_sin;
    double *ripple_cos = ripple->ripple_cos;

	if (have_calc_data) {
  	   for(y=0; y < height-1;y++) {
		for (x=0; x < width; x++) {
		  dx = x - wp2;
		  dy = y - hp2;
		  
		  angle = 180.0 * (atan2(dx,dy)/M_PI);

		  if (angle < 0) angle+=360.0;

		  fast_sqrt( r, dx * dx + dy * dy);
	
		  z = amplitude/ pow(r,attenuation) * ripple_sin[ ((int)(frequency * r)) % 360 ];

		  a = ((int) (angle)) % 360;
		  sx = (int) (x+z * ripple_cos[a]);
		  sy = (int) (y+z * ripple_sin[a]);

		  if(sy > (height-1)) sy = height-1;
		  if(sx > width) sx = width;
		  if(sx < 0) sx =0;
		  if(sy < 0) sy =0;
	 		
		  ripple_table[(y*width)+x] = (sx + (sy * width));

		  Y[((y * width) +x)] = ripple_data[0][(sx +( sy * width)) ];		
		  Cb[((y * width) +x)] = ripple_data[1][(sx +( sy * width)) ];
		  Cr[((y * width) +x)] = ripple_data[2][(sx +( sy * width)) ];
		}
	    }
	}
	else {
	   for(y=0; y < height-1;y++) {
		for (x=0; x < width; x++) {
		  sx = (int) ripple_table[(y*width)+x];	
		  Y[(y * width) +x] = ripple_data[0][sx];
		  Cb[(y * width) +x] = ripple_data[1][sx];
		  Cr[(y * width) +x] = ripple_data[2][sx];
		}
  	   }
	}
}
