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

#ifndef LUMAMAGIC_H
#define LUMAMAGIC_H
#include "../vj-effect.h"
#include <sys/types.h>
#include <stdint.h>

vj_effect *lumamagick_init();

void lumamagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		     int height, int n, int op_a, int op_b);

void _lumamagick_add_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b);

void _lumamagick_sub_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b);

void _lumamagick_multiply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);

void _lumamagick_divide(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b);

void _lumamagick_additive(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);

void _lumamagick_substractive(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b);

void _lumamagick_softburn(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);

void _lumamagick_inverseburn(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b);

void _lumamagick_colordodge(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b);

void _lumamagick_mulsub(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b);

void _lumamagick_lighten(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height, int op_a, int op_b);

void _lumamagick_difference(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b);

void _lumamagick_diffnegate(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b);

void _lumamagick_exclusive(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);

void _lumamagick_basecolor(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);

void _lumamagick_freeze(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b);

void _lumamagick_unfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);

void _lumamagick_hardlight(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);


void _lumamagick_relativesublum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b);


void _lumamagick_relativeaddlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b);


void _lumamagick_relativesub(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b);

void _lumamagick_relativeadd(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b);


void _lumamagick_maxselect(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);


void _lumamagick_minselect(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);


void _lumamagick_minsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b);
void _lumamagick_maxsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b);
void _lumamagick_addsubselectlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				 int width, int height, int op_a,
				 int op_b);

void _lumamagick_addsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b);
void _lumamagick_addtest(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height, int op_a, int op_b);
void _lumamagick_addtest2(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);
void _lumamagick_addtest3(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);
void _lumamagick_addtest4(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b);

void _lumamagick_selectmin(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);

void _lumamagick_selectmax(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b);

void _lumamagick_selectfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b);

void _lumamagick_selectunfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b);

void _lumamagick_selectdiff(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b);

void _lumamagick_selectdiffneg(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b);
void _lumamagick_addlum(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b);

#endif
