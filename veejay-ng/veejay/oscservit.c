#ifndef OSC_SERVER_H
#define OSC_SERVER_H
/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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

#include <config.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <libvevo/libvevo.h>
#include <veejay/vims.h>
#include <veejay/veejay.h>
#include <lo/lo.h>


//@ client side implementation
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include <veejay/oscservit.h>

typedef struct
{
	lo_server_thread	st;
	void		   *events;
} osc_recv_t;

static struct
{
	const char *path;
	int	    id;
} osc_paths_[] = {
	{	"/sample/new",			VIMS_SAMPLE_NEW },
	{	"/sample/select",		VIMS_SAMPLE_SELECT },
	{	"/sample/del",			VIMS_SAMPLE_DEL },
	{	"/sample/set/properties",	VIMS_SAMPLE_SET_PROPERTIES },
	{	"/load/samplelist",		VIMS_SAMPLE_LOAD },
	{	"/save/samplelist",		VIMS_SAMPLE_SAVE },
	{	"/sample/rec/start",		VIMS_SAMPLE_START_RECORDER },
	{	"/sample/rec/stop",		VIMS_SAMPLE_STOP_RECORDER },
	{	"/sample/rec/configure",	VIMS_SAMPLE_CONFIGURE_RECORDER },
	{	"/sample/set/volume",		VIMS_SAMPLE_SET_VOLUME },
	{	"/sample/fxc/active",		VIMS_SAMPLE_CHAIN_ACTIVE },
	{	"/sample/edl/paste_at",		VIMS_SAMPLE_EDL_PASTE_AT },
	{	"/sample/edl/copy",		VIMS_SAMPLE_EDL_COPY },
	{	"/sample/edl/del",		VIMS_SAMPLE_EDL_DEL },
	{	"/sample/edl/crop",		VIMS_SAMPLE_EDL_CROP },
	{	"/sample/edl/cut",		VIMS_SAMPLE_EDL_CUT },
	{ 	"/sample/edl/append",		VIMS_SAMPLE_EDL_ADD },
	{	"/sample/edl/export",		VIMS_SAMPLE_EDL_EXPORT },
	{	"/sample/edl/load",		VIMS_SAMPLE_EDL_LOAD },
	{	"/sample/edl/save",		VIMS_SAMPLE_EDL_SAVE },
	{	"/sample/chain/clear",		VIMS_SAMPLE_CHAIN_CLEAR },
	{	"/sample/chain/entry/select",	VIMS_SAMPLE_CHAIN_SET_ENTRY },
	{	"/sample/chain/entry/set",	VIMS_SAMPLE_CHAIN_ENTRY_SET_FX },
	{	"/sample/chain/entry/preset",	VIMS_SAMPLE_CHAIN_ENTRY_SET_PRESET },
	{	"/sample/chain/entry/active",	VIMS_SAMPLE_CHAIN_ENTRY_SET_ACTIVE },
	{	"/sample/chain/entry/value",	VIMS_SAMPLE_CHAIN_ENTRY_SET_VALUE },
	{	"/sample/chain/entry/channel",	VIMS_SAMPLE_CHAIN_ENTRY_SET_INPUT },
	{	"/sample/chain/entry/alpha",	VIMS_SAMPLE_CHAIN_ENTRY_SET_ALPHA },
	{	"/sample/chain/entry/clear",	VIMS_SAMPLE_CHAIN_ENTRY_CLEAR },
	{	"/sample/chain/entry/state",	VIMS_SAMPLE_CHAIN_ENTRY_SET_STATE },
	{	"/sample/chain/entry/bind",	VIMS_SAMPLE_ATTACH_OUT_PARAMETER },
	{	"/sample/chain/entry/release",	VIMS_SAMPLE_DETACH_OUT_PARAMETER },
	{	"/sample/chain/entry/register_osc_path", VIMS_SAMPLE_BIND_OUTP_OSC },
	{	"/sample/chain/entry/unregister_osc_path",VIMS_SAMPLE_RELEASE_OUTP_OSC },
	{	"/sample/register_osc_client",	VIMS_SAMPLE_OSC_START },
	{	"/sample/unregister_osc_client", VIMS_SAMPLE_OSC_STOP },
	{	"/samplebank/add",		VIMS_SAMPLEBANK_ADD },
	{	"/samplebank/del",		VIMS_SAMPLEBANK_DEL },
	{	"/play",			VIMS_SAMPLE_PLAY_FORWARD },
	{	"/reverse",			VIMS_SAMPLE_PLAY_BACKWARD },
	{	"/pause",			VIMS_SAMPLE_PLAY_STOP },
	{	"/skip",			VIMS_SAMPLE_SKIP_FRAME },
	{	"/prev",			VIMS_SAMPLE_PREV_FRAME },
	{	"/up",				VIMS_SAMPLE_SKIP_SECOND },
	{	"/down",			VIMS_SAMPLE_PREV_SECOND },
	{	"/start",			VIMS_SAMPLE_GOTO_START },
	{	"/end",				VIMS_SAMPLE_GOTO_END },
	{	"/frame",			VIMS_SAMPLE_SET_FRAME },
	{	"/speed",			VIMS_SAMPLE_SET_SPEED },
	{	"/slow",			VIMS_SAMPLE_SET_SLOW },
	
	{	"/fullscreen",			VIMS_FULLSCREEN },
	{	"/audio",			VIMS_AUDIO_SET_ACTIVE },
	{	"/resize",			VIMS_RESIZE_SCREEN },
	
	{	NULL,	0	}
	
};

void	error_handler( int num, const char *msg, const char *path )
{
	veejay_msg(0,"Liblo server error %d in path %s: %s",num,path,msg );
}

typedef void (*osc_event)(void *ptr, const char format[], va_list ap);


static char	*vevo_str_to_liblo( const char *format )
{
	int n = strlen(format);
	if( n <= 0 )
		return NULL;
	
	char *res = (char*) malloc( n );
	int k = 0;
	bzero(res,n );
	while(*format)
	{
		if(*format=='d' || *format=='s')
			res[k++] = (*format == 'd' ? 'i' : 's');
		*format++;
	}
	return res;
}

int	generic_handler( const char *path, const char *types,
		lo_arg **argv, int argc, void *data, void *user_data )
{
	int vims_id = 0;
	int args[16];
	double gargs[16];
	char *str[16];
	int i;
	int n = 0;
	int s = 0;
	int g = 0;
	veejay_t *info = (veejay_t*) user_data;
	osc_recv_t *st = (osc_recv_t*) info->osc_server;

	int error = vevo_property_get( st->events, path,0, &vims_id );

	memset(str, 0, sizeof(str));
	
	if( vims_id == 0 || error != VEVO_NO_ERROR)
	{
		veejay_msg(0, "OSC path '%s' does not exist", path );
		return 0;
	}
	
	int vims_args = vj_event_vevo_get_num_args( vims_id );

	char *format = vj_event_vevo_get_event_format( vims_id );

	for( i = 0; i < argc ; i ++ )
	{
		switch(types[i])
		{
			case 'i':
				args[n++] = argv[i]->i32;
				break;

			case 's':
				str[s++] = strdup( (char*) &argv[i]->s );
				break;

			case 'd':
				gargs[g++] = argv[i]->d;
				break;

			default:
				veejay_msg(0, "Skipping unknown argument type '%c' ",
						types[i]);
				break;
				
		}
	}

	if( vims_args != (n + s) )
	{
		char *name = vj_event_vevo_get_event_name( vims_id );
		char *loarg = vevo_str_to_liblo(format);
		veejay_msg(0, "Wrong number of arguments for VIMS %d (%s)", vims_id, name);
		veejay_msg(0, "FORMAT is '%s' but I received '%s'", loarg, types );
		free(name);
		if(loarg) free(loarg);
	}
	else
	{
		vj_event_fire_net_event( info,
			 vims_id,
			 str[0],
			 args,
			 n + s,
			 0,
			 gargs,
			 g );
	}
	if(format) 
		free(format);
	
	for( i = 0; i < 16 ; i ++ )
	 if( str[i] ) 
	  free(str[i]);	 
	
	return 0;
}


void	*veejay_new_osc_server( void *data, const char *port )
{
	osc_recv_t *s = (osc_recv_t*) vj_malloc(sizeof( osc_recv_t));
	s->st = lo_server_thread_new( port, error_handler );

	s->events = vevo_port_new( VEVO_ANONYMOUS_PORT );

	int i;
	for( i =0; osc_paths_[i].path != NULL ; i ++ )
	{
		int error = 
			vevo_property_set( s->events, osc_paths_[i].path, VEVO_ATOM_TYPE_INT,1,
						      &(osc_paths_[i].id) );
		lo_server_thread_add_method(
				s->st, 
				NULL,
				NULL,
				generic_handler,
				data );
		veejay_msg(0, "Added '%s", osc_paths_[i].path );
	}
	lo_server_thread_start( s->st );
	veejay_msg( 0, "OSC server ready at UDP port %d", lo_server_thread_get_port(s->st) );	
	return (void*) s;
}
void	veejay_free_osc_server( void *dosc )
{
	osc_recv_t *s = (osc_recv_t*) dosc;
	lo_server_thread_stop( s->st );
	vevo_port_free( s->events );
	free(s);
	s = NULL;
}
#endif
