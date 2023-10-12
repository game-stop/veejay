/* 
 * veejay  
 *
 * Copyright (C) 2000-2019 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
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
#include <stdio.h>
#include <string.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/avcommon.h>
#include <veejaycore/vjmem.h>
#include "effects/common.h"
#include <libplugger/plugload.h>
#include <veejaycore/vims.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define MAX_EFFECTS 4096
#define NUM_CHAINS 2
#define MAX_ENTRY_PER_CHAIN 20

#define CHECK_BOUNDS(id) {\
    if( id < 0 || id >= MAX_EFFECTS )\
        return 0;\
}


typedef struct {
    void  (*fx_request_set_private)(void *ptr, void *priv);
    int fx_id;
    void *ptr;
} used_by;

typedef struct {
    int fx_id;
    void *priv;
    used_by used[MAX_ENTRY_PER_CHAIN];
} vj_fx_priv_map_t;

static struct {
    vj_effect* (*fx_init)(int w, int h);
    void* (*fx_malloc)(int w, int h);
    void  (*fx_free)(void *ptr);
    int   (*prepare)(void *ptr, VJFrame *frame);
    uint8_t* (*get_bg)(void *ptr, unsigned int plane);
    void  (*fx_filter)(void *ptr, VJFrame *frame, int *args);
    void  (*fx_process)(void *ptr, VJFrame *A, VJFrame *B, int *args);
    int   (*fx_transition_ready)(void *ptr, int w, int h );
    int   (*fx_request_fx)(void);
    void  (*fx_request_set_private)(void *ptr, void *priv);
    int   fx_id;
} vj_fx[] = {
    { motionmap_init,motionmap_malloc,motionmap_free,motionmap_prepare,NULL,motionmap_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_MOTIONMAP },
    { zoom_init, zoom_malloc, zoom_free, NULL, NULL, zoom_apply, NULL, NULL,NULL,NULL,VJ_IMAGE_EFFECT_ZOOM },
    { widthmirror_init, NULL, NULL, NULL, NULL, widthmirror_apply, NULL, NULL, NULL,NULL,VJ_IMAGE_EFFECT_WIDTHMIRROR },
    { whiteframe_init, NULL, NULL, NULL,NULL, NULL, whiteframe_apply,NULL, NULL,NULL,VJ_VIDEO_EFFECT_WHITEFRAME },
    { waterrippletv_init, waterrippletv_malloc, waterrippletv_free,NULL,NULL,waterrippletv_apply, NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_RIPPLETV },
    { water_init,water_malloc,water_free,NULL,NULL,NULL,water_apply, NULL,NULL,NULL, VJ_VIDEO_EFFECT_RIPPLETV },
    { videowall_init,videowall_malloc,videowall_free,NULL,NULL,NULL,videowall_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_VIDEOWALL },
    { videoplay_init,videoplay_malloc,videoplay_free,NULL,NULL,NULL,videoplay_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_VIDEOPLAY },
    { uvcorrect_init,uvcorrect_malloc,uvcorrect_free,NULL,NULL,uvcorrect_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_UVCORRECT},
    { tripplicity_init,NULL,NULL,NULL,NULL,NULL,tripplicity_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_TRIPPLICITY},
    { travelmatte_init,NULL,NULL,NULL,NULL,NULL,travelmatte_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_TRAVELMATTE},
    { transform_init,NULL,NULL,NULL,NULL,transform_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_TRANSFORM},
    { tracer_init,tracer_malloc,tracer_free,NULL,NULL,NULL,tracer_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_TRACER},
    { toalpha_init,toalpha_malloc,toalpha_free,NULL,NULL,toalpha_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_TOALPHA},
    { timedistort_init,timedistort_malloc,timedistort_free,NULL,NULL,timedistort_apply,NULL,NULL,timedistort_request_fx,timedistort_set_motionmap, VJ_IMAGE_EFFECT_TIMEDISTORT },
    { threshold_init,threshold_malloc,threshold_free,NULL,NULL,NULL,threshold_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_THRESHOLD },
    { swirl_init,swirl_malloc,swirl_free,NULL,NULL,swirl_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SWIRL },
    { stretch_init,NULL,NULL,NULL,NULL,stretch_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CREATIVESTRETCH },
    { squares_init,NULL,NULL,NULL,NULL,squares_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SQUARES},
    { split_init,split_malloc,split_free,NULL,NULL,NULL,split_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_SPLIT},
    { solarize_init,NULL,NULL,NULL,NULL,solarize_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SOLARIZE},
    { softblur_init,NULL,NULL,NULL,NULL,softblur_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SOFTBLUR },
    { smuck_init,smuck_malloc,smuck_free,NULL,NULL,smuck_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SMUCK },
    { smear_init,smear_malloc,smear_free,NULL,NULL,smear_apply,NULL,NULL,smear_request_fx,smear_set_motionmap, VJ_IMAGE_EFFECT_PIXELSMEAR },
    { slicer_init,slicer_malloc,slicer_free,NULL,NULL,NULL,slicer_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_SLICER },
    { slice_init,slice_malloc,slice_free,NULL,NULL,slice_apply,NULL,NULL,slice_request_fx, slice_set_motionmap, VJ_IMAGE_EFFECT_SLICE },
    { sinoids_init,sinoids_malloc,sinoids_free,NULL,NULL,sinoids_apply,NULL,NULL,sinoids_request_fx,sinoids_set_motionmap, VJ_IMAGE_EFFECT_SINOIDS },
    { scratcher_init,scratcher_malloc,scratcher_free,NULL,NULL,scratcher_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SCRATCHER},
    { rotozoom_init,rotozoom_malloc,rotozoom_free,NULL,NULL,rotozoom_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ROTOZOOM },
    { ripple_init,ripple_malloc,ripple_free,NULL,NULL,ripple_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_RIPPLE },
    { rgbkeysmooth_init,rgbkeysmooth_malloc,rgbkeysmooth_free,NULL,NULL,NULL,rgbkeysmooth_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_RGBKEYSMOOTH },
    { rgbkey_init,NULL,NULL,NULL,NULL,NULL,rgbkey_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_RGBKEY },
    { rgbchannel_init,NULL,NULL,NULL,NULL,rgbchannel_apply,NULL, NULL,NULL,NULL, VJ_IMAGE_EFFECT_RGBCHANNEL},
    { revtv_init,NULL,NULL,NULL,NULL,revtv_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_REVTV },
    { reflection_init, reflection_malloc,reflection_free,NULL,NULL, reflection_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_REFLECTION },
    { rawval_init,NULL,NULL,NULL,NULL,rawval_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_RAWVAL },
    { rawman_init,NULL,NULL,NULL,NULL,rawman_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_RAWMAN },
    { raster_init,raster_malloc,raster_free,NULL,NULL,raster_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_RASTER },
    { randnoise_init,NULL,NULL,NULL,NULL,randnoise_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_RANDNOISE },
    { radioactivetv_init,radioactivetv_malloc,radioactivetv_free,NULL,NULL,NULL,radioactivetv_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_RADIOACTIVE },
    { radialblur_init,radialblur_malloc,radialblur_free,NULL,NULL,radialblur_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_RADIALBLUR },
    { radcor_init,radcor_malloc,radcor_free,NULL,NULL,radcor_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_LENSCORRECTION },
    { posterize_init,NULL,NULL,NULL,NULL,posterize_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_POSTERIZE },
    { posterize2_init,NULL,NULL,NULL,NULL,posterize2_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_POSTERIZE2 },
    { porterduff_init,NULL,NULL,NULL,NULL,NULL,porterduff_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_PORTERDUFF },
    { pixelsort_init,pixelsort_malloc,pixelsort_free,NULL,NULL,pixelsort_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PIXELSORT },
    { pixelsortalpha_init,NULL,NULL,NULL,NULL,pixelsortalpha_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PIXELSORTALPHA },
    { pixelate_init,NULL,NULL,NULL,NULL,pixelate_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PIXELATE },
    { picinpic_init,picinpic_malloc,picinpic_free,NULL,NULL,NULL,picinpic_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_PICINPIC },
    { photoplay_init,photoplay_malloc,photoplay_free,NULL,NULL,photoplay_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_PHOTOPLAY },
    { perspective_init,perspective_malloc,perspective_free,NULL,NULL,perspective_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PERSPECTIVE },
    { pencilsketch_init,NULL,NULL,NULL,NULL,pencilsketch_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PENCILSKETCH },
    { pencilsketch2_init,pencilsketch2_malloc,pencilsketch2_free,NULL,NULL,pencilsketch2_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_PENCILSKETCH2 },
    { overclock_init,overclock_malloc,overclock_free,NULL,NULL,overclock_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_OVERCLOCK },
    { opacitythreshold_init,NULL,NULL,NULL,NULL,NULL,opacitythreshold_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_THRESHOLDSMOOTH },
    { opacity_init,NULL,NULL,NULL,NULL,NULL,opacity_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_OPACITY },
    { opacityadv_init,NULL,NULL,NULL,NULL,NULL,opacityadv_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_THRESHOLD },
    { noisepencil_init,noisepencil_malloc,noisepencil_free,NULL,NULL,noisepencil_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NOISEPENCIL },
    { noiseadd_init,noiseadd_malloc,noiseadd_free,NULL,NULL,noiseadd_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NOISEADD },
    { nervous_init,nervous_malloc,nervous_free,NULL,NULL,nervous_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NERVOUS },
    { neighbours_init,neighbours_malloc,neighbours_free,NULL,NULL,neighbours_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEIGHBOUR },
    { neighbours2_init,neighbours2_malloc,neighbours2_free,NULL,NULL,neighbours2_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEIGHBOUR2 },
    { neighbours3_init,neighbours3_malloc,neighbours3_free,NULL,NULL,neighbours3_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEIGHBOUR3 },
    { neighbours4_init,neighbours4_malloc,neighbours4_free,NULL,NULL,neighbours4_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEIGHBOUR4 },
    { neighbours5_init,neighbours5_malloc,neighbours5_free,NULL,NULL,neighbours5_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEIGHBOUR5 },
    { negation_init,NULL,NULL,NULL,NULL,negation_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_NEGATION },
    { rainbowshift_init, NULL,NULL,NULL,NULL, rainbowshift_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_RAINBOWSHIFT },
    { vintagefilm_init,NULL,NULL,NULL,NULL, vintagefilm_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_VINTAGEFILM },
    { mirrordistortion_init,mirrordistortion_malloc,mirrordistortion_free,NULL,NULL,mirrordistortion_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_MIRRORDISTORTION },
    { shutterdrag_init,shutterdrag_malloc,shutterdrag_free,NULL,NULL,shutterdrag_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_SHUTTERDRAG },
    { pointilism_init,pointilism_malloc,pointilism_free,NULL,NULL,pointilism_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_POINTILISM },
    { negatechannel_init,NULL,NULL,NULL,NULL,negatechannel_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_NEGATECHANNEL },
    { mtracer_init,mtracer_malloc,mtracer_free,NULL,NULL,NULL,mtracer_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_MTRACER },
    { overlaymagic_init,NULL,NULL,NULL,NULL,NULL,overlaymagic_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_OVERLAYMAGIC },
    { motionblur_init,motionblur_malloc,motionblur_free,NULL,NULL,motionblur_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_MOTIONBLUR },
    { morphology_init,morphology_malloc,morphology_free,NULL,NULL,morphology_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_MORPHOLOGY },
    { mixtoalpha_init,mixtoalpha_malloc,mixtoalpha_free,NULL,NULL,NULL,mixtoalpha_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_MIXTOALPHA }, 
    { mirrors_init,mirrors_malloc,mirrors_free,NULL,NULL,mirrors_apply,NULL,NULL,mirrors_request_fx,mirrors_set_motionmap, VJ_IMAGE_EFFECT_MIRRORS },
    { mirrors2_init,NULL,NULL,NULL,NULL,mirrors2_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_MIRROR },
    { medianfilter_init,medianfilter_malloc,medianfilter_free,NULL,NULL,medianfilter_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_MEDIANFILTER},
    { masktransition_init,NULL,NULL,NULL,NULL,NULL,masktransition_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_MASKTRANSITION },
    { maskstop_init,maskstop_malloc,maskstop_free,NULL,NULL,maskstop_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_MASKSTOP },
    { simplemask_init,NULL,NULL,NULL,NULL,NULL,simplemask_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_MASK },
    { shapewipe_init,shapewipe_malloc,shapewipe_free,NULL,NULL,NULL,shapewipe_apply,shapewipe_ready,NULL,NULL,VJ_VIDEO_EFFECT_SHAPEWIPE },
    { magicmirror_init,magicmirror_malloc,magicmirror_free,NULL,NULL,magicmirror_apply,NULL,NULL,magicmirror_request_fx,magicmirror_set_motionmap,VJ_IMAGE_EFFECT_MAGICMIRROR },
    { magicscratcher_init,magicscratcher_malloc,magicscratcher_free,NULL,NULL,magicscratcher_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_MAGICSCRATCHER },
    { overlaymagicalpha_init,NULL,NULL,NULL,NULL,NULL,overlaymagicalpha_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_MAGICOVERLAYALPHA },
    { lumamask_init,lumamask_malloc,lumamask_free,NULL,NULL,NULL,lumamask_apply,NULL,lumamask_requests_fx, lumamask_set_motionmap, VJ_VIDEO_EFFECT_LUMAMASK },
    { lumamagick_init,NULL,NULL,NULL,NULL,NULL,lumamagick_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_LUMAMAGICK },
    { lumakey_init,NULL,NULL,NULL,NULL,NULL,lumakey_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_LUMAKEY },
    { lumakeyalpha_init, NULL,NULL,NULL,NULL,NULL,lumakeyalpha_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_LUMAKEYALPHA },
    { lumablend_init, NULL,NULL,NULL,NULL,NULL, lumablend_apply, NULL,NULL,NULL, VJ_VIDEO_EFFECT_LUMABLEND },
    { levelcorrection_init,NULL,NULL,NULL,NULL,levelcorrection_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_LEVELCORRECTION },
    { killchroma_init,NULL,NULL,NULL,NULL,killchroma_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_KILLCHROMA },
    { keyselect_init,NULL,NULL,NULL,NULL,NULL,keyselect_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_KEYSELECT },
    { isolate_init,NULL,NULL,NULL,NULL, isolate_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ISOLATE },
    { iris_init, NULL,NULL,NULL,NULL,NULL, iris_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_IRIS },
    { halftone_init,NULL,NULL,NULL,NULL,halftone_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_HALFTONE },
    { greyselect_init, NULL,NULL,NULL,NULL, greyselect_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_GREYSELECT },
    { ghost_init,ghost_malloc,ghost_free,NULL,NULL,ghost_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_GHOST },
    { gaussblur_init,gaussblur_malloc,gaussblur_free,NULL,NULL, gaussblur_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_CHOKEMATTE },
    { gammacompr_init,gammacompr_malloc,gammacompr_free,NULL,NULL,gammacompr_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_GAMMACOMPR },
    { gamma_init,gamma_malloc,gamma_free,NULL,NULL,gamma_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_GAMMA },
    { frameborder_init,NULL,NULL,NULL,NULL,NULL,frameborder_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_FRAMEBORDER },
    { flip_init,NULL,NULL,NULL,NULL,flip_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_FLIP },
    { flare_init, flare_malloc,flare_free,NULL,NULL,flare_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_FLARE },
    { fisheye_init, fisheye_malloc, fisheye_free,NULL,NULL,fisheye_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_FISHEYE },
    { fibdownscale_init, NULL,NULL,NULL,NULL, fibdownscale_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_FIBDOWNSCALE },
    { feathermask_init, feathermask_malloc, feathermask_free,NULL,NULL, feathermask_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_ALPHAFEATHERMASK },
    { enhancemask_init, NULL, NULL, NULL,NULL, enhancemask_apply,NULL,NULL, NULL,NULL, VJ_IMAGE_EFFECT_ENHANCEMASK },
    { emboss_init, NULL, NULL, NULL, NULL, emboss_apply, NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_EMBOSS }, 
    { dupmagic_init, NULL, NULL, NULL, NULL, NULL, dupmagic_apply, NULL, NULL, NULL, VJ_VIDEO_EFFECT_DUPMAGIC },
    { dotillism_init, dotillism_malloc, dotillism_free, NULL,NULL, dotillism_apply, NULL,NULL,NULL, NULL, VJ_IMAGE_EFFECT_DOTILLISM },
    { dither_init, dither_malloc, dither_free, NULL,NULL, dither_apply, NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_DITHER },
    { distortion_init, distortion_malloc, distortion_free, NULL, NULL, distortion_apply, NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_DISTORTION },
    { dissolve_init, NULL, NULL, NULL, NULL, NULL, dissolve_apply, NULL, NULL, NULL, VJ_VIDEO_EFFECT_DISSOLVE },
    { differencemap_init,differencemap_malloc,differencemap_free,NULL,NULL,NULL,differencemap_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_EXTDIFF },
    { diffimg_init,NULL,NULL,NULL,NULL,diffimg_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_DIFF },
    { diff_init,diff_malloc,diff_free,diff_prepare,NULL,NULL,diff_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_DIFF },
    { dices_init,dices_malloc,dices_free,NULL,NULL,dices_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_DICES },
    { deinterlace_init,NULL,NULL,NULL,NULL,deinterlace_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_DEINTERLACE },
    { cutstop_init,cutstop_malloc,cutstop_free,NULL,NULL,cutstop_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_CUTSTOP },
    { crosspixel_init,crosspixel_malloc,crosspixel_free,NULL,NULL,crosspixel_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CROSSPIXEL },
    { contrast_init,NULL,NULL,NULL,NULL,contrast_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CONTRAST },
    { constantblend_init,NULL,NULL,NULL,NULL,constantblend_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_CONSTANTBLEND },
    { complexthreshold_init,NULL,NULL,NULL,NULL,NULL,complexthreshold_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD },
    { complexsync_init,complexsync_malloc,complexsync_free,NULL,NULL,NULL,complexsync_apply,complexsync_ready,NULL,NULL,VJ_VIDEO_EFFECT_COMPLEXSYNC },
    { complexsaturation_init,NULL,NULL,NULL,NULL,complexsaturation_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_COMPLEXSATURATE },
    { complexopacity_init,NULL,NULL,NULL,NULL,NULL,complexopacity_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_COMPLEXOPACITY },
    { complexinvert_init,NULL,NULL,NULL,NULL,complexinvert_apply,NULL,NULL,NULL,NULL,  VJ_IMAGE_EFFECT_COMPLEXINVERT },
    { colorshift_init,NULL,NULL,NULL,NULL,colorshift_apply, NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_COLORSHIFT },
    { colormap_init,NULL,NULL,NULL,NULL,colormap_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_COLORMAP },
    { colorhis_init,colorhis_malloc,colorhis_free,NULL,NULL,colorhis_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_COLORHIS },
    { color_init,NULL,NULL,NULL,NULL,color_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_COLORTEST },
    { coloradjust_init, NULL,NULL,NULL,NULL,coloradjust_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_COLORADJUST },
    { colmorphology_init,colmorphology_malloc,colmorphology_free,NULL,NULL,colmorphology_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_COLMORPH },
    { colflash_init,colflash_malloc,colflash_free,NULL,NULL,colflash_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_COLFLASH },
    { chromium_init,NULL,NULL,NULL,NULL,chromium_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CHROMIUM },
    { chromascratcher_init,chromascratcher_malloc,chromascratcher_free,NULL,NULL,chromascratcher_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CHROMASCRATCHER},
    { chromapalette_init,NULL,NULL,NULL,NULL,chromapalette_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CHROMAPALETTE },
    { chromamagick_init,NULL,NULL,NULL,NULL,NULL,chromamagick_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_CHROMAMAGICK },
    { chromamagickalpha_init, NULL,NULL,NULL,NULL,NULL,chromamagickalpha_apply,NULL,NULL,NULL, VJ_VIDEO_EFFECT_CHROMAMAGICKALPHA },
    { chameleon_init, chameleon_malloc,chameleon_free,chameleon_prepare,NULL,chameleon_apply,NULL,NULL,chameleon_request_fx,chameleon_set_motionmap,VJ_IMAGE_EFFECT_CHAMELEON},
    { chameleonblend_init, chameleonblend_malloc,chameleonblend_free, chameleonblend_prepare,NULL,NULL,chameleonblend_apply,NULL,chameleonblend_request_fx,chameleonblend_set_motionmap,VJ_VIDEO_EFFECT_CHAMBLEND},
    { cartonize_init,NULL,NULL,NULL,NULL,cartonize_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_CARTONIZE},
    { bwselect_init,bwselect_malloc,bwselect_free,NULL,NULL,bwselect_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BWSELECT },
    { bwotsu_init,NULL,NULL,NULL,NULL,bwotsu_apply,NULL,NULL,NULL,NULL, VJ_IMAGE_EFFECT_BWOTSU },
    { borders_init,NULL,NULL,NULL,NULL,borders_apply,NULL,NULL,NULL,NULL,VJ_VIDEO_EFFECT_BORDERS }, 
    { boids_init,boids_malloc,boids_free,NULL,NULL,boids_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_VIDBOIDS },
    { bloom_init,bloom_malloc,bloom_free,NULL,NULL,bloom_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BLOOM },
    { blob_init,blob_malloc,blob_free,NULL,NULL,blob_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_VIDBLOB },
    { binaryoverlay_init,NULL,NULL,NULL,NULL,NULL,binaryoverlay_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_BINARYOVERLAY },
    { bgsubtractgauss_init,bgsubtractgauss_malloc,bgsubtractgauss_free,bgsubtractgauss_prepare,bgsubtractgauss_get_bg_frame,bgsubtractgauss_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BGSUBTRACTGAUSS},
    { bgsubtract_init,bgsubtract_malloc,bgsubtract_free,bgsubtract_prepare,bgsubtract_get_bg_frame,bgsubtract_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BGSUBTRACT},
    { bgpush_init,bgpush_malloc,bgpush_free,bgpush_prepare,bgpush_get_bg_frame,bgpush_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BGPUSH},
    { baltantv_init,baltantv_malloc,baltantv_free,NULL,NULL,baltantv_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BALTANTV},
    { average_init,average_malloc,average_free,NULL,NULL,average_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_AVERAGE},
    { average_blend_init,NULL,NULL,NULL,NULL,NULL,average_blend_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_AVERAGEBLEND },
    { autoeq_init,autoeq_malloc,autoeq_free,NULL,NULL,autoeq_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_AUTOEQ },
    { alphatransition_init, NULL,NULL,NULL,NULL,NULL,alphatransition_apply, NULL,NULL,NULL,VJ_VIDEO_EFFECT_ALPHATRANSITION },
    { alphaselect_init,NULL,NULL,NULL,NULL,alphaselect_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHASELECT },
    { alphaselect2_init,NULL,NULL,NULL,NULL,alphaselect2_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHASELECT2 },
    { alphanegate_init, NULL,NULL,NULL,NULL, alphanegate_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHANEGATE },
    { alphaflatten_init,NULL,NULL,NULL,NULL,alphaflatten_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHAFLATTEN },
    { alphafill_init,NULL,NULL,NULL,NULL,alphafill_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHAFILL },
    { alphadampen_init,NULL,NULL,NULL,NULL,alphadampen_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHADAMPEN },
    { alphablend_init,NULL,NULL,NULL,NULL,NULL,alphablend_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_ALPHABLEND },
    { alpha2img_init,NULL,NULL,NULL,NULL,alpha2img_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_ALPHA2IMG },
    { wipe_init,wipe_malloc,wipe_free,NULL,NULL,NULL,wipe_apply,wipe_ready,NULL,NULL,VJ_VIDEO_EFFECT_WIPE },
    { vbar_init,vbar_malloc,vbar_free,NULL,NULL,NULL,vbar_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_VBAR },
    { transop_init,NULL,NULL,NULL,NULL,NULL,transop_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_TRANSOP },
    { transline_init,transline_malloc,transline_free,NULL,NULL,NULL,transline_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_LINE },
    { transcarot_init,transcarot_malloc,transcarot_free,NULL,NULL,NULL,transcarot_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_CAROT },
    { transblend_init,transblend_malloc,transblend_free,NULL,NULL,NULL,transblend_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_TRANSBLEND },
    { channeloverlay_init,NULL,NULL,NULL,NULL,NULL,channeloverlay_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_CHANNELOVERLAY },
    { fadecolorrgb_init,fadecolorrgb_malloc,fadecolorrgb_free,NULL,NULL,fadecolorrgb_apply,NULL,NULL,NULL,NULL,VJ_VIDEO_EFFECT_FADECOLORRGB },
    { fadecolor_init,fadecolor_malloc,fadecolor_free,NULL,NULL,fadecolor_apply,NULL,NULL,NULL,NULL,VJ_VIDEO_EFFECT_FADECOLORRGB },
    { bar_init,bar_malloc,bar_free,NULL,NULL,NULL,bar_apply,NULL,NULL,NULL,VJ_VIDEO_EFFECT_3BAR },
    { buffer_init,buffer_malloc,buffer_free,NULL,NULL,buffer_apply,NULL, NULL,NULL,NULL,VJ_IMAGE_EFFECT_BUFFER },
    { blackreplace_init,NULL,NULL,NULL,NULL,blackreplace_apply,NULL,NULL,NULL,NULL,VJ_IMAGE_EFFECT_BLACKREPLACE },
    { NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL, 0},


    // FIXME: global tagged FX : motionmap, bgsubtract, bgsubtractgauss, bgpush
    //        1 motionmap per FX (set of FX that can request motionmap)
    //        1 static bg per motionmap
};

static int *vj_fx_map = NULL;
static vj_effect **vj_effect_map = NULL;
static int parallel_enabled = 1;
static vj_fx_priv_map_t **vj_fx_priv_chain = NULL;
static int num_fx = 0;
static int VJ_INTERNAL = 0;
static int LAST_ID = 0;
static VJFrame *vj_fx_bg = NULL;

uint8_t  pixel_Y_hi_ = 235;
uint8_t  pixel_U_hi_ = 240;
uint8_t  pixel_Y_lo_ = 16;
uint8_t  pixel_U_lo_ = 16;

static void vje_global_store(int chain_id, int entry, int fx_id, void *ptr);
static int vje_global_couple(int chain_id, int ref_id, int fx_id, void *ptr);
static void vje_global_clear(int chain_id, int entry);

void    vje_set_bg(VJFrame *bg)
{
    if(vj_fx_bg == NULL) {
        vj_fx_bg = vj_calloc(sizeof(VJFrame));
    }
    else {
        if(vj_fx_bg->data[0]) {
            free(vj_fx_bg->data[0]);
            vj_fx_bg->data[0] = NULL;
        }
    }

    veejay_memcpy(vj_fx_bg, bg, sizeof(VJFrame));
    
    vj_fx_bg->data[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * ( bg->len * 3 ) ); // enough space to hold 4:4:4
    vj_fx_bg->data[1] = vj_fx_bg->data[0] + bg->len;
    vj_fx_bg->data[2] = vj_fx_bg->data[1] + bg->len;
    vj_fx_bg->data[3] = NULL;

    veejay_memcpy( vj_fx_bg->data[0], bg->data[0], bg->len );
    veejay_memcpy( vj_fx_bg->data[1], bg->data[1], bg->uv_len );
    veejay_memcpy( vj_fx_bg->data[2], bg->data[2], bg->uv_len );

    veejay_msg(VEEJAY_MSG_DEBUG, "Frame stored in FX process chain as background frame");
}

unsigned int	get_pixel_range_min_Y() {
	return pixel_Y_lo_;
}
unsigned int	get_pixel_range_min_UV() {
	return pixel_U_lo_;
}

void    vje_enable_parallel() {
    parallel_enabled = 1;
}
void    vje_disable_parallel() {
    parallel_enabled = 0;
}

static int rgb_parameter_conversion_type_ = GIMP_RGB;

void vje_set_rgb_parameter_conversion_type( int full_range ) 
{
    if(full_range) {
        rgb_parameter_conversion_type_ = GIMP_RGB;
    }
    else {
        rgb_parameter_conversion_type_ = CCIR601_RGB;
    }
}

int vje_get_rgb_parameter_conversion_type() {
    return rgb_parameter_conversion_type_;
}

void	vje_set_pixel_range(uint8_t Yhi,uint8_t Uhi, uint8_t Ylo, uint8_t Ulo)
{
	pixel_Y_hi_ = Yhi;
	pixel_U_hi_ = Uhi;
	pixel_U_lo_ = Ylo;
	pixel_Y_lo_ = Ulo;
}

int vje_init(int w, int h)
{   
    int i;
    vj_fx_map = (int*) vj_malloc( sizeof(int) * MAX_EFFECTS );
    if(vj_fx_map == NULL)
        return 0;
    vj_effect_map = (vj_effect**) vj_calloc(sizeof(vj_effect*) * MAX_EFFECTS);
    if(!vj_effect_map)
        return 0;
    vj_fx_priv_chain = (vj_fx_priv_map_t**) vj_calloc(sizeof(vj_fx_priv_map_t*) * NUM_CHAINS * MAX_ENTRY_PER_CHAIN );
    if(!vj_fx_priv_chain) 
        return 0;
    
    for( i = 0; i < MAX_EFFECTS; i ++ )
        vj_fx_map[i] = -1;

    for( i = 0; vj_fx[i].fx_id != 0; i ++ ) {
        vj_fx_map[ vj_fx[i].fx_id ] = i;
        vj_effect_map[ i ] = vj_fx[i].fx_init(w,h);
        num_fx ++;
        if( vj_fx[i].fx_id > LAST_ID ) {
            LAST_ID = vj_fx[i].fx_id;
        }
    }

    for( i = 0; i < (NUM_CHAINS * MAX_ENTRY_PER_CHAIN); i ++ ) {
        vj_fx_priv_chain[i] = vj_calloc(sizeof(vj_fx_priv_map_t));
    } 

    VJ_INTERNAL = num_fx;

    int n_plugs = plug_sys_detect_plugins();
    const int p_stop = num_fx + n_plugs;
    int offset = num_fx;
    for( i = num_fx; i < p_stop; i ++ ) {
        vj_effect_map[i] = plug_get_plugin( i - offset );
        vj_effect_map[i]->is_plugin = 1;
        vj_fx_map[ VJ_PLUGIN + (i - offset) ] = VJ_INTERNAL + (i - offset);
        LAST_ID = VJ_PLUGIN + ( i - offset );
        num_fx ++;
    }   

    init_sqrt_map_pixel_values();

    plug_sys_init( PIX_FMT_YUVA444P, w,h, 0 );

    return 1;
}

int vje_get_plugin_id(int fx_id)
{
    return (vj_fx_map[ fx_id ] - VJ_INTERNAL);
}

void *vje_fx_malloc(int fx_id, int chain_id, int entry, int w, int h, int *error )
{
    if( fx_id >= VJ_PLUGIN )
        return plug_activate( vj_fx_map[ fx_id  ] );
   
    int idx = vj_fx_map[ fx_id ];
    if( vj_fx[ idx ].fx_malloc == NULL ) {
        *error = 0;
        return NULL;
    }

    void *ptr = vj_fx[ idx ].fx_malloc( w, h );
    if( ptr == NULL ) {
        *error = 1;
        return NULL;
    }
    
    if( vj_effect_map[ idx ]->global ) {
        vje_global_store( chain_id, entry, fx_id, ptr );
    }
    else {
        if( vj_fx[ idx ].fx_request_fx != NULL ) {
            int req_fx_id = vj_fx[ idx ].fx_request_fx();
            vje_global_couple( chain_id, req_fx_id, fx_id, ptr ); //FIXME: design error in coupling
        }
    }

    *error = 0;
    return ptr;
}

void vje_fx_free( int fx_id, int chain_id, int entry, void *ptr )
{
    if( fx_id >= VJ_PLUGIN ) {
        plug_deactivate( ptr );
    }
    else {
        int idx = vj_fx_map[ fx_id ];
        if( vj_fx[ idx ].fx_free != NULL )
            vj_fx[ idx ].fx_free( ptr );

        vje_global_clear( chain_id, entry );
    }
}

static void vje_fx_parallel_apply( void *arg )
{
    vj_task_arg_t *v = (vj_task_arg_t*) arg;
    VJFrame a,b;

    int idx = v->iparams[0];
    int extra_frame = v->iparams[1];
    int *param_values = &(v->iparams[2]);

    vj_task_set_to_frame( &a, 0, v->jobnum );

    if(extra_frame) {
#ifdef STRICT_CHECKING
        assert( vj_fx[ idx ].fx_process != NULL );
#endif
	vj_task_set_to_frame( &b, 1, v->jobnum );
        vj_fx[ idx ].fx_process( v->ptr, &a, &b, param_values );
    }
    else {
#ifdef STRICT_CHECKING
        assert( vj_fx[ idx ].fx_filter != NULL );
#endif
        vj_fx[ idx ].fx_filter( v->ptr, &a, param_values );
    }
}

static int vje_fx_parallize( vj_effect *fx, void *instance, int idx, VJFrame *A, VJFrame *B, int *args )
{
    if(!fx->parallel)
        return 0;

    if( vj_task_get_workers() <= 0 )
	return 0;

    int i;

    vj_task_set_from_frame( A );
    vj_task_set_param( idx, 0 );
    vj_task_set_param( fx->extra_frame, 1 );
    vj_task_set_ptr( instance ); 

    for( i = 0; i < fx->num_params; i ++ ) {
        vj_task_set_param( args[i], 2 + i );
    }

    vj_task_run( A->data, B->data, NULL, NULL, 4, (performer_job_routine) &vje_fx_parallel_apply );

    return 1;
}

static void vje_fx_plugin_apply( int plug_id, void *ptr, VJFrame *A, VJFrame *B, int *args, vj_effect *fx )
{
    int n = plug_get_num_input_channels( plug_id );
    int i;

    if( plug_is_frei0r( ptr ) ) {
        plug_set_parameters( ptr, fx->num_params, args );
    }
    else {
        for( i = 0; i < fx->num_params; i ++ ) {
            plug_set_parameter( ptr, i, 1, &(args[i]));
        }
    }

    if( n >= 1 )
        plug_push_frame( ptr, 0, 0, A );
    if( n >= 2 )
        plug_push_frame( ptr, 0, 1, B );

    if( plug_get_num_output_channels( plug_id ) > 0 )
        plug_push_frame( ptr, 1, 0, A );

    plug_process( ptr, A->timecode );
}

void vje_fx_apply( int fx_id, void *ptr, VJFrame *A, VJFrame *B, int *args )
{
    int idx = vj_fx_map[ fx_id ];
    vj_effect *fx = vj_effect_map [ idx ];

#ifdef STRICT_CHECKING
    assert( args != NULL );
#endif

    if(fx_id >= VJ_PLUGIN) {
        vje_fx_plugin_apply( idx, ptr, A, B, args, fx );
    }
    else {
        int doneProcessing = 0;

        if( parallel_enabled && fx->parallel ) {
            doneProcessing = vje_fx_parallize( fx, ptr, idx, A, B, args );
        }

        if(doneProcessing)
            return;

        if(fx->extra_frame) {
#ifdef STRICT_CHECKING
            assert( vj_fx[ idx ].fx_process != NULL );
#endif
            vj_fx[ idx ].fx_process( ptr, A, B, args );
        } else {
#ifdef STRICT_CHECKING
            assert( vj_fx[ idx ].fx_filter != NULL );
#endif
            vj_fx[ idx ].fx_filter( ptr, A, args );
        }
    }
}

void vje_fx_prepare( int fx_id, void *ptr, VJFrame *A )
{
    int idx = vj_fx_map[ fx_id ];

    VJFrame *bg = A;

    //FX that are tagged with static_bg = 1 get a pointer to stored VJFrame
    //other FX, get A passed in
    if( vj_effect_map[ idx ] != NULL ) {
        if( vj_effect_map[idx]->static_bg && vj_fx_bg != NULL ) {
            bg = vj_fx_bg;
            veejay_msg(VEEJAY_MSG_DEBUG, "Using background frame stored in FX process chain");
        }
    }

    if( vj_fx[ idx ].prepare != NULL ) {
        vj_fx[ idx ].prepare( ptr, bg );
    }
}

//FIXME: add method to signal start/end of chain processing


int vje_fx_is_transition_ready( int fx_id, void *ptr, int w, int h )
{
    int idx = vj_fx_map[ fx_id ];
    if( vj_fx[ idx ].fx_transition_ready == NULL )
        return 0;

    return vj_fx[ idx ].fx_transition_ready( ptr, w, h );
}

uint8_t *vje_fx_get_bg( int fx_id, void *ptr, unsigned int plane)
{
    int idx = vj_fx_map[ fx_id ];
    if( vj_fx[ idx ].get_bg == NULL )
        return NULL;

    return vj_fx[ idx ].get_bg( ptr, plane );
}

int vje_get_last_id() {
    return LAST_ID;
}

int vje_max_effects()
{
    return num_fx;
}

int vje_max_space()
{
    return MAX_EFFECTS;
}

int vje_is_plugin( int fx_id ) 
{
    return ( fx_id >= VJ_PLUGIN );
}

int vje_get_num_params( int fx_id ) 
{
    if( fx_id < 0 || fx_id > MAX_EFFECTS)
        return 0;

    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map[ idx ]->num_params;
}

const char *vje_get_description( int fx_id )
{
    CHECK_BOUNDS(fx_id)

    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map[ idx ]->description;
}

const char *vje_get_param_description( int fx_id, int param_nr )
{
    if( param_nr < 0 || param_nr >= vje_get_num_params(fx_id) )
        return NULL;
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map[ idx ]->param_description[ param_nr ];
}

int vje_get_param_default( int fx_id, int param_nr )
{
    if( param_nr < 0 || param_nr >= vje_get_num_params(fx_id) )
        return 0;
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map[ idx ]->defaults[ param_nr ];
}

int vje_get_param_min_limit( int fx_id, int param_nr )
{
    if( param_nr < 0 || param_nr >= vje_get_num_params(fx_id) )
        return 0;
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map[ idx ]->limits[0][ param_nr ];
}

int vje_get_param_max_limit( int fx_id, int param_nr )
{
    if( param_nr < 0 || param_nr >= vje_get_num_params(fx_id) )
        return 0;
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map [ idx ]->limits[1][ param_nr ];
}

int vje_get_extra_frame( int fx_id )
{
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map [ idx ]->extra_frame;
}

int vje_is_param_value_valid( int fx_id, int param_nr, int value )
{
    if( param_nr < 0 || param_nr >= vje_get_num_params(fx_id) )
       return 0;
    int idx = vj_fx_map[ fx_id ];
    vj_effect *fx = vj_effect_map[ idx ];
    if( param_nr >= 0 && param_nr < fx->num_params && value >= fx->limits[0][param_nr] && value <= fx->limits[1][param_nr] )
        return 1;
    return 0;
}

int vje_has_rgbkey( int fx_id )
{
    CHECK_BOUNDS(fx_id)
    
    int idx = vj_fx_map[ fx_id ];
    vj_effect *fx = vj_effect_map[ idx ];
    return fx->rgb_conv;
}

int vje_is_valid( int fx_id )
{   
    CHECK_BOUNDS(fx_id)

    int idx = vj_fx_map[ fx_id ];
    if( idx >= 0 )
        return 1;
    return 0;
}

int vje_get_info(int fx_id, int *is_mixer, int *n_params, int *rgba_only)
{
    int idx = vj_fx_map[ fx_id ];
    if(idx >= 0 ) {
        vj_effect *fx = vj_effect_map[idx];
        *is_mixer = fx->extra_frame;
        *n_params = fx->num_params;
        *rgba_only = fx->rgba_only;
        return 1;
    }
    return 0;
}

int vje_get_param_hints_length( int fx_id, int p, int limit )
{
    int idx = vj_fx_map [ fx_id ];
    vj_effect *fx = vj_effect_map[ idx ];

    if( fx->hints == NULL )
        return 0;
    if( fx->hints[p] == NULL )
        return 0;
    if( fx->hints[p]->description == NULL)
        return 0;

    int len = 0;
    int i;
    for( i = 0; i <= limit; i ++ ) {
        len += strlen( fx->hints[p]->description[i] );
        len += 3;
    }

    return len;
}

int vje_get_subformat( int fx_id )
{
    int idx = vj_fx_map[ fx_id ];
    return vj_effect_map [ idx ]->sub_format;
}

int vje_get_summarylen(int fx_id)
{
    CHECK_BOUNDS(fx_id)

    int idx = vj_fx_map [ fx_id ];
    if( idx == -1 ) {
        return 0;
    }

    vj_effect *fx = vj_effect_map[ idx ];

    if( fx == NULL ) {
        return 0;
    }

	int p = fx->num_params;
	int len = 0;
	len += 3; // "%03d%s" name length, name
    len += strlen( fx->description );
	len += 3; // "%03d" fx_id
	len += 1; // "%01d" is_video
	len += 1; // "%01d" has_rgb
	len += 1; // "%01d" is_gen
	len += 2; // "%02d" num_arg
	int i;
	for( i = 0; i < p; i ++ ) {
	    len += 3;
        len += (strlen(fx->param_description[i]));
        len += 6;
        len += 6;
        len += 6;
    }

	for( i = 0; i < p; i ++ ) {
        int lim =  fx->limits[1][i];
        int hints_len =vje_get_param_hints_length( fx_id, i, lim );
        len += 6;
		len += hints_len;
	}

    len += 4;

	return len;
}

#define MAX_TEMP_SIZE 4096 
int vje_get_summary(int fx_id, char *dst)
{
    CHECK_BOUNDS(fx_id)

    int idx = vj_fx_map [ fx_id ];
    if( idx == -1 ) {
        return 0;
    }

    vj_effect *fx = vj_effect_map[ idx ];
    if( fx == NULL ) {
        return 0;
    }

	int p = fx->num_params;
	int i,j;		
	char tmp[MAX_TEMP_SIZE];

	snprintf(dst,MAX_TEMP_SIZE, "%03zu%s%03d%1d%1d%1d%02d",
		strlen( fx->description),
		fx->description,
		fx_id,
		fx->extra_frame,
		fx->rgb_conv,
		fx->is_gen,
		p
		);
	for(i=0; i < p; i++)
	{
		snprintf(tmp,MAX_TEMP_SIZE,
			"%06d%06d%06d%03zu%s",
			fx->limits[0][i],
			fx->limits[1][i],
			fx->defaults[i],
			strlen(fx->param_description[i]),
			fx->param_description[i]
		
		);
		strncat( dst, tmp,MAX_TEMP_SIZE - strlen(tmp) - 1 );
	}
	for (i = 0; i < p; i++) {
    	int limit = fx->limits[1][i];
		int vlen = vje_get_param_hints_length(fx_id, i, limit);
 		snprintf(tmp, MAX_TEMP_SIZE, "%06d", vlen);

 		strncat(dst, tmp, MAX_TEMP_SIZE - strlen(dst) - 1);

  		if (vlen > 0) {
    		for (j = 0; j <= limit; j++) {
            	int slen = strlen(fx->hints[i]->description[j]);
                snprintf(tmp, MAX_TEMP_SIZE, "%03d%s", slen, fx->hints[i]->description[j]);
                strncat(dst, tmp, MAX_TEMP_SIZE - strlen(dst) - 1);
            }
        }
    }

	return 1;
}

static void vje_global_store(int chain_id, int entry, int fx_id, void *ptr)
{
    vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->fx_id = fx_id;
    vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->priv = ptr;
}

static void vje_global_clear(int chain_id, int entry)
{
    int i;
    int start = 0;
    int end = MAX_ENTRY_PER_CHAIN;

    if( vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->fx_id == 0 )
        return;

    for( i = start; i < end; i ++ ) {
        // test if FX has a coupling
        if( vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->used[i].fx_id > 0 ) {
            // clear the coupling
            vj_fx_priv_chain[i]->used[i].fx_request_set_private( 
                vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->used[i].ptr,
                NULL
            );
            // clear coupling data
            vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->used[i].fx_id = 0;
            vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->used[i].ptr = NULL;
            vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->used[i].fx_request_set_private = NULL;    
        }
    }
    vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->fx_id = 0;
    vj_fx_priv_chain[ (chain_id * MAX_ENTRY_PER_CHAIN) + entry ]->priv = NULL;
}

static int vje_global_couple(int chain_id, int ref_id, int fx_id, void *ptr)
{
    int i,j;
    int start = (chain_id * MAX_ENTRY_PER_CHAIN);
    int end = start + MAX_ENTRY_PER_CHAIN;
    int idx = vj_fx_map [ fx_id ];
  
    for( i = start; i < end ; i ++ ) {
        // test if requested fx is initialized
        if( vj_fx_priv_chain[i]->fx_id == ref_id ) {
            for( j = 0; j < MAX_ENTRY_PER_CHAIN; j ++ ) {
                // find free slot
                if( vj_fx_priv_chain[i]->used[j].fx_id == 0 ) {
                    // coupling
                    vj_fx_priv_chain[i]->used[j].fx_id = fx_id;
                    vj_fx_priv_chain[i]->used[j].ptr = ptr;
                    vj_fx_priv_chain[i]->used[j].fx_request_set_private = vj_fx[ idx ].fx_request_set_private;
                    vj_fx_priv_chain[i]->used[j].fx_request_set_private( vj_fx_priv_chain[i]->used[j].ptr, vj_fx_priv_chain[i]->priv );
                    return 1;
                }
            }
        }
    }

    return 0;
}

void vje_dump() {
	veejay_msg(VEEJAY_MSG_INFO, "Below follow all effects in Veejay,");
	veejay_msg(VEEJAY_MSG_INFO, "Effect numbers starting with 2xx are effects that use");
	veejay_msg(VEEJAY_MSG_INFO, "*two* sources (by default a copy of itself)");
	veejay_msg(VEEJAY_MSG_INFO, "Use the channel/source commands to select another sample/stream");
	veejay_msg(VEEJAY_MSG_INFO, "to mix with.");
	veejay_msg(VEEJAY_MSG_INFO, "\n [effect num] [effect name] [arg 0 , min/max ] [ arg 1, min/max ] ...");
	
    for( int i = 0; i < MAX_EFFECTS; i ++ ) {
        int idx = vj_fx_map[i];
        if(idx == -1)
            continue;

        vj_effect *fx = vj_effect_map[ idx ];
        if(fx == NULL)
            continue;

        veejay_msg(VEEJAY_MSG_INFO,"\t%d\t%-32s", i, fx->description );
        for( int j = 0; j < fx->num_params; j ++ ) {
            veejay_msg(VEEJAY_MSG_INFO, "\t\t\t%-24s\t\t\t%d\t%d - %d",
                    fx->param_description[j],
                    j,
                    fx->limits[0][j],
                    fx->limits[1][j] );
        }
    }

}
