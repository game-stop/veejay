/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <string.h>
#include <libvevo/vevo.h>
#include <veejay/vj-event.h>
#include <veejay/vims.h>
#include <veejay/vevo.h>
#include <libvevo/libvevo.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>

#define MAX_INDEX 1024

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)			/* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)		/* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)				/* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)				/* use defaults when optional arguments are not given */	
#define	livido_port_t vevo_port_t

#define	SAMPLE_ID_HELP	"Sample ID (0=current playing, -1=last created, > 0 = Sample ID)"
#define	STREAM_ID_HELP	"Stream ID (-1=last created, > 0 = Stream ID)"
#define	SAMPLE_STREAM_ID_HELP	"Sample or Stream ID (0=current playing, -1=last created, > 0 = ID)"
static	vevo_port_t **index_map_ = NULL;
/* define the function pointer to any event */
typedef void (*vevo_event)(void *ptr, const char format[], va_list ap);

static	void		dump_event_stderr(vevo_port_t *event)
{
	char *fmt = NULL;
	char *name = NULL;	
	int  n_arg = 0;
 	int  vims_id = 0;
	char *param = NULL;
	int i;
	char key[16];

	size_t len = vevo_property_element_size(event, "format", 0 );
	if(len > 0 )
	{
		fmt = vj_malloc(sizeof(char) * len);
		vevo_property_get( event, "format", 0, &fmt );
	}

	name = vj_malloc(sizeof(char) * vevo_property_element_size( event, "description", 0 ));
	vevo_property_get( event, "description", 0, &name );
	vevo_property_get( event, "arguments", 0, &n_arg );
	vevo_property_get( event, "vims_id", 0, &vims_id );

	veejay_msg(VEEJAY_MSG_INFO, "VIMS selector %03d\t'%s'", vims_id, name );  
	if(fmt)
		veejay_msg(VEEJAY_MSG_INFO, "\tFORMAT: '%s', where:", fmt );
	
	for( i = 0; i < n_arg; i ++ )
	{
		snprintf(key,sizeof(key), "help_%d", i );
		size_t len2 = vevo_property_element_size( event, key, 0 );
		if(len2 > 0 )
		{
			param = vj_malloc(sizeof(char) * len2 );
			vevo_property_get( event, key, 0, &param );
			veejay_msg(VEEJAY_MSG_INFO,"\t\tArgument %d is %s", i, param );
			free(param);
		}
	}
	
	if(fmt) free(fmt);
	free(name);
}

int	vj_event_vevo_list_size(void)
{
	int i;	
	int len =0;
	for ( i = 0; i < MAX_INDEX ;i ++ )
	{
		if( index_map_[i] != NULL )
		{
			char *name  = vj_event_vevo_get_event_name( i );
			char *format= vj_event_vevo_get_event_format( i ); 
			len += (name == NULL ?  0: strlen( name ));
			len += (format == NULL ? 0: strlen( format ));
			len += 12;	
			if(name) free(name);
			if(format)free(format);
		}
	}
	return len;
}

char	*vj_event_vevo_help_vims( int id, int n )
{
	char *help = NULL;
	char key[10];
	sprintf(key, "help_%d", n);
	size_t len = vevo_property_element_size( index_map_[id], key, 0  );
	if(len > 0 )
	{
		help = (char*) vj_malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], key, 0, &help );
	}
	return help;
}

char	*vj_event_vevo_list_serialize(void)
{
	int len = vj_event_vevo_list_size() + 5;
	char *res = (char*) vj_calloc(sizeof(char) * len + 100 );
	int i;
	sprintf(res, "%05d", len  - 5);
	for ( i = 0; i < MAX_INDEX ;i ++ )
	{
		if ( index_map_[i] != NULL )
		{
			char *name  = vj_event_vevo_get_event_name( i );
			char *format= vj_event_vevo_get_event_format( i ); 
			int name_len = (name == NULL ?  0: strlen( name ));
			int fmt_len  = (format == NULL? 0: strlen( format ));
			char tmp[16];
			snprintf( tmp,sizeof(tmp),"%04d%02d%03d%03d",
				i, vj_event_vevo_get_num_args(i), fmt_len, name_len );
			veejay_strncat( res, tmp, 12 );
			if( format != NULL )
				veejay_strncat( res, format, fmt_len );
			if( name != NULL )
				veejay_strncat( res, name, name_len );
			if(name) free(name);
			if(format) free(format);
			
		}
	}
	return res;
}

void	vj_event_vevo_inline_fire(void *super, int vims_id, const char *format, ... )
{
	va_list ap;
	va_start( ap, format );
	void *func = NULL;
	if( vevo_property_get( index_map_[vims_id], "function", 0, &func ) == VEVO_NO_ERROR )
	{
		vevo_event f = (vevo_event) func;
		f( super, format, ap );
	}
	va_end( ap );
}

void		vj_event_vevo_inline_fire_default( void *super, int vims_id, const char *format )
{
	char key[16];
	int i = 0;
	int n = 0;
	int dval[4] = {0,0,0,0};
	if(!index_map_[vims_id])
	{
		veejay_msg(0, "No such event: %d", vims_id);
		return;
	}
	vevo_property_get( index_map_[vims_id] , "arguments", 0, &n );
	// dangerous, dval != atom_type, i != n defaults
	while( i < n )
	{
		snprintf(key,sizeof(key), "argument_%d", i );
		vevo_property_get( index_map_[vims_id], key, 0, &dval[i] );
		i++;
	}
	vj_event_vevo_inline_fire( super, vims_id, format, &dval[0],&dval[1],&dval[2],&dval[3]);
}	

static vevo_port_t	*_new_event(
		const char *format,
		int vims_id,	
		const char *name,
		void *function,
		int n_arg,
		int flags,
		... )
{
	int n = 0;
	int it = 1;
	char param_name[32];
	char descr_name[255];

	vevo_port_t *p = (void*) vpn( VEVO_EVENT_PORT );
	if( format )
		vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING, 1, &format );
	else
		vevo_property_set( p, "format", VEVO_ATOM_TYPE_STRING, 0, NULL );

	vevo_property_set( p, "description", VEVO_ATOM_TYPE_STRING, 1, &name );
	vevo_property_set( p, "function", VEVO_ATOM_TYPE_VOIDPTR, 1,&function );
	vevo_property_set( p, "arguments", VEVO_ATOM_TYPE_INT, 1, &n_arg );
	vevo_property_set( p, "flags",	VEVO_ATOM_TYPE_INT, 1, &flags );
	vevo_property_set( p, "vims_id", VEVO_ATOM_TYPE_INT, 1, &vims_id );

	va_list ap;
	va_start(ap, flags);

	for( n = 0; n < n_arg ; n ++)
	{
		int dd   = 0;
		char *ds = NULL;

		snprintf(param_name,sizeof(param_name), "argument_%d", n );
		const char *arg = va_arg( ap, const char*);
		char *descr = (char*) vj_strdup( arg );
		snprintf(descr_name,sizeof(descr_name), "help_%d", n );
		
		if (format[it] == 'd')
		{
			dd = va_arg( ap, int );
			vevo_property_set( p, param_name, VEVO_ATOM_TYPE_INT,1, &dd );
		}
		else
		{
			ds = va_arg( ap, char*);
			if(!ds)
			 vevo_property_set( p, param_name, VEVO_ATOM_TYPE_STRING, 0, NULL );
			else
			 vevo_property_set( p, param_name, VEVO_ATOM_TYPE_STRING,1, &ds );
		}

		vevo_property_set( p, descr_name, VEVO_ATOM_TYPE_STRING, 1,&descr );

		it += 3;

		if( ds )
			free( ds);
		if( descr )
			free( descr );
	}

	va_end(ap);

	return p;
}

void *		vj_event_vevo_get_event_function( int id )
{
	void *func = NULL;
	if( index_map_[id] )
		vevo_property_get( index_map_[id] , "function", 0, &func );
	return func;
}

char	*vj_event_vevo_get_event_name( int id )
{
	char *descr = NULL;
	if( index_map_[id] == NULL )
		return NULL;

	size_t len = vevo_property_element_size( index_map_[id], "description", 0  );
	if(len > 0 )
	{
		descr = (char*) vj_malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], "description", 0, &descr );
	}
	return descr;
}
char	*vj_event_vevo_get_event_format( int id )
{
	char *fmt = NULL;
	if(!index_map_[id])
		return NULL;
	size_t len = vevo_property_element_size( index_map_[id], "format", 0 );
	if(len > 0 )
	{
		fmt = (char*) vj_malloc(sizeof(char) * len );
		vevo_property_get( index_map_[id], "format", 0, &fmt );
	}
	return fmt;
}

int	vj_event_exists( int id )
{
	if( index_map_[id])
		return 1;
	return 0;	
}


int	vj_event_vevo_get_default_value(int id, int p)
{
	int n =0;
	if(!index_map_[id])
		return 0;
	char key[16];
	snprintf(key,sizeof(key), "argument_%d",p);
	vevo_property_get(index_map_[id], key, 0, &n );
	return n;
}
int	vj_event_vevo_get_num_args(int id)
{
	if(!index_map_[id])
		return 0;
	int n =0;
	vevo_property_get(index_map_[id], "arguments", 0, &n );
	return n;
}
int		vj_event_vevo_get_flags( int id )
{
	if(!index_map_[id])
		return 0;
	int flags = 0;
	vevo_property_get( index_map_[id], "flags", 0, &flags );
	return flags;
}

int		vj_event_vevo_get_vims_id( int id )
{
	if(!index_map_[id])
		return 0;
	int vims_id = 0;
	vevo_property_get( index_map_[id], "vims_id", 0, &vims_id );
	return vims_id;
}

void		vj_event_vevo_dump(void)
{
	int i;
	veejay_msg(VEEJAY_MSG_INFO, "VIMS  Syntax: '<selector>:<arguments>;'");
	veejay_msg(VEEJAY_MSG_INFO, "Use arguments according to FORMAT");
	veejay_msg(VEEJAY_MSG_INFO, "FORMAT controls the arguments as in C printf. Interpreted sequences are:");
	veejay_msg(VEEJAY_MSG_INFO, "\t%%d\tinteger");
	veejay_msg(VEEJAY_MSG_INFO, "\t%%s\tstring");
	
	for( i = 0; i < MAX_INDEX; i ++ )
	{
		if( index_map_[i] )
		{
			dump_event_stderr( index_map_[i] );
		}
	}
}

void		vj_event_vevo_free(void)
{
	unsigned int i;
	if( index_map_ ) {
		for( i = 0 ; i < MAX_INDEX  ; i ++ ) {
		  if( index_map_[i] ) vpf( index_map_[i] );
		} 
		free(index_map_);
	}
}	

void		vj_init_vevo_events(void)
{	
	index_map_ = (vevo_port_t**) vj_calloc(sizeof(vevo_port_t*) * MAX_INDEX );

	index_map_[VIMS_MACRO] = _new_event(
				"%d %d",
				VIMS_MACRO,
				"Macro keystroke recorder/playback",
				vj_event_set_macro_status,
				2,
				VIMS_ALLOW_ANY,
				"Keep or reset (1=reset)",
				1,
				"Macro status (0=disabled,1=record,2=playing)",
				0,
				NULL );

	index_map_[VIMS_MACRO_SELECT] = _new_event(
				"%d",
				VIMS_MACRO_SELECT,
				"Select a bank to store macro keystrokes",
				vj_event_select_macro,
				1,
				VIMS_ALLOW_ANY,
				"Bank ID",
				0,
				NULL );

	index_map_[VIMS_VIDEO_PLAY_FORWARD] = _new_event(  
				NULL,
				VIMS_VIDEO_PLAY_FORWARD,
				"Play forward",
				vj_event_play_forward,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_VIDEO_PLAY_BACKWARD]	= 	_new_event(
				NULL,
				 VIMS_VIDEO_PLAY_BACKWARD,
				"Play backward",
				vj_event_play_reverse,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_VIDEO_PLAY_STOP]	=	_new_event(
				NULL,
				VIMS_VIDEO_PLAY_STOP,
				"Play stop",
				vj_event_play_stop,
				0,	
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_VIDEO_SKIP_FRAME]	=	_new_event(
				"%d",
				VIMS_VIDEO_SKIP_FRAME,
				"Skip N frames forward",
				vj_event_inc_frame,
				1,
				VIMS_ALLOW_ANY,
				"Number of frames", // param label
				1,		 // default
				NULL );

	index_map_[VIMS_SAMPLE_SKIP_FRAME]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SKIP_FRAME,
				"Skip N frames forward or backward",
				vj_event_sample_skip_frame,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
                                SAMPLE_ID_HELP,
                                0,
                                "Increment value",
                                0,
                                NULL );

	index_map_[VIMS_VIDEO_PREV_FRAME]	=	_new_event(
				"%d",
				VIMS_VIDEO_PREV_FRAME,
				"Skip N frames backward",
				vj_event_dec_frame,
				1,
				VIMS_ALLOW_ANY,
				"Number of frames",
				1,
				NULL );

	index_map_[VIMS_VIDEO_SKIP_SECOND]	=	_new_event(
				"%d",
				VIMS_VIDEO_SKIP_SECOND,
				"Skip N seconds forward",
				vj_event_next_second,
				1,
				VIMS_ALLOW_ANY,
				"Number of seconds",
				1,
				NULL );

	index_map_[VIMS_VIDEO_PREV_SECOND]	=	_new_event(
				"%d",
				VIMS_VIDEO_PREV_SECOND,
				"Skip N seconds backward",
				vj_event_prev_second,
				1,
				VIMS_ALLOW_ANY,
				"Number of seconds",
				1,
				NULL );

	index_map_[VIMS_VIDEO_GOTO_START]	=	_new_event(
				NULL,
				VIMS_VIDEO_GOTO_START,
				"Go to starting position",
				vj_event_goto_start,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_VIDEO_GOTO_END]		=	_new_event(	
				NULL,
				VIMS_VIDEO_GOTO_END,
				"Go to ending position",
				vj_event_goto_end,
				0,
				VIMS_ALLOW_ANY,		
				NULL );

	index_map_[VIMS_VIDEO_SET_SPEEDK] 	= 	_new_event(
				"%d",
				VIMS_VIDEO_SET_SPEED,
				"Change trickplay speed depending on play direction",
				vj_event_play_speed_kb,
				1,
				VIMS_ALLOW_ANY,
				"Frame step",	
				1,	
				NULL );

	index_map_[VIMS_VIDEO_SET_SPEED] 	= 	_new_event(
				"%d",
				VIMS_VIDEO_SET_SPEED,
				"Change trickplay speed",
				vj_event_play_speed,
				1,
				VIMS_ALLOW_ANY,
				"Frame step",	
				1,	
				NULL );

	index_map_[VIMS_VIDEO_SET_SLOW] 	= 	_new_event(
				"%d",
				VIMS_VIDEO_SET_SLOW,
				"Change frameduplication",
				vj_event_play_slow,
				1,
				VIMS_ALLOW_ANY,
				"Frame repeat",
				0,
				NULL );

	index_map_[VIMS_VIDEO_SET_FRAME]	= 	_new_event(
				"%d",
				VIMS_VIDEO_SET_FRAME,
				"Set current frame number",
				vj_event_set_frame,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Frame number",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_UP]		= 	_new_event(
				"%d",
				VIMS_CHAIN_ENTRY_UP,
				"Increment current FX chain entry",
				vj_event_entry_up,
				1,
				VIMS_ALLOW_ANY,
				"Increment value",
				1,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_DOWN] 	= 	_new_event(
				"%d",
				VIMS_CHAIN_ENTRY_DOWN,
				"Decrement current FX chain entry",
				vj_event_entry_down,
				1,
				VIMS_ALLOW_ANY,
				"Decrement value",
				-1,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_CHANNEL_INC]	= 	_new_event(
				"%d",
				VIMS_CHAIN_ENTRY_CHANNEL_INC,
				"Increment current Channel ID on selected chain entry",
				vj_event_chain_entry_channel_inc,
				1,
				VIMS_ALLOW_ANY,
				"Increment vale",
				1,	
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_CHANNEL_DEC] 	= 	_new_event(
				"%d",
				VIMS_CHAIN_ENTRY_CHANNEL_DEC,
				"Decrement current Channel ID on selected chain entry",
				vj_event_chain_entry_channel_dec,
				1,
				VIMS_ALLOW_ANY,
				"Decrement value",
				1,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SOURCE_TOGGLE]	= 	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_SOURCE_TOGGLE,
				"Change source type of a chain entry",
 				vj_event_chain_entry_src_toggle,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Chain entry",
				1,
				"Source type (0=Sample, 1=Stream)",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_INC_ARG] 	= 	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_INC_ARG,
				"Increment current value of a parameter",
				vj_event_chain_arg_inc,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Parameter number",	
				0,
				"Step size",
				0,
				NULL );



	index_map_[VIMS_CHAIN_ENTRY_DEC_ARG]	=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_DEC_ARG,
				"Decrement current value of a parameter",
				vj_event_chain_arg_inc,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Parameter number",
				0,
				"Step size",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_STATE]		=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_SET_STATE,
				"Enable / disable effect on current entry",
				vj_event_chain_entry_video_toggle,
				0,	
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_CHAIN_TOGGLE]			=	_new_event(
				NULL,
				VIMS_CHAIN_TOGGLE,
				"Enable / disable Effect Chain",
				vj_event_chain_toggle,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SET_SAMPLE_START]		=	_new_event(
				NULL,
				VIMS_SET_SAMPLE_START,
				"Store current frame as starting position of new sample",
				vj_event_sample_start,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SAMPLE_KF_STATUS]		=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_KF_STATUS,
				"Change KF play status for entry X",
				vj_event_set_kf_status,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				"Entry ID",
				0,
				"Status value",
				0,
				"Curve type",
				0,
				NULL );
	index_map_[VIMS_SAMPLE_KF_RESET]		=	_new_event(
				"%d",
				VIMS_SAMPLE_KF_STATUS,
				"Clear KF series on entry X",
				vj_event_reset_kf,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Entry ID",
				0,
				NULL );
	index_map_[VIMS_SAMPLE_KF_GET]		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_KF_GET,
				"Get keyframes for parameter Y on entry X",
				vj_event_get_keyframes,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Entry ID",
				0,
				"Parameter ID",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_KF_CLEAR]		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_KF_CLEAR,
				"Clear Animted FX parameter",
				vj_event_del_keyframes,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Entry ID",
				0,
				"Parameter ID",
				0,
				NULL );


	index_map_[VIMS_SET_SAMPLE_END] 		=	_new_event(
				NULL,
				VIMS_SET_SAMPLE_END,
				"Store current frame as ending position of a new sample ( and commit )",
				vj_event_sample_end,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_FXLIST_INC]	=	_new_event(
				"%d",
				VIMS_FXLIST_INC,
				"Increment index of Effect List",
				vj_event_effect_inc,
				1,
				VIMS_ALLOW_ANY,
				"Step size",
				1,	
				NULL );

	index_map_[VIMS_FXLIST_DEC]	= 	_new_event(
				"%d",
				VIMS_FXLIST_DEC,
				"Decrement index of Effect List",
				vj_event_effect_dec,
				1,	
				VIMS_ALLOW_ANY,
				"Step size",
				1,
				NULL );
	index_map_[VIMS_FXLIST_ADD]	=	_new_event(
				NULL,
				VIMS_FXLIST_ADD,
				"Put selected effect in Effect List to current sample and current entry",
				vj_event_effect_add,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SELECT_BANK]	=	_new_event(
				"%d",
				VIMS_SELECT_BANK,
				"Set current sample bank",
				vj_event_select_bank,
				1, 
				VIMS_ALLOW_ANY,
				"Bank number",
				0,
				NULL );
	index_map_[VIMS_SELECT_ID]	= 	_new_event(
				"%d",
				VIMS_SELECT_ID,
				"Play stream or sample slot (depends on current playmode)",
				vj_event_select_id,
				1,
				VIMS_ALLOW_ANY,
				"Slot number",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_RAND_START]	=	_new_event(
				"%d",
				VIMS_SAMPLE_RAND_START,
				"Start sample randomizer",
				vj_event_sample_rand_start,
				1,
				VIMS_ALLOW_ANY,
				"Mode (0=Random duration, 1=Sample duration)",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_RAND_STOP]	=	_new_event(
				NULL,
				VIMS_SAMPLE_RAND_STOP,
				"Stop sample randomizer",
				vj_event_sample_rand_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SAMPLE_TOGGLE_LOOP]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_TOGGLE_LOOP,
				"Switch between loop types",
				vj_event_sample_set_loop_type,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Looptype (0=None, 1=Normal, 2=Pingpong)",
				-1,
				NULL);

	index_map_[VIMS_PREVIEW_BW]	=	_new_event(
				NULL,
				VIMS_PREVIEW_BW,
				"Toggle grayscale preview on/off (default=off)",
				vj_event_toggle_bw,	
				0,
				VIMS_ALLOW_ANY,
				NULL,
				NULL );	
	index_map_[VIMS_RECORD_DATAFORMAT]	=	_new_event(
				"%s",
				VIMS_RECORD_DATAFORMAT,
				"Set codec to use for recording (global setting)",
				vj_event_tag_set_format,	
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Codec name (use 'x' to see list)",
				NULL,
				NULL );	

	index_map_[VIMS_REC_AUTO_START]		=	_new_event(
				NULL,
				VIMS_REC_AUTO_START,
				"Start recording now and play when finished",
				vj_event_misc_start_rec_auto,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_REC_START]		=	_new_event(
				NULL,
				VIMS_REC_START,
				"Start recording",
				vj_event_misc_start_rec,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_REC_STOP]		=	_new_event(
				NULL,
				VIMS_REC_STOP,
				"Stop recording",	
				vj_event_misc_stop_rec,
				0,
				VIMS_ALLOW_ANY ,
				NULL );

	index_map_[VIMS_SAMPLE_NEW] 		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_NEW,
				"Create a new sample",
				vj_event_sample_new,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Starting position",
				0,
				"Ending position",
				-1,
				NULL );

	index_map_[VIMS_PRINT_INFO]		=	_new_event(
				"%d",
				VIMS_PRINT_INFO,
				"Print current settings",
				vj_event_print_info,
				1,
				VIMS_ALLOW_ANY,
				"Sample or Stream ID (depends on playmode, 0=current playing)",
				0,
				NULL );

	index_map_[VIMS_SET_PLAIN_MODE]		=	_new_event(
				"%d",
				VIMS_SET_PLAIN_MODE,
				"Change playback mode",
				vj_event_set_play_mode,
				1,
				VIMS_ALLOW_ANY,	
				"Playback (2=plain,1=stream,0=sample)",
				2,
				NULL );

	index_map_[VIMS_SAMPLE_SET_LOOPTYPE]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_LOOPTYPE,
				"Change looptype of sample",
				vj_event_sample_set_loop_type,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Looptype (0=None,1=Normal,2=Pingpong)",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_SET_SPEED]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_SPEED,
				"Change playback speed of sample",
				vj_event_sample_set_speed,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Speed (0=pause, > 0  and < (end-start)",
				1,
				NULL );

	index_map_[VIMS_SAMPLE_HOLD_FRAME]	=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_HOLD_FRAME,
				"Hold frame (pause and resume from frame position + pause duration)",
				vj_event_hold_frame,
				3,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				"Minimum delay offset (0=1 frame)",
				0,
				"Initial delay ( > 4 frames < 300 )",
				5,
				NULL );

	index_map_[VIMS_SAMPLE_NEXT] 		= _new_event(
				NULL,
				VIMS_SAMPLE_NEXT,
				"Play next sample in queue",
				vj_event_sample_next,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SAMPLE_SET_POSITION]	=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_SET_POSITION,
				"Change mixing position of sample",
				vj_event_sample_set_position,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Chain entry",
				-1,
				"Relative position",
				-1,
				NULL );


	index_map_[VIMS_SAMPLE_SET_DESCRIPTION]	=	_new_event(
				"%d %s",
				VIMS_SAMPLE_SET_DESCRIPTION,
				"Change title of sample",
				vj_event_sample_set_descr,
				2,
				VIMS_REQUIRE_ALL_PARAMS |  VIMS_LONG_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Title",
				NULL,
				NULL );

	index_map_[VIMS_SAMPLE_SET_END]		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_END,
				"Change end position of sample",
				vj_event_sample_set_end,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Frame number",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SET_START]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_START,
				"Change start position of sample",
				vj_event_sample_set_start,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Frame number",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SET_DUP]		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_DUP,
				"Change frame repeat for this sample",
				vj_event_sample_set_dup,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Frame repeat",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SET_MARKER_START]	=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_MARKER_START,
				"Set in point in sample",
				vj_event_sample_set_marker_start,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Position",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SET_MARKER_END]		=	_new_event(
				"%d %d",
				VIMS_SAMPLE_SET_MARKER_END,
				"Set out point in sample",
				vj_event_sample_set_marker_end,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Position",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SET_MARKER]		=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_SET_MARKER,
				"Set in and out points in sample",
				vj_event_sample_set_marker,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Starting position",
				0,
				"Ending position",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_CLEAR_MARKER]		=	_new_event(
				"%d",
				VIMS_SAMPLE_CLEAR_MARKER,
				"Clear in and out points",
				vj_event_sample_set_marker_clear,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );
#ifdef HAVE_XML2
	index_map_[VIMS_SAMPLE_LOAD_SAMPLELIST]		=	_new_event(
				"%s",
				VIMS_SAMPLE_LOAD_SAMPLELIST,
				"Load samples from file",
				vj_event_sample_load_list,
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_SAMPLE_SAVE_SAMPLELIST]		=	_new_event(
				"%s",
				VIMS_SAMPLE_SAVE_SAMPLELIST,
				"Save samples to file",
				vj_event_sample_save_list,
				1,
				VIMS_REQUIRE_ALL_PARAMS|VIMS_LONG_PARAMS,
				"Filename",
				NULL,
				NULL );
#endif


	index_map_[VIMS_SAMPLE_CHAIN_ENABLE]			=	_new_event(
				"%d",	
				VIMS_SAMPLE_CHAIN_ENABLE,
				"Enable effect chain of sample",
				vj_event_sample_chain_enable,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_CHAIN_DISABLE]			=	_new_event(
				"%d",
				VIMS_SAMPLE_CHAIN_DISABLE,
				"Disable effect chain of sample",	
				vj_event_sample_chain_disable,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,	
				NULL );

	index_map_[VIMS_SAMPLE_REC_START]			=	_new_event(
				"%d %d",
				VIMS_SAMPLE_REC_START,
				"Start recording from sample",
				vj_event_sample_rec_start,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Number of frames (0=sample duration)",
				0,
				"Auto Play (0=disable, 1=enable)",
				0,
				NULL );
 
	index_map_[VIMS_SAMPLE_REC_STOP]			=	_new_event(
				NULL,
				VIMS_SAMPLE_REC_STOP,
				"Stop recording from this sample",
				vj_event_sample_rec_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	
	index_map_[VIMS_SAMPLE_DEL]				=	_new_event(
				"%d",
				VIMS_SAMPLE_DEL,
				"Delete sample",
				vj_event_sample_del,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Sample ID >= 1",
				0,
				NULL );

	index_map_[VIMS_SAMPLE_DEL_ALL]				=	_new_event(
				NULL,
				VIMS_SAMPLE_DEL_ALL,
				"Delete all samples (caution!)",
				vj_event_sample_clear_all,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SAMPLE_COPY]				=	_new_event(
				"%d",
				VIMS_SAMPLE_COPY,	
				"Copy sample to new",
				vj_event_sample_copy,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_SELECT]				=	_new_event(
				"%d",
				VIMS_SAMPLE_SELECT,
				"Select and play sample",
				vj_event_sample_select,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_STREAM_SELECT]				=	_new_event(
				"%d",
				VIMS_STREAM_SELECT,
				"Select and play stream",	
				vj_event_tag_select,
				1,
				VIMS_ALLOW_ANY,
				"Stream ID >= 1",
				0,
				NULL );

	index_map_[VIMS_STREAM_DELETE]				=	_new_event(
				"%d",
				VIMS_STREAM_DELETE,
				"Delete stream",
				vj_event_tag_del,
				1,	
				VIMS_REQUIRE_ALL_PARAMS,
				"Stream ID >= 1",
				0,
				NULL );
	index_map_[VIMS_V4L_CALI]				=	_new_event(
				"%s",
				VIMS_V4L_CALI,
				"Write calibration data to file",
				vj_event_cali_write_file,
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_STREAM_NEW_CALI]			=	_new_event(
				"%s",
				VIMS_STREAM_NEW_CALI,
				"Load calibration data",
				vj_event_stream_new_cali,
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_V4L_BLACKFRAME]				=	_new_event(
				"%d %d %d %d",
				VIMS_V4L_BLACKFRAME,
				"Capture a black/light frame and subtract it from the video stream",
				vj_event_v4l_blackframe,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				"Tag ID",
				0,
				"Frame Duration (Use 0 to drop blackframe)",
				5,
				"Median Radius (0=Average, N=NxN square)",
				0,
				"Blackframe=0,Lightframe=1",
				0,
				NULL);

	index_map_[VIMS_STREAM_NEW_SHARED]			=	_new_event(
				"%d",
			   VIMS_STREAM_NEW_SHARED,
		   		"Request shared resource from another veejay",
		 		vj_event_connect_shm,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Port number",
				0,
				NULL,
				NULL );	

	index_map_[VIMS_STREAM_NEW_GENERATOR]		=	_new_event(
				"%d %s",
				VIMS_STREAM_NEW_GENERATOR,
				"Open a generator plugin that was loaded. use .so name",
				vj_event_tag_new_generator,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Optional argument (some value)",
				0,
				".so plugin filename (must have been loaded at startup)",
				NULL );

	index_map_[VIMS_STREAM_NEW_V4L]				=	_new_event(
				"%d %d",
				VIMS_STREAM_NEW_V4L,
				"Open video4linux device as new input stream",
				vj_event_tag_new_v4l,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Device Number (0=/dev/video0,1=/dev/video1, ... )",
				0,
				"Channel Number (0=TV,1=composite,2=svideo)",
				0,
				NULL );
#ifdef SUPPORT_READ_DV2
	index_map_[VIMS_STREAM_NEW_DV1394]			=	_new_event(
				"%d",
				VIMS_STREAM_NEW_DV1394,
				"Open dv1394 device as new input stream",
				vj_event_tag_new_dv1394,
				1,
				VIMS_ALLOW_ANY,
				"Channel number",
				63,
				NULL );
#endif
	index_map_[VIMS_STREAM_NEW_Y4M]				=	_new_event(
				"%s",
				VIMS_STREAM_NEW_Y4M,
				"Open yuv4mpeg (special) file as new input stream",
				vj_event_tag_new_y4m,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_STREAM_NEW_COLOR]			=	_new_event(
				"%d %d %d",
				VIMS_STREAM_NEW_COLOR,
				"Solid RGB color fill as new input stream",
				vj_event_tag_new_color,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				"Red",
				0,
				"Green",
				0,
				"Blue",
				0,
				NULL );
	index_map_[VIMS_RGB_PARAMETER_TYPE]			=	_new_event(
				"%d",
				VIMS_RGB_PARAMETER_TYPE,
				"Change YUV <-> RGB conversion",
				vj_event_set_rgb_parameter_type,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Mode (0=GIMP,1=CCIR701,2=broken)",
				0,
				NULL );
	index_map_[VIMS_STREAM_COLOR]				=	_new_event(
				"%d %d %d %d",
				VIMS_STREAM_COLOR,
				"Change RGB color of solid stream",
				vj_event_set_stream_color,
				4,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Red",
				0,
				"Green",
				0,
				"Blue",
				0,
				NULL );
	index_map_[VIMS_STREAM_NEW_UNICAST]			=	_new_event(
				"%d %s",
				VIMS_STREAM_NEW_UNICAST,
				"Open TCP veejay connection (peer to peer, raw data) as new input stream",
				vj_event_tag_new_net,
				2,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Port number",
				0,
				"Hostname or IP address",
				NULL,
				NULL );
	index_map_[VIMS_STREAM_NEW_MCAST]			=	_new_event(
				"%d %s",
				VIMS_STREAM_NEW_MCAST,
				"Open UDP multicast as new input stream",
				vj_event_tag_new_mcast,
				2,	
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Port Number",
				0,
				"Multicast Address",
				NULL,
				NULL );
#ifdef USE_GDK_PIXBUF
	index_map_[VIMS_STREAM_NEW_PICTURE]			=	_new_event(
				"%s",
				VIMS_STREAM_NEW_PICTURE,
				"Open image from file as new input stream",
				vj_event_tag_new_picture,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL,
				NULL );
#endif

	index_map_[VIMS_STREAM_OFFLINE_REC_START]		=	_new_event(
				"%d %d %d",
				VIMS_STREAM_OFFLINE_REC_START,
				"Start offline recording from stream",
				vj_event_tag_rec_offline_start,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Number of frames",
				0,
				"Auto Play (0=disable,1=enable)",
				0,
				NULL );
	index_map_[VIMS_STREAM_OFFLINE_REC_STOP]			=	_new_event(
				NULL,
				VIMS_STREAM_OFFLINE_REC_STOP,
				"Stop offline recording from this stream",
				vj_event_tag_rec_offline_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_STREAM_SET_DESCRIPTION]			=	_new_event(
				"%d %s",
				VIMS_STREAM_SET_DESCRIPTION,
				"Change title of stream",
				vj_event_tag_set_descr,
				2,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Title",
				NULL,
				NULL );
	index_map_[VIMS_STREAM_REC_START]			=	_new_event(
				"%d %d",
				VIMS_STREAM_REC_START,
				"Start recording from stream",
				vj_event_tag_rec_start,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Number of frames",
				0,
				"Auto Play (0=disable,1=enable)",
				0,
				NULL );
	index_map_[VIMS_STREAM_REC_STOP]			=	_new_event(
				"%d %d",
				VIMS_STREAM_REC_STOP,
				"Stop recording from this stream",
				vj_event_tag_rec_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_STREAM_CHAIN_ENABLE]			=	_new_event(
				"%d",
				VIMS_STREAM_CHAIN_ENABLE,
				"Enable effect chain of stream",
				vj_event_tag_chain_enable,
				1,
				VIMS_ALLOW_ANY,
				STREAM_ID_HELP,
				0,
				NULL );
	index_map_[VIMS_STREAM_CHAIN_DISABLE]			=	_new_event(
				"%d",
				VIMS_STREAM_CHAIN_DISABLE,
				"Disable effect chain of stream",
				vj_event_tag_chain_disable,
				1,
				VIMS_ALLOW_ANY,
				STREAM_ID_HELP,
				0,
				NULL );
	index_map_[VIMS_CHAIN_ENTRY_SET_EFFECT]			=	_new_event(
				"%d %d %d",
				VIMS_CHAIN_ENTRY_SET_EFFECT,
				"Add effect to chain entry with default values",
				vj_event_chain_entry_set,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Effect ID",
				0,
				NULL );
	index_map_[VIMS_CHAIN_ENTRY_SET_PRESET]			=	_new_event(
				"%d %d %d %s",
				VIMS_CHAIN_ENTRY_SET_PRESET,
				"Preset effect on chain entry",
				vj_event_chain_entry_preset,
				4,
				VIMS_LONG_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current",
				-1,
				"Effect ID",
				0,
				"space separated value string",
				NULL,
				NULL );	
	

	index_map_[VIMS_CHAIN_ENTRY_SET_NARG_VAL]		=	_new_event(
				"%d %d %d %s",
				VIMS_CHAIN_ENTRY_SET_ARG_VAL,
				"Set a normalized parameter value ( 0.0 - 1.0 )",
				vj_event_chain_entry_set_narg_val,
				4,
				VIMS_LONG_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Parameter number",
				0,
				"Value",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_ARG_VAL]		=	_new_event(
				"%d %d %d %d",
				VIMS_CHAIN_ENTRY_SET_ARG_VAL,
				"Set a parameter value",
				vj_event_chain_entry_set_arg_val,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Parameter number",
				0,
				"Value",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_VIDEO_ON]		=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_SET_VIDEO_ON,
				"Enable effect on chain index",
				vj_event_chain_entry_enable_video,
				2,	
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_VIDEO_OFF]		=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_SET_VIDEO_OFF,
				"Disable effect on chain index",
				vj_event_chain_entry_disable_video,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_DEFAULTS]		=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_SET_DEFAULTS,
				"Reset effect to default",
				vj_event_chain_entry_set_defaults,
				2,
				VIMS_REQUIRE_ALL_PARAMS,	
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				NULL );
	index_map_[VIMS_CHAIN_ENTRY_SET_CHANNEL]		=	_new_event(
				"%d %d %d",
				VIMS_CHAIN_ENTRY_SET_CHANNEL,
				"Set mixing channel",
				vj_event_chain_entry_channel,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Sample ID",
				1,
				NULL );
	index_map_[VIMS_CHAIN_ENTRY_SET_SOURCE]			=	_new_event(
				"%d %d %d",
				VIMS_CHAIN_ENTRY_SET_SOURCE,
				"Set mixing source type",		
				vj_event_chain_entry_source,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Source Type (0=sample,1=stream)",
				0,
				NULL );

	index_map_[VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL]		=	_new_event(
				"%d %d %d %d",
				VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
				"Set mixing channel and source type",
				vj_event_chain_entry_srccha,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				"Source Type (0=sample,1=stream)",
				0,
				"Sample or Stream ID",
				0,
				NULL );
	index_map_[VIMS_CHAIN_ENTRY_CLEAR]			=	_new_event(
				"%d %d",
				VIMS_CHAIN_ENTRY_CLEAR,
				"Reset chain index",
				vj_event_chain_entry_del,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index (-1=current)",
				-1,
				NULL );
	index_map_[VIMS_CHAIN_ENABLE]				=	_new_event(
				NULL,
				VIMS_CHAIN_ENABLE,
				"Enable Effect Chain",
				vj_event_chain_enable,
				0,	
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_CHAIN_DISABLE]				=	_new_event(
				NULL,
				VIMS_CHAIN_DISABLE,
				"Disable Effect Chain",
				vj_event_chain_disable,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_CHAIN_CLEAR]				=	_new_event(
				"%d",	
				VIMS_CHAIN_CLEAR,
				"Reset Effect Chain",
				vj_event_chain_clear,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				NULL );
	index_map_[VIMS_CHAIN_FADE_IN]				=	_new_event(
				"%d %d",
				VIMS_CHAIN_FADE_IN,
				"Fade in effect chain",
				vj_event_chain_fade_in,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Duration in frames",
				100,
				NULL );
	index_map_[VIMS_CHAIN_FADE_OUT]				=	_new_event(
				"%d %d",
				VIMS_CHAIN_FADE_OUT,
				"Fade out effet chain",
				vj_event_chain_fade_out,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Duration in frames",
				100,
				NULL );

	index_map_[VIMS_CHAIN_FOLLOW_FADE]			=	_new_event(
				"%d",
				VIMS_CHAIN_FOLLOW_FADE,
				"Follow to sample #B after finishing fade from sample #A",
				vj_event_chain_fade_follow,
				1,
				VIMS_ALLOW_ANY,
				"0=On, 1=Off",
				0,
				NULL );


	index_map_[VIMS_CHAIN_MANUAL_FADE]			=	_new_event(
				"%d %d",
				VIMS_CHAIN_MANUAL_FADE,
				"Set opacity of Effect Chain",
				vj_event_manual_chain_fade,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Opacity value [0-255]",
				0,
				NULL );
	index_map_[VIMS_CHAIN_SET_ENTRY]			=	_new_event(
				"%d",
				VIMS_CHAIN_SET_ENTRY,
				"Set Chain Index",
				vj_event_chain_entry_select,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Index value",
				0,
				NULL );

	index_map_[VIMS_OUTPUT_Y4M_START]			=	_new_event(
				"%s",
				VIMS_OUTPUT_Y4M_START,
				"(OUT) Write video output to (special) file in yuv4mpeg format", 
				vj_event_output_y4m_start,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS ,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_OUTPUT_Y4M_STOP]			=	_new_event(
				NULL,
				VIMS_OUTPUT_Y4M_STOP,
				"(OUT) Stop writing video output to yuv4mpeg file",
				vj_event_output_y4m_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );
#ifdef HAVE_SDL
	index_map_[VIMS_RESIZE_SDL_SCREEN]			=	_new_event(
				"%d %d %d %d",
				VIMS_RESIZE_SDL_SCREEN,
				"(OUT) Resize SDL video window",
				vj_event_set_screen_size,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				"X offset",
				0,
				"Y offset",
				0,
				NULL );
#endif
	index_map_[VIMS_SET_PLAY_MODE]				=	_new_event(
				"%d",
				VIMS_SET_PLAY_MODE,
				"Change playback mode",
				vj_event_set_play_mode,
				1,
				VIMS_ALLOW_ANY,
				"Playback mode (0=sample,1=stream,2=plain)",
				0 );
	index_map_[VIMS_SET_MODE_AND_GO]			=	_new_event(
				"%d %d",
				VIMS_SET_MODE_AND_GO,
				"Play sample / stream",
				vj_event_set_play_mode_go,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Source type (0=sample,1=stream)",
				0,
				NULL );
	index_map_[VIMS_SWITCH_SAMPLE_STREAM]			=	_new_event(
				NULL,
				VIMS_SWITCH_SAMPLE_STREAM,
				"Switch between sample and stream playback",
				vj_event_switch_sample_tag,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_AUDIO_DISABLE]				=	_new_event(
				NULL,
				VIMS_AUDIO_DISABLE,
				"Disable audio playback",
				vj_event_disable_audio,
				0,
				VIMS_ALLOW_ANY ,
				NULL );

	index_map_[VIMS_AUDIO_ENABLE]				=	_new_event(
				NULL,
				VIMS_AUDIO_ENABLE,
				"Enable audio playback",
				vj_event_enable_audio,
				0,
				VIMS_ALLOW_ANY ,
				NULL );

	
	index_map_[VIMS_EDITLIST_PASTE_AT]			=	_new_event(
				"%d",
				VIMS_EDITLIST_PASTE_AT,
				"Paste frames from buffer at frame into edit descision list",
				vj_event_el_paste_at,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"EDL position",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_CUT]				=	_new_event(
				"%d %d",
				VIMS_EDITLIST_CUT,
				"Cut frames from edit descision list to buffer",
				vj_event_el_cut,
				2,
				VIMS_REQUIRE_ALL_PARAMS,	
				"EDL start position",
				0,
				"EDL end position",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_COPY]				=	_new_event(
				"%d %d",
				VIMS_EDITLIST_COPY,
				"Copy frames from edit descision list to buffer",
				vj_event_el_copy,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"EDL start position",
				0,
				"EDL end position",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_CROP]				=	_new_event(
				"%d %d",
				VIMS_EDITLIST_CROP,
				"Crop frames from edit descision list to buffer",
				vj_event_el_crop,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"EDL start position",
				0,
				"EDL end position",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_DEL]				=	_new_event(
				"%d %d",
				VIMS_EDITLIST_DEL,
				"Delete frames from editlist (no undo!)",
				vj_event_el_del,
				2,	
				VIMS_REQUIRE_ALL_PARAMS,
				"EDL start position",
				0,
				"EDL end position",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_SAVE]				=	_new_event(
				"%d %d %s",
				VIMS_EDITLIST_SAVE,
				"Save (selection of) edit descision list to new file",
				vj_event_el_save_editlist,
				3,
				VIMS_LONG_PARAMS | VIMS_ALLOW_ANY,
				"EDL start position (0=start position)",
				0,
				"EDL end position (0=end position)",
				0,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_EDITLIST_LOAD]				=	_new_event(
				"%s",
				VIMS_EDITLIST_LOAD,
				"Load edit descision list from file",
				vj_event_el_load_editlist,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_EDITLIST_ADD]				=	_new_event(
				"%s",
				VIMS_EDITLIST_ADD,
				"Add video file to edit descision list",
				vj_event_el_add_video,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_EDITLIST_ADD_SAMPLE]			=	_new_event(
				"%d %s",
				VIMS_EDITLIST_ADD_SAMPLE,
				"GUI: Append a file to the plain EDL and create a new sample",
				vj_event_el_add_video_sample,
				2,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"existing or new ID",
				0,
				"Filename",
				NULL,
				NULL );
	index_map_[VIMS_STREAM_LIST]				=	_new_event(
				"%d",
				VIMS_STREAM_LIST,
				"GUI: Get a list of all streams",
				vj_event_send_tag_list,
				1,
				VIMS_ALLOW_ANY,
				"stream offset",
				0,
				NULL );

	index_map_[VIMS_TRACK_LIST]				=	_new_event(
				NULL,
				VIMS_TRACK_LIST,
				"GUI: Get a list of all tracks",
				vj_event_send_track_list,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_WORKINGDIR]				=	_new_event(
				"%d",
				VIMS_WORKINGDIR,
				"GUI: Get all video files starting in cwd",
				vj_event_send_working_dir,
				1,
				VIMS_ALLOW_ANY,
				"(unused)",
				0,
				NULL );


	index_map_[VIMS_SAMPLE_LIST]				=	_new_event(
				"%d",
				VIMS_SAMPLE_LIST,
				"GUI: Get a list of all samples",
				vj_event_send_sample_list,
				1,
				VIMS_ALLOW_ANY,
				"sample offset",
				0,
				NULL );
	index_map_[VIMS_SAMPLE_INFO]				=	_new_event(
				"%d %d",
				VIMS_SAMPLE_INFO,
				"GUI: Get sample or stream information (unadivsed!)",
				vj_event_send_sample_info,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Source Type (0=sample,1=stream)",
				0,
				NULL );
	index_map_[VIMS_SAMPLE_OPTIONS]				=	_new_event(
				"%d %d",
				VIMS_SAMPLE_OPTIONS,
				"GUI: Get sample options",
				vj_event_send_sample_options,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Source Type (0=sample,1=stream)",
				0,
				NULL );
	index_map_[VIMS_EDITLIST_LIST]				=	_new_event(
				NULL,
				VIMS_EDITLIST_LIST,
				"GUI: Get EDL",
				vj_event_send_editlist,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_BUNDLE]					=	_new_event(
				"%d",
				VIMS_BUNDLE,
				"Execute VIMS bundle", 
				vj_event_do_bundled_msg,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Bundle ID",
				0,
				NULL );
#ifdef HAVE_XML2
	index_map_[VIMS_BUNDLE_FILE]				=	_new_event(	
				"%s",
				VIMS_BUNDLE_FILE,
				"Veejay load action file",
				vj_event_read_file,
				1,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Filename",
				NULL ,
				NULL );
	index_map_[VIMS_BUNDLE_SAVE]				=	_new_event(
				"%d %s",
				VIMS_BUNDLE_SAVE,
				"Veejay save action file",
				vj_event_write_actionfile,
				2,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Mode (0=only Bundles,1=save edl/sample list)",
				0,
				"Filename",
				NULL,
				NULL );	
#endif
	index_map_[VIMS_BUNDLE_DEL]				=	_new_event(
				"%d",
				VIMS_BUNDLE_DEL,
				"Delete a VIMS bundle",
				vj_event_bundled_msg_del,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Bundle ID",
				0,
				NULL );
	index_map_[VIMS_BUNDLE_LIST]				=	_new_event(
				NULL,
				VIMS_BUNDLE_LIST,
				"GUI: Get all bundles",
				vj_event_send_bundles,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[ VIMS_KEYLIST ]				=	_new_event(
				NULL,
				VIMS_KEYLIST,
				"GUI: Get all keys",
				vj_event_send_keylist,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_DEVICE_LIST]				=	_new_event(
				NULL,
				VIMS_DEVICE_LIST,
				"GUI: Get all devices and their locations",	
				vj_event_send_devicelist,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_VIMS_LIST]				=	_new_event(
				NULL,
				VIMS_VIMS_LIST,
				"GUI: Get all VIMS events",	
				vj_event_send_vimslist,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_BUNDLE_ADD]				=	_new_event(
				"%d %s",	
				VIMS_BUNDLE_ADD,
				"Add a new bundle to the event list",
				vj_event_bundled_msg_add,
				2,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Bundle ID (0=new, 1=overwrite existing)",
				0,
				"VIMS text",
				0,
				NULL );

	index_map_[VIMS_CALI_IMAGE]				=	_new_event(
				"%d %d",	
				VIMS_REQUIRE_ALL_PARAMS,
				"GUI: Get Calibrated image (raw)",
				vj_event_get_cali_image,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"ID",
				0,
				"Type",
				0,
				NULL );

	index_map_[VIMS_BUNDLE_CAPTURE]				=	_new_event(
				NULL,
				VIMS_BUNDLE_CAPTURE,
				"Capture Effect Chain to a new Bundle",
				vj_event_quick_bundle,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	
	index_map_[VIMS_CHAIN_LIST]				=	_new_event(
				"%d",
				VIMS_CHAIN_LIST,
				"GUI: Get effect chain",
				vj_event_send_chain_list,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				NULL );

	index_map_[VIMS_SAMPLE_STACK]				=	_new_event(
				"%d",
				VIMS_SAMPLE_STACK,
				"Get sample stack details",
				vj_event_send_sample_stack,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				NULL );

	index_map_[VIMS_CHAIN_GET_ENTRY]			=	_new_event(
				"%d %d",
				VIMS_CHAIN_GET_ENTRY,
				"GUI: Get effect chain index details",
				vj_event_send_chain_entry,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index",
				-1,
				NULL );

	index_map_[VIMS_CHAIN_GET_PARAMETERS]			=	_new_event(
				"%d %d",
				VIMS_CHAIN_GET_ENTRY,
				"GUI: Get effect chain index details (incl. min/max/default)",
				vj_event_send_chain_entry_parameters,
				2,
				VIMS_ALLOW_ANY,
				SAMPLE_STREAM_ID_HELP,
				0,
				"Chain Index",
				-1,
				NULL );


	index_map_[VIMS_EFFECT_LIST]				=	_new_event(
				NULL,
				VIMS_EFFECT_LIST,
				"GUI: Get all effects",
				vj_event_send_effect_list,
				0,
				VIMS_ALLOW_ANY );

	index_map_[VIMS_PROMOTION]				=	_new_event(
				NULL,
				VIMS_PROMOTION,
				"Tell client of new samples immediately after creation (reloaded)",
				vj_event_promote_me,
				0,
				VIMS_ALLOW_ANY );

	index_map_[VIMS_VIDEO_INFORMATION]			=	_new_event(
				NULL,
				VIMS_VIDEO_INFORMATION,
				"GUI: Get video information details",
				vj_event_send_video_information,
				0,
				VIMS_ALLOW_ANY );
#ifdef HAVE_SDL
	index_map_[VIMS_BUNDLE_ATTACH_KEY]			=	_new_event(
				"%d %d %d %s",
				VIMS_BUNDLE_ATTACH_KEY,
				"Attach/Detach a Key to VIMS Event",
				vj_event_attach_detach_key,
				4,
				VIMS_ALLOW_ANY,
				"VIMS ID",
				0,
				"SDL Key symbol",
				0,
				"SDL Key modifier (0=none,1=alt,2=ctrl,3=shift)",
				0,
				"VIMS message",
				NULL,
				NULL );

#endif
#ifdef USE_SWSCALER
	index_map_[VIMS_RGB24_IMAGE]				=	_new_event(
				"%d %d",	
				VIMS_REQUIRE_ALL_PARAMS,
				"GUI: Get preview image (raw RGB24)",
				vj_event_get_scaled_image,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				NULL );

#else
#ifdef USE_GDK_PIXBUF
		index_map_[VIMS_RGB24_IMAGE]				=	_new_event(
				"%d %d",	
				VIMS_REQUIRE_ALL_PARAMS,
				"GUI: Get preview image (raw RGB24)",
				vj_event_get_scaled_image,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				NULL );
#endif
#endif
#ifdef USE_GDK_PIXBUF
	index_map_[VIMS_SCREENSHOT]				=	_new_event(
				"%d %d %s",
				VIMS_SCREENSHOT,
				"Save output frame to file",
				vj_event_screenshot,
				3,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				"Filename",
				NULL,
				NULL );
#else
#ifdef HAVE_JPEG
	index_map_[VIMS_SCREENSHOT]				=	_new_event(
				"%d %d %s",
				VIMS_SCREENSHOT,
				"Save output frame to file",
				vj_event_screenshot,
				3,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Width",
				0,
				"Height",
				0,
				"Filename",
				NULL,
				NULL );
#endif
#endif
	index_map_[VIMS_CHAIN_TOGGLE_ALL]			=	_new_event(
				"%d",
				VIMS_CHAIN_TOGGLE_ALL,
				"Enable or disable Effect Chain for ALL samples or streams",
				vj_event_all_samples_chain_toggle,
				1,	
				VIMS_REQUIRE_ALL_PARAMS,
				"On = 1, Off= 0",
				0,
				NULL );
	index_map_[VIMS_SAMPLE_UPDATE]				=	_new_event(
				"%d %d %d",
				VIMS_SAMPLE_UPDATE,
				"Set sample's starting and ending position",
				vj_event_sample_rel_start,
				3,
				VIMS_REQUIRE_ALL_PARAMS,
				SAMPLE_ID_HELP,
				0,
				"Starting position",
				0,
				"Ending position",
				0,
				NULL );
	index_map_[VIMS_STREAM_SET_LENGTH]			=	_new_event(
				"%d",
				VIMS_STREAM_SET_LENGTH,
				"Set ficticious stream length",
				vj_event_stream_set_length,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Number of frames",
				0,
				NULL);
	index_map_[VIMS_STREAM_SET_BRIGHTNESS]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_BRIGHTNESS,
				"Set brightness value for Video4linux stream",
				vj_event_v4l_set_brightness,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );
	index_map_[VIMS_STREAM_SET_CONTRAST]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_CONTRAST,
				"Set constrast value for Video4linux stream",
				vj_event_v4l_set_contrast,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );

	index_map_[VIMS_STREAM_SET_HUE]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_BRIGHTNESS,
				"Set hue value for Video4linux stream",
				vj_event_v4l_set_hue,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );

	index_map_[VIMS_STREAM_SET_COLOR]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_COLOR,
				"Set color value for Video4linux stream",
				vj_event_v4l_set_color,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );
	index_map_[VIMS_STREAM_SET_SATURATION]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_WHITE,
				"Set saturation value for Video4linux stream",
				vj_event_v4l_set_saturation,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );
	index_map_[VIMS_STREAM_SET_WHITE]			=	_new_event(
				"%d %d",
				VIMS_STREAM_SET_WHITE,
				"Set white balance value for Video4linux stream",
				vj_event_v4l_set_white,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				"Value 0-65535",	
				0,
				NULL );
	index_map_[VIMS_STREAM_GET_V4L]			=	_new_event(
				"%d",
				VIMS_STREAM_GET_V4L,
				"GUI: Get video4linux properties",
				vj_event_v4l_get_info,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				STREAM_ID_HELP,
				0,
				NULL );
	index_map_[VIMS_VLOOPBACK_START]		=	_new_event(
				"%d",
				VIMS_VLOOPBACK_START,
				"OUT: Start writing video output to a vloopback device",
				vj_event_vloopback_start,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Vloopback pipe number",
				0,
				NULL );
	index_map_[VIMS_VLOOPBACK_STOP]			=	_new_event(
				NULL,
				VIMS_VLOOPBACK_STOP,
				"OUT: Stop writing to vloopback device",
				vj_event_vloopback_stop,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_EFFECT_SET_BG]			=	_new_event(
				NULL,
				VIMS_EFFECT_SET_BG,
				"Take current frame as Mask for this Effect",
				vj_event_effect_set_bg,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_QUIT]				=	_new_event(
				NULL,
				VIMS_QUIT,
				"Quit Veejay (caution!)",
				vj_event_quit,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_CLOSE]				=	_new_event(
				NULL,
				VIMS_QUIT,
				"End sessions with veejay",
				vj_event_linkclose,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_SET_VOLUME]			=	_new_event(
				"%d",
				VIMS_SET_VOLUME,
				"Set audio volume",
				vj_event_set_volume,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Volume 0-100",
				0,
				NULL );
	index_map_[VIMS_SUSPEND]			=	_new_event(
				NULL,
				VIMS_SUSPEND,
				"Suspend Veejay (caution!)",
				vj_event_suspend,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_DEBUG_LEVEL]			=	_new_event(
				NULL,
				VIMS_DEBUG_LEVEL,
				"More/Less verbosive console output",
				vj_event_debug_level,
				0,
				VIMS_ALLOW_ANY,	
				NULL );
	index_map_[VIMS_SYNC_CORRECTION]		=	_new_event(
				"%d",
				VIMS_SYNC_CORRECTION,
				"Enable/Disable sync correction",
				vj_event_sync_correction,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"0=off,1=on",
				0,
				NULL );
	index_map_[VIMS_FRAMERATE]			=	_new_event(
				"%d",
				VIMS_FRAMERATE,
				"Change playback engine framerate",
				vj_event_set_framerate,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Multiple by 100 (ie. for 25fps, use 2500)",
				0,
				NULL );
	index_map_[VIMS_BEZERK]				=	_new_event(
				NULL,
				VIMS_BEZERK,	
				"Bezerk mode toggle ",
				vj_event_bezerk,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_NO_CACHING]			=	_new_event(	
				NULL,
				VIMS_NO_CACHING,
				"Editlist cache mode toggle",
				vj_event_no_caching,
				0,
				VIMS_ALLOW_ANY,
				NULL );


	index_map_[VIMS_SAMPLE_MODE]			=	_new_event(
				NULL,
				VIMS_SAMPLE_MODE,
				"Change between box or triangle filter for sampling purposes",
				vj_event_sample_mode,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_VIDEO_MCAST_START]		=	_new_event(
				"%d",	
				VIMS_VIDEO_MCAST_START,
				"Start built-in UDP mcast server (YUV planar)",
				vj_event_mcast_start,
				1,
				VIMS_ALLOW_ANY,
				"0=Color,1=Grayscale (default)",
				1,
				NULL );
	index_map_[VIMS_VIDEO_MCAST_STOP]		=	_new_event(
				NULL,
				VIMS_VIDEO_MCAST_STOP,
				"Stop built-in UDP mcast server",
				vj_event_mcast_stop,
				0,
				VIMS_ALLOW_ANY ,
				NULL );
	index_map_[VIMS_GET_FRAME]			=	_new_event(
				NULL,
				VIMS_GET_FRAME,
				"TCP: Send a frame to a connected veejay client",
				vj_event_send_frame,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[ VIMS_SUB_RENDER ] 		=	_new_event(
				"%d",
				VIMS_SUB_RENDER,
				"Render image effects on mixing source",
				vj_event_sub_render,
				1,
				VIMS_ALLOW_ANY,
				SAMPLE_ID_HELP,
				0,
				NULL );



	index_map_[ VIMS_CONTINUOUS_PLAY ] 		=	_new_event(
				"%d",
				VIMS_CONTINUOUS_PLAY,
				"Continuous sample play, do not restart samples",
				vj_event_play_norestart,
				1,
				VIMS_ALLOW_ANY,
				"0=continious play ,1=sample restart (default)",
				0,
				NULL );

	index_map_[ VIMS_PROJ_INC ] 			=	_new_event(
				"%d %d",
				VIMS_PROJ_INC,
				"Increase projection/camera point",
				vj_event_projection_inc,
				2,
				VIMS_ALLOW_ANY,
				"X increment",
				0,
				"Y increment",
				0,
				NULL );

	index_map_[ VIMS_PROJ_DEC ] 			=	_new_event(
				"%d %d",
				VIMS_PROJ_DEC,
				"Decrease projection/camera point",
				vj_event_projection_dec,
				2,
				VIMS_ALLOW_ANY,
				"X increment",
				0,
				"Y increment",
				0,
				NULL );

	index_map_[ VIMS_FRONTBACK ]				=	_new_event(
				NULL,
				VIMS_FRONTBACK,
				"Camera/Projection calibration setup",
				vj_event_viewport_frontback,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[ VIMS_FEEDBACK ]				=	_new_event(
				"%d",
				VIMS_FEEDBACK,
				"Toggle feedback loop",
				vj_event_feedback,
				1,
				VIMS_ALLOW_ANY,
				"Enable/disable",
				"0",
				NULL 
				);

	index_map_[ VIMS_RENDER_DEPTH ]				=	_new_event(
				"%d",
				VIMS_RENDER_DEPTH,
				"Set render depth, use 1 to render chain entries 0,1 and 2 of underlying sample, use 2 to toggle on/off",
				vj_event_render_depth,
				1,
				VIMS_ALLOW_ANY,
				"Depth switch",
				"2",
				NULL );

	index_map_[ VIMS_COMPOSITE ] 				=	_new_event(
				NULL,
				VIMS_COMPOSITE,
				"Push current playing sample or stream as viewport input",
				vj_event_viewport_composition,
				0,
				VIMS_ALLOW_ANY,	
				NULL );

	index_map_[ VIMS_PROJ_TOGGLE ] 				=	_new_event(
				NULL,
				VIMS_PROJ_TOGGLE,
				"Enable/disable viewport rendering",
				vj_event_vp_proj_toggle,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[ VIMS_PROJ_STACK ] 				=	_new_event(
				"%d %d",
				VIMS_PROJ_STACK,
				"Push viewport to secundary input",
				vj_event_vp_stack,
				2,
				VIMS_ALLOW_ANY,
				"On/Off",
				1,
				"Color=0, Grayscale=1",
				1,
				NULL );

	index_map_[ VIMS_PROJ_SET_POINT ]				=	_new_event(
				"%d %d %d %d",
				VIMS_PROJ_SET_POINT,
				"Set a viewport point using scale",
				vj_event_vp_set_points,
				4,
				VIMS_REQUIRE_ALL_PARAMS,
				"Point number",
				0,
				"Scale factor",
				0,
				"X",
				0,
				"Y",
				0,
				NULL );


#ifdef HAVE_SDL
	index_map_[VIMS_FULLSCREEN]			=	_new_event(
				"%d",
				VIMS_FULLSCREEN,
				"Enable / Disable Fullscreen video output",
				vj_event_fullscreen,
				1,
				VIMS_ALLOW_ANY,
				"On = 1, Off=0",
				1,
				NULL );
#endif

#ifdef HAVE_FREETYPE

	index_map_[ VIMS_FONT_COL ]			=	_new_event(
				"%d %d %d %d %d",
				VIMS_FONT_COL,
				"Set font color",
				vj_event_font_set_color,
				5,
				VIMS_REQUIRE_ALL_PARAMS,
				"Red",
				0,
				"Green",
				0,
				"Blue",
				0,
				"Alpha",
				0,
				"0=Transparent 1=BG 2=FG",
				0,
				NULL );

	index_map_[ VIMS_FONT_POS ]			=	_new_event(
				"%d %d",
				VIMS_FONT_POS,
				"Set font position",
				vj_event_font_set_position,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"X position",
				0,
				"Y position",
				0,
				NULL );

	index_map_[ VIMS_FONT_SIZE_FONT ] 		=	_new_event(
				"%d %d",
				VIMS_FONT_SIZE_FONT,
				"Set font type and font size",
				vj_event_font_set_size_and_font,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Font type",
				0,
				"Font size",
				0,
				NULL );

	index_map_[ VIMS_OSL ]				=	_new_event(
				NULL,
				VIMS_OSL,
				"On screen logging (if enabled)",
				vj_event_toggle_osl,
				0,
				VIMS_ALLOW_ANY,
				NULL 
				);
	
	index_map_[ VIMS_OSD_EXTRA ]			=	_new_event(
				NULL,
				VIMS_OSD_EXTRA,
				"Print help in OSD (if available)",
				vj_event_toggle_osd_extra,
				0,
				VIMS_ALLOW_ANY,
				NULL
				);

	index_map_[ VIMS_COPYRIGHT ] 			= _new_event(
				NULL,
				VIMS_OSD,
				"Print copyright",
				vj_event_toggle_copyright,
				0,
				VIMS_ALLOW_ANY,
				NULL
				);

	index_map_[ VIMS_OSD ] 			= _new_event(
				NULL,
				VIMS_OSD,
				"Toggle OSD status",
				vj_event_toggle_osd,
				0,
				VIMS_ALLOW_ANY,
				NULL
				);

	index_map_[ VIMS_SRT_ADD ]			= 	_new_event(
				"%d %d %d %d %d %s",
				VIMS_SRT_ADD,
				"Add a subtitle sequence",
				vj_event_add_subtitle,
				6,
				VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS,
				"Subtitle sequence (0=new)",
				0,
				"Start position",
				0,
				"End position",
				0,
				"X position",
				0,
				"Y position",
				0,
				"Text",
				NULL,
				NULL );

	index_map_[ VIMS_SRT_SELECT ]			=	_new_event(
				"%d",
				VIMS_SRT_SELECT,
				"Select a subtitle sequence",
				vj_event_select_subtitle,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Subtitle sequence",
				0,
				NULL );
	
	index_map_[ VIMS_SRT_DEL ]			=	_new_event(
				"%d",
				VIMS_SRT_DEL,
				"Delete a subtitle sequence",
				vj_event_del_subtitle,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Subtitle sequence",
				0,
				NULL );

	index_map_[ VIMS_SRT_UPDATE ]			=	_new_event(
				"%d %d %d %s",
				VIMS_SRT_UPDATE,
				"Update a subtitle sequence",
				vj_event_upd_subtitle,
				4,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Subtitle sequence",
				0,
				"Start position",
				0,
				"End position",
				0,
				"Text",
				NULL,
				NULL );

	index_map_[ VIMS_SRT_SAVE ]			=	_new_event(
				"%s",
				VIMS_SRT_SAVE,
				"Export subtitles to SRT",
				vj_event_save_srt,
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Filename",	
				NULL,
				NULL );			
	index_map_[ VIMS_SRT_LOAD ]			=	_new_event(
				"%s",
				VIMS_SRT_LOAD,
				"Import subtitles from SRT",
				vj_event_load_srt,
				1,
				VIMS_REQUIRE_ALL_PARAMS | VIMS_LONG_PARAMS,
				"Filename",	
				NULL,
				NULL );	

	index_map_[VIMS_OFFLINE_PLAYMODE]		=	_new_event(
				NULL,
				VIMS_OFFLINE_PLAYMODE,
				"VIMS: actions depend on playback mode",
				vj_event_playmode_rule,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	index_map_[VIMS_OFFLINE_SAMPLES]		=	_new_event(
				NULL,
				VIMS_OFFLINE_SAMPLES,
				"VIMS: pretend playmode is sample (offline sample editing)",
				vj_event_offline_samples,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_OFFLINE_TAGS]			=	_new_event(
				NULL,
				VIMS_OFFLINE_TAGS,
				"VIMS: pretend playmode is tag (offline stream editing)",
				vj_event_offline_tags,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_FONT_LIST]			=	_new_event(
				NULL,
				VIMS_FONT_LIST,
				"GUI: Get list of loaded fonts",
				vj_event_get_font_list,
				0,
				VIMS_ALLOW_ANY,
				NULL );
	
	index_map_[VIMS_SRT_LIST]			=	_new_event(
				NULL,
				VIMS_SRT_LIST,
				"GUI: Get list of loaded subtitle sequences",
				vj_event_get_srt_list,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[VIMS_SRT_INFO]			=	_new_event(
				"%d",
				VIMS_SRT_INFO,
				"GUI: Get subtitle sequence",
				vj_event_get_srt_info,
				1,
				VIMS_ALLOW_ANY,
				"Subtitle sequence",
				1,
				NULL );


	index_map_[ VIMS_SEQUENCE_LIST ]		=	_new_event(
				NULL,
				VIMS_SRT_INFO,
				"GUI: Get list of sample sequences",
				vj_event_get_sample_sequences,
				0,
				VIMS_ALLOW_ANY,
				NULL );

	index_map_[ VIMS_SEQUENCE_STATUS ]		=	_new_event(
				"%d",
				VIMS_SEQUENCE_STATUS,
				"Set sequence play on or off",
				vj_event_sample_sequencer_active,
				1,
				VIMS_ALLOW_ANY,
				"Status 0=off,1=on",
				0,
				NULL );

	index_map_[ VIMS_SEQUENCE_ADD ]			=	_new_event(
				"%d %d",
				VIMS_SEQUENCE_ADD,
				"Add a sample to the sequence",
				vj_event_sequencer_add_sample,
				2,
				VIMS_REQUIRE_ALL_PARAMS,
				"Seq ID",
				0,
				"Sample ID",
				0,
				NULL );

	index_map_[ VIMS_SEQUENCE_DEL ]			=	_new_event(
				"%d",
				VIMS_SEQUENCE_DEL,
				"Del sample from sequence slot",
				vj_event_sequencer_del_sample,
				1,
				VIMS_REQUIRE_ALL_PARAMS,
				"Seq ID",
				0,
				NULL );

	index_map_[ VIMS_GET_IMAGE ]			=	_new_event(
				"%d %d %d %d %d",
				VIMS_GET_IMAGE,
				"Get image region (x,y,w,h,g)",
				vj_event_get_image_part,
				5,
				VIMS_ALLOW_ANY,
				"start X",
				0,
				"start Y",
				0,
				"width",
				0,
				"height",
				0,
				"greyscale",
				0,
				NULL );	

	index_map_[ VIMS_SHM_WRITER ]			=	_new_event(
				"%d",
				VIMS_SHM_WRITER,
				"Start/Stop writing frames to a shared memory segment",
				vj_event_set_shm_status,
				1,
				VIMS_ALLOW_ANY,
				"Status (0=off,1=on)",
				0,
				NULL );

	index_map_[ VIMS_GET_SHM ] 				= _new_event(
				"%d",
				VIMS_GET_SHM,
				"Write back veejay's SHM ID",
				vj_event_get_shm,
				1,
				VIMS_ALLOW_ANY,
				"Not used yet",
				0,
				NULL );
#endif
}




