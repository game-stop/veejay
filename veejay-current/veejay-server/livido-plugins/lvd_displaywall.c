/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */
/*
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * DisplayWall
 * Copyright (C) 2005-2006 FUKUCHI Kentaro
 *
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
	int *vecx;
	int *vecy;
	int scale;
	int dx, dy;
	int bx, by;
	int speed;
	int speedi;
	int cx, cy;	
} displaywall_t;

static void displaywall_initVec(displaywall_t *wall, int w, int h)
{
	int x, y, i;
	double vx, vy;

	i = 0;
	for(y=0; y < h; y++) {
		for(x=0; x < w; x++) {
			vx = (double)(x - wall->cx) / w;
			vy = (double)(y - wall->cy) / w;

			vx *= 1.0 - vx * vx * 0.4;
			vy *= 1.0 - vx * vx * 0.8;
			vx *= 1.0 - (double)y / h * 0.15;
			wall->vecx[i] = vx * w;
			wall->vecy[i] = vy * w;

			i++;
		}
	}
}

static void displaywall_tick( displaywall_t *wall, int video_width, int video_height )
{
	wall->speed += wall->speedi;
	if(wall->speed < 0) wall->speed = 0;

	wall->bx += wall->dx * wall->speed;
	wall->by += wall->dy * wall->speed;
	while(wall->bx < 0) wall->bx += video_width;
	while(wall->bx >= video_width) wall->bx -= video_width;
	while(wall->by < 0) wall->by += video_height;
	while(wall->by >= video_height) wall->by -= video_height;

	if(wall->scale == 1) {
		wall->bx = wall->cx;
		wall->by = wall->cy;
	}
}

static int displaywall_draw(displaywall_t *wall, uint8_t *src, uint8_t *dst, int video_width, int video_height)
{
	int x, y, i;
	int px, py;

	i = 0;
	for(y=0; y<video_height; y++) {
		for(x=0; x<video_width; x++) {
			px = wall->bx + wall->vecx[i] * wall->scale;
			py = wall->by + wall->vecy[i] * wall->scale;
			while(px < 0) px += video_width;
			while(px >= video_width) px -= video_width;
			while(py < 0) py += video_height;
			while(py >= video_height) py -= video_height;

			dst[i++] = src[py * video_width + px];
		}
	}

	return 0;
}

livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
        lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

	int video_area = w * h * 3;

	displaywall_t *entry = (displaywall_t*) livido_malloc(sizeof(displaywall_t));
	livido_memset( entry, 0, sizeof(displaywall_t));

	entry->vecx = (int *)livido_malloc(sizeof(int) * video_area);
	entry->vecy = (int *)livido_malloc(sizeof(int) * video_area);
	if(entry->vecx == NULL || entry->vecy == NULL) {
		free(entry);
		return NULL;
	}

	entry->scale = 3;
	entry->dx = 0;
	entry->dy = 0;
	entry->speed = 10;
	entry->cx = w / 2;
	entry->cy = h / 2;

	displaywall_initVec(entry, w, h);

	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &entry);

	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	displaywall_t *wall = NULL;
	livido_property_get( my_instance, "PLUGIN_private", 0, &wall );

	if( wall ) {
		if( wall->vecx ) free(wall->vecx);
		if( wall->vecy ) free(wall->vecy);
		free(wall);
	}		

	return LIVIDO_NO_ERROR;
}


int		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_INPUT_CHANNELS;

	int		scale =  lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int		mode  =  lvd_extract_param_index( my_instance,"in_parameters", 1 );
	int		shift = 1;

	if( palette == LIVIDO_PALETTE_YUV444P )
	    shift = 0;

	displaywall_t *wall = NULL;
	livido_property_get( my_instance, "PLUGIN_private", 0, &wall );

	switch( mode ) {
		case 0:
			wall->dx = 0; wall->dy = 1; 
			break;
		case 1:
			wall->dy = 1; wall->dx = 0;
			break;
		case 2:
			wall->dy = -1; wall->dx = 0;		
			break;
		case 3:
			wall->dy = 0; wall->dx = -1;
			break;
	}

	wall->scale = scale;

	displaywall_tick( wall, w, h );

	displaywall_draw( wall, A[0], O[0], w, h);
	displaywall_draw( wall, A[1], O[1], w >> shift, h );
	displaywall_draw( wall, A[2], O[2], w >> shift, h );

	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[2];
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
		livido_set_string_value( port, "name", "Displaywall (EffecTV)");	
		livido_set_string_value( port, "description", "Displaywall");
		livido_set_string_value( port, "author", "Kentaro Fukuchi"); //ported to livido
		
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

		livido_set_string_value(port, "name", "Scale" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 2 );
		livido_set_int_value( port, "max", 10 );
		livido_set_int_value( port, "default", 2 );
		livido_set_string_value( port, "description" ,"Scale");

	
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Mode" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 3 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Mode");

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",2, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
        livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
