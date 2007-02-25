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

typedef struct
{
	uint8_t *tmpy;
	uint8_t *tmpu;
	int	empty;
	unsigned int *axis_y;
	unsigned int *axis_x;
	unsigned int prev_x;
	unsigned int prev_y;
	double	activity;
	double  wx;
	double  wy;
	double  lx;
	double  ly;
	double	hisx[5];
	double  hisy[5];
	double  his0[5];
	double  his1[5];
	double  ip[5];
	int	h;
	
} bg_t;

#define ru8(num)(((num)+8)&~8)


livido_init_f	init_instance( livido_port_t *my_instance )
{
	int w=0;
        int h=0;

	lvd_extract_dimensions( my_instance, "out_channels", &w, &h );
     
	bg_t *b = (bg_t*) livido_malloc( sizeof( bg_t ));
	livido_memset( b,0,sizeof(bg_t));
	b->tmpy = (uint8_t*) livido_malloc(sizeof(uint8_t) * ru8( w * h ));
	b->tmpu = (uint8_t*) livido_malloc(sizeof(uint8_t) * ru8( w * h ));
	
	b->axis_x   = (unsigned int*) livido_malloc(sizeof(unsigned int) * ru8(w)  );
	b->axis_y   = (unsigned int*) livido_malloc(sizeof(unsigned int) * ru8(h)  );

	int error = livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &b );
        
        return error;
}


livido_deinit_f	deinit_instance( livido_port_t *my_instance )
{
	bg_t *b = NULL;
	int error = livido_property_get( my_instance, "PLUGIN_private",
                        0, &b );
#ifdef STRICT_CHECKING
        assert( b != NULL );
#endif
	livido_free( b->tmpy );
	livido_free( b->tmpu );
	livido_free( b->axis_x );
	livido_free( b->axis_y );
	livido_free( b );
	return LIVIDO_NO_ERROR;
}

static	int		_max(unsigned int *arr, int n)
{
	unsigned int i;
	unsigned int max = 0;
	int k = -1;
	for( i = 0; i < n; i ++)
	{
		if( arr[i] > max )
		{
			max = arr[i];
			k = i;
		}
	}
	return k;
}

static	int	binarify( uint8_t *dst, uint8_t *bg, uint8_t *src,int threshold,int reverse, const int len )
{
	int i;
	int act = 0;
	if(!reverse)
	{
		for( i = 0; i < len; i ++ )
		{
			if ( abs(bg[i] - src[i]) <= threshold )
				dst[i] = 0;
			else
			{	dst[i] = 0xff; act ++ ; }
		}

	}
	else
	{
		for( i = 0; i < len; i ++ )
		{
			if ( abs(bg[i] - src[i]) >= threshold )
				dst[i] = 0;
			else
			{	dst[i] = 0xff; act ++;}
		
		}
	}
	return act;
}

/* mmx_blur() taken from libvisual plugins
 *
 * Libvisual-plugins - Standard plugins for libvisual
 *
 * Copyright (C) 2002, 2003, 2004, 2005 Dennis Smit <ds@nerds-incorporated.org>
 *
 * Authors: Dennis Smit <ds@nerds-incorporated.org>
 */

static  void    mmx_blur(uint8_t *buffer, int width, int height)
{
        __asm __volatile
                ("\n\t pxor %%mm6, %%mm6"
                 ::);

        int scrsh = height / 2;
        int i;
        int len = width * height;
        uint8_t *buf = buffer;
        /* Prepare substraction register */
        for (i = 0; i < scrsh; i += 4) {
                __asm __volatile
                        ("\n\t movd %[buf], %%mm0"
                         "\n\t movd %[add1], %%mm1"
                         "\n\t punpcklbw %%mm6, %%mm0"
                         "\n\t movd %[add2], %%mm2"
                         "\n\t punpcklbw %%mm6, %%mm1"
                         "\n\t movd %[add3], %%mm3"
                         "\n\t punpcklbw %%mm6, %%mm2"
                         "\n\t paddw %%mm1, %%mm0"
                         "\n\t punpcklbw %%mm6, %%mm3"
                         "\n\t paddw %%mm2, %%mm0"
                         "\n\t paddw %%mm3, %%mm0"
                         "\n\t psrlw $2, %%mm0"
                         "\n\t packuswb %%mm6, %%mm0"
                         "\n\t movd %%mm0, %[buf]"
                         :: [buf] "m" (*(buf + i))
                         , [add1] "m" (*(buf + i + width))
                         , [add2] "m" (*(buf + i + width + 1))
                         , [add3] "m" (*(buf + i + width - 1))
                  );
                //       : "mm0", "mm1", "mm2", "mm3", "mm6");
        }

        for (i = len - 4; i > scrsh; i -= 4) {
                __asm __volatile
                        ("\n\t movd %[buf], %%mm0"
                         "\n\t movd %[add1], %%mm1"
                         "\n\t punpcklbw %%mm6, %%mm0"
                         "\n\t movd %[add2], %%mm2"
                         "\n\t punpcklbw %%mm6, %%mm1"
                         "\n\t movd %[add3], %%mm3"
                         "\n\t punpcklbw %%mm6, %%mm2"
                         "\n\t paddw %%mm1, %%mm0"
                         "\n\t punpcklbw %%mm6, %%mm3"
                         "\n\t paddw %%mm2, %%mm0"
                         "\n\t paddw %%mm3, %%mm0"
                         "\n\t psrlw $2, %%mm0"
                         "\n\t packuswb %%mm6, %%mm0"
                         "\n\t movd %%mm0, %[buf]"
                         :: [buf] "m" (*(buf + i))
                         , [add1] "m" (*(buf + i - width))
                         , [add2] "m" (*(buf + i - width + 1))
                         , [add3] "m" (*(buf + i - width - 1))
                );//     : "mm0", "mm1", "mm2", "mm3", "mm6");
        }

         __asm__ __volatile__ ( "emms":::"memory");

}



static	inline	double	calc( double n, double ip, double x)
{
	double res = x;
	if( n > 0.0 )
	{
		if(ip > 0.0)
		 res = (x + n ) * 0.5;
		else
	         res = n;
	}
	return res;
}


livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	int palette[3];
	int w[3];
	int h[3];
	int i,j;

	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[2],&h[2], O,&palette[2] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

#ifdef STRICT_CHECKING
	assert( A[0] != NULL );
	assert( A[1] != NULL );
	assert( A[2] != NULL );
	assert( O[0] != NULL );
	assert( O[1] != NULL );
	assert( O[2] != NULL );
	assert( O[3] != NULL );

#endif
	bg_t *b = NULL;
        error     = livido_property_get( my_instance, "PLUGIN_private", 0, &b );
#ifdef STRICT_CHECKING
	assert( error == NULL );
#endif

        double thres  = lvd_extract_param_number(
                                my_instance,
                                "in_parameters",
                                0 );
        double weight  = lvd_extract_param_number(
                                my_instance,
                                "in_parameters",
                                1 );
                              
        double wacf  = lvd_extract_param_number(
                                my_instance,
                                "in_parameters",
                                2 );

	int	reverse = lvd_extract_param_boolean(
				my_instance, "in_parameters", 3 );

	const int	threshold = (const int)(255.0 * thres);
	
	const int	wac       = (const int)(100.0 * wacf);
	
	const int len = w[0] * h[0];
	const int wid = w[0];
	const int hei = h[0];
	
	if( !b->empty )
	{
		livido_memcpy( b->tmpy, A[0], len );
		mmx_blur( b->tmpy, wid,hei );
		b->empty=1;
		return LIVIDO_NO_ERROR;
	}
	else
	{
		const uint8_t *prev = b->tmpy;
		uint8_t *diff = b->tmpu;
		unsigned int activity = 0;
		//@ construct difference
		//
		//
		mmx_blur( A[0], wid, hei );

		activity = binarify( diff, prev, A[0], threshold, reverse, len );

		livido_memcpy( prev, A[0], len );	
/*
		for( i = 0; i < len ; i ++ )
		{
			uint8_t a = A[0][i];
			if( a > prev[i] && abs( a - prev[i] ) > threshold)
			{	diff[i] = 0xff; activity ++; }
			else 
				diff[i] = 0;
			b->tmpy[i] = a;
			O[0][i] = diff[i];
		}*/
		unsigned int *ax_x = b->axis_x;
		unsigned int *ax_y = b->axis_y;

		double w = (double)len;
		
		
		if( activity )
			b->activity = (1.0 / len ) * (double) (activity * wac);		else
			b->activity = 0.0;

		//@ only interest in where MOST of the activity finds place
		livido_memset( ax_x , 0, wid * sizeof(unsigned int) );
		livido_memset( ax_y , 0, hei * sizeof(unsigned int) );
	
		for( i = 0;i < hei; i ++ )
		{
			int q = 0;
			for( j = 0; j < wid; j ++ )
			{
				int pos = i * wid + j;
				if( diff[pos] == 0xff )
				{	ax_x[j] += 1;	ax_y[i] += 1; }
			}
		}

		int x_i = _max( ax_x, hei );
		int y_i = _max( ax_y, hei );
		int ta=0.0,tb=0.0;

		if( x_i < 0 || y_i < 0 )
		{
			b->prev_x = 0;
			b->prev_y = 0;
			b->activity = 0.0;
			x_i = 0;
			y_i = 0;
		}
		else
		{
			if( b->prev_x > 0 && b->prev_y > 0 )
			{
				//@ direction X, direction Y
				ta = abs( x_i - b->prev_x);
				tb = abs( y_i - b->prev_y);
			}
			b->prev_x = x_i;
			b->prev_y = y_i;
		}

		if( b->activity > 1.0 )
			b->activity = 1.0;

		double norm[4];
		double values[4];
		double  r  = (double)wid/(double)hei;
		int max_ta = (int)( weight * (double) wid);
		int max_tb = (int)( weight * (double) hei * r);

		int da = ( ta > max_ta ? max_ta : ta );
		int db = ( tb > max_tb ? max_tb : tb );

		norm[0] = ( 1.0 / (double) wid ) * fabs(x_i);
		norm[1] = ( 1.0 / (double) hei ) * fabs(y_i);
		norm[2] = ( 1.0 / (double) max_ta  ) * (double) da;
		norm[3] = ( 1.0 / (double) max_tb  ) * (double) db;
	
	printf("  Measure {%g\t%g\t%g\t%g} %d\n", norm[0],norm[1],norm[2],norm[3], b->h);

		b->ip[0] = calc( norm[0], b->ip[0], b->lx );
		b->ip[1] = calc( norm[1], b->ip[1], b->ly );
		b->ip[2] = calc( norm[2], b->ip[2], b->wx );
		b->ip[3] = calc( norm[3], b->ip[3], b->wy );
	

		b->lx = b->ip[0];
		b->ly = b->ip[1];
		b->wx = b->ip[2];
		b->wy = b->ip[3];
		
		values[0] = b->lx;
		values[1] = b->ly;
		values[2] = b->wx;
		values[3] = b->wy;
	printf("  Set: {%g\t%g\t%g\t%g} | {%g\t%g\t%g\t%g}\n", values[0],values[1],values[2],values[3],
	     b->ip[0],b->ip[1],b->ip[2],b->ip[3] );		
	lvd_set_param_number(my_instance, "out_parameters",0,b->ip[0] );
	lvd_set_param_number(my_instance, "out_parameters",1,1.0 - b->ip[1] );

	lvd_set_param_number(my_instance, "out_parameters",2, b->ip[2] );
	lvd_set_param_number(my_instance, "out_parameters",3, b->ip[3] );


	//	for( i = 0; i < 4; i ++ )
	//		lvd_set_param_number(my_instance, "out_parameters",i, b->ip[i] );
		lvd_set_param_number( my_instance, "out_parameters", 4, b->activity );

	}
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);
        static struct
        {
                const char *name;
                const char *descr;
        } out_p[] = {
                {       "X pos",       "Most of motion is at [X,y]"  },
                {       "Y pos",         "Most of motion is at [Y,x]" },
                {       "Velocity X",        "Velocity X" },
                {       "Velocity Y",          "Velocity" },
                {       "Activity",       "Motion activity" },
                {       NULL,           NULL },
        };

	
        livido_port_t *out_params[8];
	livido_port_t *port = NULL;
	livido_port_t *in_params[5];
	livido_port_t *in_chans[2];
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

		livido_set_string_value( port, "name", "Simple Motion Sensor");	
		livido_set_string_value( port, "description", "Measure motion activity ,position and velocity");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
                     // 	LIVIDO_PALETTE_YUV420P,
                       //	LIVIDO_PALETTE_YUV422P,
			LIVIDO_PALETTE_YUV444P,
               		0
	};

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[0];

                livido_set_string_value(port, "name", "Threshold" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Difference threshold");
		
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Range" );
                livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.2 );

		livido_set_string_value( port, "description" ,"Scale coordinates to smaller range");
	
	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[2];

                livido_set_string_value(port, "name", "Multiply" );
                livido_set_string_value(port, "kind", "NUMBER" );
		livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.2 );

		livido_set_string_value( port, "description" ,"Multiply activity");
	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[3];

                livido_set_string_value(port, "name", "Invert" );
                livido_set_string_value(port, "kind", "SWITCH" );
                livido_set_int_value( port, "default", 0 );

		livido_set_string_value( port, "description" ,"Invert mask");


  	int i; 
	int np = 0;
        for( i = 0; out_p[i].name != NULL; i ++ )
        {       
                out_params[i] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
		port = out_params[i];
                livido_set_string_value(port, "name", out_p[i].name );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.0 );
                livido_set_string_value( port, "description" , out_p[i].descr);
		np ++;
        }
		
        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
            
                livido_set_string_value( port, "name", "Channel A");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	livido_set_portptr_array( filter, "in_channel_templates", 1 , in_chans );
	livido_set_portptr_array( filter, "in_parameter_templates",4, in_params );
	livido_set_portptr_array( filter, "out_parameter_templates",np, out_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
