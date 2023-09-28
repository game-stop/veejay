/* scale0tilt.c
 * Copyright (C) 2007 Richard Spindler (richard.spindler@gmail.com)
 * This file is a Frei0r plugin.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* lvd_scale0tilt.c
 * Copyright (C) 2015 Niels Elburg (nwelburg@gmail.com)
 *
 * Same as scale0tilt but as mixing effect with optional alpha blend 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#include <libavutil/cpu.h>
#include <math.h>
#include <gavl/gavl.h>

#define EPSILON 1e-6

typedef struct scale0tilt_instance {
	double cl, ct, cr, cb;
	double sx, sy;
	double tx, ty;
	int w, h;
	gavl_video_scaler_t* video_scaler;
	gavl_video_frame_t* frame_src;
	gavl_video_frame_t* frame_dst;
	int do_scale;
	gavl_video_format_t format_src;
	gavl_video_frame_t* temp;
	gavl_video_frame_t* temp_alpha;
} scale0tilt_instance_t;


static void update_scaler( scale0tilt_instance_t* inst )
{
	float dst_x, dst_y, dst_w, dst_h;
	float src_x, src_y, src_w, src_h;
        
	inst->do_scale = 1;
	src_x = inst->w * inst->cl;
	src_y = inst->h * inst->ct;
	src_w = inst->w * (1.0 - inst->cl - inst->cr );
	src_h = inst->h * (1.0 - inst->ct - inst->cb );

	dst_x = inst->w * inst->cl * inst->sx + inst->tx * inst->w;
	dst_y = inst->h * inst->ct * inst->sy + inst->ty * inst->h;
	dst_w = inst->w * (1.0 - inst->cl - inst->cr) * inst->sx;
	dst_h = inst->h * (1.0 - inst->ct - inst->cb) * inst->sy;

	if((dst_w < EPSILON) || (dst_h < EPSILON) || 
	   (src_w < EPSILON) || (src_h < EPSILON)) {
		inst->do_scale = 0;
		return;
	}

	if ( dst_x + dst_w > inst->w ) {
		src_w = src_w * ( (inst->w-dst_x) / dst_w );
		dst_w = inst->w - dst_x;
	}
	if ( dst_y + dst_h > inst->h ) {
		src_h = src_h * ( (inst->h-dst_y) / dst_h );
		dst_h = inst->h - dst_y;
	}
	if ( dst_x < 0 ) {
		src_x = src_x - dst_x * ( src_w / dst_w );
		src_w = src_w * ( (dst_w+dst_x) / dst_w );
		dst_w = dst_w + dst_x;
		dst_x = 0;
	}
	if ( dst_y < 0 ) {
		src_y = src_y - dst_y * ( src_h / dst_h );
		src_h = src_h * ( (dst_h+dst_y) / dst_h );
		dst_h = dst_h + dst_y;
		dst_y = 0;
	}

	if((dst_w < EPSILON) || (dst_h < EPSILON) ||
	   (src_w < EPSILON) || (src_h < EPSILON)) {
		inst->do_scale = 0;
		return;
	}

	gavl_video_options_t* options = gavl_video_scaler_get_options( inst->video_scaler );

	gavl_video_format_t format_dst;
	
	livido_memset(&format_dst, 0, sizeof(format_dst));
	format_dst.frame_width  = inst->w;
	format_dst.frame_height = inst->h;
	format_dst.image_width  = inst->w;
	format_dst.image_height = inst->h;
	format_dst.pixel_width = 1;
	format_dst.pixel_height = 1;
	format_dst.pixelformat = GAVL_YUVJ_444_P;

	gavl_rectangle_f_t src_rect;
	gavl_rectangle_i_t dst_rect;

	src_rect.x = src_x;
	src_rect.y = src_y;
	src_rect.w = src_w;
	src_rect.h = src_h;

	dst_rect.x = lroundf(dst_x);
	dst_rect.y = lroundf(dst_y);
	dst_rect.w = lroundf(dst_w);
	dst_rect.h = lroundf(dst_h);
	
	gavl_video_options_set_rectangles( options, &src_rect, &dst_rect );
	gavl_video_scaler_init( inst->video_scaler, &inst->format_src, &format_dst );
}

int	init_instance( livido_port_t *my_instance )
{
	int width = 0, height = 0;

	lvd_extract_dimensions( my_instance, "out_channels", &width, &height );

	scale0tilt_instance_t* inst = (scale0tilt_instance_t*)livido_malloc(sizeof(scale0tilt_instance_t));
    if(!inst) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	livido_memset( inst, 0, sizeof(scale0tilt_instance_t) );

	inst->w = width;
	inst->h = height;
	inst->sx = 1.0;
	inst->sy = 1.0;
	
	inst->format_src.frame_width  = inst->w;
	inst->format_src.frame_height = inst->h;
	inst->format_src.image_width  = inst->w;
	inst->format_src.image_height = inst->h;
	inst->format_src.pixel_width = 1;
	inst->format_src.pixel_height = 1;
	inst->format_src.pixelformat = GAVL_YUVJ_444_P;

	inst->video_scaler = gavl_video_scaler_create();
    if(!inst->video_scaler) {
        free(inst);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	inst->frame_src = gavl_video_frame_create( NULL );
    if(!inst->frame_src) {
        gavl_video_scaler_destroy(inst->video_scaler);
        free(inst);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	inst->frame_dst = gavl_video_frame_create( NULL );
    if(!inst->frame_dst) {
        gavl_video_scaler_destroy(inst->video_scaler);
        gavl_video_frame_destroy(inst->frame_src);
        free(inst);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	inst->frame_src->strides[0] = width;
	inst->frame_src->strides[1] = width;
	inst->frame_src->strides[2] = width;

	inst->frame_dst->strides[0] = width;
	inst->frame_dst->strides[1] = width;
	inst->frame_dst->strides[2] = width;

	update_scaler(inst);
	
	inst->temp = gavl_video_frame_create( &(inst->format_src) );
	inst->temp_alpha = gavl_video_frame_create( &(inst->format_src) );
	
	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &inst);

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	scale0tilt_instance_t *inst = NULL;
    if(livido_property_get( my_instance, "PLUGIN_private", 0, &inst ) == LIVIDO_NO_ERROR ) {
	    gavl_video_scaler_destroy(inst->video_scaler);
	    gavl_video_frame_null( inst->frame_src );
	    gavl_video_frame_destroy( inst->frame_src );
	    gavl_video_frame_null( inst->frame_dst );
	    gavl_video_frame_destroy( inst->frame_dst );
	    gavl_video_frame_null( inst->temp );
	    gavl_video_frame_destroy( inst->temp );
	    gavl_video_frame_null( inst->temp_alpha );
	    gavl_video_frame_destroy( inst->temp_alpha );

	    free(inst);
    
	    livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
    }

	return LIVIDO_NO_ERROR;
}

int		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *B[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4] = {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	scale0tilt_instance_t *inst = NULL;
	livido_property_get( my_instance, "PLUGIN_private", 0, &inst );
	
	if( inst == NULL )
		return LIVIDO_ERROR_INTERNAL;

	int error  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 1, &w, &h, B, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;

	double width = (double)w;
	double height= (double)h;

	inst->cl = lvd_extract_param_index( my_instance,"in_parameters", 0 ) / width;
	inst->cr = ((double) lvd_extract_param_index( my_instance,"in_parameters", 1 )) / width;
	inst->ct = ((double) lvd_extract_param_index( my_instance, "in_parameters", 2 )) / height;
	inst->cb = ((double) lvd_extract_param_index( my_instance, "in_parameters", 3)) / height;
	
	inst->sx = ( (double) lvd_extract_param_index( my_instance,"in_parameters", 4 )  / width) * 2.0;
	inst->sy = ( (double) lvd_extract_param_index( my_instance,"in_parameters", 5 )  / height) * 2.0;
	inst->tx = ( (double) lvd_extract_param_index( my_instance, "in_parameters", 6 ) / width) * 2.0 - 1.0;
	inst->ty = ( (double) lvd_extract_param_index( my_instance, "in_parameters", 7) / height) * 2.0 - 1.0;

	int alpha = lvd_extract_param_index( my_instance, "in_parameters", 8 );

	inst->frame_src->strides[0] = width;
	inst->frame_src->strides[1] = width;
	inst->frame_src->strides[2] = width;

	inst->frame_dst->strides[0] = width;
	inst->frame_dst->strides[1] = width;
	inst->frame_dst->strides[2] = width;

	update_scaler( inst );

	gavl_video_frame_t* frame_src = inst->frame_src;

	inst->frame_src->planes[0] = B[0];
	inst->frame_src->planes[1] = B[1];
	inst->frame_src->planes[2] = B[2];

	inst->frame_dst->planes[0] = O[0];
	inst->frame_dst->planes[1] = O[1];
	inst->frame_dst->planes[2] = O[2];

	if ( inst->do_scale )
	{
		int len = w * h;
		int i;
		livido_memset( inst->temp_alpha->planes[0],0,len);

		/* scale alpha channel first */
		inst->frame_src->strides[0] = width;
		inst->frame_src->strides[1] = 0;
		inst->frame_src->strides[2] = 0;
		inst->frame_src->planes[0] = B[3];
	
		inst->temp_alpha->strides[0] = width;
		inst->temp_alpha->strides[1] = 0;
		inst->temp_alpha->strides[2] = 0;

		gavl_video_scaler_scale( inst->video_scaler, frame_src, inst->temp_alpha );

		/* setup pointers for frames */
		inst->frame_src->strides[1] = width;
		inst->frame_src->strides[2] = width;
		inst->frame_src->planes[0] = B[0];
		inst->frame_src->planes[1] = B[1];
		inst->frame_src->planes[2] = B[2];

		inst->frame_dst->strides[0] = width;
		inst->frame_dst->strides[1] = width;
		inst->frame_dst->strides[2] = width;
		inst->frame_dst->planes[0] = O[0];
		inst->frame_dst->planes[1] = O[1];
		inst->frame_dst->planes[2] = O[2];

		if(alpha)
		{	
			livido_memset( inst->temp->planes[0], 0, len );
			livido_memset( inst->temp->planes[1],128,len );
			livido_memset( inst->temp->planes[2],128,len );

			gavl_video_scaler_scale( inst->video_scaler, frame_src, inst->temp );

			const uint8_t *t0 = inst->temp->planes[0];
			const uint8_t *t1 = inst->temp->planes[1];
			const uint8_t *t2 = inst->temp->planes[2];
			const uint8_t *tA = inst->temp_alpha->planes[0];

			for( i = 0; i < len; i ++ ) {
				if( tA[i] > 0 )
				{
					unsigned int op1 = tA[i];
					unsigned int op0 = 0xff - tA[i];
					O[0][i] = (op0 * O[0][i] + op1 * t0[i]) >> 8;
					O[1][i] = (op0 * O[1][i] + op1 * t1[i]) >> 8;
					O[2][i] = (op0 * O[2][i] + op1 * t2[i]) >> 8;
					O[3][i] = op1;
				}
			}
		}
		else
		{
			gavl_video_scaler_scale( inst->video_scaler, frame_src, inst->frame_dst );
			const uint8_t *tA = inst->temp_alpha->planes[0];

			for( i = 0; i < len; i ++ )
			{
				if( tA[i] > 0 )
				{
					O[3][i] = tA[i];
				}
			}
		}
	}


	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[9];
	livido_port_t *in_chans[2];
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
		livido_set_string_value( port, "name", "scale0tilt");	
		livido_set_string_value( port, "description", "Crop,Scale,Tilt");
		livido_set_string_value( port, "author", "Richard Spindler, Niels Elburg"); 
		
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
	in_chans[1] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
    port = in_chans[1];
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
		livido_set_int_value(port, "default", 0 );
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
		livido_set_int_value(port, "default", 0 );
		livido_set_string_value( port, "description" ,"Bottom");

	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[4];

		livido_set_string_value(port, "name", "Scale X" );
		livido_set_string_value(port, "kind", "WIDTH" );
		livido_set_string_value( port, "description" ,"Scale X");
		livido_set_int_value(port, "default", 0 );

	in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[5];

		livido_set_string_value(port, "name", "Scale Y" );
		livido_set_string_value(port, "kind", "HEIGHT" );
		livido_set_string_value( port, "description" ,"Scale X");
		livido_set_int_value(port, "default", 0 );

	in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[6];

		livido_set_string_value(port, "name", "Tilt X" );
		livido_set_string_value(port, "kind", "WIDTH" );
		livido_set_string_value( port, "description" ,"Scale X");
		livido_set_int_value(port, "default", 0 );

	in_params[7] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[7];

		livido_set_string_value(port, "name", "Tilt Y" );
		livido_set_string_value(port, "kind", "HEIGHT" );
		livido_set_string_value( port, "description" ,"Scale X");
		livido_set_int_value(port, "default", 0 );

	in_params[8] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[8];

		livido_set_string_value(port, "name", "Alpha" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_string_value( port, "description" ,"Alpha");
		livido_set_int_value(port, "min", 0);
		livido_set_int_value(port, "max", 1);
		livido_set_int_value(port, "default",0);


	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",9, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
    livido_set_portptr_array( filter, "in_channel_templates",2, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
