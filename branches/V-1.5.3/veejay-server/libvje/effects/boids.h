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

#ifndef BOIDS_H
#define BOIDS_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *boids_init(int w, int h);
void boids_apply( VJFrame *frame, int width, int height, int p0,int p1, int p2, int p3, int p4, int p5, int p6, int p7);
int	boids_malloc( int w, int h );
void boids_free(void);

#endif
