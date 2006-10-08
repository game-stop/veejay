/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */

//@ this fx is slightly different from magic mirror

#define IS_LIVIDO_PLUGIN 
#include 	<livido.h>
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"


typedef struct
{
	double	*x;
	double	*y;
	int	*cx;
	int	*cy;
	double  last[2];
} magicmirror_t;

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w=0;
	int h=0;
	magicmirror_t *m = (magicmirror_t*) livido_malloc( sizeof(magicmirror_t));
	livido_memset( m, 0, sizeof( magicmirror_t ));

	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );
	
	m->x = (double*) livido_malloc( sizeof(double) * w );
	m->y = (double*) livido_malloc( sizeof(double) * h );

	m->cx = (int*) livido_malloc(sizeof(int) * w );
	m->cy = (int*) livido_malloc(sizeof(int) * h );
	
	livido_memset( m->x, 0, sizeof(double) * w );
	livido_memset( m->y, 0, sizeof(double) * h );
	livido_memset( m->cx,0, sizeof(int) * w );
	livido_memset( m->cy,0, sizeof(int) * h );


	int error = livido_property_set( my_instance, "PLUGIN_private", 
			LIVIDO_ATOM_TYPE_VOIDPTR, 1, &m );
	
	return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	magicmirror_t *m = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
			0, &m );
#ifdef STRICT_CHECKING
	assert( m != NULL );
#endif
	livido_free( m->x );
	livido_free( m->y );
	livido_free( m->cx );
	livido_free( m->cy );
	livido_free( m );

	livido_property_set( my_instance , "PLUGIN_private", 
			LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
	
	return error;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int x, y;
	unsigned int i,q;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette[3];
	int w[3];
	int h[3];

	double p[4];
	for( i = 0; i < 4; i ++ )
		p[i] = lvd_extract_param_number( my_instance, "in_parameters", i );

	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[1],&h[1], O,&palette[1] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	magicmirror_t	*mm = NULL;

	error     = livido_property_get( my_instance, "PLUGIN_private", 0, &mm );
	
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
	assert( mm != NULL );
#endif

	p[0] *= w[0];
	p[1] *= h[0];
	p[2] = p[2] * 0.001;
	p[3] = p[3] * 0.001;

	if( mm->last[0] != p[2] )
	{
		mm->last[0] = p[2];
		for( x = 0; x < w[0]; x ++ )
			fast_sin( mm->x[x], (double)(x * p[2]));
	

	}
	if( mm->last[1] != p[3] )
	{
		mm->last[1] = p[3];
		for( y = 0; y < h[0]; y ++ )
			fast_sin( mm->y[y], (double)(y * p[3]));
		}

	for( x = 0; x < w[0]; x ++ )
	{
		int n = x + mm->x[x] * p[0];
		while( n < 0 )
			n += w[0];
		mm->cx[x] = n;
	}		
	for( y = 0; y < h[0]; y ++ )
	{
		int v = y + mm->y[y] * p[1];
		while( v < 0)
			v += h[0];	
		mm->cy[y] = v;
	}

	for( y = 1; y < h[0] - 1; y ++ )
	{
		for ( x = 1; x < w[0] - 1; x ++ )
		{
			i = mm->cy[y] * w[0] + mm->cx[x];
			q = y * w[0] + x;
			O[0][q] = A[0][i];
			O[1][q] = A[1][i];
			O[2][q] = A[2][i];
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

		livido_set_string_value( port, "name", "Fun House Mirror");	
		livido_set_string_value( port, "description", "Look into the mirror just like in a fun house");
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
           	livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "X" );
		livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
		livido_set_double_value( port, "max", 0.1 );
		livido_set_double_value( port, "default", 0.01 );
		livido_set_string_value( port, "description" ,"Displace horizontally");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Y" );
		livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
		livido_set_double_value( port, "max", 0.1 );
		livido_set_double_value( port, "default", 0.01 );
		livido_set_string_value( port, "description" ,"Displace vertically");
	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "DegX" );
		livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
		livido_set_double_value( port, "max", 25.0 );
		livido_set_double_value( port, "default", 1.2 );
		livido_set_string_value( port, "description" ,"DegreeX");

	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[3];

		livido_set_string_value(port, "name", "DegY" );
		livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
		livido_set_double_value( port, "max", 25.0 );
		livido_set_double_value( port, "default", 1.3 );
		livido_set_string_value( port, "description" ,"DegreeY");

		
	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",4, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
