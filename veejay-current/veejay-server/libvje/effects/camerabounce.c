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
#include "camerabounce.h"

vj_effect *camerabounce_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 500;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 500;
    
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1000;

    ve->limits[0][3] = 100;
    ve->limits[1][3] = 3000;

    ve->defaults[0] = 25;
    ve->defaults[1] = 5;
    ve->defaults[2] = 3;
    ve->defaults[3] = 200;

    ve->description = "Camera Bounce";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Zoom Interval", "Zoom duration" , "Shutter Angle", "Zoom Factor" );
    return ve;
}

typedef struct {
    uint8_t *buf[3];
    uint8_t *blurred[3];
    int frameNumber;
} camera_t;

void *camerabounce_malloc(int w, int h)
{
    camera_t *c = (camera_t*) vj_malloc(sizeof(camera_t));
    if(!c) {
        return NULL;
    }
    c->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 * 2 );
    if(!c->buf[0]) {
        free(c);
        return NULL;
    }

    c->buf[1] = c->buf[0] + (w*h);
    c->buf[2] = c->buf[1] + (w*h);

    c->blurred[0] = c->buf[2] + (w*h);
    c->blurred[1] = c->blurred[0] + (w*h);
    c->blurred[2] = c->blurred[1] + (w*h);

    c->frameNumber = 0;

    return (void*) c;
}

void camerabounce_free(void *ptr) {
    camera_t *c = (camera_t*) ptr;
    free(c->buf[0]);
    free(c);
}

void camerabounce_apply(void *ptr, VJFrame* frame, int *args) {
    camera_t *c = (camera_t*) ptr;
    const int zoomInterval = args[0];
    const int zoomDuration = args[1];
    const int currentFrame = c->frameNumber % zoomInterval;
    const double zoom = ( args[3] / 10.0f ) * M_PI / 180.0f;
    
    double zoomFactor = 1.0 / ( 1.0 - 2.0 * tan( zoom / 2.0 ));
    double interpolationFactor;

    c->frameNumber ++;

    if( currentFrame <= zoomDuration ) {
        if( currentFrame <= zoomDuration/2) {
            interpolationFactor = (double) currentFrame / (zoomDuration/2);
        }
        else {
            interpolationFactor = (double)( zoomDuration - currentFrame)  / (zoomDuration/2);
        }
    }
    else {
        // Nothing to do!
        return;
    }

    zoomFactor = 1.0 + interpolationFactor * ( zoomFactor - 1.0 );  

    const double blurAmount = (args[2] / 100.0f);

    const int width = frame->width;
    const int height = frame->height;

    const int newWidth = width * zoomFactor;
    const int newHeight = height * zoomFactor;

    const int offsetX = (width - newWidth) >> 1;
    const int offsetY = (height - newHeight) >> 1;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bY = c->blurred[0];
    uint8_t *restrict bU = c->blurred[1];
    uint8_t *restrict bV = c->blurred[2];


    int newX, newY;

    // Zoom
    for (int y = 0; y < height; ++y) {
        newY = (int)(y - offsetY) / zoomFactor;
        newY = (newY < 0) ? 0 : ((newY > height - 1) ? height - 1 : newY);

        for (int x = 0; x < width; ++x) {
                newX = (int)(x - offsetX) / zoomFactor;
                newX = (newX < 0) ? 0 : ((newX > width - 1) ? width - 1 : newX);

                int srcIndex = newY * width + newX;
                int dstIndex = y * width + x;
                
                bY[dstIndex] = srcY[srcIndex];
                bU[dstIndex] = srcU[srcIndex];
                bV[dstIndex] = srcV[srcIndex];
            }
    }

    const float maxDistanceSquared = pow( width / 2 , 2 ) + pow( height / 2, 2 );


    // Blur, more towards the edges and less towards the centre
    for (int y = 0; y < height; ++y) {
		int distanceY = ( height >> 1 ) - y;
        for (int x = 0; x < width; ++x) {

            int distanceX = ( width >> 1 ) - x;

            int distanceSquared = distanceX * distanceX + distanceY * distanceY;

            if( distanceSquared <= maxDistanceSquared ) {
                
                float normalizedDistance = (float) distanceSquared / maxDistanceSquared;
                normalizedDistance = normalizedDistance * normalizedDistance;
                normalizedDistance *= 100.0f;

                float blurStrength = blurAmount * normalizedDistance;
                blurStrength = ( blurStrength > 6.0f ? 6.0f: blurStrength );

                const int minX = MAX(0, x - blurStrength);
                const int minY = MAX(0, y - blurStrength);
                const int maxX = MIN(width - 1, x + blurStrength);
                const int maxY = MIN(height - 1, y + blurStrength);

                int tmpY = 0; int tmpU = 0; int tmpV = 0; uint16_t totalPixels = 0;

#pragma omp simd
                for (int blurY = minY; blurY <= maxY; ++blurY) {
					int bpos = blurY * width;
  					for (int blurX = minX; blurX <= maxX; ++blurX) {
                        int blurIndex = bpos + blurX;
                        tmpY += bY[blurIndex];
                        tmpU += (bU[blurIndex] - 128);
                        tmpV += (bV[blurIndex] - 128);
                        totalPixels++;
                    }
                }

                const int dstIndex = y * width + x;
                srcY[dstIndex] = tmpY / totalPixels;
                srcU[dstIndex] = 128 + (tmpU / totalPixels);
                srcV[dstIndex] = 128 + (tmpV / totalPixels);
            }
        }
    } 

}

