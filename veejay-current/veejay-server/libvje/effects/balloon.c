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
#include <time.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "balloon.h"

#define CLAMP(x, min, max) ((x < min) ? min : ((x > max) ? max : x))

vj_effect *balloonfit_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w < h ? w/4 : h/4);

    ve->limits[0][1] = 3;
    ve->limits[1][1] = ( w < h ? w/4 : h/4);

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 0xff;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 100;

    ve->defaults[0] = 18;
    ve->defaults[1] = 33;
    ve->defaults[2] = 196;
    ve->defaults[3] = 25;

    ve->description = "Balloon Accumulator";
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
} balloonfit_t;

static int balloonfit_prepare(balloonfit_t *balloonfit, int min_size, int max_size);

void *balloonfit_malloc(int w, int h)
{
    balloonfit_t *s = (balloonfit_t *)vj_calloc(sizeof(balloonfit_t));

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

    srand((unsigned int) time(NULL) );

    return (void *)s;
}

void balloonfit_free(void *ptr)
{
    balloonfit_t *s = (balloonfit_t *)ptr;
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

static int balloonfit_prepare(balloonfit_t *balloonfit, int min_size, int max_size)
{
    if (balloonfit->minSize == min_size && balloonfit->maxSize == max_size &&
        balloonfit->boxSizes != NULL && balloonfit->boxIndices != NULL)
    {
        return 1;
    }

    if (balloonfit->minSize != min_size || balloonfit->maxSize != max_size)
    {
        free(balloonfit->boxSizes);
        free(balloonfit->boxIndices);
        balloonfit->boxSizes = NULL;
        balloonfit->boxIndices = NULL;
    }

    if (max_size <= min_size || min_size < 2 || max_size < 3)
    {
        return 0;
    }

    balloonfit->numBoxes = rand() % (max_size - min_size + 1) + min_size;

    balloonfit->boxSizes = (int *)vj_malloc(sizeof(int) * balloonfit->numBoxes);
    balloonfit->boxIndices = (int *)vj_malloc(sizeof(int) * balloonfit->numBoxes);

    if (balloonfit->boxSizes == NULL || balloonfit->boxIndices == NULL)
    {
        return 0;
    }

    for (int k = 0; k < balloonfit->numBoxes; ++k)
    {
        balloonfit->boxSizes[k] = rand() % (max_size - min_size + 1) + min_size;
        balloonfit->boxIndices[k] = k;
    }

    shuffle_array(balloonfit->boxIndices, balloonfit->numBoxes);

    balloonfit->minSize = min_size;
    balloonfit->maxSize = max_size;

    return 1;
}

static double calculateLocalContrast(uint8_t *srcY, int width, int height, int i, int j, int box_size, double *runavg, double weight)
{
    int sum = 0;
    for (int ii = 0; ii < box_size; ++ii)
    {
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

void balloonfit_apply(void *ptr, VJFrame *frame, int *args)
{
    balloonfit_t *s = (balloonfit_t *)ptr;
    const uint8_t min_size = args[0];
    const uint8_t max_size = args[1];
    const uint8_t contrast = args[2];
    const double weight = (double)args[3] * 0.01;

    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    if (balloonfit_prepare(s, min_size, max_size) == 0)
        return;

    int *restrict boxSizes = s->boxSizes;
    int *restrict boxIndices = s->boxIndices;
    int numBoxes = s->numBoxes;
    int boxIndex = 0;

    for (int i = 0; i < height;)
    {
        for (int j = 0; j < width;)
        {
            int box_index = boxIndices[boxIndex];
            int box_size = boxSizes[box_index];

            double localContrast = calculateLocalContrast(srcY, width, height, i, j, box_size, &(s->runningAvg), weight);
            double sizeMultiplier = 1.0 - (localContrast / contrast);

            box_size = CLAMP((int)(box_size * sizeMultiplier), min_size, max_size);

			int box_size_squared = box_size * box_size;

            int avgY = 0, avgU = 128, avgV = 128;

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
                    avgU += (srcU[src_idx] - 128);
                    avgV += (srcV[src_idx] - 128);
                }
            }

            avgY /= box_size_squared;
            avgU /= box_size_squared;
            avgV /= box_size_squared;

            int cx = j + (box_size >> 1);
            int cy = i + (box_size >> 1);
            int radius = (box_size >> 1);

              veejay_draw_balloon(bufY, cx, cy, box_size, box_size, width, height, radius, avgY);
            veejay_draw_balloonUV(bufU, cx, cy, box_size, box_size, width, height, radius, 128 + avgU);
            veejay_draw_balloonUV(bufV, cx, cy, box_size, box_size, width, height, radius, 128 + avgV);

            boxIndex = (boxIndex + 1) % numBoxes;
            j += box_size;
        }

        i += boxSizes[boxIndices[boxIndex]];
    }

    veejay_memcpy(srcY, bufY, frame->len);
    veejay_memcpy(srcU, bufU, frame->len);
    veejay_memcpy(srcV, bufV, frame->len);
}

