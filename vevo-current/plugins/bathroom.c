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

vevo_channel_templ_t channel =
{
	.name	= "Input A",
	.help	= "internal",
	.flags  = 0,
	.same_as= NULL,
	NULL,
	{ VEVO_YUV444P, VEVO_INVALID },
};

vevo_parameter_templ_t parameter1 = 
{
	.name	=	"Bars",
	.help	=	"Number of Bathroom bars",
	.format = 	"%d",
	.hint	=	0,
	.arglen = 	1,
	NULL,
};


vevo_parameter_templ_t parameter0 =
{
	.name	=	"Orientation",
	.help	=	"Vertical/Horizontal bathroom window",
	.format =   "%d",
	.hint   =   0,
	.arglen =   1,
	&parameter1,
};


vevo_instance_templ_t	inst = 
{
	.name			=	"Bathroom Window",
	.author 		= 	"Niels Elburg <nelburg@looze.net>",
	.description 	= 	"As if you were looking through the bathroom window",
	.license		=	"GPL",
	.version		=	"1.0",
	.vevo_version 	= 	"1.0",
	.flags			=	VEVOI_FLAG_REQUIRE_INPLACE | VEVOI_FLAG_CAN_DO_REALTIME,
};

typedef struct
{
	uint8_t *data[3];
} priv_data;

static		void	process_verti( vevo_frame_t *A, priv_data *pd, const int bars )
{
	int x,y,i;
	uint8_t *l,*cb,*cr;
	int len = A->height * A->width;
	l = A->data_u8[0];
	cb= A->data_u8[1];
	cr= A->data_u8[2];

	for( y = 0; y < A->height; y ++ )
	{
		for(x = 0; x < A->width; x ++ )
		{
			i = ( x + ( x % bars ) - ( bars >> 1 ) ) + ( y * A->width );
			if( i < 0 ) i = 0;
			if( i >= len ) i = len - 1;
			 l[( y * A->width + x )] = pd->data[0][i];
			cb[( y * A->width + x )] = pd->data[1][i];
			cr[( y * A->width + x )] = pd->data[2][i];
		}
	}
}

static		void	process_hori( vevo_frame_t *A, priv_data *pd, const int bars )
{
	int x,y,i;
	uint8_t *l,*cb,*cr;

	l = A->data_u8[0];
	cb= A->data_u8[1];
	cr= A->data_u8[2];

	for( y = 0; y < A->height; y ++ )
	{
		for(x = 0; x < A->width; x ++ )
		{
			i = (( y * A->width) + ( y % bars ) - ( bars >> 1 ) ) + x;
			if( i < 0 ) i += A->width;
			 l[( y * A->width + x )] = pd->data[0][i];
			cb[( y * A->width + x )] = pd->data[1][i];
			cr[( y * A->width + x )] = pd->data[2][i];
		}
	}
}

int		process( vevo_instance_t *instance )
{
	vevo_frame_t	A;
	priv_data	*pd; 
	int		err;
	int 		p0,p1;
	int		len;

	err = vevo_collect_frame_data( instance->in_channels[0], &A );
	if( err != VEVO_ERR_SUCCESS )
		return err;

	err = vevo_get_property_as(	instance->in_params[0], VEVOP_VALUE, VEVO_INT, &p0 );
	if( err != VEVO_ERR_SUCCESS)
		return err;

	err = vevo_get_property_as( instance->in_params[1], VEVOP_VALUE, VEVO_INT, &p1 );
	if( err != VEVO_ERR_SUCCESS) 
		return err;

	err = vevo_get_property( instance->self, VEVOI_PRIVATE, &pd );
	if( err != VEVO_ERR_SUCCESS)
		return err;

	len = A.width * A.height;

	// inplace,so we must copy the buffer first.
	memcpy( pd->data[0], A.data_u8[0], len );
	memcpy( pd->data[1], A.data_u8[1], len );
	memcpy( pd->data[2], A.data_u8[2], len );

	if( p0 == 0 )
		process_verti( &A,pd, p1 );
	if( p0 == 1 )
		process_hori( &A,pd, p1 );
	
	return VEVO_ERR_SUCCESS;	
}

int		init( vevo_instance_t *instance )
{
	int 	width;
	int	height;
	int	err;

	priv_data *pd = (priv_data*) malloc(sizeof(priv_data));

	err = vevo_get_property_as( instance->in_channels[0], VEVOC_WIDTH, VEVO_INT, &width );
	if(err != VEVO_ERR_SUCCESS)
		return err;

	err = vevo_get_property_as( instance->in_channels[0], VEVOC_HEIGHT, VEVO_INT, &height );
	if( err != VEVO_ERR_SUCCESS)
		return err;

	pd->data[0] = (uint8_t*) malloc( sizeof(uint8_t) * width * height );
	pd->data[1] = (uint8_t*) malloc( sizeof(uint8_t) * width * height );
	pd->data[2] = (uint8_t*) malloc( sizeof(uint8_t) * width * height );
	
	vevo_set_property( instance->self, VEVOI_PRIVATE, VEVO_PTR_VOID, 1, &pd );

	return VEVO_ERR_SUCCESS;	
}

int		deinit( vevo_instance_t *instance )
{
	priv_data *pd;
	int err = vevo_get_property( instance->self, VEVOI_PRIVATE, &pd );
	if(err != VEVO_ERR_SUCCESS)
		return err;
	
	if( pd->data[0] ) free( pd->data[0] );
	if( pd->data[1] ) free( pd->data[1] );
	if( pd->data[2] ) free( pd->data[2] );

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
	inst.in_channels = &channel;
	inst.in_params = &parameter0;
	return &inst;
}  	
