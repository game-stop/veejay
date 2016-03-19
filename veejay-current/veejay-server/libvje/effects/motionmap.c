/* 
 * Linux VeeJay
 *
 * Copyright(C)2007-2016 Niels Elburg <nwelburg@gmail.com>
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

   This is a quite simple motion detection filter

   There are 5 parameters:
	 p0 = Threshold 
	 p1 = Motion Energy Threshold
     p2 = Draw difference frame (no processing)
     p3 = Motion Energy Histogram
	 p4 = Decay
*/

/*
 * This FX relies on gcc's auto vectorization.
 * To use the plain C version, define NO_AUTOVECTORIZATION
 */

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include "motionmap.h"
#include "common.h"
#include "softblur.h"
#include "opacity.h"
#include <veejay/vj-task.h>

#define HIS_DEFAULT 6
#define HIS_LEN (8*25)
vj_effect *motionmap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;    
    ve->limits[1][0] = 255; /* threshold */
    ve->limits[0][1] = 1;  
    ve->limits[1][1] = (w*h)/2;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = HIS_LEN;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = HIS_LEN;
    ve->defaults[0] = 40;
    ve->defaults[1] = 1000;
    ve->defaults[2] = 1;
    ve->defaults[3] = HIS_DEFAULT;
    ve->defaults[4] = HIS_DEFAULT;
    ve->description = "Motion Mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->n_out = 2;
	ve->param_description = vje_build_param_list( ve->num_params, "Difference Threshold", "Maximum Motion Energy","Draw Motion Map","History in frames" ,"Decay");
    return ve;
}

static int32_t histogram_[HIS_LEN];

static uint8_t *bg_image = NULL;
static uint8_t *binary_img = NULL;
static uint8_t *diff_img = NULL;
static uint8_t *prev_img = NULL;
static uint8_t *interpolate_buf = NULL;

static int32_t max = 0;
static uint32_t nframe_ =0;
static int current_his_len = HIS_DEFAULT;
static int current_decay = HIS_DEFAULT;
static uint32_t	key1_ = 0, key2_ = 0, keyv_ = 0, keyp_ = 0;
static int have_bg = 0;
static int running = 0;
static int is_initialized = 0;

int		motionmap_malloc(int w, int h )
{
	bg_image = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w * h));
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h)); 
	prev_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w*h));
	interpolate_buf = vj_malloc( sizeof(uint8_t) * RUP8(w*h*3));
	diff_img = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w*h*2));

	veejay_msg(2, "This is 'Motion Mapping'");
	veejay_msg(2, "This FX calculates motion energy activity levels over a period of time to scale FX parameters");
	veejay_msg(2, "Add any of the following to the FX chain (if not already present)");
	veejay_msg(2, "\tBathroom Window, Displacement Mapping, Multi Mirrors, Magic Mirror, Sinoids");
	veejay_msg(2, "\tSlice Window , Smear, ChameleonTV and TimeDistort TV");
	veejay_memset( histogram_, 0, sizeof(int32_t) * HIS_LEN );
	nframe_ = 0;
	running = 0;

	is_initialized ++;

	return 1;
}

void		motionmap_free(void)
{
	if( interpolate_buf )
		free(interpolate_buf); 
	if( bg_image )
		free(bg_image);
	if( binary_img )
		free(binary_img);
	if( prev_img )
		free(prev_img);
	if( diff_img )
		free(diff_img);

	if( is_initialized > 0 )
		is_initialized --;

	have_bg = 0;
	interpolate_buf = NULL;
	nframe_ = 0;
	running = 0;
	keyv_ = 0;
	keyp_ = 0;
	binary_img = NULL;
	prev_img = NULL;
}

uint8_t	*motionmap_interpolate_buffer()
{
	return interpolate_buf;
}

uint8_t *motionmap_bgmap()
{
	return binary_img;
}

int	motionmap_active()
{
	return running;
}

uint32_t	motionmap_activity()
{
	return keyv_;
}

void	motionmap_scale_to( int p1max, int p2max, int p1min, int p2min, int *p1val, int *p2val, int *pos, int *len )
{
	if( keyv_ > max )
	{
		keyv_ = max;
	}

	int n  = (nframe_ % current_decay) + 1;
	float q = 1.0f / (float) current_decay * n;
	float diff = (float) keyv_ - (float) keyp_ ;
	float pu = keyp_ + (q * diff);
	float m  = (float) max;

	if( pu > m )
		pu = m;

	float w = 1.0 / max;
	float pw = w * pu;

	*p1val = p1min + (int) ((p1max-p1min) * pw);
	*p2val = p2min + (int) ((p2max-p2min) * pw);
	*len = current_decay;
	*pos = n;
}

void	motionmap_lerp_frame( VJFrame *cur, VJFrame *prev, int N, int n )
{
	unsigned int i;
	const int n1 = (( n-1) % N ) + 1;
	const float frac = 1.0f / (float) N * n1;

	const int len = cur->len;
    uint8_t *__restrict__ Y0 = cur->data[0];
    const uint8_t *__restrict__ Y1 = prev->data[0];
    uint8_t *__restrict__ U0 = cur->data[1];
    const uint8_t *__restrict__ U1 = prev->data[1];
    uint8_t *__restrict__ V0 = cur->data[2];
    const uint8_t *__restrict__ V1 = prev->data[2];

#ifndef NO_AUTOVECTORIZATION
    for ( i = 0; i < len ; i ++ ) {
    	Y0[i] = Y1[i] + ( frac * (Y0[i] - Y1[i]));
	}

	for( i = 0; i < len; i ++ ) {
        U0[i] = U1[i] + ( frac * (U0[i] - U1[i]));
	}
	
  	for( i = 0; i < len; i ++ ) {	
		V0[i] = V1[i] + ( frac * (V0[i] - V1[i]));
    }
#else
 	for ( i = 0; i < len ; i ++ ) {
    	Y0[i] = Y1[i] + ( frac * (Y0[i] - Y1[i]));
        U0[i] = U1[i] + ( frac * (U0[i] - U1[i]));
		V0[i] = V1[i] + ( frac * (V0[i] - V1[i]));
    }
#endif
}

void	motionmap_store_frame( VJFrame *fx )
{
	uint8_t *dest[4] = {
		interpolate_buf, interpolate_buf + fx->len, interpolate_buf + fx->len + fx->len,NULL };
	int strides[4] = { fx->len, fx->len, fx->len, 0 };
	vj_frame_copy( fx->data, dest, strides );
}

void	motionmap_interpolate_frame( VJFrame *fx, int N, int n )
{
	if( n == 0 || N == 0 )
		return;

	VJFrame prev;
    veejay_memcpy(&prev, fx, sizeof(VJFrame));
    prev.data[0] = interpolate_buf;
    prev.data[1] = interpolate_buf + (fx->len);
    prev.data[2] = interpolate_buf + (2*fx->len);

	motionmap_lerp_frame( fx, &prev, N, n );
}

static void motionmap_blur( uint8_t *Y, int width, int height )
{
    const unsigned int len = (width * height);
	int r,c;
    for (r = 0; r < len; r += width) {
		for (c = 1; c < width-1; c++) {
	    	Y[c + r] = (Y[r + c - 1] + Y[r + c] + Y[r + c + 1] ) / 3;
		}
    }
}

static int32_t motionmap_activity_level( uint8_t *I, int width, int height )
{
	const unsigned int len = (width * height);
	int32_t level = 0;
	int r,c;
   	for (r = 0; r < len; r += width) {
		for ( c = 0; c < width; c ++ ) {
			level += I[r + c];
		}
	}
	return (level>>8);
}

void motionmap_calc_diff( const uint8_t *bg, uint8_t *prev_img, const uint8_t *img, uint8_t *pI1, uint8_t *pI2, uint8_t *bDst, const int len, const int threshold )
{
	unsigned int i;
	uint8_t p1,p2;

#ifndef NO_AUTOVECTORIZATION
	uint8_t *I1 = pI1;
	uint8_t *I2 = pI2;

	for( i = 0; i < len; i ++ ) 
	{
		I1[i] = abs( bg[i] - img[i] );
		if( I1[i] < threshold )
			I1[i] = 0;
		else
			I1[i] = 0xff;

		I2[i] = abs( bg[i] - prev_img[i] );
		if( I2[i] < threshold )
			I2[i] = 0; 
		else
			I2[i] = 0xff;

		I1[i] = abs( I1[i] - I2[i] );
		I2[i] = bDst[i] >> 1;
	}

	for( i = 0; i < len; i ++ ) 
	{
		bDst[i] = I1[i] + I2[i];
		prev_img[i] = img[i];
	}
#else
	for( i = 0; i < len; i ++ ) 
	{
		uint8_t q1 = 0, q2 = 0;
		p1 = abs( bg[i] - img[i] );
		if( p1 > threshold ) {
			q1 = 1;
		}

		p2 = abs( bg[i] - prev_img[i] );
		if( p2 > threshold ) {
			q2 = 1;
		}

		if( (!q1 && q2) || (!q2 && q1) ) {
			bDst[i] = 0xff;
		}
		else {
			bDst[i] = (bDst[i] >> 1); //@ decay
		}

		prev_img[i] = img[i]; 
	}
#endif
}

void motionmap_find_diff_job( void *arg )
{
	vj_task_arg_t *t = (vj_task_arg_t*) arg;

	const uint8_t *t_bg = t->input[0];
	const uint8_t *t_img = t->input[1];
	uint8_t *t_prev_img = t->input[2];
	uint8_t *t_binary_img = t->output[0];
	uint8_t *t_diff1 = t->output[1];
	uint8_t *t_diff2 = t->output[2];

	const int len = t->strides[0];
	const int threshold = t->iparams[0];

	motionmap_calc_diff( t_bg, t_prev_img, t_img,t_diff1,t_diff2, t_binary_img, len, threshold );
}

int	motionmap_prepare( uint8_t *map[4], int width, int height )
{
	if(!is_initialized)
			return 0;

	vj_frame_copy1( map[0], bg_image, width * height );
	motionmap_blur( bg_image, width,height );
	veejay_memcpy( prev_img, bg_image, width * height );

	have_bg = 1;
	nframe_ = 0;
	running = 0;
	veejay_msg(2, "Motion Mapping: Snapped background frame");
	return 1;
}

void motionmap_apply( VJFrame *frame, int width, int height, int threshold, int limit, int draw, int history, int decay )
{
	unsigned int i;
	const unsigned int len = (width * height);
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	if(!have_bg) {
		veejay_msg(VEEJAY_MSG_ERROR,"Motion Mapping: Snap the background frame with VIMS 339 or mask button in reloaded");
		return;
	}

	// run difference algorithm over multiple threads
	if( vj_task_available() ) {
		VJFrame task;
		task.stride[0] = len;		   // plane length 
		task.stride[1] = len;
		task.stride[2] = len;
		task.stride[3] = 0;
		task.data[0] = bg_image;       // plane 0 = background image 
		task.data[1] = frame->data[0]; // plane 1 = luminance channel 
		task.data[2] = prev_img;       // plane 2 = luminance channel of previous frame
		task.data[3] = NULL;
		task.ssm = 1;                  // all planes are the same size 
		task.format = frame->format;   // not important, but cannot be 0
		task.shift_v = 0;
		task.shift_h = 0;
		task.uv_width = width;
		task.uv_height = height;
		task.width = width;            // dimension
		task.height = height;

		uint8_t *dst[4] = { binary_img, diff_img, diff_img + RUP8(len), NULL };

		vj_task_set_from_frame( &task );
		vj_task_set_param( threshold, 0 );

		vj_task_run( task.data, dst, NULL,NULL,3, (performer_job_routine) &motionmap_find_diff_job );
	}
	else { 
		motionmap_calc_diff( (const uint8_t*) bg_image, prev_img, (const uint8_t*) frame->data[0], diff_img, diff_img + RUP8(len), binary_img, len, threshold );
	}

	if( draw )
	{
		vj_frame_clear1( Cb, 128, len );
		vj_frame_clear1( Cr, 128, len );
		vj_frame_copy1( binary_img, frame->data[0], len );
		running = 0;
		return;
	}

	int32_t activity_level = motionmap_activity_level( binary_img, width, height );
	int32_t avg_actlvl = 0;
	int32_t min = INT_MAX;

	current_his_len = history;
	current_decay = decay;

	histogram_[ (nframe_%current_his_len) ] = activity_level;

	for( i = 0; i < current_his_len; i ++ )
	{
		avg_actlvl += histogram_[i];
		if(histogram_[i] > max ) max = histogram_[i];
		if(histogram_[i] < min ) min = histogram_[i];
	}	

	avg_actlvl = avg_actlvl / current_his_len;

	if( avg_actlvl < limit ) 
		avg_actlvl = 0;

	nframe_ ++;

	if( (nframe_ % current_his_len)==0 )
	{
		key1_ = min;
		key2_ = max;
		keyp_ = keyv_;
		keyv_ = avg_actlvl;
	}

	running = 1;
}
