/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include "vj-frameinfo.h"
#include "colorspace.h"

/* not activly in use, will change this someday */

void vj_frameinfo_init(vj_frameinfo * frameinfo, int width, int height)
{
    int j, i;
    if (frameinfo == NULL)
	return;
    if (frameinfo->histogram_yuv[0] == NULL)
	return;
    if (frameinfo->histogram_yuv[1] == NULL)
	return;
    if (frameinfo->histogram_yuv[2] == NULL)
	return;
    frameinfo->width = width;
    frameinfo->height = height;
    frameinfo->len = (width * height);
    frameinfo->x = 0;
    frameinfo->y = 0;
    for (i = 0; i < 256; i++) {
	for (j = 0; j < 3; j++) {
	    frameinfo->histogram_yuv[j][i] = 0;
	}
    }
    if (!vj_frameinfo_reset(frameinfo)) {
	printf("woops\n");
    }
}

int vj_frameinfo_clean(vj_frameinfo * fi)
{
    int res = 1;
    if (fi == NULL)
	return 0;
    if (fi->histogram_yuv[0])
	free(fi->histogram_yuv[0]);
    else
	res = 0;
    if (fi->histogram_yuv[1])
	free(fi->histogram_yuv[1]);
    else
	res = 0;
    if (fi->histogram_yuv[2])
	free(fi->histogram_yuv[2]);
    else
	res = 0;
    if (fi)
	free(fi);
    else
	res = 0;
    return res;
}

int vj_frameinfo_make_histogram(vj_frameinfo * frameinfo,
				uint8_t * yuv_frame[3])
{
    int i;
    if (frameinfo == NULL)
	return -1;
    if (yuv_frame == NULL)
	return -1;
    /* summarize occuring values */
    for (i = 0; i < frameinfo->len; i++) {
	frameinfo->histogram_yuv[0][(yuv_frame[0][i])]++;
	frameinfo->histogram_yuv[1][(yuv_frame[1][i])]++;
	frameinfo->histogram_yuv[2][(yuv_frame[2][i])]++;
    }
    return 1;
}

int vj_frameinfo_most_occuring(vj_frameinfo * frameinfo, int *r, int *g,
			       int *b)
{
    int i;
    uint8_t y = 0, aa;
    uint8_t cb = 0, bb;
    uint8_t cr = 0, cc;
    uint8_t planes[1][3];
    if (frameinfo == NULL)
	return 0;
    if (frameinfo->histogram_yuv[0] == NULL)
	return 0;
    if (frameinfo->histogram_yuv[1] == NULL)
	return 0;
    if (frameinfo->histogram_yuv[2] == NULL)
	return 0;
    /* simple linear method to find highest value.  */
    for (i = 0; i < 255; i++) {
	aa = frameinfo->histogram_yuv[0][i];
	bb = frameinfo->histogram_yuv[1][i];
	cc = frameinfo->histogram_yuv[2][i];
	if (aa < frameinfo->histogram_yuv[0][i + 1])
	    y = frameinfo->histogram_yuv[0][i + 1];
	if (bb < frameinfo->histogram_yuv[1][i + 1])
	    cb = frameinfo->histogram_yuv[1][i + 1];
	if (cc < frameinfo->histogram_yuv[2][i + 1])
	    cr = frameinfo->histogram_yuv[2][i + 1];
    }
    planes[0][0] = y;
    planes[0][1] = cb;
    planes[0][2] = cr;

    //convert_YCbCr_to_RGB(planes,1);

    *r = planes[0][0];
    *g = planes[0][1];
    *b = planes[0][2];
    return 1;
}

int vj_frameinfo_less_occuring(vj_frameinfo * frameinfo, int *r, int *g,
			       int *b)
{
    int i;
    uint8_t y = 0, aa;
    uint8_t cb = 0, bb;
    uint8_t cr = 0, cc;
    if (frameinfo == NULL)
	return 0;
    if (frameinfo->histogram_yuv[0] == NULL)
	return 0;
    if (frameinfo->histogram_yuv[1] == NULL)
	return 0;
    if (frameinfo->histogram_yuv[2] == NULL)
	return 0;
    /* simple linear method to find highest value.  */
    for (i = 0; i < 255; i++) {
	aa = frameinfo->histogram_yuv[0][i];
	bb = frameinfo->histogram_yuv[1][i];
	cc = frameinfo->histogram_yuv[2][i];
	if (aa > frameinfo->histogram_yuv[0][i + 1])
	    y = frameinfo->histogram_yuv[0][i + 1];
	if (bb > frameinfo->histogram_yuv[1][i + 1])
	    cb = frameinfo->histogram_yuv[1][i + 1];
	if (cc > frameinfo->histogram_yuv[2][i + 1])
	    cr = frameinfo->histogram_yuv[2][i + 1];
    }
    *r = (int) (+(1.000 * y) + (0.000 * cb) + (1.1402 * cr));
    *g = (int) (+(1.000 * y) - (0.344136 * cb) - (0.714136 * cr));
    *b = (int) (+(1.000 * y) + (1.772 * cb) + (0.0 * cr));
    return 1;
}

int vj_frameinfo_reset(vj_frameinfo * fi)
{
    int i;
    if (!fi)
	return 0;
    if (fi->histogram_yuv[0] == NULL)
	return 0;
    if (fi->histogram_yuv[1] == NULL)
	return 0;
    if (fi->histogram_yuv[2] == NULL)
	return 0;
    for (i = 0; i < 256; i++) {
	fi->histogram_yuv[0][i] = 0;
	fi->histogram_yuv[1][i] = 0;
	fi->histogram_yuv[2][i] = 0;
    }
    fi->x = 0;
    fi->y = 0;
    return 1;
}

void vj_frameinfo_what_is(vj_frameinfo * fi, uint8_t * yuv_frame[3],
			  int *r, int *g, int *b)
{

    *r = (int) (+(1.000 * yuv_frame[0][(fi->x * fi->y)]) +
		(0.000 * yuv_frame[1][(fi->x * fi->y)]) +
		(1.1402 * yuv_frame[2][(fi->x * fi->y)]));
    *g = (int) (+(1.000 * yuv_frame[0][(fi->x * fi->y)]) -
		(0.344136 * yuv_frame[1][(fi->x * fi->y)]) -
		(0.714136 * yuv_frame[2][(fi->x * fi->y)]));
    *b = (int) (+(1.000 * yuv_frame[0][(fi->x * fi->y)]) +
		(1.772 * yuv_frame[1][(fi->x * fi->y)]) +
		(0.0 * yuv_frame[2][(fi->x * fi->y)]));
}

int vj_frameinfo_set_dot(vj_frameinfo * fi, int x, int y)
{
    if (fi == NULL)
	return 0;
    if (x < 0 || y < 0)
	return 0;
    fi->x = x;
    fi->y = y;
    return 1;
}
