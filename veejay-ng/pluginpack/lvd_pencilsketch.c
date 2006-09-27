/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */


#define IS_LIVIDO_PLUGIN 
#include 	<livido.h>
LIVIDO_PLUGIN
#include	"utils.h"
#include	"livido-utils.c"

livido_init_f	init_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	return LIVIDO_NO_ERROR;
}
/* PenCil sketch pixel Functions
   applies some artithematic on pixel a and b,
*/
	typedef uint8_t (*_pcf) (uint8_t a, uint8_t b, int t_max);
	typedef uint8_t (*_pcbcr) (uint8_t a, uint8_t b);

	static uint8_t _pcf_dneg(uint8_t a, uint8_t b, int t_max)
        {
                uint8_t p =  
                        255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
                p = (abs(abs(p-b) - b));
                return p;
        }

        static uint8_t _pcf_lghtn(uint8_t a, uint8_t b, int t_max)
        {
                uint8_t p = (a > b ? a : b );
                return p;
        }

        static uint8_t _pcf_dneg2(uint8_t a,uint8_t b, int t_max)
        {
                uint8_t p = ( 255 - abs ( (255-a)- b )  );
                return p;
        }

        static uint8_t _pcf_min(uint8_t a, uint8_t b, int t_max)
        {
                uint8_t p = ( (b < a) ? b : a);
                p = ( 255 - abs( (255-p) - b ) );
                return p;
        }

        static uint8_t _pcf_max(uint8_t a,uint8_t b, int t_max)
        {
                int p = ( (b > a) ? b : a);
                p = ( 255 - ((255 - b) * (255 - b)) / p);
                return (uint8_t)p;
        }

        static uint8_t _pcf_pq(uint8_t a,uint8_t b, int t_max)
        {
                int p = 255 - ((255-a) * (255-a)) / a;
                int q = 255 - ((255-b) * (255-b)) / b;
                p = ( 255 - ((255-p) * (255 - a)) / q);
                return (uint8_t)p;
        }
   	static uint8_t _pcf_color(uint8_t a, uint8_t b, int t_max)
        {
                uint8_t p =  
                        255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
                p = (abs(abs(p-b) - b));
                p = p + b - (( p * b ) >> 8);
                return p;
        }
        static uint8_t _pcbcr_color(uint8_t a,uint8_t b)
        {
                int p = a - 128;
                int q = b - 128;
                return ( p + q - (( p * q ) >> 8) ) + 128 ;
        }

        static uint8_t _pcf_none(uint8_t a, uint8_t b, int t_max)
        {
                if( a >= 16 || a <= t_max) a = 16 ; else a = 240;
                return a;
        }

        /* get a pointer to a pixel function */
        static _pcf    _get_pcf(int type)
        {
        
                switch(type)
                {
         
                 case 0: return &_pcf_dneg;
                 case 3: return &_pcf_lghtn;
                 case 1: return &_pcf_min;
                 case 2: return &_pcf_max;
                 case 5: return &_pcf_pq;
                 case 6: return &_pcf_dneg2;
                 case 7: return &_pcf_color;
                }
        
                return &_pcf_none;
        }
/* PenCil sketch pixel Functions
   applies some artithematic on pixel a and b,
*/


livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};

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
	len = w[0] * h[0];

	int	type = lvd_extract_param_index( my_instance, "in_parameters", 0 );
	double  lt   = lvd_extract_param_number( my_instance, "in_parameters", 1 );
	double  ht   = lvd_extract_param_number( my_instance, "in_parameters",2 );
	uint8_t min_threshold = 255 * lt;
	uint8_t max_threshold = 255 * ht;
	int	m,d;
  	/* get a pointer to a pixel blend function */
        _pcf _pff = _get_pcf(type);
        _pcbcr _pcbcrff = &_pcbcr_color;
	
	int	rlen = len - w[0] - 1;
	uint8_t y,yb;
	for ( i = 0; i < rlen ;i ++ )
	{
		yb = y = A[0][i];
		if(yb<16)
			yb=16;
		if( y >= min_threshold && y <= max_threshold )
		{
			// sharpen
			m = ( A[0][i+1] + A[0][(i + w[0])] + A[0][ (i + w[0] + 1)]  + 2 ) >> 2;
			d = y - m;
			d *= 500;
			d /= 100;
			m = m + d;
			/* a magical forumula to combine the pixel with the original*/
                        y = ((((y << 1) - (255 - m))>>1) + A[0][i])>>1;
			if(y < 16) y =16;
                        O[0][i] = _pff(y,yb,max_threshold);
		}
		else
			O[0][i] = 0xff;
	}	

	if( type != y )
	{
		livido_memset( O[1], 128, uv_len );
		livido_memset( O[2], 128, uv_len );
	}
	else
	{
		uint8_t u,v;
		for( i = 0; i < uv_len; i ++ )
		{
			u = A[1][i];
			v = A[2][i];
			if( u < 16 ) u = 16;
			if( v < 16 ) v = 16;
			O[1][i] =  _pcbcrff(128, u);
			O[2][i] =  _pcbcrff(128, v);
		}
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

		livido_set_string_value( port, "name", "Pencil Sketch");	
		livido_set_string_value( port, "description", "Artistify Image");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
                      	LIVIDO_PALETTE_YUV420P,
                       	LIVIDO_PALETTE_YUV422P,
			LIVIDO_PALETTE_YUV444P,
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

                livido_set_string_value(port, "name", "Mode" );
                livido_set_string_value(port, "kind", "INDEX" );
                livido_set_int_value( port, "min", 0 );
                livido_set_int_value( port, "max", 8 );
                livido_set_int_value( port, "default", 0 );
                livido_set_string_value( port, "description" ,"Pencil Type");

	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Min Threshold" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Minimum threshold");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[2];
                livido_set_string_value(port, "name", "Maximum Threshold" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.9 );
                livido_set_string_value( port, "description" ,"Maximum threshold");

	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",3, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
