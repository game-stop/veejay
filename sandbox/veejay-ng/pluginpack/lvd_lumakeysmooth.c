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
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	uint8_t *B[4] = { NULL,NULL,NULL,NULL };
	int palette[3];
	int w[3];
	int h[3];
	int x,y;
	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error = lvd_extract_channel_values( my_instance, "in_channels", 1, &w[1], &h[1], B, &palette[1] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky


	
	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[2],&h[2], O,&palette[2] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

#ifdef STRICT_CHECKING
	assert( w[0] == w[1] );
	assert( h[0] == h[1] );
	assert( w[2] == w[1] );
	assert( h[2] == h[1] );
	assert( palette[0] == palette[1] );
	assert( palette[2] == palette[1] );
	assert( A[0] != NULL );
	assert( A[1] != NULL );
	assert( A[2] != NULL );
	assert( B[0] != NULL );
	assert( B[1] != NULL );
	assert( B[2] != NULL );
	assert( O[0] != NULL );
	assert( O[1] != NULL );
	assert( O[2] != NULL );
#endif

	const int uv_len = lvd_uv_plane_len( palette[0],w[0],h[0] );
	len = w[0] * h[0];

	uint8_t op0;
	uint8_t op1;

	double opacity = lvd_extract_param_number( my_instance, "in_parameters", 0 );	
	double min_t	= lvd_extract_param_number( my_instance, "in_parameters", 1 );	
	double max_t	=  lvd_extract_param_number( my_instance, "in_parameters", 2 );	
	double smoot    =  lvd_extract_param_number( my_instance, "in_parameters", 3 );
	const int wid = w[0];
	const int hei = h[0];

	const uint8_t min_threshold = (const uint8_t)(min_t * 255.0);
	const uint8_t max_threshold = (const uint8_t)(max_t * 255.0);
	const uint8_t diff_threshold = (const uint8_t)(smoot * 32.0);

	
	op1 = (uint8_t) (opacity * 255.0);
	op0 = 255 - op1;
	
	uint8_t tmp1,tmp2;
	
	for (x = 0; x < wid; x++)
	{
		tmp1 = A[0][x];
		tmp2 = B[0][x];
		if (tmp1 >= min_threshold && tmp2 <= max_threshold)
		{
		    O[0][x] = (op0 * tmp1 + op1 * tmp2) >> 8;
		    O[1][x] = (op0 * A[1][x] + op1 * B[1][x]) >> 8;
		    O[2][x] = (op0 * A[2][x] + op1 * B[2][x]) >> 8;
		}
		else
		{
			O[0][x] = A[0][x];
			O[1][x] = A[1][x];
			O[2][x] = A[2][x];
		}
    	}

    	for (y = wid; y < (len - wid); y += wid)
	{
		/* first pixel in column */
		tmp1 = A[0][y];
		tmp2 = B[0][y];
		if (tmp1 >= min_threshold && tmp1 <= max_threshold)
		{
	   		O[0][y] = (op0 * tmp1 + op1 * tmp2)  >> 8;
	   	 	O[1][y] = (op0 * A[1][y] + op1 * B[1][y]) >> 8;
	    		O[2][y] = (op0 * A[2][y] + op1 * B[2][y]) >> 8;
		}
		else
		{
			O[0][y] = A[0][y];
			O[1][y] = A[1][y];
			O[2][y] = A[2][y];
		}

		/* rest of pixels in column */
		for (x = 1; x < wid - 1; x++)
		{
			tmp1 = A[0][x + y];
	    		tmp2 = B[0][x + y];

			if( abs( tmp1 - min_threshold )  <= diff_threshold  || abs( tmp1 - max_threshold ) <= diff_threshold)
			{
				/* handle near threshold values differently */
				const uint8_t p1 = (	
				 A[0][y - wid + x - 1] +
				 A[0][y - wid + x + 1] +
				 A[0][y - wid + x] +
				 A[0][y + x] +
				 A[0][y + x - 1] +
				 A[0][y + x + 1] +
				 A[0][y + wid + x] +
				 A[0][y + wid + x + 1] +
				 A[0][y + wid + x - 1]
			    ) / 9;
				const uint8_t p2 = (	
				 B[0][y - wid + x - 1] +
				 B[0][y - wid + x + 1] +
				 B[0][y - wid + x] +
				 B[0][y + x] +
				 B[0][y + x - 1] +
				 B[0][y + x + 1] +
				 B[0][y + wid + x] +
				 B[0][y + wid + x + 1] +
				 B[0][y + wid  + x - 1]
			    ) / 9;

			O[0][x + y] = (op0 * p1 + op1 * p2)  >> 8;
			O[1][x + y] =
			    (op0 * A[1][x + y] + op1 * B[1][x + y])  >> 8;
			O[2][x + y] =
			    (op0 * A[2][x + y] + op1 * B[2][x + y])  >> 8;
			}
	    		else
   			{
				if (tmp1 >= min_threshold && tmp1 <= max_threshold)
				{
					O[0][x+y] = (op0 * tmp1 + op1 * tmp2)  >> 8;
		    			O[1][x+y] = (op0 * A[1][x + y] + op1 * B[1][x + y])  >> 8;
		  			O[2][x+y] = (op0 * A[2][x + y] + op1 * B[2][x + y])  >> 8;
				}
				else
				{
					O[0][x+y] = A[0][x+y];
					O[1][x+y] = A[1][x+y];
					O[2][x+y] = A[2][x+y];
				}
			}
		}
	}
   
	for (y = (wid-1); y < (len - wid - 1); y += wid)
	{
		tmp1 = A[0][y];
		tmp2 = B[0][y];
		if (tmp1 >= min_threshold && tmp1 <= max_threshold)
		{
	   		O[0][y] = (op0 * tmp1 + op1 * tmp2)  >> 8;
	   	 	O[1][y] = (op0 * A[1][y] + op1 * B[1][y]) >> 8;
	    		O[2][y] = (op0 * A[2][y] + op1 * B[2][y]) >> 8;
		}
		else
		{
			O[0][y] = A[0][y];
			O[1][y] = A[1][y];
			O[2][y] = A[2][y];
		}

	}

	/* last row */
	for (x = len - wid; x < len; x++)
	{
		tmp1 = A[0][x];
		tmp2 = B[0][x];

		if (tmp1 >= min_threshold && tmp1 <= max_threshold)
		{
		    O[0][x] = (op0 * tmp1 + op1 * tmp2)  >> 8;
		    O[1][x] = (op0 * A[1][x] + op1 * B[1][x])  >> 8;
		    O[2][x] = (op0 * A[2][x] + op1 * B[2][x])  >> 8;
    		}
		else
		{
			O[0][x] = A[0][x];
			O[1][x] = A[1][x];
			O[2][x] = A[2][x];
		}
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

		livido_set_string_value( port, "name", "Smooth Lumakey");	
		livido_set_string_value( port, "description", "Smooth Overlay by threshold");
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

                livido_set_string_value(port, "name", "Opacity" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Transparency");


	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Min Thres" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Minimum Threshold");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[2];

                livido_set_string_value(port, "name", "Max Thres" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.75 );
                livido_set_string_value( port, "description" ,"Maximum Threshold");
	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[3];

                livido_set_string_value(port, "name", "Smooth Thres" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.2 );
                livido_set_string_value( port, "description" ,"Smooth Threshold");




		
        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
            
                livido_set_string_value( port, "name", "Channel A");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	in_chans[1] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[1];
            
                livido_set_string_value( port, "name", "Channel B");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	livido_set_portptr_array( filter, "in_channel_templates", 2 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",4, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
