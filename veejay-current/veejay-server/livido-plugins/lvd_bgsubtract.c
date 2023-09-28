/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#include 	"lvd_common.h"

typedef struct
{
	uint8_t *bg;
	int      frame_no;
} bgsubtract_t;

int	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );
	
	bgsubtract_t *b = (bgsubtract_t*) livido_malloc(sizeof(bgsubtract_t));
    if(!b) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }
    b->bg = (uint8_t*) livido_malloc( sizeof(uint8_t) * (w * h) );
    if(!b->bg) {
        free(b);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &b);

	livido_memset( b->bg, 0, w * h );

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	void *ptr = NULL;
	if ( livido_property_get( my_instance, "PLUGIN_private", 0, &ptr ) == LIVIDO_NO_ERROR )
	{
		bgsubtract_t *b = (bgsubtract_t*) ptr;
		if( b->bg ) {
			livido_free( b->bg );
		}
		livido_free(b);
	}

	return LIVIDO_NO_ERROR;
}

void	lvd_vje_diff_plane( uint8_t *__restrict__ A, uint8_t *__restrict__ B, uint8_t *__restrict__ O, int threshold, const int len )
{
	unsigned int i;
	for( i = 0; i < len; i ++ ) {
		O[i] = abs( A[i] - B[i] );
		if( O[i] < threshold ) 
			O[i] = 0;
		else 
			O[i] = 0xff;
	}
}

void	lvd_avg_frame( uint8_t *__restrict__ A, uint8_t *__restrict__ O, const int len )
{
	unsigned int i;
	for( i = 0; i < len; i ++ )
	{
		O[i] = (O[i] + A[i]) >> 1;
	}
}

int	process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	int palette;
	int w;
	int h;
	bgsubtract_t *ptr = NULL;

	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

	error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;
	
	error = livido_property_get( my_instance, "PLUGIN_private", 0, &ptr );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_INTERNAL;

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;

	int		snap_bg    =  lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int		threshold  =  lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int		mode       =  lvd_extract_param_index( my_instance,"in_parameters", 2 );
    
	if( snap_bg ) {
		if( ptr->frame_no == 0 ) {
			livido_memcpy( ptr->bg, A[0], len );
		}
		else {
			/* average frames (assumes that background is static) */
			lvd_avg_frame( A[0], ptr->bg, len );
		}
		ptr->frame_no ++;

		/* while snapping, show bg frame */
		livido_memcpy( O[0], ptr->bg, len );
		livido_memset( O[1], 128, uv_len );
		livido_memset( O[2], 128, uv_len );
	}
	else {
		if( mode == 0 ) {
			lvd_vje_diff_plane( ptr->bg, A[0], O[0], threshold, len );
			livido_memset( O[1], 128, uv_len );
			livido_memset( O[2], 128, uv_len );
		}
		else {
			lvd_vje_diff_plane( ptr->bg, A[0], O[3], threshold, len );
		}

		ptr->frame_no = 0;
	}

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
	livido_port_t *in_chans[3];
	livido_port_t *out_chans[1];
	livido_port_t *info = NULL;
	livido_port_t *filter = NULL;

	//@ setup root node, plugin info
	info = livido_port_new( LIVIDO_PORT_TYPE_PLUGIN_INFO );
	port = info;

		livido_set_string_value( port, "maintainer", "Niels");
		livido_set_string_value( port, "version","1");
	
	filter = livido_port_new( LIVIDO_PORT_TYPE_FILTER_CLASS );
	livido_set_int_value( filter, "api_version", LIVIDO_API_VERSION );

	//@ setup function pointers
	livido_set_voidptr_value( filter, "deinit_func", &deinit_instance );
	livido_set_voidptr_value( filter, "init_func", &init_instance );
	livido_set_voidptr_value( filter, "process_func", &process_instance );
	port = filter;

	//@ meta information
		livido_set_string_value( port, "name", "Background Subtraction");	
		livido_set_string_value( port, "description", "Subtract the background");
		livido_set_string_value( port, "author", "Niels Elburg"); //ported to livido
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
		LIVIDO_PALETTE_YUV422P,
		LIVIDO_PALETTE_YUV444P,
            	0,
	};
	
	//@ setup output channel
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
		livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);

        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
        port = in_chans[0];
  		livido_set_string_value( port, "name", "Input Channel");
       		livido_set_int_array( port, "palette_list", 3, palettes0);
       		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Background" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Set to snap current frame as a background frame");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Threshold" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 40 );
		livido_set_string_value( port, "description" ,"Difference Threshold");


	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "Alpha" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 1 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Set difference frame as Alpha Channel");


	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
        livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
