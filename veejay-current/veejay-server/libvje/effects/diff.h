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

#ifndef DIFFEFFECT_H
#define DIFFEFFECT_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *diff_init(int width, int height);
void diff_free(void *d);
int diff_malloc(void **c, int w, int h);
int diff_prepare(void *d, uint8_t *map[4], int w, int h); 
void diff_apply(void *d , VJFrame *frame,
		VJFrame *frame2, int width, int height, 
		int th, int reverse, int show, int feather);
void	diff_destroy();
#endif
