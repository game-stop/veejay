/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */


#define IS_LIVIDO_PLUGIN 
#include 	<livido.h>
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

typedef struct
{
	int *s;
	int  n;
	int  c;
} sinoids_t;

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w= 0;
	int h= 0;
	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );
     
	sinoids_t *s = (sinoids_t*) livido_malloc(sizeof(sinoids_t));
	s->s = (int*) livido_malloc(sizeof(int) * w );
	s->n = 0;
	s->c = 0;

	int error = livido_property_set( my_instance, "PLUGIN_private", 
                        LIVIDO_ATOM_TYPE_VOIDPTR, 1, &s );
        
        return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	sinoids_t *s = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
                        0, &s );
#ifdef STRICT_CHECKING
        assert( s != NULL );
#endif
        livido_free( s->s );
	livido_free( s );

	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	int x,y;
	int palette[3];
	int w[3];
	int h[3];

	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[1],&h[1], O,&palette[1] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

#ifdef STRICT_CHECKING
	assert( w[0] == w[1] );
	assert( h[0] == h[1] );
	assert( palette[0] == palette[1] );
	assert( A[0] != NULL );
	assert( A[1] != NULL );
	assert( A[2] != NULL );
	assert( O[0] != NULL );
	assert( O[1] != NULL );
	assert( O[2] != NULL );
#endif

	len = w[0] * h[0];
	
	double  sinoids = lvd_extract_param_number( my_instance,
			"in_parameters", 0 );
	double	c	= lvd_extract_param_number( my_instance,
			"in_parameters", 1 );
	int     ns = (int)(w[0] * sinoids);
	int    constant = (int)( c * 100.0 );
	sinoids_t *s = NULL;
	error = livido_property_get( my_instance, "PLUGIN_private",0, &s );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	double wid = (double) w[0];
	if( s->n != ns || s->c != constant)
	{
		s->n = ns;
		s->c = constant;
		for ( i = 0; i < w[0]; i ++ )
		{
			double deg = (double)i / wid;
			s->s[i] = (int)
	  			(sin( deg * 2 * M_PI ) * ns );
			s->s[i] *= constant;
		}
	}
				
	for( y = 0 ; y < len ; y += w[0] )
	{
		for( x = 0; x < w[0]; x ++ ) 
		{
			int pos = s->s[x] + y + x;
			if( pos >= 0 && pos < len )
			{
				O[0][y+x] = 
					A[0][pos];
				O[1][y+x] = 
					A[1][pos];
				O[2][y+x] =
					A[2][pos];
			}
		}
	}
	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[4];
	livido_port_t *in_chans[1];
	livido_port_t *out_chans[1];
	livido_port_t *info = NULL;
	livido_port_t *filter = NULL;

	info = livido_port_new( LIVIDO_PORT_TYPE_PLUGIN_INFO );
	port = info;

		livido_set_string_value( port, "maintainer", "Niels");
		livido_set_string_value( port, "version","1");
	
	filter = livido_port_new( LIVIDO_PORT_TYPE_FILTER_CLASS );
	livido_set_int_value( filter, "api_version", LIVIDO_API_VERSION );
	livido_set_voidptr_value( filter, "deinit_func", &deinit_instance );
	livido_set_voidptr_value( filter, "init_func", &init_instance );
	livido_set_voidptr_value( filter, "process_func", &process_instance );
	port = filter;

		livido_set_string_value( port, "name", "Sinoids");	
		livido_set_string_value( port, "description", "Sinoids, classic FX");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
			LIVIDO_PALETTE_YUV444P,
               		0
	};
	
        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
            
                livido_set_string_value( port, "name", "Channel A");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[0];

                livido_set_string_value(port, "name", "Sinoids" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.5 );
                livido_set_string_value( port, "description" ,"Sinoids");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Const" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.5 );
                livido_set_string_value( port, "description" ,"Const");

	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",2, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
