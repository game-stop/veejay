/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */
/*
  *  yuvcorrect_functions.c
  *  Common functions between yuvcorrect and yuvcorrect_tune
  *  Copyright (C) 2002 Xavier Biquard <xbiquard@free.fr>
  * 
  *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation; either version 2 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program; if not, write to the Free Software
  *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  */


#define IS_LIVIDO_PLUGIN 
#include 	<livido.h>
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

typedef struct
{
	uint8_t *c;
	double	fac[2];
	double  cen[2];
	double  angle;
	int	ran[2];
} correct_t;

livido_init_f	init_instance( livido_port_t *my_instance )
{
	correct_t *s = (correct_t*) livido_malloc(sizeof(correct_t));
	s->c = (int*) livido_malloc(sizeof(int) * 2 * 256 * 256 );
	s->fac[0] = 0.0;
	s->fac[1] = 0.0;
	s->cen[0] = 0.0;
	s->cen[1] = 0.0;
	s->angle  = 0.0;
	s->ran[0] = 0;
	s->ran[2] = 0;
	int error = livido_property_set( my_instance, "PLUGIN_private", 
                        LIVIDO_ATOM_TYPE_VOIDPTR, 1, &s );
        
        return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	correct_t *s = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
                        0, &s );
#ifdef STRICT_CHECKING
        assert( s != NULL );
#endif
        livido_free( s->c );
	livido_free( s );

	return LIVIDO_NO_ERROR;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
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
	int len    = w[0] * h[0];
	
	double  angle = lvd_extract_param_number( my_instance,
			"in_parameters", 0 );
	double	centeru	= lvd_extract_param_number( my_instance,
			"in_parameters", 1 );
	double	centerv	= lvd_extract_param_number( my_instance,
			"in_parameters", 2 );
	double	ufactor	= lvd_extract_param_number( my_instance,
			"in_parameters", 3 );
	double	vfactor	= lvd_extract_param_number( my_instance,
			"in_parameters", 4 );
	double  minu     = lvd_extract_param_number( my_instance,
			"in_parameters", 5 );
	double  maxu     = lvd_extract_param_number( my_instance,
			"in_parameters", 6 );

	const int uvmin = (const int)( 255.0 * minu );
	const int uvmax = (const int)( 255.0 * maxu );

	const double rad = angle / 180.0 * M_PI;
	
	correct_t *s = NULL;
	error = livido_property_get( my_instance, "PLUGIN_private",0, &s );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	double	si,co,fU,fV;
	sin_cos( si,co, rad );

	unsigned int iU,iV;

	uint8_t *table = s->c;
	uint8_t *mtable = table;
	if( centeru != s->cen[0] || centerv != s->cen[1] || ufactor != s->fac[0] || vfactor != s->fac[1] || rad !=
	 	s->angle || uvmin != s->ran[0] || uvmax != s->ran[1] )
	{
		s->cen[0] = centeru;
		s->cen[1] = centerv;
		s->fac[0] = ufactor;
		s->fac[1] = vfactor;
		s->angle = rad;
		s->ran[0] = uvmin;
		s->ran[1] = uvmax;
	
		for ( iU = 0; iU <= 255 ; iU ++ )
		{
			for( iV = 0; iV <= 255; iV ++ )
			{
				//U component  
				fU =  (((double) (iU - centeru ) * ufactor ) * co - 
				       ((double) (iV - centerv ) * vfactor ) * si) +
					 128.0;
	
				fU = (double) floor( 0.5 + fU );
	
					//clamp U values
				if( fU  < uvmin )
					fU = uvmin;
				else if( fU  > uvmax )
					fU = uvmax;
			
				//V component
				fV = (((float) ( iV - centeru) * vfactor ) * co + 
			 	      ((float) ( iU - centerv) * ufactor ) * si ) + 
					128.0;

				fV = (float) floor( 0.5 + fV );
	
				//clamp V values
				if(  fV < uvmin )
					fV = uvmin;
				else if(  fV > uvmax )
					fV = uvmax;

				//store in vector
				*(table)++ = (uint8_t) fU;
				*(table)++ = (uint8_t) fV;
			}
		}
	}

	livido_memcpy( O[0], A[0], len );

	uint32_t base;
	for( i = 0; i < uv_len; i ++ )
	{
		base = ((((uint32_t) A[1][i] ) << 8 ) + A[2][i] ) << 1;
		O[1][i] = mtable[ base ++ ];
		O[2][i] = mtable[ base ];
	}
	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[8];
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

		livido_set_string_value( port, "name", "UV Correction");	
		livido_set_string_value( port, "description", "UV Correction");
		livido_set_string_value( port, "author", " Xavier Biquard,Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
			LIVIDO_PALETTE_YUV444P,
			LIVIDO_PALETTE_YUV420P,
			LIVIDO_PALETTE_YUV422P,
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

                livido_set_string_value(port, "name", "Angle" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 360.0 );
                livido_set_double_value( port, "default", 15.5 );
                livido_set_string_value( port, "description" ,"UV rotation angle");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "U Rot" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 255.0 );
                livido_set_double_value( port, "default", 128.0 );
                livido_set_string_value( port, "description" ,"U rotate center");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[2];

                livido_set_string_value(port, "name", "V Rot" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 255.0 );
                livido_set_double_value( port, "default", 128.0 );
                livido_set_string_value( port, "description" ,"V rotate center");

	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[3];

                livido_set_string_value(port, "name", "U factor" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 10.0 );
                livido_set_double_value( port, "default", 0.5 );
                livido_set_string_value( port, "description" ,"U factor");

	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[4];

                livido_set_string_value(port, "name", "V factor" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 10.0 );
                livido_set_double_value( port, "default", 0.5 );
                livido_set_string_value( port, "description" ,"U factor");

	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[5];

                livido_set_string_value(port, "name", "UV min" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.0 );
                livido_set_string_value( port, "description" ,"UV min");

	in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[6];

                livido_set_string_value(port, "name", "UV max" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 1.0 );
                livido_set_string_value( port, "description" ,"UV max");





	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",7, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
