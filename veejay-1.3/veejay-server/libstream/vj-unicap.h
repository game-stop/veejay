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
#ifndef  VJUNICAPHH
#define  VJUNICAPHH
void	*vj_unicap_init(void);
void	vj_unicap_deinit(void *dud );
int	vj_unicap_num_capture_devices( void *dud );
char **vj_unicap_get_devices(void *unicap, int *n);
void	*vj_unicap_new_device( void *ud, int device_id );
int	vj_unicap_configure_device( void *ud, int pixel_format, int w, int h, int composite );
int	vj_unicap_start_capture( void *vut );
int	vj_unicap_grab_frame( void *vut, uint8_t *buffer[3], const int w, const int h );
int	vj_unicap_stop_capture( void *vut );
int	vj_unicap_composite_status(void *ud );
int	vj_unicap_status(void *vut);
void	vj_unicap_free_device( void *dud, void *vut );
char        **vj_unicap_get_list( void *ud );
int vj_unicap_get_value( void *ud, char *key, int atom_type, void *value );
int	vj_unicap_select_value( void *ud, int key, double );

void	vj_unicap_set_pause( void *vut , int status );
int	vj_unicap_get_pause( void *vut );

#define	UNICAP_BRIGHTNESS	0
#define	UNICAP_COLOR		1
#define UNICAP_SATURATION	2
#define UNICAP_HUE		3
#define UNICAP_CONTRAST		4
#define	UNICAP_SOURCE0		5
#define UNICAP_SOURCE1		6
#define UNICAP_SOURCE2		7
#define UNICAP_SOURCE3		8
#define UNICAP_SOURCE4		9
#define UNICAP_PAL		10
#define UNICAP_NTSC		11
#define UNICAP_WHITE		12
#endif

