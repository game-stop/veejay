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

#include "softblur.h"
#include <stdlib.h>


vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 4; /* 3*/
    ve->limits[0][1] = 0; /* 1 */
    ve->limits[1][1] = 255;
    ve->description = "Soft Blur";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void softblur1_apply(uint8_t * yuv1[3], int width, int height)
{
    int r, c;
    int len = (width * height);
 
    for (r = 0; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
	    yuv1[0][c + r] = (yuv1[0][r + c - 1] +
			      yuv1[0][r + c] +
			      yuv1[0][r + c + 1]
				) / 3;
				
	}
    }

}

void softblur3_apply(uint8_t *yuv1[3], int width, int height, int distance) {
	int r,c;
	for(r=width; r < (width*height)-width; r+=width) {
		for(c=1; c < (width-1); c++) {
			uint8_t p1 = yuv1[0][r - width + c - 1]; /* top left */
			uint8_t p2 = yuv1[0][r - width + c ]; /* top center */
			uint8_t p3 = yuv1[0][r + c + 1]; /* top right */
			uint8_t p4 = yuv1[0][r - width + c]; /* left */
			uint8_t p5 = yuv1[0][r + c]; /* center */
			uint8_t p6 = yuv1[0][r + c + 1]; /* right */
			uint8_t p7 = yuv1[0][r + width + c - 1]; /* down left */
			uint8_t p8 = yuv1[0][r + width + c]; /* down center */
			uint8_t p9 = yuv1[0][r + width + c + 1]; /* down right */
			uint8_t center = distance + p5;
			/*
			if(p1 < center && p2 < center && p3 < center && p4 < center && p5 < center &&
				p6 < center && p7 < center && p8 < center && p9 < center) {

				yuv1[0][r+c] = (p1+p2+p3+p4+p5+p6+p7+p8+p9)/9;

			}
			*/

			/* if the pixel value is larger than (distance + center pixel)
                           set the pixel to it's center value */


			if( p1 > center) p1 = p5;
			if( p2 > center) p2 = p5;
			if( p3 > center) p3 = p5;
			if( p4 > center) p4 = p5;
			if( p6 > center) p6 = p5;
			if( p7 > center) p7 = p5;
			if( p8 > center) p8 = p5;
			if( p9 > center) p9 = p5;

			yuv1[0][r+c] = (p1+p2+p3+p4+p5+p6+p7+p8+p9)/9;
		}
	}
}


void softblur2_apply(uint8_t * yuv1[3], int width, int height, int n)
{
    int r, c;
     /* incomplete */
    for (r = 0; r < (width * height); r += width) {
	for (c = 1; c < width-1; c++) {
	    if (yuv1[0][c + r] > n) {
		yuv1[0][c + r] = (yuv1[0][r + c - 1] +
				  yuv1[0][r + c] +
				  yuv1[0][r + c + 1]
		    ) / 3;
	    }
	}
    }
}


void sharpen_apply(uint8_t * yuv1[3], int width, int height, int n)
{
    double k = n / 100.0;

    unsigned int i, len = width * height;
    uint8_t p;
    for (i = 0; i < len; i++) {
	p = yuv1[0][i] + (k * yuv1[0][i]);
	yuv1[0][i] = p < 16 ? 16 : p > 240 ? 240 : p;
    }

}
/*
void softblur_apply(uint8_t * yuv1[3], int width, int height, int lightx, int lighty) {
    unsigned int normalx, normaly, x, y;

    for (y = 1; y < height - 1; y++) {
	for (x = 0; x < width; x++) {
	    int i1 = yuv1[0][x + (y * width)];
	    int i2 = yuv1[0][x + 1 + (y * width)];
	    int i3 = yuv1[0][x - (y * width)];
	    normalx = i2 - i1 + lightx;
	    normaly = i1 - i3 + lighty;
	
	    if(normaly > normalx ) yuv1[0][x+(y*width)] = normaly;
	    if(normalx > normaly) yuv1[0][x+(y*width)] = normalx;
	}
    }
}
*/

/* calculate the median of a neighbour hood, the median is the middle
   value of a shell sorted list of numbers. */

void median_sort(int numbers[], int n) {
  int i,j,increment=3,temp;
  while (increment > 0) {
    for(i=0; i < n; i++) {
      j=i;
      temp = numbers[i];
      while(( j>= increment) && (numbers[j-increment]>temp)) {
	numbers[j] = numbers[j - increment];
	j = j - increment;
      }
      numbers[j] = temp;
  }
  if(increment >> 1 != 0) {
	 increment = increment >> 1;
  }
  else {
	 if(increment == 1) increment = 0; else increment = 1;
  }
 }
}

void median_apply(uint8_t *yuv1[3], int width, int height, int val)
{
    unsigned int r,c=0;
    unsigned int len = (width * height) - width;
    unsigned int twidth = width - 1;
    int median[9];

    /* first pixel */
    median[0] = yuv1[0][0];
    median[1] = yuv1[0][1];
    median[2] = yuv1[0][width];
    median[3] = yuv1[0][width+1];
    median_sort(median,4);
    yuv1[0][0] = median[2];

    /* first row */
    for(r=1; r < twidth; r++) {
	median[0] = yuv1[0][r - 1];
	median[1] = yuv1[0][r];
	median[2] = yuv1[0][r + 1];
	median[3] = yuv1[0][r + width - 1];
	median[4] = yuv1[0][r + width];
	median[5] = yuv1[0][r + width + 1];
	median_sort(median,6);
	yuv1[0][r] = median[3];
    }

    /* first large window */
    for(r=width; r < len; r+=width ) {
	
	/* first pixel in row */
	median[0] = yuv1[0][r + c - width];
	median[1] = yuv1[0][r + c - width + 1];
	median[2] = yuv1[0][r + c];
	median[3] = yuv1[0][r + c + 1];
	median[4] = yuv1[0][r + c + width];
	median[5] = yuv1[0][r + c + width + 1];	
	median_sort(median, 6);
	yuv1[0][r+c] = median[3];        

	/* row */
	for(c=1; c < twidth; c++) {
	   median[0] = yuv1[0][ r + c - width - 1 ];	
	   median[1] = yuv1[0][ r + c - width ];
	   median[2] = yuv1[0][ r + c - width + 1];
	   median[3] = yuv1[0][ r + c - 1];
	   median[4] = yuv1[0][ r + c ];
	   median[5] = yuv1[0][ r + c + 1];
	   median[6] = yuv1[0][ r + c + width - 1];
	   median[7] = yuv1[0][ r + c + width ];
	   median[8] = yuv1[0][ r + c + width + 1];
	   median_sort( median, 9);
	   yuv1[0][r + c] = median[4];
	}
	/* last pixel in row */
	median[0] = yuv1[0][ r - width -1 ];
	median[1] = yuv1[0][ r - width ];
	median[2] = yuv1[0][ r + width -1 ];
	median[3] = yuv1[0][ r + width];
	median[4] = yuv1[0][ r + width + width -1];
	median[5] = yuv1[0][ r + width + width];
	median_sort(median, 6);
	yuv1[0][r + width] = median[3];
	
     }

     /* not doing last row yet , too lazy */
}
void softblur_apply(uint8_t * yuv1[3], int width, int height, int type,
		    int n)
{
    switch (type) {
    case 0:
	softblur1_apply(yuv1, width, height);
	break;
  //  case 1:
//	median_apply(yuv1,width,height,n);
	break;
    case 1:
	softblur2_apply(yuv1, width, height, n);
	break;
    case 2:
       softblur3_apply(yuv1,width,height,n);
	break;
    }
}


void softblur_free(){}
