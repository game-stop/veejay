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
	
};

enum {
    /* image effects */
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
	VJ_IMAGE_EFFECT_DUMMY=100,
};

#define VJ_IMAGE_EFFECT_MIN 100
#define VJ_IMAGE_EFFECT_MAX 192

#define VJ_VIDEO_EFFECT_MIN 200
#define VJ_VIDEO_EFFECT_MAX 248

#define VJ_VIDEO_COUNT (VJ_VIDEO_EFFECT_MAX - VJ_VIDEO_EFFECT_MIN)

#define	VJ_EXT_EFFECT	500

/* luma blend types */
enum {
    VJ_EFFECT_BLEND_ADDITIVE = 1,
    VJ_EFFECT_BLEND_SUBSTRACTIVE = 2,
    VJ_EFFECT_BLEND_MULTIPLY = 3,
    VJ_EFFECT_BLEND_DIVIDE = 4,
    VJ_EFFECT_BLEND_LIGHTEN = 5,
    VJ_EFFECT_BLEND_HARDLIGHT = 6,
    VJ_EFFECT_BLEND_DIFFERENCE = 7,
    VJ_EFFECT_BLEND_DIFFNEGATE = 8,
    VJ_EFFECT_BLEND_EXCLUSIVE = 9,
    VJ_EFFECT_BLEND_BASECOLOR = 10,
    VJ_EFFECT_BLEND_FREEZE = 11,
    VJ_EFFECT_BLEND_UNFREEZE = 12,
    VJ_EFFECT_BLEND_RELADD = 13,
    VJ_EFFECT_BLEND_RELSUB = 14,
    VJ_EFFECT_BLEND_MAXSEL = 15,
    VJ_EFFECT_BLEND_MINSEL = 16,
    VJ_EFFECT_BLEND_RELADDLUM = 17,
    VJ_EFFECT_BLEND_RELSUBLUM = 18,
    VJ_EFFECT_BLEND_MINSUBSEL = 19,
    VJ_EFFECT_BLEND_MAXSUBSEL = 20,
    VJ_EFFECT_BLEND_ADDSUBSEL = 21,
    VJ_EFFECT_BLEND_ADDAVG = 22,
    VJ_EFFECT_BLEND_ADDTEST2 = 23,
    VJ_EFFECT_BLEND_ADDTEST3 = 24,
    VJ_EFFECT_BLEND_ADDTEST4 = 25,
    VJ_EFFECT_BLEND_MULSUB = 26,
    VJ_EFFECT_BLEND_SOFTBURN = 27,
    VJ_EFFECT_BLEND_INVERSEBURN = 28,
    VJ_EFFECT_BLEND_COLORDODGE = 29,
    VJ_EFFECT_BLEND_ADDDISTORT = 30,
    VJ_EFFECT_BLEND_SUBDISTORT = 31,

};
extern void tripplicity_apply(VJFrame *frame1,VJFrame *frame2, int w, int h,
		int a, int b, int c );

extern void dices_apply(void * data, VJFrame *frame, int width,
			int height, int cube_bits);
extern void dither_apply( VJFrame *frame, int width, int height, int size,
			 int n);
extern void emboss_apply( VJFrame *frame, int width, int height, int n);
extern void fibdownscale_apply(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int n);
extern void _fibdownscale_apply(VJFrame *frame, VJFrame *frame2,
				int width, int height);
extern void _fibrectangle_apply(VJFrame *frame, VJFrame *frame2,
				int width, int height);
extern void flip_apply( VJFrame *frame, int width, int height, int n);
extern void killchroma_apply(VJFrame *frame, int width, int height,
			     int n);
extern void lumamagic_apply(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int n, int op_a, int op_b
			    );
extern void overlaymagic_apply(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int n, int m);
extern void mirrors_apply( VJFrame *frame, int width, int height,
			  int type, int factor);
extern void mirrors2_apply( VJFrame *frame, int width, int height,
			   int type);
extern void negation_apply( VJFrame *frame, int width, int height,
			   int val);
extern void medianfilter_apply( VJFrame *frame, int width, int height,
			   int val);

extern void negatechannel_apply( VJFrame *frame, int width, int height,
			  int chan, int val);

extern void colormap_apply( VJFrame *frame, int width, int height,
			   int r, int g, int b);
extern void opacity_apply(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int opacity);
extern void opacityadv_apply(VJFrame *frame, VJFrame *frame2, int w,
			     int h, int o, int t1, int t2);
extern void opacitythreshold_apply(VJFrame *frame, VJFrame *frame2,
				   int w, int h, int o, int t1, int t2);


extern void posterize_apply( VJFrame *frame, int width, int height,
			    int factor, int t1, int t2);
extern void revtv_apply( VJFrame *frame, int width, int height, int space,
			int vscale, int c, int cn);
extern void softblur_apply( VJFrame *frame, int width, int height, int n);
extern void split_apply( VJFrame *frame, VJFrame *frame2, int width,
			int height, int n, int swap);
extern void widthmirror_apply( VJFrame *frame, int width, int height,
			      int div);
extern void transblend_apply(VJFrame *frame, VJFrame *frame2, int w,
			     int h, int mode, int twidth, int theight,
			     int x1, int y1, int x2, int y2);

extern void borders_apply(VJFrame *frame, int width, int height,
			  int size, int color);
extern void frameborder_apply(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int size);
extern void noisepencil_apply(VJFrame *frame, int width, int height,
	int a, int b, int c , int d );

extern void rawman_apply(VJFrame *frame, unsigned int width,
			 unsigned int height, unsigned int mode,
			 unsigned int Y);
extern void rawval_apply(VJFrame *frame, int width, int height,
			 int color_cb, int color_cr, int new_cb,
			 int new_cr);
extern void smuck_apply( VJFrame *frame, VJFrame *frame2, int width,
			int height, int level);
extern void colorfade_apply( VJFrame *frame, int width, int height,
			    int op, int color);
extern void slidingdoor_apply( VJFrame *frame, VJFrame *frame2,
			      int width, int height, int size);
extern void transop_apply(VJFrame *frame, VJFrame *frame2, int twidth,
			  int theight, int x1, int y1, int x2, int y2,
			  int width, int height, int opacity);

extern void lumakey_apply(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int a, int b, int c, int d, int e);
extern void pointfade_apply(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int pointsize,
			    int opacity);
extern	void	slicer_apply( VJFrame *frame, VJFrame *frame2, int width, int height, int a, int b );

extern void transcarot_apply(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int point_size, int dy,
			     int dye, int row_start, int opacity,
			     int type);

extern
void transline_apply(VJFrame *frame, VJFrame *frame2, int width,
		     int height, int distance, int line_width, int opacity,
		     int type);
extern void transform_apply(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int size);

extern void coloradjust_apply( VJFrame *frame, int width, int height,
			      int val, int degrees);

extern void rgbkey_apply( VJFrame *frame, VJFrame *frame2, int width,
			 int height, int i_angle, int i_noise,
			 int r, int g, int b, int sup);

extern void gamma_apply( VJFrame *frame,
			int width, int height, int val);

extern void solarize_apply(VJFrame *frame, int width, int height,
			   int threshold);
extern void dummy_apply(VJFrame *frame, int width, int height,
			int color_num);
extern void rotozoom_apply(VJFrame *frame, int width, int height, int a,
			   int b, int c, int d);

extern void whiteframe_apply(VJFrame *frame, VJFrame *frame2,
			     int width, int height);

extern void texmap_apply(void *dd, VJFrame *frame,
		       VJFrame *frame2, int width, int height, 
		       int mode, int threshold, int c , int feather, int blob);
extern void contourextract_apply(void *dd, VJFrame *frame,
		       int width, int height, 
		       int mode, int threshold, int c , int feather, int blob);

extern void diff_apply(void *dd, VJFrame *frame,
		       VJFrame *frame2, int width, int height, 
		       int mode, int threshold, int c ,int feather);

extern void chromamagick_apply(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int type, int op0);
extern void colorfadergb_apply(VJFrame *frame, int width, int height,
			       int opacity, int r, int g, int b);

void lumablend_apply(VJFrame *frame, VJFrame *frame2, int width,
		     int height, int type, int t1, int t2, int op);

extern void diffimg_apply(VJFrame *frame, 
			  int width, int height, int type, int delta,
			  int zeta);
extern void rgbkeysmooth_apply(VJFrame *frame, VJFrame *frame2, int w,
			       int h, int angle, int r, int g, int b,
			       int level, int noise);
extern void scratcher_apply(VJFrame *frame, int w, int h, int o, int n,
			    int r);
extern void colorshift_apply(VJFrame *frame, int width, int height,
			     int type, int param);
extern void reflection_apply(VJFrame *frame, int w, int h, int n1,
			     int n2, int n3);
extern void distortion_apply(VJFrame *frame, int w, int h, int p1,
			     int p2);
extern void magicscratcher_apply(VJFrame *frame, int w, int h, int mode,
				 int nframes, int r);
extern void wipe_apply(VJFrame *frame, VJFrame *frame2, int w, int h,
		       int inc, int opacity);
/* begin API */
extern void chromascratcher_apply(VJFrame *frame,
				  int width, int height, int mode,
				  int opacity, int nframes,
				  int no_reverse);

extern void tracer_apply(VJFrame *frame, VJFrame *frame2,
			 int w, int h, int opacity, int n);

extern void mtracer_apply(VJFrame *frame, VJFrame *frame2, int w,
			  int h, int mode, int n);

extern void keyselect_apply(VJFrame *frame, VJFrame *frame2,int w,int h, int angle,int r,
	int g, int b, int mode, int noise);

extern void greyselect_apply(VJFrame *frame, int w, int h, int angle, int r, int g, int b);
extern void isolate_apply(VJFrame *frame, int w, int h, int angle, int r, int g, int b,
int opacity);

extern void bwselect_apply(VJFrame *frame, int w, int h, int a , int b);

extern void complexinvert_apply(VJFrame *frame, int w, int h, int angle, int r, int g, int b, int i_noise);

extern void complexsaturation_apply(VJFrame *frame, int w, int h, int angle, int r, int g, int b, int adj, int adjv, int inoise);

extern void complexthreshold_apply(VJFrame *frame, VJFrame *frame2, int w, int h, int angle, int r, 
	int g, int b, int level, int threshold);
		
extern void complexsync_apply(VJFrame *frame, VJFrame *frame2, int w, int h, int val );

extern void enhancemask_apply(VJFrame *frame,int w, int h, int *t);

extern void contrast_apply(VJFrame *frame, int w, int h, int *t);

extern void noiseadd_apply(VJFrame *frame, int w , int h , int t, int n);

extern void motionblur_apply(VJFrame *frame, int w, int h, int n);

extern void sinoids_apply(VJFrame *frame, int w, int h, int a,int b);

extern void dupmagic_apply(VJFrame *frame, VJFrame *frame2, int width,
                    int height, int n);

extern void simplemask_apply(VJFrame *frame, VJFrame *frame2, int width,
                   int height, int threshold, int invert);

extern void bar_apply(VJFrame *frame, VJFrame *frame2,
                   int width, int height, int d, int x1, int x2, int t1, int b1);

extern void vbar_apply(VJFrame *frame, VJFrame *frame2,
	int w, int h, int d, int x1, int x2, int t1, int t2);

extern void average_apply(VJFrame *frame, int w, int h, int val);

extern void ripple_apply(VJFrame *frame, int width, int height, int waves, int ampli,int atten);

extern void bathroom_apply(VJFrame *frame, int width, int height, int mode, int val);

extern void slice_apply(VJFrame *frame, int width, int height, int val, int reinit);

extern void zoom_apply(VJFrame *frame, int w, int h , int xo, int yo, int f, int dir);

extern void deinterlace_apply(VJFrame *frame, int w, int h, int val);

extern void simplematte_apply(VJFrame *frame, int w, int h, int threshold, int invert);

extern void crosspixel_apply(VJFrame *frame, int w, int h,int type, int val);

extern void color_apply(VJFrame *frame, int w, int h, int a,int b, int c);

//extern void water_apply(VJFrame *frame, int w, int h, int val, int l, int d);
extern void	water_apply(void *user_data, VJFrame *frame, VJFrame *frame2, int width, int height, int fresh,int loopnum, int decay, int mode, int threshold);

extern void pencilsketch_apply(VJFrame *frame, int w, int h, int type, int threshold, int opacity, int mode);

extern void pixelate_apply(VJFrame *frame, int w, int h, int v );

extern void magicmirror_apply(VJFrame *frame, int w, int h, int x, int y, int d, int n );

extern void lumamask_apply(VJFrame *frame,VJFrame *frame2, int w, int h, int n, int m, int border);

extern void smear_apply(VJFrame *frame, int w, int h, int n, int m);

extern void raster_apply(VJFrame *frame, int w, int h, int v );

extern void fisheye_apply(VJFrame *frame, int w, int h, int v );

extern void swirl_apply(VJFrame *frame, int w, int h , int v );

extern void radialblur_apply(VJFrame *frame, int w, int h, int r, int p, int n);

extern void binaryoverlay_apply(VJFrame *frame, VJFrame *frame2,int w, int h, int n);

extern void chromium_apply( VJFrame *frame, int w, int h, int n);

extern void chromapalette_apply( VJFrame *frame, int w, int h, int a, int r, int g, int b, int c1, int c2);


extern void uvcorrect_apply(VJFrame *frame, int width, int height, int angle, int urot_center, int vrot_center, int iuFactor, int ivFactor, int uvmin, int uvmax );

extern void dissolve_apply(VJFrame *frame,VJFrame *frame2, int w, int h, int opacity);

extern void overclock_apply(VJFrame *frame, int w, int h, int val, int r);

extern int bgsubstract_prepare(void *user, uint8_t *map[4], int width, int height);

extern void bgsubstract_apply(VJFrame *frame,int width, int height, int mode, int threshold );

extern int diff_prepare(void *data, uint8_t *map[4], int w, int h);

extern void	cartonize_apply( VJFrame *frame, int w, int h, int b1, int b2, int b3 );

extern void 	morphology_apply( VJFrame *frame, int w, int h, int t, int v, int p);

extern void 	colmorphology_apply( VJFrame *frame, int w, int h, int t, int v, int p);

extern void		blob_apply( VJFrame *frame, int w, int h, int p0,int p1, int p2, int p3);

extern void		boids_apply( VJFrame *frame, int w, int h, int p0,int p1, int p2, int p3, int p4, int p5, int p6, int p7
);


extern void		ghost_apply(VJFrame *frame, int w, int h, int o );
extern void		neighbours_apply( VJFrame *frame, int width, int height, int brush_size, int level, int mode);
extern void		neighbours2_apply( VJFrame *frame, int width, int height, int brush_size, int level, int mode);
extern void		neighbours3_apply( VJFrame *frame, int width, int height, int brush_size, int level, int mode);
extern void		neighbours4_apply( VJFrame *frame, int width, int height, int radius, int brush_size, int level, int mode);
extern void		neighbours5_apply( VJFrame *frame, int width, int height, int radius, int brush_size, int level);
extern void cutstop_apply( VJFrame *frame,
        int width, int height, int treshold,
        int freq, int cutmode, int holdmode);
extern void maskstop_apply( VJFrame *frame,
        int width, int height, int treshold,
        int freq, int cutmode, int holdmode);
extern void photoplay_apply(VJFrame *frame, int w, int h, int a, int b, int c);

extern void videoplay_apply(VJFrame *frame,VJFrame *B, int w, int h, int a, int b, int c);

extern void videowall_apply(VJFrame *frame,VJFrame *B, int w, int h, int a, int b, int c, int d);

extern void flare_apply(VJFrame *frame, int w, int h, int type, int threshold, int radius );

extern void constantblend_apply(VJFrame *frame , int w, int h, int type, int scale, int y );

extern void picinpic_apply( void *user_data, VJFrame *frame, VJFrame *frame2,
		   int twidth, int theight, int x1, int y1,
		   int width, int height);

extern void threshold_apply( VJFrame *frame, VJFrame *frame2,int width, int height, int threshold, int reverse );

extern void	motionmap_apply( VJFrame *frame, int w, int h, int threshold, int reverse, int draw, int his, int op );

extern void rgbchannel_apply( VJFrame *frame, int width, int height, int chr, int chg , int chb);

extern	void	differencemap_apply( VJFrame *f, VJFrame *f2, int w, int h, int t1, int rev, int show );

extern void autoeq_apply( VJFrame *frame, int width, int height, int val, int i, int s);

extern void colorhis_apply( VJFrame *frame, int w, int h, int v, int m, int i, int s );

extern void diff_destroy();

extern void texmap_destroy();

extern void contourextract_destroy();

extern void distortion_destroy();

extern void rotozoom_destroy();

extern void timedistort_apply( VJFrame *frame, int w, int h, int val );

extern void chameleon_apply( VJFrame *frame, int w, int h, int mode );

extern void	chameleonblend_apply( VJFrame *frame, VJFrame *source, int w, int h, int mode );

extern void	baltantv_apply (VJFrame *frame, int w, int h , int stride, int mode );

extern	void	radioactivetv_apply( VJFrame *a, VJFrame *b,int w, int h, int mode, int t, int sn, int threhold);

extern void nervous_apply(VJFrame *Frame, int width, int height,int delay);

extern void colflash_apply( VJFrame *frame, int width, int height, int f,int r, int g, int b, int d);

extern	void	iris_apply( VJFrame *frame,VJFrame *frame2, int width, int height, int value, int shape );

extern void cali_apply(void *d , VJFrame *frame,
                int width, int height, 
                int mode, int full);

extern void waterrippletv_apply(VJFrame *frame, int width, int height, int fresh_rate, int loopnum, int decay);

extern void radcor_apply( VJFrame *frame, int width, int height, int a, int b, int c);

extern void bgsubtract_apply(VJFrame *frame,int width,int height,int mode, int threshold);

extern int motionmap_prepare( uint8_t *map[4], int w, int h );
extern int chameleon_prepare( uint8_t *bg[4], int w, int h );
extern int bgsubtract_prepare(uint8_t *map[4], int w, int h); 
extern int contourextract_prepare(uint8_t *map[4], int w, int h);
extern int chameleonblend_prepare( uint8_t *bg[4],int w, int h );

extern void average_blend_applyN( VJFrame *frame, VJFrame *frame2, int width,  int height, int average_blend);

extern void average_blend_apply( VJFrame *frame, VJFrame *frame2, int width,int height, int average_blend);
#endif
