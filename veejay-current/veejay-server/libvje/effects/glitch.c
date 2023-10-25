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
#include "glitch.h"

vj_effect *glitch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->defaults[0] = 20;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 10;
    ve->defaults[1] = 2;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 100;
    ve->defaults[2] = 2;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 200;
    ve->defaults[3] = 100;

    ve->limits[0][4] = 1;
    ve->limits[1][4] = 500;
    ve->defaults[4] = 20;

    ve->limits[0][5] = -100;
    ve->limits[1][5] = 100;
    ve->defaults[5] = 2;

    ve->limits[0][6] = -100;
    ve->limits[1][6] = 100;
    ve->defaults[6] = 2;

    ve->limits[0][7] = 0;
    ve->limits[1][7] = 500;
    ve->defaults[7] = 5;

    ve->description = "Glitch";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    
    ve->param_description = vje_build_param_list( ve->num_params, "Amplitude", "Noise Strength", "Noise Quantity", "Noise Scale", "Interval", "Distortion X", "Distortion Y",
                    "Duration"  );
    return ve;
}

typedef struct
{
    uint8_t *buf[3];
    int     *rand_lut;
    int     *lsfr_lut;
    int     count;
    int     frameCount;
} glitch_t;


void *glitch_malloc(int w, int h) {
    glitch_t *g = (glitch_t*) vj_malloc(sizeof(glitch_t));
    if(!g) return NULL;
    g->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
    if(!g->buf[0]) {
        free(g);
        return NULL;
    }
    g->buf[1] = g->buf[0] + (w*h);
    g->buf[2] = g->buf[1] + (w*h);

    g->rand_lut = (int*) vj_malloc(sizeof(int) * w * h * 2 );
    g->lsfr_lut     = g->rand_lut + (w*h);
    g->count        = 0;

    if(!g->rand_lut) {
        free(g->buf[0]);
        free(g);
        return NULL;
    }

    for( int i = 0; i < (w*h); i ++ ) {
        g->rand_lut[i] = rand();
    }

    veejay_memcpy( g->lsfr_lut, g->rand_lut, sizeof(int) * (w*h));

    return (void*) g;
}

void glitch_free( void *ptr ) {
    glitch_t *g = (glitch_t*) ptr;
    free(g->buf[0]);
    free(g->rand_lut);
    free(g);
}

  
void glitch_apply( void *ptr, VJFrame *frame, int *args ) {
    glitch_t *g = (glitch_t*) ptr;
    const int len = frame->len;

    int i;
    uint8_t *Y = frame->data[0];    
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    uint8_t *bY = g->buf[0];
    uint8_t *bU = g->buf[1];
    uint8_t *bV = g->buf[2];

    const int masterAmplitude = args[0];
    const int noiseStrength = args[1];
    const int noiseQuantity = args[2];
    const int noiseScale = args[3];
    const int interval = args[4];
    const int geometryDistortionX = args[5];
    const int geometryDistortionY = args[6];
    const int randinterval = args[7];
    const int width = frame->width;
    const int height = frame->height;
    
    int noise;

    int *rand_lut = g->rand_lut;
    int *lsfr_lut = g->lsfr_lut;

    float nS = 0.01 * noiseScale;
    
    if( g->count == 0 && randinterval > 0 ) {
        veejay_memcpy( lsfr_lut, rand_lut, sizeof(int) * len );
    }

    if( randinterval > 0 ) {
        g->count = (g->count + 1) % randinterval;
    }

    g->frameCount = (g->frameCount + 1) % interval;

    if( g->frameCount > randinterval ) {
        return;
    }

    for ( i = 0; i < len; i ++ ) {
        noise = (lsfr_lut[i] % noiseQuantity - noiseQuantity / 2) * noiseStrength * nS;

        int nY = Y[i] + masterAmplitude * noise;
        int nU = 128 + ( U[i] - 128 + masterAmplitude * noise );
        int nV = 128 + ( V[i] - 128 + masterAmplitude * noise );

        Y[i] = CLAMP_Y( nY );
        U[i] = CLAMP_UV( nU );
        V[i] = CLAMP_UV( nV );

    }

    if(randinterval > 0 ) {
        for( i = 0; i < len; i ++ ) {
            lsfr_lut[i] = fastrand( lsfr_lut[i] );
        }
    }

    float normalizedDistortionX = (float)geometryDistortionX / 100.0;
    float normalizedDistortionY = (float)geometryDistortionY / 100.0;

    int distortionX = normalizedDistortionX * width;
    int distortionY = normalizedDistortionY * height;
    
    veejay_memcpy( bU, U, len );
    veejay_memcpy( bV, V, len );
    veejay_memcpy( bY, Y, len );

    for( int y = 0; y < height; y ++ ) {
        int ny = (y + distortionY) % height;
        for( int x = 0; x < width; x ++ ) {
            int nx = (x + distortionX) % width;
            U[ y * width + x ] = bU[ ny * width + nx ];
            V[ y * width + x ] = bV[ ny * width + nx ];
            Y[ y * width + x ] = (Y[y * width + x ] + bY[ ny * width + nx ]) >> 1;
        }

    }

}

