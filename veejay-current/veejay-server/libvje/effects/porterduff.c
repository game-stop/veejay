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
#include <omp.h>

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif

static inline uint8_t DIV255(int x) {
    int v = x + 128;
    return (uint8_t)((v + (v >> 8)) >> 8);
}

typedef struct {
    int n_threads;
} porterduff_t;

vj_effect *porterduff_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 15;

    ve->param_description = vje_build_param_list(ve->num_params, "Operator");
    ve->has_user = 0;
    ve->description = "Porter Duff operations (Luma only)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 0;
    ve->rgba_only = 1;
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
        "Dest", "Dest Atop", "Dest In", "Dest Over", "Dest Out", "Src Over", "Src Atop", "Src In", "Src Out", "Multiply", "Xor", "Add", "Subtract", "Divide", "Screen" , "Overlay" );

    return ve;
}

void *porterduff_malloc(int w, int h) {
    porterduff_t *pt = (porterduff_t*) vj_malloc(sizeof(porterduff_t));
    if(!pt)
      return NULL;
    pt->n_threads = vje_advise_num_threads(w*h);
    return (void*) pt;
}

void porterduff_free(void *ptr) {
    if(ptr) 
      free(ptr);
}

static void porterduff_dst( uint8_t *A, uint8_t *B, int n_pixels)
{
    veejay_memcpy(A, B, n_pixels * 4);
}

static void porterduff_atop( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels ; i ++ ) 
    {
        int idx = i * 4;
        uint8_t as = B[idx + 3];
        uint8_t ad = A[idx + 3];
        A[idx + 0] = DIV255((B[idx + 0] * ad) + (A[idx + 0] * (255 - as)));
        A[idx + 1] = DIV255((B[idx + 1] * ad) + (A[idx + 1] * (255 - as)));
        A[idx + 2] = DIV255((B[idx + 2] * ad) + (A[idx + 2] * (255 - as)));
        A[idx + 3] = ad;
    }
}

static void porterduff_dst_in( uint8_t *A, uint8_t *B, int n_pixels, int n_threads)
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i++ ) 
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        for( int j = 0; j < 4; j ++ )
        {
            A[idx + j] = DIV255( A[idx + j] * a_s );
        }
    }
}

static void porterduff_dst_out( uint8_t *A, uint8_t *B, int n_pixels, int n_threads)
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t inv_a_s = 255 - B[idx + 3];
        for( int j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255( A[idx + j] * inv_a_s );
        }
    }
}

static void porterduff_dst_over(uint8_t *A, uint8_t *B, int n_pixel, int n_threads)
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixel; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];

        A[idx + 3] = a_d + DIV255(a_s * (255 - a_d));
        uint8_t inv_ad = 255 - a_d;

        for( int j = 0; j < 3 ; j ++ )
        {
            int v = A[idx + j] + DIV255(B[idx + j] * inv_ad);
            A[idx + j] = (v > 255) ? 255 : v;
        }
    }
}

static void porterduff_src_over( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        uint8_t out_a = a_s + DIV255(a_d * (255 - a_s));
        for( int j = 0; j < 3 ; j ++ )
        {
            int v = B[idx + j] + DIV255(A[idx + j] * (255 - a_s));
            A[idx+j] = ( v > 255 ? 255: v);
        }
        A[idx + 3] = out_a;
    }
}

static void porterduff_src_atop( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_s = B[idx + 3];
        uint8_t a_d = A[idx + 3];
        for( int j = 0; j < 3 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * a_d + A[idx + j] * (255 - a_s));
        }
        A[idx + 3] = a_s;
    }
}

static void porterduff_src_in( uint8_t *A, uint8_t *B, int n_pixels, int n_threads)
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t a_d = A[idx + 3];
        for( int j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * a_d);
        }
    }
}

static void porterduff_src_out( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t inv_a_d = 255 - A[idx + 3];
        for( int j = 0; j < 4 ; j ++ )
        {
            A[idx + j] = DIV255(B[idx + j] * inv_a_d);
        }
    }
}

static void svg_multiply( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{   
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 3 ; j ++ )
        {
            uint8_t s = B[idx + j];
            uint8_t d = A[idx + j];
            uint8_t sa = B[idx + 3];
            uint8_t da = A[idx + 3];
            uint8_t t1 = DIV255(s * d);
            uint8_t t2 = DIV255(s * (255 - da));
            uint8_t t3 = DIV255(d * (255 - sa));

            int v = t1 + t2 + t3;
            A[idx + j] = (v > 255) ? 255 : v;
        }
        A[idx + 3] = B[idx + 3] + DIV255(A[idx + 3] * (255 - B[idx + 3]));
    }
}

static void vj_xor( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        uint8_t sa = B[idx + 3];
        uint8_t da = A[idx + 3];
        for( int j = 0; j < 3; j ++ )
        {
            uint8_t t1 = DIV255(sa * (255 - da));
            uint8_t t2 = DIV255(da * (255 - sa));
            int v = t1 + t2;
        }
        A[idx + 3] = DIV255(sa * (255 - da) + da * (255 - sa));
    }
}

static void vj_add( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 4; j ++ )
        {
            int sum = A[idx + j] + B[idx + j];
            A[idx + j] = (sum > 255) ? 255 : sum;
        }
    }
}

static void vj_subtract( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 4; j ++ )
        {
            int res = A[idx + j] - B[idx + j];
            A[idx + j] = (res < 0) ? 0 : res;
        }
    }
}

static void vj_divide( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 3; j ++ )
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

static void vj_screen( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 3; j ++ )
        {
            A[idx + j] = 255 - DIV255((255 - A[idx + j]) * (255 - B[idx + j]));
        }
        A[idx + 3] = A[idx + 3] + DIV255(B[idx + 3] * (255 - A[idx + 3]));
    }
}

static void vj_overlay( uint8_t *A, uint8_t *B, int n_pixels, int n_threads )
{
#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for( int i = 0; i < n_pixels; i ++ )
    {
        int idx = i * 4;
        for( int j = 0; j < 3; j ++ )
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
    porterduff_t *pt = (porterduff_t*) ptr;

    pt->n_threads = vje_advise_num_threads(frame->len);
    const int len = frame->len;

    switch( mode )
    {
        case 0:
            porterduff_dst( frame->data[0],frame2->data[0],len );
            break;
        case 1:
            porterduff_atop( frame->data[0],frame2->data[0], len, pt->n_threads );
            break;
        case 2:
            porterduff_dst_in( frame->data[0],frame2->data[0], len, pt->n_threads);
            break;
        case 3:
            porterduff_dst_over( frame->data[0],frame2->data[0],len, pt->n_threads );
            break;
        case 4:
            porterduff_dst_out( frame->data[0],frame2->data[0],len, pt->n_threads );
            break;
        case 5:
            porterduff_src_over( frame->data[0],frame2->data[0],len, pt->n_threads );
            break;
        case 6:
            porterduff_src_atop( frame->data[0],frame2->data[0],len, pt->n_threads );
            break;
        case 7:
            porterduff_src_in( frame->data[0],frame2->data[0],len, pt->n_threads );
            break;
        case 8:
            porterduff_src_out( frame->data[0],frame2->data[0],len, pt->n_threads);
            break;
        case 9:
            svg_multiply( frame->data[0], frame2->data[0], len, pt->n_threads );
            break;
        case 10:
            vj_xor( frame->data[0], frame2->data[0], len, pt->n_threads);
            break;
        case 11:
            vj_add( frame->data[0], frame2->data[0], len, pt->n_threads);
            break;
        case 12:
            vj_subtract(frame->data[0],frame2->data[0],len, pt->n_threads);
            break;
        case 13:
            vj_divide(frame->data[0],frame2->data[0],len, pt->n_threads);
            break;
        case 14:
            vj_screen(frame->data[0],frame2->data[0], len, pt->n_threads);
            break;
        case 15:
            vj_overlay(frame->data[0], frame2->data[0], len, pt->n_threads );
            break;
    }
}