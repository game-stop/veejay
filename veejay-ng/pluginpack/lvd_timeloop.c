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
// 40 MB of memory for full pal 4 seconds delay
#define MAXLEN (4*25)
#define PLAY 0
#define REC 1

typedef struct
{
	uint8_t *tmpy;
	uint8_t *tmpu;
	uint8_t *tmpv;
	int	empty;
	int	nframes;
	int	pframes;
	int	state;
	
} bg_t;

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w=0;
        int h=0;

	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );
     
	bg_t *b = (bg_t*) livido_malloc( sizeof( bg_t ));
	livido_memset( b,0,sizeof(bg_t));
	b->tmpy = (uint8_t*) livido_malloc(sizeof(uint8_t) * w * h * MAXLEN);
	b->tmpu = (uint8_t*) livido_malloc(sizeof(uint8_t) * w * h * MAXLEN);
	b->tmpv = (uint8_t*) livido_malloc(sizeof(uint8_t) * w * h * MAXLEN);
	b->state = REC;
	int error = livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &b );
        
        return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	bg_t *b = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
                        0, &b );
#ifdef STRICT_CHECKING
        assert( b != NULL );
#endif
	livido_free( b->tmpy );
	livido_free( b->tmpu );
	livido_free( b->tmpv );
	livido_free( b );
	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	int palette[3];
	int w[3];
	int h[3];
	int i,j;

	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[2],&h[2], O,&palette[2] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

#ifdef STRICT_CHECKING
	assert( A[0] != NULL );
	assert( A[1] != NULL );
	assert( A[2] != NULL );
	assert( O[0] != NULL );
	assert( O[1] != NULL );
	assert( O[2] != NULL );
	assert( O[3] != NULL );

#endif
	bg_t *b = NULL;
        error     = livido_property_get( my_instance, "PLUGIN_private", 0, &b );
#ifdef STRICT_CHECKING
	assert( error == NULL );
#endif

        double activity  = lvd_extract_param_number(
                                my_instance,
                                "in_parameters",
                                0 );
        double activity_thres  = lvd_extract_param_number(
                                my_instance,
                                "in_parameters",
                                1 );

	const int len = w[0] * h[0];
	const int wid = w[0];
	const int hei = h[0];

	if( activity > activity_thres )
		b->state = PLAY;
	else
		b->state = REC;

	if( b->state == REC)
	{
		int offset = len * b->nframes;
		livido_memcpy( b->tmpy + offset , A[0], len );
		livido_memcpy( b->tmpu + offset , A[1], len );
		livido_memcpy( b->tmpv + offset,  A[2], len );
		b->nframes ++;
		if( b->nframes >= MAXLEN )
			b->nframes = 0;
		livido_memcpy( O[0], A[0], len );
		livido_memcpy( O[1], A[1], len );
		livido_memcpy( O[2], A[2], len );
	}	
	else
	{
		int offset = len * b->pframes;
		
		int roffset = len * b->nframes;

		livido_memcpy( b->tmpy + roffset , A[0], len );
		livido_memcpy( b->tmpu + roffset , A[1], len );
		livido_memcpy( b->tmpv + roffset,  A[2], len );
		b->nframes ++;
		if( b->nframes >= MAXLEN )
			b->nframes = 0;

		
		livido_memcpy( O[0], b->tmpy + offset, len );
		livido_memcpy( O[1], b->tmpu + offset, len );
		livido_memcpy( O[2], b->tmpv + offset, len );
		b->pframes ++;
		if( b->pframes >= b->nframes )
			b->pframes = 0;
	}


	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);
	livido_port_t *port = NULL;
	livido_port_t *in_params[4];
	livido_port_t *in_chans[2];
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

		livido_set_string_value( port, "name", "Activity Timeloop");	
		livido_set_string_value( port, "description", "Band loop delay with activity levels");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
			LIVIDO_PALETTE_YUV444P,
               		0
	};

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[0];

                livido_set_string_value(port, "name", "Activity level" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.0 );
                livido_set_string_value( port, "description" ,"Activity level");
		
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Activity Threshold" );
                livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.5 );

		livido_set_string_value( port, "description" ,"Activity Threshold");
	
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
	
	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "in_parameter_templates",2, in_params );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
