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

#ifndef MAGICOVERLAYS_H
#define MAGICOVERLAYS_H
vj_effect *overlaymagic_init(int w, int h);
void overlaymagic_apply(void *ptr, VJFrame *frame, VJFrame *frame2,int *args);
void overlaymagic_adddistorted(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_add_distorted(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_subdistorted(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_sub_distorted(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_multiply(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_simpledivide(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_divide(VJFrame *frame, VJFrame *frame2);
void overlaymagic_additive(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_substractive(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_softburn(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_inverseburn(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_colordodge(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_mulsub(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_lighten(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_difference(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_diffnegate(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_exclusive(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_freeze(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_unfreeze(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_basecolor(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_relativeaddlum(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_relativeadd(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_relativesublum(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_relativesub(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_hardlight(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_minsubselect(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_maxsubselect(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_addsubselect(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_maxselect(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_minselect(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_addtest(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_addtest2(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_addtest4(VJFrame *frame, VJFrame *frame2 );
void overlaymagic_try(VJFrame *frame, VJFrame *frame2 );
#endif
