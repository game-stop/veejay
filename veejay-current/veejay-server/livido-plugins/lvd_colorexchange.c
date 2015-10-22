/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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

livido_init_f	init_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


int	process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	int palette;
	int w;
	int h;
	
	int error = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;
	
	len = w * h;

	int	src_red    =  lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int	src_green  =  lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int	src_blue   =  lvd_extract_param_index( my_instance,"in_parameters", 2 );

	int	dst_red    =  lvd_extract_param_index( my_instance,"in_parameters", 3 );
	int	dst_green  =  lvd_extract_param_index( my_instance,"in_parameters", 4 );
	int	dst_blue   =  lvd_extract_param_index( my_instance,"in_parameters", 5 );

	int mode	   =  lvd_extract_param_index( my_instance,"in_parameters", 6 );

	int		sy,su,sv;
	GIMP_rgb2yuv( src_red, src_green, src_blue, sy,su,sv );

	int		dy,du,dv;
	GIMP_rgb2yuv( dst_red, dst_green, dst_blue, dy,du,dv );

	uint8_t *ch0 = A[0];
	uint8_t *ch1 = A[1];
	uint8_t *ch2 = A[2];

	uint8_t *o0 = O[0];
	uint8_t *o1 = O[1];
	uint8_t *o2 = O[2];

	const int minY = ( mode == 1 ? 16: 0 );
	const int maxY = ( mode == 1 ? 235: 255 );
	const int minUV= ( mode == 1 ? 16: 0 );
	const int maxUV= ( mode == 1 ? 240: 255 );

	int i;
	for( i = 0; i < len; i ++ )
	{
		o0[i] = CLAMP( ch0[i] + dy - sy,minY,maxY );
		o1[i] = CLAMP( ch1[i] + du - su,minUV,maxUV );
		o2[i] = CLAMP( ch2[i] + dv - sv,minUV,maxUV );

	}	


	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[7];
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
		livido_set_string_value( port, "name", "Color Exchange");	
		livido_set_string_value( port, "description", "Color Exchange in YUV space");
		livido_set_string_value( port, "author", "Niels Elburg"); //ported to livido
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
		LIVIDO_PALETTE_YUV444P,
        0,
	};
	
	//@ setup output channel
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
		livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);

        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
        port = in_chans[0];
  		livido_set_string_value( port, "name", "Input Channel");
       		livido_set_int_array( port, "palette_list", 2, palettes0);
       		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "From color (Red)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 112 );
		livido_set_string_value( port, "description" ,"Color Red");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "From color (Green)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 66 );
		livido_set_string_value( port, "description" ,"Color Green");


	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "From color (Blue)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 20 );
		livido_set_string_value( port, "description" ,"Speed");

		//@ setup parameters (INDEX type, 0-255) 
	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[3];

		livido_set_string_value(port, "name", "To color (Red)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 112 );
		livido_set_string_value( port, "description" ,"Color Red");

	
	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[4];

		livido_set_string_value(port, "name", "To color (Green)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 66 );
		livido_set_string_value( port, "description" ,"Color Green");


	
	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[5];

		livido_set_string_value(port, "name", "To color (Blue)" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 255 );
		livido_set_int_value( port, "default", 20 );
		livido_set_string_value( port, "description" ,"Blue");

	
	in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[6];

		livido_set_string_value(port, "name", "Clamp range" );
		livido_set_string_value(port, "kind", "SWITCH" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 1 );
		livido_set_int_value( port, "default",0 );
		livido_set_string_value( port, "description" ,"Clamp range");

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",7, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
        livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
