/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 *
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


int		process_instance( livido_port_t *my_instance, double timecode )
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
	
    lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );

	int orientations = lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int starting_angle = lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int angle_range = lvd_extract_param_index(my_instance, "in_parameters",2);
	int stroke_length = lvd_extract_param_index(my_instance, "in_parameters",3);
	int contour_threshold = lvd_extract_param_index(my_instance, "in_parameters",4);
	int opacity = lvd_extract_param_index( my_instance,"in_parameters", 5);
	int bg_intensity = lvd_extract_param_index( my_instance,"in_parameters",6);
	int density = lvd_extract_param_index(my_instance, "in_parameters",7);
	int sharpness = lvd_extract_param_index(my_instance, "in_parameters",8);
	int anisotropy = lvd_extract_param_index(my_instance, "in_parameters",9);
	int smoothness = lvd_extract_param_index( my_instance,"in_parameters",10);
	int coherence = lvd_extract_param_index( my_instance,"in_parameters",11);
	int boost_stroke = lvd_extract_param_index(my_instance, "in_parameters",12);
	int curved_stroke = lvd_extract_param_index(my_instance, "in_parameters",13);

	snprintf(cmd,sizeof(cmd),"-sketchbw %d,%d,%d,%d,%d,%f,%d,%f,%f,%f,%f,%d,%d,%d",
		orientations,
		starting_angle,
		angle_range,
		stroke_length,
		contour_threshold,
 		(float)opacity/100.0f,
		bg_intensity,
		(float)density/100.0f,
		(float)sharpness/100.0f,
		(float)anisotropy/100.0f,
		(float)smoothness/100.0f,
		coherence,
		boost_stroke,
		curved_stroke		
		);

	lvdgmic_push( gmic, w, h, 0, A, 0);

	lvdgmic_gmic( gmic, cmd );

	lvdgmic_pull( gmic, 0, O );

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[14];
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
		livido_set_string_value( port, "name", "G'MIC/GREYs Sketch BW");	
		livido_set_string_value( port, "description", "Draw BW sketch");
		livido_set_string_value( port, "author", "GREYC's Magic for Image Computing"); 
		
		livido_set_int_value( port, "flags", LIVIDO_FILTER_NON_REALTIME);
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
		livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);

        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
        port = in_chans[0];
  		livido_set_string_value( port, "name", "Input Channel");
       		livido_set_int_array( port, "palette_list", 4, palettes0);
       		livido_set_int_value( port, "flags", 0);
	
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Orientations" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 1 );
		livido_set_int_value( port, "max", 16 );
		livido_set_int_value( port, "default", 2 );
		livido_set_string_value( port, "description" ,"Number of orientations");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Starting angle" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 180 );
		livido_set_int_value( port, "default", 45);
		livido_set_string_value( port, "description" ,"Starting angle");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "Angle range" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 180 );
		livido_set_int_value( port, "default", 180);
		livido_set_string_value( port, "description" ,"Angle range");

	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[3];

		livido_set_string_value(port, "name", "Stroke length" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1000 );
		livido_set_int_value( port, "default", 30);
		livido_set_string_value( port, "description" ,"Stroke length");

	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[4];

		livido_set_string_value(port, "name", "Contour threshold" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 3 );
		livido_set_int_value( port, "default", 1);
		livido_set_string_value( port, "description" ,"Contour threshold" );

	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[5];

		livido_set_string_value(port, "name", "Opacity" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 100 );
		livido_set_int_value( port, "default", 1);
		livido_set_string_value( port, "description" ,"Opacity" );

	in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[6];

		livido_set_string_value(port, "name", "Background intensity" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 2);
		livido_set_int_value( port, "default", 0);
		livido_set_string_value( port, "description" ,"Background intensity" );

	in_params[7] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[7];

		livido_set_string_value(port, "name", "Density" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 500);
		livido_set_int_value( port, "default", 60);
		livido_set_string_value( port, "description" ,"Density" );


	in_params[8] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[8];

		livido_set_string_value(port, "name", "Sharpness" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 150);
		livido_set_int_value( port, "default", 10);
		livido_set_string_value( port, "description" ,"Sharpness" );

	in_params[9] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[9];

		livido_set_string_value(port, "name", "Anisotropy" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1);
		livido_set_int_value( port, "default", 6);
		livido_set_string_value( port, "description" ,"Anisotropy" );

	in_params[10] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[10];

		livido_set_string_value(port, "name", "Smoothness" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1000);
		livido_set_int_value( port, "default", 25);
		livido_set_string_value( port, "description" ,"Anisotropy" );


	in_params[11] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[11];

		livido_set_string_value(port, "name", "Coherence" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 10);
		livido_set_int_value( port, "default", 1);
		livido_set_string_value( port, "description" ,"Coherence" );

	in_params[12] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[12];

		livido_set_string_value(port, "name", "Boost stroke" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1);
		livido_set_int_value( port, "default", 0);
		livido_set_string_value( port, "description" ,"Boost stroke" );

	in_params[13] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[13];

		livido_set_string_value(port, "name", "Curved stroke" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 1);
		livido_set_int_value( port, "default", 0);
		livido_set_string_value( port, "description" ,"Curved stroke" );

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",14, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
        livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
