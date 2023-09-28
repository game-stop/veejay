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
*/

#include "common.h"
#include <limits.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "softblur.h"
#include "opacity.h"
#include "motionmap.h"

#define HIS_DEFAULT 6
#define HIS_LEN (8*25)
vj_effect *motionmap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;    
    ve->limits[1][0] = 255; /* threshold */
    ve->limits[0][1] = 1;  
    ve->limits[1][1] = (w*h)/20;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = HIS_LEN;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = HIS_LEN;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = 3;
	ve->limits[0][7] = 0;
	ve->limits[1][7] = 25 * 60;
    ve->defaults[0] = 40;
    ve->defaults[1] = 1000;
    ve->defaults[2] = 1;
    ve->defaults[3] = HIS_DEFAULT;
    ve->defaults[4] = HIS_DEFAULT;
    ve->defaults[5] = 0;
    ve->defaults[6] = 0;
	ve->defaults[7] = 0;
    ve->description = "Motion Mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->global = 1;
    ve->n_out = 2;
    ve->static_bg = 1;
    ve->param_description = vje_build_param_list( ve->num_params, 
			"Difference Threshold", "Maximum Motion Energy","Draw Motion Map","History in frames" ,"Decay", "Interpolate frames", "Activity Mode", "Activity Decay");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][6], 6, "Normal", "Local Max", "Global Max", "Hold Last" );
	

    return ve;
}

typedef struct {
    int32_t histogram_[HIS_LEN];
    uint8_t *bg_image;
    uint8_t *binary_img;
    uint8_t *diff_img;
    uint8_t *prev_img;
    uint8_t *interpolate_buf;
    int32_t max;
    int32_t global_max;
    uint32_t nframe_;
    int current_his_len; // HIS_DEFAULT;
    int current_decay; // HIS_DEFAULT;
    uint32_t key1_;
    uint32_t key2_;
    uint32_t keyv_;
    uint32_t keyp_;
    int have_bg;
    int running;
    int is_initialized;
    int do_interpolation;
    int stored_frame;
    int scale_lock;
    int activity_decay;
    int last_act_decay;
} motionmap_t;

void *motionmap_malloc(int w, int h )
{
    motionmap_t *mm = (motionmap_t*) vj_calloc(sizeof(motionmap_t));
    if(!mm) {
        return NULL;
    }

	mm->bg_image = (uint8_t*) vj_malloc( sizeof(uint8_t) * (w * h));
    if(!mm->bg_image) {
        free(mm);
        return NULL;
    }

	mm->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h));
    if(!mm->binary_img) {
        free(mm->bg_image);
        free(mm);
        return NULL;
    }
    
	mm->prev_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w*h));
    if(!mm->prev_img) {
        free(mm->bg_image);
        free(mm->binary_img);
        free(mm);
        return NULL;
    }

	mm->interpolate_buf = vj_malloc( sizeof(uint8_t) * (w*h*3));
    if(!mm->interpolate_buf) {
        free(mm->bg_image);
        free(mm->binary_img);
        free(mm->prev_img);
        free(mm);
        return NULL;
    }
	mm->diff_img = (uint8_t*) vj_malloc( sizeof(uint8_t) * (w*h*2));
    if(!mm->diff_img) {
        free(mm->bg_image);
        free(mm->binary_img);
        free(mm->prev_img);
        free(mm->interpolate_buf);
        free(mm);
        return NULL;
    }

	veejay_msg(2, "This is 'Motion Mapping'");
	veejay_msg(2, "This FX calculates motion energy activity levels over a period of time to scale FX parameters");
	veejay_msg(2, "Add any of the following to the FX chain (if not already present)");
	veejay_msg(2, "\tBathroom Window, Displacement Mapping, Multi Mirrors, Magic Mirror, Sinoids");
	veejay_msg(2, "\tSlice Window , Smear, ChameleonTV and TimeDistort TV");

    mm->last_act_decay = -1;
	mm->is_initialized ++;

	return (void*) mm;
}

void	motionmap_free(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	if( mm->interpolate_buf )
		free(mm->interpolate_buf); 
	if( mm->bg_image )
		free(mm->bg_image);
	if( mm->binary_img )
		free(mm->binary_img);
	if( mm->prev_img )
		free(mm->prev_img);
	if( mm->diff_img )
		free(mm->diff_img);

	if( mm->is_initialized > 0 )
		mm->is_initialized --;

    free(mm);
}

uint8_t	*motionmap_interpolate_buffer(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->interpolate_buf;
}

uint8_t *motionmap_bgmap(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->binary_img;
}

int	motionmap_active(void *ptr)
{
    if( ptr == NULL )
        return 0;

    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->running;
}

int motionmap_is_locked(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->scale_lock;
}

uint32_t	motionmap_activity(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->keyv_;
}

int	motionmap_instances(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;
	return mm->is_initialized;
}

void	motionmap_scale_to( void *ptr, int p1max, int p2max, int p1min, int p2min, int *p1val, int *p2val, int *pos, int *len )
{
    motionmap_t *mm = (motionmap_t*) ptr;

	if( mm->global_max == 0 || mm->scale_lock )
		return;

	if( mm->keyv_ > mm->global_max )
	{
		mm->keyv_ = mm->global_max;
	}

	int n  = (mm->nframe_ % mm->current_decay) + 1;
	float q = 1.0f / (float) mm->current_decay * n;
	float diff = (float) mm->keyv_ - (float) mm->keyp_ ;
	float pu = mm->keyp_ + (q * diff);
	float m  = (float) mm->global_max;

	if( pu > m )
		pu = m;

	float w = 1.0 / mm->global_max;
	float pw = w * pu;

	*p1val = p1min + (int) ((p1max-p1min) * pw);
	*p2val = p2min + (int) ((p2max-p2min) * pw);
	*len = mm->current_decay;
	*pos = n;
}

void	motionmap_lerp_frame( void *ptr, VJFrame *cur, VJFrame *prev, int N, int n )
{
    motionmap_t *mm = (motionmap_t*) ptr;

	if( mm->stored_frame == 0 )
		return;

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

void	motionmap_store_frame( void *ptr, VJFrame *fx )
{
    motionmap_t *mm = (motionmap_t*) ptr;

	if( mm->running == 0  || !mm->do_interpolation)
		return;

	veejay_memcpy( mm->interpolate_buf, fx->data[0], fx->len );
	veejay_memcpy( mm->interpolate_buf + fx->len, fx->data[1], fx->len );
	veejay_memcpy( mm->interpolate_buf + fx->len + fx->len, fx->data[2], fx->len );

	mm->stored_frame = 1;
}

void	motionmap_interpolate_frame( void *ptr, VJFrame *fx, int N, int n )
{
    motionmap_t *mm = (motionmap_t*) ptr;

	if( mm->running == 0 || !mm->do_interpolation) 
		return;

	VJFrame prev;
	veejay_memcpy(&prev, fx, sizeof(VJFrame));
	prev.data[0] = mm->interpolate_buf;
	prev.data[1] = mm->interpolate_buf + (fx->len);
	prev.data[2] = mm->interpolate_buf + (2*fx->len);

	motionmap_lerp_frame( ptr, fx, &prev, N, n );
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
	unsigned int i;
	for( i = 0; i < len; i ++ ) {
		level += I[i];
	}
	return (level>>8);
}

static void motionmap_calc_diff( const uint8_t *bg, uint8_t *pimg, const uint8_t *img, uint8_t *pI1, uint8_t *pI2, uint8_t *bDst, const int len, const int threshold )
{
	unsigned int i;

	uint8_t *I1 = pI1;
	uint8_t *I2 = pI2;

	for( i = 0; i < len; i ++ ) 
	{
		I1[i] = abs( bg[i] - img[i] );
		if( I1[i] < threshold )
			I1[i] = 0;
		else
			I1[i] = 0xff;

		I2[i] = abs( bg[i] - pimg[i] );
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
		pimg[i] = img[i];
	}
}

int	motionmap_prepare( void *ptr, VJFrame *frame)
{
    int width = frame->width;
    int height = frame->height;
    uint8_t **map = frame->data;
    motionmap_t *mm = (motionmap_t*) ptr;
	if(!mm->is_initialized)
		return 0;

	vj_frame_copy1( map[0], mm->bg_image, width * height );
	motionmap_blur( mm->bg_image, width,height );
	veejay_memcpy( mm->prev_img, mm->bg_image, width * height );

	mm->have_bg = 1;
	mm->nframe_ = 0;
	mm->running = 0;
	mm->stored_frame = 0;
	mm->do_interpolation = 0;
	mm->scale_lock = 0;
	veejay_msg(2, "Motion Mapping: Snapped background frame");
	return 1;
}

void motionmap_apply( void *ptr, VJFrame *frame, int *args )
{
	unsigned int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    int threshold = args[0];
    int limit1 = args[1];
    int draw = args[2];
    int history = args[3];
    int decay = args[4];
    int interpol = args[5];
    int last_act_level = args[6];
    int act_decay = args[7];
    motionmap_t *mm = (motionmap_t*) ptr;
    const int limit = limit1 * 10;

	if(!mm->have_bg) {
		veejay_msg(VEEJAY_MSG_ERROR,"Motion Mapping: Snap the background frame with VIMS 339 or mask button in reloaded");
		return;
	}

	if( act_decay != mm->last_act_decay ) {
		mm->last_act_decay = act_decay;
		mm->activity_decay = act_decay;
	}

    motionmap_calc_diff( (const uint8_t*) mm->bg_image, mm->prev_img, (const uint8_t*) frame->data[0], mm->diff_img, mm->diff_img + len, mm->binary_img, len, threshold );

	if( draw )
	{
		vj_frame_clear1( Cb, 128, len );
		vj_frame_clear1( Cr, 128, len );
		vj_frame_copy1( mm->binary_img, frame->data[0], len );
		mm->running = 0;
		mm->stored_frame = 0;
		mm->scale_lock = 0;
		return;
	}

	int32_t activity_level = motionmap_activity_level( mm->binary_img, width, height );
	int32_t avg_actlvl = 0;
	int32_t min = INT_MAX;
	int32_t local_max = 0;

	mm->current_his_len = history;
	mm->current_decay = decay;

	mm->histogram_[ (mm->nframe_%mm->current_his_len) ] = activity_level;

	for( i = 0; i < mm->current_his_len; i ++ )
	{
		avg_actlvl += mm->histogram_[i];
		if(mm->histogram_[i] > mm->max ) mm->max = mm->histogram_[i];
		if(mm->histogram_[i] < min ) min = mm->histogram_[i];
		if(mm->histogram_[i] > local_max) local_max = mm->histogram_[i];
	}	

	avg_actlvl = avg_actlvl / mm->current_his_len;
	if( avg_actlvl < limit ) { 
		avg_actlvl = 0;
	}

	mm->nframe_ ++;

	switch( last_act_level ) {
		case 0:
			if( (mm->nframe_ % mm->current_his_len)==0 )
			{
				mm->key1_ = min;
				mm->key2_ = mm->max;
				mm->keyp_ = mm->keyv_;
				mm->keyv_ = avg_actlvl;
				mm->global_max = mm->max;
			}
			break;
		case 1:
			mm->key1_ = min;
			mm->key2_ = mm->max;
			mm->keyv_ = local_max;
			mm->global_max = local_max;
			break;
		case 2:
			mm->key1_ = min;
			mm->key2_ = mm->max;
			mm->keyp_ = mm->keyv_;
			mm->keyv_ = avg_actlvl;
			mm->global_max = mm->max;
			break;
		case 3:
			if( (mm->nframe_ % mm->current_his_len)==0 )
			{
				mm->key1_ = min;
				mm->key2_ = mm->max;
				mm->keyp_ = mm->keyv_;
				mm->keyv_ = avg_actlvl;
				mm->global_max = mm->max;
			}
			
			if( avg_actlvl == 0 )
				mm->scale_lock = 1;
			else 
				mm->scale_lock = 0;
	
			//reset to normal after "acitivity_decay"  ticks
			if( mm->scale_lock && act_decay > 0) {
				mm->activity_decay --;
				if( mm->activity_decay == 0 ) {
					mm->last_act_decay = 0;
					mm->scale_lock = 0;
				}
			}

			break;
	}

	mm->running = 1;
	mm->do_interpolation = interpol;
}
