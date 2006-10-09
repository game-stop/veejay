/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2006 Niels Elburg <nelburg@looze.net>
 * See COPYING for software license and distribution details
 */
/*
	This effect is based on this small project:

	http://www.cs.utah.edu/~michael/chroma/

	The algorithm decides which pixels belong to resp. foreground 
        or background. 
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



void rgbkeysmooth_apply( uint8_t *A[], uint8_t *B[], uint8_t *O[],
			int width,
			int height,
			float angle,
			int r,
			int g,
			int b,
			int opacity,
			float noise_level)
{
	uint8_t *fg_y, *fg_cb, *fg_cr;
	uint8_t *bg_y, *bg_cb, *bg_cr;

    	int accept_angle_tg, accept_angle_ctg, one_over_kc;
	int kfgy_scale, kg;

	int cb, cr;
	int kbgb,kbg, x1, y1;

	float kg1, tmp, aa = 128, bb = 128, _y = 0;
    	unsigned int pos;
    	uint8_t val, tmp1;
    	uint8_t *Y = A[0];
	uint8_t *Cb= A[1];
	uint8_t *Cr= A[2];
	uint8_t *Y2 = B[0];
 	uint8_t *Cb2= B[1];
	uint8_t *Cr2= B[2];

	int	iy=16,iu=128,iv=128;

	unsigned int op0 = opacity > 255 ? 255 : opacity;
	unsigned int op1 = 255 - op0;

	_rgb2yuv( r,g,b, iy,iu,iv );
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;

     	tmp = sqrt(((aa * aa) + (bb * bb)));
    	cb = 127 * (aa / tmp);
    	cr = 127 * (bb / tmp);
    	kg1 = tmp;

	/* obtain coordinate system for cb / cr */
	accept_angle_tg = 0xf * tan(M_PI * angle / 180.0);
	accept_angle_ctg = 0xf / tan(M_PI * angle / 180.0);

	tmp = 1 / kg1;
	one_over_kc = 0xff * 2 * tmp - 0xff;
	kfgy_scale = 0xf * (float) (_y) / kg1;
	kg = kg1;

	/* intialize pointers */
    	fg_y = Y;
    	fg_cb = Cb;
    	fg_cr = Cr;
	bg_y = Y2;
	bg_cb = Cb2;
    	bg_cr = Cr2;

	
    for (pos = (width * height); pos != 0; pos--) {

        short xx, yy;
	/* convert foreground to xz coordinates where x direction is
	   defined by key color */

	xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;

	if (xx < -128) {
	    xx = -128;
	}
	if (xx > 127) {
	    xx = 127;
	}

	yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

	if (yy < -128) {
	    yy = -128;
	}
	if (yy > 127) {
	    yy = 127;	
	}


	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

	val = (xx * accept_angle_tg) >> 4;
	if (val > 127)
	    val = 127;
	if (abs(yy) < val) {
	    val = (yy * accept_angle_ctg) >> 4;

	    x1 = abs(val);
	    y1 = yy;
	    tmp1 = xx - x1;

	    kbg = (tmp1 * one_over_kc) >> 1;
	    if (kbg < 0)
		kbg = 0;
	    if (kbg > 255)
		kbg = 255;

	    val = (tmp1 * kfgy_scale) >> 4;
	    val = fg_y[pos] - val;

	    O[0][pos] = val;
	    
	   // val = (Y[pos] + (kbg * bg_y[pos])) >> 8;

	    // convert suppressed fg back to cbcr 
		// cb,cr are signed, go back to unsigned !
	    val = ((x1 * (cb-128)) - (y1 * (cr-128))) >> 7;
	    O[1][pos] = val;

	    val = ((x1 * (cr-128)) - (y1 * (cb-128))) >> 7;
	    O[2][pos] = val;

	    val = (yy * yy) + (kg * kg);
            if (val < (noise_level * noise_level)) {
                O[0][pos] = 16;
		O[1][pos] = 128;
	       	O[2][pos] = 128;
                kbg = 255;
            }

	    O[0][pos] = (O[0][pos] + (kbg * bg_y[pos])) >> 8;
	    O[1][pos] = (O[1][pos] + (kbg * bg_cb[pos])) >> 8;
	    O[2][pos] = (O[2][pos] + (kbg * bg_cr[pos])) >> 8;


	}
	else
	{
		O[0][pos] = A[0][pos];
		O[1][pos] = A[1][pos];
		O[2][pos] = A[2][pos];
	}
    }

}


livido_process_f		process_instance( livido_port_t *my_instance, double timecode )
{
	int len =0;
	int i = 0;
	uint8_t *A[4] = {NULL,NULL,NULL,NULL};
	uint8_t *O[4]= {NULL,NULL,NULL,NULL};
	uint8_t *B[4] = { NULL,NULL,NULL,NULL };
	int palette[3];
	int w[3];
	int h[3];

	int error = lvd_extract_channel_values( my_instance, "in_channels", 0, &w[0], &h[0], A, &palette[0] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

	error = lvd_extract_channel_values( my_instance, "in_channels", 1, &w[1], &h[1], B, &palette[1] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky


	
	error	  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w[2],&h[2], O,&palette[2] );
	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_HARDWARE; //@ error codes in livido flanky

#ifdef STRICT_CHECKING
	assert( w[0] == w[1] );
	assert( h[0] == h[1] );
	assert( w[2] == w[1] );
	assert( h[2] == h[1] );
	assert( palette[0] == palette[1] );
	assert( palette[2] == palette[1] );
	assert( A[0] != NULL );
	assert( A[1] != NULL );
	assert( A[2] != NULL );
	assert( B[0] != NULL );
	assert( B[1] != NULL );
	assert( B[2] != NULL );
	assert( O[0] != NULL );
	assert( O[1] != NULL );
	assert( O[2] != NULL );
#endif

	int uv_len = lvd_uv_plane_len( palette[0],w[0],h[0] );
	len = w[0] * h[0];

	uint8_t op0;
	uint8_t op1;

	double degree = lvd_extract_param_number( my_instance, "in_parameters", 0 );	
	double red    =lvd_extract_param_number( my_instance, "in_parameters", 1 );	
	double green  = lvd_extract_param_number( my_instance, "in_parameters", 2 );	
	double blue   = lvd_extract_param_number( my_instance, "in_parameters", 3 );	
  	double noise  =lvd_extract_param_number( my_instance, "in_parameters", 4 );	



	rgbkeysmooth_apply(
			A,
			B,
			O,
			w[0],
			h[0],
			(float) degree,
			(int) (255.0 * red),
			(int) (255.0 * green),
			(int) (255.0 * blue),
			0,
			(float) noise );
	
	return LIVIDO_NO_ERROR;
}

livido_port_t	*livido_setup(livido_setup_t list[], int version)

{
	LIVIDO_IMPORT(list);

	livido_port_t *port = NULL;
	livido_port_t *in_params[6];
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

		livido_set_string_value( port, "name", "RGB Key");	
		livido_set_string_value( port, "description", "Replace foreground color with mixing channel");
		livido_set_string_value( port, "author", "Niels Elburg");
		
		livido_set_int_value( port, "flags", 0);
		livido_set_string_value( port, "license", "GPL2");
		livido_set_int_value( port, "version", 1);
	
	int palettes0[] = {
			LIVIDO_PALETTE_YUV444P,
               		0
	};

	in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[0];

                livido_set_string_value(port, "name", "Degrees" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 90.0 );
                livido_set_double_value( port, "default", 15.0 );
                livido_set_string_value( port, "description" ,"Degrees");

	//@ replace for COLOR kind once host supports this
	in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[1];

                livido_set_string_value(port, "name", "Red" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Red");

	in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[2];

                livido_set_string_value(port, "name", "Green" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Green");

	in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[3];

                livido_set_string_value(port, "name", "Blue" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 1.0 );
                livido_set_double_value( port, "default", 0.1 );
                livido_set_string_value( port, "description" ,"Blue");

	in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
        port = in_params[4];

                livido_set_string_value(port, "name", "Noise level" );
                livido_set_string_value(port, "kind", "NUMBER" );
                livido_set_double_value( port, "min", 0.0 );
                livido_set_double_value( port, "max", 255.0 );
                livido_set_double_value( port, "default", 0.7 );
                livido_set_string_value( port, "description" ,"Noise level");

		
        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[0];
            
                livido_set_string_value( port, "name", "Channel A");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	in_chans[1] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = in_chans[1];
            
                livido_set_string_value( port, "name", "Channel B");
           	livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);

	out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
	port = out_chans[0];
	
	        livido_set_string_value( port, "name", "Output Channel");
		livido_set_int_array( port, "palette_list", 1, palettes0);
		livido_set_int_value( port, "flags", 0);
	
	livido_set_portptr_array( filter, "in_channel_templates", 2 , in_chans );
	livido_set_portptr_array( filter, "out_parameter_templates",0, NULL );
	livido_set_portptr_array( filter, "in_parameter_templates",5, in_params );
	livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );

	livido_set_portptr_value(info, "filters", filter);
	return info;
}
