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
#include <libvje/plugload.h>
extern vj_effect *vj_effects[]; 

void vj_effman_apply_ff_effect(
	VJFrame **frames,
	VJFrameInfo *frameinfo,
	vjp_kf *todo_info,
	int *arg,
	int entry,
	int fx_id)

{
	plug_control( entry - MAX_EFFECTS, arg );
	
	plug_process( frames[0],frames[1],entry - MAX_EFFECTS, get_ffmpeg_pixfmt(frames[0]->format) );
}

void vj_effman_apply_image_effect(
	VJFrame **frames,
	VJFrameInfo *frameinfo,
	vjp_kf *todo_info,
	int *arg,
	int entry,
	int e)

	{

   int j;

   switch (e) {
	case VJ_IMAGE_EFFECT_CONSTANTBLEND:
		constantblend_apply( frames[0], frameinfo->width,
			frameinfo->height, arg[0], arg[1], arg[2]);
		break;
	case VJ_IMAGE_EFFECT_FLARE:
		flare_apply( frames[0], frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_PHOTOPLAY:
		photoplay_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2]);
			break;
	case VJ_IMAGE_EFFECT_MASKSTOP:
		maskstop_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_CUTSTOP:
		cutstop_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR4:
		neighbours4_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR5:
		neighbours5_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR2:
		neighbours2_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR:
		neighbours_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NEIGHBOUR3:
	neighbours3_apply(frames[0],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2]);
		break;

	case VJ_IMAGE_EFFECT_RIPPLETV:
	 water_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
	 case VJ_IMAGE_EFFECT_PENCILSKETCH:
	  pencilsketch_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	 break;
      case VJ_IMAGE_EFFECT_NOISEPENCIL:
	noisepencil_apply(frames[0],frameinfo->width,frameinfo->height,
		arg[0],arg[1],arg[2],arg[3]);
	break;
      case VJ_IMAGE_EFFECT_DIFF:
	diffimg_apply(frames[0], 
		      frameinfo->width, frameinfo->height, arg[0], arg[1],
		      arg[2]);
	break;

     case VJ_IMAGE_EFFECT_COMPLEXINVERT:
	complexinvert_apply(frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_COMPLEXSATURATE:
	complexsaturation_apply(frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
     case VJ_IMAGE_EFFECT_REFLECTION:
	reflection_apply(frames[0], frameinfo->width, frameinfo->height,
			 arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_FIBDOWNSCALE:
	for (j = 0; j < arg[1]; j++) {
	    fibdownscale_apply(frames[0], frames[0], frameinfo->width,
			       frameinfo->height, arg[0]);
	}
	break;
     case VJ_IMAGE_EFFECT_NOISEADD:
	noiseadd_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_CONTRAST:
	contrast_apply(frames[0], frameinfo->width, frameinfo->height,arg);
	break;
     case VJ_IMAGE_EFFECT_ENHANCEMASK:
	enhancemask_apply(frames[0],frameinfo->width,frameinfo->height,arg);
	break;
     case VJ_IMAGE_EFFECT_SOLARIZE:
	solarize_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DISTORTION:
	distortion_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
			 arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GAMMA:
	gamma_apply(
		    frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORADJUST:
	coloradjust_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
			  arg[1]);
	break;
     case VJ_IMAGE_EFFECT_MOTIONBLUR:
        motionblur_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;

     case VJ_IMAGE_EFFECT_MAGICSCRATCHER:
	magicscratcher_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
			     arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_CHROMASCRATCHER:
	chromascratcher_apply(frames[0], frameinfo->width, frameinfo->height,
			      arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SCRATCHER:
	scratcher_apply(frames[0], frameinfo->width, frameinfo->height,
			arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_KILLCHROMA:
	killchroma_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]
			 );
	break;
     case VJ_IMAGE_EFFECT_MIRROR:
	mirrors2_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_MIRRORS:
	mirrors_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
		      arg[1]);
	break;
	case VJ_IMAGE_EFFECT_MAGICMIRROR:
	magicmirror_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
	break;
    case  VJ_IMAGE_EFFECT_RASTER:
	raster_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;
	case VJ_IMAGE_EFFECT_SWIRL:
	swirl_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;
    case VJ_IMAGE_EFFECT_RADIALBLUR:
	radialblur_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1],arg[2]);
	break;
    case VJ_IMAGE_EFFECT_FISHEYE:
	fisheye_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_PIXELSMEAR:
	smear_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1]);
	break;  
     case VJ_IMAGE_EFFECT_PIXELATE:  
	pixelate_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_UVCORRECT:
	uvcorrect_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
   case VJ_IMAGE_EFFECT_CHROMAPALETTE:
	chromapalette_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
   case VJ_IMAGE_EFFECT_CHROMIUM:
	chromium_apply( frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
	case VJ_IMAGE_EFFECT_CARTONIZE:
		cartonize_apply( frames[0], frameinfo->width,frameinfo->height,
			arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_VIDBLOB:
		blob_apply( frames[0],frameinfo->width,frameinfo->height,
			arg[0],arg[1],arg[2],arg[3] );
			break;
	case VJ_IMAGE_EFFECT_VIDBOIDS:
		boids_apply( frames[0],frameinfo->width,frameinfo->height,
			arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],arg[7] );
		break;
	case VJ_IMAGE_EFFECT_GHOST:
		ghost_apply( frames[0], frameinfo->width,frameinfo->height,arg[0]);
		break;
	case VJ_IMAGE_EFFECT_MORPHOLOGY:
		morphology_apply( frames[0], frameinfo->width,frameinfo->height,
			arg[0],arg[1],arg[2] );
		break;
	case VJ_IMAGE_EFFECT_COLMORPH:
		colmorphology_apply( frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_IMAGE_EFFECT_NERVOUS:
		nervous_apply( frames[0], frameinfo->width, frameinfo->height,
			arg[0]); break;
   case VJ_IMAGE_EFFECT_OVERCLOCK:
	overclock_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1]);
	break;
	case VJ_IMAGE_EFFECT_COLORHIS:
		colorhis_apply( frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3] );
		break;
	case VJ_IMAGE_EFFECT_AUTOEQ:
		autoeq_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
	case VJ_IMAGE_EFFECT_BALTANTV:
		baltantv_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1]);
		break;
	case VJ_IMAGE_EFFECT_CHAMELEON:
		chameleon_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1]);
		break;
	case VJ_IMAGE_EFFECT_TIMEDISTORT:
		timedistort_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
		break;
     case VJ_IMAGE_EFFECT_NEGATION:
	negation_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
	case VJ_IMAGE_EFFECT_COLFLASH:
	colflash_apply(frames[0], frameinfo->width,frameinfo->height,arg[0],
			arg[1],arg[2],arg[3],arg[4] );
	break;
     case VJ_IMAGE_EFFECT_COLORMAP:
	colormap_apply(frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_POSTERIZE:
	posterize_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
			arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_DITHER:
	dither_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_EMBOSS:
	emboss_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_FLIP:
	flip_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_REVTV:
	revtv_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
		    arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_COLORSHIFT:
	colorshift_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
			 arg[1]); 
	break;
     case VJ_IMAGE_EFFECT_SOFTBLUR:
	softblur_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_WIDTHMIRROR:
	widthmirror_apply(frames[0], frameinfo->width, frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DICES:
	dices_apply(vj_effects[entry], frames[0], frameinfo->width,
		    frameinfo->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORTEST:
	color_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_RAWMAN:
	rawman_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_RAWVAL:
	rawval_apply(frames[0], frameinfo->width, frameinfo->height, arg[0], arg[1],
		     arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SMUCK:
	smuck_apply(frames[0], frames[0], frameinfo->width, frameinfo->height,
		    arg[0]);
	break;
     case VJ_IMAGE_EFFECT_TRANSFORM:
	transform_apply(frames[0], frames[0], frameinfo->width, frameinfo->height,
			arg[0]);
	break;
     case VJ_IMAGE_EFFECT_BWSELECT:
	bwselect_apply(frames[0], frameinfo->width, frameinfo->height, arg[0], arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GREYSELECT:
	greyselect_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
	break;
     case VJ_IMAGE_EFFECT_ISOLATE:
	isolate_apply(frames[0],frameinfo->width,frameinfo->height,
		arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_ROTOZOOM:
	rotozoom_apply(frames[0],
		       frameinfo->width,
		       frameinfo->height, arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SINOIDS:
	sinoids_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_AVERAGE:
	average_apply(frames[0],frameinfo->width,frameinfo->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_RIPPLE:
	ripple_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_BATHROOM:
	bathroom_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_RGBCHANNEL:
	rgbchannel_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
    case VJ_IMAGE_EFFECT_GOOM:
	goomfx_apply( frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_ZOOM:
	zoom_apply(frames[0], frameinfo->width, frameinfo->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_CROSSPIXEL:
	crosspixel_apply(frames[0],frameinfo->width,frameinfo->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_DEINTERLACE:
	 deinterlace_apply( frames[0], frameinfo->width, frameinfo->height, arg[0]);
	 break;
	case VJ_IMAGE_EFFECT_MOTIONMAP:
	motionmap_apply( frames[0], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_SLICE:
	if(arg[2] > 0) { 
	   todo_info->tmp[0] ++; 
	   if(todo_info->tmp[0] > arg[2]) { todo_info->tmp[0] = 0; todo_info->tmp[1] = 1; }
	} else { 
	     todo_info->tmp[1] = arg[1];
	}

	slice_apply(frames[0],frameinfo->width,frameinfo->height,arg[0], todo_info->tmp[1]);
	todo_info->tmp[1] = 0;
	break;
    }
}


void vj_effman_apply_video_effect( VJFrame **frames, VJFrameInfo *frameinfo ,vjp_kf *todo_info,int *arg, int entry, int e) {

    switch(e) {
	case VJ_VIDEO_EFFECT_CHAMBLEND:
		chameleonblend_apply(frames[0],frames[1], frameinfo->width,frameinfo->height,arg[0],arg[1]);
		break;
	case VJ_VIDEO_EFFECT_EXTDIFF:
		differencemap_apply( frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_VIDEO_EFFECT_EXTTHRESHOLD:
		threshold_apply( frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0],arg[1]);
		break;
	case VJ_VIDEO_EFFECT_VIDEOWALL:
		videowall_apply(frames[0],frames[1],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
		break;
	case VJ_VIDEO_EFFECT_VIDEOPLAY:
		videoplay_apply(frames[0],frames[1],frameinfo->width,
			frameinfo->height,arg[0],arg[1],arg[2]);
		break;
	case VJ_VIDEO_EFFECT_TRIPPLICITY:
	tripplicity_apply(frames[0],frames[1], 
		frameinfo->width,frameinfo->height, arg[0],arg[1],arg[2]);
		break;
      case VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD:
	complexthreshold_apply(frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],
		arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_DUPMAGIC:
	dupmagic_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		       arg[0]);
	break;
      case VJ_VIDEO_EFFECT_LUMAMAGICK:
	lumamagic_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
			arg[0], arg[1],arg[2]);
   	break;
      case VJ_VIDEO_EFFECT_BINARYOVERLAY:
	binaryoverlay_apply(frames[0], frames[1],frameinfo->width,frameinfo->height,arg[0] );   break;
      case VJ_VIDEO_EFFECT_OVERLAYMAGIC:
	overlaymagic_apply(frames[0], frames[1], frameinfo->width,
			   frameinfo->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_MASK:
	simplemask_apply(frames[0],frames[1], frameinfo->width,frameinfo->height,arg[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_LUMAMASK:
	lumamask_apply(frames[0], frames[1], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2]);
	break;
	case VJ_VIDEO_EFFECT_DISSOLVE:
	dissolve_apply(frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0]);break; 
      case VJ_VIDEO_EFFECT_OPACITY:
	opacity_apply(frames[0], frames[1], frameinfo->width,
		      frameinfo->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLDSMOOTH:
	opacitythreshold_apply(frames[0], frames[1], frameinfo->width,
			       frameinfo->height, arg[0], arg[1],arg[2]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLD:
	opacityadv_apply(frames[0], frames[1], frameinfo->width,
			 frameinfo->height, arg[0], arg[1], arg[2]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEY:
	rgbkey_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		     arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_KEYSELECT:
	keyselect_apply(frames[0],frames[1],frameinfo->width,frameinfo->height,
			arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
      case VJ_VIDEO_EFFECT_CHROMAMAGICK:
	chromamagick_apply(frames[0], frames[1], frameinfo->width,
			   frameinfo->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LUMABLEND:
	lumablend_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
			arg[0], arg[1], arg[2], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_LUMAKEY:
	lumakey_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		      arg[4], arg[1], arg[2], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_DIFF:
	diff_apply( vj_effects[entry]->user_data, frames[0], frames[1],
		   frameinfo->width, frameinfo->height, arg[0], arg[1],arg[2]);
	break;

      case VJ_VIDEO_EFFECT_WHITEFRAME:
	whiteframe_apply(frames[0], frames[1], frameinfo->width, frameinfo->height);
	break;
      case VJ_VIDEO_EFFECT_MTRACER:
	mtracer_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		      arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_TRACER:
	tracer_apply(frames[0], frames[1], frameinfo->width,
		     frameinfo->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_CAROT:
	transcarot_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
			 arg[2], arg[3], arg[4], arg[5], arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LINE:
	transline_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
			arg[2], arg[1], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_TRANSOP:
	transop_apply(frames[0], frames[1], arg[1], arg[2], arg[3],
		      arg[4], arg[5], arg[6], frameinfo->width, frameinfo->height,
		      arg[0]);
	break;
      case VJ_VIDEO_EFFECT_COMPLEXSYNC:
	if(arg[1] == 1) { /* auto increment as option in effect*/
	   if(arg[2]==0) arg[2]=1;   
	   todo_info->tmp[0] += (arg[0]/arg[2] )+1; /* val / max frames */
	   if(todo_info->tmp[0] > frameinfo->height-2) todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[0];
	}
	complexsync_apply(frames[0], frames[1],frameinfo->width, frameinfo->height, 
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

	colorfadergb_apply(frames[0], frameinfo->width, frameinfo->height,
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


	colorfade_apply(frames[0], frameinfo->width, frameinfo->height,
			todo_info->tmp[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_VBAR:
		vbar_apply(frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
		break;
      case VJ_VIDEO_EFFECT_3BAR:
	bar_apply(frames[0],frames[1],frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
      case VJ_VIDEO_EFFECT_SLIDINGDOOR:
	if(arg[1] == 1) { /* auto increment as option in effect*/
	   todo_info->tmp[0] ++; /* val / max frames */
	   if(todo_info->tmp[0] >= (frameinfo->height/16))
		   todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[0];
	}

	slidingdoor_apply(frames[0], frames[1], frameinfo->width,
			  frameinfo->height, todo_info->tmp[0]  );
	break;
      case VJ_VIDEO_EFFECT_WIPE:
	wipe_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		   arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEYSMOOTH:
	rgbkeysmooth_apply(frames[0], frames[1], frameinfo->width,
			   frameinfo->height, arg[0], arg[1], arg[2], arg[3],
			   arg[4],arg[5]);
	break;
      case VJ_VIDEO_EFFECT_SPLIT:
	split_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
		    arg[0], arg[1]);
	break;
       case VJ_VIDEO_EFFECT_BORDERS:
	borders_apply(frames[0], frameinfo->width, frameinfo->height, arg[0],
		      arg[1]);
	break;
      case VJ_VIDEO_EFFECT_FRAMEBORDER:
	frameborder_apply(frames[0], frames[1], frameinfo->width,
			  frameinfo->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_TRANSBLEND:
	transblend_apply(frames[0], frames[1], frameinfo->width, frameinfo->height,
			 arg[0], arg[1], arg[2], arg[3], arg[4], arg[5],
			 arg[6]);
	break;
	case VJ_VIDEO_EFFECT_PICINPIC:
	picinpic_apply( vj_effects[entry]->user_data,frames[0], frames[1], frameinfo->width, frameinfo->height,
			arg[0], arg[1], arg[2], arg[3] );
	break;
	case VJ_VIDEO_EFFECT_RADIOACTIVE:
	radioactivetv_apply( frames[0],frames[1], frameinfo->width,frameinfo->height,arg[0],arg[1],arg[2],arg[3]);
	break;
   
      default:
	break;
    }

}

int vj_effect_prepare( VJFrame *frame, int selector)
{
	veejay_msg(VEEJAY_MSG_DEBUG, "Found FX %d", selector);
	if(selector == VJ_VIDEO_EFFECT_DIFF && vj_effect_has_cb(selector))
	{
		int i = vj_effect_real_to_sequence( selector );
		veejay_msg(VEEJAY_MSG_DEBUG,"internal id = %d", i );
		if( vj_effects[i]->user_data != NULL)
		{
			diff_prepare(
				(void*) vj_effects[i]->user_data,
				frame->data,
				frame->width,
				frame->height );
	
			return 1;
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR,"There is currently no FX that needs a background image");
	}
	return 0;
}


int	vj_effect_apply( VJFrame **frames, VJFrameInfo *frameinfo, vjp_kf *kf, int selector, int *arguments )
{
	int entry = vj_effect_real_to_sequence( selector );
	int n_a   = vj_effect_get_num_params( selector );

	if( !frames || !frames[0] ) return VJE_NO_FRAMES;

	if(!vj_effect_initialized(selector))
	{
		return VJE_NEED_INIT;
	}

	if( selector >= 500 )
		vj_effman_apply_ff_effect( frames, frameinfo, kf, arguments, entry, selector );
	else
	{		
		if( selector > 200 )	
			vj_effman_apply_video_effect(frames,frameinfo,kf,arguments, entry,selector);
		else
			vj_effman_apply_image_effect( frames, frameinfo, kf, arguments,entry, selector);
	}
	return VJE_SUCCESS;
}


