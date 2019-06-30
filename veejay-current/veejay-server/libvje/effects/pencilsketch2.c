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
#include "pencilsketch2.h"

/**
 * Initially based on 'sketchify', added gamma compr, strength,contrast and level parmeters.
 * https://github.com/rra94/sketchify
 */

/**
 * This FX works in the following order:
 * 1. Invert luminance channel
 * 2. Blur the inverted luminance channel
 * 3. Dodge blend the blurred image
 * 4. Apply Posterize (if levels > 0)
 * 5. Apply Histogram equaliser (if strength > 0)
 * 6. Apply Contrast (if contrast > 0 )
 * 7. Apply Gamma Correction
 *
 */

vj_effect *pencilsketch2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 3;
    ve->limits[1][0] = 128;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 9000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;
    ve->defaults[0] = 24;
    ve->defaults[1] = 9000;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 1;
    ve->description = "Sketchify";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Blur Radius", "Gamma Compression", "Strength", "Contrast", "Levels", "Grayscale" );
    return ve;
}

static uint8_t *pencilbuf = NULL;
static uint8_t *pencilhblur = NULL;
static uint8_t *pencilvblur = NULL;
static void *histogram_ = NULL;
#define GAMMA_MAX 256
static double *gamma_table = NULL;
static double gamma_value = 0;
static void gammacompr_setup();

int  pencilsketch2_malloc(int w, int h) {
    if(pencilbuf == NULL) {
        pencilbuf = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w*h*3) );
        if(!pencilbuf) 
            return 0;
        pencilhblur = pencilbuf + RUP8(w*h);
        pencilvblur = pencilhblur + RUP8(w*h);
    }
    if( histogram_ ) {
        veejay_histogram_del(histogram_);
    }
    histogram_ = veejay_histogram_new();
    if(gamma_table == NULL) {
        gamma_table = (double**) vj_calloc(sizeof(double) * GAMMA_MAX );
        gamma_value = 9000;
        gammacompr_setup();
    }
    return 1;
}

void pencilsketch2_free() {
    if( pencilbuf ) {
        free(pencilbuf);
        pencilbuf = NULL;
    }
    if( histogram_) {
        veejay_histogram_del(histogram_);
        histogram_ = NULL;
    }
    if( gamma_table ) {
        free(gamma_table);
        gamma_table = NULL;
    }
}

static void gammacompr_setup()
{
    int i;
    double val;
    double gm = (double) GAMMA_MAX;
    for (i = 0; i < GAMMA_MAX; i++) {
         val = i / gm;
         val = pow(val, gamma_value + ((double) i * 0.01));
         val = gm * val;
         gamma_table[i] = (val < 0.0 ? 0.0 : val > 255.0 ? 255.0 : val);
    }
}


static void rhblur_apply( uint8_t *dst , uint8_t *src, int w, int h, int r)
{
    int y;
    for(y = 0; y < h ; y ++ )
    {
        blur( dst + y * w, src + y *w , w, r,1, 1);
    }       
}

static void rvblur_apply( uint8_t *dst, uint8_t *src, int w, int h, int r)
{
    int x;
    for(x=0; x < w; x++)
    {
        blur( dst + x, src + x , h, r, w, w );
    }
}

static void pencilsketch2_negate(uint8_t *src, const int len)
{
    int i;
    uint8_t *dst = pencilbuf;
    for( i = 0; i < len; i ++ ) {
        dst[i] = 0xff - src[i];
    }
}

static void pencilsketch2_posterize(uint8_t *dst, const int len, const int levels)
{
    const int factor = (256 / levels);
    int i;
    for( i = 0; i < len; i ++ ) {
        dst[i] = dst[i] - (dst[i] % factor);
    }       
}

static void pencilsketch2_dodge(uint8_t *result, uint8_t *front, uint8_t *back, const int len)
{
    int i;
    for( i = 0; i < len; i ++ ) {
       
        if( back[i] == 0xff ) {
            result[i] = 0xff;
        }
        else {
            int v = ( front[i] * 0xff ) / ( 0xff - back[i] );
            result[i] = CLAMP_Y( v );
        }
    }
}

static void pencilsketch2_contrast(uint8_t *dst, const int len, const int contrast)
{
    int i;
    for( i = 0; i < len; i ++ ) {
        int m = dst[i];
        m -= 128;
        m *= contrast;
        m = (m + 50) / 100;
        m += 128;
        dst[i] = CLAMP_Y(m);
    }
}

static void pencilsketch2_gammacompr(uint8_t *dst, const int len)
{
    int i;
    for( i = 0; i < len; i ++ ) {
        dst[i] = (uint8_t) gamma_table[ dst[i] ];
    }
}

void pencilsketch2_apply( VJFrame *frame, int val, int gamma_compr, int strength, int contrast, int levels, int mode)
{
    double v = ( (double) gamma_compr - 4500.0) / 1000.0;
    if (v != gamma_value) {
        gamma_value = v;
        gammacompr_setup();
    }

    pencilsketch2_negate(frame->data[0], frame->len);

    rhblur_apply( pencilhblur, pencilbuf, frame->width, frame->height, val );
    rvblur_apply( pencilvblur, pencilhblur, frame->width, frame->height, val );

    pencilsketch2_dodge( frame->data[0], pencilhblur, frame->data[0], frame->len );

    if(mode) {
       veejay_memset( frame->data[1], 128, frame->uv_len );
       veejay_memset( frame->data[2], 128, frame->uv_len );
    }

    if(levels > 0 ) {
        pencilsketch2_posterize(frame->data[0], frame->len, levels);
    }

    if(strength > 0) {
        veejay_histogram_analyze( histogram_, frame, 0 );
        veejay_histogram_equalize( histogram_, frame, 0xff, strength );
    }

    if( contrast > 0) {
        pencilsketch2_contrast(frame->data[0], frame->len, contrast );
    }

    pencilsketch2_gammacompr( frame->data[0], frame->len );
}
