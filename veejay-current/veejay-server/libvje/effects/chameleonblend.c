/* 
 * Linux VeeJay
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * ChameleonTV - Vanishing into the wall!!
 * Copyright (C) 2003 FUKUCHI Kentaro
 *
 * Ported to veejay by Niels Elburg 
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
#include <veejaycore/vj-msg.h>
#include "softblur.h"
#include "chameleonblend.h"
#include "motionmap.h"

vj_effect *chameleonblend_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;

	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 1;
	ve->defaults[0] = 0;
	ve->description = "ChameleonMixTV (EffectTV)";
	ve->sub_format = 1;
    ve->static_bg = 1;
	ve->extra_frame = 1;
	ve->has_user = 0;
	ve->motion = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Appearing/Dissapearing");
	return ve;
}

typedef struct {
    int last_mode_;
    int N__;
    int n__;
    void *motionmap;
    int has_bg;
    int32_t *sum;
    uint8_t *timebuffer;
    int plane;
    uint8_t	*bgimage[4];
	int n_threads;
} chameleonblend_t;

#define PLANES_DEPTH 6
#define	PLANES (1<< PLANES_DEPTH)

int	chameleonblend_prepare( void *ptr, VJFrame *frame ) 
{
    chameleonblend_t *c = (chameleonblend_t*) ptr;

	int strides[4] = { frame->len, frame->len, frame->len, 0  };
	vj_frame_copy( frame->data, c->bgimage, strides );	
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = c->bgimage[0];
	tmp.width = frame->width;
	tmp.height = frame->height;
    tmp.len = frame->len;
	//@ 3x3 blur

	softblur_apply_internal( &tmp );

	veejay_msg(2, "ChameleonTV: Snapped background frame");
	return 1;
}

void *chameleonblend_malloc(int w, int h)
{
    int i;
    chameleonblend_t *c = (chameleonblend_t*) vj_calloc(sizeof(chameleonblend_t));
    if(!c) {
        return NULL;
    }

    const int len = (w*h);
    const int safe_zone = (w*2);
    c->bgimage[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * (len + safe_zone) * 3 );
    if(!c->bgimage[0]) {
        free(c);
        return NULL;
    }

    c->bgimage[1] = c->bgimage[0] + len + safe_zone;
    c->bgimage[2] = c->bgimage[1] + len + safe_zone;

	vj_frame_clear1( c->bgimage[0], pixel_Y_lo_, len + safe_zone);
    
	for( i = 1; i < 3; i ++ ) {
		vj_frame_clear1( c->bgimage[i], 128, len + safe_zone);
	}
	
	c->sum = (int32_t*) vj_calloc( len * sizeof(int32_t));
    if(!c->sum) {
        free(c->bgimage[0]);
        free(c);
        return NULL;
    }

	c->timebuffer = (uint8_t*) vj_calloc( len * PLANES );
    if(!c->timebuffer) {
        free(c->bgimage[0]);
        free(c->sum);
        free(c);
        return NULL;
    }

	c->n_threads = vje_advise_num_threads(len);
	c->last_mode_ = -1;

	return (void*) c;
}

void	chameleonblend_free(void *ptr)
{
    chameleonblend_t *c = (chameleonblend_t*) ptr;

    free(c->bgimage[0]);
    free(c->timebuffer);
    free(c->sum);
    free(c);
}

static void drawAppearing(chameleonblend_t *cb, VJFrame *src, VJFrame *dest)
{
    const int video_area = src->len;
    
    uint8_t *restrict p_buf = cb->timebuffer + (cb->plane * video_area);
    int32_t *restrict s_buf = cb->sum;

    uint8_t *restrict bgY = cb->bgimage[0];
    uint8_t *restrict bgU = cb->bgimage[1];
    uint8_t *restrict bgV = cb->bgimage[2];

    uint8_t *restrict srcY = src->data[0];
    uint8_t *restrict srcU = src->data[1];
    uint8_t *restrict srcV = src->data[2];

    uint8_t *restrict dstY = dest->data[0];
    uint8_t *restrict dstU = dest->data[1];
    uint8_t *restrict dstV = dest->data[2];

    #pragma omp parallel for simd num_threads(cb->n_threads) schedule(static)
    for(int i = 0; i < video_area; i++)
    {
        const int Y_curr = srcY[i];

        int current_sum = s_buf[i] - p_buf[i] + Y_curr;
        s_buf[i] = current_sum;
        p_buf[i] = (uint8_t)Y_curr;

        int diff = (Y_curr << PLANES_DEPTH) - current_sum;
        if (diff < 0) diff = -diff;
        
        int alpha_calc = (diff << 3) >> PLANES_DEPTH;
        uint8_t alpha = (alpha_calc > 255) ? 255 : (uint8_t)alpha_calc;

		dstY[i] = (uint8_t)(srcY[i] + (((bgY[i] - srcY[i]) * alpha) >> 8));
		dstU[i] = (uint8_t)(srcU[i] + (((bgU[i] - srcU[i]) * alpha) >> 8));
		dstV[i] = (uint8_t)(srcV[i] + (((bgV[i] - srcV[i]) * alpha) >> 8));
    }

    cb->plane = (cb->plane + 1) & (PLANES - 1);
}

static void drawDisappearing(chameleonblend_t *cb, VJFrame *src, VJFrame *dest)
{
    const int video_area = src->len;
    
    uint8_t *restrict p_buf = cb->timebuffer + (cb->plane * video_area);
    int32_t *restrict s_buf = cb->sum;

    uint8_t *restrict bgY = cb->bgimage[0];
    uint8_t *restrict bgU = cb->bgimage[1];
    uint8_t *restrict bgV = cb->bgimage[2];

    uint8_t *restrict srcY = src->data[0];
    uint8_t *restrict srcU = src->data[1];
    uint8_t *restrict srcV = src->data[2];

    uint8_t *restrict dstY = dest->data[0];
    uint8_t *restrict dstU = dest->data[1];
    uint8_t *restrict dstV = dest->data[2];

    #pragma omp parallel for simd num_threads(cb->n_threads) schedule(static)
    for(int i = 0; i < video_area; i++)
    {
        const int Y_curr = srcY[i];
        
        int current_sum = s_buf[i] - p_buf[i] + Y_curr;
        s_buf[i] = current_sum;
        p_buf[i] = (uint8_t)Y_curr;

        int diff = (Y_curr << PLANES_DEPTH) - current_sum;
        if (diff < 0) diff = -diff;
        
        int alpha_calc = (diff << 3) >> PLANES_DEPTH;
        uint8_t alpha = (alpha_calc > 255) ? 255 : (uint8_t)alpha_calc;

		dstY[i] = (uint8_t)(bgY[i] + (((srcY[i] - bgY[i]) * alpha) >> 8));
		dstU[i] = (uint8_t)(bgU[i] + (((srcU[i] - bgU[i]) * alpha) >> 8));
		dstV[i] = (uint8_t)(bgV[i] + (((srcV[i] - bgV[i]) * alpha) >> 8));
        
    }

    cb->plane = (cb->plane + 1) & (PLANES - 1);
}

void chameleonblend_apply( void *ptr, VJFrame *frame,VJFrame *source, int *args ){
    int mode = args[0];
    chameleonblend_t *c = (chameleonblend_t*) ptr;

	uint32_t activity = 0;
	int auto_switch = 0;
	int tmp1,tmp2;

	if( motionmap_active(c->motionmap) )
	{
		motionmap_scale_to(c->motionmap, 32,32,1,1, &tmp1,&tmp2, &(c->n__), &(c->N__) );
		auto_switch = 1;
		activity = motionmap_activity(c->motionmap);
	}
	else
	{
		c->N__ = 0;
		c->n__ = 0;
	}

	if( c->n__ == c->N__ || c->n__ == 0 )
		auto_switch = 0;
	
	if(auto_switch)
	{
		if( activity <= 40 )
		{
			// into the wall
			drawDisappearing(c, source, frame );
		}
		else
		{
			// out of the wall
			drawAppearing(c, source, frame );
		}
	}
    else {

	    if( mode == 0 )
		    drawDisappearing(c, source, frame );
	    else
		    drawAppearing(c, source, frame );
    }

}

int chameleonblend_request_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void chameleonblend_set_motionmap(void *ptr, void *priv) {
    chameleonblend_t *c = (chameleonblend_t*) ptr;
    c->motionmap = priv;
}

