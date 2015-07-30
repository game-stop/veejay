/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C) 2015 Niels Elburg <nwelburg@gmail.com>
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

typedef struct
{
	uint8_t *buffer;
	uint8_t *map;
	uint8_t *planes[3];
	int current;
} hold_buffer_t;

#define RUP8(num)(((num)+8)&~8)


livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

	hold_buffer_t *hb = (hold_buffer_t*) livido_malloc( sizeof( hold_buffer_t ));
	hb->buffer = (uint8_t*) livido_malloc( sizeof(uint8_t) * RUP8( w * h * 3));
	hb->planes[0] = hb->buffer;
	hb->planes[1] = hb->planes[0] + RUP8(w*h);
	hb->planes[2] = hb->planes[1] + RUP8(w*h);
	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &hb);

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	void *ptr = NULL;
	if ( livido_property_get( my_instance, "PLUGIN_private", 0, &ptr ) == LIVIDO_NO_ERROR )
	{
		hold_buffer_t *hb = (hold_buffer_t*) ptr;
		if( hb->buffer ) livido_free(hb->buffer);
		livido_free(hb);
	}

	return LIVIDO_NO_ERROR;
}

static inline void stroboscope( 
		uint8_t *O1, uint8_t *O2, uint8_t *O0,
		const uint8_t *A1, uint8_t *A2,
		const uint8_t *B1, uint8_t *B2,
		const uint8_t *Y1,
		const uint8_t *Y2, 
		const int w, 
		const int h,
		const int feather,
		const int shift )
{
	const int len = (w*h);
	const int uv_len = (w*h) >> shift;
	unsigned int i;
	
	for( i = 0; i < uv_len; i ++ )
	{
		if( Y1[(i<<shift)] > Y2[(i<<shift)] ) {
			O1[i] = A1[i];
			O2[i] = A2[i];
		}
		else {
			O1[i] = B1[i];
			O2[i] = B2[i];
		}
	}

	for( i = 0; i < len; i ++ )
	{
		if( Y1[i] > Y2[i] ) {
			O0[i] = Y1[i];
		} else {
			O0[i] = Y2[i];
		}
	}


	// post process
	if( feather > 0 )
	{
		for( i = 0; i < uv_len; i ++ ) {
			if( abs( Y1[(i<<shift)] - Y2[(i<<shift)] ) < feather ) {
				O1[i] = ( A1[i] + B1[i] ) >> 1;
				O2[i] = ( A2[i] + B2[i] ) >> 1;
			}
		}

	}
}

static inline void fading_stroboscope( uint8_t *O, uint8_t *A, uint8_t *B, uint8_t *Op, const int len )
{
	unsigned int i;
	for( i = 0; i < len; i ++ )
	{
		O[i] = ( ( 0xff - Op[i]) * A[i] + (Op[i]) * B[i] ) >> 8;
	}
}

static inline void fading_stroboscopeUV( 
		uint8_t *O1, uint8_t *O2,
		const uint8_t *A1, uint8_t *A2,
		const uint8_t *B1, uint8_t *B2,
		const uint8_t *Y1,
		const uint8_t *Y2, 
		const int w, 
		const int h,
		const int feather,
		const int shift )
{
	const int uv_len = (w*h) >> shift;
	unsigned int i;
	
	for( i = 0; i < uv_len; i ++ )
	{
		if( Y1[(i<<shift)] > Y2[(i<<shift)] ) {
			O1[i] = A1[i];
			O2[i] = A2[i];
		}
		else {
			O1[i] = B1[i];
			O2[i] = B2[i];
		}
	}

	if( feather > 0 )
	{
		for( i = 0; i < uv_len; i ++ ) {
			if( abs( Y1[(i<<shift)] - Y2[(i<<shift)] ) < feather ) {
				O1[i] = ( A1[i] + B1[i] ) >> 1;
				O2[i] = ( A2[i] + B2[i] ) >> 1;
			}
		}
	}
}

int		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	int shift = 1;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	hold_buffer_t *ptr = NULL;

	error = livido_property_get( my_instance, "PLUGIN_private", 0, &ptr );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_INTERNAL;

	error = lvd_extract_channel_values( my_instance, "out_channels", 0, &w, &h, O, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_INTERNAL;

	error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_INTERNAL;

	len = w * h;

	int duration = lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int mode = lvd_extract_param_index( my_instance, "in_parameters", 1 );
	int feather = lvd_extract_param_index( my_instance, "in_parameters", 2 );
	
	if( palette == LIVIDO_PALETTE_YUV444P )
		shift = 0;

	int uv_len = lvd_uv_plane_len( palette,w,h );

	if( ptr->current == 0 ) {  
		livido_memcpy( ptr->planes[0], A[0], len );
		livido_memcpy( ptr->planes[1], A[1], uv_len );
		livido_memcpy( ptr->planes[2], A[2], uv_len );	
		ptr->current ++;
	}
	else 
	{
		if( mode == 0 ) {
			fading_stroboscope( O[0], ptr->planes[0], A[0], A[0],len );
			fading_stroboscopeUV( 
				O[1],O[2],
				ptr->planes[1],ptr->planes[2],
				A[1],A[2],
				ptr->planes[0],A[0],
				w,h, feather, shift );

		} else if ( mode == 1) {
			stroboscope( 
				O[1],O[2],O[0],
				ptr->planes[1],ptr->planes[2],
				A[1],A[2],
				ptr->planes[0],A[0],
				w,h, feather, shift );
			ptr->current ++;
		}
		else if( mode == 2 ) {
			fading_stroboscope( O[0], ptr->planes[0], A[0], A[0],len );
			fading_stroboscope( O[1], ptr->planes[1], A[1], A[1],uv_len );
			fading_stroboscope( O[2], ptr->planes[2], A[2], A[2],uv_len );
		}
		else if ( mode == 3 ) {
			fading_stroboscope( O[0], ptr->planes[0], A[0], ptr->planes[0], len );
			fading_stroboscope( O[1], ptr->planes[1], A[1], ptr->planes[1], uv_len );
			fading_stroboscope( O[2], ptr->planes[2], A[2], ptr->planes[2], uv_len );
		}

		livido_memcpy( ptr->planes[0], O[0], len );
		livido_memcpy( ptr->planes[1], O[1], len );
		livido_memcpy( ptr->planes[2], O[2], len );	
	}

	if( ptr->current > duration ) {
		ptr->current = 0;
	}
		
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)
{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
	livido_port_t *in_chans[1];
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

	livido_set_string_value( port, "name", "Stroboscope");	
	livido_set_string_value( port, "description", "Stroboscope FX");
	livido_set_string_value( port, "author", "Niels Elburg");
		
	livido_set_int_value( port, "flags", 0);
	livido_set_string_value( port, "license", "GPL2");
	livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
	    LIVIDO_PALETTE_YUV422P,
            0
	};
	
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	livido_set_string_value( port, "name", "Output Channel");
	livido_set_int_array( port, "palette_list", 4, palettes0);
	livido_set_int_value( port, "flags", 0);

	in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
	livido_set_string_value( port, "name", "Input Channel");
	livido_set_int_array( port, "palette_list", 4, palettes0);
	livido_set_int_value( port, "flags", 0);

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

	livido_set_string_value(port, "name", "Duration" );
	livido_set_string_value(port, "kind", "INDEX" );
	livido_set_int_value( port, "min", 0 );
	livido_set_int_value( port, "max", 1000 );
	livido_set_int_value( port, "default", 25 );
	livido_set_string_value( port, "description" ,"Duration in frames");
	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

	livido_set_string_value(port, "name", "Mode" );
	livido_set_string_value(port, "kind", "INDEX" );
	livido_set_int_value( port, "min", 0 );
	livido_set_int_value( port, "max", 3 );
	livido_set_int_value( port, "default", 1 );
	livido_set_string_value( port, "description" ,"Mode");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

	livido_set_string_value(port, "name", "Feather" );
	livido_set_string_value(port, "kind", "INDEX" );
	livido_set_int_value( port, "min", 0 );
	livido_set_int_value( port, "max", 255 );
	livido_set_int_value( port, "default", 0 );
	livido_set_string_value( port, "description" ,"Feather");


	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
	livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );
	livido_set_portptr_value(info, "filters", filter);
	return info;
}
