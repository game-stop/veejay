/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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


/* Note that the 'opacity' parameter is sometimes used as a 
   threshold value or substraction value depending on the mode
   of this effect */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "chromamagickalpha.h"
// FIXME: mode 8 and 9 corrupt (green/purple cbcr)

vj_effect *chromamagickalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->defaults[1] = 150;
	ve->parallel = 1;
    ve->description = "Alpha: Chroma Magic Matte";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 25;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->parallel = 1;
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );
    
	ve->hints = vje_init_value_hint_list( ve->num_params );

	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A | FLAG_ALPHA_SRC_B;

	/*fixme */

	vje_build_value_hint_list( ve->hints, ve->limits[1][0],0, 
		"Add Subselect Luma", "Select Min", "Select Max", "Select Difference",
		"Select Difference Negate", "Add Luma", "Select Unfreeze", "Exclusive",
		"Difference Negate", "Additive", "Basecolor", "Freeze", "Unfreeze",
		"Hardlight", "Multiply", "Divide", "Subtract", "Add", "Screen",
		"Difference", "Softlight", "Dodge", "Reflect", "Difference Replace",
		"Darken", "Lighten", "Modulo Add" 
	);
	return ve;
}

static void chromamagicalpha_selectmin(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
 	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];
	uint8_t *restrict aB = frame2->data[3];
	uint8_t *restrict aA = frame2->data[3];
    int a, b;
    const int op_b = 255 - op_a;

	#pragma omp simd 
    for (int i = 0; i < len; i++) {
        int mask = (aA[i] != 0); 
        
        int val_a = (Y[i] * op_a) >> 8;
        int val_b = (Y2[i] * op_b) >> 8;

        if (mask && (val_b < val_a)) {
            Y[i] = val_b;
            Cb[i] = Cb2[i];
            Cr[i] = Cr2[i];
        }
    }
}

static void chromamagicalpha_addsubselectlum(VJFrame *restrict frame, VJFrame *restrict frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict aA = frame->data[3];
    const uint8_t *restrict aB = frame2->data[3];

    const int op_b = 255 - op_a;

    for (int i = 0; i < len; i++)
    {
        const int ya = (Y[i] * op_a) >> 8;
        const int yb = (Y2[i] * op_b) >> 8;

        const int mask = (aA[i] != 0) & (aB[i] != 0) & (yb < ya);

        Y[i]  = mask ? (uint8_t)((ya + yb) >> 1) : Y[i];
        Cb[i] = mask ? (uint8_t)((Cb[i] + Cb2[i]) >> 1) : Cb[i];
        Cr[i] = mask ? (uint8_t)((Cr[i] + Cr2[i]) >> 1) : Cr[i];
    }
}

static void chromamagicalpha_selectmax(VJFrame *restrict frame, VJFrame *restrict frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict aA = frame->data[3];
    const uint8_t *restrict aB = frame2->data[3];

    const int op_b = 255 - op_a;

    for (int i = 0; i < len; i++)
    {
        const int ya = (Y[i] * op_a) >> 8;
        const int yb = (Y2[i] * op_b) >> 8;

        const int mask = (aA[i] != 0) & (aB[i] != 0) & (yb > ya);

        Y[i]  = mask ? (uint8_t)((3 * yb + ya) >> 2) : Y[i];
        Cb[i] = mask ? Cb2[i] : Cb[i];
        Cr[i] = mask ? Cr2[i] : Cr[i];
    }
}

static void chromamagicalpha_selectdiff(VJFrame *restrict frame, VJFrame *restrict frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict aA = frame->data[3];
    const uint8_t *restrict aB = frame2->data[3];

    const int op_b = 255 - op_a;

    for (int i = 0; i < len; i++)
    {
        const int ya_w = (Y[i] * op_a) >> 8;
        const int yb_w = (Y2[i] * op_b) >> 8;

        const int mask = (aA[i] != 0) & (aB[i] != 0) & (ya_w > yb_w);

        const int dy = (int)Y[i] - (int)Y2[i];
        const uint8_t res_y = (uint8_t)(dy < 0 ? -dy : dy);
        const uint8_t res_cb = (uint8_t)(((int)Cb[i] + (int)Cb2[i]) >> 1);
        const uint8_t res_cr = (uint8_t)(((int)Cr[i] + (int)Cr2[i]) >> 1);

        Y[i]  = mask ? res_y : Y[i];
        Cb[i] = mask ? res_cb : Cb[i];
        Cr[i] = mask ? res_cr : Cr[i];
    }
}

static void chromamagicalpha_diffreplace(VJFrame *restrict frame, VJFrame *restrict frame2, int threshold)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict aA = frame->data[3];
    const uint8_t *restrict aB = frame2->data[3];

    unsigned long sum = 0;
    for (int i = 0; i < len; i++)
    {
        const int alpha_mask = (aA[i] != 0) & (aB[i] != 0);
        sum += (Y[i] & -alpha_mask);
    }

    const int op_b = (int)(sum & 0xff);
    const int op_a = 255 - op_b;

    for (int i = 0; i < len; i++)
    {
        const int dy = (int)Y[i] - (int)Y2[i];
        const int abs_dy = dy < 0 ? -dy : dy;
        const int mask = (aA[i] != 0) & (aB[i] != 0) & (abs_dy >= threshold);

        const uint8_t res_y  = (uint8_t)((Y[i] * op_a + Y2[i] * op_b) >> 8);
        const uint8_t res_cb = (uint8_t)((Cb[i] * op_a + Cb2[i] * op_b) >> 8);
        const uint8_t res_cr = (uint8_t)((Cr[i] * op_a + Cr2[i] * op_b) >> 8);

        Y[i]  = mask ? res_y  : Y[i];
        Cb[i] = mask ? res_cb : Cb[i];
        Cr[i] = mask ? res_cr : Cr[i];
    }
}

static void chromamagicalpha_selectdiffneg(VJFrame *restrict frame, VJFrame *restrict frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];
    const uint8_t *restrict aA = frame->data[3];
    const uint8_t *restrict aB = frame2->data[3];

    const int op_b = 255 - op_a;

    for (int i = 0; i < len; i++)
    {
        const int ya_w = (Y[i] * op_a) >> 8;
        const int yb_w = (Y2[i] * op_b) >> 8;

        const int mask = (aA[i] != 0) & (aB[i] != 0) & (ya_w > yb_w);

        const int sum_w = ya_w + yb_w;
        const int fold = 255 - sum_w;
        const int abs_fold = (fold < 0) ? -fold : fold;
        const uint8_t res_y = (uint8_t)(255 - abs_fold);

        const uint8_t res_cb = (uint8_t)((Cb[i] * op_a + Cb2[i] * op_b) >> 8);
        const uint8_t res_cr = (uint8_t)((Cr[i] * op_a + Cr2[i] * op_b) >> 8);

        Y[i]  = mask ? res_y  : Y[i];
        Cb[i] = mask ? res_cb : Cb[i];
        Cr[i] = mask ? res_cr : Cr[i];
    }
}

static void chromamagicalpha_selectunfreeze(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA = frame2->data[3];

    const int op_b = 255 - op_a;
    const int Y_lo = pixel_Y_lo_;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = (Y[i] * op_a) >> 8;
            int b = (Y2[i] * op_b) >> 8;

            if (a > b) {
                if (a > Y_lo) {
                    int inv_b = 256 - b;
                    Y[i] = 255 - (inv_b * inv_b) / a;
                }
                Cb[i] = (Cb[i] + Cb2[i]) >> 1;
                Cr[i] = (Cr[i] + Cr2[i]) >> 1;
            }
        }
    }
}

static void chromamagicalpha_addlum(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA = frame2->data[3];

    const int op_b = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            uint8_t a = (Y[i] * op_a) >> 8;
            uint8_t b = (Y2[i] * op_b) >> 8;

            int inv_b = 256 - b;

            Y[i] = (a * a) / inv_b;
            Cb[i] = (Cb[i] + Cb2[i]) >> 1;
            Cr[i] = (Cr[i] + Cr2[i]) >> 1;
        }
    }
}
static void chromamagicalpha_exclusive(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int c = a + (2 * b) - op_a;
            Y[i] = CLAMP_Y(c - ((a * b) >> 8));

            a = Cb[i];
            b = Cb2[i];
            c = a + (2 * b);
            Cb[i] = CLAMP_UV(c - 0xff);

            a = Cr[i];
            b = Cr2[i];
            c = a + (2 * b);
            Cr[i] = CLAMP_UV(c - 0xff);
        }
    }
}

static void chromamagicalpha_diffnegate(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - o1;
    const int MAGIC_THRESHOLD = 40;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int d = abs(a - b);

            if (d > MAGIC_THRESHOLD) {
                a = Y[i] * o1;
                b = Y2[i] * o2;
                Y[i] = 255 - ((a + b) >> 8);

                a = (Cb[i] - 128) * o1;
                b = (Cb2[i] - 128) * o2;
                Cb[i] = 255 - (128 + ((a + b) >> 8));

                a = (Cr[i] - 128) * o1;
                b = (Cr2[i] - 128) * o2;
                Cr[i] = 255 - (128 + ((a + b) >> 8));
            }
        }
    }
}
static void chromamagicalpha_additive(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = (Y[i] * o1) >> 7;
            int b = (Y2[i] * o2) >> 7;
            int y = a + ((2 * b) - 255);
            Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            a = Cb[i];
            b = Cb2[i];
            int cb = a + ((2 * b) - 255);
            Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));

            a = Cr[i];
            b = Cr2[i];
            int cr = a + ((2 * b) - 255);
            Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
        }
    }
}

static void chromamagicalpha_basecolor(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;

    for (i = 0; i < (size_t)len; i++) {
        if (aA[i] != 0) {
            int a = o1 - Y[i];
            int b = o1 - Y2[i];
            int c = (a * b) >> 8;
            int y = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);
            Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            c = (a * b) >> 8;
            int d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);
            d += 128;
            Cb[i] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            c = (a * b) >> 8;
            d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);
            d += 128;
            Cr[i] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
        }
    }
}

static void chromamagicalpha_freeze(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    int a, b, c;

    if (op_a == 0) op_a = 255;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            a = Y[i];
            b = Y2[i];
            if (b > 0)
                c = 255 - ((op_a - a) * (op_a - a)) / b;
            else
                c = 255 - a;
            Y[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cb[i];
            b = Cb2[i];
            if (b > 0)
                c = 255 - ((256 - a) * (256 - a)) / b;
            else
                c = 255 - a;
            Cb[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cr[i];
            b = Cr2[i];
            if (b > 0)
                c = 255 - ((256 - a) * (256 - a)) / b;
            else
                c = 255 - a;
            Cr[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
        }
    }
}

static void chromamagicalpha_unfreeze(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            if (a > pixel_Y_lo_) {
                int y = 255 - ((op_a - b) * (op_a - b)) / a;
                Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));
            }

            a = Cb[i];
            b = Cb2[i];
            if (a > pixel_U_lo_) {
                int cb = 255 - ((256 - b) * (256 - b)) / a;
                Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));
            }

            a = Cr[i];
            b = Cr2[i];
            if (a > pixel_U_lo_) {
                int cr = 255 - ((256 - b) * (256 - b)) / a;
                Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
            }
        }
    }
}

static void chromamagicalpha_hardlight(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int c;
            if (b < 128)
                c = (a * b) >> 8;
            else
                c = 255 - (((op_a - b) * (op_a - a)) >> 8);
            Y[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            if (b < 128)
                c = (a * b) >> 8;
            else
                c = 255 - (((256 - b) * (256 - a)) >> 8);
            c += 128;
            Cb[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            if (b < 128)
                c = (a * b) >> 8;
            else
                c = 255 - (((256 - b) * (256 - a)) >> 8);
            c += 128;
            Cr[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
        }
    }
}

static void chromamagicalpha_multiply(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = (Y[i] * o1) >> 8;
            int b = (Y2[i] * o2) >> 8;
            Y[i] = (uint8_t)((a * b) >> 8);

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            int c = ((a * b) >> 8) + 128;
            Cb[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            c = ((a * b) >> 8) + 128;
            Cr[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
        }
    }
}

static void chromamagicalpha_divide(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i] * Y[i];
            int b = o1 - Y2[i];
            if (b > pixel_Y_lo_) {
                int y = a / b;
                Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));
            }

            a = Cb[i] * Cb2[i];
            b = 255 - Cb2[i];
            if (b > pixel_U_lo_) {
                int cb = a / b;
                Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));
            }

            a = Cr[i] * Cr[i];
            b = 255 - Cr2[i];
            if (b > pixel_U_lo_) {
                int cr = a / b;
                Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
            }
        }
    }
}

static void chromamagicalpha_subtract(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int y = a - ((b * o1) >> 8);
            Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            a = Cb[i];
            b = Cb2[i];
            int cb = ((a * o2 + b * o1) >> 8);
            Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));

            a = Cr[i];
            b = Cr2[i];
            int cr = ((a * o2 + b * o1) >> 8);
            Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
        }
    }
}

static void chromamagicalpha_add(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int y = a + ((2 * b) - op_a);
            Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            int cb = a + (2 * b) + 128;
            Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            int cr = a + (2 * b) + 128;
            Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
        }
    }
}

static void chromamagicalpha_screen(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = Y[i];
            int b = Y2[i];
            int y = 255 - (((op_a - a) * (op_a - b)) >> 8);
            Y[i] = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            int cb = 255 - (((256 - a) * (256 - b)) >> 8) + 128;
            Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            int cr = 255 - (((256 - a) * (256 - b)) >> 8) + 128;
            Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
        }
    }
}

static void chromamagicalpha_difference(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int a = (Y[i] * o1) >> 7;
            int b = (Y2[i] * o2) >> 7;
            int y = a - b;
            Y[i] = (uint8_t)(y < 0 ? -y : y);

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            int cb = a - b;
            cb = (cb < 0 ? -cb : cb) + 128;
            Cb[i] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            int cr = a - b;
            cr = (cr < 0 ? -cr : cr) + 128;
            Cr[i] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
        }
    }
}

/* not really softlight but still cool */
static void chromamagicalpha_softlightmode(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0 && Y[i] < op_a) {
            int a = Y[i];
            int b = Y2[i];
            int c = (a * b) >> 8;
            Y[i] = (c + a * (255 - (((255 - a) * (255 - b)) >> 8) - c)) >> 8;

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            int abs_a = (a ^ (a >> 31)) - (a >> 31);
            int abs_b = (b ^ (b >> 31)) - (b >> 31);
            c = (abs_a * abs_b) >> 7;
            int d = (c + abs_a * (255 - ((abs_a * abs_b) >> 7) - c)) >> 7;
            d += 128;
            Cb[i] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            abs_a = (a ^ (a >> 31)) - (a >> 31);
            abs_b = (b ^ (b >> 31)) - (b >> 31);
            c = (abs_a * abs_b) >> 7;
            d = (c + abs_a * (255 - ((abs_a * abs_b) >> 7) - c)) >> 7;
            d += 128;
            Cr[i] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
        }
    }
}

static void chromamagicalpha_dodge(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < len; i++) {
        if (aA[i] != 0) {
            int aY = Y[i];
            int bY = Y2[i];

            Y[i] = (aY >= op_a) ? (uint8_t)aY : (uint8_t)((aY << 8) / (bY < 256 ? 256 - bY : 1));

            for (int chan = 0; chan < 2; chan++) {
                int a = (chan == 0 ? Cb[i] : Cr[i]) - 128;
                int b = (chan == 0 ? Cb2[i] : Cr2[i]) - 128;
                if (b > 127) b = 127;
                int denom = 128 - b;
                if (denom <= 0) denom = 1;
                int c = ((a << 7) / denom) + 128;
                uint8_t result = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

                if (chan == 0) Cb[i] = result;
                else Cr[i] = result;
            }
        }
    }
}

static void chromamagicalpha_darken(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0 && Y[i] > Y2[i]) {
            Y[i]  = (uint8_t)(((Y[i]  * o1) + (Y2[i]  * o2)) >> 8);
            Cb[i] = (uint8_t)(((Cb[i] * o1) + (Cb2[i] * o2)) >> 8);
            Cr[i] = (uint8_t)(((Cr[i] * o1) + (Cr2[i] * o2)) >> 8);
        }
    }
}

static void chromamagicalpha_lighten(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    for (i = 0; i < len; i++) {
        if (aA[i] != 0 && Y[i] < Y2[i]) {
            Y[i]  = (uint8_t)(((Y[i]  * o1) + (Y2[i]  * o2)) >> 8);
            Cb[i] = (uint8_t)(((Cb[i] * o1) + (Cb2[i] * o2)) >> 8);
            Cr[i] = (uint8_t)(((Cr[i] * o1) + (Cr2[i] * o2)) >> 8);
        }
    }
}

static void chromamagicalpha_reflect(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    for (i = 0; i < (size_t)len; i++) {
        if (aA[i] != 0 && Y2[i] >= op_a) {
            int a = Y[i];
            int b = Y2[i];
            int c;

            int denomY = 256 - b;
            if (denomY <= 0) denomY = 1;
            Y[i] = (uint8_t)((a * a) / denomY);

            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            if (b == 128) b = 127;
            int denomCb = 128 - b;
            if (denomCb == 0) denomCb = 1;
            c = ((a * a) / denomCb) + 128;
            Cb[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
            if (b == 128) b = 127;
            int denomCr = 128 - b;
            if (denomCr == 0) denomCr = 1;
            c = ((a * a) / denomCr) + 128;
            Cr[i] = (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
        }
    }
}
static void chromamagicalpha_modadd(VJFrame *frame, VJFrame *frame2, int op_a)
{
    size_t i;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];
    uint8_t *restrict aA  = frame2->data[3];

    const int op_b = 255 - op_a;

    for (i = 0; i < (size_t)len; i++) {
        if (aA[i] != 0) {
            int a = (Y[i] * op_a) >> 8;
            int b = (Y2[i] * op_b) >> 8;
            Y[i] = (uint8_t)((a + ((2 * b) - 128)) & 0xFF);

            a = (Cb[i] * op_a) >> 8;
            b = (Cb2[i] * op_b) >> 8;
            Cb[i] = (uint8_t)((a + (2 * b)) & 0xFF);

            a = (Cr[i] * op_a) >> 8;
            b = (Cr2[i] * op_b) >> 8;
            Cr[i] = (uint8_t)((a + (2 * b)) & 0xFF);
        }
    }
}

void chromamagickalpha_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int type = args[0];
    int op_a = args[1];

    switch (type) {
    case 0:
	chromamagicalpha_addsubselectlum(frame, frame2, op_a);
	break;
    case 1:
	chromamagicalpha_selectmin(frame, frame2, op_a);
	break;
    case 2:
	chromamagicalpha_selectmax(frame, frame2, op_a);
	break;
    case 3:
	chromamagicalpha_selectdiff(frame, frame2, op_a);
	break;
    case 4:
	chromamagicalpha_selectdiffneg(frame, frame2, op_a);
	break;
    case 5:
	chromamagicalpha_addlum(frame, frame2, op_a);
	break;
    case 6:
	chromamagicalpha_selectunfreeze(frame, frame2, op_a);
	break;
    case 7:
	chromamagicalpha_exclusive(frame,frame2,op_a);
	break;
   case 8:
	chromamagicalpha_diffnegate(frame,frame2,op_a);
	break;
   case 9:
	chromamagicalpha_additive( frame,frame2,op_a);
	break;
   case 10:
	chromamagicalpha_basecolor(frame,frame2,op_a);
	break;
   case 11:
	chromamagicalpha_freeze(frame,frame2,op_a);
	break;
   case 12:
	chromamagicalpha_unfreeze(frame,frame2,op_a);
	break;
   case 13:
	chromamagicalpha_hardlight(frame,frame2,op_a);
	break;
   case 14:
	chromamagicalpha_multiply(frame,frame2,op_a);
	break;
  case 15:
	chromamagicalpha_divide(frame,frame2,op_a);
	break;
  case 16:
	chromamagicalpha_subtract(frame,frame2,op_a);
	break;
  case 17:
	chromamagicalpha_add(frame,frame2,op_a);
	break;
  case 18:
	chromamagicalpha_screen(frame,frame2,op_a);
	break;
  case 19:
	chromamagicalpha_difference(frame,frame2,op_a);
	break;
  case 20:
	chromamagicalpha_softlightmode(frame,frame2,op_a);
	break;
  case 21:
	chromamagicalpha_dodge(frame,frame2,op_a);
	break;
  case 22:
	chromamagicalpha_reflect(frame,frame2,op_a);
	break;
  case 23:
	chromamagicalpha_diffreplace(frame,frame2,op_a);
	break;
  case 24:
	chromamagicalpha_darken( frame,frame2,op_a);
	break;
  case 25:
	chromamagicalpha_lighten( frame,frame2,op_a);
	break;
  case 26:
	chromamagicalpha_modadd( frame,frame2,op_a);
	break;
    }
}


