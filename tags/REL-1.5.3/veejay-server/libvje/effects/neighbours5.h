/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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

#ifndef NEIGHBOURS5_H
#define NEIGHBOURS5_H
#include <libvje/vje.h>
#include <sys/types.h>
#include <stdint.h>

vj_effect *neighbours5_init(int w, int h);
int neighbours5_malloc(int w, int h);
void neighbours5_free(void);
void neighbours5_apply( VJFrame *frame, int width, int height, int radius,int brush_size, int level);
#endif
