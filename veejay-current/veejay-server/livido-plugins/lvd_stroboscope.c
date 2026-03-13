/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C) 2015 Niels Elburg <nwelburg@gmail.com>
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif
#include <stdlib.h>
#include <unistd.h>
#include 	"../libplugger/specs/livido.h"
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"
#include <omp.h>

typedef struct
{
	uint8_t *buffer;
	uint8_t *map;
	uint8_t *planes[3];
	int current;
	int n_threads;
} hold_buffer_t;

static inline int __advise_num_threads(const int len) {
	static int ncores = -1;
    if (ncores == -1) {
        ncores = (int) sysconf(_SC_NPROCESSORS_ONLN);
    }
    int nthreads = ncores;

    if (len < (1920*1080)) nthreads = ncores / 2;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 6) nthreads = 6; // avoid too much overhead

    return nthreads;
}

int	init_instance( livido_port_t *my_instance )
{
	int w = 0, h = 0;
	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

	hold_buffer_t *hb = (hold_buffer_t*) livido_malloc( sizeof( hold_buffer_t ));
    if(!hb) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }
    hb->buffer = (uint8_t*) livido_malloc( sizeof(uint8_t) * ( w * h * 3));
    if(!hb->buffer) {
        free(hb);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }
    
	hb->planes[0] = hb->buffer;
	hb->planes[1] = hb->planes[0] + (w*h);
	hb->planes[2] = hb->planes[1] + (w*h);
	hb->current   = 0;
	
	hb->n_threads = __advise_num_threads(w*h);

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
        uint8_t *restrict O1, uint8_t *restrict O2, uint8_t *restrict O0,
        const uint8_t *restrict A1, const uint8_t *restrict A2,
        const uint8_t *restrict B1, const uint8_t *restrict B2,
        const uint8_t *restrict Y1,
        const uint8_t *restrict Y2, 
        const int w, 
        const int h,
        const int feather,
        const int shift,
		const int n_threads)
{
    const int len    = w * h;
    const int uv_len = len >> shift;

	#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < uv_len; i++) {
        const int idx = i << shift;
        const int y1v = Y1[idx];
        const int y2v = Y2[idx];

        int diff = y1v - y2v;
        int abs_diff = diff >= 0 ? diff : -diff;

        const uint8_t feather_mask = -(abs_diff < feather);
        const uint8_t y1_mask      = -(diff > 0);
        const uint8_t y2_mask      = ~y1_mask;

        uint8_t blended1 = (A1[i] + B1[i]) >> 1;
        uint8_t blended2 = (A2[i] + B2[i]) >> 1;

        O1[i] = (feather_mask & blended1) | (~feather_mask & ((y1_mask & A1[i]) | (y2_mask & B1[i])));
        O2[i] = (feather_mask & blended2) | (~feather_mask & ((y1_mask & A2[i]) | (y2_mask & B2[i])));
    }

	#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++) {
        const uint8_t y1v = Y1[i];
        const uint8_t y2v = Y2[i];
        O0[i] = y1v - ((y1v - y2v) & ((y1v - y2v) >> 7));
    }
}

static inline void fading_stroboscope(uint8_t *O, const uint8_t *A, const uint8_t *B, const uint8_t *Op, const int len, const int n_threads)
{    
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++) {
        O[i] = A[i] + (((int)Op[i] * ((int)B[i] - (int)A[i])) >> 8);
    }
}

static inline void fading_stroboscopeUV( 
        uint8_t *restrict O1, uint8_t *restrict O2,
        const uint8_t *restrict A1, const uint8_t *restrict A2,
        const uint8_t *restrict B1, const uint8_t *restrict B2,
        const uint8_t *restrict Y1,
        const uint8_t *restrict Y2, 
        const int w, 
        const int h,
        const int feather,
        const int shift,
		const int n_threads )
{
    const int uv_len = (w*h) >> shift;

	#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < uv_len; i++) {
        const int y_idx = i << shift;
        const int y1v = Y1[y_idx];
        const int y2v = Y2[y_idx];

        const uint8_t mask = -(y1v > y2v);  

        O1[i] = (mask & A1[i]) | (~mask & B1[i]);
        O2[i] = (mask & A2[i]) | (~mask & B2[i]);

        if (feather > 0) {
            const int diff = y1v - y2v;
            const uint8_t feather_mask = (uint8_t)(-(abs(diff) < feather));
            O1[i] = (feather_mask & ((A1[i]+B1[i])>>1)) | (~feather_mask & O1[i]);
            O2[i] = (feather_mask & ((A2[i]+B2[i])>>1)) | (~feather_mask & O2[i]);
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
			fading_stroboscope( O[0], ptr->planes[0], A[0], A[0],len, ptr->n_threads );
			fading_stroboscopeUV( 
				O[1],O[2],
				ptr->planes[1],ptr->planes[2],
				A[1],A[2],
				ptr->planes[0],A[0],
				w,h, feather, shift, ptr->n_threads );

		} else if ( mode == 1) {
			stroboscope( 
				O[1],O[2],O[0],
				ptr->planes[1],ptr->planes[2],
				A[1],A[2],
				ptr->planes[0],A[0],
				w,h, feather, shift, ptr->n_threads );
			ptr->current ++;
		}
		else if( mode == 2 ) {
			fading_stroboscope( O[0], ptr->planes[0], A[0], A[0],len, ptr->n_threads);
			fading_stroboscope( O[1], ptr->planes[1], A[1], A[1],uv_len, ptr->n_threads );
			fading_stroboscope( O[2], ptr->planes[2], A[2], A[2],uv_len, ptr->n_threads );
		}
		else if ( mode == 3 ) {
			fading_stroboscope( O[0], ptr->planes[0], A[0], ptr->planes[0], len, ptr->n_threads );
			fading_stroboscope( O[1], ptr->planes[1], A[1], ptr->planes[1], uv_len, ptr->n_threads );
			fading_stroboscope( O[2], ptr->planes[2], A[2], ptr->planes[2], uv_len, ptr->n_threads );
		}

		livido_memcpy( ptr->planes[0], O[0], len );
		livido_memcpy( ptr->planes[1], O[1], uv_len );
		livido_memcpy( ptr->planes[2], O[2], uv_len );	
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
		LIVIDO_PALETTE_YUV444P,
        0
	};
	
	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	livido_set_string_value( port, "name", "Output Channel");
	livido_set_int_array( port, "palette_list", 3, palettes0);
	livido_set_int_value( port, "flags", 0);

	in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
	livido_set_string_value( port, "name", "Input Channel");
	livido_set_int_array( port, "palette_list", 3, palettes0);
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
