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

#ifndef OPACITY_H
#define OPACITY_H
vj_effect *opacity_init(int w, int h);
void opacity_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args);
void opacity_blend_luma_apply( uint8_t *src, uint8_t *dst, int len, int opacity );
void opacity_blend_apply( uint8_t *src[3], uint8_t *dst[3], int len, int uv_len, int opacity );
void opacity_apply1( VJFrame *frame, VJFrame *frame2, int opacity);
#endif
