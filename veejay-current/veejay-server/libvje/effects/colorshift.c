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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "colorshift.h"

vj_effect *colorshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 235;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->description = "Shift pixel values YCbCr";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );
	ve->parallel = 1;
    return ve;
}


/* bitwise and test */

void softmask2_apply(VJFrame *frame, int width, int height, int paramt)
{
    const unsigned int len = frame->len;
    unsigned int x;
	uint8_t *Y = frame->data[0];
    for (x = 0; x < len; x++)
		Y[x] &= paramt;
}

void softmask2_applycb(VJFrame *frame, int width, int height,
		       int paramt)
{
    const unsigned int len = frame->uv_len;
	uint8_t *Cb = frame->data[1];
    unsigned int x;
	for (x = 0; x < len; x++)
		Cb[x] &= paramt;
}

void softmask2_applycr(VJFrame *frame, int width, int height,
		       int paramt)
{
	uint8_t *Cr = frame->data[2];
    const unsigned int len = frame->uv_len;
    unsigned int x;
    for (x = 0; x < len; x++)
		Cr[x] &= paramt;
}

void softmask2_applycbcr(VJFrame *frame, int width, int height,
			 int paramt)
{
    const unsigned int len = frame->uv_len;
    unsigned int x;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    for (x = 0; x < len; x++) {
		Cb[x] &= paramt;
		Cr[x] &= paramt;
    }
}

void softmask2_applyycbcr(VJFrame *frame, int width, int height,
			  int paramt)
{
    const unsigned int len = frame->len;
    const int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	
    unsigned int x;

    for (x = 0; x < len; x++)
		Y[x] &= paramt;

    for (x = 0; x < uv_len; x++) {
		Cb[x] &= paramt;
		Cr[x] &= paramt;
    }
}

void softmask_apply(VJFrame *frame, int width, int height, int paramt)
{
    const unsigned int len = frame->len;
    unsigned int x;
	uint8_t *Y = frame->data[0];
	

    for (x = 0; x < len; x++)
		Y[x] |= paramt;
}


void softmask_applycb(VJFrame *frame, int width, int height, int paramt)
{
    const unsigned int len = frame->uv_len;
	uint8_t *Cb = frame->data[1];
	
 
   unsigned int x;
    for (x = 0; x < len; x++)
		Cb[x] |= paramt;
}


void softmask_applycr(VJFrame *frame, int width, int height, int paramt)
{
    const unsigned int len = frame->uv_len;
	uint8_t *Cr = frame->data[2];
	

    unsigned int x;
    for (x = 0; x < len; x++)
		Cr[x] |= paramt;
}


void softmask_applycbcr(VJFrame *frame, int width, int height,
			int paramt)
{
    const unsigned int len = frame->uv_len;
    unsigned int x;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

    for (x = 0; x < len; x++)
	{
		Cb[x] |= paramt;
		Cr[x] |= paramt;
    }
}

void softmask_applyycbcr(VJFrame *frame, int width, int height,
			 int paramt)
{
    const int len =  frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	const int uv_len = frame->uv_len;

    unsigned int x;
    for (x = 0; x < len; x++)
		Y[x] |= paramt;

    for (x = 0; x < uv_len; x++) {
		Cb[x] |= paramt;
		Cr[x] |= paramt;
    }
}


void colorshift_apply(VJFrame *frame, int width, int height, int type,
		      int n)
{
    switch (type) {
    case 0:
	softmask_apply(frame, width, height, n);
	break;
    case 1:
	softmask_applycb(frame, width, height, n);
	break;
    case 2:
	softmask_applycr(frame, width, height, n);
	break;
    case 3:
	softmask_applycbcr(frame, width, height, n);
	break;
    case 4:
	softmask_applyycbcr(frame, width, height, n);
	break;
    case 5:
	softmask2_apply(frame, width, height, n);
	break;
    case 6:
	softmask2_applycb(frame, width, height, n);
	break;
    case 7:
	softmask2_applycr(frame, width, height, n);
	break;
    case 8:
	softmask2_applycbcr(frame, width, height, n);
	break;
    case 9:
	softmask2_applyycbcr(frame, width, height, n);
	break;
    }
}
void colorshift_free(){}
