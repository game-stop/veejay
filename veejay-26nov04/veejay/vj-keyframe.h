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
#ifdef VJ_KEYFRAME_H
#define VJ_KEYFRAME_H
#include "sampleadm.h"


void vj_effect_new_key_frame(vj_effect_key_frame *,int, int, int, int);
int vj_effect_apply_key_frame(vj_effect_key_frame *, int );
int vj_effect_clear_key_frame(vj_effect_key_frame *);
int vj_effect_enable_key_frame(vj_effect_key_frame *);
int vj_effect_disable_key_frame(vj_effect_key_frame *);


#endif 
