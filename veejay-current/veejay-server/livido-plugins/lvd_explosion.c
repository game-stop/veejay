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

#define MAX_PARTICLES 2000
/*particle structure*/
typedef struct 
{
  uint16_t xpos,ypos,xdir,ydir;
  uint8_t colorindex;
  uint8_t dead;
} PARTICLE;

typedef struct
{
	PARTICLE **particles;
	uint8_t *fire;
	int	last_n;
} particles_t;

void init_particle(PARTICLE* particle, int w, int h)
{
  /* randomly init particles, generate them in the center of the screen */

  particle->xpos =  (w >> 1) - 20 + (int)(40.0 * (rand()/(RAND_MAX+1.0)));
  particle->ypos =  (h >> 1) - 20 + (int)(40.0 * (rand()/(RAND_MAX+1.0)));
  particle->xdir =   -10 + (int)(20.0 * (rand()/(RAND_MAX+1.0)));
  particle->ydir =   -17 + (int)(19.0 * (rand()/(RAND_MAX+1.0)));
  particle->colorindex = 255;
  particle->dead = 0;
}

int	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

	particles_t *particles = (particles_t*) livido_malloc( sizeof(particles_t));
    if(!particles) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    livido_memset( particles, 0, sizeof(particles_t) );

    particles->particles = (PARTICLE**) livido_malloc(sizeof(PARTICLE*) * MAX_PARTICLES );
	if(!particles->particles) {
        free(particles);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    particles->fire = (uint8_t*) livido_malloc( sizeof(uint8_t) * (w * h) + (w + w) );
    if(!particles->fire) {
        free(particles->particles);
        free(particles);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

	livido_memset( particles->fire, 0, w * h );
	int i,j;
	for( i = 0; i < MAX_PARTICLES ; i ++ ) {
		particles->particles[i] = (PARTICLE*) livido_malloc( sizeof(PARTICLE) );
	    if(particles->particles[i]==NULL) {
            for(j = 0; j < i; j ++ ) {
                free(particles->particles[j]);
            }
            free(particles->particles);
            free(particles->fire);
            free(particles);
            return LIVIDO_ERROR_MEMORY_ALLOCATION;
        }
        init_particle( particles->particles[i], w,h );
	}

	livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &particles);

	return LIVIDO_NO_ERROR;
}

livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	void *ptr = NULL;
	if ( livido_property_get( my_instance, "PLUGIN_private", 0, &ptr ) == LIVIDO_NO_ERROR )
	{
		particles_t *particles = (particles_t*) ptr;
		int i;
		for( i = 0; i < MAX_PARTICLES; i ++ ) {
			livido_free(particles->particles[i]);
		}
		livido_free(particles->particles);
		livido_free(particles->fire);
		livido_free(particles);
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
	int i,j;
	
	//@ get output channel details
	int error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS; //@ error codes in livido flanky

	int uv_len = lvd_uv_plane_len( palette,w,h );
	len = w * h;
	
	particles_t *parts = NULL;
	if ( livido_property_get( my_instance, "PLUGIN_private", 0, &parts ) != LIVIDO_NO_ERROR )
   	 	return LIVIDO_ERROR_INTERNAL;

	//@ get parameter values
	int		number_of_particles =  lvd_extract_param_index( my_instance,"in_parameters", 0 );

	int all_dead = 1;
	PARTICLE **particles = parts->particles;

	int		continuous_explosions = lvd_extract_param_index( my_instance, "in_parameters", 1 );
	if( !continuous_explosions ) {
		all_dead = 0;
	}

	if( continuous_explosions ) {
		for( i = 0; i < number_of_particles; i ++ ) {
			if(!particles[i]->dead) {
				all_dead = 0;
				break;
			}
		}
	}

	livido_memset( O[0], 0, len );
	livido_memset( O[1], 128, uv_len );
	livido_memset( O[2], 128, uv_len );

	if( all_dead && !continuous_explosions )
		return LIVIDO_NO_ERROR;


	if( number_of_particles != parts->last_n || all_dead ) {
		parts->last_n = number_of_particles;

		for( i = 0; i < MAX_PARTICLES; i ++ )
			init_particle( parts->particles[i],w,h );
	}


    const int lim = w * h;
	uint32_t temp,index,buf;
	uint8_t *fire = parts->fire;

	for( i = 0; i < number_of_particles; i ++ ) {
	
		if(!particles[i]->dead) {
			particles[i]->xpos += particles[i]->xdir;
			particles[i]->ypos += particles[i]->ydir;

			/* is particle dead? */
			if((particles[i]->ypos > (h -3)) || particles[i]->colorindex == 0 ||
				particles[i]->xpos <= 1 || particles[i]->xpos > (w - 3) ) {
				particles[i]->dead = 1;
				continue;
			}

			/* gravity takes over */
			particles[i]->ydir ++;

			/* particle cools off */
			particles[i]->colorindex --;

			/* draw particle */
			temp = particles[i]->ypos * w + particles[i]->xpos;

			fire[temp] = particles[i]->colorindex;
			
            fire[temp - 1] = particles[i]->colorindex;
			fire[temp + w] = particles[i]->colorindex;
            
            if ( (temp-w) > 0 && (temp-w) < lim ) 
                fire[temp - w] = particles[i]->colorindex;
            
            fire[temp + 1] = particles[i]->colorindex;
            
		}
	}

	for( i = 1; i < (h-2); i ++ ) {
		index = ( i - 1 ) * w;
		for( j = 1; j < (w-2); j ++ ) {
			buf = index + j;

			temp = fire[buf];
			temp += fire[buf + 1];
			temp += fire[buf - 1];

			buf += w;

			temp += fire[buf - 1];
			temp += fire[buf + 1];

			buf += w;

			temp += fire[buf];
			temp += fire[buf + 1];
			temp += fire[buf - 1];

			temp >>= 3;

			if( temp > 4 ) {
				temp -= 4;
			}
			else {
				temp = 0;
			}

			fire[ buf - w ] = temp;
		}
	}

	uint8_t *image = O[0];

	for( i = h - 1; i >= 0; -- i ) {
		temp = i * w;
		for( j = w - 1; j >= 0; --j ) {
			image[ i * w + j] = fire[ temp + j ];
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
		livido_set_string_value( port, "name", "Explosion");	
		livido_set_string_value( port, "description", "Particle Explosion");
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
		livido_set_int_array( port, "palette_list", 3, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	//@ setup parameters (INDEX type, 0-255) 
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Particles" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", MAX_PARTICLES );
		livido_set_int_value( port, "default", 500 );
		livido_set_string_value( port, "description" ,"Number of particles");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Continuous" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min" , 0 );
		livido_set_int_value( port, "max",  1 );
		livido_set_int_value( port, "default", 1 );
		livido_set_string_value( port, "description", "Continous Explosions" );

	
	//@ setup the nodes
	livido_set_portptr_array( filter, "in_parameter_templates",2, in_params );
	livido_set_portptr_array( filter, "in_channel_templates",0, NULL );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
