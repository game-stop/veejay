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
#include "binaryoverlays.h"
#include <omp.h>

vj_effect *binaryoverlay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;
    ve->description = "Binary Overlays";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 14;
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Mode");

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
        "Not A and Not B",
        "Not A or Not B",
        "Not A xor Not B",
        "A and Not B",
        "A or Not B",
        "A xor Not B",
        "Not A and B",
        "Not A or B",
        "Not A xor B",
        "A or B",
        "A and B",
        "A xor B",
        "Not (A and B)",
        "Not (A or B)",
        "Not (A xor B)"
    );
    
    return ve;
}

static void _binary_not_and( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = ~(Y[i]) & ~(Y2[i]);
        Cb[i] = 128 + (~(Cb[i]-128) & ~(Cb2[i]-128));
        Cr[i] = 128 + (~(Cr[i]-128) & ~(Cr2[i]-128));
    }
}

static void _binary_xor( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++) {
        Y[i] = Y[i] ^ Y2[i];
        Cb[i] = 128 + ( (Cb[i] - 128) ^ (Cb2[i] - 128) );
        Cr[i] = 128 + ( (Cr[i] - 128) ^ (Cr2[i] - 128) );
    }
}

static void _binary_not_xor( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = ~(Y[i]) ^ ~(Y2[i]);
        Cb[i] = 128 + (~(Cb[i]-128) ^ ~(Cb2[i]-128));
        Cr[i] = 128 + (~(Cr[i]-128) ^ ~(Cr2[i]-128));
    }
}

static void _binary_not_or( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = ~(Y[i]) | ~(Y2[i]);
        Cb[i] = 128 + ( ~(Cb[i]-128) | ~(Cb2[i]-128) );
        Cr[i] = 128 + (~(Cr[i]-128) | ~(Cr2[i]-128) );
    }
}

static void _binary_not_and_lh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
	{
        Y[i] = Y[i] & ~(Y2[i]);
        Cb[i] = 128 + ( (Cb[i] - 128) & ~(Cb2[i] - 128) );
        Cr[i] = 128 + ( (Cr[i] - 128) & ~(Cr2[i] - 128) );
    }
}

static void _binary_not_xor_lh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = Y[i] ^ ~(Y2[i]);
        Cb[i] = 128 + ( (Cb[i]-128) ^ ~(Cb2[i]-128));
        Cr[i] = 128 + ( (Cr[i]-128) ^ ~(Cr2[i]-128));
    }
}

static void _binary_not_or_lh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = Y[i] | ~(Y2[i]);
        Cb[i] = 128 + ( (Cb[i]-128) | ~(Cb2[i]-128));
        Cr[i] = 128 + ( (Cr[i]-128) | ~(Cr2[i]-128));
    }
}
static void _binary_not_and_rh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++) {
        Y[i] = ~(Y[i]) & Y2[i];
        Cb[i] = 128 + ( ~(Cb[i] - 128) & (Cb2[i] - 128) );
        Cr[i] = 128 + ( ~(Cr[i] - 128) & (Cr2[i] - 128) );
    }
}
static void _binary_not_xor_rh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = ~(Y[i]) ^ Y2[i];
        Cb[i] = 128 + ( ~(Cb[i]-128) ^ (Cb2[i]-128));
        Cr[i] = 128 + (~(Cr[i]-128) ^ (Cr2[i]-128));
    }
}

static void _binary_not_or_rh( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
    {
        Y[i] = ~(Y[i]) | Y2[i];
        Cb[i] = 128 + ( ~(Cb[i]-128) | (Cb2[i]-128));
        Cr[i] = 128 + ( ~(Cr[i]-128) | (Cr2[i]-128));
    }
}

static void _binary_or( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
	{
        Y[i] = Y[i] | Y2[i];
        Cb[i] = 128 + ( (Cb[i] - 128) | (Cb2[i] - 128) );
        Cr[i] = 128 + ( (Cr[i] - 128) | (Cr2[i] - 128) );
    }
}

static void _binary_and( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++) {
        Y[i] = Y[i] & Y2[i];
        Cb[i] = 128 + ( (Cb[i] - 128) & (Cb2[i] - 128) );
        Cr[i] = 128 + ( (Cr[i] - 128) & (Cr2[i] - 128) );
    }
}

static void _binary_nand( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++)
	{
        Y[i] = ~(Y[i] & Y2[i]);
        Cb[i] = 128 + ( ~((Cb[i] - 128) & (Cb2[i] - 128)) );
        Cr[i] = 128 + ( ~((Cr[i] - 128) & (Cr2[i] - 128)) );
    }
}

static void _binary_nor( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++) {
        Y[i] = ~(Y[i] | Y2[i]);
        Cb[i] = 128 + ( ~((Cb[i] - 128) | (Cb2[i] - 128)) );
        Cr[i] = 128 + ( ~((Cr[i] - 128) | (Cr2[i] - 128)) );
    }
}

static void _binary_nxor( VJFrame *frame, VJFrame *frame2, int w, int h, int n_threads )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0],  *restrict Y2 = frame2->data[0];
    uint8_t *restrict Cb = frame->data[1], *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr = frame->data[2], *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i=0; i < len; i++) {
        Y[i] = ~(Y[i] ^ Y2[i]);
        Cb[i] = 128 + ( ~((Cb[i] - 128) ^ (Cb2[i] - 128)) );
        Cr[i] = 128 + ( ~((Cr[i] - 128) ^ (Cr2[i] - 128)) );
    }
}


void binaryoverlay_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {

    int mode = args[0];
    const int width = frame->width;
    const int height = frame->height;
    int n_threads = vje_advise_num_threads(frame->len);

    switch(mode) {
        case 0:  _binary_not_and(frame, frame2, width, height, n_threads);    break;
        case 1:  _binary_not_or(frame, frame2, width, height, n_threads);     break;
        case 2:  _binary_not_xor(frame, frame2, width, height, n_threads);    break;
        case 3:  _binary_not_and_lh(frame, frame2, width, height, n_threads); break;
        case 4:  _binary_not_or_lh(frame, frame2, width, height, n_threads);  break;
        case 5:  _binary_not_xor_lh(frame, frame2, width, height, n_threads); break;
        case 6:  _binary_not_and_rh(frame, frame2, width, height, n_threads); break;
        case 7:  _binary_not_or_rh(frame, frame2, width, height, n_threads);  break;
        case 8:  _binary_not_xor_rh(frame, frame2, width, height, n_threads); break;
        case 9:  _binary_or(frame, frame2, width, height, n_threads);         break;
        case 10: _binary_and(frame, frame2, width, height, n_threads);        break;
        case 11: _binary_xor(frame, frame2, width, height, n_threads);        break;
        case 12: _binary_nand(frame, frame2, width, height, n_threads);       break;
        case 13: _binary_nor(frame, frame2, width, height, n_threads);        break;
        case 14: _binary_nxor(frame, frame2, width, height, n_threads);       break;
    }
}
