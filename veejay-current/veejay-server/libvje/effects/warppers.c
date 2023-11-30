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
#include "warppers.h"

vj_effect *warppers_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3600;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3600;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 1000;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = w;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = h;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1000;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 1000;


    ve->defaults[0] = 15;
    ve->defaults[1] = 0;
    ve->defaults[2] = 100;
    ve->defaults[3] = w/2;
    ve->defaults[4] = h/2;
    ve->defaults[5] = 0;
    ve->defaults[6] = 0;

    ve->description = "Warp Perspective";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 2; //use thread local
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "X Angle", "Y Angle", "Zoom" , "X Center" , "Y Center", "Distance Falloff", "Perspective Strength" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
    double *lut;

    double *cos_lut;
    double *sin_lut;
    

} warppers_t;

#define LUT_SIZE 3600

static void init_cos_lut(warppers_t *f)
{
    for (int i = 0; i < LUT_SIZE; ++i)
    {
        double angle = i * 0.1;
        f->cos_lut[i] = cos(angle * M_PI / 180.0);
    }
}

static void init_sin_lut(warppers_t *f)
{
    for (int i = 0; i < LUT_SIZE; ++i)
    {
        double angle = i * 0.1;
        f->sin_lut[i] = sin(angle * M_PI / 180.0);
    }
}

void *warppers_malloc(int w, int h) {
    warppers_t *s = (warppers_t*) vj_calloc(sizeof(warppers_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    s->lut = (double*) vj_malloc(sizeof(double) * (LUT_SIZE * 2) );
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->sin_lut = s->lut;
    s->cos_lut = s->sin_lut + LUT_SIZE;

    init_cos_lut(s);
    init_sin_lut(s);

    return (void*) s;
}

void warppers_free(void *ptr) {
    warppers_t *s = (warppers_t*) ptr;
    free(s->buf[0]);
    free(s->lut);
    free(s);
}

void warppers_apply(void *ptr, VJFrame *frame, int *args) {
    warppers_t *warp = (warppers_t*)ptr;

    const int width = frame->out_width;
    const int height = frame->out_height;

	const int w = frame->width;
	const int h = frame->height;

    uint8_t *restrict srcY = frame->data[0] - frame->offset;
    uint8_t *restrict srcU = frame->data[1] - frame->offset;
    uint8_t *restrict srcV = frame->data[2] - frame->offset;

    double *restrict cos_lut = warp->cos_lut;
    double *restrict sin_lut = warp->sin_lut;

    const double zoom = (double)args[2] * 0.01;
    double falloff = (double)args[5] * 0.01;
    const double strength = (double)args[6] * 0.01;

    falloff *= falloff;

    const int x_angle = args[0];
    const int y_angle = args[1];
    const int x_center = args[3];
    const int y_center = args[4];

    uint8_t *outY;
    uint8_t *outU;
    uint8_t *outV;

    if( vje_setup_local_bufs( 1, frame, &outY, &outU, &outV, NULL ) == 0 ) {
        const int len = width * height;
    	uint8_t *restrict bufY = warp->buf[0];
    	uint8_t *restrict bufU = warp->buf[1];
    	uint8_t *restrict bufV = warp->buf[2];

        veejay_memcpy( bufY, srcY, len );
        veejay_memcpy( bufU, srcU, len );
        veejay_memcpy( bufV, srcV, len );

        srcY = bufY;
        srcU = bufU;
        srcV = bufV;
    }

    const int max_dist = (width >> 1) * (width >> 1) + (height >> 1) * (height >> 1);
    const double strength_factor = 1.0 - strength;
    const double cos_val = cos_lut[x_angle];
    const double sin_val = sin_lut[y_angle];

	const int start = (frame->jobnum * h);	
	const int end = start + h;

    for (int y_pos = 0; y_pos < h; y_pos++) {
        for (int x_pos = 0; x_pos < w; x_pos++) {
            const int idx = y_pos * w + x_pos;

            const int dx = x_pos - x_center;
            const int dy = start + y_pos - y_center;
            const int dist = dx * dx + dy * dy;
            const double dmd = (double) dist / max_dist;
            const double factor = (1.0 - falloff * dmd) * (strength_factor + strength * dmd);

            int x = x_center + (zoom * factor * (cos_val * dx - sin_val * dy));
            int y = y_center + (zoom * factor * (sin_val * dx + cos_val * dy));

            x = x % width;
            y = y % height;

            x += (x < 0) ? width : 0;
            y += (y < 0) ? height : 0;

            outY[idx] = srcY[ y * width + x ];
            outU[idx] = srcU[ y * width + x ];
            outV[idx] = srcV[ y * width + x ];
        }
    }
}

