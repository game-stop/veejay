/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nelburg@looze.net>
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

#ifndef UVCORRECT_H
#define UVCORRECT_H
#include "../vj-effect.h"
#include <sys/types.h>
#include <stdint.h>

vj_effect *uvcorrect_init(int w, int h);
int uvcorrect_malloc(int w, int h);
void uvcorrect_free(void);
void uvcorrect_apply(VJFrame *frame, int width, int height,
			int alpha, int ualpha, int valpha, int uf, 
			int vf, int min, int max );
#endif
