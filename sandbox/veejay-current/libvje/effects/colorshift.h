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

#ifndef COLORSHIFT_H
#define COLORSHIFT_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

/* this effect uses bit masking to increase/decrease the luma cq chroma values
   in a frame. with this you can create distorted colours. */

vj_effect *colorshift_init();
void colorshift_apply( VJFrame *frame, int width, int height, int n,
		      int type);
void colorshift_free();
#endif
