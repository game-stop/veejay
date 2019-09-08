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

#ifndef CHAMELEON_H
#define CHAMELEON_H
vj_effect *chameleon_init(int w, int h);
void chameleon_apply( void *ptr, VJFrame *frame, int *args);
void *chameleon_malloc(int w, int h );
void chameleon_free(void *ptr);
int chameleon_prepare(void *ptr, VJFrame *frame);
int chameleon_request_fx();
void chameleon_set_motionmap(void *ptr, void *priv);

#endif
