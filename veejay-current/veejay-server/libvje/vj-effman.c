/*
 * Copyright (C) 2002-2004 Niels Elburg <nelburg@looze.nl>
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
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <libplugger/plugload.h>
#include <veejay/vj-task.h>
extern vj_effect *vj_effects[]; 

#define VEVO_PLUG_LIVIDO        0xffaa
#define VEVO_PLUG_FF            0x00ff
#define VEVO_PLUG_FR            0xffbb

void vj_effman_apply_plug_effect(
	VJFrame **frames,
	VJFrameInfo *frameinfo,
	vjp_kf *todo_info,
	int *arg,
	int n_arg,
	int entry,
	int fx_id,
	void *instance)

{
	int plug_id = entry - MAX_EFFECTS;
	int n 	    = plug_get_num_input_channels( plug_id );

	int i;

	if( plug_is_frei0r( instance ) ) {
		plug_set_parameters( instance, n_arg, arg );
	} else {
		n_arg	    = plug_get_num_parameters( plug_id );

		for( i = 0; i < n_arg; i ++ ) {
			plug_set_parameter(instance,i, 1, &(arg[i]) );
		}
	}

	for( i = 0; i < n; i ++ ) {
		plug_push_frame(instance, 0, i, frames[i]);
	}

	if( plug_get_num_output_channels(plug_id ) > 0 )
		plug_push_frame(instance, 1, 0, frames[0] );

	
	plug_process( instance, (double) frameinfo->timecode );
}

void vj_effman_apply_image_effect(
	VJFrame **frames,
	vjp_kf *todo_info,
	int *arg,
	int entry,
	int e)

	{

   int j;

   switch (e) {
	case VJ_IMAGE_EFFECT_CONSTANTBLEND:
		constantblend_apply( frames[0], frames[0]->width,
			frames[0]->height, arg[0], arg[1], arg[2]);
		break;
	case VJ_IMAGE_EFFECT_FLARE:
		flare_apply( frames[0], frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_PHOTOPLAY:
		photoplay_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2]);
			break;
	case VJ_IMAGE_EFFECT_MASKSTOP:
		maskstop_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_CUTSTOP:
		cutstop_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR4:
		neighbours4_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR5:
		neighbours5_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR2:
		neighbours2_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR:
		neighbours_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR3:
	neighbours3_apply(frames[0],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2]);
		break;

	case VJ_IMAGE_EFFECT_RIPPLETV:
	waterrippletv_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
	 case VJ_IMAGE_EFFECT_PENCILSKETCH:
	  pencilsketch_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
	 break;
      case VJ_IMAGE_EFFECT_NOISEPENCIL:
	noisepencil_apply(frames[0],frames[0]->width,frames[0]->height,
		arg[0],arg[1],arg[2],arg[3]);
	break;
	case VJ_IMAGE_EFFECT_CALI:
		cali_apply( vj_effects[entry]->user_data,frames[0], frames[0]->width,frames[0]->height,arg[0], arg[1] );
		break;
      case VJ_IMAGE_EFFECT_DIFF:
	diffimg_apply(frames[0], 
		      frames[0]->width, frames[0]->height, arg[0], arg[1],
		      arg[2]);
	break;

     case VJ_IMAGE_EFFECT_COMPLEXINVERT:
	complexinvert_apply(frames[0], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_COMPLEXSATURATE:
	complexsaturation_apply(frames[0], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
     case VJ_IMAGE_EFFECT_REFLECTION:
	reflection_apply(frames[0], frames[0]->width, frames[0]->height,
			 arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_FIBDOWNSCALE:
	for (j = 0; j < arg[1]; j++) {
	    fibdownscale_apply(frames[0], frames[0], frames[0]->width,
			       frames[0]->height, arg[0]);
	}
	break;
     case VJ_IMAGE_EFFECT_NOISEADD:
	noiseadd_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_CONTRAST:
	contrast_apply(frames[0], frames[0]->width, frames[0]->height,arg);
	break;
     case VJ_IMAGE_EFFECT_ENHANCEMASK:
	enhancemask_apply(frames[0],frames[0]->width,frames[0]->height,arg);
	break;
     case VJ_IMAGE_EFFECT_SOLARIZE:
	solarize_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DISTORTION:
	distortion_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
			 arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GAMMA:
	gamma_apply(
		    frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORADJUST:
	coloradjust_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
			  arg[1]);
	break;
     case VJ_IMAGE_EFFECT_MOTIONBLUR:
        motionblur_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;

     case VJ_IMAGE_EFFECT_MAGICSCRATCHER:
	magicscratcher_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
			     arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_CHROMASCRATCHER:
	chromascratcher_apply(frames[0], frames[0]->width, frames[0]->height,
			      arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SCRATCHER:
	scratcher_apply(frames[0], frames[0]->width, frames[0]->height,
			arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_KILLCHROMA:
	killchroma_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]
			 );
	break;
     case VJ_IMAGE_EFFECT_MIRROR:
	mirrors2_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_MIRRORS:
	mirrors_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
		      arg[1]);
	break;
	case VJ_IMAGE_EFFECT_MAGICMIRROR:
	magicmirror_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
	break;
    case  VJ_IMAGE_EFFECT_RASTER:
	raster_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;
	case VJ_IMAGE_EFFECT_SWIRL:
	swirl_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;
    case VJ_IMAGE_EFFECT_RADIALBLUR:
	radialblur_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1],arg[2]);
	break;
    case VJ_IMAGE_EFFECT_FISHEYE:
	fisheye_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_PIXELSMEAR:
	smear_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1]);
	break;  
     case VJ_IMAGE_EFFECT_UVCORRECT:
	uvcorrect_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
   case VJ_IMAGE_EFFECT_CHROMAPALETTE:
	chromapalette_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
   case VJ_IMAGE_EFFECT_CHROMIUM:
	chromium_apply( frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
	case VJ_IMAGE_EFFECT_CARTONIZE:
		cartonize_apply( frames[0], frames[0]->width,frames[0]->height,
			arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_VIDBLOB:
		blob_apply( frames[0],frames[0]->width,frames[0]->height,
			arg[0],arg[1],arg[2],arg[3] );
			break;
	case VJ_IMAGE_EFFECT_VIDBOIDS:
		boids_apply( frames[0],frames[0]->width,frames[0]->height,
			arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],arg[7] );
		break;
	case VJ_IMAGE_EFFECT_GHOST:
		ghost_apply( frames[0], frames[0]->width,frames[0]->height,arg[0]);
		break;
	case VJ_IMAGE_EFFECT_MORPHOLOGY:
		morphology_apply( frames[0], frames[0]->width,frames[0]->height,
			arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_COLMORPH:
		colmorphology_apply( frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NERVOUS:
		nervous_apply( frames[0], frames[0]->width, frames[0]->height,
			arg[0]); break;
   case VJ_IMAGE_EFFECT_OVERCLOCK:
	overclock_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1]);
	break;
	case VJ_IMAGE_EFFECT_COLORHIS:
		colorhis_apply( frames[0], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3] );
		break;
	case VJ_IMAGE_EFFECT_AUTOEQ:
		autoeq_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
	case VJ_IMAGE_EFFECT_BALTANTV:
		baltantv_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1]);
		break;
	case VJ_IMAGE_EFFECT_CHAMELEON:
		chameleon_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
		break;
	case VJ_IMAGE_EFFECT_TIMEDISTORT:
		timedistort_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
		break;
	case VJ_IMAGE_EFFECT_LENSCORRECTION:
		radcor_apply( frames[0], frames[0]->width,frames[0]->height, arg[0],arg[1] ,arg[2]);
		break;
     case VJ_IMAGE_EFFECT_NEGATION:
	negation_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
	case VJ_IMAGE_EFFECT_MEDIANFILTER:
	medianfilter_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;
   case VJ_IMAGE_EFFECT_NEGATECHANNEL:
	negatechannel_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],arg[1]);
	break;

	case VJ_IMAGE_EFFECT_COLFLASH:
	colflash_apply(frames[0], frames[0]->width,frames[0]->height,arg[0],
			arg[1],arg[2],arg[3],arg[4] );
	break;
     case VJ_IMAGE_EFFECT_COLORMAP:
	colormap_apply(frames[0], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_POSTERIZE:
	posterize_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
			arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_DITHER:
	dither_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_EMBOSS:
	emboss_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_FLIP:
	flip_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_REVTV:
	revtv_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
		    arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_COLORSHIFT:
	colorshift_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
			 arg[1]); 
	break;
     case VJ_IMAGE_EFFECT_SOFTBLUR:
	softblur_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_WIDTHMIRROR:
	widthmirror_apply(frames[0], frames[0]->width, frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DICES:
	dices_apply(vj_effects[entry], frames[0], frames[0]->width,
		    frames[0]->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORTEST:
	color_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_RAWMAN:
	rawman_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_RAWVAL:
	rawval_apply(frames[0], frames[0]->width, frames[0]->height, arg[0], arg[1],
		     arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SMUCK:
	smuck_apply(frames[0], frames[0], frames[0]->width, frames[0]->height,
		    arg[0]);
	break;
     case VJ_IMAGE_EFFECT_TRANSFORM:
	transform_apply(frames[0], frames[0], frames[0]->width, frames[0]->height,
			arg[0]);
	break;
     case VJ_IMAGE_EFFECT_BWSELECT:
	bwselect_apply(frames[0], frames[0]->width, frames[0]->height, arg[0], arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GREYSELECT:
	greyselect_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
	break;
     case VJ_IMAGE_EFFECT_ISOLATE:
	isolate_apply(frames[0],frames[0]->width,frames[0]->height,
		arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_ROTOZOOM:
	rotozoom_apply(frames[0],
		       frames[0]->width,
		       frames[0]->height, arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SINOIDS:
	sinoids_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_AVERAGE:
	average_apply(frames[0],frames[0]->width,frames[0]->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_RIPPLE:
	ripple_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_BGSUBTRACT:
	bgsubtract_apply( frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_BATHROOM:
	bathroom_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_RGBCHANNEL:
	rgbchannel_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_ZOOM:
	zoom_apply(frames[0], frames[0]->width, frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
	break;
     case VJ_IMAGE_EFFECT_CROSSPIXEL:
	crosspixel_apply(frames[0],frames[0]->width,frames[0]->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_DEINTERLACE:
	 deinterlace_apply( frames[0], frames[0]->width, frames[0]->height, arg[0]);
	 break;
	case VJ_IMAGE_EFFECT_MOTIONMAP:
	motionmap_apply( frames[0], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
	case VJ_IMAGE_EFFECT_CONTOUR:
	contourextract_apply( vj_effects[entry]->user_data, frames[0],
			frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4] );	
	break;
     case VJ_IMAGE_EFFECT_SLICE:
	if(arg[2] > 0) { 
	   todo_info->tmp[0] ++; 
	   if(todo_info->tmp[0] > arg[2]) { todo_info->tmp[0] = 0; todo_info->tmp[1] = 1; }
	} else { 
	     todo_info->tmp[1] = arg[1];
	}

	slice_apply(frames[0],frames[0]->width,frames[0]->height,arg[0], todo_info->tmp[1]);
	todo_info->tmp[1] = 0;
	break;
    }
}

void vj_effman_apply_video_effect( VJFrame **frames, vjp_kf *todo_info,int *arg, int entry, int e) {

    switch(e) {
	case VJ_VIDEO_EFFECT_CHAMBLEND:
		chameleonblend_apply(frames[0],frames[1], frames[0]->width,frames[0]->height,arg[0]);
		break;
	case VJ_VIDEO_EFFECT_EXTDIFF:
		differencemap_apply( frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_VIDEO_EFFECT_EXTTHRESHOLD:
		threshold_apply( frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1]);
		break;
	case VJ_VIDEO_EFFECT_VIDEOWALL:
		videowall_apply(frames[0],frames[1],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_VIDEO_EFFECT_VIDEOPLAY:
		videoplay_apply(frames[0],frames[1],frames[0]->width,
			frames[0]->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_VIDEO_EFFECT_TRIPPLICITY:
	tripplicity_apply(frames[0],frames[1], 
		frames[0]->width,frames[0]->height, arg[0],arg[1],arg[2]);
		break;
      case VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD:
	complexthreshold_apply(frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],
		arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_DUPMAGIC:
	dupmagic_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		       arg[0]);
	break;
      case VJ_VIDEO_EFFECT_LUMAMAGICK:
	lumamagic_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
			arg[0], arg[1],arg[2]);
   	break;
      case VJ_VIDEO_EFFECT_BINARYOVERLAY:
	binaryoverlay_apply(frames[0], frames[1],frames[0]->width,frames[0]->height,arg[0] );   break;
      case VJ_VIDEO_EFFECT_OVERLAYMAGIC:
	overlaymagic_apply(frames[0], frames[1], frames[0]->width,frames[0]->height, arg[0],arg[1]);
	break;
	case VJ_VIDEO_EFFECT_SLICER:
		slicer_apply(frames[0],frames[1], frames[0]->width, frames[0]->height, arg[0],arg[1] );
		break;
      case VJ_VIDEO_EFFECT_MASK:
	simplemask_apply(frames[0],frames[1], frames[0]->width,frames[0]->height,arg[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_LUMAMASK:
	lumamask_apply(frames[0], frames[1], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2]);
	break;
	case VJ_VIDEO_EFFECT_DISSOLVE:
	dissolve_apply(frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0]);break; 
      case VJ_VIDEO_EFFECT_OPACITY:
	opacity_apply(frames[0], frames[1], frames[0]->width,
		      frames[0]->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_IRIS:
	iris_apply( frames[0],frames[1], frames[0]->width,frames[0]->height,arg[0],arg[1]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLDSMOOTH:
	opacitythreshold_apply(frames[0], frames[1], frames[0]->width,
			       frames[0]->height, arg[0], arg[1],arg[2]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLD:
	opacityadv_apply(frames[0], frames[1], frames[0]->width,
			 frames[0]->height, arg[0], arg[1], arg[2]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEY:
	rgbkey_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		     arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_KEYSELECT:
	keyselect_apply(frames[0],frames[1],frames[0]->width,frames[0]->height,
			arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
      case VJ_VIDEO_EFFECT_CHROMAMAGICK:
	chromamagick_apply(frames[0], frames[1], frames[0]->width,
			   frames[0]->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LUMABLEND:
	lumablend_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
			arg[0], arg[1], arg[2], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_LUMAKEY:
	lumakey_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		      arg[4], arg[1], arg[2], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_DIFF:
	diff_apply( vj_effects[entry]->user_data, frames[0], frames[1],
		   frames[0]->width, frames[0]->height, arg[0], arg[1],arg[2],arg[3]);
	break;
	/*case VJ_VIDEO_EFFECT_TEXMAP:
	texmap_apply( vj_effects[entry]->user_data, frames[0],frames[1],
			frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4] );	
	break;*/
      case VJ_VIDEO_EFFECT_WHITEFRAME:
	whiteframe_apply(frames[0], frames[1], frames[0]->width, frames[0]->height);
	break;
      case VJ_VIDEO_EFFECT_MTRACER:
	mtracer_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		      arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_TRACER:
	tracer_apply(frames[0], frames[1], frames[0]->width,
		     frames[0]->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_CAROT:
	transcarot_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
			 arg[2], arg[3], arg[4], arg[5], arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LINE:
	transline_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
			arg[2], arg[1], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_TRANSOP:
	transop_apply(frames[0], frames[1], arg[1], arg[2], arg[3],
		      arg[4], arg[5], arg[6], frames[0]->width, frames[0]->height,
		      arg[0]);
	break;
      case VJ_VIDEO_EFFECT_COMPLEXSYNC:
	if(arg[1] == 1) { /* auto increment as option in effect*/
	   if(arg[2]==0) arg[2]=1;   
	   todo_info->tmp[0] += (arg[0]/arg[2] )+1; /* val / max frames */
	   if(todo_info->tmp[0] > frames[0]->height-2) todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[0];
	}
	complexsync_apply(frames[0], frames[1],frames[0]->width, frames[0]->height, 
			todo_info->tmp[0]);
	break;
      case VJ_VIDEO_EFFECT_FADECOLORRGB:
	if (arg[4] == 0) {
	    if (todo_info->tmp[0] >= 255)
		todo_info->tmp[0] = arg[0];
	    todo_info->tmp[0] += (arg[0] / arg[5]);
	} else {
	    if (todo_info->tmp[0] <= 0)
		todo_info->tmp[0] = arg[0];

	    todo_info->tmp[0] -= (arg[0] / arg[5]);
	}

	colorfadergb_apply(frames[0], frames[0]->width, frames[0]->height,
			   todo_info->tmp[0], arg[1], arg[2], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_FADECOLOR:
	if (arg[3] == 0) {
	    if (todo_info->tmp[0] >= 255)
		todo_info->tmp[0] = arg[0];
	    todo_info->tmp[0] += (arg[0] / arg[2]);
	} else {
	    if (todo_info->tmp[0] <= 0)
		todo_info->tmp[0] = arg[0];
	    todo_info->tmp[0] -= (arg[0] / arg[2]);
	}


	colorfade_apply(frames[0], frames[0]->width, frames[0]->height,
			todo_info->tmp[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_VBAR:
		vbar_apply(frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
		break;
      case VJ_VIDEO_EFFECT_3BAR:
	bar_apply(frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
      case VJ_VIDEO_EFFECT_SLIDINGDOOR:
	if(arg[1] == 1) { /* auto increment as option in effect*/
	   todo_info->tmp[0] ++; /* val / max frames */
	   if(todo_info->tmp[0] >= (frames[0]->height/16))
		   todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[0];
	}

	slidingdoor_apply(frames[0], frames[1], frames[0]->width,
			  frames[0]->height, todo_info->tmp[0]  );
	break;
      case VJ_VIDEO_EFFECT_WIPE:
	wipe_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		   arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEYSMOOTH:
	rgbkeysmooth_apply(frames[0], frames[1], frames[0]->width,
			   frames[0]->height, arg[0], arg[1], arg[2], arg[3],
			   arg[4],arg[5]);
	break;
      case VJ_VIDEO_EFFECT_SPLIT:
	split_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
		    arg[0], arg[1]);
	break;
       case VJ_VIDEO_EFFECT_BORDERS:
	borders_apply(frames[0], frames[0]->width, frames[0]->height, arg[0],
		      arg[1]);
	break;
      case VJ_VIDEO_EFFECT_FRAMEBORDER:
	frameborder_apply(frames[0], frames[1], frames[0]->width,
			  frames[0]->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_TRANSBLEND:
	transblend_apply(frames[0], frames[1], frames[0]->width, frames[0]->height,
			 arg[0], arg[1], arg[2], arg[3], arg[4], arg[5],
			 arg[6]);
	break;
	case VJ_VIDEO_EFFECT_PICINPIC:
	picinpic_apply( vj_effects[entry]->user_data,frames[0], frames[1], frames[0]->width, frames[0]->height,
			arg[0], arg[1], arg[2], arg[3] );
	break;
	case VJ_VIDEO_EFFECT_RIPPLETV:
	water_apply( vj_effects[entry]->user_data,frames[0],frames[1],frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3],arg[4] );

	break;
	case VJ_VIDEO_EFFECT_RADIOACTIVE:
	radioactivetv_apply( frames[0],frames[1], frames[0]->width,frames[0]->height,arg[0],arg[1],arg[2],arg[3]);
	break;
	case VJ_VIDEO_EFFECT_AVERAGEBLEND:
	average_blend_apply(frames[0], frames[1], frames[0]->width,
		      frames[0]->height, arg[0]);
	break;

   
      default:
	break;
    }

}

int vj_effect_prepare( VJFrame *frame, int selector)
{
	int fx_id = vj_effect_real_to_sequence( selector );
	if( fx_id < 0 || fx_id > MAX_EFFECTS )
		return 0;

	switch( selector ) {
		case VJ_IMAGE_EFFECT_BGSUBTRACT:
			return bgsubtract_prepare( frame->data, frame->width,frame->height );
			break;	
		case 	VJ_IMAGE_EFFECT_CONTOUR:
			return contourextract_prepare(frame->data,frame->width,frame->height );
			break;
		case	VJ_VIDEO_EFFECT_DIFF:
			if( !vj_effect_has_cb(selector))
				return 0;
			return diff_prepare(	(void*) vj_effects[fx_id]->user_data,	frame->data,	frame->width,	frame->height );
			break;
		case 	VJ_IMAGE_EFFECT_CHAMELEON:
			return chameleon_prepare(	frame->data,	frame->width,	frame->height );
			break;
		case 	VJ_IMAGE_EFFECT_MOTIONMAP:
			return	motionmap_prepare(	frame->data,	frame->width,	frame->height );
			break;
		case 	VJ_VIDEO_EFFECT_CHAMBLEND:
			return chameleonblend_prepare(	frame->data,	frame->width,	frame->height );
		default:
			break;
	}
	return 0;
}
static void	vj_effman_apply_job( void *arg )
{
	vj_task_arg_t *v = (vj_task_arg_t*) arg;

	int entry	 = v->iparams[11];
	int selector	 = v->iparam;
	vjp_kf *kf	 = v->ptr;
	VJFrame frame;
	VJFrame frame2;
		
	vj_task_set_to_frame( &frame, 0, v->jobnum );

	VJFrame *frames[2];

	frames[0] = &frame;
	frames[1] = &frame2;

//f( v->overlap ) {
//frame.data[3] = v->overlaprow;
//

	if( selector > 200 )	
	{
		vj_task_set_to_frame( &frame2, 1, v->jobnum);
		vj_effman_apply_video_effect(frames,kf, v->iparams, entry,selector);
	}
	else
	{
		vj_effman_apply_image_effect( frames,kf,v->iparams,entry, selector);
	}	

}

int	vj_effect_apply( VJFrame **frames, VJFrameInfo *frameinfo, vjp_kf *kf, int selector, int *arguments, void *ptr )
{
	int entry = vj_effect_real_to_sequence( selector );
	int n_a   = vj_effect_get_num_params( selector );

	if( n_a > 10 )  {
		n_a = 10;
	}

	if( !frames || !frames[0] ) return VJE_NO_FRAMES;

	if( !vj_effect_initialized( selector, ptr ) ) {
		return VJE_NEED_INIT;
	}

	if( selector >= 500 ) {
		vj_effman_apply_plug_effect( frames, frameinfo, kf, arguments,n_a, entry, selector, ptr );
	}
	else
	{
		int isP = vj_effect_is_parallel(selector);
		if( vj_task_available() && isP > 0 ) {
			vj_task_set_from_frame( frames[0] );
			vj_task_set_int( selector );
			vj_task_set_param( entry, 11 ); // 10 + 1
			vj_task_set_ptr( (void*) kf );
			int i;
			for ( i = 0; i < n_a; i ++ )
				vj_task_set_param( arguments[i], i );
			
			vj_task_run( frames[0]->data,frames[1]->data, NULL, NULL, 3, (performer_job_routine) &vj_effman_apply_job );
		} 
		else {
			
			if( selector > 200 )	
				vj_effman_apply_video_effect(frames,kf,arguments, entry,selector);
			else
				vj_effman_apply_image_effect( frames,kf, arguments,entry, selector);
	
		}
	}
	return VJE_SUCCESS;
}


