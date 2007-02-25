/* veejay - Linux VeeJay Unicap interface
 * 	     (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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

void	*vj_unicap_init(void);
#ifndef VJUNICAP_H
#define VJUNICAP_H
void	vj_unicap_deinit(void *dud );
int	vj_unicap_num_capture_devices( void *dud );



void	*vj_unicap_new_device( void *ud, int device_id );
int	vj_unicap_configure_device( void *ud, int pixel_format, int w, int h );
int	vj_unicap_start_capture( void *vut );
int	vj_unicap_stop_capture( void *vut );
void	vj_unicap_free_device( void *vut );

void	*vj_unicap_get_devices(void *in);

#endif

