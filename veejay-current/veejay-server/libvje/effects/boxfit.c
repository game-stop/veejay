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
#include <config.h>
#include <time.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "boxfit.h"

#define CLAMP(x, min, max) ((x < min) ? min : ((x > max) ? max : x))

vj_effect *boxfit_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    
    ve->limits[0][0] = 1;
    ve->limits[1][0] = ( w < h ? w/4 : h/4);

    ve->limits[0][1] = 2;
    ve->limits[1][1] = ( w < h ? w/4 : h/4);

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 0xff;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 100;

    ve->defaults[0] = 6;
    ve->defaults[1] = 33;
    ve->defaults[2] = 196;
    ve->defaults[3] = 25;

    /*
     * box fitting while keeping some reflection/representation of the original frame
     *
     * inspired by the gmic's boxfitting effect
     *
     * https://gmic.eu/gallery/img/artistic_zelda_full_24.jpg
     */

    ve->description = "Box Accumulator";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Min Size", "Max Size" , "Contrast", "Smooth" );
    return ve;
}

typedef struct
{
    uint8_t *buf[3];
    int *boxSizes;
    int *boxIndices;
    int numBoxes;
    int minSize;
    int maxSize;
    double runningAvg;
} boxfit_t;

static int boxfit_prepare(boxfit_t *boxfit, int min_size, int max_size);

void *boxfit_malloc(int w, int h)
{
    boxfit_t *s = (boxfit_t *)vj_calloc(sizeof(boxfit_t));

    if (!s)
        return NULL;

    s->buf[0] = (uint8_t *)vj_malloc(sizeof(uint8_t) * w * h * 3);

    if (!s->buf[0])
    {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + (w * h);
    s->buf[2] = s->buf[1] + (w * h);

    veejay_memset(s->buf[0], 0, (w*h));
    veejay_memset(s->buf[1], 128, (w*h));
    veejay_memset(s->buf[2], 128, (w*h));

    srand((unsigned int) time(NULL));

    return (void *)s;
}

void boxfit_free(void *ptr)
{
    boxfit_t *s = (boxfit_t *)ptr;
    free(s->buf[0]);
    free(s);
}

// fisher yates shuffling
static void shuffle_array(int *array, int size)
{
    for (int i = size - 1; i > 0; --i)
    {
        int j = rand() % (i + 1);
        int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

// adjust buffers when size change and calculate random sized boxes
static int boxfit_prepare(boxfit_t *boxfit, int min_size, int max_size)
{
    if (boxfit->minSize == min_size && boxfit->maxSize == max_size &&
        boxfit->boxSizes != NULL && boxfit->boxIndices != NULL)
    {
        return 1;
    }

    if (boxfit->minSize != min_size || boxfit->maxSize != max_size)
    {
        free(boxfit->boxSizes);
        free(boxfit->boxIndices);
        boxfit->boxSizes = NULL;
        boxfit->boxIndices = NULL;
    }

    if (max_size <= min_size)
    {
        return 0;
    }

    boxfit->numBoxes = rand() % (max_size - min_size + 1) + min_size;

    boxfit->boxSizes = (int *)vj_malloc(sizeof(int) * boxfit->numBoxes);
    boxfit->boxIndices = (int *)vj_malloc(sizeof(int) * boxfit->numBoxes);

    if (boxfit->boxSizes == NULL || boxfit->boxIndices == NULL)
    {
        return 0;
    }

    for (int k = 0; k < boxfit->numBoxes; ++k)
    {
        boxfit->boxSizes[k] = rand() % (max_size - min_size + 1) + min_size;
        boxfit->boxIndices[k] = k;
    }

    shuffle_array(boxfit->boxIndices, boxfit->numBoxes);

    boxfit->minSize = min_size;
    boxfit->maxSize = max_size;

    return 1;
}

// determine the local contrast within the specified box and keep a running average (to prevent jumpy behaviour)
static double calculateLocalContrast(uint8_t *srcY, int width, int height, int i, int j, int box_size, double *runavg, double weight)
{
    int sum = 0;
    for (int ii = 0; ii < box_size; ++ii)
    {
#pragma omp simd
        for (int jj = 0; jj < box_size; ++jj)
        {
            int row = i + ii;
            int col = j + jj;

            row = (row < height) ? row : (row - height);
            col = (col < width) ? col : (col - width);

            int src_idx = row * width + col;
            sum += srcY[src_idx];
        }
    }

    int avg = sum / (box_size * box_size);
    int contrast = 0;
    double maxContrast = 255.0 * box_size;
    
    for (int ii = 0; ii < box_size; ++ii)
    {
#pragma omp simd
        for (int jj = 0; jj < box_size; ++jj)
        {
            int row = i + ii;
            int col = j + jj;

            row = (row < height) ? row : (row - height);
            col = (col < width) ? col : (col - width);

            int src_idx = row * width + col;
            int pixelValue = srcY[src_idx];

            int diff = pixelValue - avg;
            contrast += (diff ^ (diff >> 31)) - (diff >> 31);
        }
    }

    double nc = (double) contrast / maxContrast;
    *runavg = (*runavg * ( 1.0 - weight )) + (nc * weight);

    return *runavg;
}


void boxfit_apply(void *ptr, VJFrame *frame, int *args)
{
    boxfit_t *s = (boxfit_t *)ptr;
    const uint8_t min_size = args[0];
    const uint8_t max_size = args[1];
    const uint8_t contrast = args[2];
    const double weight = (double) args[3] * 0.01;

    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    if (boxfit_prepare(s, min_size, max_size) == 0)
        return;

    int *restrict boxSizes = s->boxSizes;
    int *restrict boxIndices = s->boxIndices;
    int numBoxes = s->numBoxes;
    int boxIndex = 0;
    int ii=0,jj=0;

    for (int i = 0; i < height;)
    {
        for (int j = 0; j < width;)
        {
            int box_index = boxIndices[ boxIndex ];
            int box_size = boxSizes[box_index];

            double localContrast = calculateLocalContrast(srcY, width, height, i, j, box_size, &(s->runningAvg), weight);
            double sizeMultiplier = 1.0 - (localContrast / contrast); 

            box_size = CLAMP((int)(box_size * sizeMultiplier), min_size, max_size);
    
            int avgY = 0, avgU = 128, avgV = 128;

            const int box_size_squared = box_size * box_size;

            // determine the average color of the "selected" box
            for (int ii = 0; ii < box_size; ++ii)
            {
                for (int jj = 0; jj < box_size; ++jj)
                {
                    int row = i + ii;
                    int col = j + jj;

                    row = (row < height) ? row : (row - height);
                    col = (col < width) ? col : (col - width);

                    int src_idx = row * width + col;

                    avgY += srcY[src_idx];
                    avgU += (srcU[src_idx]-128);
                    avgV += (srcV[src_idx]-128);
                }
            }

            avgY /= box_size_squared;
            avgU /= box_size_squared;
            avgV /= box_size_squared; 

            // fill up and draw a border around it
            for (ii = 0; ii < box_size; ++ii)
            {
                for (jj = 0; jj < box_size; ++jj)
                {
                    int row = i + ii;
                    int col = j + jj;

                    row = (row < height) ? row : (row - height);
                    col = (col < width) ? col : (col - width);

                    int src_idx = row * width + col;

                    if (ii == 0 || ii == box_size - 1 || jj == 0 || jj == box_size - 1)
                    {
                        bufY[src_idx] = pixel_Y_lo_;
                        bufU[src_idx] = 128;
                        bufV[src_idx] = 128;
                    }
                    else
                    { 
                        bufY[src_idx] = avgY;
                        bufU[src_idx] = CLAMP_UV(128 + avgU);
                        bufV[src_idx] = CLAMP_UV(128 + avgV);
                    } 
                }
            }

            j += box_size;
            boxIndex = (boxIndex + 1) % numBoxes;
        }

        i += boxSizes[ boxIndices[boxIndex] ];
    }

    // copy back, inplace 
    veejay_memcpy( srcY, bufY, frame->len);
    veejay_memcpy( srcU, bufU, frame->len);
    veejay_memcpy( srcV, bufV, frame->len);
}

