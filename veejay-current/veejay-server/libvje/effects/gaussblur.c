/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include "gaussblur.h"

vj_effect *gaussblur_init(int w,int h)
{
	vj_effect *ve;
	ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

	ve->defaults[0] = 100;	/* Radius */
	ve->defaults[1] = 100;	/* Strength */
	ve->defaults[2] = 300;	/* Quality */

	ve->limits[0][0] = 1;
	ve->limits[1][0] = 500;

	ve->limits[0][1] = -100;
	ve->limits[1][1] = 100;

	ve->limits[0][2] = 0;
	ve->limits[1][2] = 300;

	ve->param_description = vje_build_param_list(ve->num_params, 
			"Radius", "Strength", "Quality" );

	ve->has_user = 0;
	ve->description = "Alpha: Choke Matte";
	ve->extra_frame = 0;
	ve->sub_format = -1;
	ve->rgb_conv = 0;
	ve->parallel = 0;

	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;

	return ve;
}

extern int yuv_sws_get_cpu_flags();

typedef struct {
	float			   radius;
	float			   strength;
	float			   quality;
	struct SwsContext *filter_context;
} FilterParam;

static int alloc_sws_context(FilterParam *f, int width, int height, unsigned int flags)
{
	SwsVector *vec;
	SwsFilter sws_filter;

	vec = sws_getGaussianVec(f->radius, f->quality);

	if (!vec)
		return 0;

	sws_scaleVec(vec, f->strength);
	vec->coeff[vec->length / 2] += 1.0 - f->strength;
	sws_filter.lumH = sws_filter.lumV = vec;
	sws_filter.chrH = sws_filter.chrV = NULL;
	f->filter_context = sws_getCachedContext(NULL,
											 width, height, AV_PIX_FMT_GRAY8,
											 width, height, AV_PIX_FMT_GRAY8,
											 flags, &sws_filter, NULL, NULL);

	sws_freeVec(vec);

	if (!f->filter_context)
		return 0;

	return 1;
}

static uint8_t *temp = NULL;
static FilterParam *gaussfilter = NULL;
static int last_radius = 0;
static int last_strength = 0;
static int last_quality = 0;

int	gaussblur_malloc(int w, int h)
{
	gaussfilter = (FilterParam*) vj_calloc(sizeof(FilterParam));
	temp = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w*h));
	if(temp == NULL)
		return 0;
	last_radius = 0;
	last_strength = 0;
	last_quality = 0;
	return 1;
}

void gaussblur_free()
{
	if(temp) {
		free(temp);
		temp = NULL;
	}

	if( gaussfilter->filter_context ) {
		sws_freeContext( gaussfilter->filter_context );
		gaussfilter->filter_context = NULL;
	}

	if( gaussfilter ) {
		free(gaussfilter);
		gaussfilter = NULL;
	}
}

static int	 gaussfilter_init(int w, int h, int radius, int strength, int quality)
{
	gaussfilter->radius = (float) radius * 0.01f;
	gaussfilter->strength = (float) strength * 0.01f;
	gaussfilter->quality = (float) quality * 0.01f;

	if(!alloc_sws_context( gaussfilter,w,h,yuv_sws_get_cpu_flags() ) )
		return 0;

	return 1;
}

static void gaussblur(uint8_t *dst, const int dst_linesize,const uint8_t *src, const int src_linesize,
				 const int w, const int h,struct SwsContext *filter_context)
{
	const uint8_t* const src_array[4] = {src,0,0,0};
	uint8_t *dst_array[4]			  = {dst,0,0,0};
	int src_linesize_array[4] = {src_linesize,0,0,0};
	int dst_linesize_array[4] = {dst_linesize,0,0,0};

	sws_scale(filter_context, src_array, src_linesize_array,
			  0, h, dst_array, dst_linesize_array);
}

void gaussblur_apply(VJFrame *frame, int radius, int strength, int quality )
{
	uint8_t *A = frame->data[3];
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	if( last_radius != radius || last_strength != strength || last_quality != quality )
	{
		if( gaussfilter->filter_context ) {
			sws_freeContext( gaussfilter->filter_context );
		}
		if( gaussfilter_init( width, height, radius, strength, quality ) == 0 )
			return;

		last_radius = radius;
		last_strength = strength;
		last_quality = quality;
	}


	veejay_memcpy( temp, A, len );
	gaussblur( A, width, temp, width, width, height, gaussfilter->filter_context );

}
