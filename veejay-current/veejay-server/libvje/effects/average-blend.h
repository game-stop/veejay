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

#ifndef AVERAGEBLEND_H
#define AVERAGEBLEND_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *average_blend_init();
void average_blend_apply( VJFrame *frame, VJFrame *frame2, int width,int height, int average_blend);
void average_blend_blend_luma_apply( uint8_t *src, uint8_t *dst, int len, int average_blend );
void average_blend_blend_apply( uint8_t *src[3], uint8_t *dst[3], int len, int uv_len, int average_blend );
void average_blend_applyN( VJFrame *frame, VJFrame *frame2, int width,  int height, int average_blend);
void average_blend_free();
#endif
