/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2006 Niels Elburg <nwelburg@gmail.com>
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

#ifndef NETINSTR_HH
#define NETINSTR_HH

int	net_already_opened(const char *filname, int n, int chan);
void	net_thread_stop(vj_tag *tag);
int	net_thread_start(vj_tag *tag, int w, int h, int f);
void	net_thread_remote(void *priv, void *p );
int	net_thread_get_frame( vj_tag *tag, uint8_t *buffer[3]);
void	net_thread_exit(vj_tag *tag);
void	*net_threader(VJFrame *frame);

#endif
