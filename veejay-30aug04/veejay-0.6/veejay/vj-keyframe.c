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
#include <config.h>
#include "sampleadm.h"
#include "vj-common.h"
void vj_effect_new_key_frame(vj_effect_key_frame *ekf, int start_val, int end_val, int length, int clip_offset)
{
	if(length <= 0)
	{
		return;
	}
	ekf->duration = length;
	ekf->start_value = start_val;
	ekf->end_value = end_val;
	ekf->last_value = start_val;
	ekf->enabled = 1;
	ekf->clip_offset = clip_offset;
	/* descend */
	if(start_val > end_val)
	{
		ekf->direction = -1;
		ekf->inc_val = (float) ( start_val - end_val) / length;
	}
	else /* ascend */
	{
		ekf->direction = 1;
		ekf->inc_val = (float) ( end_val - start_val) / length;
	}
	veejay_msg(VEEJAY_MSG_INFO,"Ekf direction %d -> increment value is %03.2f. [%d -> %d]",ekf->direction,	
		ekf->inc_val,ekf->start_value,ekf->end_value);
}

int vj_effect_clear_key_frame(vj_effect_key_frame *ekf)
{
	if(ekf==NULL) return -1;
	ekf->enabled = 0;
	ekf->last_value = 0;
	ekf->end_value = 0;
	ekf->start_value = 0;
	ekf->direction = 0;
	ekf->clip_offset = 0;
	return 1;
}

int vj_effect_enable_key_frame(vj_effect_key_frame *ekf)
{	
	if(ekf==NULL) return 0;
	ekf->enabled = 1;
	return 1;
}	

 int vj_effect_disable_key_frame(vj_effect_key_frame *ekf)
{
	if(ekf==NULL) return 0;
	ekf->enabled = 0;
	return 1;
}

int vj_effect_apply_key_frame(vj_effect_key_frame *ekf, int n_elapsed)
{
	if(ekf->enabled == 0) return ekf->last_value;
	
	if(n_elapsed <= ekf->duration)
	{
		int val = (int) (n_elapsed * ekf->inc_val);
		int ret_val = 0;
		if ( ekf->direction == -1) 
		{
			ret_val = ekf->start_value + val;
			if(ret_val < ekf->start_value)
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Assertion failedon ret_val < start_value");
			}
		}
		else
		{
			ret_val = ekf->start_value + val;
			if(ret_val > ekf->end_value)
			{
				veejay_msg(VEEJAY_MSG_DEBUG,"Assertion failed, ret_val > end_value");
				return ekf->end_value;
			}
		}
		veejay_msg(VEEJAY_MSG_DEBUG,"Value %d\n", ret_val);
		return ret_val;
	}
	return ekf->last_value; /* initial effect value */
}

