/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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

#include "scratcher.h"
#include "../../config.h"
#include <stdlib.h>
#include "../subsample.h"
#include "common.h"
#define __MAX_SCRATCH 100
uint8_t *frame[3];
int nframe = 0;
int nreverse = 0;
int cycle_check = 0;

vj_effect *scratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = (__MAX_SCRATCH-1);
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->defaults[2] = 1;
    ve->description = "Overlay Scratcher";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 1;
    frame[0] =
	(uint8_t *) malloc(w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    frame[1] =
	(uint8_t *) malloc(w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    frame[2] =
	(uint8_t *) malloc(w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    memset(frame[0], 16, w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    memset(frame[1], 128, w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    memset(frame[2], 128, w * h * sizeof(uint8_t) * __MAX_SCRATCH);
    return ve;

}

void scratcher_free() {
   if(frame[0]) free(frame[0]);
   if(frame[1]) free(frame[1]);
   if(frame[2]) free(frame[2]);

}


void store_frame(uint8_t * yuv1[3], int w, int h, int n, int no_reverse)
{
    int uv_len = (w * h) >> 1;
    if (nreverse)
	nframe--;
    else
	nframe++;

    if (nframe > n) {
	if (no_reverse == 0) {
	    nreverse = 1;
	} else {
	    nframe = 0;
	}
    }

    if (nframe == 0)
	nreverse = 0;

    if (!nreverse) {
	veejay_memcpy(frame[0] + (w * h * nframe), yuv1[0], (w * h));
	veejay_memcpy(frame[1] + (uv_len * nframe), yuv1[1], uv_len);
	veejay_memcpy(frame[2] + (uv_len * nframe), yuv1[2], uv_len);
    } else {
	veejay_memcpy(yuv1[0], frame[0] + (w * h * nframe), (w * h));
	veejay_memcpy(yuv1[1], frame[1] + (uv_len * nframe), uv_len);
	veejay_memcpy(yuv1[2], frame[2] + (uv_len * nframe), uv_len);
    }
}

void scratcher_apply(uint8_t * yuv1[3],
		     int width, int height, int opacity, int n,
		     int no_reverse)
{

    unsigned int x, y, len = width * height;
    unsigned int op1 = (opacity > 255) ? 255 : opacity;
    unsigned int op0 = 255 - op1;
    unsigned int uv_len = len >> 1;
    int offset = len * nframe;

    if(no_reverse)
    if (nframe == 0) {
	veejay_memcpy(frame[0] + (len * nframe), yuv1[0], len);
	veejay_memcpy(frame[1] + (uv_len * nframe), yuv1[1], uv_len);
	veejay_memcpy(frame[2] + (uv_len * nframe), yuv1[2], uv_len);
    }
    if(!no_reverse && nframe==0) {
		veejay_memcpy(frame[0] + (len * nframe), yuv1[0], len);
	veejay_memcpy(frame[1] + (uv_len * nframe), yuv1[1], uv_len);
	veejay_memcpy(frame[2] + (uv_len * nframe), yuv1[2], uv_len);

	}
    for (x = 0; x < len; x++) {
	yuv1[0][x] =
	    ((op0 * yuv1[0][x]) + (op1 * frame[0][offset + x])) / 255;
    }
    offset = uv_len * nframe;

    for (x = 0; x < uv_len; x++) {
	yuv1[1][x] =
	    ((op0 * yuv1[1][x]) + (op1 * frame[1][offset + x])) / 255;
	yuv1[2][x] =
	    ((op0 * yuv1[2][x]) + (op1 * frame[2][offset + x])) / 255;
    }

    store_frame(yuv1, width, height, n, no_reverse);
  /*  if (nframe > 0 && no_reverse == 1) {
	memset(frame[0] + (len * (nframe - 1)), 16, len);
	memset(frame[1] + (uv_len * (nframe - 1)), 128, uv_len);
	memset(frame[2] + (uv_len * (nframe - 1)), 128, uv_len);
  }*/  

}
void oldscratcher_free(){}
