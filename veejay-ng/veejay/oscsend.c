/* veejay - Linux VeeJay
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
#include <config.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <libvevo/libvevo.h>
#include <lo/lo.h>

//@ client side implementation
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	lo_address addr;
	char *addr_str;
	char *port_str;
} oscclient_t;

void	*veejay_new_osc_sender( const char *addr, const char *port )
{
	oscclient_t *osc = (oscclient_t*) vj_malloc(sizeof(oscclient_t));
	osc->addr = lo_address_new( addr, port );

	osc->addr_str = strdup( addr );
	osc->port_str = port ? strdup( port ) : NULL;

	return (void*) osc;
}

void	veejay_free_osc_sender( void *dosc )
{
	oscclient_t *osc = (oscclient_t*) dosc;

	lo_address_free( osc->addr );
	if( osc->port_str )
		free( osc->port_str);
	free(osc->addr_str);
	free(osc);
	osc = NULL;
}

static	void veejay_add_arguments_ ( lo_message lmsg, const char *format, va_list ap )
{
	//http://liblo.sourceforge.net/docs/group__liblolowlevel.html#g31ac1e4c0ec6c61f665ce3f9bbdc53c3
	while( *format != 'x' )
	{
		switch(*format)
		{
			case 'i':
				lo_message_add_int32( lmsg, (int32_t) *(va_arg( ap, int*)));
				break;
			case 'h':
				lo_message_add_int64( lmsg, (int64_t) *(va_arg( ap, int64_t*)));
				break;
			case 'c':
				lo_message_add_string( lmsg, (char*) va_arg( ap, char*) );
				break;
			case 'f':
				lo_message_add_float( lmsg, (float) *(va_arg( ap, float*)));
				break;
			case 'd':
				lo_message_add_double( lmsg, (double) *(va_arg(ap, double*)));
				break;
			default:
#ifdef STRICT_CHECKING
				assert(0);
#endif
				break;
		}
		*format ++;
	}
}

int	veejay_send_osc( void *osc ,const char *msg, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message lmsg = lo_message_new();
	
	va_list ap;

	va_start( ap, format );
	veejay_add_arguments_( lmsg, format, ap );
	va_end(ap);

	int result = lo_send_message( c->addr, msg, lmsg );

	lo_message_free( lmsg );

	return result;
}

static	int	_vevo_get_int( void *port, const char *key, int n_elem, lo_message lmsg  )
{
	int32_t *values = NULL;
	if( n_elem == 0 )
		return VEVO_NO_ERROR;
	
	values = (int32_t*) vj_malloc(sizeof(int32_t) * n_elem );
	int i;

	int error;
	for( i = 0; i < n_elem; i ++ )
	{
		error = vevo_property_get( port, key, i, &(values[i]));
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		lo_message_add_int32( lmsg, values[i]);

	}
	return VEVO_NO_ERROR;
}
static	int	_vevo_get_dbl( void *port, const char *key, int n_elem, lo_message lmsg  )
{
	double *values = NULL;
	if( n_elem == 0 )
		return VEVO_NO_ERROR;
	
	values = (double*) vj_malloc(sizeof(double) * n_elem );
	int i;

	int error;
	for( i = 0; i < n_elem; i ++ )
	{
		error = vevo_property_get( port, key, i, &(values[i]));
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		lo_message_add_double( lmsg, values[i]);
	}
	return VEVO_NO_ERROR;
}

int	veejay_vevo_send_osc( void *osc, const char *msg, void *vevo_port )
{
	char  **keys = vevo_list_properties( vevo_port );
	int i;
#ifdef STRICT_CHECKING
	assert( keys != NULL );
#endif
	lo_message lmsg = lo_message_new();
	oscclient_t *c  = (oscclient_t*) osc;

	
	for ( i = 0; keys[i] != NULL; i ++ )
	{
		char *format = vevo_format_kind( vevo_port, keys[i] );
#ifdef STRICT_CHECKING
		assert( format != NULL );
#endif

		int n_elems = vevo_property_num_elements( vevo_port, keys[i] );

		while( *format )
		{
			switch(*format)
			{
				case 'd':
					_vevo_get_int( vevo_port, keys[i], n_elems, lmsg  );
					break;
				case 'g':
					_vevo_get_dbl( vevo_port, keys[i], n_elems, lmsg );
					break;
				default:
#ifdef STRICT_CHECKING
					assert(0);
#endif
					break;
			}
			*format++;
		}

		free(keys[i]);	
	}

	free(keys);

	int result = lo_send_message( c->addr, msg, lmsg );

	lo_message_free( lmsg );

	if( result == -1)
		return VEVO_ERROR_HARDWARE; //@ long live bogus error codes

	
	return VEVO_NO_ERROR;
}

