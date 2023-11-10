/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "rgbkeysmooth.h"
#include "softblur.h"

vj_effect *rgbkeysmooth_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 1200; /* angle */
    ve->defaults[1] = 0;    /* r */
    ve->defaults[2] = 0;    /* g */
    ve->defaults[3] = 255;  /* b */
    ve->defaults[4] = 0;    /* opacity */
    ve->defaults[5] = 255;  /* opacity */
    ve->defaults[6] = 150;  /* noise */
    ve->defaults[7] = 0;    /* mode */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 255;

    ve->limits[0][7] = 0;
    ve->limits[1][7] = 3;

    ve->has_user = 0;
    ve->description = "Chroma Key with Feathering";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list( ve->num_params,"Angle","Red","Green","Blue","Erosion Opacity","Dilation Opacity", "Noise level", "Preview Mask");
    return ve;
}

typedef struct {
    uint8_t *matte;
    uint8_t *eroded_mask;
    uint8_t *mask;
    uint8_t *blurred_mask;
} rgbkey_t;

void *rgbkeysmooth_malloc(int w, int h)
{
    rgbkey_t *t = (rgbkey_t*) vj_malloc(sizeof(rgbkey_t));
    if(!t) {
        return NULL;
    }
    t->matte = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h * 4);
    if(!t->matte) {
        free(t);
        return NULL;
    }
    t->eroded_mask = t->matte + (w*h);
    t->mask = t->eroded_mask + (w*h);
    t->blurred_mask = t->mask +(w*h);
    return (void*) t;
}

void rgbkeysmooth_free(void *ptr) {
    rgbkey_t *t = (rgbkey_t*) ptr;
    free(t->matte);
    free(t);
}

void dilate_and_smooth_matte(uint8_t *dilated_mask, const uint8_t *mask, int width, int height, int opacity) {
    for (int y = 1; y < height - 1; ++y) {
#pragma omp simd
	for (int x = 1; x < width - 1; ++x) {
            int max_val = 0;
            for (int i = -1; i <= 1; ++i) {
                for (int j = -1; j <= 1; ++j) {
                    int val = mask[(y + i) * width + (x + j)];
                    if (val > max_val) {
                        max_val = val;
                    }
                }
            }
            if( max_val > 0 ) {
                max_val = (uint8_t) ((max_val * opacity) >> 8);
            }
            dilated_mask[y * width + x] = max_val;
        }
    }
}


void erode_and_smooth_matte(uint8_t *eroded_mask, const uint8_t *matte_mask, int width, int height, int opacity) {
    for (int y = 1; y < height - 1; ++y) {
#pragma omp simd
	    for (int x = 1; x < width - 1; ++x) {
            int sum = 9 * matte_mask[y * width + x];
            sum -= matte_mask[(y - 1) * width + x - 1];
            sum -= matte_mask[(y - 1) * width + x];
            sum -= matte_mask[(y - 1) * width + x + 1];
            sum -= matte_mask[y * width + x - 1];
            sum -= matte_mask[y * width + x + 1];
            sum -= matte_mask[(y + 1) * width + x - 1];
            sum -= matte_mask[(y + 1) * width + x];
            sum -= matte_mask[(y + 1) * width + x + 1];

            eroded_mask[y * width + x] = (sum == 9 * 255) ? 255 : 0;

        }
    }

    for (int y = 0; y < height; ++y) {
#pragma omp simd
	for (int x = 0; x < width; ++x) {
            eroded_mask[y * width + x] = (((matte_mask[y * width + x] * (255 - opacity) + eroded_mask[y * width + x] * opacity) >> 8));
        }
    }

}

void blur_matte(uint8_t *blurred_mask, const uint8_t *mask, int width, int height, int blur_radius) {
    for (int y = blur_radius; y < height - blur_radius; ++y) {
        for (int x = blur_radius; x < width - blur_radius; ++x) {
            int blur_sum = 0;
            for (int i = -blur_radius; i <= blur_radius; ++i) {
                for (int j = -blur_radius; j <= blur_radius; ++j) {
                    blur_sum += mask[(y + i) * width + (x + j)];
                }
            }
            blurred_mask[y * width + x] = (uint8_t)(blur_sum / ((2 * blur_radius + 1) * (2 * blur_radius + 1)));
        }
    }
}


void rgbkeysmooth_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    rgbkey_t *t = (rgbkey_t*) ptr;

    int r = args[1];
    int g = args[2];
    int b = args[3];
    int angleThreshold = (args[0] * 0.01f);
    int noiseParam = args[6];
    int erosion_opacity = args[4];
    int dilation_opacity = args[5];
    int mode = args[7];

    int iy=0, iu=128, iv=128;
    _rgb2yuv(r, g, b, iy, iu, iv);

    int angleThresholdSquared = angleThreshold * angleThreshold;
    int noiseThreshold = noiseParam * noiseParam;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];
    const int len = frame->len;

    int pos;

    uint8_t *matte = t->matte + frame->offset;
    uint8_t *eroded_matte = t->eroded_mask + frame->offset;
    uint8_t *fg = t->mask + frame->offset;
    uint8_t *dilated_matte = t->blurred_mask + frame->offset;

#pragma omp simd
    for ( pos = 0; pos < len; pos++) {
        int Y_diff = abs(Y[pos] - iy);
        int Cb_diff = abs(Cb[pos] - iu);
        int Cr_diff = abs(Cr[pos] - iv);

        int angleDiffSquared = (Cb_diff * Cb_diff) + (Cr_diff * Cr_diff);

        int distanceSquared = Y_diff * Y_diff;

        if (angleDiffSquared <= angleThresholdSquared && distanceSquared <= noiseThreshold) {
            matte[pos] = 0;
        }
        else {
            matte[pos] = 0xff;
        }   
    }

    erode_and_smooth_matte( eroded_matte, matte, frame->width, frame->height, erosion_opacity );

    dilate_and_smooth_matte( dilated_matte, eroded_matte, frame->width, frame->height, dilation_opacity );

    blur_matte( fg, dilated_matte, frame->width,frame->height, 2 );

    if( mode == 0 ) {
        for( pos = 0; pos < len ; pos ++ ) {
            unsigned int mask = fg[pos];
            unsigned int invMask = 0xff - mask;
        
            Y[pos]  = (( Y[pos] * mask +  Y2[pos] * invMask) >> 8);
            Cb[pos] = ((Cb[pos] * mask + Cb2[pos] * invMask) >> 8);
            Cr[pos] = ((Cr[pos] * mask + Cr2[pos] * invMask) >> 8);    
        }
    }
    else {
        veejay_memset( Cb, 128, frame->len );
        veejay_memset( Cr, 128, frame->len );

        switch(mode) {
          case 1:
                veejay_memcpy( Y, matte, frame->len );
            break;
          case 2:
                veejay_memcpy( Y, eroded_matte, frame->len );
            break;
          case 3:
                veejay_memcpy( Y, fg, frame->len );
            break;
        }

    }
}

