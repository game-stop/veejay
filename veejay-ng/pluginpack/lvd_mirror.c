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


static void mirror_multi_dr(uint8_t *A[4], uint8_t *O[4], int width, int height)
{

    unsigned int x, y;
    unsigned int hlen = height / 2;
    unsigned int vlen = width / 2;
    unsigned int yi1, yi2;
    uint8_t p, cr, cb;

    for (y = hlen; y < height; y++) {
	yi1 = y * width;
	yi2 = (height - y - 1) * width;
	for (x = vlen; x < width; x++) {
	    p = A[0][yi1 + x];
	    O[0][yi1 + x + (width - x - 1)] = p;
	    O[0][yi2 + x] = p;
	    O[0][yi2 + (width - x - 1)] = p;
	    cb = A[1][yi1 + x];
	    cr = A[2][yi1 + x];
	    O[1][yi1 + x + (width - x - 1)] = cb;
	    O[1][yi2 + x] = cb;
	    O[1][yi2 + (width - x - 1)] = cb;
	    O[2][yi1 + x + (width - x - 1)] = cr;
	    O[2][yi2 + x] = cr;
	    O[2][yi2 + (width - x - 1)] = cr;
	}
    }
}
static void mirror_multi_u(uint8_t *A[4], uint8_t *O[4], int width, int height)
{
    unsigned int x, y;

    unsigned int yi1, yi2;
    unsigned int hlen = height / 2;

    uint8_t p, cb, cr;

    for (y = 0; y < hlen; y++) {
	yi1 = y * width;
	yi2 = (height - y - 1) * width;
	for (x = 0; x < width; x++) {
	    O[0][yi2 + x] = A[0][yi1 + x];
	    O[1][yi2 + x] = A[1][yi1 + x];
	    O[2][yi2 + x] = A[2][yi1 + x];
	    O[0][yi1 + x ] = A[0][yi1 + x];
	    O[1][yi1 + x ] = A[1][yi1 + x];
	    O[2][yi1 + x ] = A[2][yi1 + x];
	     
   	 }
	}
}

static void mirror_multi_l(uint8_t *A[4] , uint8_t *O[4], int width, int height)
{
    unsigned int x, y;

    unsigned int yi;
    unsigned int vlen = width / 2;
    for (y = 0; y < height; y++) {
	yi = y * width;
	for (x = vlen; x < width; x++) {
	    O[0][yi + (width - x - 1)] = A[0][yi + x];
	    O[1][yi + (width - x - 1)] = A[1][yi + x];
	    O[2][yi + (width - x - 1)] = A[2][yi + x];
	    O[0][yi + x] 	       = A[0][yi + x];
	    O[1][yi + x] 	       = A[1][yi + x];
	    O[2][yi + x] 	       = A[2][yi + x];
		    
	}
	
    }
}

static void mirror_multi_r(uint8_t *A[4], uint8_t *O[4], int width, int height)
{
    unsigned int x, y;
    unsigned int yi;
	unsigned int vlen = width / 2;
    for (y = 0; y < height; y++) {
	yi = y * width;
	for (x = 0; x < vlen; x++) {
	    O[0][yi + (width - x)] = A[0][yi + x + vlen];
	    O[1][yi + (width - x)] = A[1][yi + x + vlen];
	    O[2][yi + (width - x)] = A[2][yi + x + vlen];
	    O[0][yi + x] 	       = A[0][yi + x + vlen];
	    O[1][yi + x] 	       = A[1][yi + x + vlen];
	    O[2][yi + x] 	       = A[2][yi + x + vlen];
	     
	}
    }
}



static void mirror_multi_d(uint8_t *A[4], uint8_t *O[4], int width, int height)
{
    unsigned int x, y;
    unsigned int y1 = 0;
    unsigned int y2 = 0;
    unsigned int hlen = height / 2;
    uint8_t p, cb, cr;

    for (y = hlen; y < height; y++) {
	y1 = y * width;
	y2 = (height - y) * width;

	for (x = 0; x < width; x++) {
	    O[0][y2 + x] = A[0][y1 + x];
	    O[1][y2 + x] = A[1][y1 + x];
	    O[2][y2 + x] = A[2][y1 + x];
	    O[0][y1 + x] = A[0][y1 + x];
	    O[1][y1 + x] = A[1][y1 + x];
	    O[2][y1 + x] = A[2][y1 + x];
	    
	}
    }
}

/*
static void mirror_multi_ur(uint8_t *A[4] , uint8_t *O[4], int width, int height)
{
    unsigned int x, y;

    unsigned int yi, yi2;
    unsigned int vlen = width / 2;
    unsigned int hlen = width / 2;
    uint8_t p, cb, cr;

    for (y = hlen; y < height; y++) {
	yi = y * width;
	yi2 = (height - y - 1) * width;
	for (x = 0; x < vlen; x++) {
    		O[0][yi2 + x] = A[0][yi1 + x];
		O[1][yi2 + x] = A[1][yi1 + x];
		O[2][yi2 + x] = A[2][yi1 + x];
		O[0][yi1 + x ] = A[0][yi1 + x];
		O[1][yi1 + x ] = A[1][yi1 + x];
		O[2][yi1 + x ] = A[2][yi1 + x];
	}
	for( x = vlen; x < wid; x ++ )
	{
	 	O[0][yi2 + x] = A[0][yi1 + x - vlen];
	   	O[1][yi2 + x] = A[1][yi1 + x - vlen];
	  	O[2][yi2 + x] = A[2][yi1 + x - vlen];
	    	O[0][yi + x]  = A[0][yi1 + x - vlen];
	    	O[1][yi + x]  = A[1][yi1 + x - vlen];
	    	O[2][yi + x]  = A[2][yi1 + x - vlen];
	}
    }
}

*/
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

	int uv_len = lvd_uv_plane_len( palette[0],w[0],h[0] );
	len = w[0] * h[0];
	const int wid = w[0];
	const int hei = h[1];

        int type = lvd_extract_param_index(
                                my_instance,
                                "in_parameters",
                                0 );
  	switch (type)
	{
//		case 0:
//			mirror_multi_dr(A,O, wid, hei);
//		break;
  //  		case 1:
//			mirror_multi_ur(A,O, wid, hei);
//		break;
    		case 2:
			mirror_multi_u(A,O, wid, hei);
		break;
    		case 3:
			mirror_multi_d(A,O, wid, hei);
		break;
		case 4:
			mirror_multi_l(A,O, wid, hei);
		break;
    		case 5:
			mirror_multi_r(A,O, wid, hei);
		break;
    	}

	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[4];
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

		livido_set_string_value( port, "name", "Mirror");	
		livido_set_string_value( port, "description", "Mirror");
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

                livido_set_string_value(port, "name", "Direction" );
                livido_set_string_value(port, "kind", "INDEX" );
                livido_set_int_value( port, "default",2);
		livido_set_int_value( port, "min", 2 );
		livido_set_int_value( port, "max", 5 );
                livido_set_string_value( port, "description" ,"Direction");

	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",1, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );


	livido_set_portptr_value(info, "filters", filter);
	return info;
}
