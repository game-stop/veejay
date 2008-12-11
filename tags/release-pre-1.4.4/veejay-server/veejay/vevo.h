/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VEVO_H_INC
#define VEVO_H_INC

#ifndef HAVE_LIVIDO_PORT_T
#define HAVE_LIVIDO_PORT_T
typedef struct livido_port_t void
#endif

#include <libvevo/vevo.h>

// rename functions
/*
#define vevo_port_free	livido_port_free
#define	vevo_port_new	livido_port_new
#define vevo_property_set livido_property_set
#define vevo_property_get livido_property_get
#define vevo_property_element_size livido_property_element_size
#define vevo_property_num_elements livido_property_num_elements
#define vevo_property_atom_type livido_property_atom_type
#define vevo_list_properties livido_list_properties*/

void 	*vj_event_vevo_get_event_function( int id );
char	*vj_event_vevo_get_event_name( int id );
char	*vj_event_vevo_get_event_format( int id );
int	vj_event_vevo_get_num_args(int id);
int	vj_event_vevo_get_flags( int id );
int	vj_event_vevo_get_vims_id( int id );
void	vj_init_vevo_events(void);
void	vj_event_vevo_inline_fire_default( void *super, int vims_id, const char *format );
void	vj_event_vevo_inline_fire(void *super, int vims_id, const char *format, ... );
char	*vj_event_vevo_list_serialize(void);
void	vj_event_vevo_dump(void);
char	*vj_event_vevo_help_vims( int id, int n );
int	vj_event_vevo_get_default_value(int id, int p);
void 	vj_event_vevo_free(void);
#endif
