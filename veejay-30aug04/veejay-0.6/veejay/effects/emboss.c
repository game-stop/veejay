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

#include "emboss.h"
#include <stdlib.h>
#include <stdio.h>
vj_effect *emboss_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 10;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 10;
    ve->description = "Various Weird Effects";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}


void simpleedge_yuvdata(uint8_t * yuv[3], int width, int height)
{
    unsigned int x, y;
    uint8_t a1, a2, a3, b1, b2, b3, c1, c2, c3;
    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++) {
	    a1 = yuv[0][(y - 1) * width + (x - 1)];
	    a2 = yuv[0][(y - 1) * width + x];
	    a3 = yuv[0][(y - 1) * width + (x + 1)];
	    b1 = yuv[0][y * width + (x - 1)];
	    b2 = yuv[0][y * width + x];	/* center */
	    b3 = yuv[0][y * width + (x + 1)];
	    c1 = yuv[0][(y + 1) * width + (x - 1)];
	    c2 = yuv[0][(y + 1) * width + x];
	    c3 = yuv[0][(y + 1) * width + (x + 1)];
	    if (b2 > a1 && b2 > a2 && b2 > a3 &&
		b2 > b1 && b2 > b3 && b3 > c1 && b2 > c2 && b2 > c2)
		yuv[0][y * width + x] = 235;
	    else
		yuv[0][y * width + x] = 16;
	}
    }
}

/**********************************************************************************************
 *
 * xtreme_emboss: looks a bit like emboss, but with dark colours and distorted edges
 *
 **********************************************************************************************/
void xtreme_emboss_yuvdata(uint8_t * yuv[3], int width, int height)
{
    unsigned int r, c;
    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width; c++) {
	    yuv[0][c + r] = (yuv[0][r - 1 + c - 1] -
			     yuv[0][r - 1 + c] -
			     yuv[0][r - 1 + c + 1] +
			     yuv[0][r + c - 1] -
			     yuv[0][r + c] +
			     yuv[0][r + c + 1] +
			     yuv[0][r + 1 + c - 1] +
			     yuv[0][r + 1 + c] - yuv[0][r + 1 + c + 1]
		) / 9;
	}
    }
}

void another_try_edge(uint8_t *yuv[3], int w, int h) {
  uint8_t p;
  const unsigned int len=(w*h)-w;
  unsigned int r,c;
  for(r=w; r < len; r+= w) {
    for(c=1; c < w-1; c++) {
	p = ((yuv[0][r+c-w] * -1) + (yuv[0][r+c-w-1] * -1) +
	           (yuv[0][r+c-w+1] * -1) + (yuv[0][r+c-1] * -1) +
		   (yuv[0][r+c] * -8) + (yuv[0][r+c+1] * -1) +
		   (yuv[0][r+c+w] * -1) + (yuv[0][r+c+w-1] * -1) +
	           (yuv[0][r+c+w+1] * -1))/9;
	if(p>240) p = 240; else if ( p < 16 ) p = 16;
	yuv[0][r+c] = p;
    }
  }
}


/**********************************************************************************************
 *
 * lines_white_balanced_yuvdata: it looks cool, just try it. 
 *
 **********************************************************************************************/
void lines_white_balance_yuvdata(uint8_t * yuv[3], int width, int height)
{
    unsigned int r, c;
    const unsigned int len = (width * height);
    uint8_t val;
    for (r = 0; r < len; r += width) {
	for (c = 0; c < width; c++) {
	    val = (yuv[0][r - 1 + c - 1] -
		   yuv[0][r - 1 + c] -
		   yuv[0][r - 1 + c + 1] +
		   yuv[0][r + c - 1] -
		   yuv[0][r + c] +
		   yuv[0][r + c + 1] +
		   yuv[0][r + 1 + c - 1] -
		   yuv[0][r + 1 + c] - yuv[0][r + 1 + c + 1]
		) / 9;
	    if (val < 16 || val > 235)
		val = 235;
	    yuv[0][c + r] = val;
	}
    }
}

/**********************************************************************************************
 *
 * white_emboss_yuvdata: again, i cannot explain what it looks like.
 *
 **********************************************************************************************/
void white_emboss_yuvdata(uint8_t * yuv[3], int width, int height)
{
    unsigned int r, c;
    int val;
    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width; c++) {
	    val = (yuv[0][r - 1 + c - 1] -
		   yuv[0][r - 1 + c] -
		   yuv[0][r - 1 + c + 1] +
		   yuv[0][r + c - 1] +
		   yuv[0][r + c] -
		   yuv[0][r + c + 1] -
		   yuv[0][r + 1 + c - 1] -
		   yuv[0][r + 1 + c] - yuv[0][r + 1 + c + 1]
		) / 9;
	    if (val < 16 || val > 235)
		val = 235;
	    yuv[0][c + r] = val;

	}
    }
}

void emboss_test_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int a, b, c;
    int i;
    const int len = width * height;
    for (i = 0; i < len; i++) {
	a = yuv[0][i];
	b = (a + 235) >> 1;
	c = (b + 235) >> 1;
	yuv[0][i] = c;
    }
}

/**********************************************************************************************
 *
 * gray_emboss_yuvdata: similar as white_emboss_yuvdata, but image is more grayish
 *
 **********************************************************************************************/
void gray_emboss_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int r, c;
    uint8_t val;
    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width; c++) {
	    val = (yuv[0][r - 1 + c - 1] -
		   yuv[0][r - 1 + c] -
		   yuv[0][r - 1 + c + 1] +
		   yuv[0][r + c - 1] -
		   yuv[0][r + c] -
		   yuv[0][r + c + 1] -
		   yuv[0][r + 1 + c - 1] -
		   yuv[0][r + 1 + c] - yuv[0][r + 1 + c + 1]
		) / 9;
	    if (val < 16 || val > 235)
		val = 16;
	    yuv[0][r + c] = val;
	}
    }

}

/**********************************************************************************************
 *
 * aggressive_emboss_yuvdata: much like the above two, but more aggressive.
 *
 **********************************************************************************************/
void aggressive_emboss_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int r, c;
    uint8_t val;
    const int len = width * height;
    for (r = 0; r < len; r += width) {
	for (c = 0; c < width; c++) {
	    val = (yuv[0][r - 1 + c - 1] -
		   yuv[0][r - 1 + c] -
		   yuv[0][r - 1 + c + 1] +
		   yuv[0][r + c - 1] -
		   yuv[0][r + c] -
		   yuv[0][r + c + 1] -
		   yuv[0][r + 1 + c - 1] +
		   yuv[0][r + 1 + c] + yuv[0][r + 1 + c + 1]
		) / 9;
	    if (val < 16 || val > 235)
		val = 16;
	    yuv[0][c + r] = val;
	}
    }

}

/**********************************************************************************************
 *
 * dark_emboss_yuvdata: like the above, but much less light.
 *
 **********************************************************************************************/
void dark_emboss_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int r, c;

    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width; c++) {
	    yuv[0][c + r] = (yuv[0][r - 1 + c - 1] -
			     yuv[0][r - 1 + c] -
			     yuv[0][r - 1 + c + 1] +
			     yuv[0][r + c - 1] +
			     yuv[0][r + c] -
			     yuv[0][r + c + 1] -
			     yuv[0][r + 1 + c - 1] +
			     yuv[0][r + 1 + c] + yuv[0][r + 1 + c + 1]
		) / 9;
	}
    }

}

/**********************************************************************************************
 *
 * grayish_mood_yuvdata: less light, more gray, only overal colour changes.
 * Name probably does not reflect resulting effects.
 **********************************************************************************************/
void grayish_mood_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int r, c;

    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width; c++) {
	    yuv[0][c + r] = (yuv[0][r - 1 + c - 1] -
			     yuv[0][r - 1 + c] -
			     yuv[0][r - 1 + c + 1] -
			     yuv[0][r + c - 1] -
			     yuv[0][r + c] -
			     yuv[0][r + c + 1] -
			     yuv[0][r + 1 + c - 1] -
			     yuv[0][r + 1 + c] - yuv[0][r + 1 + c + 1]
		) / 9;
	}
    }


}

/**********************************************************************************************
 *
 * blur_dark_yuvdata: blurs the image a little and decreases luminance
 * 
 **********************************************************************************************/

void blur_dark_yuvdata(uint8_t * yuv[3], int width, int height)
{
    int r, c;
    int len = (width*height) - width;
	/* incomplete */
    for (r = width; r < len; r += width) {
	for (c = 1; c < width-1; c++) {
	    yuv[0][c + r] = (yuv[0][r - 1 + c - 1] -
			     yuv[0][r - 1 + c] -
			     yuv[0][r - 1 + c + 1] +
			     yuv[0][r + c - 1] +
			     yuv[0][r + c] +
			     yuv[0][r + c + 1] +
			     yuv[0][r + 1 + c - 1] -
			     yuv[0][r + 1 + c] + yuv[0][r + 1 + c + 1]
		) / 9;
	}
    }
}

void emboss_apply(uint8_t * yuv[3], int width, int height, int n)
{
    switch (n) {
    case 1:
	xtreme_emboss_yuvdata(yuv, width, height);
	break;
    case 2:
	lines_white_balance_yuvdata(yuv, width, height);
	break;
    case 3:
	white_emboss_yuvdata(yuv, width, height);
	break;
    case 4:
	gray_emboss_yuvdata(yuv, width, height);
	break;
    case 5:
	aggressive_emboss_yuvdata(yuv, width, height);
	break;
    case 6:
	dark_emboss_yuvdata(yuv, width, height);
	break;
    case 7:
	grayish_mood_yuvdata(yuv, width, height);
	break;
    case 8:
	simpleedge_yuvdata(yuv, width, height);
	break;
    case 9:
	emboss_test_yuvdata(yuv, width, height);
	break;
    case 10:
	another_try_edge(yuv,width,height);
	break;
    default:
	blur_dark_yuvdata(yuv, width, height);
	break;
    }
}
void emboss_free(){}
