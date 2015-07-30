/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"
#include 	"lvd_common.h"
#include	"lvdgmicglue.h"

livido_init_f	init_instance( livido_port_t *my_instance )
{
	Clvdgmic *gmic = lvdgmic_new(1);

	livido_property_set( my_instance, "PLUGIN_private",
			LIVIDO_ATOM_TYPE_VOIDPTR,1,&gmic );

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	Clvdgmic *gmic = NULL;
	livido_property_get( my_instance, "PLUGIN_private",0, &gmic);
	lvdgmic_delete(gmic);

	return LIVIDO_NO_ERROR;
}


livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	char cmd[256];
	int palette;
	int w;
	int h;
	
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; 
	Clvdgmic *gmic = NULL;
	livido_property_get( my_instance, "PLUGIN_private",0, &gmic);
	
    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;

	int		x_tiles = lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int		y_tiles = lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int		x_offset = lvd_extract_param_index( my_instance,"in_parameters", 2 );
	int		y_offset = lvd_extract_param_index( my_instance,"in_parameters", 3 );
	int		fade_start = lvd_extract_param_index( my_instance,"in_parameters", 4 );
	int		fade_end = lvd_extract_param_index( my_instance,"in_parameters", 5 );
	int		mirror = lvd_extract_param_index( my_instance,"in_parameters", 6 );
	int		size = lvd_extract_param_index( my_instance,"in_parameters", 7 );

	snprintf(cmd,sizeof(cmd),"-array_fade %d,%d,%d,%d,%d,%d,%d,%d",
			x_tiles,y_tiles,x_offset,y_offset,fade_start,fade_end,mirror,size);

	lvdgmic_push( gmic, w, h, palette, A, 0);

	lvdgmic_gmic( gmic, cmd );

	lvdgmic_pull( gmic, 0, O );

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[8];
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
		livido_set_string_value( port, "name", "G'MIC/GREYs Array fade");	
		livido_set_string_value( port, "description", "Create MxN array from selected images");
		livido_set_string_value( port, "author", "GREYC's Magic for Image Computing"); 
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
		LIVIDO_PALETTE_YUV422P,
		LIVIDO_PALETTE_YUV444P,
		LIVIDO_PALETTE_RGBFLOAT,
		LIVIDO_PALETTE_A8,
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
       		livido_set_int_array( port, "palette_list", 4, palettes0);
       		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "X-tiles" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1 );
		livido_set_int_value( port, "max", 10 );
		livido_set_int_value( port, "default", 2 );
		livido_set_string_value( port, "description" ,"X-tiles");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Y-tiles" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1 );
		livido_set_int_value( port, "max", 10 );
		livido_set_int_value( port, "default", 2 );
		livido_set_string_value( port, "description" ,"Y-tiles");


	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "X-offset" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 100 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"X-offset");

		
	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[3];

		livido_set_string_value(port, "name", "Y-offset" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0);
		livido_set_int_value( port, "max", 100 );
		livido_set_int_value( port, "default", 0);
		livido_set_string_value( port, "description" ,"Y-offset");

		
	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[4];

		livido_set_string_value(port, "name", "Fade start" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1);
		livido_set_int_value( port, "max", 100 );
		livido_set_int_value( port, "default", 80 );
		livido_set_string_value( port, "description" ,"Fade start");


		
	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[5];

		livido_set_string_value(port, "name", "Fade end" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1);
		livido_set_int_value( port, "max", 100 );
		livido_set_int_value( port, "default", 90 );
		livido_set_string_value( port, "description" ,"Fade end");

	in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[6];

		livido_set_string_value(port, "name", "Mirror" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1);
		livido_set_int_value( port, "max",3 );
		livido_set_int_value( port, "default", 1 );
		livido_set_string_value( port, "description" ,"Mirror");

	in_params[7] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[7];

		livido_set_string_value(port, "name", "Size" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1);
		livido_set_int_value( port, "max",3 );
		livido_set_int_value( port, "default", 1 );
		livido_set_string_value( port, "description" ,"Size");



	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",8, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
        livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
