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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "chromascratcher.h"
#include "chromamagick.h"

typedef struct {
    uint8_t *cframe[4];
    int cnframe;
    int cnreverse;
    int chroma_restart;
    VJFrame _tmp;
} chromascratcher_t;

vj_effect *chromascratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = (MAX_SCRATCH_FRAMES-1); /* uses the chromamagick effect for scratchign */
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 29;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->defaults[0] = 1;
    ve->defaults[1] = 150;
    ve->defaults[2] = 8;
    ve->defaults[3] = 1;
    ve->description = "Matte Scratcher";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user =0;
	ve->param_description = vje_build_param_list(ve->num_params, "Length", "Value", "Mode", "Pingpong" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][2],2,
		"Appearing", "Dissapearing","Appearing suppressed", "Dissappearing suppressed",	
		"Add Subselect Luma", "Select Min", "Select Max", "Select Difference",
		"Select Difference Negate", "Add Luma", "Select Unfreeze", "Exclusive",
		"Difference Negate", "Additive", "Basecolor", "Freeze", "Unfreeze",
		"Hardlight", "Multiply", "Divide", "Subtract", "Add", "Screen",
		"Difference", "Softlight", "Dodge", "Reflect", "Difference Replace",
		"Darken", "Lighten", "Modulo Add" 
	);
    
	return ve;
}

void *chromascratcher_malloc(int w, int h)
{
    chromascratcher_t *c = (chromascratcher_t*) vj_calloc(sizeof(chromascratcher_t));
    if(!c) {
        return NULL;
    }

    c->cframe[0] = (uint8_t *) vj_malloc( w * h * 3 * MAX_SCRATCH_FRAMES * sizeof(uint8_t) );
    if(!c->cframe[0]) {
        free(c);
        return NULL;
    }

    c->cframe[1] = c->cframe[0] + ( w * h * MAX_SCRATCH_FRAMES );
    c->cframe[2] = c->cframe[1] + ( w * h * MAX_SCRATCH_FRAMES );


    int strides[4] = { w * h * MAX_SCRATCH_FRAMES, w * h * MAX_SCRATCH_FRAMES, w * h * MAX_SCRATCH_FRAMES, 0 };
    
    vj_frame_clear( c->cframe, strides, 128 );

    return (void*) c;
}

void chromascratcher_free(void *ptr) {

    chromascratcher_t *c = (chromascratcher_t*) ptr;
    free(c->cframe[0]);
    free(c);
}

static void chromastore_frame(chromascratcher_t *c, VJFrame *src, int w, int h, int n, int no_reverse)
{
	int strides[4] = { (w * h), (w*h), (w*h) , 0 };
    int cnframe = c->cnframe;
    uint8_t **cframe = c->cframe;
    int cnreverse = c->cnreverse;

	uint8_t *dest[4] = {
		cframe[0] + (w*h*cnframe),
		cframe[1] + (w*h*cnframe),
		cframe[2] + (w*h*cnframe),
       		NULL	};

	if (!cnreverse) {
		vj_frame_copy( src->data, dest, strides ); 
    } else {
		vj_frame_copy( dest, src->data, strides );
   	}

	if (cnreverse)
		cnframe--;
	else
		cnframe++;

	if (cnframe >= n) {
		if (no_reverse == 0) {
		    cnreverse = 1;
		    cnframe = n - 1;
			if(cnframe < 0 )
				cnframe = 0;
		} else {
		    cnframe = 0;
		}
    	}

   	if (cnframe == 0)
		cnreverse = 0;

    c->cnreverse = cnreverse;
    c->cnframe = cnframe;

}



void chromascratcher_apply(void *ptr, VJFrame *frame, int *args) {
    int mode = args[0];
    int opacity = args[1];
    int n = args[2];
    int no_reverse = args[3];

    chromascratcher_t *c = (chromascratcher_t*) ptr;

    int cnframe = c->cnframe;
    uint8_t **cframe = c->cframe;
    int chroma_restart = c->chroma_restart;

    unsigned int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    const int len = frame->len;
    const unsigned int op_a = (opacity > 255) ? 255 : opacity;
    const unsigned int op_b = 255 - op_a;
    const int offset = len * cnframe;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    VJFrame _tmp;
    veejay_memcpy( &_tmp, frame, (sizeof(VJFrame)));
	_tmp.data[0] = cframe[0];
	_tmp.data[1] = cframe[1];
	_tmp.data[2] = cframe[2];

    if(no_reverse != chroma_restart)
    {
		chroma_restart = no_reverse;
		cnframe = n;
    }

    if( cnframe == 0 ) {
	_tmp.data[0] = frame->data[0];
	_tmp.data[1] = frame->data[1];
	_tmp.data[2] = frame->data[2];
    }

    if(mode>3) {
	   int matte_mode = mode - 3;
       int ch_args[2] = { matte_mode, opacity };
   	   chromamagick_apply( NULL,frame,&_tmp,ch_args);
    }
    else {
	    switch (mode) {		/* scratching with a sequence of frames (no scene changes) */

		case 0:
			/* moving parts will dissapear over time */
			for (i = 0; i < len; i++) {
			    if (cframe[0][offset + i] < Y[i]) {
					Y[i] = cframe[0][offset + i];
					Cb[i] = cframe[1][offset + i];
					Cr[i] = cframe[2][offset + i];
			    }
			}
			break;
   		 case 1:
			for (i = 0; i < len; i++) {
			    /* moving parts will remain visible */
			    if (cframe[0][offset + i] > Y[i]) {
					Y[i] = cframe[0][offset + i];
					Cb[i] = cframe[1][offset + i];
					Cr[i] = cframe[2][offset + i];
		    		}
			}
		break;
  	  case 2:
		for (i = 0; i < len; i++) {
		    if ((cframe[0][offset + i] * op_a) < (Y[i] * op_b)) {
				Y[i] = cframe[0][offset + i];
				Cb[i] = cframe[1][offset + i];
					Cr[i] = cframe[2][offset + i];
	 	   }
		}
		break;
	   case 3:
		for (i = 0; i < len; i++) {
		    /* moving parts will remain visible */
		    if ((cframe[0][offset + i] * op_a) > (Y[i] * op_b)) {
			Y[i] = cframe[0][offset + i];
			Cb[i] = cframe[1][offset + i];
			Cr[i] = cframe[2][offset + i];
		    }
		}
		break;
    
		}
	}

	chromastore_frame(c, frame, width, height, n, no_reverse);
    c->chroma_restart = chroma_restart;
}
