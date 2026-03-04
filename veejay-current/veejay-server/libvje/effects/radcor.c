/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail>
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

/* Radial Distortion Correction
 * http://local.wasp.uwa.edu.au/~pbourke/projection/lenscorrection/
 *
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "radcor.h"

vj_effect *radcor_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;

	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 1;
	ve->limits[1][0] = 1000;
	ve->limits[0][1] = 1;
	ve->limits[1][1] = 1000;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 1;
	ve->defaults[0] = 10;
	ve->defaults[1] = 40;
	ve->defaults[2] = 0;
    ve->defaults[3] = 0;
	ve->description = "Lens correction";
	ve->sub_format = 1;
	ve->extra_frame = 0;
	ve->has_user = 0;

	ve->alpha = FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_OUT;
	ve->param_description = vje_build_param_list( ve->num_params, "Alpha X", "Alpha Y", "Direction", "Update Alpha");

	return ve;
}

typedef struct {
    uint8_t *badbuf;
    uint32_t *Map;
    int map_upd[4];
	float *x_lut;
} radcor_t;

void *radcor_malloc( int w, int h )
{
    radcor_t *r = (radcor_t*) vj_calloc(sizeof(radcor_t));
    if(!r) {
        return NULL;
    }

    const int len = (w * h);
    const int total_len = (len * 4);

	r->badbuf = (uint8_t*) vj_malloc( sizeof(uint8_t) * total_len );
	if(!r->badbuf) {
        free(r);
		return NULL;
    }

	r->Map = (uint32_t*) vj_calloc( sizeof(uint32_t) * len );
	if(!r->Map) {
        free(r->badbuf);
        free(r);
		return NULL;
    }

	r->x_lut = (float*) vj_calloc(sizeof(float) * w);
	if(!r->x_lut) {
	    free(r->badbuf);
        free(r->Map);
		free(r);
		return NULL;
    }
	return (void*) r;
}

void radcor_free(void *ptr)
{
    radcor_t *r = (radcor_t*) ptr;
    free(r->badbuf);
    free(r->Map);
	free(r->x_lut);
    free(r);
}

typedef struct
{
	uint32_t y;
	uint32_t v;
	uint32_t u;
} pixel_t;

void radcor_apply(void *ptr, VJFrame *frame, int *args)
{
    int alpaX = args[0];
    int alpaY = args[1];
    int dir   = args[2];
    int alpha = args[3];

    radcor_t *radcor = (radcor_t*) ptr;

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = frame->len;

    uint8_t * restrict Y  = frame->data[0];
    uint8_t * restrict Cb = frame->data[1];
    uint8_t * restrict Cr = frame->data[2];
    uint8_t * restrict A  = frame->data[3];

    uint8_t * restrict Yi  = radcor->badbuf;
    uint8_t * restrict Cbi = radcor->badbuf + len;
    uint8_t * restrict Cri = radcor->badbuf + len + len;
    uint8_t * restrict Ai  = radcor->badbuf + len + len + len;

    uint32_t * restrict Map = radcor->Map;
	float *restrict x_lut = radcor->x_lut;

    uint8_t *dest[4] = { Yi, Cbi, Cri, Ai };
    int strides[4] = { len, len, len, alpha ? len : 0 };
    vj_frame_copy(frame->data, dest, strides);

    double alphax = alpaX * (1.0 / 1000.0);
    double alphay = alpaY * (1.0 / 1000.0);

    if (!dir) {
        alphax = -alphax;
        alphay = -alphay;
    }

    vj_frame_clear1(Y,  0,   len);
    vj_frame_clear1(Cb, 128, len);
    vj_frame_clear1(Cr, 128, len);
    if (alpha)
        vj_frame_clear1(A, 0, len);

    int update_map = 0;

    if (radcor->map_upd[0] != alpaX ||
        radcor->map_upd[1] != alpaY ||
        radcor->map_upd[2] != dir)
    {
        radcor->map_upd[0] = alpaX;
        radcor->map_upd[1] = alpaY;
        radcor->map_upd[2] = dir;
        update_map = 1;
    }

	if (update_map)
	{
		const float inv_w = 1.0f / (float)width;
		const float inv_h = 1.0f / (float)height;

		const float half_w = 0.5f * (float)width;
		const float half_h = 0.5f * (float)height;

		const float ax = (float)alphax;
		const float ay = (float)alphay;

		for (int j = 0; j < width; ++j)
			x_lut[j] = ((2.0f * j) - width) * inv_w;

		for (int i = 0; i < height; ++i)
		{
			const float y = ((2.0f * (float)i) - (float)height) * inv_h;

			for (int j = 0; j < width; ++j)
			{
				const float x = x_lut[j];

				const float r  = x * x + y * y;

				const float inv1x = 1.0f / (1.0f - ax * r);
				const float inv1y = 1.0f / (1.0f - ay * r);

				const float x3 = x * inv1x;
				const float y3 = y * inv1y;

				const float r2 = x3 * x3 + y3 * y3;

				const float inv2x = 1.0f / (1.0f - ax * r2);
				const float inv2y = 1.0f / (1.0f - ay * r2);

				const float x2 = x * inv2x;
				const float y2 = y * inv2y;

				const int i2 = (int)((y2 + 1.0f) * half_h);
				const int j2 = (int)((x2 + 1.0f) * half_w);

				const int pos = i * width + j;

				if ((unsigned)i2 < (unsigned)height &&
					(unsigned)j2 < (unsigned)width)
				{
					Map[pos] = (uint32_t)(i2 * width + j2);
				}
				else
				{
					Map[pos] = 0;
				}
			}
		}
	}

    for (int i = 0; i < len; ++i)
    {
        uint32_t idx = Map[i];
        Y[i]  = Yi[idx];
        Cb[i] = Cbi[idx];
        Cr[i] = Cri[idx];
    }

    if (alpha)
    {
        for (int i = 0; i < len; ++i)
            A[i] = Ai[ Map[i] ];
    }
}