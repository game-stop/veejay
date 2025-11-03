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
#include <limits.h>
#include "gradientfield.h"

vj_effect *gradientfield_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 1;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 6;
    ve->defaults[2] = 0;

    ve->limits[0][3] = 10;
    ve->limits[1][3] = 200;
    ve->defaults[3] = 100;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 360;
    ve->defaults[4] = 0;

    ve->description = "Gradient Field Art Styles";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Value", "Style", "Magnitude", "Directional Bias" );
    return ve;
}

typedef struct
{
    int mag;
    float dir;  
} gvector_t;

#define LUT_SIZE 256
#define LUT_SIZE2 360
#define LUT_SIZE_TWICE (LUT_SIZE2 * 2)
#define LUT_SIZE_TWICE_OVER_PI (LUT_SIZE_TWICE / M_PI)
#define LUT_MASK (LUT_SIZE_TWICE - 1)

typedef struct 
{
    uint8_t *buf;
    gvector_t **vectorField;
    int w;
    int h;
    float atan_lut[LUT_SIZE];

    int *gradientX;
    int *gradientY;

    uint8_t *Y;
    uint8_t *U;
    uint8_t *V;

    float sin_lut[LUT_SIZE2];
    float cos_lut[LUT_SIZE2];

} gradientfield_t;

void *gradientfield_malloc(int w, int h) {
    gradientfield_t *s = (gradientfield_t*) vj_malloc(sizeof(gradientfield_t));
    if(!s) return NULL;
    s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 4 );
    if(!s->buf) {
        free(s);
        return NULL;
    }
    s->vectorField = (gvector_t**) vj_calloc( sizeof(gvector_t*) * w );
    for( int i = 0; i < w; i ++ ) {
        s->vectorField[i] = (gvector_t*) vj_calloc( sizeof( gvector_t) * h );
    }
    s->w = w;
    s->h = h;
    
    for( int i = 0; i < LUT_SIZE; i ++ ) {
        s->atan_lut[i] = atanf( (float) i );
    }

    s->gradientX = (int*) vj_calloc( sizeof(int) * w  * 2 );
    s->gradientY = s->gradientX + w;

    s->Y = s->buf + (w * h);
    s->U = s->Y + (w * h);
    s->V = s->U + (w * h);

    for( int i = 0; i < LUT_SIZE2; i ++ ) {
        float angle = i * (2 * M_PI) / LUT_SIZE2;
        s->sin_lut[i] = a_sin(angle);
        s->cos_lut[i] = a_cos(angle);
    }

    return (void*) s;
}

void gradientfield_free(void *ptr) {
    gradientfield_t *s = (gradientfield_t*) ptr;
    for( int i = 0; i < s->w; i ++ ) {
        free( s->vectorField[i] );
    }
    free(s->gradientX);
    free(s->vectorField);
    free(s->buf);
    free(s);
}

static void gradientfield_style0(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1 ) {
    const int lut_mask = LUT_SIZE2 - 1;
    const float divisor = 180.0f / M_PI;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;
    
    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            const int index = y * width + x;
            const int angleIndex = (int)((vectorField[x][y].dir * divisor) + 0.5) & lut_mask;

            const float dx = vectorField[x][y].mag * cos_lut[angleIndex] * param1;
            const float dy = vectorField[x][y].mag * sin_lut[angleIndex] * param1;

            int newX = (x + (int)dx);
            int newY = (y + (int)dy);

            newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
            newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

            Y[index] = inY[newY * width + newX];
            Cb[index] = inU[newY * width + newX];
            Cr[index] = inV[newY * width + newX];
        }
    }

}

static void gradientfield_style2(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1 ) {
 
    int blockSize = param1 + 1;
    const int lut_mask = LUT_SIZE2 - 1;
    const float divisor = 180.0f / M_PI;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;

    for (int y = blockSize; y < height - blockSize; y += blockSize) {
        for (int x = blockSize; x < width - blockSize; x += blockSize) {
            int avgX = 0;
            int avgY = 0;

            for (int i = 0; i < blockSize; ++i) {
                const int curX = x + i;
                for (int j = 0; j < blockSize; ++j) {
                    const int curY = y + j;
                    const int angleIndex = (int)((vectorField[curX][curY].dir * divisor) + 0.5) & lut_mask;
                    avgX += vectorField[curX][curY].mag * cos_lut[angleIndex];
                    avgY += vectorField[curX][curY].mag * sin_lut[angleIndex];
                }
            }

            avgX /= (blockSize * blockSize);
            avgY /= (blockSize * blockSize);

            for (int i = 0; i < blockSize; ++i) {
                const int curX = x + i;
                for (int j = 0; j < blockSize; ++j) {
                    const int curY = y + j;
                    int newX = curX + avgX;
                    int newY = curY + avgY;

                    newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
                    newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

                    Y[curY * width + curX] = inY[newY * width + newX];
                    Cb[curY * width + curX] = inU[newY * width + newX];
                    Cr[curY * width + curX] = inV[newY * width + newX];
                }
            }
        }
    }

}

static void gradientfield_style3(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1) {
    int blockSize = param1;
    const int lut_mask = LUT_SIZE2 - 1;
    const float divisor = 180.0f / M_PI;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;

    for (int y = blockSize; y < height - blockSize; y += blockSize) {
        for (int x = blockSize; x < width - blockSize; x += blockSize) {
            int avgX = 0;
            int avgY = 0;
            int totalMagnitude = 0;

            for (int i = 0; i < blockSize; ++i) {
                const int curX = x + i;
                for (int j = 0; j < blockSize; ++j) {
                    const int curY = y + j;
                    const int angleIndex = (int)((vectorField[curX][curY].dir * divisor) + 0.5) & lut_mask;
                    const int magnitude = vectorField[curX][curY].mag;
                    avgX += magnitude * cos_lut[angleIndex];
                    avgY += magnitude * sin_lut[angleIndex];
                    totalMagnitude += magnitude;
                }
            }

            if (totalMagnitude > 0) {
                avgX = (avgX * blockSize * blockSize) / totalMagnitude;
                avgY = (avgY * blockSize * blockSize) / totalMagnitude;
            }

            for (int i = 0; i < blockSize; ++i) {
                int curX = x + i;
                for (int j = 0; j < blockSize; ++j) {
                    const int curY = y + j;
                    int newX = curX + avgX;
                    int newY = curY + avgY;

                    newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
                    newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

                    Y[curY * width + curX] = inY[newY * width + newX];
                    Cb[curY * width + curX] = inU[newY * width + newX];
                    Cr[curY * width + curX] = inV[newY * width + newX];
                }
            }
        }
    }
}

void gradientfield_style4(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1) {
    const int brushSize = param1 + 1;
    const int op0 = 128;
    const int op1 = 255 - op0;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;

    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            const int index = y * width + x;

            const float direction = vectorField[x][y].dir;

            const int offsetX = (int)(brushSize * cos_lut[(int)(direction * LUT_SIZE_TWICE_OVER_PI) & LUT_MASK]);
            const int offsetY = (int)(brushSize * sin_lut[(int)(direction * LUT_SIZE_TWICE_OVER_PI) & LUT_MASK]);

            int newX = x + offsetX;
            int newY = y + offsetY;

            newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
            newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

            Y[index] = ((inY[newY * width + newX] * op0) + (inY[y * width + x] * op1)) >> 8;
            Cb[index] = ((inU[newY * width + newX] * op0) + (inU[y * width + x] * op1)) >> 8;
            Cr[index] = ((inV[newY * width + newX] * op0) + (inV[y * width + x] * op1)) >> 8;
        }
    }
}

void gradientfield_style5(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1) {
    const float divisor = 180.0f / M_PI;
    const int hei1 = height - 1;
    const int hei2 = height - 2;
    const int wid1 = width - 1;
    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            const int index = y * width + x;
            const float angle = vectorField[x][y].dir * divisor;
            const int angleIndex = (int)(angle * LUT_SIZE_TWICE_OVER_PI) & LUT_MASK;
            const float rippleOffset = sin_lut[angleIndex] * param1;

            int newY = (int)(y + rippleOffset);

            newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

            Y[index] = inY[newY * width + x];
            Cb[index] = inU[newY * width + x];
            Cr[index] = inV[newY * width + x];
        }
    }
}

void gradientfield_style6(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int param1) {
    const float divisor = 180.0f / M_PI;
    const float speed = (float) param1 * 0.1f;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;

    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            int index = y * width + x;
            float angle = vectorField[x][y].dir * divisor;
            int angleIndex = (int)(angle * LUT_SIZE_TWICE_OVER_PI) & LUT_MASK;
            float rippleOffset = sin_lut[angleIndex] * param1;

            int newX = (int)(x + rippleOffset + speed);
            int newY = (int)(y + rippleOffset);

            newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
            newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

            Y[index] = inY[newY * width + newX];
            Cb[index] = inU[newY * width + newX];
            Cr[index] = inV[newY * width + newX];
        }
    }
}

static void gradientfield_style1(int width, int height, gvector_t **vectorField, const float *cos_lut, const float *sin_lut, uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *inY, uint8_t *inU, uint8_t *inV, int metaballThreshold) {
    const int lut_mask = LUT_SIZE2 - 1;
    const float divisor = 180.0f / M_PI;
    const int wid1 = width - 1;
    const int hei1 = height - 1;
    const int wid2 = width - 2;
    const int hei2 = height - 2;
    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            int index = y * width + x;

            int angleIndex = (int)((vectorField[x][y].dir * divisor) + 0.5) & lut_mask;

            float dx = vectorField[x][y].dir * cos_lut[angleIndex];
            float dy = vectorField[x][y].dir * sin_lut[angleIndex];

            float distanceSquared = dx * dx + dy * dy;

            int op0 = (metaballThreshold * metaballThreshold * 255) / distanceSquared;
            op0 = (op0 < 0) ? 0 : ((op0 > 255) ? 255 : op0);
            int op1 = 0xff - op0;

            int newX = x + (int)(dx * distanceSquared);
            int newY = y + (int)(dy * distanceSquared);

            newX = (newX < 1) ? 1 : ((newX >= wid1) ? wid2 : newX);
            newY = (newY < 1) ? 1 : ((newY >= hei1) ? hei2 : newY);

            Y[index]  = (((op0 * inY[newY * width + newX] + op1 * Y[index]) + 127) >> 8);
            Cb[index] = (((op0 * inU[newY * width + newX] + op1 * Cb[index]) + 127) >> 8);
            Cr[index] = (((op0 * inV[newY * width + newX] + op1 * Cr[index]) + 127) >> 8);
        }
    }
}




void gradientfield_apply( void *ptr, VJFrame *frame, int *args ) {
    gradientfield_t *s = (gradientfield_t*) ptr;
    const int threshold = args[0];
    const int param1 = args[1];
    const int style = args[2];

    const float magnitudeScaling = args[3] / 100.0;
    const float directionalBias = ((float) args[4] - 180.0) * (M_PI / 180.0);

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict inY = s->Y;
    uint8_t *restrict inU = s->U;
    uint8_t *restrict inV = s->V;

    veejay_memcpy( inY, Y, len );
    veejay_memcpy( inU, Cb, len);
    veejay_memcpy( inV, Cr, len);

    int *restrict gradientX = s->gradientX;
    int *restrict gradientY = s->gradientY;

    const float *lut = s->atan_lut;
    const float *sin_lut = s->sin_lut;
    const float *cos_lut = s->cos_lut;

    gvector_t **vectorField = s->vectorField;

    const int wid1 = width - 1;
    const int hei1 = height - 1;

    for (int y = 1; y < hei1; ++y) {
        for (int x = 1; x < wid1; ++x) {
            const int index = y * width + x;

            const int gx = Y[index - width - 1] - Y[index - width + 1]
                         + 2 * (Y[index - 1] - Y[index + 1])
                         + Y[index + width - 1] - Y[index + width + 1];

            const int gy = Y[index - width - 1] + 2 * Y[index - width] + Y[index - width + 1]
                         - Y[index + width - 1] - 2 * Y[index + width] - Y[index + width + 1];

            const int gradientMagnitude = gx * gx + gy * gy;

            gradientX[x] = (gradientMagnitude > threshold ? gx : 0);
            gradientY[x] = (gradientMagnitude > threshold ? gy : 0);
        }

        for (int x = 1; x < wid1; ++x) {
            int gx = gradientX[x];
            int gy = gradientY[x];
            
            gx *= magnitudeScaling;
            gy *= magnitudeScaling;

            //const int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
            //const int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
			const int abs_gx = __builtin_abs(gx);
			const int abs_gy = __builtin_abs(gy);

            const int gradientMagnitudeSquared = gx * gx + gy * gy;
            //float direction = (abs_gx != 0) ? lut[(abs_gy << 8) / (abs_gx + 1)] + directionalBias: 0.0f;
			int idx = (abs_gy << 8) / (abs_gx + 1);
		    idx &= -(idx < LUT_SIZE);
			idx |= (idx >= LUT_SIZE) * (LUT_SIZE - 1);
			float direction = lut[idx] + directionalBias;

			direction = direction + (M_PI - 2 * direction) * (gx < 0);
			direction += M_PI * (direction < 0);

            vectorField[x][y].mag = gradientMagnitudeSquared;
            vectorField[x][y].dir = direction;
        }
    }


    switch(style) {
        case 0:
            gradientfield_style0(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 5:
            gradientfield_style2(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 6:
            gradientfield_style3(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 3:
            gradientfield_style4(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 4:
            gradientfield_style5(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 1:
            gradientfield_style6(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
        case 2:
            gradientfield_style1(width,height,vectorField,cos_lut,sin_lut,Y,Cb,Cr,inY,inU,inV,param1 );
            break;
    }

}
