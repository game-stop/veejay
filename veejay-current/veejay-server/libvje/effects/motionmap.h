/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail.com>
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

#ifndef MOTIONMAP_H
#define MOTIONMAP_H

#ifndef VJ_IMAGE_EFFECT_MOTIONMAP_ID
#define VJ_IMAGE_EFFECT_MOTIONMAP_ID 184
#endif

vj_effect *motionmap_init(int w, int h);
void motionmap_apply( void *ptr, VJFrame *frame, int *args );
void *motionmap_malloc(int w,int h);
void motionmap_free(void *ptr);
int	motionmap_prepare( void *ptr, VJFrame *frame );
int	motionmap_active( void *ptr);
int	motionmap_instances(void *ptr);
int	motionmap_is_locked(void *ptr);
uint8_t	*motionmap_interpolate_buffer(void *ptr);
uint8_t *motionmap_bgmap(void *ptr);
void motionmap_store_frame( void *ptr,VJFrame *fx );
void motionmap_interpolate_frame( void *ptr,VJFrame *fx, int N, int n );
void motionmap_scale_to( void *ptr, int p1max, int p2max, int p1min, int p2min, int *p1val, int *p2val, int *pos, int *len );
uint32_t motionmap_activity(void *ptr);
#endif
