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

#ifndef TMAPEFFECT_H
#define TMAPEFFECT_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *texmap_init(int width, int height);
void texmap_free(void *d);
int texmap_malloc(void **c, int w, int h);
void texmap_prepare(void *d, uint8_t *map[3], int w, int h); 
void texmap_apply(void *d , VJFrame *frame,
		VJFrame *frame2, int width, int height, 
		int th, int reverse, int show, int take, int feather, int blob);
void	texmap_destroy();
#endif
