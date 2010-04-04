/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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

#ifndef RGBKEY_H
#define RGBKEY_H
#include <sys/types.h>
#include <stdint.h>
#include <libvje/vje.h>
vj_effect *rgbkey_init();
void rgbkey_scan_fg(uint8_t * src2[3], int *r, int *g, int *b);
void rgbkey_apply(VJFrame *frame, VJFrame *frame2, int width,
		  int height, int i_angle, int i_noise,
		  int red, int green, int blue, int type );
void rgbkey_free();
#endif
