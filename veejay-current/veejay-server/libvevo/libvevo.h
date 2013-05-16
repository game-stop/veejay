/* veejay - vevo objects
 * 	     (C) 2002-2011 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef LIBVEVO
#define LIBVEVO

#include <config.h>
#include <stdint.h>
typedef void vevo_port_t;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif


int		vevo_property_num_elements( vevo_port_t *p, const char *key);

int 	vevo_property_atom_type( vevo_port_t *p, const char *key);

size_t 	vevo_property_element_size( vevo_port_t * p, const char *key, const int idx);

#ifdef STRICT_CHECKING
vevo_port_t *vevo_port_new( int port_type, const char *func,const int line_no );
void		 vevo_port_free( vevo_port_t *port, const char *func, const int line_no );
#else
vevo_port_t *vevo_port_new(int port_type);
void		 vevo_port_free( vevo_port_t *port );
#endif

#ifdef STRICT_CHECKING
#define vpn(type) vevo_port_new( type, __FUNCTION__ , __LINE__ )
#define vpf( port ) vevo_port_free( port, __FUNCTION__, __LINE__ )
#else
#define vpn(type) vevo_port_new( type )
#define vpf( port ) vevo_port_free( port )
#endif

int 	vevo_property_soft_reference(vevo_port_t * p, const char *key);

void	vevo_strict_init();

int	vevo_port_verify( vevo_port_t *port );

int	vevo_property_set(vevo_port_t * p, const char *key, int atom_type, int num_elements, void *src);

int	vevo_union_ports( void *port_a, void *port_b, int filter_type );

int 	vevo_property_get(vevo_port_t * p, const char *key, int idx, void *dst);

char 	**vevo_list_properties(vevo_port_t * p);

void	vevo_port_recursive_free(vevo_port_t *p );

char	*vevo_format_kind( vevo_port_t *port, const char *key );

char	*vevo_format_property( vevo_port_t *port, const char *key );

void	vevo_report_stats();

int	vevo_property_del(vevo_port_t * p,   const char *key );

//char	**vevo_port_deepen_namespace( void *port, char *path);

void 	*vevo_port_recurse_namespace( vevo_port_t *port, const char *base );

void	*vevo_port_register( vevo_port_t *in, vevo_port_t *ref );

char  	*vevo_sprintf_property( vevo_port_t *port, const char *key  );

int	vevo_sscanf_property( vevo_port_t *port, const char *s);

char	**vevo_sprintf_port( vevo_port_t *port );

int	vevo_sscanf_port( vevo_port_t *port, const char *s );

int	vevo_special_union_ports( void *port_a, void *port_b );

int	vevo_property_from_string( vevo_port_t *port, const char *s, const char *key, int n_elem, int type);

char	*vevo_sprintf_property_value( vevo_port_t *port, const char *key);

char    *vevo_property_get_string( void *port, const char *key );

char    *vevo_property_get_utf8string( void *port, const char *key );

void	vevo_strict_init();

int     vevo_property_call(vevo_port_t * p, const char *key, void *ctx, int32_t type, int32_t value );

int	vevo_property_call_get( vevo_port_t *p, const char *key, void *ctx );

int	vevo_property_clone( void *port, void *to_port, const char *key, const char *as_key );

int	vevo_property_protect( vevo_port_t *p, const char *key  );

void	vevo_port_dump( void *p, int lvl );

int	vevo_property_set_f(vevo_port_t * p, const char *key,  int atom_type, int num_elements, void (*set_func)() , int (*get_func)() );

int	vevo_property_softref( void *port, const char *key );

int	vevo_port_get_total_size( vevo_port_t *port );

char	**vevo_property_get_string_arr( vevo_port_t *p, const char *key );

#define VEVO_ATOM_TYPE_FUNCPTR	11
#define	VEVO_ATOM_TYPE_VOIDPTR	65
#define VEVO_ATOM_TYPE_INT	1
#define VEVO_ATOM_TYPE_DOUBLE	2
#define VEVO_ATOM_TYPE_STRING	4
#define VEVO_ATOM_TYPE_UTF8STRING 8
#define VEVO_ATOM_TYPE_BOOL	3
#define VEVO_ATOM_TYPE_PORTPTR	66
#define VEVO_ATOM_TYPE_HIDDEN   50
#define VEVO_ATOM_TYPE_UINT64	5

#define VEVO_NO_ERROR 0
#define VEVO_ERROR_MEMORY_ALLOCATION 1
#define VEVO_ERROR_PROPERTY_READONLY 2
#define VEVO_ERROR_NOSUCH_ELEMENT 3
#define VEVO_ERROR_NOSUCH_PROPERTY 4
#define VEVO_ERROR_WRONG_ATOM_TYPE 5
#define VEVO_ERROR_TOO_MANY_INSTANCES 6
#define VEVO_ERROR_HARDWARE 7
#define VEVO_ERROR_PROPERTY_EMPTY 8
#define VEVO_ERROR_INVALID_VALUE 9

#define VEVO_PORT_POOL			1041
#define VEVO_ANONYMOUS_PORT		-1

#define VEVO_PROPERTY_READONLY (1<<1)
#define VEVO_PROPERTY_SOFTREF  (1<<2)
#define	VEVO_PROPERTY_PROTECTED (1<<10)

#endif 
