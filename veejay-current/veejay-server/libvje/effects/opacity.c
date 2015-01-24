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
    	

/* Alpha blending with MMX, shamelessy ripped from libvisual
 *
 *
 * Libvisual-plugins - Standard plugins for libvisual
 *
 * Copyright (C) 2004, 2005, 2006 Dennis Smit <ds@nerds-incorporated.org>
 *
 * Authors: Dennis Smit <ds@nerds-incorporated.org>
 *
 * $Id: morph_alphablend.c,v 1.19 2006/01/27 20:19:18 synap Exp $
 *
 *
 *
 */     

#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "opacity.h"
#include "common.h"
#include <veejay/vj-task.h>

vj_effect *opacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;
    ve->description = "Normal Overlay";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->parallel = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity"); 
    return ve;
}

#ifdef HAVE_ASM_MMX
static	inline int	blend_plane(uint8_t *dst, uint8_t *A, uint8_t *B, int size, int alpha)
{
	uint32_t ialpha = alpha;
	int i;

	ialpha |= ialpha << 16;

	__asm __volatile
		("\n\t pxor %%mm6, %%mm6"
		 ::);

	for (i = size; i > 4; i -= 4) {
		__asm __volatile
			("\n\t movd %[alpha], %%mm3"
			 "\n\t movd %[src2], %%mm0"
			 "\n\t psllq $32, %%mm3"
			 "\n\t movd %[alpha], %%mm2"
			 "\n\t movd %[src1], %%mm1"
			 "\n\t por %%mm3, %%mm2"
			 "\n\t punpcklbw %%mm6, %%mm0"  /* interleaving dest */
			 "\n\t punpcklbw %%mm6, %%mm1"  /* interleaving source */
			 "\n\t psubsw %%mm1, %%mm0"     /* (src - dest) part */
			 "\n\t pmullw %%mm2, %%mm0"     /* alpha * (src - dest) */
			 "\n\t psrlw $8, %%mm0"         /* / 256 */
			 "\n\t paddb %%mm1, %%mm0"      /* + dest */
			 "\n\t packuswb %%mm0, %%mm0"
			 "\n\t movd %%mm0, %[dest]"
			 : [dest] "=m" (*(dst + i))
			 : [src1] "m" (*(A + i))
			 , [src2] "m" (*(B + i))
			 , [alpha] "m" (ialpha));
	}
	return i;
}
#else
static	inline int blend_plane( uint8_t *dst, uint8_t *A, uint8_t *B, int size, int opacity )
{
    unsigned int i, op0, op1;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for( i = 0; i < size; i ++ )
	dst[i] = (op0 * A[i] + op1 * B[i] ) >> 8;


    return 0;
}
#endif


static void opacity_apply1( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity)
{
	int y = blend_plane( frame->data[0], frame->data[0], frame2->data[0], frame->len, opacity );
	int u = blend_plane( frame->data[1], frame->data[1], frame2->data[1], frame->uv_len, opacity );
	int v = blend_plane( frame->data[2], frame->data[2], frame2->data[2], frame->uv_len, opacity );
#ifdef HAVE_ASM_MMX
	do_emms;
#endif
	if( y>0) while (y--)
		frame->data[0][y] = ((opacity * (frame->data[0][y] - frame2->data[0][y])) >> 8 ) + frame->data[0][y];

	if( u>0) while( u-- )
		frame->data[1][u] = ((opacity * (frame->data[1][u] - frame2->data[1][u])) >> 8 ) + frame->data[1][u];

	if(v>0)	 while( v-- )
		frame->data[2][v] = ((opacity * (frame->data[2][v] - frame2->data[2][v])) >> 8 ) + frame->data[2][v];

}

static void	opacity_apply_job( void *arg )
{
	vj_task_arg_t *t = (vj_task_arg_t*) arg;
	//@ output holds secondunary source here, this in inplace.
	int y = blend_plane( t->input[0], t->input[0], t->output[0], t->strides[0], t->iparam );
	int u = blend_plane( t->input[1], t->input[1], t->output[1], t->strides[1], t->iparam );
	int v = blend_plane( t->input[2], t->input[2], t->output[2], t->strides[2], t->iparam );
#ifdef HAVE_ASM_MMX
	do_emms;
#endif
	if( y>0) while (y--)
		t->input[0][y] = ((t->iparam * (t->input[0][y] - t->output[0][y])) >> 8 ) + t->input[0][y];

	if( u>0) while( u-- )
		t->input[1][u] = ((t->iparam * (t->input[1][u] - t->output[1][u])) >> 8 ) + t->input[1][u];

	if(v>0)	 while( v-- )
		t->input[2][v] = ((t->iparam * (t->input[2][v] - t->output[2][v])) >> 8 ) + t->input[2][v];

}

void opacity_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity)
{
	opacity_apply1( frame,frame2,width,height,opacity );
}

void opacity_applyN( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity)
{
	if( vj_task_available() ) {
		vj_task_set_from_frame( frame );
		vj_task_set_int( opacity );
		vj_task_run( frame->data, frame2->data, NULL, NULL, 3, (performer_job_routine) &opacity_apply_job );
	} else {
		opacity_apply1( frame,frame2,width,height,opacity );
	}
}
	
//@ used in performer
static void	opacity_blend_apply1( uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int opacity )
{
	int y = blend_plane( src1[0], src1[0], src2[0], len, opacity );
	int u = blend_plane( src1[1], src1[1], src2[1], uv_len,opacity);
	int v = blend_plane( src1[2], src1[2], src2[2], uv_len, opacity);
#ifdef HAVE_ASM_MMX
	do_emms;
#endif

	while (y--)
		src1[0][y] = ((opacity * (src1[0][y] - src2[0][y])) >> 8) + src1[0][y];
	while( u-- )
		src1[1][u] = ((opacity * (src1[1][u] - src2[1][u])) >> 8) + src1[1][u];
	while( v-- )
		src1[2][v] = ((opacity * (src1[2][v] - src2[2][v])) >> 8) + src1[2][v];
}

void	opacity_blend_apply( uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int opacity )
{
	if( vj_task_available() ) {
		vj_task_set_from_args( len,uv_len );
		vj_task_set_int( opacity );
		vj_task_run( src1, src2, NULL, NULL, 3, (performer_job_routine) &opacity_apply_job );
	} else {
		opacity_blend_apply1( src1,src2,len,uv_len,opacity );
	}
}
void	opacity_blend_luma_apply( uint8_t *A, uint8_t *B, int len,int opacity )
{
	int y = blend_plane( A,A,B, len, opacity );
#ifdef HAVE_ASM_MMX
	do_emms;
#endif
	while (y--)
		A[y] = ((opacity * (A[y] - B[y])) >> 8 ) + A[y];
}

void opacity_free(){}
