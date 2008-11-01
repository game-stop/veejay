/*
Copyright (c) 2004-2005 N.Elburg <nelburg@looze.net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#ifdef STRICT_CHECKING
#define vpn(type) vevo_port_new( type, __FUNCTION__ , __LINE__ )
#else
#define vpn(type) vevo_port_new( type )
#endif

int	vevo_property_num_elements( vevo_port_t *p, const char *key);

int 	vevo_property_atom_type( vevo_port_t *p, const char *key);

size_t vevo_property_element_size( vevo_port_t * p, const char *key, const int idx);

#ifdef STRICT_CHECKING
vevo_port_t *vevo_port_new( int port_type, const char *func, int line_no );
#else
vevo_port_t *vevo_port_new(int port_type);
#endif
int 	vevo_property_soft_reference(vevo_port_t * p, const char *key);

void	vevo_strict_init();

int	vevo_port_verify( vevo_port_t *port );

void	vevo_port_free( vevo_port_t *port );

int	vevo_property_set(vevo_port_t * p, const char *key, int atom_type, int num_elements, void *src);

int	vevo_union_ports( void *port_a, void *port_b, int filter_type );

int 	vevo_property_get(vevo_port_t * p, const char *key, int idx, void *dst);

char 	**vevo_list_properties(vevo_port_t * p);

void	vevo_port_recursive_free(vevo_port_t *p );

void	vevo_port_dump( vevo_port_t *p );

char	*vevo_format_kind( vevo_port_t *port, const char *key );

char	*vevo_format_property( vevo_port_t *port, const char *key );

void	vevo_report_stats();

int	vevo_property_del(vevo_port_t * p,   const char *key );

char	**vevo_port_deepen_namespace( void *port, char *path);

char	**vevo_port_recurse_namespace( vevo_port_t *port, const char *base );

void	*vevo_port_register( vevo_port_t *in, vevo_port_t *ref );

char  	*vevo_sprintf_property( vevo_port_t *port, const char *key  );

int	vevo_sscanf_property( vevo_port_t *port, const char *s);

char	**vevo_sprintf_port( vevo_port_t *port );

int	vevo_sscanf_port( vevo_port_t *port, const char *s );

int	vevo_special_union_ports( void *port_a, void *port_b );

int	vevo_property_from_string( vevo_port_t *port, const char *s, const char *key, int n_elem, int type);

char	*vevo_sprintf_property_value( vevo_port_t *port, const char *key);

char            *vevo_property_get_string( void *port, const char *key );

#define	VEVO_ATOM_TYPE_VOIDPTR	65
#define VEVO_ATOM_TYPE_INT	1
#define VEVO_ATOM_TYPE_DOUBLE	2
#define VEVO_ATOM_TYPE_STRING	4
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


#define VEVO_PORT_POOL			1041
#define VEVO_ANONYMOUS_PORT		-1

#define VEVO_PROPERTY_READONLY (1<<1)
#define VEVO_PROPERTY_SOFTREF  (1<<2)
#define	VEVO_PROPERTY_PROTECTED (1<<10)

#endif 
