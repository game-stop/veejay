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

#ifndef OPACITY_H
#define OPACITY_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *opacity_init();
void opacity_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity);

void	opacity_blend_luma_apply( uint8_t *src, uint8_t *dst, int len, int opacity );
void	opacity_blend_apply( uint8_t *src[3], uint8_t *dst[3], int len, int uv_len, int opacity );
void opacity_applyN( VJFrame *frame, VJFrame *frame2, int width,  int height, int opacity);
void opacity_free();
#endif
