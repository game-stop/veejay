/* 
 * EffecTV - Realtime Digital Video Effector
 * RadioacTV - motion-enlightment effect.
 * I referred to "DUNE!" by QuoVadis for this effect.
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * Veejay FX 'RadioActiveVJ'
 * (C) 2007 Niels Elburg
 *   This effect was ported from EffecTV.
 *   Differences:
 *    - difference frame over 2 frame interval intsead of bg substraction
 *    - several mask methods
 *    - more parameters
 *    - no palette (but mixing source)
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
#include "radioactive.h"
#include "softblur.h"
#include <libavutil/avutil.h>
#include <libyuv/yuvconv.h>
#include "common.h"
#include <stdlib.h>

vj_effect *radioactivetv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  /* methods */
    ve->limits[1][0] = 6;
    ve->limits[0][1] = 50;// zoom ratio
    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0; // strength 
    ve->limits[1][2] = 255; 
    ve->limits[0][3] = 0; //diff threhsold
    ve->limits[1][3] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 95;
    ve->defaults[2] = 200;
    ve->defaults[3] = 30;
    ve->description = "RadioActive EffecTV";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Zoom ratio", "Strength", "Difference Threshold" );
    return ve;
}


static  uint8_t *diffbuf = NULL;
static	uint8_t *blurzoombuf = NULL;
static	int	*blurzoomx = NULL;
static  int	*blurzoomy = NULL;
static	int	buf_width_blocks = 0;
static	int	buf_width = 0;
static  int	buf_height = 0;
static  int	buf_area = 0;
static  int	buf_margin_left = 0;
static  int	buf_margin_right = 0;
static	int	first_frame=0;
static	int	last_mode=0;
static	float	ratio_ = 0.95;

#define VIDEO_HWIDTH (buf_width/2)
#define VIDEO_HHEIGHT (buf_height/2)


/* this table assumes that video_width is times of 32 */
static void setTable(void)
{
	unsigned int bits;
	int x, y, tx, ty, xx;
	int ptr, prevptr;

	prevptr = (int)(0.5+ratio_*(-VIDEO_HWIDTH)+VIDEO_HWIDTH);
	for(xx=0; xx<(buf_width_blocks); xx++){
		bits = 0;
		for(x=0; x<32; x++){
			ptr= (int)(0.5+ratio_*(xx*32+x-VIDEO_HWIDTH)+VIDEO_HWIDTH);
			bits = bits>>1;
			if(ptr != prevptr)
				bits |= 0x80000000;
			prevptr = ptr;
		}
		blurzoomx[xx] = bits;
	}

	ty = (int)(0.5+ratio_*(-VIDEO_HHEIGHT)+VIDEO_HHEIGHT);
	tx = (int)(0.5+ratio_*(-VIDEO_HWIDTH)+VIDEO_HWIDTH);
	xx=(int)(0.5+ratio_*(buf_width-1-VIDEO_HWIDTH)+VIDEO_HWIDTH);
	blurzoomy[0] = ty * buf_width + tx;
	prevptr = ty * buf_width + xx;
	for(y=1; y<buf_height; y++){
		ty = (int)(0.5+ratio_*(y-VIDEO_HHEIGHT)+VIDEO_HHEIGHT);
		blurzoomy[y] = ty * buf_width + tx - prevptr;
		prevptr = ty * buf_width + xx;
	}
}		
static void kentaro_blur(void)
{
	int x, y;
	int width;
	unsigned char *p, *q;
	unsigned char v;
	
	width = buf_width;
	p = blurzoombuf + width + 1;
	q = p + buf_area;

	for(y=buf_height-2; y>0; y--) {
		for(x=width-2; x>0; x--) {
			v = (*(p-width) + *(p-1) + *(p+1) + *(p+width))/4 - 1;
			if(v == 255) v = 0;
			*q = v;
			p++;
			q++;
		}
		p += 2;
		q += 2;
	}
}
static void zoom(void)
{
	int b, x, y;
	unsigned char *p, *q;
	int blocks, height;
	int dx;

	p = blurzoombuf + buf_area;
	q = blurzoombuf;
	height = buf_height;
	blocks = buf_width_blocks;

	for(y=0; y<height; y++) {
		p += blurzoomy[y];
		for(b=0; b<blocks; b++) {
			dx = blurzoomx[b];
			for(x=0; x<32; x++) {
				p += (dx & 1);
				*q++ = *p;
				dx = dx>>1;
			}
		}
	}
}
void blurzoomcore(void)
{
	kentaro_blur();
	zoom();
}

int	radioactivetv_malloc(int w, int h)
{
	buf_width_blocks = (w / 32 );
	if( buf_width_blocks > 255 )
	{
		return 0;
	}
	buf_width = buf_width_blocks * 32;
	buf_height = h;

	buf_area = buf_width * buf_height;
	buf_margin_left = (w - buf_width ) >> 1;
	buf_margin_right = (w - buf_width - buf_margin_left);
	
	blurzoombuf = (uint8_t*) vj_calloc( RUP8(buf_area * 2 ));
	if(!blurzoombuf)
		return 0;
	
	blurzoomx = (int*) vj_calloc( RUP8(buf_width * sizeof(int)));
	blurzoomy = (int*) vj_calloc( RUP8(buf_width * sizeof(int)));

	if( blurzoomx == NULL || blurzoomy == NULL )
	{
		if(blurzoombuf) free(blurzoombuf);
		return 0;
	}

	diffbuf   = (uint8_t*) vj_malloc( RUP8((4*w) + 2 * w * h * sizeof(uint8_t)));

	setTable();

	first_frame = 0;
	last_mode  = 0;
	ratio_	    = 0.95;
	return 1;
}

void	radioactivetv_free()
{
	if(blurzoombuf)
		free(blurzoombuf);
	blurzoombuf = NULL;
	if(blurzoomx ) free(blurzoomx);
	blurzoomx = NULL;
	if(blurzoomy ) free(blurzoomy);
	blurzoomy = NULL;

	if(diffbuf) free(diffbuf);
	diffbuf = NULL;

}
void radioactivetv_apply( VJFrame *frame, VJFrame *blue, int width, int height,
		int mode, int snapRatio, int snapInterval, int threshold)
{
	unsigned int x, y;
	uint8_t *diff = diffbuf;
	uint8_t *prev = diff + frame->len;
	const int len = frame->len;
	uint8_t *lum = frame->data[0];
	uint8_t *dstY = lum;
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];
	uint8_t *p;
	uint8_t *blueY = blue->data[0];
	uint8_t *blueU = blue->data[1];
	uint8_t *blueV = blue->data[2];
	float new_ratio = ratio_;
	VJFrame smooth;
	veejay_memcpy( &smooth, frame, sizeof(VJFrame));
	smooth.data[0] = prev;

	//@ set new zoom ratio 
	new_ratio = (snapRatio * 0.01);
	if ( ratio_ != new_ratio )
	{
		ratio_ = new_ratio;
		setTable();
	}

	if( !first_frame )
	{	//@ take current
		veejay_memcpy( prev, lum, len );
		softblur_apply( &smooth, width,height,0);
		first_frame++;
		return;
	}
	if( last_mode != mode )
	{
//@ mode changed, reset
		veejay_memset( blurzoombuf, 0, 2*buf_area);
		veejay_memset( diff, 0, len );
		last_mode = mode;
	}

	uint8_t *d = diff;

//@ varying diff methods (strobe, normal, average, etc)
	switch( mode )
	{
		case 0:
			for( y = 0; y < len; y ++ ){
				diff[y] = abs(lum[y] - prev[y]);
				if(diff[y] < threshold )
					diff[y] = 0;	
				prev[y] = (prev[y] + lum[y])>>1;
			}
			break;
		case 1:
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs(lum[y] - prev[y]);
				if(diff[y] < threshold )
					diff[y] = 0;	
				prev[y] = lum[y];
			}
			break;
		case 2:
			for( y = 0; y < len; y ++ ){
				diff[y] = ( prev[y] >> 1 ) + lum[y] >> 1;
				if( diff[y] < threshold )
					diff[y] = 0;
				prev[y] = lum[y];
			}
			break;
		case 3:
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs( lum[y] - prev[y] );
				diff[y] = (prev[y] + lum[y] + lum[y] + lum[y])>>2;
				if( diff[y] < threshold )
					diff[y] = 0;
				prev[y] = diff[y];
			}
			break;
		case 4:
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs( lum[y] - prev[y] );
				diff[y] = (lum[y] - prev[y])>>1;
				if( diff[y] < threshold )
				{
					if(diff[y]) diff[y]--;
				}
				prev[y] = lum[y];
			}
			break;
		case 5:
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs(lum[y] - prev[y]);
				if(diff[y] < threshold )
					diff[y] = 0;	
				prev[y] = lum[y];
			}
			break;
		case 6:
			for( y = 0; y < len; y ++ ){
				if( abs( lum[y] - prev[y]) > threshold )
					diff[y] = lum[y]>>2;
				else
					diff[y] = 0;
				prev[y] = lum[y];
			}

			break;
	}
//@ end of diff


	p = blurzoombuf;
	d += buf_margin_left;
	for( y = 0; y < buf_height; y++ ) {
		for( x = 0; x< buf_width; x ++ ) {
			p[x] |= ( (d[x] * snapInterval)>>7);
		}
		d += width;
		p += buf_width;
	}
	//@ prepare frame for next difference take
	softblur_apply( &smooth, width,height,0);

	blurzoomcore();
	p = blurzoombuf;

	if(mode >= 3 )
	{
		veejay_memset( dstU,128,len);
		veejay_memset( dstV,128,len);
		veejay_memcpy( dstY, blurzoombuf, len );
		return;
	}

	
	uint32_t k =0;
	for( y = 0; y < height; y ++ )
	{
		k += buf_margin_left;
		for( x = 0; x  < buf_width; x ++ )
		{
			uint8_t op0 = (*p ++);
			uint8_t op1 = 0xff - op0;

			dstY[k] = (op0 * blueY[k] + op1 * dstY[k])>>8; 
			dstU[k] = (op0 * blueU[k] + op1 * dstU[k])>>8;
			dstV[k] = (op0 * blueV[k] + op1 * dstV[k])>>8;

			k ++;
		}
		k += buf_margin_right;
	}
}
