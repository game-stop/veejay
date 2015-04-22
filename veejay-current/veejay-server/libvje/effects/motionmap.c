/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail.com>
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
	 p1 = Acitivity Maximum Level
         p2 = Draw difference frame (no processing)
         p3 = Ringbuffer length N
	 p4 = Opacity level

   This filter detects the amount of motion in a frame. It keeps an internal
   buffer to average (smoothen) the acitivity levels over N frames 
   At each step in N , a new value is linearly interpolated which is later 
   pulled by other FX to override their parameter values.
   To compensate for jumpy video, the frames n+1 to N are linearly interpolated
   from frame n+0 to frame N automatically.


 */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include "motionmap.h"
#include "common.h"
#include "softblur.h"
#include "opacity.h"

typedef int (*morph_func)(uint8_t *kernel, uint8_t mt[9] );
#define HIS_DEFAULT 15
#define HIS_LEN (8*25)
#define ACT_TOP 4000
#define MAXCAPBUF 55
vj_effect *motionmap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // motionmap
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 50;  // motion energy
    ve->limits[1][1] = 10000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][4] = 0; // buffer 
    ve->limits[1][4] = 255;
    ve->limits[0][3] = HIS_DEFAULT;
    ve->limits[1][3] = HIS_LEN;
    ve->defaults[0] = 40;
    ve->defaults[1] = ACT_TOP;
    ve->defaults[2] = 1;
    ve->defaults[3] = HIS_DEFAULT;
    ve->defaults[4] = 0;
    ve->description = "Motion Mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->n_out = 2;
	ve->param_description = vje_build_param_list( ve->num_params, "Difference Threshold", "Maximum Motion Energy","Draw Motion Map","History in frames" ,"Capture length");
    return ve;
}

static uint8_t *binary_img = NULL;
static uint8_t *original_img = NULL;
static uint8_t *previous_img = NULL;
static uint32_t histogram_[HIS_LEN];
static uint8_t *large_buf = NULL;
static uint32_t nframe_ =0;
static uint32_t max_d = ACT_TOP;
static int current_his_len = HIS_DEFAULT;
static	uint32_t	key1_ = 0, key2_ = 0, keyv_ = 0, keyp_ = 0;
static int have_bg = 0;
static int n_captured = 0;
static int n_played   = 0;
static int capturing  = 0;
static int playing    = 0;
static uint8_t *interpolate_buf = NULL;
static int running = 0;

int		motionmap_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h * 3) );
	original_img = binary_img + RUP8(w*h);
	previous_img = original_img + RUP8(w*h);
	large_buf = vj_malloc(sizeof(uint8_t) * RUP8(w*h*3) * (MAXCAPBUF+1));
	if(!large_buf)
	{
		veejay_msg(0, "Memory allocation error for Motion Mapping. Too large: %ld bytes",(long) ((RUP8(w*h*3)*(MAXCAPBUF+1))));
		return 0;
	}
	interpolate_buf = vj_malloc( sizeof(uint8_t) * RUP8(w*h*3));
	veejay_msg(2, "This is 'Motion Mapping'");
	veejay_msg(2, "This FX calculates motion energy activity levels over a period of time to scale FX parameters");
	veejay_msg(2, "Add any of the following to the FX chain (if not already present)");
	veejay_msg(2, "\tBathroom Window, Displacement Mapping, Multi Mirrors, Magic Mirror, Sinoids");
	veejay_msg(2, "\tSlice Window , Smear, ChameleonTV and TimeDistort TV");
	veejay_msg(2, "Using %2.2fMb for large buffer", (RUP8(w*h*3)*(MAXCAPBUF+1))/1048576.0f);
	veejay_memset( histogram_, 0, sizeof(uint32_t) * HIS_LEN );
	nframe_ = 0;
	running = 0;
	return 1;
}

void		motionmap_free(void)
{
	if(binary_img)
		free(binary_img);
	if(large_buf)
		free(large_buf);
	if( interpolate_buf )
		free(interpolate_buf);
	have_bg = 0;
	interpolate_buf = NULL;
	nframe_ = 0;
	running = 0;
	keyv_ = 0;
	keyp_ = 0;
	binary_img = NULL;
	previous_img = NULL;
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

static	void	update_bgmask( uint8_t *dst,uint8_t *in, uint8_t *src, int len, int threshold )
{
	int i;
	vje_diff_plane( in, src, dst, threshold, len );
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
	if(nframe_ <= 1)
		return;

	if( keyv_ > max_d )
	{
		keyv_ = max_d;
	}

	int n  = ((nframe_-1) % current_his_len)+1;
	
	float q = 1.0f / (float) current_his_len * n;
	float diff = (float) keyv_ - (float) keyp_ ;
	float pu = keyp_ + (q * diff);
	
	float m  = (float) max_d;

	if( pu > m )
		pu = m;
	

	float w = 1.0 / max_d;
	float pw = w * pu;

	*p1val = p1min + (int) ((p1max-p1min) * pw);
	*p2val = p2min + (int) ((p2max-p2min) * pw);
	*len = current_his_len;
	*pos = n;
	
	veejay_msg(VEEJAY_MSG_DEBUG, 
		"Change from [%d-%d] to %d [%d-%d] to %d in %d frames",
			p1min,p1max,*p1val,p2min,p2max,*p2val,current_his_len);

//	veejay_msg(0, "%s:%s p1=%d,p2=%d, len=%d,pos=%d",
//		__FILE__,__FUNCTION__,*p1val,*p2val, *len, *pos );

}

void	motionmap_lerp_frame( VJFrame *cur, VJFrame *prev, int N, int n )
{
	unsigned int i;
	int n1 = (( n-1) % N ) + 1;
	float frac = 1.0f / (float) N * n1;

	const int len = cur->len;
        uint8_t *Y0 = cur->data[0];
        uint8_t *Y1 = prev->data[0];
        uint8_t *U0 = cur->data[1];
        uint8_t *U1 = prev->data[1];
        uint8_t *V0 = cur->data[2];
        uint8_t *V1 = prev->data[2];

        for ( i = 0; i < len ; i ++ )
        {
                Y0[i] = Y1[i] + ( frac * (Y0[i] - Y1[i]));
                U0[i] = U1[i] + ( frac * (U0[i] - U1[i]));
                V0[i] = V1[i] + ( frac * (V0[i] - V1[i]));
        }
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

int	motionmap_prepare( uint8_t *map[4], int width, int height )
{
	if(!previous_img)
		return 0;
	vj_frame_copy1( map[0], previous_img, width * height );
	have_bg = 1;
	nframe_ = 0;
	running = 0;
	veejay_msg(2, "Motion Mapping: Snapped background frame");
	return 1;
}

static int stop_capture_ = 0;
static int reaction_ready_ = 0;
void motionmap_apply( VJFrame *frame, int width, int height, int threshold, int reverse, int draw, int history, int capbuf )
{
	unsigned int i,y;
	int len = (width * height);
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	if(!have_bg) {
		veejay_msg(VEEJAY_MSG_ERROR,"Motion Mapping: Snap the background frame with VIMS 339 or mask button in reloaded");
		return;
	}

	vj_frame_copy1( frame->data[0], original_img, len );

	softblur_apply( frame, width,height,0 );
	update_bgmask( binary_img, previous_img, frame->data[0], len , threshold);

	uint32_t sum = 0,min=0xffff,max=0;
	uint64_t activity_level1 = 0;
	uint64_t activity_level2 = 0;
	uint64_t activity_level3 = 0;
	uint64_t activity_level4 = 0;
	for( i = 0; i < len; i += 4 )
	{
		
		activity_level1 += binary_img[i];
		activity_level2 += binary_img[i+1];
		activity_level3 += binary_img[i+2];
		activity_level4 += binary_img[i+3];
	}
	uint32_t activity_level = ( (activity_level1>>8) + (activity_level2>>8) + (activity_level3>>8) + (activity_level4>>8));

	max_d = reverse;

	current_his_len = history;
	
	histogram_[ (nframe_%current_his_len) ] = activity_level;

	for( i = 0; i < current_his_len; i ++ )
	{
		sum += histogram_[i];
		if(histogram_[i] > max ) max = histogram_[i];
		if(histogram_[i] < min ) min = histogram_[i];
	}	
	if( (nframe_ % current_his_len)==0 )
	{
		key1_ = min;
		key2_ = max;
		keyp_ = keyv_;
		keyv_ = (sum > 0 ? (sum/current_his_len):0 );
	}

	if( draw )
	{
		vj_frame_clear1( Cb, 128, len );
		vj_frame_clear1( Cr, 128, len );
		vj_frame_copy1( binary_img, frame->data[0], len );
		nframe_++;
		return;
	}
	

	if( capbuf )
	{
		if(!capturing)
		{
			if( keyv_ > max_d )
			{
				if( !reaction_ready_ )
				{
					stop_capture_ = keyv_ / 5;
					capturing = 1;
				}
				else
				{
					playing = 1;
				}
			}
		}

		if( stop_capture_ && !reaction_ready_)
		{
			if( keyv_ < stop_capture_ || n_captured >= MAXCAPBUF )
			{
				capturing = 0;
				stop_capture_= 0;
				reaction_ready_ =1;
			}
		}
	}
	else
	{	
		capturing = 0; playing = 0; stop_capture_ = 0; reaction_ready_ = 0; n_captured = 0;
	}

	if( capturing )
	{
		uint8_t *dst[4];
		dst[0] = large_buf + ( n_captured * (len*3) ); 
		dst[1] = dst[0] + len;
		dst[2] = dst[1] + len;	
		dst[3] = NULL;
		int strides[4] = { len, len, len, 0 };
		vj_frame_copy( frame->data, dst, strides );
		n_captured ++;
		if( n_captured >= MAXCAPBUF )
		{
			capturing = 0;
			stop_capture_ = 0;
		}
	}
	else if (playing )
	{
		uint8_t *src[4];
		src[0] = large_buf + ( n_played * (len*3));
		src[1] = src[0]+ len;
		src[2] = src[1]+ len;
		src[3] = NULL;
	/*  veejay_memcpy( frame->data[0], src[0], len );
		veejay_memcpy( frame->data[1], src[1], len );
		veejay_memcpy( frame->data[2], src[2], len );*/

		VJFrame b;
		veejay_memcpy(&b, frame, sizeof(VJFrame));
		b.data[0] = src[0]; b.data[1] = src[1]; b.data[2] = src[2];
		opacity_applyN( frame, &b, frame->width,frame->height, capbuf );
		

		n_played ++;
		if( n_played >= n_captured)
		{
			n_played = 0; n_captured = 0; playing = 0; capturing = 0;
			reaction_ready_ = 0;
		}
		nframe_++;
		return;
	}
	vj_frame_copy1( original_img, frame->data[0], len );
	nframe_ ++;
	running = 1;
}
