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

#ifndef VJ_EFFECT_H
#define VJ_EFFECT_H

#include "vj-global.h"

#include <stdint.h>
#include <sys/types.h>


#define VJ_EFFECT_ERROR -1024
#define VJ_EFFECT_DESCR_LEN 200;

#define ARRAY_SIZE(buf) (\
 (int) ( sizeof(buf[0]) / sizeof(uint8_t) ) + \
 (int) ( sizeof(buf[1]) / sizeof(uint8_t) ) +\
 (int) ( sizeof(buf[2]) / sizeof(uint8_t) ) )


/* someday this will be a plugin API, with dlopen()
   for now it is not plugin */

/* todo effman needs to know about effect dataformat */

typedef struct vj_effect_data_t {
    void *internal_params;
    uint8_t *data;
} vj_effect_data;


typedef struct vj_effect_t {
    vj_effect_data *vjed;
    char *description;
    int num_params;
    char **param_description;
    int *defaults;
    int *limits[2];
    int extra_frame;		/* 0 = inplace conversion, 1 = get a clip's frame */
    double dummy;		/* some double value used by chroma key to pre-calc sqrt */
    int sub_format;
    int has_internal_data;      /*specify if effect_free needs to be called */
    int static_bg;
    int has_help;
    //      int dataformat;
    /* effect requires this dataformat */
} vj_effect;



enum {
    VJ_EFFECT_LUM_RED = 65,
    VJ_EFFECT_LUM_BLUE = 35,
    VJ_EFFECT_LUM_WHITE = 240,
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
  //  VJ_VIDEO_EFFECT_CHANNELMIX = 233,
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
    VJ_IMAGE_EFFECT_PIXELATE = 150,
    VJ_IMAGE_EFFECT_MAGICMIRROR = 151,
    VJ_IMAGE_EFFECT_PIXELSMEAR = 152,
    VJ_IMAGE_EFFECT_RASTER = 153,
    VJ_IMAGE_EFFECT_FISHEYE = 154,
    VJ_IMAGE_EFFECT_SWIRL = 155,
    VJ_IMAGE_EFFECT_RADIALBLUR = 156,
    VJ_IMAGE_EFFECT_CHROMIUM = 157,
    VJ_IMAGE_EFFECT_CHROMAPALETTE = 158,
    VJ_IMAGE_EFFECT_DUMMY = 100,
};

#define VJ_IMAGE_EFFECT_MIN 100
#define VJ_IMAGE_EFFECT_MAX 159

#define VJ_VIDEO_EFFECT_MIN 200
#define VJ_VIDEO_EFFECT_MAX 235
#define VJ_VIDEO_COUNT (VJ_VIDEO_EFFECT_MAX - VJ_VIDEO_EFFECT_MIN)

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


/* chroma blend types */
enum {
    VJ_EFFECT_CHROMA_SELECTMIN = 0,
    VJ_EFFECT_CHROMA_SELECTMAX = 1,
    VJ_EFFECT_CHROMA_SELECTDIFF = 2,
    VJ_EFFECT_CHROMA_SELECTUNFREEZE = 3,
    VJ_EFFECT_CHROMA_ADDSUBSELLUM = 4,
    VJ_EFFECT_CHROMA_ADDLUM = 5,
    VJ_EFFECT_CHROMA_SELECTDIFFNEG = 6,
};

/* add uncommented vj_effect_blend types to chroma_Blend,
   int this we can include chroma key rgb by histogram 
   or custom color, also luma and possibly diff
   see chromamagick.c
 */

/* ADDSUBSEL, SUBADDSEL, ADDSUBSELLUM,SUBADDSELLUM,ADDTEST4 */
extern void dices_apply(vj_effect * data, uint8_t *yuv[3], int width,
			int height, int cube_bits);
extern void dither_apply(uint8_t * yuv[3], int width, int height, int size,
			 int n);
extern void emboss_apply(uint8_t * yuv[3], int width, int height, int n);
extern void fibdownscale_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int n);
extern void _fibdownscale_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height);
extern void _fibrectangle_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height);
extern void flip_apply(uint8_t * yuv[3], int width, int height, int n);
extern void killchroma_apply(uint8_t * yuv1[3], int width, int height,
			     int n);
extern void lumamagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int n, int op_a, int op_b
			    );
extern void overlaymagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int n);
extern void mirrors_apply(uint8_t * yuv[3], int width, int height,
			  int type, int factor);
extern void mirrors2_apply(uint8_t * yuv[3], int width, int height,
			   int type);
extern void negation_apply(uint8_t * yuv[3], int width, int height,
			   int val);
extern void opacity_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int opacity);
extern void opacityadv_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int w,
			     int h, int o, int t1, int t2);
extern void opacitythreshold_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
				   int w, int h, int o, int t1, int t2);

extern void posterize_apply(uint8_t * yuv[3], int width, int height,
			    int factor, int t1, int t2);
extern void revtv_apply(uint8_t * yuv[3], int width, int height, int space,
			int vscale, int c, int cn);
extern void softblur_apply(uint8_t * yuv[3], int width, int height, int n,
			   int type);
extern void split_apply(uint8_t * yuv[3], uint8_t * yuv2[3], int width,
			int height, int n, int swap);
extern void widthmirror_apply(uint8_t * yuv[3], int width, int height,
			      int div);
extern void transblend_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int w,
			     int h, int mode, int twidth, int theight,
			     int x1, int y1, int x2, int y2);

extern void borders_apply(uint8_t * yuv1[3], int width, int height,
			  int size, int color);
extern void frameborder_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int size);
extern void noisepencil_apply(uint8_t *yuv1[3], int width, int height,
	int a, int b, int c , int d );

extern void rawman_apply(uint8_t * yuv1[3], unsigned int width,
			 unsigned int height, unsigned int mode,
			 unsigned int Y);
extern void rawval_apply(uint8_t * yuv1[3], int width, int height,
			 int color_cb, int color_cr, int new_cb,
			 int new_cr);
extern void smuck_apply(uint8_t * yuv[3], uint8_t * yuv2[3], int width,
			int height, int level);
extern void colorfade_apply(uint8_t * yuv[3], int width, int height,
			    int op, int color);
extern void slidingdoor_apply(uint8_t * yuv[3], uint8_t * yuv2[3],
			      int width, int height, int n, int size);
extern void transop_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int twidth,
			  int theight, int x1, int y1, int x2, int y2,
			  int width, int height, int opacity);

extern void lumakey_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int a, int b, int c, int d, int e);
extern void pointfade_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int pointsize,
			    int opacity);

extern void transcarot_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int point_size, int dy,
			     int dye, int row_start, int opacity,
			     int type);

extern
void transline_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		     int height, int distance, int line_width, int opacity,
		     int type);
extern void transform_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int size);

extern void coloradjust_apply(uint8_t * yuv[3], int width, int height,
			      int val, int degrees);

extern void rgbkey_apply(uint8_t * src1[3], uint8_t * src2[3], int width,
			 int height, int i_angle, int i_noise,
			 int suppress, int r, int g, int b);

extern void gamma_apply(uint8_t * src1[3],
			int width, int height, int val);

extern void solarize_apply(uint8_t * src1[3], int width, int height,
			   int threshold);
extern void dummy_apply(uint8_t * dst1[3], int width, int height,
			int color_num);
extern void rotozoom_apply(uint8_t * yuv1[3], int width, int height, int a,
			   int b, int c, int d);

extern void whiteframe_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int val, int degrees,
			     int type);

extern void diff_apply(vj_effect_data *dd, uint8_t * yuv1[3],
		       uint8_t * yuv2[3], int width, int height, 
		       int mode, int threshold, int c, int d);

extern void chromamagick_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int type, int op0);
extern void colorfadergb_apply(uint8_t * yuv1[3], int width, int height,
			       int opacity, int r, int g, int b);

void lumablend_apply(uint8_t * yuv[3], uint8_t * yuv2[3], int width,
		     int height, int type, int t1, int t2, int op);

extern void diffimg_apply(uint8_t * yuv1[3], 
			  int width, int height, int type, int delta,
			  int zeta);
extern void rgbkeysmooth_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int w,
			       int h, int angle, int r, int g, int b,
			       int level);
extern void scratcher_apply(uint8_t * yuv1[3], int w, int h, int o, int n,
			    int r);
extern void colorshift_apply(uint8_t * yuv1[3], int width, int height,
			     int type, int param);
extern void reflection_apply(uint8_t * yuv1[3], int w, int h, int n1,
			     int n2, int n3);
extern void distortion_apply(uint8_t * yuv1[3], int w, int h, int p1,
			     int p2);
extern void magicscratcher_apply(uint8_t * yuv1[3], int w, int h, int mode,
				 int nframes, int r);
extern void wipe_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int w, int h,
		       int inc, int opacity);
/* begin API */
extern void chromascratcher_apply(uint8_t * yuv1[3],
				  int width, int height, int mode,
				  int opacity, int nframes,
				  int no_reverse);

extern void tracer_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			 int w, int h, int opacity, int n);

extern void mtracer_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int w,
			  int h, int mode, int n);

extern void keyselect_apply(uint8_t *yuv1[3], uint8_t *yuv2[3],int w,int h, int angle,int r,
	int g, int b, int mode);

extern void greyselect_apply(uint8_t *yuv1[3], int w, int h, int angle, int r, int g, int b);
extern void isolate_apply(uint8_t *yuv1[3], int w, int h, int angle, int r, int g, int b,
int opacity);

extern void bwselect_apply(uint8_t *yuv1[3], int w, int h, int a , int b);

extern void complexinvert_apply(uint8_t *yuv1[3], int w, int h, int angle, int r, int g, int b, int level);

extern void complexsaturation_apply(uint8_t *yuv1[3], int w, int h, int angle, int r, int g, int b, int adj, int adjv);

extern void complexthreshold_apply(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int angle, int r, 
	int g, int b, int level, int threshold);
		
extern void complexsync_apply(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int val );

extern void enhancemask_apply(uint8_t *y[3],int w, int h, int *t);

extern void contrast_apply(uint8_t *y[3], int w, int h, int *t);

extern void noiseadd_apply(uint8_t *y[3], int w , int h , int t, int n);

extern void motionblur_apply(uint8_t *y[3], int w, int h, int n);

extern void sinoids_apply(uint8_t *y[3], int w, int h, int a,int b);

extern void dupmagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
                    int height, int n);

extern void simplemask_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
                   int height, int threshold, int invert);

extern void bar_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
                   int width, int height, int d, int x1, int x2, int t1, int b1);

extern void vbar_apply(uint8_t * yuv1[3], uint8_t *yuv[3],
	int w, int h, int d, int x1, int x2, int t1, int t2);

extern void average_apply(uint8_t *y[3], int w, int h, int val);

extern void ripple_apply(uint8_t *yuv[3], int width, int height, int waves, int ampli,int atten);

extern void bathroom_apply(uint8_t *y[3], int width, int height, int mode, int val);

extern void slice_apply(uint8_t *y[3], int width, int height, int val, int reinit);

extern void zoom_apply(uint8_t *y[3], int w, int h , int xo, int yo, int f);

extern void deinterlace_apply(uint8_t *y[3], int w, int h, int val);

extern void simplematte_apply(uint8_t *y[3], int w, int h, int threshold, int invert);

extern void crosspixel_apply(uint8_t *y[3], int w, int h,int type, int val);

extern void color_apply(uint8_t *y[3], int w, int h, int a,int b, int c);

extern void water_apply(uint8_t *y[3], int w, int h, int val);

extern void pencilsketch_apply(uint8_t * yuv[3], int w, int h, int type, int threshold, int opacity);

extern void pixelate_apply(uint8_t *yuv[3], int w, int h, int v );

extern void magicmirror_apply(uint8_t *yuv[3], int w, int h, int x, int y, int d, int n );

extern void lumamask_apply(uint8_t *yuv[3],uint8_t *yuv2[3], int w, int h, int n, int m);

extern void smear_apply(uint8_t *yuv[3], int w, int h, int n, int m);

extern void raster_apply(uint8_t *yuv[3], int w, int h, int v );

extern void fisheye_apply(uint8_t *yuv[3], int w, int h, int v );

extern void swirl_apply(uint8_t *yuv[3], int w, int h , int v );

extern void radialblur_apply(uint8_t *yuv[3], int w, int h, int r, int p, int n);

extern void binaryoverlay_apply(uint8_t *yuv1[3], uint8_t *yuv2[3],int w, int h, int n);

extern void chromium_apply( uint8_t *yuv1[3], int w, int h, int n);

extern void chromapalette_apply( uint8_t *yuv[3], int w, int h, int a, int r, int g, int b, int c1, int c2);

//extern void yuvchannelmix_apply(uint8_t *yuv1[3],uint8_t *yuv2[3], int width, int height,
//		 int opacity_a, int opacity_b,
//		 int opacity_c, int opacity_a2, int opacity_b2, int opacity_c2, int mode, int luma);

void vj_effect_initialize(int width, int height);
void vj_effect_shutdown();
inline int vj_effect_real_to_sequence(int effect_id);
char *vj_effect_get_description(int effect_id);
char *vj_effect_get_param_description(int effect_id, int param_nr);
int vj_effect_get_extra_frame(int effect_id);
int vj_effect_get_num_params(int effect_id);
int vj_effect_get_default(int effect_id, int param_nr);
int vj_effect_get_min_limit(int effect_id, int param_nr);
int vj_effect_get_max_limit(int effect_id, int param_nr);
int vj_effect_valid_value(int effect_id, int param_nr, int value);
int vj_effect_get_subformat(int effect_id);
void vj_effect_update_max_limit(int effect_id, int param_nr, int value);
int vj_effect_get_real_id(int effect_id);
int vj_effect_is_valid(int effect_id);
int vj_effect_get_summary(int entry, char *dst);

int vj_effect_activate(int e);
int vj_effect_deactive(int e);
int vj_effect_initialized(int e);
#endif
