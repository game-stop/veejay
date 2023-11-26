/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "strobo.h"

vj_effect *strobo_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 70;
    ve->defaults[1] = 10;
    ve->defaults[2] = 150;
    ve->defaults[3] = 3;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 1500;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 1;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1500;


    ve->description = "Strobotsu";
    
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user =0;
    ve->parallel = 0;
    
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Duration", "Opacity", "Echoes", "Mode", "Delay" );

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    int timestamp;
} strobo_t;

static struct {
    int r;
    int g;
    int b;
} rainbow_t[] = {
    {255, 0, 0},     // Red
    {255, 127, 0},   // Orange
    {255, 255, 0},   // Yellow
    {0, 255, 0},     // Green
    {0, 0, 255},     // Blue
    {75, 0, 130},    // Indigo
    {148, 0, 211},   // Violet
    {128, 0, 0},     // Maroon
    {255, 69, 0},    // Red-Orange
    {255, 140, 0},   // Dark Orange
    {255, 255, 255}, // White
    {0, 128, 0},     // Dark Green
    {0, 0, 128},     // Navy
    {139, 69, 19},   // Saddle Brown
};

void *strobo_malloc(int w, int h) 
{
    strobo_t *s = (strobo_t*) vj_calloc(sizeof(strobo_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + (w * h);
    s->buf[2] = s->buf[1] + (w * h);

    veejay_memset( s->buf[0], 0, (w*h));
    veejay_memset( s->buf[1], 128, (w*h));
    veejay_memset( s->buf[2], 128, (w*h));

    return s;
}

void strobo_free(void *ptr) {
    strobo_t *s = (strobo_t*) ptr;
    free(s->buf[0]);
    free(s);
}

static uint32_t strobo( uint32_t *H, const int N )
{
    uint32_t threshold = 0;
    double wF, wB=0.0, mB, mF, between, max = 0.0;
    double sum = 0.0, sumB=0.0;
    uint32_t i;

    for( i = 0; i < 256; i++ ) 
    {
        wB += H[i];
        if( wB == 0 )
            continue;
        wF = N - wB;
        if( wF == 0 )
            break;
        sumB += ( i * H[i] );
        mB = sumB / wB;
        mF = (sum - sumB) / wF;
        between = wB * wF * (mB - mF) * (mB - mF);
        if( between > max ) {
            max = between;
            threshold = i;
        }
        sum += i * H[i];
    }
    return threshold;
}

void strobo_apply(void *ptr, VJFrame *frame, int *args) {
    strobo_t *s = (strobo_t*) ptr;
    const int skew = args[0];
    const int duration = args[1];
    const int op0 = args[2];
    const int echoes = args[3];
    const int mode = args[4];
    const int op1 = 0xff - op0;
    const int delay = args[5];

    uint32_t Histogram[256];
    unsigned int i;
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict bY = s->buf[0];
    uint8_t *restrict bU = s->buf[1];
    uint8_t *restrict bV = s->buf[2];

    veejay_memset( Histogram, 0, sizeof( Histogram ) );

    uint8_t Lookup[256];
    __init_lookup_table( Lookup, 256, 0.0f, 255.0f, 0.0f, (float)skew ); 
    for( i = 0; i < len; i ++ )
    {
        Histogram[ Lookup[ Y[i] ] ] += 1;
    }   
    
    // calculate a threshold using otsu's method
    const uint32_t threshold = strobo( Histogram, len );

    int color_count = 14;
    if( duration < color_count )
        color_count = duration;

    const int color_index = (s->timestamp / (duration/color_count)) % color_count;
    const int decay = ((s->timestamp % echoes) * 255) / echoes;

    int a,b,c,cy=0, cu=128,cv=128;

    // pick a color from the table
    _rgb2yuv( rainbow_t[ color_index ].r,
              rainbow_t[ color_index ].g,
              rainbow_t[ color_index ].b,
              cy, cu, cv );   

    if( delay == 0 || (s->timestamp % delay) == 0 ) {
#pragma omp simd
        for( i = 0; i < len; i ++ )
        {
            // create a mask
            int mask = (Y[i] < threshold);

            // decay the current buffer if mask value is zero
            // or blend-in the selected color using a + (( 2 * b ) - opacity )
            a = bY[i] * decay >> 8;
            bY[i] = CLAMP_Y((1 - mask) * ((bY[i] * decay) >> 8) + mask * (a + ((2 * cy) - op0)));

            a = ((bU[i] - 128) * decay) >> 8;
            bU[i] = CLAMP_UV((1 - mask) * (bU[i] - (((bU[i] - 128) * decay) >> 8)) + mask * (a + 2 * (cu - 128) + 128));

            a = ((bV[i] - 128) * decay) >> 8;;
            bV[i] = CLAMP_UV((1 - mask) * (bV[i] - (((bV[i] - 128) * decay) >> 8)) + mask * (a + 2 * (cv - 128) + 128));
        }
    }

    if( mode == 0 ) {
#pragma omp simd
        // copy back the strobo buffer for display
        for( i = 0; i < len; i ++ ) 
        {
            Y[i] = bY[i];
            U[i] = bU[i];
            V[i] = bV[i];
        }
    }
    else {

#pragma omp simd
        for( i = 0; i < len; i ++ ) {
            // create a mask from the strobo buffer
            uint8_t mask = (bY[i] != 0);
            
            // dont blend-in black
            a = (mask * bY[i]) | (!mask * Y[i]);
            b = (mask * bU[i]) | (!mask * U[i]);
            c = (mask * bV[i]) | (!mask * V[i]);

            // opacity blend the strobo buffer with the original frame
            Y[i] = (mask * ((op0 * Y[i] + op1 * a) >> 8)) | (!mask * Y[i]);
            U[i] = (mask * ((op0 * U[i] + op1 * b) >> 8)) | (!mask * U[i]);
            V[i] = (mask * ((op0 * V[i] + op1 * c) >> 8)) | (!mask * V[i]);
        }
    }


    s->timestamp ++;

}

