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
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/yuvconv.h>
#include "colorhis.h"

vj_effect *colorhis_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->defaults[0] = 0; // r only, g only, b only, rgb
    ve->defaults[1] = 0; // draw
    ve->defaults[2] = 200; // intensity
    ve->defaults[3] = 132; // strength
	ve->param_description = vje_build_param_list ( ve->num_params, "Mode (R,G,B,All)", "Draw","Intensity", "Strength" );
    ve->description = "Color Histogram";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;

	ve->hints = vje_init_value_hint_list( ve->num_params );
	
	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		"Red Channel", "Green Channel", "Blue Channel", "All Channels"
	);	
    return ve;
}

typedef struct {
    void	*histogram_;
    VJFrame *rgb_frame_;
    uint8_t *rgb_;
    void *convert_yuv;
    void *convert_rgb;
} colorhis_t;

void *colorhis_malloc(int w, int h)
{
    colorhis_t *c = (colorhis_t*) vj_calloc(sizeof(colorhis_t));
    if(!c) {
        return NULL;
    }
	c->histogram_ = veejay_histogram_new();
    if(!c->histogram_) {
        free(c);
        return NULL;
    }

	c->rgb_ = vj_malloc(sizeof(uint8_t) * RUP8(w * h * 3) );
    if(!c->rgb_) {
        veejay_histogram_del(c->histogram_);
        free(c);
        return NULL;
    }
	
    c->rgb_frame_ = yuv_rgb_template( c->rgb_, w, h, AV_PIX_FMT_RGB24 );
    if(!c->rgb_frame_) {
        veejay_histogram_del(c->histogram_);
        free(c->rgb_);
        free(c);
        return NULL;
    }

	return (void*) c;
}

void	colorhis_free(void *ptr)
{
    colorhis_t *c = (colorhis_t*) ptr;
	veejay_histogram_del(c->histogram_);
	free(c->rgb_);
	free(c->rgb_frame_);
    if(c->convert_yuv)
		yuv_fx_context_destroy( c->convert_yuv );
	if(c->convert_rgb )
		yuv_fx_context_destroy( c->convert_rgb );
    free(c);
}


void colorhis_apply( void *ptr, VJFrame *frame, int *args) {
    int mode = args[0];
    int val = args[1];
    int intensity = args[2];
    int strength = args[3];

    colorhis_t *c = (colorhis_t*) ptr;

	if(!c->convert_yuv)
		c->convert_yuv = yuv_fx_context_create( frame, c->rgb_frame_ );

	yuv_fx_context_process( c->convert_yuv, frame, c->rgb_frame_ );

	if( val == 0 )
	{
		veejay_histogram_draw_rgb( c->histogram_, frame, c->rgb_, intensity, strength, mode );
	}
	else
	{
		veejay_histogram_analyze_rgb( c->histogram_,c->rgb_, frame );
		veejay_histogram_equalize_rgb( c->histogram_, frame, c->rgb_, intensity, strength, mode );

		if(!c->convert_rgb )
			c->convert_rgb = yuv_fx_context_create( c->rgb_frame_, frame);
		yuv_fx_context_process( c->convert_rgb, c->rgb_frame_, frame );
	}
}

