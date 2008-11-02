/*
 * veejay - Linux VeeJay
 * 	     (C) 2003 Niels Elburg <elburg@hio.hen.nl> <nwelburg@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef VJ_OSC 
#define VJ_OSC


int vj_osc_setup_addr_space(void *o);
int vj_osc_get_packet(void *o);
void vj_osc_free(void *o);
void vj_osc_dump();
void* vj_osc_allocate(int port_id);
 
#endif
