/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */


#define IS_LIVIDO_PLUGIN 
#include 	<livido.h>
LIVIDO_PLUGIN


#define USE_MATRIX_PLACEMENT

#include	"utils.h"
#include	"livido-utils.c"

typedef struct
{
	uint8_t *data[3];
	int	w;
	int	h;
} picture_t;

typedef struct
{
	picture_t	**photo_list;
	int		num_photos;
	int		frame_counter;
	int		frame_delay;
} photoplay_t;

static	int	prepare_filmstrip(photoplay_t *p, int film_length, int w, int h )
{
 	int i,j;
        int picture_width = w / sqrt(film_length);
        int picture_height = h / sqrt(film_length);
	
        p->photo_list = (picture_t**) livido_malloc(sizeof(picture_t*) * (film_length + 1) );
#ifdef STRICT_CHECKING
	assert( p->photo_list != NULL );
#endif
	if(!p->photo_list)
		return 0;
	
        p->num_photos = film_length;

        uint8_t val = 0;

        for ( i = 0; i < p->num_photos; i ++ )
        {
                p->photo_list[i] = livido_malloc(sizeof(picture_t));
                if(!p->photo_list[i])
                        return 0;
                p->photo_list[i]->w = picture_width;
                p->photo_list[i]->h = picture_height;
                for( j = 0; j < 3; j ++ )
                {
                        p->photo_list[i]->data[j] = livido_malloc(sizeof(uint8_t) * picture_width * picture_height );
                        if(!p->photo_list[i]->data[j])
                                return 0;
                        memset(p->photo_list[i]->data[j], (j==0 ? 16 : 128), picture_width *picture_height );
                }
        }
        p->frame_counter = 0;
	
        return 1;

}
static void destroy_filmstrip(photoplay_t *p)
{
        if(p)
        {
                int i = 0;
                while(i < p->num_photos)
                {
                        if( p->photo_list[i] )
                        {
                                int j;
                                for( j = 0; j < 3; j ++ )
                                        if(p->photo_list[i]->data[j]) 
                                         free(p->photo_list[i]->data[j]);
                                free(p->photo_list[i]);
                        }
                        i++;
                }
                free(p->photo_list);
        }
        p->photo_list = NULL;
        p->num_photos = 0;
        p->frame_counter = 0;
}
static void     take_photo( photoplay_t *p, uint8_t *plane, uint8_t *dst_plane, int w, int h, int index )
{
        int x,y,dx,dy;
        int sum;
        int dst_x, dst_y;
        int box_width = p->photo_list[index]->w;
        int box_height = p->photo_list[index]->h;
#ifdef STRICT_CHECKING
	assert( box_width > 0 );
	assert( box_height > 0 );
	assert( w > 0 );
	assert( h > 0 );
#endif
        int step_x = w / box_width;
        int step_y = h / box_height;

        for( y = 0 ,dst_y = 0; y < h && dst_y < box_height; y += step_y )
        {
                for( x = 0, dst_x = 0; x < w && dst_x < box_width; x+= step_x )
                {
                        sum = 0;
                        for( dy = 0; dy < step_y; dy ++ )
                        {
                                for( dx = 0; dx < step_x; dx++) 
                                {
                                        sum += plane[ ((y+dy)*w+(dx+x)) ];      
                                }
                        }
                        if(sum > 0)
                          dst_plane[(dst_y*box_width)+dst_x] = sum / (step_y*step_x);
                        else
                          dst_plane[(dst_y*box_width)+dst_x] = 0;

                        dst_x++;
                }
                dst_y++;
        }
}
static void put_photo( photoplay_t *p, uint8_t *dst_plane, uint8_t *photo, int dst_w, int dst_h, int index , matrix_t matrix)
{
        int box_w = p->photo_list[index]->w;
        int box_h = p->photo_list[index]->h;
        int x,y;

        uint8_t *P = dst_plane + (matrix.h*dst_w);
        int     offset = matrix.w;

        for( y = 0; y < box_h; y ++ )
        {
                for( x = 0; x < box_w; x ++ )
                {
                        *(P+offset+x) = photo[(y*box_w)+x];
                }
                P += dst_w;
        }
}



livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w=0;
	int h=0;
	photoplay_t *p = (photoplay_t*) livido_malloc( sizeof(photoplay_t));
	livido_memset( p, 0, sizeof( photoplay_t ));

	int error = livido_property_set( my_instance, "PLUGIN_private", 
			LIVIDO_ATOM_TYPE_VOIDPTR, 1, &p );
	
	return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	photoplay_t *p = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
			0, &p );
#ifdef STRICT_CHECKING
	assert( p != NULL );
#endif

	destroy_filmstrip( p );

	free(p);
	
	livido_property_set( my_instance , "PLUGIN_private", 
			LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
	
	return error;
}

livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	unsigned int i,j;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

	int palette[3];
	int w[3];
	int h[3];

	int	p1 = lvd_extract_param_index( my_instance, "in_parameters", 0 );
	int	p2 = lvd_extract_param_index( my_instance, "in_parameters", 1 );
	int	p3 = lvd_extract_param_index( my_instance, "in_parameters", 2 );
	
	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[1],&h[1], O,&palette[1] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	photoplay_t 	*p = NULL;
	error     = livido_property_get( my_instance, "PLUGIN_private", 0, &p );
	
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
	assert( p != NULL );
#endif

	matrix_f	matrix_placement = get_matrix_func( p3 );

	if( (p1*p1) != p->num_photos || p->num_photos == 0)
	{
		destroy_filmstrip(p);
		prepare_filmstrip(p, (p1*p1), w[0],h[0]);
		p->frame_delay = 0;
	}

	if( p->frame_delay )
		p->frame_delay --;

	if( p->frame_delay == 0 )
	{
		for( i = 0; i < 3 ; i ++ )
			take_photo(
					p,
					A[i],
				        p->photo_list[  (p->frame_counter % p->num_photos) ]->data[i],
					w[0],
					h[0],
					(p->frame_counter % p->num_photos )
				);
		p->frame_delay = p2;	
	}

	for( i = 0; i < p->num_photos; i ++ )
	{
		for( j = 0; j < 3 ; j ++ )
		{
			put_photo(
				p,
				O[j],
				p->photo_list[i]->data[j],
				w[0],
				h[0],
				i,
				matrix_placement( i, p1,w[0],h[0] ) 
				 );
		}
	}

	if( p->frame_delay == p2 )
		p->frame_counter ++;
	
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

		livido_set_string_value( port, "name", "PhotoPlay");	
		livido_set_string_value( port, "description", "Create a filmstrip of many small images");
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
           	livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 2, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[0];

		livido_set_string_value(port, "name", "Square size" );
		livido_set_string_value(port, "kind", "INDEX" ); //@ visual representation could be different
		livido_set_int_value( port, "min", 2 );
		livido_set_int_value( port, "max", 64 );
		livido_set_int_value( port, "default", 4 );
		livido_set_string_value( port, "description" ,"Number of Squares");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[1];

		livido_set_string_value(port, "name", "Shutter" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 2500 );
		livido_set_int_value( port, "default", 1 );
		livido_set_string_value( port, "description" ,"Shutter speed");
	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
	port = in_params[2];

		livido_set_string_value(port, "name", "Mode" );
		livido_set_string_value(port, "kind", "INDEX" );
		livido_set_int_value( port, "min", 0 );
		livido_set_int_value( port, "max", 3 );
		livido_set_int_value( port, "default", 0 );
		livido_set_string_value( port, "description" ,"Placement");

	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
