/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2011 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

/*
 * lifecycle:
 *
 *   HOST calls livido_setup
 *   
 *   HOST calls init_instance *once* before calling process_instance
 *   HOST calls deinit_instance when plugin is no longer needed.
 *   HOST calls process_instance 
 *
 *
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#include 	"lvd_common.h"

livido_init_f	init_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


int		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;

	//@ get parameter values
	int		r =  lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int		b =  lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int		g =  lvd_extract_param_index( my_instance,"in_parameters", 2 );

	uint8_t y,u,v;

	GIMP_rgb2yuv(r,g,b,y,u,v);

	livido_memset( O[0], y, len );
	livido_memset( O[1], u, uv_len );
	livido_memset( O[2], v, uv_len );

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
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
		livido_set_string_value( port, "name", "Solid Color Fill");	
		livido_set_string_value( port, "description", "Fill frame with a single color");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
           	LIVIDO_PALETTE_YUV420P,
           	LIVIDO_PALETTE_YUV422P,
            0
	};
	
	//@ setup output channel
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	    livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Red" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Color Red");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Blue" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Color Blue");


	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "Green" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Color Green");


	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
