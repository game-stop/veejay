/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef VJE_INTERNAL_H
#define VJE_INTERNAL_H

#define VJE_MAX_ARGS 10
#define VJE_INVALID_ARGS -1
#define VJE_NEED_INIT -2
#define VJE_NO_FRAMES -3
#define VJE_SUCCESS 0
#include <libvje/vje.h>

#include "./effects/alpha2img.h"
#include "./effects/alphablend.h"
#include "./effects/alphadampen.h"
#include "./effects/alphafill.h"
#include "./effects/alphaflatten.h"
#include "./effects/alphanegate.h"
#include "./effects/alphaselect2.h"
#include "./effects/alphaselect.h"
#include "./effects/alphatransition.h"
#include "./effects/autoeq.h"
#include "./effects/average-blend.h"
#include "./effects/average.h"
#include "./effects/baltantv.h"
#include "./effects/bathroom.h"
#include "./effects/bgpush.h"
#include "./effects/bgsubtractgauss.h"
#include "./effects/bgsubtract.h"
#include "./effects/binaryoverlays.h"
#include "./effects/blob.h"
#include "./effects/boids.h"
#include "./effects/borders.h"
#include "./effects/bwotsu.h"
#include "./effects/bwselect.h"
#include "./effects/cali.h"
#include "./effects/cartonize.h"
#include "./effects/chameleonblend.h"
#include "./effects/chameleon.h"
#include "./effects/chromamagickalpha.h"
#include "./effects/chromamagick.h"
#include "./effects/chromapalette.h"
#include "./effects/chromascratcher.h"
#include "./effects/chromium.h"
#include "./effects/colflash.h"
#include "./effects/colmorphology.h"
#include "./effects/coloradjust.h"
#include "./effects/color.h"
#include "./effects/colorhis.h"
#include "./effects/colormap.h"
#include "./effects/colorshift.h"
#include "./effects/common.h"
#include "./effects/complexinvert.h"
#include "./effects/complexsaturate.h"
#include "./effects/complexsync.h"
#include "./effects/complexthreshold.h"
#include "./effects/constantblend.h"
#include "./effects/contourextract.h"
#include "./effects/contrast.h"
#include "./effects/crosspixel.h"
#include "./effects/cutstop.h"
#include "./effects/deinterlace.h"
#include "./effects/dices.h"
#include "./effects/diff.h"
#include "./effects/diffimg.h"
#include "./effects/diffmap.h"
#include "./effects/dissolve.h"
#include "./effects/distort.h"
#include "./effects/dither.h"
#include "./effects/dummy.h"
#include "./effects/dupmagic.h"
#include "./effects/emboss.h"
#include "./effects/enhancemask.h"
#include "./effects/feathermask.h"
#include "./effects/fibdownscale.h"
#include "./effects/fisheye.h"
#include "./effects/flare.h"
#include "./effects/flip.h"
#include "./effects/frameborder.h"
#include "./effects/gamma.h"
#include "./effects/gaussblur.h"
#include "./effects/ghost.h"
#include "./effects/greyselect.h"
#include "./effects/iris.h"
#include "./effects/isolate.h"
#include "./effects/keyselect.h"
#include "./effects/killchroma.h"
#include "./effects/levelcorrection.h"
#include "./effects/lumablend.h"
#include "./effects/lumakeyalpha.h"
#include "./effects/lumakey.h"
#include "./effects/lumamagick.h"
#include "./effects/lumamask.h"
#include "./effects/magicalphaoverlays.h"
#include "./effects/magicmirror.h"
#include "./effects/magicoverlaysalpha.h"
#include "./effects/magicoverlays.h"
#include "./effects/magicscratcher.h"
#include "./effects/mask.h"
#include "./effects/maskstop.h"
#include "./effects/masktransition.h"
#include "./effects/meanfilter.h"
#include "./effects/median.h"
#include "./effects/mirrors2.h"
#include "./effects/mirrors.h"
#include "./effects/mixtoalpha.h"
#include "./effects/morphology.h"
#include "./effects/motionblur.h"
#include "./effects/motionmap.h"
#include "./effects/mtracer.h"
#include "./effects/negatechannel.h"
#include "./effects/negation.h"
#include "./effects/neighbours2.h"
#include "./effects/neighbours3.h"
#include "./effects/neighbours4.h"
#include "./effects/neighbours5.h"
#include "./effects/neighbours.h"
#include "./effects/nervous.h"
#include "./effects/noiseadd.h"
#include "./effects/noisepencil.h"
#include "./effects/opacityadv.h"
#include "./effects/opacity.h"
#include "./effects/opacitythreshold.h"
#include "./effects/overclock.h"
#include "./effects/passthrough.h"
#include "./effects/pencilsketch.h"
#include "./effects/perspective.h"
#include "./effects/photoplay.h"
#include "./effects/picinpic.h"
#include "./effects/pixelate.h"
#include "./effects/porterduff.h"
#include "./effects/posterize.h"
#include "./effects/radcor.h"
#include "./effects/radialblur.h"
#include "./effects/radioactive.h"
#include "./effects/randnoise.h"
#include "./effects/raster.h"
#include "./effects/rawman.h"
#include "./effects/rawval.h"
#include "./effects/reflection.h"
#include "./effects/revtv.h"
#include "./effects/rgbchannel.h"
#include "./effects/rgbkey.h"
#include "./effects/rgbkeysmooth.h"
#include "./effects/ripple.h"
#include "./effects/rotozoom.h"
#include "./effects/scratcher.h"
#include "./effects/sinoids.h"
#include "./effects/slice.h"
#include "./effects/slicer.h"
#include "./effects/smear.h"
#include "./effects/smuck.h"
#include "./effects/softblur.h"
#include "./effects/solarize.h"
#include "./effects/split.h"
#include "./effects/swirl.h"
#include "./effects/threshold.h"
#include "./effects/timedistort.h"
#include "./effects/toalpha.h"
#include "./effects/tracer.h"
#include "./effects/transform.h"
#include "./effects/travelmatte.h"
#include "./effects/tripplicity.h"
#include "./effects/uvcorrect.h"
#include "./effects/videoplay.h"
#include "./effects/videowall.h"
#include "./effects/water.h"
#include "./effects/waterrippletv.h"
#include "./effects/whiteframe.h"
#include "./effects/widthmirror.h"
#include "./effects/zoom.h"
#include "./transitions/3bar.h"
#include "./transitions/fadecolor.h"
#include "./transitions/fadecolorrgb.h"
#include "./transitions/slidingdoor.h"
#include "./transitions/transblend.h"
#include "./transitions/transcarot.h"
#include "./transitions/transline.h"
#include "./transitions/transop.h"
#include "./transitions/vbar.h"
#include "./transitions/wipe.h"

#define VJ_IMAGE_EFFECT_MIN 91
#define VJ_IMAGE_EFFECT_MAX 199

#define VJ_VIDEO_EFFECT_MIN 200
#define VJ_VIDEO_EFFECT_MAX 259

#define VJ_PLUGIN 500


#define VJ_VIDEO_COUNT (VJ_VIDEO_EFFECT_MAX - VJ_VIDEO_EFFECT_MIN + 1)

enum {
    VJ_EFFECT_LUM_RED = 65,
    VJ_EFFECT_LUM_BLUE = 35,
    VJ_EFFECT_LUM_WHITE = 235,
    VJ_EFFECT_LUM_BLACK = 16,
    VJ_EFFECT_LUM_YELLOW = 162,
    VJ_EFFECT_LUM_CYAN = 131,
    VJ_EFFECT_LUM_GREEN = 112,
    VJ_EFFECT_LUM_MAGNETA = 84,
};

enum {
    VJ_EFFECT_CB_RED = 100,
    VJ_EFFECT_CB_WHITE = 128,
    VJ_EFFECT_CB_YELLOW = 44,
    VJ_EFFECT_CB_CYAN = 156,
    VJ_EFFECT_CB_MAGNETA = 184,
    VJ_EFFECT_CB_BLUE = 212,
    VJ_EFFECT_CB_GREEN = 72,
    VJ_EFFECT_CB_BLACK = 128,
};

enum {
    VJ_EFFECT_CR_RED = 212,
    VJ_EFFECT_CR_WHITE = 128,
    VJ_EFFECT_CR_YELLOW = 142,
    VJ_EFFECT_CR_CYAN = 44,
    VJ_EFFECT_CR_MAGNETA = 198,
    VJ_EFFECT_CR_BLUE = 114,
    VJ_EFFECT_CR_GREEN = 58,
    VJ_EFFECT_CR_BLACK = 128,
};

enum {
    VJ_EFFECT_COLOR_YELLOW = 7,
    VJ_EFFECT_COLOR_RED = 6,
    VJ_EFFECT_COLOR_BLUE = 5,
    VJ_EFFECT_COLOR_MAGNETA = 4,
    VJ_EFFECT_COLOR_CYAN = 3,
    VJ_EFFECT_COLOR_GREEN = 2,
    VJ_EFFECT_COLOR_BLACK = 1,
    VJ_EFFECT_COLOR_WHITE = 0,
};

enum {
    /* video effects */
    VJ_VIDEO_EFFECT_OVERLAYMAGIC = 201,
    VJ_VIDEO_EFFECT_LUMAMAGICK = 202,
    VJ_VIDEO_EFFECT_DIFF = 203,
    VJ_VIDEO_EFFECT_OPACITY = 204,
    VJ_VIDEO_EFFECT_LUMAKEY = 205,
    VJ_VIDEO_EFFECT_RGBKEY = 206,
    VJ_VIDEO_EFFECT_CHROMAMAGICK = 207,
    VJ_VIDEO_EFFECT_LUMABLEND = 208,
    VJ_VIDEO_EFFECT_SPLIT = 209,
    VJ_VIDEO_EFFECT_BORDERS = 210,
    VJ_VIDEO_EFFECT_FRAMEBORDER = 211,
    VJ_VIDEO_EFFECT_SLIDINGDOOR = 212,
    VJ_VIDEO_EFFECT_TRANSOP = 213,
    VJ_VIDEO_EFFECT_CAROT = 214,
    VJ_VIDEO_EFFECT_LINE = 215,
    VJ_VIDEO_EFFECT_TRANSBLEND = 216,
    VJ_VIDEO_EFFECT_FADECOLOR = 217,
    VJ_VIDEO_EFFECT_FADECOLORRGB = 218,
    VJ_VIDEO_EFFECT_WHITEFRAME = 219,
    //VJ_VIDEO_EFFECT_DIFFIMG = 220,
    VJ_VIDEO_EFFECT_MASK = 220,
    VJ_VIDEO_EFFECT_THRESHOLDSMOOTH = 221,
    VJ_VIDEO_EFFECT_THRESHOLD = 222,
    VJ_VIDEO_EFFECT_RGBKEYSMOOTH = 223,
    VJ_VIDEO_EFFECT_WIPE = 224,
    VJ_VIDEO_EFFECT_TRACER = 225,
    VJ_VIDEO_EFFECT_MTRACER = 226,
    VJ_VIDEO_EFFECT_DUPMAGIC = 227,
    VJ_VIDEO_EFFECT_KEYSELECT = 228,
    VJ_VIDEO_EFFECT_COMPLEXTHRESHOLD = 229,
    VJ_VIDEO_EFFECT_COMPLEXSYNC = 230,
    VJ_VIDEO_EFFECT_3BAR = 231,
    VJ_VIDEO_EFFECT_VBAR = 232,
    VJ_VIDEO_EFFECT_LUMAMASK = 233,
    VJ_VIDEO_EFFECT_BINARYOVERLAY = 234,
    VJ_VIDEO_EFFECT_DISSOLVE = 235,
    VJ_VIDEO_EFFECT_TRIPPLICITY = 236,
	VJ_VIDEO_EFFECT_VIDEOPLAY = 237,
	VJ_VIDEO_EFFECT_VIDEOWALL = 238,
	VJ_VIDEO_EFFECT_EXTTHRESHOLD = 239,
	VJ_VIDEO_EFFECT_EXTDIFF		= 240,
	VJ_VIDEO_EFFECT_PICINPIC = 241,
	VJ_VIDEO_EFFECT_CHAMBLEND = 242,
	VJ_VIDEO_EFFECT_RADIOACTIVE = 243,
	VJ_VIDEO_EFFECT_IRIS   = 244,
	VJ_VIDEO_EFFECT_RIPPLETV = 245,
	VJ_VIDEO_EFFECT_SLICER = 246,
	VJ_VIDEO_EFFECT_AVERAGEBLEND = 247,
	VJ_VIDEO_EFFECT_MIXTOALPHA = 248,
	VJ_VIDEO_EFFECT_MAGICALPHA = 249,
	VJ_VIDEO_EFFECT_TRAVELMATTE = 250,
	VJ_VIDEO_EFFECT_ALPHABLEND = 251,
	VJ_VIDEO_EFFECT_PORTERDUFF = 252,
	VJ_VIDEO_EFFECT_LUMAKEYALPHA = 253,	
	VJ_VIDEO_EFFECT_CHROMAMAGICKALPHA = 254,
	VJ_VIDEO_EFFECT_MAGICOVERLAYALPHA = 255,
	VJ_VIDEO_EFFECT_MASKTRANSITION = 256,
	VJ_VIDEO_EFFECT_PASSTHROUGH = 257,
	VJ_VIDEO_EFFECT_ALPHATRANSITION = 258,
};

enum {
    /* image effects */
    VJ_IMAGE_EFFECT_PIXELATE = 100,
	VJ_IMAGE_EFFECT_MIRROR = 101,
    VJ_IMAGE_EFFECT_MIRRORS = 102,
    VJ_IMAGE_EFFECT_WIDTHMIRROR = 103,
    VJ_IMAGE_EFFECT_FLIP = 104,
    VJ_IMAGE_EFFECT_POSTERIZE = 105,
    VJ_IMAGE_EFFECT_NEGATION = 106,
    VJ_IMAGE_EFFECT_SOLARIZE = 107,
    VJ_IMAGE_EFFECT_COLORADJUST = 108,
    VJ_IMAGE_EFFECT_GAMMA = 109,
    VJ_IMAGE_EFFECT_SOFTBLUR = 110,
    VJ_IMAGE_EFFECT_REVTV = 111,
    VJ_IMAGE_EFFECT_DICES = 112,
    VJ_IMAGE_EFFECT_SMUCK = 113,
    VJ_IMAGE_EFFECT_KILLCHROMA = 114,
    VJ_IMAGE_EFFECT_EMBOSS = 115,
    VJ_IMAGE_EFFECT_DITHER = 116,
    VJ_IMAGE_EFFECT_RAWMAN = 117,
    VJ_IMAGE_EFFECT_RAWVAL = 118,
    VJ_IMAGE_EFFECT_TRANSFORM = 119,
    VJ_IMAGE_EFFECT_FIBDOWNSCALE = 120,
    VJ_IMAGE_EFFECT_REFLECTION = 121,
    VJ_IMAGE_EFFECT_ROTOZOOM = 122,
    VJ_IMAGE_EFFECT_COLORSHIFT = 123,
    VJ_IMAGE_EFFECT_SCRATCHER = 124,
    VJ_IMAGE_EFFECT_MAGICSCRATCHER = 125,
    VJ_IMAGE_EFFECT_CHROMASCRATCHER = 126,
    VJ_IMAGE_EFFECT_DISTORTION = 127,
    VJ_IMAGE_EFFECT_GREYSELECT = 128,
    VJ_IMAGE_EFFECT_BWSELECT = 129,
    VJ_IMAGE_EFFECT_COMPLEXINVERT = 130,
    VJ_IMAGE_EFFECT_COMPLEXSATURATE = 131,
    VJ_IMAGE_EFFECT_ISOLATE = 132,
    VJ_IMAGE_EFFECT_ENHANCEMASK = 133,
    VJ_IMAGE_EFFECT_NOISEADD = 134,
    VJ_IMAGE_EFFECT_CONTRAST = 135,
    VJ_IMAGE_EFFECT_MOTIONBLUR = 136,
    VJ_IMAGE_EFFECT_SINOIDS = 137,
    VJ_IMAGE_EFFECT_AVERAGE = 138,
    VJ_IMAGE_EFFECT_RIPPLE = 139,
    VJ_IMAGE_EFFECT_BATHROOM = 140,
    VJ_IMAGE_EFFECT_SLICE = 141,
    VJ_IMAGE_EFFECT_ZOOM = 142,
    VJ_IMAGE_EFFECT_DEINTERLACE = 144,
    VJ_IMAGE_EFFECT_CROSSPIXEL = 145,
    VJ_IMAGE_EFFECT_COLORTEST = 146,
    VJ_IMAGE_EFFECT_DIFF = 147,
    VJ_IMAGE_EFFECT_NOISEPENCIL = 148,
    VJ_IMAGE_EFFECT_RIPPLETV = 149,
    VJ_IMAGE_EFFECT_PENCILSKETCH = 143,
    VJ_IMAGE_EFFECT_BGSUBTRACT = 150,
    VJ_IMAGE_EFFECT_MAGICMIRROR = 151,
    VJ_IMAGE_EFFECT_PIXELSMEAR = 152,
    VJ_IMAGE_EFFECT_RASTER = 153,
    VJ_IMAGE_EFFECT_FISHEYE = 154,
    VJ_IMAGE_EFFECT_SWIRL = 155,
    VJ_IMAGE_EFFECT_RADIALBLUR = 156,
    VJ_IMAGE_EFFECT_CHROMIUM = 157,
    VJ_IMAGE_EFFECT_CHROMAPALETTE = 158,
    VJ_IMAGE_EFFECT_UVCORRECT = 159,
    VJ_IMAGE_EFFECT_OVERCLOCK = 160,
	VJ_IMAGE_EFFECT_CARTONIZE = 161,
	VJ_IMAGE_EFFECT_NERVOUS  = 162,
	VJ_IMAGE_EFFECT_MORPHOLOGY = 163,
	VJ_IMAGE_EFFECT_VIDBLOB = 164,
	VJ_IMAGE_EFFECT_VIDBOIDS = 165,
	VJ_IMAGE_EFFECT_GHOST = 166,
	VJ_IMAGE_EFFECT_NEIGHBOUR = 167,
	VJ_IMAGE_EFFECT_NEIGHBOUR2= 168,
	VJ_IMAGE_EFFECT_NEIGHBOUR3= 169,
	VJ_IMAGE_EFFECT_NEIGHBOUR4= 170,
	VJ_IMAGE_EFFECT_NEIGHBOUR5= 171,
	VJ_IMAGE_EFFECT_CUTSTOP = 172, 
	VJ_IMAGE_EFFECT_MASKSTOP = 173,
	VJ_IMAGE_EFFECT_PHOTOPLAY = 174,
	VJ_IMAGE_EFFECT_FLARE = 175,
	VJ_IMAGE_EFFECT_CONSTANTBLEND = 176,
	VJ_IMAGE_EFFECT_COLORMAP = 177,
	VJ_IMAGE_EFFECT_NEGATECHANNEL = 178,
	VJ_IMAGE_EFFECT_COLMORPH = 179,
	VJ_IMAGE_EFFECT_COLFLASH = 180,
	VJ_IMAGE_EFFECT_RGBCHANNEL = 181,
	VJ_IMAGE_EFFECT_AUTOEQ	=	182,
	VJ_IMAGE_EFFECT_COLORHIS = 	183,
	VJ_IMAGE_EFFECT_MOTIONMAP	=	184,
	VJ_IMAGE_EFFECT_TIMEDISTORT	=	185,
	VJ_IMAGE_EFFECT_CHAMELEON	=	186,
	VJ_IMAGE_EFFECT_BALTANTV	=	187,
	VJ_IMAGE_EFFECT_CONTOUR		=	188,
	VJ_IMAGE_EFFECT_LENSCORRECTION  = 	189,
	VJ_IMAGE_EFFECT_CALI		=	190,
	VJ_IMAGE_EFFECT_MEDIANFILTER	=	191,
	VJ_IMAGE_EFFECT_PERSPECTIVE = 192,
	VJ_IMAGE_EFFECT_ALPHAFILL = 193,
	VJ_IMAGE_EFFECT_ALPHA2IMG = 194,
	VJ_IMAGE_EFFECT_TOALPHA = 195,
	VJ_IMAGE_EFFECT_ALPHAFLATTEN = 196,
	VJ_IMAGE_EFFECT_ALPHAFEATHERMASK = 197,
	VJ_IMAGE_EFFECT_ALPHASELECT = 198,
	VJ_IMAGE_EFFECT_ALPHASELECT2 = 199,
	VJ_IMAGE_EFFECT_ALPHANEGATE = 99,
	VJ_IMAGE_EFFECT_CHOKEMATTE = 98,
	VJ_IMAGE_EFFECT_LEVELCORRECTION = 97,
	VJ_IMAGE_EFFECT_ALPHADAMPEN = 96,
	VJ_IMAGE_EFFECT_RANDNOISE = 95,
	VJ_IMAGE_EFFECT_BGSUBTRACTGAUSS = 94,
	VJ_IMAGE_EFFECT_BWOTSU = 93,
	VJ_IMAGE_EFFECT_MEANFILTER = 92,
	VJ_IMAGE_EFFECT_BGPUSH = 91,
	VJ_IMAGE_EFFECT_DUMMY=0,
};



#define	VJ_EXT_EFFECT	500

/* luma blend types */
enum {
	VJ_EFFECT_BLEND_ADDITIVE = 0,
	VJ_EFFECT_BLEND_SUBSTRACTIVE = 1,
	VJ_EFFECT_BLEND_MULTIPLY = 2,
	VJ_EFFECT_BLEND_DIVIDE = 3,
	VJ_EFFECT_BLEND_LIGHTEN = 4,
	VJ_EFFECT_BLEND_HARDLIGHT = 5,
	VJ_EFFECT_BLEND_DIFFERENCE = 6,
	VJ_EFFECT_BLEND_DIFFNEGATE = 7,
	VJ_EFFECT_BLEND_EXCLUSIVE = 8,
	VJ_EFFECT_BLEND_BASECOLOR = 9,
	VJ_EFFECT_BLEND_FREEZE = 10,
	VJ_EFFECT_BLEND_UNFREEZE = 11,
	VJ_EFFECT_BLEND_RELADD = 12,
	VJ_EFFECT_BLEND_RELSUB = 13,
	VJ_EFFECT_BLEND_MAXSEL = 14,
	VJ_EFFECT_BLEND_MINSEL = 15,
	VJ_EFFECT_BLEND_RELADDLUM = 16,
	VJ_EFFECT_BLEND_RELSUBLUM = 17,
	VJ_EFFECT_BLEND_MINSUBSEL = 18,
	VJ_EFFECT_BLEND_MAXSUBSEL = 19,
	VJ_EFFECT_BLEND_ADDSUBSEL = 20,
	VJ_EFFECT_BLEND_ADDAVG = 21,
	VJ_EFFECT_BLEND_ADDTEST2 = 22,
	VJ_EFFECT_BLEND_ADDTEST3 = 23,
	VJ_EFFECT_BLEND_ADDTEST4 = 24,
	VJ_EFFECT_BLEND_MULSUB = 25,
	VJ_EFFECT_BLEND_SOFTBURN = 26,
	VJ_EFFECT_BLEND_INVERSEBURN = 27,
	VJ_EFFECT_BLEND_COLORDODGE = 28,
	VJ_EFFECT_BLEND_ADDDISTORT = 29,
	VJ_EFFECT_BLEND_SUBDISTORT = 30, 
	VJ_EFFECT_BLEND_ADDTEST5 = 31,
	VJ_EFFECT_BLEND_NEGDIV = 32,
	VJ_EFFECT_BLEND_ADDLUM = 33, 
	VJ_EFFECT_BLEND_ADDTEST6 = 34,
	VJ_EFFECT_BLEND_ADDTEST7 = 35,
};

#define VJ_NUM_BLEND_EFFECTS VJ_EFFECT_BLEND_ADDTEST7

#endif
