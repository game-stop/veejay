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
    for(int y = 0; y < h; y++) {
        int row = y * w;

        double fy = (double)(y - wall->cy) / w;
        fy *= 1.0 - fy * fy * 0.8;
        fy *= 1.0 - (double)y / h * 0.15;

#pragma omp simd
        for(int x = 0; x < w; x++) {
            int i = row + x;

            double fx = (double)(x - wall->cx) / w;
            fx *= 1.0 - fx * fx * 0.4;

            wall->vecx[i] = (int)(fx * w);
            wall->vecy[i] = (int)(fy * w);
        }
    }
}

static inline int wrap_mod(int v, int max)
{
    int r = v % max; // or replace by LUT
    return r + ((r >> 31) & max);
}

static void displaywall_tick(displaywall_t *wall, int w, int h)
{
    wall->speed += wall->speedi;
    if (wall->speed < 0) wall->speed = 0;

    wall->bx += wall->dx * wall->speed;
    wall->by += wall->dy * wall->speed;

    wall->bx = wrap_mod(wall->bx, w);
    wall->by = wrap_mod(wall->by, h);

    if (wall->scale == 1) {
        wall->bx = wall->cx;
        wall->by = wall->cy;
    }
}

static int displaywall_draw(
    displaywall_t *wall,
    uint8_t *restrict src,
    uint8_t *restrict dst,
    int w,
    int h)
{
    const int bx = wall->bx;
    const int by = wall->by;
    const int scale = wall->scale;

    int area = w * h;
    for(int i = 0; i < area; i++) {

        int px = bx + wall->vecx[i] * scale;
        int py = by + wall->vecy[i] * scale;

        px = wrap_mod(px, w);
        py = wrap_mod(py, h);

        dst[i] = src[py * w + px];
    }

    return 0;
}

int init_instance(livido_port_t *my_instance)
{
    int w = 0, h = 0;
    lvd_extract_dimensions(my_instance, "out_channels", &w, &h);

    int video_area = w * h;

    displaywall_t *entry = livido_malloc(sizeof(displaywall_t));
    if (!entry)
        return LIVIDO_ERROR_MEMORY_ALLOCATION;

    livido_memset(entry, 0, sizeof(displaywall_t));

    entry->vecx = livido_malloc(sizeof(int) * video_area);
    entry->vecy = livido_malloc(sizeof(int) * video_area);

    if (!entry->vecx || !entry->vecy) {
        if (entry->vecx) free(entry->vecx);
        if (entry->vecy) free(entry->vecy);
        free(entry);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    entry->scale = 3;
    entry->speed = 10;
    entry->cx = w >> 1;
    entry->cy = h >> 1;

    displaywall_initVec(entry, w, h);

    livido_property_set(my_instance, "PLUGIN_private",
                        LIVIDO_ATOM_TYPE_VOIDPTR, 1, &entry);

    return LIVIDO_NO_ERROR;
}

livido_deinit_f deinit_instance(livido_port_t *my_instance)
{
    displaywall_t *wall = NULL;
    livido_property_get(my_instance, "PLUGIN_private", 0, &wall);

    if (wall) {
        free(wall->vecx);
        free(wall->vecy);
        free(wall);
    }

    return LIVIDO_NO_ERROR;
}

static inline int wrap_fast(int v, int max)
{
    if (v >= max) return v - max;
    if (v < 0)    return v + max;
    return v;
}

int process_instance(livido_port_t *my_instance, double timecode)
{
    uint8_t *A[4] = {0};
    uint8_t *O[4] = {0};

    int palette, w, h;

    if (lvd_extract_channel_values(my_instance, "out_channels", 0,
                                   &w, &h, O, &palette)
        != LIVIDO_NO_ERROR)
        return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    if (lvd_extract_channel_values(my_instance, "in_channels", 0,
                                   &w, &h, A, &palette)
        != LIVIDO_NO_ERROR)
        return LIVIDO_ERROR_NO_INPUT_CHANNELS;

    int scale = lvd_extract_param_index(my_instance,"in_parameters",0);
    int mode  = lvd_extract_param_index(my_instance,"in_parameters",1);

    int shift = (palette != LIVIDO_PALETTE_YUV444P);

    displaywall_t *wall = NULL;
    livido_property_get(my_instance, "PLUGIN_private", 0, &wall);

    static const int dx_table[4] = { 0,  0,  0, -1 };
    static const int dy_table[4] = { 1,  1, -1,  0 };

    wall->dx = dx_table[mode & 3];
    wall->dy = dy_table[mode & 3];
    wall->scale = scale;

    displaywall_tick(wall, w, h);

    displaywall_draw(wall, A[0], O[0], w, h);
    displaywall_draw(wall, A[1], O[1], w >> shift, h);
    displaywall_draw(wall, A[2], O[2], w >> shift, h);

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