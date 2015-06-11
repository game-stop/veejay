/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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

typedef struct
{
	lo_address addr;
	char *addr_str;
	char *port_str;
	lo_bundle bundle;
	char *window;
} oscclient_t;

void	*veejay_new_osc_sender_uri( const char *uri )
{
	oscclient_t *osc = (oscclient_t*) vj_malloc(sizeof(oscclient_t));
	memset(osc,0,sizeof(oscclient_t));
	osc->addr = lo_address_new_from_url( uri );

	osc->addr_str = strdup(lo_address_get_hostname( osc->addr ));
	osc->port_str = strdup(lo_address_get_port ( osc->addr ));
	veejay_msg(0,"New OSC sender from uri '%s', Host %s, Port %s",
			uri, osc->addr_str, osc->port_str );
	return (void*) osc;
}

void	*veejay_new_osc_sender( const char *addr, const char *port )
{
	oscclient_t *osc = (oscclient_t*) vj_malloc(sizeof(oscclient_t));
	memset(osc,0,sizeof(oscclient_t));
	osc->addr = lo_address_new( addr, port );

	osc->addr_str = strdup( addr );
	osc->port_str = port ? strdup( port ) : NULL;
	veejay_msg(0,"New OSC sender Host %s, Port %s",
			osc->addr_str, osc->port_str );

	return (void*) osc;
}

void	veejay_free_osc_sender( void *dosc )
{
	oscclient_t *osc = (oscclient_t*) dosc;
	if(osc->addr)
		lo_address_free( osc->addr );
	if( osc->port_str )
		free( osc->port_str);
	if( osc->addr_str)
		free(osc->addr_str);
	if( osc->window )
		free(osc->window);
	free(osc);
	osc = NULL;
}

static	void veejay_add_arguments_ ( lo_message lmsg, const char *format, va_list ap )
{
	//http://liblo.sourceforge.net/docs/group__liblolowlevel.html#g31ac1e4c0ec6c61f665ce3f9bbdc53c3
	while( *format != 'x' && *format != '\0' )
	{
		switch(*format)
		{
			case 'i':
				lo_message_add_int32( lmsg, (int32_t) va_arg( ap, int));
				break;
			case 'h':
				lo_message_add_int64( lmsg, (int64_t) va_arg( ap, int64_t));
				break;
			case 's':
				{  char *str = (char*) va_arg(ap,char*);
				lo_message_add_string( lmsg, str ); }
				break;
			case 'd':
	//			double g =  (double) *(va_arg(ap, double*));
				{	double g = (double) va_arg(ap,double);
				lo_message_add_double( lmsg, g); }
				break;
			default:
				break;
		}
		*format ++;
	}
}


void	veejay_osc_set_window( void *osc, char *window )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(c->window) free(c->window);	
	c->window = strdup(window);
}

int	veejay_send_osc_strargs( void *osc, const char *msg, int n_str, char **strs )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message lmsg = lo_message_new();
	int i;
	for( i = 0; i < n_str; i ++ )
	{
		lo_message_add_string( lmsg, strs[i] );
	}
	int result = lo_send_message( c->addr, msg, lmsg );
	lo_message_free( lmsg );
	return result;
}

int	veejay_send_osc( void *osc ,const char *msg, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message lmsg = lo_message_new();
	
	va_list ap;

	if( format )
	{
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}

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
		lo_message_add_double( lmsg, values[i]);
	}
	return VEVO_NO_ERROR;
}

int	veejay_vevo_send_osc( void *osc, const char *msg, void *vevo_port )
{
	char  **keys = vevo_list_properties( vevo_port );
	int i;
	lo_message lmsg = lo_message_new();
	oscclient_t *c  = (oscclient_t*) osc;

	
	for ( i = 0; keys[i] != NULL; i ++ )
	{
		char *format = vevo_format_kind( vevo_port, keys[i] );
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

void	veejay_xbundle_add( void *osc, const char *window, const char *widget, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();

	if(!window)
		lo_message_add_string(lmsg, c->window );
	else
		lo_message_add_string(lmsg, window );
	lo_message_add_string(lmsg, widget );
	
	if( format )
	{
		va_list ap;
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}
	lo_bundle_add_message( c->bundle, "/update/tegdiw", lmsg );
}
void	veejay_bundle_add_blobs( void *osc, const char *msg, void *blub, void *blab, void *blib )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	lo_blob    blob = (lo_blob) blub;
	lo_blob    blob2 = (lo_blob) blab;
	lo_blob    blob3 = (lo_blob) blib;
	lo_message_add_blob( lmsg, blob );
	lo_message_add_blob( lmsg, blob2 );
	lo_message_add_blob( lmsg, blob3 );

	lo_bundle_add_message( c->bundle, msg, lmsg );
}

void	veejay_bundle_add_blob( void *osc, const char *msg, void *blub )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	lo_blob    blob = (lo_blob) blub;
	lo_message_add_blob( lmsg, blob );
	lo_bundle_add_message( c->bundle, msg, lmsg );
}

void	veejay_bundle_add( void *osc, const char *msg, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	
	lo_message_add_string(lmsg, c->window );
	lo_message_add_string(lmsg, msg );
	
	va_list ap;

	if( format )
	{
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}

	lo_bundle_add_message( c->bundle, "/update/widget", lmsg );
}

void	*veejay_message_new_linked_pulldown( void *osc, const char *str1,const char *str2, const char *str3,
	       			      const char *str4, const char *format , const char *tooltip)
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	lo_message_add_string( lmsg, str1 );
	lo_message_add_string( lmsg, str2 );
	lo_message_add_string( lmsg, str3 );
	lo_message_add_string( lmsg, str4 );
	lo_message_add_string( lmsg, format );
	lo_message_add_string( lmsg, tooltip );

	return (void*) lmsg;
}

void	*veejay_message_new_pulldown( void *osc, const char *str1,const char *str2, const char *id, const char *str3,
	       			      const char *str4, double dv, const char *tooltip	)
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	lo_message_add_string( lmsg, str1 );
	lo_message_add_string( lmsg, str2 );
	lo_message_add_string( lmsg, id );
	lo_message_add_string( lmsg, str3 );
	lo_message_add_string( lmsg, str4 );
	lo_message_add_double( lmsg, dv );
	lo_message_add_string( lmsg, tooltip );

	return (void*) lmsg;
}

void	*veejay_message_new_widget( void *osc, const char *str1,const char *str2, int n_names )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	lo_message_add_string( lmsg, str1 );
	lo_message_add_string( lmsg, str2 );
	lo_message_add_int32( lmsg, n_names );

	return (void*) lmsg;
}

void	veejay_message_widget_done( void *osc, void *msg )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message	*lmsg = (lo_message*) msg;
	lo_bundle_add_message( c->bundle, "/create/channels", lmsg );
}

void	veejay_message_add_argument( void *osc, void *msg, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message	*lmsg = (lo_message*) msg;

	va_list ap;
	va_start( ap, format );
	veejay_add_arguments_( lmsg, format, ap );
	va_end(ap);
}
void	veejay_message_pulldown_done( void *osc, void *msg )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message	*lmsg = (lo_message*) msg;
	lo_bundle_add_message( c->bundle, "/create/pulldown", lmsg );
}
void	veejay_message_pulldown_done_update( void *osc, void *msg )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message	*lmsg = (lo_message*) msg;
	lo_bundle_add_message( c->bundle, "/update/pulldown", lmsg );
}
void	veejay_message_linked_pulldown_done( void *osc, void *msg )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_message	*lmsg = (lo_message*) msg;
	lo_bundle_add_message( c->bundle, "/create/fxpulldown", lmsg );
}


void	veejay_bundle_plugin_add( void *osc, const char *window, const char *path, const char *format, void *value )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );

	lo_message lmsg = lo_message_new();

	lo_message_add_string(lmsg, window );
	lo_message_add_string(lmsg, path );

	int n_elem = strlen( format );
	int ival;
	char *str;
	double gval;
	uint64_t val;
	
	switch(*format)
	{
		case 's':
			str = (char*) *( (char*) value);
			lo_message_add_string( lmsg, str );
			break;
		case 'i':
			ival = (int) *( (int*) value );
			lo_message_add_int32( lmsg, ival );
			break;
		case 'd':
			gval = (double) *( (double*) value );
			lo_message_add_double( lmsg, gval );
			break;
		case 'h':
			val = (uint64_t) *( (uint64_t*) value );
			lo_message_add_int64( lmsg, (int64_t) val );
			break;
		default:
			lo_message_free( lmsg );
			return;
			break;
	}

	lo_bundle_add_message( c->bundle,"/update/widget", lmsg );
}


void	veejay_bundle_sample_fx_add( void *osc, int id, int entry, const char *word, const char *format, ... )
{
	char osc_path[256];
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}

	sprintf(osc_path, "/sample_%d/fx_%d/%s", id, entry, word );
	lo_message lmsg = lo_message_new();

	char realwin[128];
	sprintf(realwin, "%sFX%d", c->window, entry );
	lo_message_add_string(lmsg, realwin );

	lo_message_add_string(lmsg, osc_path );
	va_list ap;
	if( format )
	{
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}
	lo_bundle_add_message( c->bundle,"/update/widget", lmsg );

}
void	veejay_bundle_sample_add( void *osc, int id, const char *word, const char *format, ... )
{
	char osc_path[256];
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}

	sprintf(osc_path, "/sample_%d/%s", id, word );
	lo_message lmsg = lo_message_new();
	lo_message_add_string(lmsg, c->window );
	lo_message_add_string(lmsg, osc_path );

	va_list ap;
	if( format )
	{
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}

	
	lo_bundle_add_message( c->bundle, "/update/widget", lmsg );
}
void	veejay_bundle_sample_add_fx_atom( void *osc, int id,int entry, const char *word, const char *format, int type, void *value )
{
	char osc_path[256];
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}

	sprintf(osc_path, "/sample_%d/fx_%d/%s", id, entry,word );
	lo_message lmsg = lo_message_new();
	char realwin[128];
	sprintf(realwin, "%sFX%d", c->window, entry );
	lo_message_add_string(lmsg, realwin );
	lo_message_add_string(lmsg, osc_path );

	int ival;
	char *str;
	double gval;
	uint64_t val;
	switch(type)
	{
		case VEVO_ATOM_TYPE_STRING:
			str = (char*) *( (char*) value);
			lo_message_add_string( lmsg, str );
			break;
		case VEVO_ATOM_TYPE_BOOL:
		case VEVO_ATOM_TYPE_INT:
			ival = (int) *( (int*) value );
			lo_message_add_int32( lmsg, ival );
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			gval = (double) *( (double*) value );
			lo_message_add_double( lmsg, gval );
			break;
		case VEVO_ATOM_TYPE_UINT64:
			val = (uint64_t) *( (uint64_t*) value );
			lo_message_add_int64( lmsg, (int64_t) val );
			break;
		default:
			lo_message_free( lmsg );
			return;
			break;
	}

	lo_bundle_add_message( c->bundle,"/update/widget", lmsg );

}
void	veejay_bundle_add_atom( void *osc, const char *osc_path, const char *format, int type, void *value )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}

	lo_message lmsg = lo_message_new();
	lo_message_add_string( lmsg,c->window );
	lo_message_add_string( lmsg,osc_path );

	int ival;
	char *str;
	double gval;
	uint64_t val;
	switch(type)
	{
		case VEVO_ATOM_TYPE_STRING:
			str = (char*) *( (char*) value);
			lo_message_add_string( lmsg, str );
			break;
		case VEVO_ATOM_TYPE_BOOL:
		case VEVO_ATOM_TYPE_INT:
			ival = (int) *( (int*) value );
			lo_message_add_int32( lmsg, ival );
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			gval = (double) *( (double*) value );
			lo_message_add_double( lmsg, gval );
			break;
		case VEVO_ATOM_TYPE_UINT64:
			val = (uint64_t) *( (uint64_t*) value );
			lo_message_add_int64( lmsg, (int64_t) val );
			break;
		default:
			lo_message_free( lmsg );
			return;
			break;
	}

	lo_bundle_add_message( c->bundle, "/update/widget", lmsg );

}



void	veejay_bundle_sample_add_atom( void *osc, int id, const char *word, const char *format, int type, void *value )
{
	char osc_path[256];
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}

	sprintf(osc_path, "/sample_%d/%s", id, word );
	lo_message lmsg = lo_message_new();
	lo_message_add_string(lmsg, c->window );
	lo_message_add_string(lmsg, osc_path );

	int ival;
	char *str;
	double gval;
	uint64_t val;
	switch(type)
	{
		case VEVO_ATOM_TYPE_STRING:
			str = (char*) *( (char*) value);
			lo_message_add_string( lmsg, str );
			break;
		case VEVO_ATOM_TYPE_BOOL:
		case VEVO_ATOM_TYPE_INT:
			ival = (int) *( (int*) value );
			lo_message_add_int32( lmsg, ival );
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			gval = (double) *( (double*) value );
			lo_message_add_double( lmsg, gval );
			break;
		case VEVO_ATOM_TYPE_UINT64:
			val = (uint64_t) *( (uint64_t*) value );
			lo_message_add_int64( lmsg, (int64_t) val );
			break;
		default:
			lo_message_free( lmsg );
			return;
			break;
	}

	lo_bundle_add_message( c->bundle, "/update/widget", lmsg );

}

void	veejay_bundle_send( void *osc )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(c->bundle)
		lo_send_bundle( c->addr, c->bundle );
}

void 	veejay_bundle_destroy(void *osc )
{
	oscclient_t *c = (oscclient_t*) osc;
	lo_bundle_free(c->bundle);
	c->bundle = NULL;
}
void	veejay_ui_bundle_add( void *osc, const char *msg, const char *format, ... )
{
	oscclient_t *c = (oscclient_t*) osc;
	if(!c->bundle)
	{
		c->bundle = lo_bundle_new( LO_TT_IMMEDIATE );
	}
	lo_message lmsg = lo_message_new();
	
	va_list ap;

	if( format )
	{
		va_start( ap, format );
		veejay_add_arguments_( lmsg, format, ap );
		va_end(ap);
	}

	lo_bundle_add_message( c->bundle, msg, lmsg );
}
