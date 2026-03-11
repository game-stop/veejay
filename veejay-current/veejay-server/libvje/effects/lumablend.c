/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "lumablend.h"

vj_effect *lumablend_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;	/* type */
	ve->limits[1][0] = 1;
	ve->limits[0][1] = 0;	/* threshold 1 */
	ve->limits[1][1] = 255;
	ve->limits[0][2] = 0;	/* threshold 2 */
	ve->limits[1][2] = 255;
	ve->limits[0][3] = 0;	/*opacity */
	ve->limits[1][3] = 255;
	ve->defaults[0] = 1;
	ve->defaults[1] = 0;
	ve->defaults[2] = 35;
	ve->defaults[3] = 150;
	ve->description = "Opacity by Threshold";
	ve->extra_frame = 1;
	ve->parallel = 1;
	ve->sub_format = 1;
	ve->parallel = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Threshold A", "Threshold B", "Opacity" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		   "Inclusive", "Exclusive","Blurred" );	

	return ve;
}

static inline void opacity_threshold_A(VJFrame * restrict frame,
                                       VJFrame * restrict frame2,
                                       int t1, int t2, int opacity)
{
    const int len = frame->len;

    uint8_t * restrict y1 = frame->data[0];
    uint8_t * restrict u1 = frame->data[1];
    uint8_t * restrict v1 = frame->data[2];

    uint8_t * restrict y2 = frame2->data[0];
    uint8_t * restrict u2 = frame2->data[1];
    uint8_t * restrict v2 = frame2->data[2];

    const unsigned int op1 = (unsigned int)(opacity < 0 ? 0 : (opacity > 255 ? 255 : opacity));
    const unsigned int op0 = 255u - op1;

    const unsigned int ut1 = (unsigned int)t1;
    const unsigned int ut2 = (unsigned int)t2;
    const unsigned int tr = ut2 - ut1;
	
    for (int i = 0; i < len; ++i) {
        const unsigned int a1 = (unsigned int)y1[i];
        const unsigned int a2 = (unsigned int)y2[i];

        const unsigned int mask = ((a1 - ut1) <= tr) ? 1u : 0u;

        const unsigned int blended_y = (op0 * a1 + op1 * a2) >> 8;
        const unsigned int ou_y      = a1;

        const unsigned int ou_u      = (unsigned int)u1[i];
        const unsigned int blended_u = (op0 * ou_u + op1 * (unsigned int)u2[i]) >> 8;

        const unsigned int ou_v      = (unsigned int)v1[i];
        const unsigned int blended_v = (op0 * ou_v + op1 * (unsigned int)v2[i]) >> 8;

        y1[i] = (uint8_t)( mask * blended_y + (1u - mask) * ou_y );
        u1[i] = (uint8_t)( mask * blended_u + (1u - mask) * ou_u );
        v1[i] = (uint8_t)( mask * blended_v + (1u - mask) * ou_v );
    }
}

static inline void opacity_threshold_B(VJFrame * restrict frame,
                                       VJFrame * restrict frame2,
                                       int t1, int t2, int opacity)
{
    const int len = frame->len;

    uint8_t * restrict y1 = frame->data[0];
    uint8_t * restrict u1 = frame->data[1];
    uint8_t * restrict v1 = frame->data[2];

    uint8_t * restrict y2 = frame2->data[0];
    uint8_t * restrict u2 = frame2->data[1];
    uint8_t * restrict v2 = frame2->data[2];

    const unsigned int op1 = (unsigned int)(opacity < 0 ? 0 : (opacity > 255 ? 255 : opacity));
    const unsigned int op0 = 255u - op1;

    const unsigned int ut1 = (unsigned int)t1;
    const unsigned int ut2 = (unsigned int)t2;
    const unsigned int tr = ut2 - ut1;
	
    for (int i = 0; i < len; ++i) {
        const unsigned int a1 = (unsigned int)y1[i];
        const unsigned int a2 = (unsigned int)y2[i];

        const unsigned int mask = ((a2 - ut1) <= tr) ? 1u : 0u;

        const unsigned int blended_y = (op0 * a1 + op1 * a2) >> 8;
        const unsigned int ou_y      = a1;

        const unsigned int ou_u      = (unsigned int)u1[i];
        const unsigned int blended_u = (op0 * ou_u + op1 * (unsigned int)u2[i]) >> 8;

        const unsigned int ou_v      = (unsigned int)v1[i];
        const unsigned int blended_v = (op0 * ou_v + op1 * (unsigned int)v2[i]) >> 8;

        y1[i] = (uint8_t)( mask * blended_y + (1u - mask) * ou_y );
        u1[i] = (uint8_t)( mask * blended_u + (1u - mask) * ou_u );
        v1[i] = (uint8_t)( mask * blended_v + (1u - mask) * ou_v );
    }
}

void lumablend_apply(void *ptr, VJFrame * frame, VJFrame * frame2, int *args)
{
    (void)ptr;

    int type    = args[0];
    int t1      = args[1];
    int t2      = args[2];
    int opacity = args[3];

    if (t1 > t2) { // invariant
        int tmp = t1;
        t1 = t2;
        t2 = tmp;
    }

    switch (type) {
        case 0:
            opacity_threshold_A(frame, frame2, t1, t2, opacity);
            break;
        case 1:
            opacity_threshold_B(frame, frame2, t1, t2, opacity);
            break;
    }
}