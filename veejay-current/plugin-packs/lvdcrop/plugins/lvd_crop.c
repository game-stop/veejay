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
#define RUP8(num)(((num)+8)&~8)

#include <libavutil/cpu.h>

typedef struct
{
	uint8_t *buf[4];
	int w;
	int h;
	int flags;
} lvd_crop_t;

int	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
        lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

	lvd_crop_t *c = (lvd_crop_t*) livido_malloc( sizeof(lvd_crop_t));
    if(!c) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	livido_memset(c,0,sizeof(lvd_crop_t));

	c->buf[0]     = (uint8_t*) livido_malloc( sizeof(uint8_t) * RUP8(w * h * 4));
	if(!c->buf[0]) {
        free(c);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    c->buf[1]     = c->buf[0] + (w*h);
	c->buf[2]     = c->buf[1] + (w*h);

	c->w		  = -1;
	c->h		  = -1; 

	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &c);

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	lvd_crop_t *crop = NULL;
	if( livido_property_get( my_instance, "PLUGIN_private", 0, &crop ) == LIVIDO_NO_ERROR ) {
	    if( crop ) {
		    if( crop->buf[0] ) {
		    	free(crop->buf[0]);
		    }
		    free(crop);
		    crop = NULL;
	    }		
        livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
    }

	return LIVIDO_NO_ERROR;
}

static int	lvd_zcrop_plane_z( uint8_t *D, int left, int right, int top, int bottom, int w, int h, uint8_t B, uint8_t alpha )
{
	int x,y;
	uint8_t *dst = D;
	
	for( y = 0; y < top; y ++ ) {
		livido_memset( dst, B, w );
		dst += w;
	}

	for( y = top; y < bottom; y ++ ) {
		for( x = 0; x < left; x ++ ) {
			*(dst++) = B;
		}

		for( x = left; x < right; x++ ) {
			*(dst++) = alpha;
		}

		for( x = right; x < w; x ++ ) {
			*(dst++) = B;
		}
	}

	for( y = bottom; y < h; y ++ ) {
		livido_memset( dst,B, w );
		dst += w;
	}

	return 1;
}


static int	lvd_zcrop_plane( uint8_t *D, uint8_t *S, int left, int right, int top, int bottom, int w, int h, uint8_t B )
{
	int x,y;
	uint8_t *src = S;
	uint8_t *dst = D;
	
	for( y = 0; y < top; y ++ ) {
		livido_memset( dst, B, w );
		dst += w;
		src += w;
	}

	for( y = top; y < bottom; y ++ ) {
		for( x = 0; x < left; x ++ ) {
			*(dst++) = B;
		}

		src += left;

		for( x = left; x < right; x++ ) {
			*(dst++) = *(src++);
		}

		for( x = right; x < w; x ++ ) {
			*(dst++) = B;
			src += 1;
		}
	}

	for( y = bottom; y < h; y ++ ) {
		livido_memset( dst,B, w );
		dst += w;
		src += w;
	}

	return 1;
}

int		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	lvd_crop_t *crop = NULL;
	livido_property_get( my_instance, "PLUGIN_private", 0, &crop );
	
	if( crop == NULL )
		return LIVIDO_ERROR_INTERNAL;

	int error  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;

	int	left = lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int	right = lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int	top = lvd_extract_param_index( my_instance, "in_parameters", 2 );
	int	bottom = lvd_extract_param_index( my_instance, "in_parameters", 3);
	int alpha = lvd_extract_param_index( my_instance, "in_parameters", 4);
	int invert_alpha = lvd_extract_param_index( my_instance, "in_parameters", 5 );

	int tmp_w = ( w - left - right);
	int tmp_h = h - top - bottom;

	if( tmp_w < 0 )
		tmp_w = 0;
	if( tmp_h < 0 )
		tmp_h = 0;

	if( tmp_w != crop->w || tmp_h != crop->h ) {
		crop->w = tmp_w;
		crop->h = tmp_h;
	}

	if( !lvd_zcrop_plane_z( O[3], left, right, top, bottom, w, h, (invert_alpha ?  0xff : 0), (invert_alpha ? 0: 0xff) ) )
		return LIVIDO_NO_ERROR;

	if(!alpha)
	{
		if( !lvd_zcrop_plane( O[0], A[0], left, right, top, bottom, w, h, 0 ) )
			return LIVIDO_NO_ERROR;
		if( !lvd_zcrop_plane( O[1], A[1], left, right, top, bottom, w, h, 128 ) )
			return LIVIDO_NO_ERROR;
		if(	!lvd_zcrop_plane( O[2], A[2], left, right, top, bottom, w, h, 128 ) )
			return LIVIDO_NO_ERROR;
	}

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[6];
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
		livido_set_string_value( port, "name", "Crop");	
		livido_set_string_value( port, "description", "Absolute crop");
		livido_set_string_value( port, "author", "Niels"); 
		
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

		livido_set_string_value(port, "name", "Left" );
		livido_set_string_value(port, "kind", "WIDTH" );
		livido_set_int_value(port, "default", 0 );
		livido_set_string_value( port, "description" ,"Left");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Right" );
		livido_set_string_value(port, "kind", "WIDTH" );
		livido_set_string_value( port, "description" ,"Right");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "Top" );
		livido_set_string_value(port, "kind", "HEIGHT" );
		livido_set_int_value(port, "default", 0 );

		livido_set_string_value( port, "description" ,"Top");

	
	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[3];

		livido_set_string_value(port, "name", "Bottom" );
		livido_set_string_value(port, "kind", "HEIGHT" );
		livido_set_string_value( port, "description" ,"Bottom");

	
	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[4];

		livido_set_string_value(port, "name", "Crop Alpha" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_string_value( port, "description" ,"Cropped area to alpha");
		livido_set_int_value(port, "min" ,0);
		livido_set_int_value(port, "max", 1);
		livido_set_int_value(port, "default", 0);

	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[5];

		livido_set_string_value(port, "name", "Invert Alpha" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_string_value( port, "description" ,"Invert");
		livido_set_int_value(port, "min" ,0);
		livido_set_int_value(port, "max", 1);
		livido_set_int_value(port, "default", 0);

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",6, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
    livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
