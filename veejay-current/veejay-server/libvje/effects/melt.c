/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <libvje/internal.h>
#include "melt.h"

vj_effect *melt_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 1;
	ve->defaults[1] = 1;
	ve->description = "Melt";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 100;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 100;
	ve->extra_frame = 1;
	ve->sub_format = 1;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Speed", "Intensity" );

	return ve;
}

typedef struct {
	uint8_t *buf[3];
} melt_t;


void *melt_malloc(int w, int h )
{
	melt_t *t = (melt_t*) vj_malloc(sizeof(melt_t));
	if(!t) {
		return NULL;
	}
	t->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
	if(!t->buf[0]) {
		free(t);
		return NULL;
	}
	t->buf[1] = t->buf[0] + (w*h);
	t->buf[2] = t->buf[1] + (w*h);
	
	return (void*) t;
}

void melt_free(void *ptr) {
	melt_t *t = (melt_t*) ptr;
	free(t->buf[0]);
	free(t);
}


void melt_apply(void *ptr, VJFrame *A, VJFrame *B, int *args ) {

	melt_t *t = (melt_t*) ptr;

    int speed = args[0];
    int intensity = args[1];

    uint8_t *restrict srcAY = A->data[0];
    uint8_t *restrict srcAU = A->data[1];
    uint8_t *restrict srcAV = A->data[2];

	uint8_t *restrict dstY = t->buf[0];
	uint8_t *restrict dstU = t->buf[1];
	uint8_t *restrict dstV = t->buf[2];

    uint8_t *restrict srcBY = B->data[0];

    const unsigned int width = A->width;
    const unsigned int height = A->height;
    const unsigned int len = A->len;

    int meltThreshold = intensity; 
   

	/* taking a copy here improves the performance because
	 * it avoids reading and writing to the same memory location
	 */

	veejay_memcpy( dstY, srcAY, A->len );
	veejay_memcpy( dstU, srcAU, A->len );
	veejay_memcpy( dstV, srcAV, A->len );


    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            unsigned int idx = y * width + x;
            
            unsigned int meltIdx = idx + speed;
            int mask = -(srcBY[idx] > meltThreshold);

            srcAY[idx] = (mask & dstY[meltIdx]) | (~mask & srcAY[idx]);
            srcAU[idx] = (mask & dstU[meltIdx]) | (~mask & srcAU[idx]);
            srcAV[idx] = (mask & dstV[meltIdx]) | (~mask & srcAV[idx]);
        }
    }





}
