/* Bathroom Effect , ported from Veejay (http://veejay.sourceforge.net)
 *
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
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

#include <libvevo.h>
#include <stdlib.h>
#include <string.h>

/* 2 Input Channels, first channel used for Inplace */
vevo_channel_templ_t channelB =
{
	.name	= "Foreground",
	.help	= "To key in",
	.flags  = 0,
	.same_as = NULL,
	NULL,
	{ VEVO_YUV444P, VEVO_INVALID },
};

vevo_channel_templ_t channelA =
{
	.name	= "Background",
	.help	= "internal",
	.flags  = 0,
	.same_as= NULL,
	&channelB,
	{ VEVO_YUV444P, VEVO_INVALID },
};

/* 4 Parameters, 2 numeric parameters, 1 stringlist , 1 rgba 
*/

/* parameter 3 */
vevo_parameter_templ_t p_modes =
{
	.name   = "Mode",
	.help	= "string selection",
	.format = "%s",
	.hint  = VEVOP_HINT_GROUP,
	.arglen = 0,
	NULL,
};
/* parameter 2 */
vevo_parameter_templ_t p_noise =
{
	.name	=	"Noise Level",
	.help	=	"Noise suppression",
	.format	=	"%f",
	.hint	=	0,
	.arglen	=	1,
	&p_modes,
};
/* parameter 1 */
vevo_parameter_templ_t p_rgba =
{
	.name	=	"RGBA",
	.help	=	"RGB Color",
	.format	=	"%d %d %d %d",
	.hint	=	VEVOP_HINT_RGBA,
	.arglen	=	4,
	&p_noise,
};

/* parameter 0*/
vevo_parameter_templ_t p_degree =
{
	.name	=	"Degree",
	.help	=	"Color Acceptance",
	.format = 	"%f",
	.hint	=	VEVOP_HINT_NORMAL,
	.arglen = 	1,
	&p_rgba,
};



vevo_instance_templ_t	inst = 
{
	.name			=	"RGB Chroma Key",
	.author 		= 	"Niels Elburg <nelburg@looze.net>",
	.description 	= 		"Lot of bla bla",
	.license		=	"GPL",
	.version		=	"1.0",
	.vevo_version 	= 	"1.0",
	.flags			=	VEVOI_FLAG_REQUIRE_INPLACE | VEVOI_FLAG_CAN_DO_REALTIME,
};

static		void	process_rgbkey(
	vevo_frame_t *A,
	vevo_frame_t *B,
	const int rgba[4],
	double degrees,
	double noise )
{

	
}

int		process( vevo_instance_t *instance )
{
	vevo_frame_t	A;
	vevo_frame_t	B;
	int		err;
	double		degrees,noise;
	double		rgba_gkey[4];
	int		rgba_key[4];
	char		selected_mode[100];

	vevo_collect_frame_data( instance->in_channels[0], &A );
	vevo_collect_frame_data( instance->in_channels[1], &B );

	/* scale float parameter to 0-255 */
	vevo_get_property_as(	instance->in_params[1], VEVOP_VALUE, VEVO_INT, rgba_key);
	/* just get the float values */
	vevo_get_property( instance->in_params[1], VEVOP_VALUE, rgba_gkey);
	vevo_get_property_as( instance->in_params[2], VEVOP_VALUE, VEVO_DOUBLE, &noise );
	// vevo_get_property( instance->in_params[2], VEVOP_VALUE, &noise );
	vevo_get_property_as( instance->in_params[0], VEVOP_VALUE, VEVO_DOUBLE, &degrees );
	vevo_get_property( instance->in_params[3], VEVOP_VALUE, &selected_mode );

	/* print to show it worked */
	printf("\t\tParameter0 : Degrees %g\n", degrees);
	printf("\t\tParameter1 : RGBA  %g,%g,%g,%g \n", rgba_gkey[0],rgba_gkey[1],rgba_gkey[2],rgba_gkey[3]);
	printf("\t\tParameter1 : RGBA  %d,%d,%d,%d \n", rgba_key[0], rgba_key[1], rgba_key[2], rgba_key[3]);
	printf("\t\tParameter2 : Noise: %g\n", noise );
	printf("\t\tParameter3 : selected item '%s'\n", selected_mode );

	// todo


	return VEVO_ERR_SUCCESS;	
}

int		init( vevo_instance_t *instance )
{
	/* parameter 1 , RGBA values for each component */
	double	val[5][4] = {
		{	0.0	0.0,	0.0,	0.0,	}, /* min */
		{	1.0 ,   1.0,	1.0,	1.0,	}, /* max */
		{	0.0 ,	0.0,	1.0,	0.0,	}, /* default */
		{	0.01,	0.01,	0.01,	0.01,	}, /* step size */
		{	0.1,	0.1,	0.1,	0.1	}  /* page size */
	};
	
	/* build properties list for this parameter */ 
	vevo_init_parameter_values( instance->in_params[1],
			4,
			VEVO_DOUBLE,
			val,
			5,
			VEVOP_MIN, VEVOP_MAX, VEVOP_DEFAULT,
			VEVOP_STEP_SIZE,VEVOP_PAGE_SIZE);
			
	/* parameter 0, Degrees (single atom) */
	double	deg[5] = 
	{
		0.1,
		80.0,
		5.0,
		0.5,
		5.0,
	};

	vevo_init_parameter_values( instance->in_params[0],
			1,
			VEVO_DOUBLE,
			deg,
			5,
			VEVOP_MIN, VEVOP_MAX, VEVOP_DEFAULT,
			VEVOP_STEP_SIZE,VEVOP_PAGE_SIZE);

	/* parameter 2, Noise suppression (single atom) */
	double	noise[5] =
	{
		1.0,
		1000.0,
		500.,
		0.1,
		1.0,
	};

	vevo_init_parameter_values( instance->in_params[2],
		1,
		VEVO_DOUBLE,
		noise,
		5,
		VEVOP_MIN, VEVOP_MAX, VEVOP_DEFAULT,
		VEVOP_STEP_SIZE,VEVOP_PAGE_SIZE);

	/* parameter 3, Bogus Parameter that show a string list */
	char *stringlist[] = 
	{
		"addition\0" ,
		"difference\0" , 
		"substraction\0",
		"multiply\0",
		"transparency\0",
		NULL,
	};


	vevo_init_parameter_values( instance->in_params[3],
			5, /* number of items in string list */
			VEVO_STRING, /* type of data (can be double or int too for splines) */
			stringlist,   
			1, /* number of properties we want to initialize */
			VEVOP_LIST); /* property is of type list */


	/* setup default selected item */
	char *group_default = group[4];

	vevo_init_parameter_values( instance->in_params[3],
		1,
		VEVO_STRING,
		&group_default,
		1,
		VEVOP_DEFAULT);


	return VEVO_ERR_SUCCESS;	
}

int		deinit( vevo_instance_t *instance )
{
	return VEVO_ERR_SUCCESS;
}


vevo_instance_templ_t	*vevo_setup()
{
	inst.init 	= init;
	inst.deinit = deinit;
	inst.process= process;
	inst.next_keyframe = NULL;
	inst.prev_keyframe = NULL;
	inst.out_channels = NULL;
	inst.out_params = NULL;
	inst.in_channels = &channelA;
	inst.in_params = &p_degree;
	return &inst;
}  	
