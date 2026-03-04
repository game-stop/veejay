/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2011 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

/* Copyright (C) 2002 W.P. van Paassen - peter@paassen.tmfweb.nl

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

#include 	"lvd_common.h"

#define MAX_STARS 2000

typedef struct 
{
  float xpos, ypos;
  short zpos, speed;
  uint8_t color;
  uint32_t seed;
} STAR;

typedef struct
{
	STAR *stars;
} starfield_t;

static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline uint32_t fastrand_lcg(uint32_t *state) {
    *state = (*state * 1103515245 + 12345) & 0x7fffffff;
    return *state;
}

static inline float get_rand_float(uint32_t *seed, int mode) {
    if (mode == 0)
        return (float)rand() / (float)RAND_MAX;
    else if (mode == 1)
        return (float)(xorshift32(seed) & 0xFFFFFF) / 16777216.0f;
    else
        return (float)fastrand_lcg(seed) / 2147483648.0f;
}

static void init_star(STAR* star, int speed, int initial_z, int mode)
{
    star->zpos = (initial_z == -1) ? MAX_STARS : initial_z;

    star->xpos = (-10.0f + (20.0f * get_rand_float(&star->seed, mode))) * 3072.0f;
    star->ypos = (-10.0f + (20.0f * get_rand_float(&star->seed, mode))) * 3072.0f;
    
    star->speed = (speed + 2) + (int)((float)speed * get_rand_float(&star->seed, mode));
    
    star->color = (uint8_t)(255 - (star->zpos >> 3)); 
}


int init_instance( livido_port_t *my_instance )
{
    starfield_t *starfield = (starfield_t*) livido_malloc( sizeof(starfield_t));
    if(!starfield) return LIVIDO_ERROR_MEMORY_ALLOCATION;

    starfield->stars = (STAR*) livido_malloc(sizeof(STAR) * MAX_STARS);
    if(!starfield->stars) {
        livido_free(starfield);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    srand((unsigned int)time(NULL));
    uint32_t global_seed = (uint32_t)rand();

    for(int i = 0; i < MAX_STARS ; i ++ ) {
        STAR *s = &starfield->stars[i];
        s->seed = global_seed + i;
        int random_depth = (rand() % MAX_STARS) + 1;
        init_star(s, 2, random_depth, 0);
    }

    livido_property_set(my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &starfield);
    return LIVIDO_NO_ERROR;
}

livido_deinit_f deinit_instance( livido_port_t *my_instance )
{
    void *ptr = NULL;
    if ( livido_property_get( my_instance, "PLUGIN_private", 0, &ptr ) == LIVIDO_NO_ERROR )
    {
        starfield_t *starfield = (starfield_t*) ptr;
        livido_free(starfield->stars); // Single free for the whole block
        livido_free(starfield);
    }
    return LIVIDO_NO_ERROR;
}


int		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette;
	int w;
	int h;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;
	
	starfield_t *starfield = NULL;
	if ( livido_property_get( my_instance, "PLUGIN_private", 0, &starfield ) != LIVIDO_NO_ERROR )
    	return LIVIDO_ERROR_INTERNAL;

	//@ get parameter values
	int	num_stars =  lvd_extract_param_index( my_instance,"in_parameters", 0 );
	int	speed = 2 + lvd_extract_param_index( my_instance, "in_parameters", 1 );
	int mode = lvd_extract_param_index( my_instance, "in_parameters", 2 );
	const int n = (num_stars > MAX_STARS) ? MAX_STARS : num_stars;

	livido_memset( O[0], 0, len );
	livido_memset( O[1], 128, uv_len );
	livido_memset( O[2], 128, uv_len );

	int center_x = w >> 1;
	int center_y = h >> 1;
	int i;

	STAR *s = starfield->stars;

	for(int i = 0; i < n; i++) {
        s[i].zpos -= s[i].speed;

        if( s[i].zpos <= 0 ) {
            init_star(&s[i], speed, -1, mode);
        }

        float inv_z = 1.0f / (float)s[i].zpos;
        int tx = (int)(s[i].xpos * inv_z) + center_x;
        int ty = (int)(s[i].ypos * inv_z) + center_y;

        if( (unsigned int)tx < (unsigned int)w && (unsigned int)ty < (unsigned int)h ) {
            s[i].color = (uint8_t)(255 - (s[i].zpos >> 3));
            O[0][ty * w + tx] = s[i].color;
        } else {
            init_star(&s[i], speed, -1, mode);
        }
    }
	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[3];
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
		livido_set_string_value( port, "name", "Starfield");	
		livido_set_string_value( port, "description", "Starfield");
		livido_set_string_value( port, "author", "The Demo Effects Collection");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	//@ some palettes veejay-classic uses
	int palettes0[] = {
           	LIVIDO_PALETTE_YUV420P,
           	LIVIDO_PALETTE_YUV422P,
            LIVIDO_PALETTE_YUV444P,
            0
	};
	
	//@ setup output channel
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	    livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 4, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Stars" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", MAX_STARS );
		livido_set_int_value( port, "default", 1020 );
		livido_set_string_value( port, "description" ,"Number of stars");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Speed" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 64 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Speed");


	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    livido_set_string_value(in_params[2], "name", "Random Mode" );
    livido_set_string_value(in_params[2], "kind", "INDEX" );
    livido_set_int_value( in_params[2], "min", 0 );
    livido_set_int_value( in_params[2], "max", 2 );
    livido_set_int_value( in_params[2], "default", 2 );

	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
