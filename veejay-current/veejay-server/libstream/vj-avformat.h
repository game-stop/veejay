/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2018 Niels Elburg <nwelburg@gmail.com>
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
 */

#ifndef AVFORMAT_STREAM_HH
#define AVFORMAT_STREAM_HH

void	avformat_thread_stop(vj_tag *tag);
int	avformat_thread_start(vj_tag *tag, VJFrame *info);
int	avformat_thread_get_frame( vj_tag *tag, VJFrame *dst);
void	*avformat_thread_allocate(VJFrame *frame);
int	avformat_thread_set_state(vj_tag *,int new_state);
#endif
