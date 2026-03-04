/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "porterduff.h"

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif

static inline uint8_t DIV255(int x) {
    int v = x + 128;
    return (uint8_t)((v + (v >> 8)) >> 8);
}

//FIXME too slow, blend operators still wrong

vj_effect *porterduff_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;	/* operator */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 15;

	ve->param_description = vje_build_param_list(ve->num_params, "Operator");
	ve->has_user = 0;
    ve->description = "Porter Duff operations (Luma only)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 0;
    ve->parallel = 1;
	ve->rgba_only = 1;
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, 
		"Dest", "Dest Atop", "Dest In", "Dest Over", "Dest Out", "Src Over", "Src Atop", "Src In", "Src Out", "Multiply", "Xor", "Add", "Subtract", "Divide", "Screen" , "Overlay" );

	return ve;
} 

static void porterduff_dst( uint8_t *A, uint8_t *B, int n_pixels)
{
	veejay_memcpy(A, B, n_pixels * 4);	
}

static void porterduff_atop( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels ; i ++ ) 
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        for( j = 0; j < 3; j ++ ) 
        {
            A[idx + j] = DIV255( (B[idx + j] * a_d) + (A[idx + j] * (255 - a_s)) );
        }
        A[idx + 3] = a_d; 
    }
}
static void porterduff_dst_in( uint8_t *A, uint8_t *B, int n_pixels)
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i++ ) 
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        for( j = 0; j < 4; j ++ ) // Apply to RGB and Alpha
        {
            A[idx + j] = DIV255( A[idx + j] * a_s );
        }
    }
}
static void porterduff_dst_out( uint8_t *A, uint8_t *B, int n_pixels)
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t inv_a_s = 255 - B[idx + 3];
        for( j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255( A[idx + j] * inv_a_s );
        }
    }
}

static void porterduff_dst_over( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        A[idx + 3] = a_d + DIV255(a_s * (255 - a_d));
        for( j = 0; j < 3 ; j ++ )
        {
            A[idx + j] = A[idx + j] + DIV255(B[idx + j] * (255 - a_d)); 
        }
    }
}
static void porterduff_src_over( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        uint8_t out_a = a_s + DIV255(a_d * (255 - a_s));
        for( j = 0; j < 3 ; j ++ )
        {
            A[idx + j] = B[idx + j] + DIV255(A[idx + j] * (255 - a_s));
        }
        A[idx + 3] = out_a;
    }
}

static void porterduff_src_atop( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        for( j = 0; j < 3 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * a_d + A[idx + j] * (255 - a_s));
        }
        A[idx + 3] = a_s;
    }
}

static void porterduff_src_in( uint8_t *A, uint8_t *B, int n_pixels)
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_d = A[idx + 3];
        for( j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * a_d);
        }
    }
}

static void porterduff_src_out( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t inv_a_d = 255 - A[idx + 3];
        for( j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * inv_a_d);
        }
    }
}

static void svg_multiply( uint8_t *A, uint8_t *B, int n_pixels )
{   
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 3 ; j ++ )
        {
            uint8_t s = B[idx + j];
            uint8_t d = A[idx + j];
            uint8_t sa = B[idx + 3];
            uint8_t da = A[idx + 3];
            A[idx + j] = DIV255(s * d + s * (255 - da) + d * (255 - sa));
        }
        A[idx + 3] = B[idx + 3] + DIV255(A[idx + 3] * (255 - B[idx + 3]));
    }
}

static void xor( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t sa = B[idx + 3];
        uint8_t da = A[idx + 3];
        for( j = 0; j < 3; j ++ ) 
        {
            A[idx + j] = DIV255(B[idx + j] * (255 - da) + A[idx + j] * (255 - sa));
        }
        A[idx + 3] = DIV255(sa * (255 - da) + da * (255 - sa));
    }
}
static void add( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 4; j ++ ) 
        {
            int sum = A[idx + j] + B[idx + j];
            A[idx + j] = (sum > 255) ? 255 : sum;
        }
    }
}
static void subtract( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 4; j ++ ) 
        {
            int res = A[idx + j] - B[idx + j];
            A[idx + j] = (res < 0) ? 0 : res;
        }
    }
}

static void divide( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 3; j ++ ) 
        {
            if (B[idx + j] == 0) {
                A[idx + j] = 255; 
            } else {
                float res = ((float)A[idx + j] / (float)B[idx + j]) * 255.0f;
                A[idx + j] = (res > 255.0f) ? 255 : (uint8_t)res;
            }
        }
    }
}

static void screen( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 3; j ++ ) 
        {
            A[idx + j] = 255 - DIV255((255 - A[idx + j]) * (255 - B[idx + j]));
        }
        A[idx + 3] = A[idx + 3] + DIV255(B[idx + 3] * (255 - A[idx + 3]));
    }
}

static void overlay( uint8_t *A, uint8_t *B, int n_pixels )
{
    int i, j;
#pragma omp simd
    for( i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( j = 0; j < 3; j ++ ) 
        {
            uint8_t d = A[idx + j];
            uint8_t s = B[idx + j];
            if (d < 128) {
                A[idx + j] = DIV255(2 * d * s);
            } else {
                A[idx + j] = 255 - DIV255(2 * (255 - d) * (255 - s));
            }
        }
        A[idx + 3] = A[idx + 3] + DIV255(B[idx + 3] * (255 - A[idx + 3]));
    }
}

void porterduff_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args ){

    int mode = args[0];

	const int len = frame->len;
	switch( mode )
	{
		case 0:
			porterduff_dst( frame->data[0],frame2->data[0],len );
			break;
		case 1:
			porterduff_atop( frame->data[0],frame2->data[0], len );
			break;
		case 2:
			porterduff_dst_in( frame->data[0],frame2->data[0], len );
			break;
		case 3:
			porterduff_dst_over( frame->data[0],frame2->data[0],len );
			break;
		case 4:
			porterduff_dst_out( frame->data[0],frame2->data[0],len );
			break;
		case 5:
			porterduff_src_over( frame->data[0],frame2->data[0],len );
			break;
		case 6:
			porterduff_src_atop( frame->data[0],frame2->data[0],len );
			break;
		case 7:
			porterduff_src_in( frame->data[0],frame2->data[0],len );
			break;
		case 8:
			porterduff_src_out( frame->data[0],frame2->data[0],len);
			break;
		case 9:
			svg_multiply( frame->data[0], frame2->data[0], len );
			break;
		case 10:
			xor( frame->data[0], frame2->data[0], len);
			break;
		case 11:
			add( frame->data[0], frame2->data[0], len );
			break;
		case 12:
			subtract(frame->data[0],frame2->data[0],len);
			break;
		case 13:
			divide(frame->data[0],frame2->data[0],len);
			break;
		case 14:
			screen(frame->data[0],frame2->data[0], len);
			break;
		case 15:
			overlay(frame->data[0], frame2->data[0], len );
	}
}
