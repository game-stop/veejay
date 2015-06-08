/*
 * Copyright (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
// todo: clean up initialization (use function pointers!)   

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <libavutil/pixfmt.h>
#include "effects/fibdownscale.h"
#include "effects/magicoverlays.h"
#include "effects/negation.h"
#include "effects/negatechannel.h"
#include "effects/radcor.h"
#include "effects/opacity.h"
#include "effects/posterize.h"
#include "effects/killchroma.h"
#include "effects/mirrors.h"
#include "effects/mirrors2.h"
#include "effects/colormap.h"
#include "effects/dices.h"
#include "effects/emboss.h"
#include "effects/flip.h"
#include "effects/revtv.h"
#include "effects/softblur.h"
#include "effects/split.h"
#include "effects/widthmirror.h"
#include "effects/dither.h"
#include "effects/borders.h"
#include "effects/dummy.h"
#include "effects/frameborder.h"
#include "effects/rawman.h"
#include "effects/rawval.h"
#include "effects/solarize.h"
#include "effects/smuck.h"
#include "effects/transform.h"
#include "effects/coloradjust.h"
#include "effects/gamma.h"
#include "effects/rgbkey.h"
#include "effects/median.h"
#include "transitions/transblend.h"
#include "transitions/slidingdoor.h"
#include "transitions/fadecolor.h"
#include "transitions/transop.h"
#include "transitions/transline.h"
#include "transitions/transcarot.h"
#include "transitions/wipe.h"
#include "transitions/vbar.h"
#include "effects/diff.h"
#include "effects/bgsubtract.h"
//#include "effects/texmap.h"
#include "effects/contourextract.h"
#include "effects/autoeq.h"
#include "effects/colorhis.h"
#include "effects/diffimg.h"
#include "effects/whiteframe.h"
#include "effects/lumakey.h"
#include "effects/chromamagick.h"
#include "effects/lumablend.h"
#include "effects/magicoverlays.h"
#include "effects/lumamagick.h"
#include "transitions/fadecolorrgb.h"
#include "effects/reflection.h"
#include "effects/rotozoom.h"
#include "effects/scratcher.h"
#include "effects/colorshift.h"
#include "effects/opacitythreshold.h"
#include "effects/opacityadv.h"
#include "effects/iris.h"
#include "effects/rgbkeysmooth.h"
#include "effects/magicscratcher.h"
#include "effects/chromascratcher.h"
#include "effects/distort.h"
#include "effects/tracer.h"
#include "effects/mtracer.h"
#include "effects/dupmagic.h"
#include "effects/keyselect.h"
#include "effects/greyselect.h"
#include "effects/bwselect.h"
#include "effects/complexinvert.h"
#include "effects/complexthreshold.h"
#include "effects/complexsaturate.h"
#include "effects/complexsync.h"
#include "effects/isolate.h"
#include "transitions/3bar.h"
#include "effects/enhancemask.h"
#include "effects/noiseadd.h"
#include "effects/contrast.h"
#include "effects/motionblur.h"
#include "effects/sinoids.h"
#include "effects/average.h"
#include "effects/ripple.h"
#include "effects/rippletv.h"
#include "effects/waterrippletv.h"
#include "effects/bathroom.h"
#include "effects/slicer.h"
#include "effects/timedistort.h"
#include "effects/chameleon.h"
#include "effects/baltantv.h"
#include "effects/radioactive.h"
#include "effects/chameleonblend.h"
#include "effects/slice.h"
#include "effects/zoom.h"
#include "effects/deinterlace.h"
#include "effects/mask.h"
#include "effects/crosspixel.h"
#include "effects/color.h"
#include "effects/noisepencil.h"
#include "effects/pencilsketch.h"
#include "effects/magicmirror.h"
#include "effects/lumamask.h" 
#include "effects/smear.h"
#include "effects/raster.h"
#include "effects/fisheye.h"
#include "effects/swirl.h"
#include "effects/radialblur.h"
#include "effects/binaryoverlays.h"
#include "effects/chromium.h"
#include "effects/chromapalette.h"
#include "effects/uvcorrect.h"
#include "effects/dissolve.h"
#include "effects/overclock.h"
#include "effects/cartonize.h"
#include "effects/nervous.h"
#include "effects/morphology.h"
#include "effects/threshold.h"
#include "effects/motionmap.h"
#include "effects/colmorphology.h"
#include "effects/blob.h"
#include "effects/ghost.h"
#include "effects/boids.h"
#include "effects/tripplicity.h"
#include "effects/neighbours.h"
#include "effects/neighbours2.h"
#include "effects/neighbours3.h"
#include "effects/neighbours4.h"
#include "effects/neighbours5.h"
#include "effects/cutstop.h"
#include "effects/maskstop.h"
#include "effects/photoplay.h"
#include "effects/videoplay.h"
#include "effects/videowall.h"
#include "effects/flare.h"
#include "effects/constantblend.h"
#include "effects/colflash.h"
#include "effects/rgbchannel.h"
#include "effects/diffmap.h"
#include "effects/picinpic.h"
#include "effects/cali.h"
#include "effects/bgsubtract.h"
#include "effects/average-blend.h"
#include <libplugger/plugload.h>
#include <veejay/vims.h>

int  pixel_Y_hi_ = 235;
int  pixel_U_hi_ = 240;
int  pixel_Y_lo_ = 16;
int  pixel_U_lo_ = 16;

static uint8_t *vje_buffer = NULL;
static ssize_t vje_buflen = 0;

int	get_pixel_range_min_Y() {
	return pixel_Y_lo_;
}
int	get_pixel_range_min_UV() {
	return pixel_U_lo_;
}

void	set_pixel_range(uint8_t Yhi,uint8_t Uhi, uint8_t Ylo, uint8_t Ulo)
{
	pixel_Y_hi_ = Yhi;
	pixel_U_hi_ = Uhi;
	pixel_U_lo_ = Ylo;
	pixel_Y_lo_ = Ulo;
}

static struct
{
	int	(*mem_init)(int width, int height);
	void	(*free)(void);
	int effect_id;
} simple_effect_index[] = {
{ 	bathroom_malloc		,	bathroom_free		,VJ_IMAGE_EFFECT_BATHROOM	},
{ 	chromascratcher_malloc	,	chromascratcher_free	,VJ_IMAGE_EFFECT_CHROMASCRATCHER},
{	complexsync_malloc	,	complexsync_free	,VJ_VIDEO_EFFECT_COMPLEXSYNC	},
{	dices_malloc	 	,	dices_free		,VJ_IMAGE_EFFECT_DICES		},
{	colorhis_malloc,		colorhis_free,		VJ_IMAGE_EFFECT_COLORHIS	},
{	autoeq_malloc,			autoeq_free		,VJ_IMAGE_EFFECT_AUTOEQ		},
{	magicscratcher_malloc	,	magicscratcher_free	,VJ_IMAGE_EFFECT_MAGICSCRATCHER	},
{	lumamask_malloc		,	lumamask_free		,VJ_VIDEO_EFFECT_LUMAMASK	},
{	motionblur_malloc	, 	motionblur_free		,VJ_IMAGE_EFFECT_MOTIONBLUR	},
{ 	magicmirror_malloc	,	magicmirror_free	,VJ_IMAGE_EFFECT_MAGICMIRROR	},
{	mtracer_malloc		, 	mtracer_free		,VJ_VIDEO_EFFECT_MTRACER	},
{	noiseadd_malloc		,	noiseadd_free		,VJ_IMAGE_EFFECT_NOISEADD	},
{	noisepencil_malloc	,	noisepencil_free	,VJ_IMAGE_EFFECT_NOISEPENCIL	},
{	reflection_malloc	,	reflection_free		,VJ_IMAGE_EFFECT_REFLECTION	},
{	ripple_malloc		,	ripple_free		,VJ_IMAGE_EFFECT_RIPPLE		},
{	rotozoom_malloc		,	rotozoom_free		,VJ_IMAGE_EFFECT_ROTOZOOM	},
{	scratcher_malloc	,	scratcher_free		,VJ_IMAGE_EFFECT_SCRATCHER	},
{	sinoids_malloc		,	sinoids_free		,VJ_IMAGE_EFFECT_SINOIDS	},
{	slice_malloc		,	slice_free		,VJ_IMAGE_EFFECT_SLICE		},
{	split_malloc		,	split_free		,VJ_VIDEO_EFFECT_SPLIT		},
{	tracer_malloc		,	tracer_free		,VJ_VIDEO_EFFECT_TRACER		},
{	zoom_malloc		,	zoom_free		,VJ_IMAGE_EFFECT_ZOOM		},
{	crosspixel_malloc	,	crosspixel_free		,VJ_IMAGE_EFFECT_CROSSPIXEL	},
{	fisheye_malloc,			fisheye_free		,VJ_IMAGE_EFFECT_FISHEYE	},
{	swirl_malloc		,	swirl_free		,VJ_IMAGE_EFFECT_SWIRL		},
{       radialblur_malloc,		radialblur_free,	 VJ_IMAGE_EFFECT_RADIALBLUR	},
{	uvcorrect_malloc,		uvcorrect_free,		VJ_IMAGE_EFFECT_UVCORRECT	},
{	overclock_malloc, 		overclock_free,		VJ_IMAGE_EFFECT_OVERCLOCK	},
{	nervous_malloc,			nervous_free,		VJ_IMAGE_EFFECT_NERVOUS		},
{	morphology_malloc,		morphology_free,	VJ_IMAGE_EFFECT_MORPHOLOGY	},
{	differencemap_malloc,		differencemap_free,	VJ_VIDEO_EFFECT_EXTDIFF		},
{	threshold_malloc,		threshold_free,		VJ_VIDEO_EFFECT_EXTTHRESHOLD	},
{	motionmap_malloc,		motionmap_free,		VJ_IMAGE_EFFECT_MOTIONMAP	},
{	colmorphology_malloc,		colmorphology_free,	VJ_IMAGE_EFFECT_COLMORPH	},
{	blob_malloc,			blob_free,		VJ_IMAGE_EFFECT_VIDBLOB 	},
{	boids_malloc,			boids_free,		VJ_IMAGE_EFFECT_VIDBOIDS 	},
{	ghost_malloc,			ghost_free,		VJ_IMAGE_EFFECT_GHOST		},
{	neighbours_malloc,		neighbours_free,	VJ_IMAGE_EFFECT_NEIGHBOUR	},
{	neighbours2_malloc,		neighbours2_free,	VJ_IMAGE_EFFECT_NEIGHBOUR2	},
{	neighbours3_malloc,		neighbours3_free,	VJ_IMAGE_EFFECT_NEIGHBOUR3	},
{	neighbours4_malloc,		neighbours4_free,	VJ_IMAGE_EFFECT_NEIGHBOUR4	},
{	neighbours5_malloc,		neighbours5_free,	VJ_IMAGE_EFFECT_NEIGHBOUR5	},
{	cutstop_malloc,			cutstop_free,		VJ_IMAGE_EFFECT_CUTSTOP		},
{	maskstop_malloc,		maskstop_free,		VJ_IMAGE_EFFECT_MASKSTOP	},
{	photoplay_malloc,		photoplay_free,		VJ_IMAGE_EFFECT_PHOTOPLAY	},
{	videoplay_malloc,		videoplay_free,		VJ_VIDEO_EFFECT_VIDEOPLAY	},
{	videowall_malloc,		videowall_free,		VJ_VIDEO_EFFECT_VIDEOWALL	},
{	flare_malloc,			flare_free,		VJ_IMAGE_EFFECT_FLARE		},
{	rgbchannel_malloc,		rgbchannel_free,	VJ_IMAGE_EFFECT_RGBCHANNEL	},
{	timedistort_malloc,		timedistort_free,	VJ_IMAGE_EFFECT_TIMEDISTORT	},
{	chameleon_malloc,		chameleon_free,		VJ_IMAGE_EFFECT_CHAMELEON	},
{	chameleonblend_malloc,		chameleonblend_free,	VJ_VIDEO_EFFECT_CHAMBLEND	},
{	baltantv_malloc,		baltantv_free,		VJ_IMAGE_EFFECT_BALTANTV	},
{	radcor_malloc,			radcor_free,		VJ_IMAGE_EFFECT_LENSCORRECTION	},
{	radioactivetv_malloc,		radioactivetv_free,	VJ_VIDEO_EFFECT_RADIOACTIVE	},
{	waterrippletv_malloc,		waterrippletv_free,	VJ_IMAGE_EFFECT_RIPPLETV	},
{	bgsubtract_malloc,		bgsubtract_free,	VJ_IMAGE_EFFECT_BGSUBTRACT	},
{	slicer_malloc,			slicer_free,		VJ_VIDEO_EFFECT_SLICER		},
{	NULL			,	NULL			,0				},
};

// complex effects have a buffer per instance
static struct
{
	int 	(*mem_init)(void **d,int w, int h );
	void	(*free)(void *d);
	int effect_id;
} complex_effect_index[] = 
{
	{	diff_malloc,		diff_free,			VJ_VIDEO_EFFECT_DIFF },
//	{	texmap_malloc,		texmap_free,			VJ_VIDEO_EFFECT_TEXMAP },
	{	contourextract_malloc,  contourextract_free,		VJ_IMAGE_EFFECT_CONTOUR },
	{	cali_malloc,		cali_free,			VJ_IMAGE_EFFECT_CALI },
	{	picinpic_malloc,	picinpic_free,			VJ_VIDEO_EFFECT_PICINPIC },
	{	water_malloc,		water_free,			VJ_VIDEO_EFFECT_RIPPLETV },
	{	NULL,			NULL,				0		     },
};

vj_effect *vj_effects[FX_LIMIT];
int vj_effect_ready[FX_LIMIT];

static int max_width = 0;
static int max_height =0;

static	int	n_ext_plugs_ = 0;

int	rgb_parameter_conversion_type_ = 0;

static int _get_simple_effect( int effect_id)
{
	int i;
	for(i = 0; simple_effect_index[i].effect_id != 0 ; i++)
	{
		if( simple_effect_index[i].effect_id == effect_id ) return i;
	}
	return -1;
} 

static int _get_complex_effect( int effect_id)
{
	int i;
	for(i = 0; complex_effect_index[i].effect_id != 0 ; i++)
	{
		if( complex_effect_index[i].effect_id == effect_id ) return i;
	}
	return -1;
} 

static int _no_mem_required(int effect_id)
{
	if( effect_id >= VJ_EXT_EFFECT )
		return 0;
	if( _get_simple_effect(effect_id) == -1 && _get_complex_effect(effect_id) == -1 )
		return 1;
	return 0;
}

int	vj_effect_is_plugin( int effect_id )
{
	int seq = vj_effect_real_to_sequence(effect_id);
	if(seq >= MAX_EFFECTS && seq < (MAX_EFFECTS + n_ext_plugs_)) {
		return 1;
	}
	return 0;
}

int vj_effect_initialized(int effect_id, void *instance_ptr )
{
 
	int seq = vj_effect_real_to_sequence(effect_id);
	if( seq < 0 )
		return 0;
	
	if( seq >= MAX_EFFECTS && seq < (MAX_EFFECTS + n_ext_plugs_)) {
		//@ is plugin
		if( instance_ptr == NULL ) {
			return 0;
		}
		return 1;
	} else if( seq < MAX_EFFECTS ) { //@ veejay internal FX
		if( _no_mem_required(effect_id) || vj_effect_ready[seq] == 1 ) {
			return 1;
		}
	}
	return 0;
}

static void 	*vj_effect_activate_ext( int fx_id, int *result )
{
	if( fx_id > (MAX_EFFECTS + n_ext_plugs_) ) {
		return NULL;
	}
	
	void *plug = plug_activate( fx_id - MAX_EFFECTS );
	if(plug)
	{
		*result = 1;
		return plug;
	} 
	*result = 0;
	
	return NULL;
}


int vj_effect_is_parallel(int effect_id)
{
	int seq = vj_effect_real_to_sequence(effect_id);
	return vj_effects[seq]->parallel;
}
void *vj_effect_activate(int effect_id, int *result)
{
	int seq = vj_effect_real_to_sequence(effect_id);

	if( seq < 0 || seq > (MAX_EFFECTS + n_ext_plugs_ )) {
		*result = 0;
		return NULL;
	}

	// activate some plugin instance
	if(seq >= MAX_EFFECTS && seq < (MAX_EFFECTS + n_ext_plugs_)) {
		return vj_effect_activate_ext(seq, result);
	}


	if( _no_mem_required(effect_id) ) {
		*result = 1;
		return NULL;
	}

	if( vj_effect_ready[seq] == 1 )
	{
	//	veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s already initialized",
		//	vj_effects[seq]->description);
		*result = 1;
		return NULL;
	}

	if( vj_effect_ready[seq] == 0 )
	{
		int index = _get_simple_effect(effect_id);
		if(index==-1)
		{
			index = _get_complex_effect(effect_id);
			if(index == -1)
			{
				*result = 0;
				return NULL;
			}
			if(!complex_effect_index[index].mem_init( &(vj_effects[seq]->user_data), max_width, max_height ))
			{
				*result = 0;
				return NULL;	
			}
			else
			{
		//		veejay_msg(VEEJAY_MSG_DEBUG, "Initialized complex effects %s",
			//			vj_effects[seq]->description);
				vj_effect_ready[seq] = 1;
				*result = 1;
				return NULL;
			}
			//perhaps it is a complex effect
		}
		if(!simple_effect_index[index].mem_init( max_width, max_height ))
		{
				*result = 0;
				return NULL;
		}
		else
		{
		//		veejay_msg(VEEJAY_MSG_DEBUG, "Initialized simple effect %s",
		//				vj_effects[seq]->description);
				vj_effect_ready[seq]= 1;
				*result = 1;
				return NULL;
		}
	}
	*result = 1;
	return NULL;
}

void	*vj_effect_get_data( int seq_id ) {

	return vj_effects[seq_id]->user_data;
}

int vj_effect_deactivate(int effect_id, void *ptr)
{
	int seq = vj_effect_real_to_sequence(effect_id);

	if(seq < 0 || seq >= MAX_EFFECTS)
		if( seq > n_ext_plugs_ + MAX_EFFECTS) { 
			return 0;
		}
	
	if( seq >= MAX_EFFECTS && seq < (n_ext_plugs_ + MAX_EFFECTS))
	{
		if(ptr) 
		{
			plug_deactivate( ptr );
			return 1;
		}
	} else if ( seq < MAX_EFFECTS ) {
		if( vj_effect_ready[seq] == 1 ) {
			int index = _get_simple_effect(effect_id);
			if(index==-1) {
				index = _get_complex_effect(effect_id);
				if(index == -1)
				{
					return 0;
				}
				complex_effect_index[index].free( vj_effects[seq]->user_data );
				vj_effect_ready[seq] = 0;
		//		veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated complex effect %s",	vj_effects[seq]->description);
				return 1;
			}
			simple_effect_index[index].free();
			vj_effect_ready[seq] = 0;
		//	veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated simple effect %s", vj_effects[seq]->description);
			return 1;
		}
	}

	return 0;
}

void vj_effect_deactivate_all()
{
	int i;
	for(i = 0 ; i < MAX_EFFECTS + n_ext_plugs_; i++)
	{
		int effect_id = vj_effect_get_real_id( i );
		if( effect_id > 100)
		{
			vj_effect_deactivate( effect_id, NULL );
		}
	}	
}

void vj_effect_initialize(int width, int height, int full_range)
{
    int i = VJ_VIDEO_COUNT;
    int k;

    if( full_range )
    {
	    set_pixel_range( 255, 255,0,0 );
    }

	if( (width % 32) != 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING,"Video width should be a multiple of 32 for some effects" );
	}

	veejay_memset( vj_effects, 0, sizeof(vj_effects));
	veejay_memset( vj_effect_ready,0,sizeof(vj_effect_ready));

    vj_effects[0] = dummy_init(width,height);
    vj_effects[1] = overlaymagic_init( width,height );
    vj_effects[2] = lumamagick_init( width,height );
    vj_effects[3] = diff_init(width, height);
    vj_effects[4] = opacity_init( width,height );
    vj_effects[5] = lumakey_init(width, height);
    vj_effects[6] = rgbkey_init( width,height );
    vj_effects[7] = chromamagick_init( width,height );
    vj_effects[8] = lumablend_init( width,height );
    vj_effects[9] = split_init(width,height);
    vj_effects[10] = borders_init(width,height);
    vj_effects[11] = frameborder_init(width,height);
    vj_effects[12] = slidingdoor_init(width, height);
    vj_effects[13] = transop_init(width, height);
    vj_effects[14] = transcarot_init(width, height);
    vj_effects[15] = transline_init(width, height);
    vj_effects[16] = transblend_init(width, height);
    vj_effects[17] = fadecolor_init(width,height);
    vj_effects[18] = fadecolorrgb_init(width,height);
    vj_effects[19] = whiteframe_init(width,height);
    vj_effects[20] = simplemask_init(width,height);
    vj_effects[21] = opacitythreshold_init(width,height);
    vj_effects[22] = opacityadv_init(width,height);
    vj_effects[23] = rgbkeysmooth_init(width,height);
    vj_effects[24] = wipe_init(width,height);
    vj_effects[25] = tracer_init(width, height);
    vj_effects[26] = mtracer_init(width, height);
    vj_effects[27] = dupmagic_init(width,height);
    vj_effects[28] = keyselect_init(width,height);
    vj_effects[29] = complexthreshold_init(width,height);
    vj_effects[30] = complexsync_init(width,height);
    vj_effects[31] = bar_init(width,height);
    vj_effects[32] = vbar_init(width,height);
    vj_effects[33] = lumamask_init(width,height);
    vj_effects[34] = binaryoverlay_init(width,height);
    vj_effects[35] = dissolve_init(width,height);
    vj_effects[36] = tripplicity_init(width,height);
    vj_effects[37] = videoplay_init(width,height);
    vj_effects[38] = videowall_init(width,height);
    vj_effects[39] = threshold_init(width,height);
	vj_effects[40] = differencemap_init(width,height);
	vj_effects[41] = picinpic_init(width,height);
	vj_effects[42] = chameleonblend_init(width,height);
	vj_effects[43] = radioactivetv_init(width,height);
//	vj_effects[44] = texmap_init( width,height);
	vj_effects[45] = water_init(width,height);
	vj_effects[46] = slicer_init(width,height);
	vj_effects[47] = average_blend_init(width,height);
	vj_effects[44] = iris_init(width,height);
    vj_effects[48] = dummy_init(width,height);
    vj_effects[i + 1] = mirrors2_init(width,height);
    vj_effects[i + 2] = mirrors_init(width,height);
    vj_effects[i + 3] = widthmirror_init(width,height);
    vj_effects[i + 4] = flip_init(width,height);
    vj_effects[i + 5] = posterize_init(width,height);
    vj_effects[i + 6] = negation_init(width,height);
    vj_effects[i + 7] = solarize_init(width,height);
    vj_effects[i + 8] = coloradjust_init(width,height);
    vj_effects[i + 9] = gamma_init(width,height);
    vj_effects[i + 10] = softblur_init(width,height);
    vj_effects[i + 11] = revtv_init(width, height);
    vj_effects[i + 12] = dices_init(width, height);
    vj_effects[i + 13] = smuck_init(width,height);
    vj_effects[i + 14] = killchroma_init(width,height);
    vj_effects[i + 15] = emboss_init(width,height);
    vj_effects[i + 16] = dither_init(width,height);
    vj_effects[i + 17] = rawman_init(width,height);
    vj_effects[i + 18] = rawval_init(width,height);
    vj_effects[i + 19] = transform_init(width,height);
    vj_effects[i + 20] = fibdownscale_init(width,height);
    vj_effects[i + 21] = reflection_init( width,height );
    vj_effects[i + 22] = rotozoom_init(width, height);
    vj_effects[i + 23] = colorshift_init(width,height);
    vj_effects[i + 24] = scratcher_init(width, height);
    vj_effects[i + 25] = magicscratcher_init(width, height);
    vj_effects[i + 26] = chromascratcher_init(width, height);
    vj_effects[i + 27] = distortion_init(width, height);
    vj_effects[i + 28] = greyselect_init(width,height);
    vj_effects[i + 29] = bwselect_init(width,height);
    vj_effects[i + 30] = complexinvert_init(width,height);
    vj_effects[i + 31] = complexsaturation_init(width,height);
    vj_effects[i + 32] = isolate_init(width,height);
    vj_effects[i + 33] = enhancemask_init(width,height);
    vj_effects[i + 34] = noiseadd_init(width,height);
    vj_effects[i + 35] = contrast_init(width,height);
    vj_effects[i + 36] = motionblur_init(width,height);
    vj_effects[i + 37] = sinoids_init(width,height);
    vj_effects[i + 38] = average_init(width,height);
    vj_effects[i + 39] = ripple_init(width,height);
    vj_effects[i + 40] = bathroom_init(width,height);
    vj_effects[i + 41] = slice_init(width,height);
    vj_effects[i + 42] = zoom_init(width, height);
    vj_effects[i + 44] = deinterlace_init(width,height);
    vj_effects[i + 45] = crosspixel_init(width,height);
    vj_effects[i + 46] = color_init(width,height);
    vj_effects[i + 47] = diffimg_init(width,height);
    vj_effects[i + 48] = noisepencil_init(width,height);  	
    vj_effects[i + 43] = pencilsketch_init(width,height);
    vj_effects[i + 50] = bgsubtract_init(width,height);
    vj_effects[i + 51] = magicmirror_init(width,height);
    vj_effects[i + 52] = smear_init(width,height);
    vj_effects[i + 53] = raster_init(width,height);
    vj_effects[i + 54] = fisheye_init(width,height);
    vj_effects[i + 55] = swirl_init(width,height);
    vj_effects[i + 56] = radialblur_init(width,height);
    vj_effects[i + 57] = chromium_init(width,height);
    vj_effects[i + 58] = chromapalette_init(width,height);
    vj_effects[i + 59] = uvcorrect_init(width,height);
    vj_effects[i + 60] = overclock_init(width,height);
	vj_effects[i + 61] = cartonize_init(width,height);
	vj_effects[i + 62] = nervous_init(width,height);
	vj_effects[i + 63] = morphology_init(width,height);
	vj_effects[i + 64] = blob_init(width,height);
	vj_effects[i + 65] = boids_init(width,height);
	vj_effects[i + 66] = ghost_init(width,height);
	vj_effects[i + 67] = neighbours_init(width,height);		
	vj_effects[i + 68] = neighbours2_init(width,height);
	vj_effects[i + 69] = neighbours3_init(width,height);
	vj_effects[i + 70] = neighbours4_init(width,height);
	vj_effects[i + 71] = neighbours5_init(width,height);
	vj_effects[i + 72] = cutstop_init(width,height);
	vj_effects[i + 73] = maskstop_init(width,height);
	vj_effects[i + 74] = photoplay_init(width,height);
	vj_effects[i + 75] = flare_init(width,height );
	vj_effects[i + 76] = constantblend_init(width,height);
	vj_effects[i + 77] = colormap_init(width,height);
	vj_effects[i + 78] = negatechannel_init(width,height);
	vj_effects[i + 79] = colmorphology_init(width,height);
	vj_effects[i + 80] = colflash_init(width,height);
	vj_effects[i + 81] = rgbchannel_init(width,height);
	vj_effects[i + 82] = autoeq_init(width,height);
	vj_effects[i + 83] = colorhis_init(width,height);
	vj_effects[i + 84] = motionmap_init(width,height);
	vj_effects[i + 85] = timedistort_init(width,height);
	vj_effects[i + 86] = chameleon_init(width,height);
	vj_effects[i + 87] = baltantv_init(width,height);
	vj_effects[i + 88] = contourextract_init(width,height);
	vj_effects[i + 49] = waterrippletv_init(width,height);
	vj_effects[i + 89 ]= radcor_init(width,height);
	vj_effects[i + 90 ]= cali_init(width,height);
	vj_effects[i + 91 ] = medianfilter_init(width,height);

	max_width = width;
	max_height = height;

        for(i=0; i  < MAX_EFFECTS; i++)
	{
		if(vj_effects[i])
		{
			if(i!=3) vj_effects[i]->static_bg = 0;
			vj_effects[i]->has_help = 0; 
			if(vj_effects[i]->rgb_conv != 1)
				vj_effects[i]->rgb_conv = 0;
		}
	}

	plug_sys_init( (full_range == 0 ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P ),width,height );

	n_ext_plugs_ = plug_sys_detect_plugins();

	int p = 0;
	int p_stop = MAX_EFFECTS + n_ext_plugs_;


	for( p = MAX_EFFECTS; p < p_stop; p ++ )
		vj_effects[p] = plug_get_plugin( (p-MAX_EFFECTS) );

}

static void vj_effect_free_parameters( vj_effect *v )
{
	int i;
	for( i = 0; i < v->num_params; i ++ ) {
		if( v->param_description[i] ) 
			free( v->param_description[i] );
	}
	free( v->param_description );
}

void vj_effect_free(vj_effect *ve) {
  if( ve ) {
	 if(ve->limits[0]) free(ve->limits[0]);
	 if(ve->limits[1]) free(ve->limits[1]);
	 if(ve->defaults) free(ve->defaults);
     	 if(ve->param_description) vj_effect_free_parameters( ve );
	 free(ve);
  }
}

void vj_effect_shutdown() {
    int i;
    vj_effect_deactivate_all(); 
    for(i=0; i < vj_effect_max_effects(); i++) { 
		if(vj_effects[i]) {
			if( i >= MAX_EFFECTS && vj_effects[i]->description) 
				free(vj_effects[i]->description);
		  vj_effect_free(vj_effects[i]);
		}
    }

    diff_destroy();
    //texmap_destroy();
    contourextract_destroy();
    rotozoom_destroy();
    distortion_destroy();
    cali_destroy();
    plug_sys_free();
}

void vj_effect_dump() {
	int i;
	veejay_msg(VEEJAY_MSG_INFO, "Below follow all effects in Veejay,");
	veejay_msg(VEEJAY_MSG_INFO, "Effect numbers starting with 2xx are effects that use");
	veejay_msg(VEEJAY_MSG_INFO, "*two* sources (by default a copy of itself)");
	veejay_msg(VEEJAY_MSG_INFO, "Use the channel/source commands to select another sample/stream");
	veejay_msg(VEEJAY_MSG_INFO, "to mix with.");
	veejay_msg(VEEJAY_MSG_INFO, "\n [effect num] [effect name] [arg 0 , min/max ] [ arg 1, min/max ] ...");
	for(i=0; i < vj_effect_max_effects(); i++) 
	{
		if(vj_effects[i])
		{
			printf("\t%d\t%-32s\n", vj_effect_get_real_id(i), vj_effects[i]->description);
			if(vj_effects[i]->num_params > 0)
			{
				int j=0;
				for(j=0; j < vj_effects[i]->num_params; j++) {
					printf("\t\t\t%-24s\t\t\t%d\t%d - %d\n", vj_effects[i]->param_description[j] , j, vj_effects[i]->limits[0][j],vj_effects[i]->limits[1][j]);
				}
			}	
			printf("\n");
			
		}

	}
}

/* figure out the position in the array, returns index of vj_effects array given an effect ID */
int vj_effect_real_to_sequence(int effect_id)
{
    if( effect_id >= VJ_EXT_EFFECT )
    {
	effect_id -= VJ_EXT_EFFECT;
	effect_id += MAX_EFFECTS;
	return effect_id;
    }
    else
    {
	if (effect_id > VJ_IMAGE_EFFECT_MIN && effect_id < VJ_IMAGE_EFFECT_MAX) {
		effect_id -= VJ_IMAGE_EFFECT_MIN;
		effect_id += VJ_VIDEO_COUNT;
		return effect_id;
	} else if (effect_id > VJ_VIDEO_EFFECT_MIN &&
	       effect_id < VJ_VIDEO_EFFECT_MAX) {
			effect_id -= VJ_VIDEO_EFFECT_MIN;
			return effect_id;
    		}
    }
    return -1;
}


int vj_effect_get_real_id(int effect_id)
{
	if (effect_id > 0 && effect_id < VJ_VIDEO_COUNT)
	{	/* video effect */
		effect_id += VJ_VIDEO_EFFECT_MIN;
		return effect_id;
	}
	else
	{
		if (effect_id >= VJ_VIDEO_COUNT && effect_id < MAX_EFFECTS)
		{	/* image effect */
			effect_id -= VJ_VIDEO_COUNT;	/* substract video count */
			effect_id += VJ_IMAGE_EFFECT_MIN;
			return effect_id;
    		}
    		else 
		{
			if( effect_id >= MAX_EFFECTS && effect_id <= vj_effect_max_effects())
			{
				effect_id -= MAX_EFFECTS;
				effect_id += VJ_EXT_EFFECT;
				return effect_id;
			}
		}
	}
	return 0;
}

int	vj_effect_get_by_name(char *name)
{	
	int i;
	if(!name) return 0;

	for ( i = 0; i < vj_effect_max_effects(); i ++ )
	{
		if( vj_effects[i]->description )
		{
			if(strcasecmp(name, vj_effects[i]->description ) == 0 )
			  return (int) vj_effect_get_real_id( i );
		}
	}

	return 0;
}

/* returns the description of an effect */
char *vj_effect_get_description(int effect_id)
{				/* 115 */
    int entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0)
	return vj_effects[entry]->description;

    return "<none>";
}

char *vj_effect_get_param_description(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0 && param_nr < vj_effects[entry]->num_params)
		return vj_effects[entry]->param_description[param_nr];
    return "Invalid paramater";
}

/* returns number of parameters */
int vj_effect_get_num_params(int effect_id)
{
    int entry;
    if(effect_id<0) return 0;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0)
	return vj_effects[entry]->num_params;
    return 0;
}

int vj_effect_get_static_bg(int effect_id)
{
	int entry;
	if(effect_id < 0) return 0;
	entry = vj_effect_real_to_sequence(effect_id);
	if(entry>0)
	  return vj_effects[entry]->static_bg;
 	return 0; 
}

/* returns default value of a parameter */
int vj_effect_get_default(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0 && param_nr >= 0
	&& param_nr < vj_effects[entry]->num_params)
	return vj_effects[entry]->defaults[param_nr];
    return 0;
}

/* returns minimum value of a parameter */
int vj_effect_get_min_limit(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0 && param_nr >= 0
	&& param_nr < vj_effects[entry]->num_params)
	return vj_effects[entry]->limits[0][param_nr];
    return 0;
}

/* returns the maximum value of a parameter */
int vj_effect_get_max_limit(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0 && param_nr >= 0
	&& param_nr < vj_effects[entry]->num_params)
	return vj_effects[entry]->limits[1][param_nr];
    return 0;
}

int vj_effect_get_extra_frame(int effect_id)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry > 0)
	return vj_effects[entry]->extra_frame;
    return 0;
}

int vj_effect_get_help(int entry)
{
	if(!vj_effects[entry])
		return 0;
	
	return 0;
}

int vj_effect_get_summary_len(int entry)
{
	if( !vj_effects[entry] )
		return 0;

	int p = vj_effects[entry]->num_params;
	int len = strlen( vj_effects[entry]->description );
	len += 3;
	len += 3;
	len += 1;
	len += 1;
	len += 2;
	len += 3;
	len += ( p * 18 );
	int i;
	for( i = 0; i < p; i ++ )
		len += (strlen(vj_effects[entry]->param_description[i])+3);
	

	return len;
}

int vj_effect_get_summary(int entry, char *dst)
{
	int p = vj_effects[entry]->num_params;
	int i;		
	char tmp[4096];

	if(!vj_effects[entry])
		return 0;

	sprintf(dst,"%03zu%s%03d%1d%1d%02d",
		strlen( vj_effects[entry]->description),
		vj_effects[entry]->description,
		vj_effect_get_real_id(entry),
		vj_effects[entry]->extra_frame,
		vj_effects[entry]->rgb_conv,
		p
		);
	for(i=0; i < p; i++)
	{
		snprintf(tmp,sizeof(tmp),
			"%06d%06d%06d%03zu%s",
			vj_effects[entry]->limits[0][i],
			vj_effects[entry]->limits[1][i],
			vj_effects[entry]->defaults[i],
			strlen(vj_effects[entry]->param_description[i]),
			vj_effects[entry]->param_description[i]
		
		);
		strncat( dst, tmp,strlen(tmp) );
	}
	return 1;
}

int	vj_effect_max_effects()
{
	return MAX_EFFECTS + n_ext_plugs_;
}

int vj_effect_get_subformat(int effect_id)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0)
	return vj_effects[entry]->sub_format;
    return 0;
}

/* return 1 if suggested value is acceptable, 0 if not */
int vj_effect_valid_value(int effect_id, int param_nr, int value)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0) {
	if (param_nr >= 0 && param_nr < vj_effects[entry]->num_params) {
	    if ((value >= vj_effects[entry]->limits[0][param_nr]) &&
		(value <= vj_effects[entry]->limits[1][param_nr]))
		return 1;
	}
    }
    return 0;
}

static int	vj_is_complex_effect(int effect_id)
{
	int i;
	for(i = 0; complex_effect_index[i].effect_id != 0; i ++ )
	{
		if(effect_id == complex_effect_index[i].effect_id )
			return 1;
	}
	return 0;
}

int	vj_effect_has_cb(int effect_id)
{
	int entry = vj_effect_real_to_sequence( effect_id );
	if(entry < 0) return 0;
	if( (vj_effects[entry]->has_user == 1 ) &&
		(vj_is_complex_effect(effect_id) == 1 ) )
		return 1;
	return 0;
}

int vj_effect_get_min_i()
{
	return VJ_IMAGE_EFFECT_MIN;
}

int vj_effect_get_max_i()
{
	return VJ_IMAGE_EFFECT_MAX;
}	

int vj_effect_get_min_v()
{
	return VJ_VIDEO_EFFECT_MIN;
}

int vj_effect_get_max_v()
{
	return VJ_VIDEO_EFFECT_MAX;
}

int	vj_effect_has_rgbkey(int effect_id)
{
   int entry;
   entry = vj_effect_real_to_sequence(effect_id);
   if (entry >= 0)
	{
		return ( vj_effects[entry]->rgb_conv);

	}	
   return 0;
}

int vj_effect_is_valid(int effect_id)
{
	if( effect_id >= VJ_EXT_EFFECT && effect_id < VJ_EXT_EFFECT + n_ext_plugs_)
		return 1;
	if( effect_id > VJ_IMAGE_EFFECT_MIN && effect_id < VJ_IMAGE_EFFECT_MAX )
		return 1;
	if( effect_id > VJ_VIDEO_EFFECT_MIN && effect_id < VJ_VIDEO_EFFECT_MAX )
		return 1;
	return 0;
}

