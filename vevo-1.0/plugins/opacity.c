/* Opacity, effect ported from Veejay (http://veejay.sourceforge.net>
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

vevo_channel_templ_t channel1 = 
{
	.name	= "Input B",
	.help	= "Mixing in",
	.flags	= VEVOC_FLAG_FORMAT | VEVOC_FLAG_SIZE,
	.same_as = "Input A",
	NULL,
	{ VEVO_YUV422P, VEVO_YUV420P, VEVO_YUV444P, VEVO_INVALID },
};

vevo_channel_templ_t channel0 =
{
	.name	= "Input A",
	.help	= "Mixing in",	
	.flags	= 0,
	NULL,
	&channel1,
	{ VEVO_YUV420P, VEVO_YUV422P, VEVO_YUV444P, VEVO_INVALID },
};

vevo_parameter_templ_t parameter =
{
	.name	= "Opacity",
	.help	= "Set transparancy of channel between 0 and 100%",
	.format = NULL,
	.hint 	= VEVOP_HINT_TRANSITION,
	.flags  = 0,
	.arglen = 1,
	NULL,
};

vevo_instance_templ_t plugininfo
=
{
	.name			= "Normal Overlay",
	.author			= "Niels Elburg",
	.description 	= "Blends two frames",
	.license		=  "GPL",
	.version		=  "1.0",
	.vevo_version 	=  "1.0",
	.flags			=  VEVOI_FLAG_REQUIRE_INPLACE| VEVOI_FLAG_CAN_DO_REALTIME| VEVOI_FLAG_REQUIRE_STATIC_SIZES | VEVOI_FLAG_REQUIRE_STATIC_FORMAT,
};


int							 process( vevo_instance_t *inst ) 
{
	vevo_frame_t	A;
	vevo_frame_t	B;
	int 			i,err;
	int				uv_len,len;
	int				op0=0,op1=0;
	double				opacity;
	err = vevo_collect_frame_data( inst->in_channels[0], &A ); 
	if(err != VEVO_ERR_SUCCESS)
		return err;
	err	= vevo_collect_frame_data( inst->in_channels[1], &B );
	if(err != VEVO_ERR_SUCCESS) 
		return err;
	err	= vevo_get_property_as( inst->in_params[0], VEVOP_VALUE,VEVO_INT, &op0);
	if( err != VEVO_ERR_SUCCESS)
		return err;
	err	= vevo_get_property( inst->in_params[0], VEVOP_VALUE,&opacity );	
	if( err != VEVO_ERR_SUCCESS)
		return err;
	
	len = (A.width * A.height);

	uv_len = (A.width >> A.shift_h) * (A.height >> A.shift_v );

	op1 = 255 - op0;

	printf("\t\tParameter 0: Opacity %g\n", opacity);
	printf("\t\tParameter 0: Opacity scaled %d\n", op0);
	printf("\t\tFrame A (%p,%p,%p) \n\t\tFrame B (%p,%p,%p), %d  %d :\n\t\topacity A = %d, B = %d\n",
		A.data_u8[0], A.data_u8[1], A.data_u8[2],
		B.data_u8[0], B.data_u8[1], B.data_u8[2],	
		len,
		uv_len,
		op0, op1 );
	
	for( i = 0; i <len ; i ++ )
	{
		A.data_u8[0][i] = ( (A.data_u8[0][i] * op0) + (B.data_u8[0][i] * op1) ) >> 8; 
	}
	for( i = 0; i < uv_len ; i++ )
	{
		A.data_u8[1][i] = ( (B.data_u8[1][i] * op0) + (B.data_u8[1][i] * op1) ) >> 8;
		A.data_u8[2][i] = ( (B.data_u8[2][i] * op0) + (B.data_u8[2][i] * op1) ) >> 8;
	}	

	return VEVO_ERR_SUCCESS;
}

int							deinit( vevo_instance_t *i )
{
	return 0;
}

int							init( vevo_instance_t *i )
{
	double op[5] =
		{
		0.0,
		1.0,
		0.5,
		0.25,
		1.0
		};

	vevo_init_parameter_values( i->in_params[0], 1, VEVO_DOUBLE, op, 5,
		VEVOP_MIN,VEVOP_MAX,VEVOP_DEFAULT,
		VEVOP_STEP_SIZE, VEVOP_PAGE_SIZE );

	return 0;
}

vevo_instance_templ_t 	*vevo_setup()
{
	plugininfo.init = init;
	plugininfo.deinit = deinit;
	plugininfo.process = process;
	plugininfo.next_keyframe = NULL;
	plugininfo.prev_keyframe = NULL;
	plugininfo.out_channels = NULL;
	plugininfo.out_params = NULL;
	plugininfo.in_channels = &channel0;
	plugininfo.in_params = &parameter;
	return &plugininfo;
}

