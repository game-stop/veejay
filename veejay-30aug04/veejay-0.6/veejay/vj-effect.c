/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "vj-effect.h"
#include "vj-common.h"
#include "sampleadm.h"
#include "effects/fibdownscale.h"
#include "effects/magicoverlays.h"
#include "effects/negation.h"
#include "effects/opacity.h"
#include "effects/posterize.h"
#include "effects/killchroma.h"
#include "effects/mirrors.h"
#include "effects/mirrors2.h"
#include "effects/negation.h"
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
#include "transitions/transblend.h"
#include "transitions/slidingdoor.h"
#include "transitions/fadecolor.h"
#include "transitions/transop.h"
#include "transitions/transline.h"
#include "transitions/transcarot.h"
#include "transitions/wipe.h"
#include "transitions/vbar.h"
#include "effects/diff.h"
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
#include "effects/bathroom.h"
#include "effects/slice.h"
#include "effects/zoom.h"
#include "effects/deinterlace.h"
#include "effects/mask.h"
#include "effects/crosspixel.h"
#include "effects/color.h"
#include "effects/noisepencil.h"
#include "effects/pencilsketch.h"
#include "effects/pixelate.h"
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
//{ 	diff_malloc		,	diff_free		,VJ_VIDEO_EFFECT_DIFF		},	
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
{	water_malloc		,	water_free		,VJ_IMAGE_EFFECT_RIPPLETV	},
{	zoom_malloc		,	zoom_free		,VJ_IMAGE_EFFECT_ZOOM		},
{	crosspixel_malloc	,	crosspixel_free		,VJ_IMAGE_EFFECT_CROSSPIXEL	},
{	fisheye_malloc,			fisheye_free		,VJ_IMAGE_EFFECT_FISHEYE	},
{	swirl_malloc		,	swirl_free		,VJ_IMAGE_EFFECT_SWIRL		},
{       radialblur_malloc,		radialblur_free,	 VJ_IMAGE_EFFECT_RADIALBLUR	},
{	uvcorrect_malloc,		uvcorrect_free,		VJ_IMAGE_EFFECT_UVCORRECT	},
{	NULL			,	NULL			,0				},
};

// complex effects have a buffer per instance
static struct
{
	int 	(*mem_init)(vj_effect_data *d,int w, int h );
	void	(*free)(vj_effect_data *d);
	int effect_id;
} complex_effect_index[] = 
{
	{	diff_malloc,		diff_free,			VJ_VIDEO_EFFECT_DIFF },
	{	NULL,			NULL,				0		     },
};

vj_effect *vj_effects[MAX_EFFECTS];

static int vj_effect_ready[MAX_EFFECTS];
static int max_width = 0;
static int max_height =0;


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

int vj_effect_initialized(int effect_id)
{

	int seq = vj_effect_real_to_sequence(effect_id);
	if( vj_effect_ready[seq] == 1)
		return 1;
	if ( _get_simple_effect(effect_id) == -1 && _get_complex_effect(effect_id) == -1) 
		return 1;
	
	return 0;
}
int vj_effect_activate(int effect_id)
{
	int seq = vj_effect_real_to_sequence(effect_id);
	if(seq < 0 || seq >= MAX_EFFECTS)
	{
		//veejay_msg(VEEJAY_MSG_ERROR, "Effect %d is unknown -> %d", effect_id,seq);
		return 0;
	}

	if( vj_effect_ready[seq] == 1 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s already activate, not allocating ???", vj_effect_get_description(effect_id));
		return 0;
	}

	if( vj_effect_ready[seq] == 0 )
	{
		int index = _get_simple_effect(effect_id);
		if(index==-1)
		{
			index = _get_complex_effect(effect_id);
			if(index == -1)
			{
				//veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s needs not be initialized",
				//	vj_effect_get_description(effect_id));
				return 0;
			}
			if(!complex_effect_index[index].mem_init( vj_effects[seq]->vjed, max_width, max_height ))
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Failed to initialize complex effect %s",
					vj_effect_get_description(effect_id));
				return 0;	
			}
			else
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Activated %s", vj_effect_get_description(effect_id));
				vj_effect_ready[seq] = 1;
				return 1;
			}
			//perhaps it is a complex effect
		}
		if(! simple_effect_index[index].mem_init( max_width, max_height ))
		{
				veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize simple effect %s",
					vj_effect_get_description(effect_id));
				return 0;
		}
		else
		{
				veejay_msg(VEEJAY_MSG_DEBUG, "Activated %s", vj_effect_get_description(effect_id));
				vj_effect_ready[seq] = 1;
				return 1;
		}
	}
	return 0;
}

int vj_effect_deactivate(int effect_id)
{
	int seq = vj_effect_real_to_sequence(effect_id);
	if(seq < 0 || seq >= MAX_EFFECTS)
	{
	//	veejay_msg(VEEJAY_MSG_ERROR, "(off)Effect %d is unknown -> %d", effect_id,seq);
		return 0;
	}
	if( vj_effect_ready[seq] == 0 )
	{
		//veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s not activated", vj_effect_get_description(effect_id));
		return 0;
	}
	if( vj_effect_ready[seq] == 1 )
	{
		int index = _get_simple_effect(effect_id);
		if(index==-1)
		{
			index = _get_complex_effect(effect_id);
			if(index == -1)
			{
				//veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s needs not be freed",
				//	vj_effect_get_description(effect_id));
				return 0;
			}
			complex_effect_index[index].free( vj_effects[seq]->vjed );
			veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %s", vj_effect_get_description(effect_id));
			vj_effect_ready[seq] = 0;
			return 1;
		}
		simple_effect_index[index].free(  );
		veejay_msg(VEEJAY_MSG_DEBUG, "Deactivated %s", vj_effect_get_description(effect_id));
		vj_effect_ready[seq] = 0;
		return 1;
	}
	return 0;
}

void vj_effect_deactivate_all()
{
	int i;
	for(i = 0 ; i < MAX_EFFECTS; i++)
	{
		int effect_id = vj_effect_get_real_id( i );
		if( effect_id > 100)
		{
			vj_effect_deactivate( effect_id );
		}
	}	
}

void vj_effect_initialize(int width, int height)
{
    int i = VJ_VIDEO_COUNT;
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
    //vj_effects[20] = diffimg_init(width,height);
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
//    vj_effects[33] = channelmix_init(width, int height);
    vj_effects[34] = binaryoverlay_init(width,height);
    vj_effects[35] = dissolve_init(width,height);
    vj_effects[36] = dummy_init(width,height);
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
	vj_effects[i + 49] = water_init(width,height);
    vj_effects[i + 43] = pencilsketch_init(width,height);
    vj_effects[i + 50] = pixelate_init(width,height); 
    vj_effects[i + 51] = magicmirror_init(width,height);
    vj_effects[i + 52] = smear_init(width,height);
    vj_effects[i + 53] = raster_init(width,height);
    vj_effects[i + 54] = fisheye_init(width,height);
    vj_effects[i + 55] = swirl_init(width,height);
    vj_effects[i + 56] = radialblur_init(width,height);
    vj_effects[i + 57] = chromium_init(width,height);
    vj_effects[i + 58] = chromapalette_init(width,height);
    vj_effects[i + 59] = uvcorrect_init(width,height);
    max_width = width;
    max_height = height;

    for(i=0; i  < MAX_EFFECTS; i++)
	{
		if(vj_effects[i])
		{
			if(i!=3) vj_effects[i]->static_bg = 0;
			if(i!=(VJ_VIDEO_COUNT+58)) vj_effects[i]->has_help = 0; 
		}
	}

}



void vj_effect_free(vj_effect *ve) {
  if(ve->limits[0]) free(ve->limits[0]);
  if(ve->limits[1]) free(ve->limits[1]);
  if(ve->defaults) free(ve->defaults);
 // if(ve->vjed) free(ve->vjed);
  free(ve);
}

void vj_effect_shutdown() {
    int i;
    vj_effect_deactivate_all(); 

    for(i=0; i < MAX_EFFECTS; i++) { 
	if(vj_effects[i]) {
	 vj_effect_free(vj_effects[i]);
	}
    }

	
}

void vj_effect_dump() {
	int i;
	for(i=0; i < MAX_EFFECTS; i++) 
	{
		if(vj_effects[i])
		{
			printf("\t%d\t\t\t%s\t\t", vj_effect_get_real_id(i), vj_effects[i]->description);
		
		
		if(vj_effects[i]->num_params > 0)
		{
			int j=0;
			for(j=0; j < vj_effects[i]->num_params; j++)
			{
				printf("\n\t\t\t\t\t\t\t%d\t%d - %d\n", j, vj_effects[i]->limits[0][j],vj_effects[i]->limits[1][j]);
			}
		}
		else
		{
			printf("\n");
		}
		}

	}

}

/* figure out the position in the array, returns index of vj_effects array given an effect ID */
int vj_effect_real_to_sequence(int effect_id)
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
    return -1;
}


int vj_effect_get_real_id(int effect_id)
{
    if (effect_id > 0 && effect_id < VJ_VIDEO_COUNT) {	/* video effect */
	effect_id += VJ_VIDEO_EFFECT_MIN;
	return effect_id;
    } else if (effect_id >= VJ_VIDEO_COUNT) {	/* image effect */
	effect_id -= VJ_VIDEO_COUNT;	/* substract video count */
	effect_id += VJ_IMAGE_EFFECT_MIN;
	return effect_id;
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

int vj_effect_get_summary(int entry, char *dst)
{
	int p = vj_effects[entry]->num_params;
	int i;		
	char tmp[20];

	if(!vj_effects[entry])
		return 0;
	

	sprintf(dst,"%03d%s%03d%1d%02d",
		strlen( vj_effects[entry]->description),
		vj_effects[entry]->description,
		vj_effect_get_real_id(entry),
		vj_effects[entry]->extra_frame,
		p
		);
	for(i=0; i < p; i++)
	{
		bzero(tmp,20);
		sprintf(tmp,
			"%06d%06d%06d",
			vj_effects[entry]->limits[0][i],
			vj_effects[entry]->limits[1][i],
			vj_effects[entry]->defaults[i]
		);
		strncat( dst, tmp,strlen(tmp) );
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
void vj_effect_update_max_limit(int effect_id, int param_nr, int value)
{
    int entry;
    entry = vj_effect_real_to_sequence(effect_id);
    if (effect_id > 0 && param_nr >= 0
	&& param_nr < vj_effects[entry]->num_params)
	vj_effects[entry]->limits[1][param_nr] = value;
}



int vj_effect_is_valid(int effect_id)
{
    if (effect_id > VJ_IMAGE_EFFECT_MIN && effect_id < VJ_IMAGE_EFFECT_MAX) {
	return 1;
    } else if (effect_id > VJ_VIDEO_EFFECT_MIN
	       && effect_id < VJ_VIDEO_EFFECT_MAX) {
	return 1;
    }
    return 0;
}
