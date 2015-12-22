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
//#include "effects/contourextract.h"
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
#include "effects/rgbkey.h"
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
#include "effects/perspective.h"
#include "effects/alphafill.h"
#include "effects/alpha2img.h"
#include "effects/toalpha.h"
#include "effects/mixtoalpha.h"
#include "effects/alphaflatten.h"
#include "effects/magicalphaoverlays.h"
#include "effects/travelmatte.h"
#include "effects/feathermask.h"
#include "effects/alphaselect.h"
#include "effects/alphaselect2.h"
#include "effects/alphablend.h"
#include "effects/porterduff.h"
#include "effects/pixelate.h"
#include "effects/alphanegate.h"
#include "effects/bgsubtract.h"
#include "effects/chromamagickalpha.h"
#include "effects/magicoverlaysalpha.h"
#include "effects/lumakeyalpha.h"
#include "effects/gaussblur.h"
#include "effects/levelcorrection.h"
#include "effects/alphadampen.h"
#include "effects/masktransition.h"
#include "effects/passthrough.h"
#include "effects/alphatransition.h"
#include "effects/randnoise.h"
#include <libplugger/plugload.h>
#include <veejay/vims.h>

unsigned int  pixel_Y_hi_ = 235;
unsigned int  pixel_U_hi_ = 240;
unsigned int  pixel_Y_lo_ = 16;
unsigned int  pixel_U_lo_ = 16;

unsigned int	get_pixel_range_min_Y() {
	return pixel_Y_lo_;
}
unsigned int	get_pixel_range_min_UV() {
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
{ 	bathroom_malloc,		bathroom_free		,VJ_IMAGE_EFFECT_BATHROOM	},
{ 	chromascratcher_malloc,	chromascratcher_free,VJ_IMAGE_EFFECT_CHROMASCRATCHER},
{	complexsync_malloc	,	complexsync_free	,VJ_VIDEO_EFFECT_COMPLEXSYNC	},
{	dices_malloc	 	,	dices_free			,VJ_IMAGE_EFFECT_DICES		},
{	colorhis_malloc,		colorhis_free		,VJ_IMAGE_EFFECT_COLORHIS	},
{	autoeq_malloc,			autoeq_free			,VJ_IMAGE_EFFECT_AUTOEQ		},
{	magicscratcher_malloc,	magicscratcher_free	,VJ_IMAGE_EFFECT_MAGICSCRATCHER	},
{	lumamask_malloc		,	lumamask_free		,VJ_VIDEO_EFFECT_LUMAMASK	},
{	motionblur_malloc	, 	motionblur_free		,VJ_IMAGE_EFFECT_MOTIONBLUR	},
{ 	magicmirror_malloc	,	magicmirror_free	,VJ_IMAGE_EFFECT_MAGICMIRROR	},
{	mtracer_malloc		, 	mtracer_free		,VJ_VIDEO_EFFECT_MTRACER	},
{	noiseadd_malloc		,	noiseadd_free		,VJ_IMAGE_EFFECT_NOISEADD	},
{	noisepencil_malloc	,	noisepencil_free	,VJ_IMAGE_EFFECT_NOISEPENCIL	},
{	reflection_malloc	,	reflection_free		,VJ_IMAGE_EFFECT_REFLECTION	},
{	ripple_malloc		,	ripple_free			,VJ_IMAGE_EFFECT_RIPPLE		},
{	rotozoom_malloc		,	rotozoom_free		,VJ_IMAGE_EFFECT_ROTOZOOM	},
{	scratcher_malloc	,	scratcher_free		,VJ_IMAGE_EFFECT_SCRATCHER	},
{	sinoids_malloc		,	sinoids_free		,VJ_IMAGE_EFFECT_SINOIDS	},
{	slice_malloc		,	slice_free			,VJ_IMAGE_EFFECT_SLICE		},
{	split_malloc		,	split_free			,VJ_VIDEO_EFFECT_SPLIT		},
{	tracer_malloc		,	tracer_free			,VJ_VIDEO_EFFECT_TRACER		},
{	zoom_malloc			,	zoom_free			,VJ_IMAGE_EFFECT_ZOOM		},
{	crosspixel_malloc	,	crosspixel_free		,VJ_IMAGE_EFFECT_CROSSPIXEL	},
{	fisheye_malloc,			fisheye_free		,VJ_IMAGE_EFFECT_FISHEYE	},
{	swirl_malloc		,	swirl_free			,VJ_IMAGE_EFFECT_SWIRL		},
{   radialblur_malloc,		radialblur_free,	 VJ_IMAGE_EFFECT_RADIALBLUR	},
{	uvcorrect_malloc,		uvcorrect_free,		VJ_IMAGE_EFFECT_UVCORRECT	},
{	overclock_malloc, 		overclock_free,		VJ_IMAGE_EFFECT_OVERCLOCK	},
{	nervous_malloc,			nervous_free,		VJ_IMAGE_EFFECT_NERVOUS		},
{	morphology_malloc,		morphology_free,	VJ_IMAGE_EFFECT_MORPHOLOGY	},
{	differencemap_malloc,	differencemap_free,	VJ_VIDEO_EFFECT_EXTDIFF		},
{	threshold_malloc,		threshold_free,		VJ_VIDEO_EFFECT_EXTTHRESHOLD	},
{	motionmap_malloc,		motionmap_free,		VJ_IMAGE_EFFECT_MOTIONMAP	},
{	colmorphology_malloc,	colmorphology_free,	VJ_IMAGE_EFFECT_COLMORPH	},
{	blob_malloc,			blob_free,			VJ_IMAGE_EFFECT_VIDBLOB 	},
{	boids_malloc,			boids_free,			VJ_IMAGE_EFFECT_VIDBOIDS 	},
{	ghost_malloc,			ghost_free,			VJ_IMAGE_EFFECT_GHOST		},
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
{	flare_malloc,			flare_free,			VJ_IMAGE_EFFECT_FLARE		},
{	timedistort_malloc,		timedistort_free,	VJ_IMAGE_EFFECT_TIMEDISTORT	},
{	chameleon_malloc,		chameleon_free,		VJ_IMAGE_EFFECT_CHAMELEON	},
{	chameleonblend_malloc,	chameleonblend_free,VJ_VIDEO_EFFECT_CHAMBLEND	},
{	baltantv_malloc,		baltantv_free,		VJ_IMAGE_EFFECT_BALTANTV	},
{	radcor_malloc,			radcor_free,		VJ_IMAGE_EFFECT_LENSCORRECTION	},
{	radioactivetv_malloc,	radioactivetv_free,	VJ_VIDEO_EFFECT_RADIOACTIVE	},
{	waterrippletv_malloc,	waterrippletv_free,	VJ_IMAGE_EFFECT_RIPPLETV	},
{	bgsubtract_malloc,		bgsubtract_free,	VJ_IMAGE_EFFECT_BGSUBTRACT	},
{	slicer_malloc,			slicer_free,		VJ_VIDEO_EFFECT_SLICER		},
{	perspective_malloc,		perspective_free,	VJ_IMAGE_EFFECT_PERSPECTIVE },
{	feathermask_malloc,		feathermask_free,	VJ_IMAGE_EFFECT_ALPHAFEATHERMASK },
{	average_malloc,			average_free,		VJ_IMAGE_EFFECT_AVERAGE },
{	rgbkey_malloc,			rgbkey_free,		VJ_VIDEO_EFFECT_RGBKEY },
{	gaussblur_malloc,		gaussblur_free,		VJ_IMAGE_EFFECT_CHOKEMATTE },
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
//	{	contourextract_malloc,  contourextract_free,		VJ_IMAGE_EFFECT_CONTOUR },
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
	if( _get_simple_effect(effect_id) == -1 && _get_complex_effect(effect_id) == -1 )
		return 1;
	return 0;
}

int	vj_effect_is_plugin( int effect_id )
{
	if( effect_id >= VJ_PLUGIN && effect_id <= (VJ_PLUGIN + n_ext_plugs_ ))
		return 1;
	return 0;
}

int vj_effect_initialized(int effect_id, void *instance_ptr )
{
 
	int seq = vj_effect_real_to_sequence(effect_id);
	if( seq < 0 )
		return 0;

	if( effect_id >= VJ_PLUGIN )
	{
		if( instance_ptr == NULL )
			return 0;
		return 1;
	}
	else if ( _no_mem_required( effect_id ) || vj_effect_ready[ effect_id ] == 1 )
		return 1;

	return 0;
}

static void 	*vj_effect_activate_ext( int fx_id, int *result )
{
	int plug_id = fx_id - VJ_PLUGIN;
	if( plug_id < 0)
		return NULL;
	
	void *plug = plug_activate( plug_id );
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
	if( seq < 0  ) {
		*result = 0;
		return NULL;
	}

	if( effect_id >= VJ_PLUGIN ) {
		return vj_effect_activate_ext( seq, result );
	}
	else if ( _no_mem_required(effect_id) ) {
		*result = 1;
		return NULL;
	}
	else if( vj_effect_ready[seq] == 1 )
	{
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
				vj_effect_ready[seq] = 1;
				*result = 1;
				return NULL;
			}
		}
		if(!simple_effect_index[index].mem_init( max_width, max_height ))
		{
				*result = 0;
				return NULL;
		}
		else
		{
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

	if(seq < 0 )
		return 0;
	
	if( seq >= VJ_PLUGIN ) {
		if(ptr) 
		{
			plug_deactivate( ptr );
			return 1;
		}
	} else if( vj_effect_ready[seq] == 1 ) {
		int index = _get_simple_effect(effect_id);
		if(index==-1) {
			index = _get_complex_effect(effect_id);
			if(index == -1)
			{
				return 0;
			}
			complex_effect_index[index].free( vj_effects[seq]->user_data );
			vj_effect_ready[seq] = 0;
			return 1;
		}
		simple_effect_index[index].free();
		vj_effect_ready[seq] = 0;
		return 1;
	}

	return 0;
}

void vj_effect_deactivate_all()
{
	int i;
	for(i = 0 ; i < FX_LIMIT; i ++ )
	{
		if( vj_effects[ i ] == NULL )
			continue;
		vj_effect_deactivate( i, NULL );
	}	
}

void vj_effect_initialize(int width, int height, int full_range)
{
    if( full_range )
    {
	    set_pixel_range( 255, 255,0,0 );
    }

	if( (width % 32) != 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING,"Video width should be a multiple of 32 for some effects" );
	}

	veejay_memset( vj_effects, 0, sizeof(vj_effects));
	veejay_memset( vj_effect_ready,0,sizeof(vj_effect_ready));

    vj_effects[VJ_IMAGE_EFFECT_DUMMY]				= dummy_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_OVERLAYMAGIC]		= overlaymagic_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_LUMAMAGICK]			= lumamagick_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_DIFF]				= diff_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_OPACITY]				= opacity_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_LUMAKEY]				= lumakey_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_RGBKEY]				= rgbkey_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_CHROMAMAGICK]		= chromamagick_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_LUMABLEND]			= lumablend_init( width,height );
    vj_effects[VJ_VIDEO_EFFECT_SPLIT]				= split_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_BORDERS]				= borders_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_FRAMEBORDER]			= frameborder_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_SLIDINGDOOR]			= slidingdoor_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_TRANSOP]				= transop_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_CAROT]				= transcarot_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_LINE]				= transline_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_TRANSBLEND]			= transblend_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_FADECOLOR]			= fadecolor_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_FADECOLORRGB]		= fadecolorrgb_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_WHITEFRAME]			= whiteframe_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_MASK]				= simplemask_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_THRESHOLDSMOOTH]		= opacitythreshold_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_THRESHOLD]			= opacityadv_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_RGBKEYSMOOTH]		= rgbkeysmooth_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_WIPE]				= wipe_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_TRACER]				= tracer_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_MTRACER]				= mtracer_init(width, height);
    vj_effects[VJ_VIDEO_EFFECT_DUPMAGIC]			= dupmagic_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_KEYSELECT]			= keyselect_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD]	= complexthreshold_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_COMPLEXSYNC]			= complexsync_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_3BAR]				= bar_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_VBAR]				= vbar_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_LUMAMASK]			= lumamask_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_BINARYOVERLAY]		= binaryoverlay_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_DISSOLVE]			= dissolve_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_TRIPPLICITY]			= tripplicity_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_VIDEOPLAY]			= videoplay_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_VIDEOWALL]			= videowall_init(width,height);
    vj_effects[VJ_VIDEO_EFFECT_EXTTHRESHOLD]		= threshold_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_EXTDIFF]				= differencemap_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_PICINPIC]			= picinpic_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_CHAMBLEND]			= chameleonblend_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_RADIOACTIVE]			= radioactivetv_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_RIPPLETV]			= water_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_SLICER]				= slicer_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_AVERAGEBLEND]		= average_blend_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_IRIS]				= iris_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_MIXTOALPHA]			= mixtoalpha_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_MAGICALPHA]			= overlayalphamagic_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_TRAVELMATTE]			= travelmatte_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_ALPHABLEND]			= alphablend_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_PORTERDUFF]			= porterduff_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_LUMAKEYALPHA]		= lumakeyalpha_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_CHROMAMAGICKALPHA]   = chromamagickalpha_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_MAGICOVERLAYALPHA]   = overlaymagicalpha_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_MASKTRANSITION]		= masktransition_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_PASSTHROUGH]			= passthrough_init(width,height);
	vj_effects[VJ_VIDEO_EFFECT_ALPHATRANSITION]		= alphatransition_init(width,height);

    vj_effects[VJ_IMAGE_EFFECT_DUMMY]				= dummy_init(width,height);
	
	vj_effects[VJ_IMAGE_EFFECT_PIXELATE]			= pixelate_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_MIRROR]				= mirrors2_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_MIRRORS]				= mirrors_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_WIDTHMIRROR]			= widthmirror_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_FLIP]				= flip_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_POSTERIZE]			= posterize_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_NEGATION]			= negation_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SOLARIZE]			= solarize_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_COLORADJUST]			= coloradjust_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_GAMMA]				= gamma_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SOFTBLUR]			= softblur_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_REVTV]				= revtv_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_DICES]				= dices_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_SMUCK]				= smuck_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_KILLCHROMA]			= killchroma_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_EMBOSS]				= emboss_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_DITHER]				= dither_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_RAWMAN]				= rawman_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_RAWVAL]				= rawval_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_TRANSFORM]			= transform_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_FIBDOWNSCALE]		= fibdownscale_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_REFLECTION]			= reflection_init( width,height );
    vj_effects[VJ_IMAGE_EFFECT_ROTOZOOM]			= rotozoom_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_COLORSHIFT]			= colorshift_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SCRATCHER]			= scratcher_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_MAGICSCRATCHER]		= magicscratcher_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_CHROMASCRATCHER]		= chromascratcher_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_DISTORTION]			= distortion_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_GREYSELECT]			= greyselect_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_BWSELECT]			= bwselect_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_COMPLEXINVERT]		= complexinvert_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_COMPLEXSATURATE]		= complexsaturation_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_ISOLATE]				= isolate_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_ENHANCEMASK]			= enhancemask_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_NOISEADD]			= noiseadd_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_CONTRAST]			= contrast_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_MOTIONBLUR]			= motionblur_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SINOIDS]				= sinoids_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_AVERAGE]				= average_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_RIPPLETV]			= ripple_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_BATHROOM]			= bathroom_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SLICE]				= slice_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_ZOOM]				= zoom_init(width, height);
    vj_effects[VJ_IMAGE_EFFECT_DEINTERLACE]			= deinterlace_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_CROSSPIXEL]			= crosspixel_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_COLORTEST]			= color_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_DIFF]				= diffimg_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_NOISEPENCIL]			= noisepencil_init(width,height);  	
    vj_effects[VJ_IMAGE_EFFECT_PENCILSKETCH]		= pencilsketch_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_BGSUBTRACT]			= bgsubtract_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_MAGICMIRROR]			= magicmirror_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_PIXELSMEAR]			= smear_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_RASTER]				= raster_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_FISHEYE]				= fisheye_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_SWIRL]				= swirl_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_RADIALBLUR]			= radialblur_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_CHROMIUM]			= chromium_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_CHROMAPALETTE]		= chromapalette_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_UVCORRECT]			= uvcorrect_init(width,height);
    vj_effects[VJ_IMAGE_EFFECT_OVERCLOCK]			= overclock_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_CARTONIZE]			= cartonize_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NERVOUS]				= nervous_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_MORPHOLOGY]			= morphology_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_VIDBLOB]				= blob_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_VIDBOIDS]			= boids_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_GHOST]				= ghost_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NEIGHBOUR]			= neighbours_init(width,height);		
	vj_effects[VJ_IMAGE_EFFECT_NEIGHBOUR2]			= neighbours2_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NEIGHBOUR3]			= neighbours3_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NEIGHBOUR4]			= neighbours4_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NEIGHBOUR5]			= neighbours5_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_CUTSTOP]				= cutstop_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_MASKSTOP]			= maskstop_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_PHOTOPLAY]			= photoplay_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_FLARE]				= flare_init(width,height );
	vj_effects[VJ_IMAGE_EFFECT_CONSTANTBLEND]		= constantblend_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_COLORMAP]			= colormap_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_NEGATECHANNEL]		= negatechannel_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_COLMORPH]			= colmorphology_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_COLFLASH]			= colflash_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_RGBCHANNEL]			= rgbchannel_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_AUTOEQ]				= autoeq_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_COLORHIS]			= colorhis_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_MOTIONMAP]			= motionmap_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_TIMEDISTORT]			= timedistort_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_CHAMELEON]			= chameleon_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_BALTANTV]			= baltantv_init(width,height);
//	vj_effects[VJ_IMAGE_EFFECT_CONTOUR]				= contourextract_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_RIPPLETV]			= waterrippletv_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_LENSCORRECTION ]		= radcor_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_CALI]				= cali_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_MEDIANFILTER]		= medianfilter_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_PERSPECTIVE]			= perspective_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHAFILL]			= alphafill_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHA2IMG]			= alpha2img_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_TOALPHA]				= toalpha_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHAFLATTEN]		= alphaflatten_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHAFEATHERMASK]	= feathermask_init(width,height);
//	vj_effects[VJ_IMAGE_EFFECT_ALPHASELECT]			= alphaselect_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHASELECT2]		= alphaselect2_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHANEGATE]			= alphanegate_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_CHOKEMATTE]			= gaussblur_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_LEVELCORRECTION]		= levelcorrection_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_ALPHADAMPEN]			= alphadampen_init(width,height);
	vj_effects[VJ_IMAGE_EFFECT_RANDNOISE]			= randnoise_init(width,height);

	max_width = width;
	max_height = height;

	plug_sys_init( (full_range == 0 ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P ),width,height );

	n_ext_plugs_ = plug_sys_detect_plugins();

	int p = 0;
	int p_stop = VJ_PLUGIN + n_ext_plugs_;


	for( p = VJ_PLUGIN; p < p_stop; p ++ )
		vj_effects[p] = plug_get_plugin( p - VJ_PLUGIN );

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

int	vj_effect_max_effects()
{
	return FX_LIMIT;
}

void vj_effect_shutdown() {
    int i;
    vj_effect_deactivate_all(); 
    for(i=0; i < FX_LIMIT; i++) { 
		if(vj_effects[i]) {
		  vj_effect_free(vj_effects[i]);
		}
    }

    diff_destroy();
    //texmap_destroy();
//    contourextract_destroy();
    rotozoom_destroy();
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
	for(i=0; i < FX_LIMIT; i++) 
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
	if( effect_id <= 0 || effect_id >= FX_LIMIT )
		return -1;

	if( vj_effects[effect_id] )
		return effect_id;

	return -1;
}


int vj_effect_get_real_id(int effect_id)
{
	if( effect_id < 0 || effect_id >= FX_LIMIT )
		return 0;

	return effect_id;
}

int	vj_effect_get_by_name(char *name)
{	
	int i;
	if(!name) return 0;

	for ( i = 0; i < FX_LIMIT; i ++ )
	{
		if( vj_effects[i]->description )
		{
			if(strcasecmp(name, vj_effects[i]->description ) == 0 )
			  return (int) i;
		}
	}
	return 0;
}

/* returns the description of an effect */
char *vj_effect_get_description(int effect_id)
{				/* 115 */
    int entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0)
		return vj_effects[entry]->description;

    return "<none>";
}

char *vj_effect_get_param_description(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0 && param_nr < vj_effects[entry]->num_params)
		return vj_effects[entry]->param_description[param_nr];
    return "Invalid paramater";
}

/* returns number of parameters */
int vj_effect_get_num_params(int effect_id)
{
    int entry;
    if(effect_id<0) return 0;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0)
		return vj_effects[entry]->num_params;
    return 0;
}

int vj_effect_get_static_bg(int effect_id)
{
	int entry;
	if(effect_id < 0) return 0;
	entry = vj_effect_real_to_sequence(effect_id);
	if(entry>=0)
		return vj_effects[entry]->static_bg;
 	return 0; 
}

/* returns default value of a parameter */
int vj_effect_get_default(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0 && param_nr >= 0 && param_nr < vj_effects[entry]->num_params)
		return vj_effects[entry]->defaults[param_nr];
    return 0;
}

/* returns minimum value of a parameter */
int vj_effect_get_min_limit(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0 && param_nr >= 0	&& param_nr < vj_effects[entry]->num_params)
		return vj_effects[entry]->limits[0][param_nr];
    return 0;
}

/* returns the maximum value of a parameter */
int vj_effect_get_max_limit(int effect_id, int param_nr)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0 && param_nr >= 0 && param_nr < vj_effects[entry]->num_params)
		return vj_effects[entry]->limits[1][param_nr];
    return 0;
}

int	vj_effect_get_info( int effect_id, int *is_mixer, int *n_params )
{
	int entry;
	entry = vj_effect_real_to_sequence( effect_id );
	if( entry >= 0 ) {
		*is_mixer = vj_effects[entry]->extra_frame;
		*n_params = vj_effects[entry]->num_params;
		return vj_effects[entry]->rgba_only;
	}
	return 0;
}

int vj_effect_get_extra_frame(int effect_id)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (entry >= 0)
		return vj_effects[entry]->extra_frame;
    return 0;
}

int vj_effect_get_help(int entry)
{
	if(!vj_effects[entry])
		return 0;
	
	return 0;
}


static int	vj_effect_get_hints_length( vj_effect *fx, int p, int limit )
{
	if( fx->hints == NULL )
		return 0;

	if( fx->hints[p] == NULL )
		return 0;

	if( fx->hints[p]->description == NULL )
		return 0;

	int len = 0;
	int i;
	
	for( i = 0; i <= limit; i ++ ) {
		len += strlen(fx->hints[p]->description[i]);
		len += 3;
	}

	return len;
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

	for( i = 0; i < p; i ++ ) {
		len += vj_effect_get_hints_length( vj_effects[entry], i, vj_effects[entry]->limits[1][i] ) + 3;	
	}

	return len;
}

int vj_effect_get_summary(int entry, char *dst)
{
	if(!vj_effects[entry])
		return 0;

	int p = vj_effects[entry]->num_params;
	int i,j;		
	char tmp[4096];

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

	for(i=0; i < p; i ++ )
	{
		int limit = vj_effects[entry]->limits[1][i];
		int vlen = vj_effect_get_hints_length( vj_effects[entry], i, limit );
		
		snprintf(tmp,sizeof(tmp),
				"%03d",
				vlen );
		
		strncat( dst, tmp, strlen(tmp) );
		
		if( vlen == 0 )
			continue;

		for( j = 0; j <= limit; j ++ ) {
			snprintf(tmp,sizeof(tmp),
				"%03zu%s",
				strlen( vj_effects[entry]->hints[i]->description[j] ),
				vj_effects[entry]->hints[i]->description[j] );
			strncat( dst,tmp,strlen(tmp));
		}
	}

	return 1;
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
	if( (vj_effects[entry]->has_user == 1 ) && (vj_is_complex_effect(effect_id) == 1 ) )
		return 1;
	return 0;
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
	if( effect_id < 0 || effect_id > FX_LIMIT )
		return 0;
	return (vj_effects[effect_id] == NULL ? 0 : 1);
}

