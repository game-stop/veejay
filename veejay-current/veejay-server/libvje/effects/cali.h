/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2010 Niels Elburg <nwelburg@gmail.com>
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

#ifndef CALIEFFECT_H
#define CALIEFFECT_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *cali_init(int width, int height);
void cali_free(void *d);
int cali_malloc(void **c, int w, int h);
int cali_prepare(void *userd, double *a, double *b, double *c, uint8_t *d, int e , int f); 
void cali_apply(void *d , VJFrame *frame,
		int width, int height, 
		int mode, int full);
void	cali_destroy();
#endif
