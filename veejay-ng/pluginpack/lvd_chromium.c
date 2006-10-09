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

livido_init_f	init_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

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
        int 		mode  = lvd_extract_param_index(
                                my_instance,
                                "in_parameters",
                                0 );

	int uv_len = lvd_uv_plane_len( palette[0],w[0],h[0] );
	len = w[0] * h[0];
	const int hei = h[0];
	const int wid = w[0];

	switch(mode)
        {
                case 0:
        	        for( i = 0; i < uv_len ; i++)
			{
			       O[1][i] = 0xff - A[1][i];
			       O[2][i] = A[2][i];
               		}
			for( i = 0; i < len ; i ++ )
				O[0][i] = A[0][i];
                break;
                case 1:
               	 	for( i = 0; i < uv_len ; i++ )
                	{
                        	O[2][i] = 0xff - A[2][i];
				O[1][i] = A[1][i];
                	}
			for( i = 0; i < len ; i ++ )
				O[0][i] = A[0][i];
                break;
                case 2:
                	for( i = 0; i < uv_len; i++)
               		{
                       		O[1][i] = 0xff - A[1][i];
                        	O[2][i] = 0xff - A[2][i];
                	}
			for( i = 0; i < len ; i ++ )
				O[0][i] = A[0][i];
                break;
                case 3:
                // swap cb/cr
               		for (i = 0; i < len ; i ++ )
                	{
                       	 	O[1][i] = A[2][i];
                        	O[2][i] = A[1][i];
                	}
			for( i = 0; i < len ; i ++ )
				O[0][i] = A[0][i];
	
                break;
        }

	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[1];
	livido_port_t *in_chans[3];
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

		livido_set_string_value( port, "name", "Chromium");	
		livido_set_string_value( port, "description", "Chromium");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
                      	LIVIDO_PALETTE_YUV420P,
                       	LIVIDO_PALETTE_YUV422P,
			LIVIDO_PALETTE_YUV444P,
               		0
	};
	
        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
            
                livido_set_string_value( port, "name", "Channel A");
           	livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[0];

                livido_set_string_value(port, "name", "Mode" );
                livido_set_string_value(port, "kind", "INDEX" );
                livido_set_int_value( port, "min", 0 );
                livido_set_int_value( port, "max", 3 );
                livido_set_int_value( port, "default", 0 );
                livido_set_string_value( port, "description" ,"Mode");

	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",1, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
	//livido_set_portptr_array( filter, "out_channel_templates", 0, NULL );


	livido_set_portptr_value(info, "filters", filter);
	return info;
}
