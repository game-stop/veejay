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
#define GAMMA_MAX 256
static void gammacompr_setup();

typedef struct {
    uint8_t *pencilbuf;
    uint8_t *pencilhblur;
    uint8_t *pencilvblur;
    void *histogram_;
    double *gamma_table;
    double gamma_value;
} pencilsketch_t;

void *pencilsketch2_malloc(int w, int h) {
    
    pencilsketch_t *p =(pencilsketch_t*) vj_calloc(sizeof(pencilsketch_t));
    if(!p) {
        return NULL;
    }
    
    p->pencilbuf = (uint8_t*) vj_malloc( sizeof(uint8_t) * (w*h*3) );
    if(!p->pencilbuf) {
        free(p);
        return NULL;
    }

    p->pencilhblur = p->pencilbuf + (w*h);
    p->pencilvblur = p->pencilhblur + (w*h);
    
    p->histogram_ = veejay_histogram_new();
    if(!p->histogram_) {
        free(p->pencilbuf);
        free(p);
        return NULL;
    }

    p->gamma_table = (double*) vj_calloc(sizeof(double) * GAMMA_MAX );
    if(!p->gamma_table) {
        free(p->pencilbuf);
        veejay_histogram_del(p->histogram_);
        free(p);
        return NULL;
    }

    p->gamma_value = 9000;
    
    gammacompr_setup(p);
    
    return (void*) p;
}

void pencilsketch2_free(void *ptr) {

    pencilsketch_t *p = (pencilsketch_t*) ptr;

    free(p->pencilbuf);
    veejay_histogram_del(p->histogram_);
    free(p->gamma_table);
    free(p);
}

static void gammacompr_setup(pencilsketch_t *p)
{
    int i;
    double val;
    double gm = (double) GAMMA_MAX;
    for (i = 0; i < GAMMA_MAX; i++) {
         val = i / gm;
         val = pow(val, p->gamma_value + ((double) i * 0.01));
         val = gm * val;
         p->gamma_table[i] = (val < 0.0 ? 0.0 : val > 255.0 ? 255.0 : val);
    }
}


static void rhblur_apply( uint8_t *dst , uint8_t *src, int w, int h, int r)
{
    int y;
    for(y = 0; y < h ; y ++ )
    {
        veejay_blur( dst + y * w, src + y *w , w, r,1, 1);
    }       
}

static void rvblur_apply( uint8_t *dst, uint8_t *src, int w, int h, int r)
{
    int x;
    for(x=0; x < w; x++)
    {
        veejay_blur( dst + x, src + x , h, r, w, w );
    }
}

static void pencilsketch2_negate(uint8_t *src, uint8_t *dst, const int len)
{
    int i;
#pragma omp simd
    for( i = 0; i < len; i ++ ) {
        dst[i] = 0xff - src[i];
    }
}

static void pencilsketch2_posterize(uint8_t *dst, const int len, const int levels)
{
    const int factor = (256 / levels);
    int i;
#pragma omp simd
    for( i = 0; i < len; i ++ ) {
        dst[i] = dst[i] - (dst[i] % factor);
    }       
}

static void pencilsketch2_dodge(uint8_t *result, uint8_t *front, uint8_t *back, const int len)
{
    int i;
#pragma omp simd
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
#pragma omp simd
    for( i = 0; i < len; i ++ ) {
        int m = dst[i];
        m -= 128;
        m *= contrast;
        m = (m + 50) / 100;
        m += 128;
        dst[i] = CLAMP_Y(m);
    }
}

static void pencilsketch2_gammacompr(double *gamma_table, uint8_t *dst, const int len)
{
    int i;
    for( i = 0; i < len; i ++ ) {
        dst[i] = (uint8_t) gamma_table[ dst[i] ];
    }
}

void pencilsketch2_apply( void *ptr, VJFrame *frame, int *args ) {
    
    int val = args[0];
    int gamma_compr = args[1];
    int strength = args[2];
    int contrast = args[3];
    int levels = args[4];
    int mode = args[5];

    pencilsketch_t *p = (pencilsketch_t*) ptr;

    double v = ( (double) gamma_compr - 4500.0) / 1000.0;
    if (v != p->gamma_value) {
        p->gamma_value = v;
        gammacompr_setup(p);
    }

    pencilsketch2_negate(frame->data[0],p->pencilbuf, frame->len);

    rhblur_apply( p->pencilhblur, p->pencilbuf, frame->width, frame->height, val );
    rvblur_apply( p->pencilvblur, p->pencilhblur, frame->width, frame->height, val );

    pencilsketch2_dodge( frame->data[0], p->pencilhblur, frame->data[0], frame->len );

    if(mode) {
       veejay_memset( frame->data[1], 128, frame->uv_len );
       veejay_memset( frame->data[2], 128, frame->uv_len );
    }

    if(levels > 0 ) {
        pencilsketch2_posterize(frame->data[0], frame->len, levels);
    }

    if(strength > 0) {
        veejay_histogram_analyze( p->histogram_, frame, 0 );
        veejay_histogram_equalize( p->histogram_, frame, 0xff, strength );
    }

    if( contrast > 0) {
        pencilsketch2_contrast(frame->data[0], frame->len, contrast );
    }

    pencilsketch2_gammacompr(p->gamma_table, frame->data[0], frame->len );
}
