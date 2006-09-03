#ifndef OSC_SERVER_H
#define OSC_SERVER_H
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
#include <veejay/vims.h>
#include <veejay/veejay.h>
#include <lo/lo.h>
#include <ctype.h>
#include <vevosample/vevosample.h>
#include <libvjmem/vjmem.h>
//@ client side implementation
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include <veejay/oscservit.h>
typedef struct {
	int seq;
	void *caller;
	void *instance;
} plugin_data_t;
typedef struct
{
	lo_server_thread	st;
//	void		   *events;
} osc_recv_t;

void	error_handler( int num, const char *msg, const char *path )
{
	veejay_msg(0,"Liblo server error %d in path %s: %s",num,path,msg );
}
static	char	*osc_create_path( const char *base, const char *path )
{
	int n = strlen(base) + strlen(path) + 2;
	char *res = (char*) vj_malloc(n);
	bzero(res,n);
	sprintf(res,"%s/%s", base,path);
	return res;
}

static	char	*osc_fx_pair_str( const char *base, const char *key )
{
	int n = strlen(base) + strlen(key) + 2;
	char *res = (char*) vj_malloc(n);
	bzero(res,n);
	snprintf( res,n, "%s/%s", base,key);
	return res;
}


static char	*vevo_str_to_liblo( const char *format )
{
	int n = strlen(format);
	if( n <= 0 )
		return NULL;
	
	char *res = (char*) vj_malloc( n );
	int k = 0;
	bzero(res,n );
	while(*format)
	{
		if(*format=='d' || *format=='s')
			res[k++] = (*format == 'd' ? 'i' : 's');
		*format++;
	}
	return res;
}

static	char	make_valid_char_( const char c )
{
	const char *invalid = " #*,?[]{}";
	int k = 0;
	char o = '_';
	char r = c;
	for( k = 0; k < 8 ; k ++ )
	{
		if ( c == invalid[k] || isspace((unsigned char)c))
			return o;
		char l = tolower(c);
		if(l)
			r = l;
	}
	return r;
}

char	*veejay_valid_osc_name( const char *in )
{
	int n = strlen( in );
	int k;
	char *res = strdup( in );
	for( k = 0; k < n ; k ++ )
	{
		res[k] = make_valid_char_( in[k] );
	}
	return res;
}

void	veejay_osc_del_methods( void *user_data, void *osc_space,void *vevo_port, void *fx_instance )
{
	veejay_t *info = (veejay_t*) user_data;
	osc_recv_t *s = (osc_recv_t*) info->osc_server;

	char **keys = vevo_list_properties( osc_space );
	int i;
	int error;
	for( i = 0; keys[i] != NULL ; i ++ )
	{
		void *event_port = NULL;
		error = vevo_property_get( osc_space, keys[i],0,&event_port );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		
		void *ptr = NULL;
		error = vevo_property_get( event_port, "userdata",0, &ptr );
		
#ifdef STRICT_CHECKING
		assert(error == VEVO_NO_ERROR );
		assert( ptr != NULL );
#endif
	
		char *types = get_str_vevo( event_port, "format" );
		
		lo_server_thread_del_method( s->st, keys[i], types );
		
		free(keys[i]);
		if(types)
			free(types);
		free(ptr);
	}
	free(keys);

}
//@ fallback handler!
int	osc_rest_handler( const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *user_data )
{
	veejay_msg(0, "Incoming OSC Path '%s' not recognized", path );
	return 0;

}

//@ plugin handler!
int	osc_plugin_handler( const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *user_data )
{
	plugin_data_t *pd = (plugin_data_t*) user_data;
	veejay_t *info    = pd->caller;

	pthread_mutex_lock( &(info->vevo_mutex) );
	char *required_format = plug_get_osc_format( pd->instance, pd->seq );
	if( strcmp( required_format , types ) != 0 )
	{
		veejay_msg(0, "Plugin Path %s wrong format '%s' , need '%s'",
				path,types, required_format );
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}
	int n_elem = strlen(required_format);
#ifdef STRICT_CHECKING
	assert( n_elem == argc );
#endif
	int k;
	if( types[0] == 'i' )
	{
		int32_t *elements = (int32_t*) vj_malloc(sizeof(int32_t) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			elements[k] =  argv[k]->i32;
		plug_set_parameter( pd->instance, pd->seq, n_elem, (void*)elements );	
		free(elements);
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}
	else if( types[0] == 'd' )
	{
		double *elements = (double*) vj_malloc(sizeof(double) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			elements[k] =  argv[k]->d;
		plug_set_parameter( pd->instance, pd->seq, n_elem, (void*) elements );
		free(elements);
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}
	else if( types[0] == 's' )
	{
		char **strs = vj_malloc(sizeof(char*) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			strs[k] = strdup( (char*) &argv[k]->s );
		plug_set_parameter( pd->instance,pd->seq, n_elem, (void*) strs );
		for( k = 0; k < n_elem; k ++ )
			if(strs[k]) free(strs[k]);
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	} 

	pthread_mutex_unlock( &(info->vevo_mutex));
	return 1; //@ try another method
}

int	osc_veejay_handler( const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *user_data )
{
	plugin_data_t *pd = (plugin_data_t*) user_data;
	veejay_t *info    = pd->caller;
	pthread_mutex_lock( &(info->vevo_mutex) );
	// format of KEY in path!!	
	
	if( veejay_osc_property_calls_event( pd->instance,
				      path,
				      types,
				      argv ))
	{
		pthread_mutex_unlock( &(info->vevo_mutex));
		return 0;
	}

	pthread_mutex_unlock( &(info->vevo_mutex));
	return 1; //@ try another method
}

int	osc_sample_handler( const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *user_data )
{
	plugin_data_t *pd = (plugin_data_t*) user_data;
	veejay_t *info    = pd->caller;
	pthread_mutex_lock( &(info->vevo_mutex) );
	if( sample_osc_property_calls_event( pd->instance,
				      path,
				      types,
				      argv ))
	{
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}

	char *required_format = sample_property_format_osc( pd->instance, path );
	if(required_format == NULL )
	{
		veejay_msg(0, "Plugin Path %s wrong format '%s' , need '%s'",
				path,types, required_format );
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}
	if( strcmp( required_format , types ) != 0 )
	{
		veejay_msg(0, "Sample Path %s wrong format '%s' , need '%s'",
				path,types, required_format );
		pthread_mutex_unlock( &(info->vevo_mutex) );
		return 0;
	}

	int n_elem = strlen(required_format);
#ifdef STRICT_CHECKING
	assert( n_elem == argc );
#endif
	int k;
	free(required_format);

	
	if( types[0] == 'i' )
	{
		int32_t *elements = (int32_t*) vj_malloc(sizeof(int32_t) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			elements[k] =  argv[k]->i32;
		sample_set_property_from_path( pd->instance, path, (void*)elements );
		free(elements);
		pthread_mutex_unlock(&(info->vevo_mutex));
		return 0;
	}
	else if( types[0] == 'd' )
	{
		double *elements = (double*) vj_malloc(sizeof(double) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			elements[k] =  argv[k]->d;
		sample_set_property_from_path( pd->instance, path, (void*)elements );
		free(elements);
		pthread_mutex_unlock(&(info->vevo_mutex));
		return 0;

	}
	else if( types[0] == 's' )
	{
		char **strs = vj_malloc(sizeof(char*) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			strs[k] = strdup( (char*) &argv[k]->s );
		sample_set_property_from_path( pd->instance, path, (void*)strs );
		for( k = 0; k < n_elem; k ++ )
			if(strs[k]) free(strs[k]);
		pthread_mutex_unlock(&(info->vevo_mutex));
		return 0;
	}
	else if( types[0] == 'h' )
	{
		uint64_t *elements = vj_malloc(sizeof(uint64_t) * n_elem );
		for( k = 0; k < n_elem; k ++ )
			elements[k] = argv[k]->h;
		sample_set_property_from_path( pd->instance, path, (void*) elements );
		pthread_mutex_unlock(&(info->vevo_mutex));
		return 0;

	} 

	
	pthread_mutex_unlock( &(info->vevo_mutex));
	
	return 1;
}

void	veejay_osc_add_sample_generic_events(void *user_data, void *osc_port, void *vevo_port, const char *base, int fx)
{
	veejay_t *info = (veejay_t*) user_data;
	osc_recv_t *s = (osc_recv_t*) info->osc_server;
	osc_add_sample_generic_events( s->st, user_data,osc_port,vevo_port,base,fx );
}

void	veejay_osc_add_sample_nonstream_events(void *user_data, void *osc_port, void *vevo_port, const char *base)
{
	veejay_t *info = (veejay_t*) user_data;
	osc_recv_t *s = (osc_recv_t*) info->osc_server;
	osc_add_sample_nonstream_events( s->st, user_data,osc_port,vevo_port,base );
}

void	veejay_osc_namespace_events(void *user_data, const char *base)
{
	veejay_t *info = (veejay_t*) user_data;
	osc_recv_t *s = (osc_recv_t*) info->osc_server;
	osc_add_veejay_events( s->st, user_data, info->osc_namespace,base );
}

void	*veejay_new_osc_server( void *data, const char *port )
{
	osc_recv_t *s = (osc_recv_t*) vj_malloc(sizeof( osc_recv_t));
	s->st = lo_server_thread_new( port, error_handler );
	lo_server_thread_start( s->st );

//	lo_server_thread_add_method( s->st, NULL,NULL, osc_rest_handler,NULL );

	
	veejay_msg( 0, "OSC server ready at UDP port %d", lo_server_thread_get_port(s->st) );	
	return (void*) s;
}

static int	servit_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		vevo_event_f *func,
		int extra_token,
		void *ptemplate,
	        lo_method_handler method )
{
	veejay_t *info = (veejay_t*) userdata;
	osc_recv_t *s = (osc_recv_t*) info->osc_server;
#ifdef STRICT_CHECKING
	void *p = (void*) vevo_port_new( VEVO_ANONYMOUS_PORT,__FUNCTION__,__LINE__ );
#else
	void *p = (void*) vevo_port_new( VEVO_ANONYMOUS_PORT );
#endif
	int error;
	
	if( func )
	{
		error = vevo_property_set( p, "func", VEVO_ATOM_TYPE_VOIDPTR,1,&func );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	}

	if( fmt == NULL )
		error = vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING,0,NULL );
	else
		error = vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING,1, &fmt );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	if( descr == NULL )
		error = vevo_property_set(p, "description",VEVO_ATOM_TYPE_STRING,0, NULL );
	else
		error = vevo_property_set(p, "description", VEVO_ATOM_TYPE_STRING,1, &descr );
	
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	if( ptemplate )
	{
		error = vevo_property_set( p, "parent", VEVO_ATOM_TYPE_VOIDPTR,1,&ptemplate );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif

	}
	
/*	if( extra_token >= 0)
	{
		error = vevo_property_set(p, "ref", VEVO_ATOM_TYPE_INT,1, &extra_token );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	}*/
	
	if( args )
	{
		int i;
		for( i = 0;i < 4; i ++ )
		{
			if(args[i])
			{
				char index[10];
				sprintf(index,"help_%d",i);
				error = vevo_property_set( p, index, VEVO_ATOM_TYPE_STRING,1,&(args[i]) );
#ifdef 	STRICT_CHECKING
				assert( error == VEVO_NO_ERROR );
#endif
			}
		}
	}

	plugin_data_t *pd = (plugin_data_t*) vj_malloc(sizeof(plugin_data_t));
	pd->seq = extra_token;
	pd->caller = userdata;
	pd->instance = instance;
	error = vevo_property_set( p,
				"userdata",
				VEVO_ATOM_TYPE_VOIDPTR,
				1,
				&pd );

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	char *tmp_path = NULL;
	if( base)
		tmp_path = osc_create_path( base, key );     
	else
		tmp_path = strdup(key);

	char *my_path  = veejay_valid_osc_name( tmp_path );
	
	error = vevo_property_set( osc_space,
				   my_path,
				   VEVO_ATOM_TYPE_PORTPTR,
				   1,
				   &p );

#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	lo_server_thread_add_method(
				s->st,
				my_path,
				fmt,
				method,
				(void*) pd );

	free(my_path);
	free(tmp_path);
	
	return VEVO_NO_ERROR;
}

int	vevosample_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		vevo_event_f *func,
		int extra_token)
{
	      return servit_new_event( userdata,osc_space,instance,base,key,fmt,args,descr,func,extra_token,NULL,
			      osc_sample_handler ); 
}

int	veejay_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		vevo_event_f *func,
		int extra_token)
{
	      return servit_new_event( userdata,osc_space,instance,base,key,fmt,args,descr,func,extra_token,NULL,
			      osc_veejay_handler ); 
}

int	plugin_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		vevo_event_f *func,
		int extra_token,
		void *ptempl )
{
	      return servit_new_event( userdata,osc_space,instance,base,key,fmt,args,descr,func,extra_token,ptempl,
			      osc_plugin_handler ); 
}

void	veejay_free_osc_server( void *dosc )
{
	osc_recv_t *s = (osc_recv_t*) dosc;
	lo_server_thread_stop( s->st );
	lo_server_thread_free( s->st );
//	vevo_port_free( s->events );
	free(s);
	s = NULL;
}
#endif
