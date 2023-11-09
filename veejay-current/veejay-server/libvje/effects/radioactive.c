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

#include "common.h"
#include <veejaycore/vjmem.h>
//#include <libavutil/avutil.h>
#include <veejaycore/yuvconv.h>
#include "softblur.h"
#include "radioactive.h"

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

	
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
			"Average", "Normal", "Strobe", "Spill (greyscale)", "Flood (greyscale)", "Frontal (greyscale)", "Low (greyscale)" ); 

    return ve;
}

typedef struct {
    uint8_t *diffbuf;
	uint8_t *blurzoombuf;
	int	*blurzoomx;
    int	*blurzoomy;
    int	buf_width_blocks;
	int	buf_width;
    int	buf_height;
    int	buf_area;
    int	buf_margin_left;
    int	buf_margin_right;
    int	first_frame;
    int	last_mode; // -1;
	float ratio_; // 0.95;
} radioactive_t;

#define VIDEO_HWIDTH (buf_width/2)
#define VIDEO_HHEIGHT (buf_height/2)


/* this table assumes that video_width is multiple of 32 */
static void setTable(radioactive_t *r)
{
	unsigned int bits;
	int x, y, tx, ty, xx;
	int ptr, prevptr;
    int *blurzoomx = r->blurzoomx;
    int *blurzoomy = r->blurzoomy;
    int buf_width_blocks = r->buf_width_blocks;
    float ratio_ = r->ratio_;
    int buf_width = r->buf_width;
    int buf_height = r->buf_height;

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

static void kentaro_blur(radioactive_t *r)
{
	int x, y;
	int width;
	unsigned char *p, *q;
	unsigned char v;
	
	width = r->buf_width;
	p = r->blurzoombuf + width + 1;
	q = p + r->buf_area;

	for(y=r->buf_height-2; y>0; y--) {
#pragma omp simd
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

static void zoom(radioactive_t *r)
{
	int b, x, y;
	unsigned char *p, *q;
	int blocks, height;
	int dx;
    int *blurzoomy = r->blurzoomy;
    int *blurzoomx = r->blurzoomx;
	p = r->blurzoombuf + r->buf_area;
	q = r->blurzoombuf;
	height = r->buf_height;
	blocks = r->buf_width_blocks;

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

static void blurzoomcore(radioactive_t *r)
{
	kentaro_blur(r);
	zoom(r);
}

void *radioactivetv_malloc(int w, int h)
{
    if( (w/32) > 255 )
        return NULL;

    radioactive_t *r = (radioactive_t*) vj_calloc(sizeof(radioactive_t));
    if(!r) {
        return NULL;
    }

    r->ratio_ = 0.95f;
    r->last_mode = -1;

	r->buf_width_blocks = (w / 32 );
	r->buf_width = r->buf_width_blocks * 32;
	r->buf_height = h;

	r->buf_area = r->buf_width * r->buf_height;
	r->buf_margin_left = (w - r->buf_width ) >> 1;
	r->buf_margin_right = (w - r->buf_width - r->buf_margin_left);
	
	r->blurzoombuf = (uint8_t*) vj_calloc( (r->buf_area * 2 ));
	if(!r->blurzoombuf) {
        radioactivetv_free(r);
		return NULL;
    }
	
	r->blurzoomx = (int*) vj_calloc( (r->buf_width * sizeof(int)));
    if(!r->blurzoomx) {
        radioactivetv_free(r);
        return NULL;
    }

	r->blurzoomy = (int*) vj_calloc( (r->buf_width * sizeof(int)));
    if(!r->blurzoomy) {
        radioactivetv_free(r);
        return NULL;
    }

	r->diffbuf   = (uint8_t*) vj_calloc( ((4*w) + 2 * w * h * sizeof(uint8_t)));
    if(!r->diffbuf) {
        radioactivetv_free(r);
        return NULL;
    }

	setTable(r);

	return (void*) r;
}

void	radioactivetv_free(void *ptr)
{
    radioactive_t *r = (radioactive_t*) ptr;
    if(r) {
	    if(r->blurzoombuf)
		    free(r->blurzoombuf);
	    if(r->blurzoomx )
            free(r->blurzoomx);
	    if(r->blurzoomy )
            free(r->blurzoomy);
	    if(r->diffbuf)
            free(r->diffbuf);
    }
}

void radioactivetv_apply( void *ptr, VJFrame *frame, VJFrame *blue, int *args ) {
    int mode = args[0];
    int snapRatio = args[1];
    int snapInterval = args[2];
    int threshold = args[3];

	unsigned int x, y;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

    radioactive_t *r = (radioactive_t*) ptr;

	uint8_t *diff = r->diffbuf;
	uint8_t *prev = diff + len;
	uint8_t *lum = frame->data[0];
	uint8_t *dstY = lum;
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];
	uint8_t *p;
	uint8_t *blueY = blue->data[0];
	uint8_t *blueU = blue->data[1];
	uint8_t *blueV = blue->data[2];
	float new_ratio = r->ratio_;

    int buf_width = r->buf_width;
    int buf_height = r->buf_height;
    int buf_margin_left = r->buf_margin_left;
    int buf_margin_right = r->buf_margin_right;

    VJFrame smooth;
	veejay_memcpy( &smooth, frame, sizeof(VJFrame));
	smooth.data[0] = prev;

	//@ set new zoom ratio 
	new_ratio = (snapRatio * 0.01);
	if ( r->ratio_ != new_ratio )
	{
		r->ratio_ = new_ratio;
		setTable(r);
	}

	if( !r->first_frame )
	{	//@ take current
		veejay_memcpy( prev, lum, len );
		softblur_apply_internal( &smooth, 0);
		r->first_frame++;
		return;
	}

	if( r->last_mode != mode )
	{
//@ mode changed, reset
		veejay_memset( r->blurzoombuf, 0, 2*r->buf_area);
		veejay_memset( diff, 0, len );
		veejay_memset( prev, 0, len );
		r->last_mode = mode;
	}

	uint8_t *d = diff;

//@ varying diff methods (strobe, normal, average, etc)
	switch( mode )
	{
		case 3:
		case 0:
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs( lum[y] - prev[y] );
				diff[y] = (prev[y] + lum[y] + lum[y] + lum[y])>>2;
				if( diff[y] < threshold )
					diff[y] = 0;
				prev[y] = diff[y];
			}
			break;
		case 4:
		case 1:
#pragma omp simd
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
		case 2:
#pragma omp simd
			for( y = 0; y < len; y ++ ) {
				diff[y] = abs(lum[y] - prev[y]);
				if(diff[y] < threshold )
					diff[y] = 0;
				prev[y] = lum[y];
			}
			break;
		case 6:
#pragma omp simd
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


	p = r->blurzoombuf;
	d += r->buf_margin_left;
	for( y = 0; y < buf_height; y++ ) {
#pragma omp simd
		for( x = 0; x< buf_width; x ++ ) {
			p[x] |= ( (d[x] * snapInterval)>>7);
		}
		d += width;
		p += buf_width;
	}
	//@ prepare frame for next difference take
	softblur_apply_internal( &smooth, 0);

	blurzoomcore(r);
	p = r->blurzoombuf;

	if(mode >= 3 )
	{
		veejay_memset( dstU,128,len);
		veejay_memset( dstV,128,len);
		veejay_memcpy( dstY, r->blurzoombuf, len );
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
