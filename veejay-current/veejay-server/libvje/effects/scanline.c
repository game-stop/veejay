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
#include "scanline.h"

#define DEFAULT_STOP_DURATION 25

vj_effect *scanline_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 500;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 500;
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = DEFAULT_STOP_DURATION;

    ve->description = "Scanline";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Direction", "Speed", "Stop Duration" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    int prevRow;
    int prevCol;
    int stopCount;
} scanline_t;

void *scanline_malloc(int w, int h) {
    scanline_t *s = (scanline_t*) vj_calloc(sizeof(scanline_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    veejay_memset( s->buf[0], pixel_Y_lo_, w * h );
    veejay_memset( s->buf[1], 128, w * h );
    veejay_memset( s->buf[2], 128, w * h );


    s->stopCount = DEFAULT_STOP_DURATION;

    return (void*) s;
}

void scanline_free(void *ptr) {
    scanline_t *s = (scanline_t*) ptr;
    free(s->buf[0]);
    free(s);
}
void scanline_apply(void *ptr, VJFrame *frame, int *args)
{
    scanline_t *s = (scanline_t*) ptr;

    const int mode     = args[0];
    const int speed    = args[1];
    const int duration = args[2];

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = width * height;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    // pause phase
    if (s->stopCount > 0)
    {
        s->stopCount--;

        if (s->stopCount == 0)
        {
            s->prevRow = 0;
            s->prevCol = 0;

            veejay_memset(bufY, pixel_Y_lo_, len);
            veejay_memset(bufU, 128, len);
            veejay_memset(bufV, 128, len);
        }

        veejay_memcpy(dstY, bufY, len);
        veejay_memcpy(dstU, bufU, len);
        veejay_memcpy(dstV, bufV, len);
        return;
    }

    switch (mode)
    {
        // top to bottom
        case 0:
        {
            int start = s->prevRow;
            int stop  = start + speed;
            if (stop > height) stop = height;

            for (int row = start; row < stop; ++row)
            {
                int offset = row * width;

                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            if (stop == height)
                s->stopCount = duration;

            s->prevRow = (start + speed) % height;
            break;
        }

        // bottom to top
        case 1:
        {
            int start = height - 1 - s->prevRow;
            int stop  = height - s->prevRow - speed;
            if (stop < 0) stop = 0;

            for (int row = start; row >= stop; --row)
            {
                int offset = row * width;

                veejay_memcpy(bufY + offset, dstY + offset, width);
                veejay_memcpy(bufU + offset, dstU + offset, width);
                veejay_memcpy(bufV + offset, dstV + offset, width);
            }

            if (stop == 0)
                s->stopCount = duration;

            s->prevRow = (s->prevRow + speed) % height;
            break;
        }

        // left to right
        case 2:
        {
            int start = s->prevCol;
            int stop  = start + speed;
            if (stop > width) stop = width;

            for (int row = 0; row < height; ++row)
            {
                int base = row * width;

                for (int col = start; col < stop; ++col)
                {
                    int idx = base + col;

                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            if (stop == width)
                s->stopCount = duration;

            s->prevCol = (start + speed) % width;
            break;
        }

        // right to left
        case 3:
        {
            int start = width - 1 - s->prevCol;
            int stop  = width - s->prevCol - speed;
            if (stop < 0) stop = 0;

            for (int row = 0; row < height; ++row)
            {
                int base = row * width;

                for (int col = start; col >= stop; --col)
                {
                    int idx = base + col;

                    bufY[idx] = dstY[idx];
                    bufU[idx] = dstU[idx];
                    bufV[idx] = dstV[idx];
                }
            }

            if (stop == 0)
                s->stopCount = duration;

            s->prevCol = (s->prevCol + speed) % width;
            break;
        }
    }

    veejay_memcpy(dstY, bufY, len);
    veejay_memcpy(dstU, bufU, len);
    veejay_memcpy(dstV, bufV, len);
}