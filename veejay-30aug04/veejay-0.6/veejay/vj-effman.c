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

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include "vj-effman.h"
#include "vj-effect.h"
#include "vj-global.h"
#include "sampleadm.h"
//#include "pattern.h"
#include "effects/magicoverlays.h"	/* used for blending */
#include "effects/rgbkey.h"
#include "subsample.h"
#include "vj-tag.h"

extern vj_effect *vj_effects[];

int vj_effman_get_subformat(vj_clip_instr * todo_info, int effect_id)
{
    return vj_effect_get_subformat(effect_id);
}


/**************** blend a frame **********************************/
int vj_effman_blend_frame(vj_video_block * data, int blend_type, int c1,
			  int c2)
{
    return 1;
}


int vj_effman_apply_row(uint8_t ** src_a, uint8_t ** src_b, int w, int h,
			int a, int b, int type)
{
    /* decide on type what to do */
    type = VJ_EFFECT_BLEND_BASECOLOR;
    a = 50;
    lumamagic_apply(src_a, src_b, w, h, type, a,50);
    return 0;
}

void vj_effman_apply_image_effect(vj_video_block *data, vj_clip_instr *todo_info, int *arg, int e, int entry) {
   int j;

   switch (e) {
	case VJ_IMAGE_EFFECT_RIPPLETV:
	 water_apply(data->src1,data->width,data->height,arg[0]);
	break;
	 case VJ_IMAGE_EFFECT_PENCILSKETCH:
	  pencilsketch_apply(data->src1,data->width,data->height,arg[0],arg[1],arg[2]);
	 break;
      case VJ_IMAGE_EFFECT_NOISEPENCIL:
	noisepencil_apply(data->src1,data->width,data->height,
		arg[0],arg[1],arg[2],arg[3]);
	break;
      case VJ_IMAGE_EFFECT_DIFF:
	diffimg_apply(data->src1, 
		      data->width, data->height, arg[0], arg[1],
		      arg[2]);
	break;

     case VJ_IMAGE_EFFECT_COMPLEXINVERT:
	complexinvert_apply(data->src1, data->width,data->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_COMPLEXSATURATE:
	complexsaturation_apply(data->src1, data->width,data->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
     case VJ_IMAGE_EFFECT_REFLECTION:
	reflection_apply(data->src1, data->width, data->height,
			 arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_FIBDOWNSCALE:
	for (j = 0; j < arg[1]; j++) {
	    fibdownscale_apply(data->src1, data->src2, data->width,
			       data->height, arg[0]);
	}
	break;
     case VJ_IMAGE_EFFECT_NOISEADD:
	noiseadd_apply(data->src1,data->width,data->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_CONTRAST:
	contrast_apply(data->src1, data->width, data->height,arg);
	break;
     case VJ_IMAGE_EFFECT_ENHANCEMASK:
	enhancemask_apply(data->src1,data->width,data->height,arg);
	break;
     case VJ_IMAGE_EFFECT_SOLARIZE:
	solarize_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DISTORTION:
	distortion_apply(data->src1, data->width, data->height, arg[0],
			 arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GAMMA:
	gamma_apply(
		    data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORADJUST:
	coloradjust_apply(data->src1, data->width, data->height, arg[0],
			  arg[1]);
	break;
     case VJ_IMAGE_EFFECT_MOTIONBLUR:
        motionblur_apply(data->src1,data->width,data->height,arg[0]);
	break;

     case VJ_IMAGE_EFFECT_MAGICSCRATCHER:
	magicscratcher_apply(data->src1, data->width, data->height, arg[0],
			     arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_CHROMASCRATCHER:
	chromascratcher_apply(data->src1, data->width, data->height,
			      arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SCRATCHER:
	scratcher_apply(data->src1, data->width, data->height,
			arg[0], arg[1], arg[2]);
	break;
     case VJ_IMAGE_EFFECT_KILLCHROMA:
	killchroma_apply(data->src1, data->width, data->height, arg[0]
			 );
	break;
     case VJ_IMAGE_EFFECT_MIRROR:
	mirrors2_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_MIRRORS:
	mirrors_apply(data->src1, data->width, data->height, arg[0],
		      arg[1]);
	break;
	case VJ_IMAGE_EFFECT_MAGICMIRROR:
	magicmirror_apply(data->src1,data->width,data->height,arg[0],arg[1],arg[2],arg[3]);
	break;
    case  VJ_IMAGE_EFFECT_RASTER:
	raster_apply(data->src1,data->width,data->height,arg[0]);
	break;
	case VJ_IMAGE_EFFECT_SWIRL:
	swirl_apply(data->src1,data->width,data->height,arg[0]);
	break;
    case VJ_IMAGE_EFFECT_RADIALBLUR:
	radialblur_apply(data->src1, data->width, data->height,arg[0],arg[1],arg[2]);
	break;
    case VJ_IMAGE_EFFECT_FISHEYE:
	fisheye_apply(data->src1,data->width,data->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_PIXELSMEAR:
	smear_apply(data->src1, data->width, data->height,arg[0],arg[1]);
	break;  
     case VJ_IMAGE_EFFECT_PIXELATE:  
	pixelate_apply(data->src1,data->width,data->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_UVCORRECT:
	uvcorrect_apply(data->src1, data->width, data->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
   case VJ_IMAGE_EFFECT_CHROMAPALETTE:
	chromapalette_apply(data->src1,data->width,data->height,arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
   case VJ_IMAGE_EFFECT_CHROMIUM:
	chromium_apply( data->src1, data->width, data->height, arg[0]);
	break;
	case VJ_IMAGE_EFFECT_OVERCLOCK:
	overclock_apply(data->src1, data->width, data->height,arg[0],arg[1]);break;
     case VJ_IMAGE_EFFECT_NEGATION:
	negation_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_POSTERIZE:
	posterize_apply(data->src1, data->width, data->height, arg[0],
			arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_DITHER:
	dither_apply(data->src1, data->width, data->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_EMBOSS:
	emboss_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_FLIP:
	flip_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_REVTV:
	revtv_apply(data->src1, data->width, data->height, arg[0],
		    arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_COLORSHIFT:
	colorshift_apply(data->src1, data->width, data->height, arg[0],
			 arg[1]); 
	break;
     case VJ_IMAGE_EFFECT_SOFTBLUR:
	softblur_apply(data->src1, data->width, data->height, arg[0],
		       arg[1]);
	break;
     case VJ_IMAGE_EFFECT_WIDTHMIRROR:
	widthmirror_apply(data->src1, data->width, data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_DICES:
	dices_apply(vj_effects[entry], data->src1, data->width,
		    data->height, arg[0]);
	break;
     case VJ_IMAGE_EFFECT_COLORTEST:
	color_apply(data->src1,data->width,data->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_RAWMAN:
	rawman_apply(data->src1, data->width, data->height, arg[0],
		     arg[1]);
	break;
     case VJ_IMAGE_EFFECT_RAWVAL:
	rawval_apply(data->src1, data->width, data->height, arg[0], arg[1],
		     arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SMUCK:
	smuck_apply(data->src1, data->src2, data->width, data->height,
		    arg[0]);
	break;
     case VJ_IMAGE_EFFECT_TRANSFORM:
	transform_apply(data->src1, data->src2, data->width, data->height,
			arg[0]);
	break;
     case VJ_IMAGE_EFFECT_BWSELECT:
	bwselect_apply(data->src1, data->width, data->height, arg[0], arg[1]);
	break;
     case VJ_IMAGE_EFFECT_GREYSELECT:
	greyselect_apply(data->src1, data->width, data->height,arg[0],arg[1],arg[2],arg[3]);
	break;
     case VJ_IMAGE_EFFECT_ISOLATE:
	isolate_apply(data->src1,data->width,data->height,
		arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
     case VJ_IMAGE_EFFECT_ROTOZOOM:
	rotozoom_apply(data->src1,
		       data->width,
		       data->height, arg[0], arg[1], arg[2], arg[3]);
	break;
     case VJ_IMAGE_EFFECT_SINOIDS:
	sinoids_apply(data->src1, data->width, data->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_AVERAGE:
	average_apply(data->src1,data->width,data->height,arg[0]);
	break;
     case VJ_IMAGE_EFFECT_RIPPLE:
	ripple_apply(data->src1,data->width,data->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_BATHROOM:
	bathroom_apply(data->src1,data->width,data->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_ZOOM:
	zoom_apply(data->src1, data->width, data->height,arg[0],arg[1],arg[2]);
	break;
     case VJ_IMAGE_EFFECT_CROSSPIXEL:
	crosspixel_apply(data->src1,data->width,data->height,arg[0],arg[1]);
	break;
     case VJ_IMAGE_EFFECT_DEINTERLACE:
	 deinterlace_apply( data->src1, data->width, data->height, arg[0]);
	 break;
     case VJ_IMAGE_EFFECT_SLICE:
	if(arg[2] > 0) { /* after x frames, randomize slice window*/
	   todo_info->tmp[0] ++; /* val / max frames */
	   if(todo_info->tmp[0] > arg[2]) { todo_info->tmp[0] = 0; todo_info->tmp[1] = 1; }
	} else { /* arg2 = off , copy arg*/
	     todo_info->tmp[1] = arg[1];
	}

	/* auto switch arg1 off after applying effect */
	slice_apply(data->src1,data->width,data->height,arg[0], todo_info->tmp[1]);
	todo_info->tmp[1] = 0;
	break;
    }
}


void vj_effman_apply_video_effect( vj_video_block *data, vj_clip_instr *todo_info, int *arg, int e,int entry) {
    switch(e) {
	//case VJ_VIDEO_EFFECT_CHANNELMIX:
	//  yuvchannelmix_apply(data->src1,data->src3,data->width,data->height,arg[0],arg[1],arg[2],
	//		arg[3],arg[4],arg[5],arg[6],arg[7]);
	//	break;
      case VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD:
	complexthreshold_apply(data->src1,data->src3,data->width,data->height,arg[0],arg[1],arg[2],arg[3],
		arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_DUPMAGIC:
	dupmagic_apply(data->src1, data->src3, data->width, data->height,
		       arg[0]);
	break;
      case VJ_VIDEO_EFFECT_LUMAMAGICK:
	lumamagic_apply(data->src1, data->src3, data->width, data->height,
			arg[0], arg[1],arg[2]);
   	break;
      case VJ_VIDEO_EFFECT_BINARYOVERLAY:
	binaryoverlay_apply(data->src1, data->src3,data->width,data->height,arg[0] );   break;
      case VJ_VIDEO_EFFECT_OVERLAYMAGIC:
	overlaymagic_apply(data->src1, data->src3, data->width,
			   data->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_MASK:
	simplemask_apply(data->src1,data->src3, data->width,data->height,arg[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_LUMAMASK:
	lumamask_apply(data->src1, data->src3, data->width,data->height,arg[0],arg[1]);
	break;
	case VJ_VIDEO_EFFECT_DISSOLVE:
	dissolve_apply(data->src1,data->src3,data->width,data->height,arg[0]);break; 
      case VJ_VIDEO_EFFECT_OPACITY:
	opacity_apply(data->src1, data->src3, data->width,
		      data->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLDSMOOTH:
	opacitythreshold_apply(data->src1, data->src3, data->width,
			       data->height, arg[0], arg[1],arg[2]);
	break;
      case VJ_VIDEO_EFFECT_THRESHOLD:
	opacityadv_apply(data->src1, data->src3, data->width,
			 data->height, arg[0], arg[1], arg[2]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEY:
	rgbkey_apply(data->src1, data->src3, data->width, data->height,
		     arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
	break;
      case VJ_VIDEO_EFFECT_KEYSELECT:
	keyselect_apply(data->src1,data->src3,data->width,data->height,
			arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
      case VJ_VIDEO_EFFECT_CHROMAMAGICK:
	chromamagick_apply(data->src1, data->src3, data->width,
			   data->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LUMABLEND:
	lumablend_apply(data->src1, data->src3, data->width, data->height,
			arg[0], arg[1], arg[2], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_LUMAKEY:
	lumakey_apply(data->src1, data->src3, data->width, data->height,
		      arg[4], arg[1], arg[2], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_DIFF:
	diff_apply( vj_effects[entry]->vjed, data->src1, data->src3,
		   data->width, data->height, arg[0], arg[1],arg[2],arg[3]);
	break;

      case VJ_VIDEO_EFFECT_WHITEFRAME:
	whiteframe_apply(data->src1, data->src3, data->width, data->height,
			 arg[1], arg[2], arg[0]);
	break;
      case VJ_VIDEO_EFFECT_MTRACER:
	mtracer_apply(data->src1, data->src3, data->width, data->height,
		      arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_TRACER:
	tracer_apply(data->src1, data->src3, data->width,
		     data->height, arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_CAROT:
	transcarot_apply(data->src1, data->src3, data->width, data->height,
			 arg[2], arg[3], arg[4], arg[5], arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_LINE:
	transline_apply(data->src1, data->src3, data->width, data->height,
			arg[2], arg[1], arg[0], arg[3]);
	break;
      case VJ_VIDEO_EFFECT_TRANSOP:
	transop_apply(data->src1, data->src3, arg[1], arg[2], arg[3],
		      arg[4], arg[5], arg[6], data->width, data->height,
		      arg[0]);
	break;
      case VJ_VIDEO_EFFECT_COMPLEXSYNC:
	if(arg[1] == 1) { /* auto increment as option in effect*/
	   if(arg[2]==0) arg[2]=1;   
	   todo_info->tmp[0] += (arg[0]/arg[2] )+1; /* val / max frames */
	   if(todo_info->tmp[0] > data->height-2) todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[0];
	}
	complexsync_apply(data->src1, data->src3,data->width, data->height, 
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

	colorfadergb_apply(data->src1, data->width, data->height,
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


	colorfade_apply(data->src1, data->width, data->height,
			todo_info->tmp[0], arg[1]);
	break;
	case VJ_VIDEO_EFFECT_VBAR:
		vbar_apply(data->src1,data->src3,data->width,data->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
		break;
      case VJ_VIDEO_EFFECT_3BAR:
	bar_apply(data->src1,data->src3,data->width,data->height,arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
      case VJ_VIDEO_EFFECT_SLIDINGDOOR:
	if(arg[2] == 1) { /* auto increment as option in effect*/
	   todo_info->tmp[0] ++; /* val / max frames */
	   if(todo_info->tmp[0] > (data->height/16)) todo_info->tmp[0] = 1;
	} else { /* arg1 = off , copy arg*/
	     todo_info->tmp[0] = arg[1];
	}

	slidingdoor_apply(data->src1, data->src3, data->width,
			  data->height, arg[0], todo_info->tmp[0]);
	break;
      case VJ_VIDEO_EFFECT_WIPE:
	wipe_apply(data->src1, data->src3, data->width, data->height,
		   arg[0], arg[1]);
	break;
      case VJ_VIDEO_EFFECT_RGBKEYSMOOTH:
	rgbkeysmooth_apply(data->src1, data->src3, data->width,
			   data->height, arg[0], arg[1], arg[2], arg[3],
			   arg[4]);
	break;
      case VJ_VIDEO_EFFECT_SPLIT:
	split_apply(data->src1, data->src3, data->width, data->height,
		    arg[0], arg[1]);
	break;
       case VJ_VIDEO_EFFECT_BORDERS:
	borders_apply(data->src1, data->width, data->height, arg[0],
		      arg[1]);
	break;
      case VJ_VIDEO_EFFECT_FRAMEBORDER:
	frameborder_apply(data->src1, data->src3, data->width,
			  data->height, arg[0]);
	break;
      case VJ_VIDEO_EFFECT_TRANSBLEND:
	transblend_apply(data->src1, data->src3, data->width, data->height,
			 arg[0], arg[1], arg[2], arg[3], arg[4], arg[5],
			 arg[6]);
	break;
      default:
	break;
    }

}

/* apply first frame *********************************************/
int vj_effman_apply_first(vj_clip_instr * todo_info,
			  vj_video_block * data, int e, int c, int n_frame)
{
    int arg[CLIP_MAX_PARAMETERS] = {0,0,0,0,0,0,0,0,0};
    int args;
    int *p = &arg[0];

    int entry = vj_effect_real_to_sequence(e);
    args = vj_effect_get_num_params(e);

    if (args == VJ_EFFECT_ERROR)
	return -1;
    if (!data || !data->src1)
	return -1;

    

    if (todo_info->is_tag) {
	if (!vj_tag_get_all_effect_args(todo_info->clip_id, c, p, args))
	    return -1;
    } else {
	if (!clip_get_all_effect_arg(todo_info->clip_id, c, p, args,n_frame))
	    return -1;
    }

	/* picture in a picture stuff ?? we could do that easily*/

     if( !vj_effect_initialized(e) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Internal error: Effect %d is not initialized",e);
		return 1;
	}

    if(e>200) {
	vj_effman_apply_video_effect( data, todo_info, arg,e,entry);
	}
    else {
	vj_effman_apply_image_effect( data, todo_info, arg,e,entry);
	}


    return 1;
}

