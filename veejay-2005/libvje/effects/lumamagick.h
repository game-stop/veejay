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
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *lumamagick_init();

void lumamagic_apply(VJFrame *frame, VJFrame *frame2, int width,
		     int height, int n, int op_a, int op_b);

void _lumamagick_add_distorted(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a, int op_b);

void _lumamagick_sub_distorted(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a, int op_b);

void _lumamagick_multiply(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);

void _lumamagick_divide(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b);

void _lumamagick_additive(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);

void _lumamagick_substractive(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b);

void _lumamagick_softburn(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);

void _lumamagick_inverseburn(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b);

void _lumamagick_colordodge(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b);

void _lumamagick_mulsub(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b);

void _lumamagick_lighten(VJFrame *frame, VJFrame *frame2, int width,
			 int height, int op_a, int op_b);

void _lumamagick_difference(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b);

void _lumamagick_diffnegate(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b);

void _lumamagick_exclusive(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);

void _lumamagick_basecolor(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);

void _lumamagick_freeze(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b);

void _lumamagick_unfreeze(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);

void _lumamagick_hardlight(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);


void _lumamagick_relativesublum(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a, int op_b);


void _lumamagick_relativeaddlum(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a, int op_b);


void _lumamagick_relativesub(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b);

void _lumamagick_relativeadd(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b);


void _lumamagick_maxselect(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);


void _lumamagick_minselect(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);


void _lumamagick_minsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b);
void _lumamagick_maxsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b);
void _lumamagick_addsubselectlum(VJFrame *frame, VJFrame *frame2,
				 int width, int height, int op_a,
				 int op_b);

void _lumamagick_addsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b);
void _lumamagick_addtest(VJFrame *frame, VJFrame *frame2, int width,
			 int height, int op_a, int op_b);
void _lumamagick_addtest2(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);
void _lumamagick_addtest3(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);
void _lumamagick_addtest4(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b);

void _lumamagick_selectmin(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);

void _lumamagick_selectmax(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b);

void _lumamagick_selectfreeze(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b);

void _lumamagick_selectunfreeze(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a, int op_b);

void _lumamagick_selectdiff(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b);

void _lumamagick_selectdiffneg(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a, int op_b);
void _lumamagick_addlum(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b);

#endif
