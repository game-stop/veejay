/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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

/**
 *
 * Based on the original implementation of Kim Asendorf
 *
 * https://github.com/kimasendorf/ASDFPixelSort/blob/master/ASDFPixelSort.pde
 *
 * ASDFPixelSort
 * Processing script to sort portions of pixels in an image.
 * DEMO: http://kimasendorf.com/mountain-tour/ http://kimasendorf.com/sorted-aerial/
 * Kim Asendorf 2010 http://kimasendorf.com
 */
#ifndef PIXELSORT_H
#define PIXELSORT_H
vj_effect *pixelsort_init(int w, int h);
void  *pixelsort_malloc(int w, int h);
void pixelsort_free(void *ptr);
void pixelsort_apply(void *ptr, VJFrame *frame, int *args);
#endif
