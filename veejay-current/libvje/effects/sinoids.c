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
#include <math.h>
#include "sinoids.h"
#include <stdlib.h>
#include "common.h"

static int *sinoids_X;
static uint8_t *sinoid_frame[3];

vj_effect *sinoids_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->defaults[1] = 70;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000; /* sinoids */
    ve->description = "Sinoids";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
    return ve;
}
//FIXME private
int sinoids_malloc(int width, int height)
{
	int i = 0;
   sinoids_X = (int*) vj_calloc(sizeof(int) * width);
  if(!sinoids_X) return 0;

  sinoid_frame[0] = (uint8_t*)vj_calloc(sizeof(uint8_t) * 3 * height * width);

  if(!sinoid_frame[0]) return 0;

  sinoid_frame[1] = sinoid_frame[0] + (width * height);
  sinoid_frame[2] = sinoid_frame[1] + (width * height);

    
  for(i=0; i < width; i++ ) {
	sinoids_X[i] = (int) ( sin( ((double)i/(double)width) * 2 * 3.1415926) * 1);
	sinoids_X[i] *= 4;
	}
  
  return 1;

}

void sinoids_free() {
	if(sinoids_X) free(sinoids_X);
	if(sinoid_frame[0])
		free(sinoid_frame[0]);
	sinoid_frame[0] = NULL;
	sinoid_frame[1] = NULL;
	sinoid_frame[2] = NULL;
}

void sinoids_recalc(int width, int z) {
	int i=0;
	double zoom = ( (double)z / 10.0);
	for(i=0; i < width; i++ ) {
	  //fast_sin(si,  (  ((double)i/(double)width)*2*3.1415926));//
	  //sinoids_X[i] = (int) si;
	  sinoids_X[i] = (int) ( sin( ((double)i/(double)width) * 2 * 3.1415926) * zoom);
	  sinoids_X[i] *= 4;
	}
}

static int current_sinoids = 100;
void sinoids_apply(VJFrame *frame, int width, int height, int m, int s) {
	unsigned int len = width * height;
	unsigned int r,c;
  uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	
        if(s != current_sinoids) {
		sinoids_recalc( width, s);
		current_sinoids = s;
	}

	if(m==0) {
  	  for( r=width ; r < len-width; r+=width) {
             for( c = 0; c < width; c++) {
		Y[r+c] = Y[(r+c+sinoids_X[c])];
		Cb[r+c] = Cb[(r+c+sinoids_X[c])];
		Cr[r+c] = Cr[(r+c+sinoids_X[c])];
	     }
	  }
	}
        else {
		/* on copy */
	      for(r=0; r < len ;r++) {
		sinoid_frame[0][r] = Y[r];
		sinoid_frame[1][r] = Cb[r];	
		sinoid_frame[2][r] = Cr[r];
		}
	      for(r=width; r < len-width; r+= width) {
	        for(c=0; c < width; c++) {
	  	  Y[r+c] = sinoid_frame[0][(r+c+sinoids_X[c])];
		  Cb[r+c] = sinoid_frame[1][(r+c+sinoids_X[c])];
		  Cr[r+c] = sinoid_frame[2][(r+c+sinoids_X[c])];
	        }
	      }
	}
}
