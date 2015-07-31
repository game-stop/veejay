/*
 * Copyright (C) 2002-2010 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/* libOSC 

   use ./sendOSC from ${veejay_package_dir}/libOMC/test
   to send OSC messages to port VJ_PORT + 4 (usually 3496)


*/

#include <config.h>
#include <veejay/vims.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <libOSC/libosc.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <veejay/vj-OSC.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <veejay/vims.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-event.h>
#include <veejay/vj-OSC.h>
#include <libvevo/libvevo.h>
#include <libvevo/vevo.h>
static veejay_t *osc_info;

#ifdef HAVE_LIBLO
#include <lo/lo.h>
static vevo_port_t **osc_clients = NULL;
#endif

#include <libOSC/libosc.h>
#include <sys/types.h>
#include <libstream/vj-tag.h>
#include <libsample/sampleadm.h>
#include <libvevo/vevo.h>
#include <libvevo/libvevo.h>

#define NUM_RECEIVE_BUFFERS 100

typedef struct osc_arg_t {
    int a;
    int b;
    int c;
} osc_arg;

typedef struct osc_tokens_t {
	char **addr;
	int	 n_addr;
	char *descr; /* keep track of all pointers */
} osc_tokens;

typedef struct vj_osc_t {
  struct OSCAddressSpaceMemoryTuner t;
  struct OSCReceiveMemoryTuner rt;
  struct OSCContainerQueryResponseInfoStruct cqinfo;
  struct OSCMethodQueryResponseInfoStruct ris;
  struct sockaddr_in cl_addr;
  int sockfd;
  int clilen;
  fd_set readfds;
  OSCcontainer container;
  OSCcontainer *leaves;
  OSCPacketBuffer packet;
  osc_arg *osc_args;
  void	*index;
  void	*clients;
  osc_tokens **addr;
  int	 n_addr;
} vj_osc;


/* VIMS does the job */
extern void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);
extern char *vj_event_vevo_get_event_name( int id );
 

#define OSC_STRING_SIZE 255
#define OPUSH(p) { _old_p_mode = osc_info->uc->playback_mode; osc_info->uc->playback_mode = p; } 
#define OPOP()  { osc_info->uc->playback_mode = _old_p_mode; } 

static	void		free_token( char **arr );

void vj_osc_set_veejay_t(veejay_t *t);

void *_vj_osc_time_malloc(int num_bytes) ;
void *_vj_osc_rt_malloc(int num_bytes);
int	vj_osc_attach_methods( vj_osc *o );
int 	vj_osc_build_cont( vj_osc *o );

#if defined(ARCH_X86) || defined(ARCH_X86_64)
/* convert a big endian 32 bit string to an int for internal use */
static int toInt(const char* b) {
   return (( (int) b[3] ) & 0xff ) + ((((int) b[2]) & 0xff) << 8) + ((((int) b[1]) & 0xff) << 16) +
	  ((((int) b[0] ) & 0xff) << 24);
}
#else
static int toInt(const char *b)
{
	return (( (int) b[0] ) & 0xff ) + (( (int) b[1] & 0xff ) << 8) + ((( (int) b[2])&0xff) << 16 ) +	
		((( (int) b[3] ) & 0xff ) << 24);
}
#endif

/* parse int arguments */
static int vj_osc_count_int_arguments(int arglen, const void *vargs)
{
	unsigned int num_args = 0;	
	// type tags indicated with 0x2c
	const char *args = (const char*) vargs;
	if(args[0] == 0x2c)
	{
			int i;
			// count occurences of 'i' (0x69)
			for ( i = 1; i < arglen ; i ++ )
			{
				if( args[i] == 0x69 ) num_args ++;
				if( (i+1) < arglen && args[i+1] == 0 ) break;
			}
	}
	else
	{
		// for non typed tags its much simpler, every integer is stored 32 bits
		if(arglen < 4) return 0;
		num_args = arglen / 4;
	}
	return num_args;
}

static int vj_osc_parse_char_arguments(int arglen, const void *vargs, char *dst)
{
	const char *args = (const char*)vargs;
	if(arglen <= 4) return 0;
	if(args[1] == 0x73)
	{
		int b = 0;
		for(b = 0; b < arglen; b++)
		{
		 dst[b] = args[b+4];
		 if(args[b+4] == 0)
		  break; 
		}
		return arglen;
	}
	return 0;
}
/* parse int arguments */
static int vj_osc_parse_int_arguments(int arglen, const void *vargs, int *arguments)
{
	int num_args = vj_osc_count_int_arguments(arglen,vargs);
	int i=0;
	int offset = 0;
	const char *args = (const char*)vargs;

	if(num_args <= 0)
		return 0;

	if( args[0] == 0x2c )
	{	// type tag
		// figure out padding length of typed tag  
		unsigned int pad = 4 + ( num_args + 1 ) / 4 * 4;
		for ( i = 0; i < num_args ; i ++ )
		{
			arguments[i] = toInt( args + pad + offset );
			offset += 4;
		}
	}
	else
	{
		for(i = 0; i < num_args; i ++)
		{
			arguments[i] = toInt( args + offset);
			offset += 4;
		}

	}

	return num_args; /* success */
}

/* memory allocation functions of libOMC_dirty (OSC) */

void *_vj_osc_time_malloc(int num_bytes) {
	return vj_malloc( num_bytes );
}

void *_vj_osc_rt_malloc(int num_bytes) {
	return vj_malloc( num_bytes );
}


/* initialize the pointer to veejay_t */

void vj_osc_set_veejay_t(veejay_t *info) {
	osc_info = info;
}

void vj_osc_free(void *d)
{
	if(!d) return;
	vj_osc *c = (vj_osc*) d;

//	void *addr = OSCPacketBufferGetClientAddr(c->packet );
//	if(addr)
//		free(addr);
	
	OSCDestroyCallbackListNodes();
	OSCDestroyDataQueues(NUM_RECEIVE_BUFFERS);
	OSCDestroyAddressSpace();

	int i;
	for ( i = 0; i < c->n_addr; i ++ ) {
			osc_tokens *ot = c->addr[i];
			if(ot == NULL)
				continue;
			if( ot->addr ) {
				free_token( ot->addr );
			}
			if( ot->descr ) {
				free(ot->descr);
			}
			free(ot);
	}
	free(c->addr);
	

	if(c->leaves) 
		free(c->leaves);
	if(c->index) 
		vevo_port_recursive_free( c->index );
	if(c) free(c);
	
	c = NULL;
}

#ifdef HAVE_LIBLO
static int	osc_has_connection( char *name ) {

	int i;
	for( i = 0; i < 32; i ++ ){

		vevo_port_t *port = osc_clients[i];
		if( port == NULL )
			continue;

		size_t len = vevo_property_element_size( port, "connection", 0 );
		char       *con = malloc( sizeof(char) * len );
		int err   	= vevo_property_get(port, "connection", 0,&con );
		if( err == VEVO_NO_ERROR ) {
			if( strncasecmp(con,name, strlen(con)) == 0  )
				return 1;
		}
	}
	return 0;
}

static	void	osc_add_client(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	int	client_id = *( (int*) context );
	char str[OSC_STRING_SIZE];
	int  args[16];
	int __a = vj_osc_count_int_arguments(arglen,vargs);
	int __n = vj_osc_parse_char_arguments(arglen,vargs,str);
	memset( args,0,16 );
	str[__n] = '\0';

	vj_osc_parse_int_arguments( arglen , vargs , args );

	if( __n > 0 ) __a ++;


	int free_id = -1;
	int i;
	for( i = 0; i < 31; i ++ ) {
	  if(osc_clients[i] == NULL ) {
	    free_id = i;
	    break;
	  }
	}

	if( free_id == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to add more OSC senders.");
		return;
	}

	if( __a != 2 || __n <= 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid arguments, use HOSTNAME PORT");
		return;
	}
	char port[6];
	snprintf( port, sizeof(port-1), "%d", args[0] );
	char name[1024];
	snprintf(name, sizeof(name)-1, "%s:%s", str,port );
	char *cmd = "/status";
	char *nptr = name;
	if( osc_has_connection( name ) ) {
		veejay_msg(0, "There already exists a status sender for %s",name);
		return;
	}

	osc_clients[ free_id ] = vpn(VEVO_ANONYMOUS_PORT );
	lo_address t 	       = lo_address_new( str, port );

	if(vevo_property_set( osc_clients[free_id], "lo", VEVO_ATOM_TYPE_VOIDPTR,1, &t ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to add lo_address to vevo port.");
		( osc_clients[free_id] );
		osc_clients[free_id] = NULL;
		return;
	}

	if(vevo_property_set( osc_clients[free_id], "cmd", VEVO_ATOM_TYPE_STRING, 1, &cmd ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to store command '%s'", cmd );
		(osc_clients[free_id]);
		osc_clients[free_id]=NULL;
		return;
	}

	if( vevo_property_set( osc_clients[free_id], "connection", VEVO_ATOM_TYPE_STRING,1,&nptr ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to store connection string.");
		(osc_clients[free_id]);
		osc_clients[free_id] = NULL;
		return;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Configured OSC sender to %s:%s, send /status [ArgList] every cycle.",
		       str,port );	
}

static	int	osc_client_status_send( lo_address t, char *cmd )
{
	sample_info *s;
	vj_tag *tag;
	int err = 0;

	switch( osc_info->uc->playback_mode ) {
		case VJ_PLAYBACK_MODE_SAMPLE:
			s = sample_get( osc_info->uc->sample_id );
			
			err = lo_send( t,
				 cmd,
				 "iiiiiiiiiiiiiiii",
				 osc_info->uc->playback_mode,
				 osc_info->real_fps,
				 osc_info->settings->current_frame_num,
				 osc_info->uc->sample_id,
				 s->effect_toggle,
				 s->first_frame,
				 s->last_frame,
				 s->speed,
				 s->dup,
				 s->looptype,
				 s->marker_start,
				 s->marker_end,
				 (sample_size()-1),
				 (int)( 100.0f/osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 vj_event_macro_status() );
				 


		break;
		case VJ_PLAYBACK_MODE_TAG:
			tag = vj_tag_get( osc_info->uc->sample_id );
			
			err = lo_send( t,
				 cmd,
				 "iiiiiiiiiiiiiiiii",
				 osc_info->uc->playback_mode,
				 osc_info->real_fps,
				 osc_info->settings->current_frame_num,
				 osc_info->uc->sample_id,
				 tag->effect_toggle,
				 0,
				 tag->n_frames,
				 1,1,1,0,0,
				 (vj_tag_size()-1),
				 (int) ( 100.0f/osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 vj_event_macro_status() );	 
		break;
		case VJ_PLAYBACK_MODE_PLAIN:
			err = lo_send( t,
				 cmd,
				 "iiiiiiiiiiiiiiiiii",
				 osc_info->uc->playback_mode,
				 osc_info->real_fps,
				 osc_info->settings->current_frame_num,
				 0,
				 0,
				 osc_info->settings->min_frame_num,
				 osc_info->settings->max_frame_num,
				 osc_info->settings->current_playback_speed,
				 0,0,0,0,0,
				 (sample_size()-1 + vj_tag_true_size() - 1),
				 (int) ( 100.0f / osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 vj_event_macro_status());
				 
		break;
	}

	if( err == -1 ) {
		lo_address_free( t );
		return 0;
	}	
	return 1;
}

static	void	osc_iterate_clients()
{
	int i;
	for( i = 0; i < 32; i ++ ){

		vevo_port_t *port = osc_clients[i];
		if( port == NULL )
			continue;
		lo_address clnt;
	       	int err = vevo_property_get( port, "lo", 0, &clnt );
		if( err == VEVO_NO_ERROR ) {
			size_t len = vevo_property_element_size( port, "cmd", 0 );
			char       *cmd = malloc( sizeof(char) * len );
			err = vevo_property_get( port, "cmd", 0, &cmd );

			int res = osc_client_status_send( clnt, cmd );
			if( res == 0 ) {
				( port );
				osc_clients[i] = NULL;
				veejay_msg(VEEJAY_MSG_WARNING,"Failed to send %s",cmd);
			} 
			free(cmd);
		}
	}
}
#endif

static 	void osc_vims_method(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	int vims_id = *( (int*) context );
	char str[OSC_STRING_SIZE];
	int  args[16];
	int __a = vj_osc_count_int_arguments(arglen,vargs);
	int __n = vj_osc_parse_char_arguments(arglen,vargs,str);
	memset( args,0,16 );
	str[__n] = '\0';

	vj_osc_parse_int_arguments( arglen , vargs , args );

	if( __n > 0 ) __a ++;
		
	vj_event_fire_net_event(osc_info, vims_id, str,args, __a ,0);
}


//@ setup osc<-> vims mapping
static struct
{
	const char	*name;
	int	vims_id;
} osc_method_layout[] = 
{
	{ "fxlist/inc"						, VIMS_FXLIST_INC },
	{ "fxlist/dec"						, VIMS_FXLIST_DEC },
	{ "fxlist/enter"					, VIMS_FXLIST_ADD },
	{ "fxlist/setbg"					, VIMS_EFFECT_SET_BG },
	{ "ui/preview"						, VIMS_PREVIEW_BW },
	{ "macro/macro"						, VIMS_MACRO },
	{ "macro/select"					, VIMS_MACRO_SELECT },
	
	{ "composite/select"					, VIMS_COMPOSITE },
#ifdef USE_GDK_PIXBUF
	{ "console/screenshot"					, VIMS_SCREENSHOT },
#else 
#ifdef HAVE_JPEG
	{ "console/screenshot",					VIMS_SCREENSHOT },
#endif
#endif
	{ "console/framerate"					, VIMS_FRAMERATE },
	{ "console/bezerk"					, VIMS_BEZERK },
#ifdef HAVE_SDL
	{ "console/resize"					, VIMS_RESIZE_SDL_SCREEN },
#endif
	{ "console/renderdepth"					, VIMS_RENDER_DEPTH },
	{ "console/continuous"					, VIMS_CONTINUOUS_PLAY },
//@ NO VIMS callback!	{ "console/recviewport"			, VIMS_RECVIEWPORT },
	{ "console/volume"					, VIMS_SET_VOLUME },
	{ "console/fullscreen"					, VIMS_FULLSCREEN },
	{ "console/suspsend"					, VIMS_SUSPEND },
	{ "console/quit"					, VIMS_QUIT },
	{ "console/close"					, VIMS_CLOSE },
//@ NO VIMS callback	{ "console/load"			, VIMS_LOAD_PLUGIN },
//@ NO VIMS callback	{ "console/unload"			, VIMS_UNLOAD_PLUGIN },
//@ NO VIMS callback	{ "console/plugcmd"			, VIMS_CMD_PLUGIN },
	{ "console/dataformat"					, VIMS_RECORD_DATAFORMAT },
	{ "console/playmode"					, VIMS_SET_PLAIN_MODE },
	{ "console/load",					VIMS_SAMPLE_LOAD_SAMPLELIST },
	{ "console/save",					VIMS_SAMPLE_SAVE_SAMPLELIST },
//@ NO VIMS callback	{ "console/display"			VIMS_INIT_GUI_SCREEN },
	{ "console/switch"					, VIMS_SWITCH_SAMPLE_STREAM },
	{ "audio/enable"					, VIMS_AUDIO_ENABLE },
	{ "audio/disable"					, VIMS_AUDIO_DISABLE },
	{ "bank/select"						, VIMS_SELECT_BANK },
	{ "bank/slot"						, VIMS_SELECT_ID },

	{ "record/autostart"					, VIMS_REC_AUTO_START },
	{ "record/stop"						, VIMS_REC_STOP },
	{ "record/start"					, VIMS_REC_START },
	{ "sample/mode"						, VIMS_SAMPLE_MODE },
	{ "sample/play"						, VIMS_SET_MODE_AND_GO },
	{ "sample/rand/start"					, VIMS_SAMPLE_RAND_START },
	{ "sample/rand/stop"					, VIMS_SAMPLE_RAND_STOP },
	{ "sample/rec/start",					VIMS_SAMPLE_REC_START },
	{ "sample/start",					VIMS_SET_SAMPLE_START },
	{ "sample/end",						VIMS_SET_SAMPLE_END },
	{ "sample/new",						VIMS_SAMPLE_NEW },
	{ "sample/select",					VIMS_SAMPLE_SELECT },
	{ "sample/delete",					VIMS_SAMPLE_DEL },
	{ "sample/looptype",					VIMS_SAMPLE_SET_LOOPTYPE },
	{ "sample/description",					VIMS_SAMPLE_SET_DESCRIPTION },
	{ "sample/speed",					VIMS_SAMPLE_SET_SPEED },
	{ "sample/startposition",				VIMS_SAMPLE_SET_START },
	{ "sample/endposition",					VIMS_SAMPLE_SET_END },
	{ "sample/slow",					VIMS_SAMPLE_SET_DUP },
	{ "sample/inpoint",					VIMS_SAMPLE_SET_MARKER_START },
	{ "sample/outpoint",					VIMS_SAMPLE_SET_MARKER_END },
	{ "sample/clearpoints",					VIMS_SAMPLE_CLEAR_MARKER },
	{ "sample/edladd",					VIMS_EDITLIST_ADD_SAMPLE },
	{ "sample/killall",					VIMS_SAMPLE_DEL_ALL },
	{ "sample/copy",					VIMS_SAMPLE_COPY },
	{ "sample/recstart",					VIMS_SAMPLE_REC_START },
	{ "sample/recstop",					VIMS_SAMPLE_REC_STOP },	
	{ "sample/fx/on",					VIMS_SAMPLE_CHAIN_ENABLE },
	{ "sample/fx/off",					VIMS_SAMPLE_CHAIN_DISABLE },
	{ "sample/looptoggle",					VIMS_SAMPLE_TOGGLE_LOOP },
	{ "stream/play"						, VIMS_SET_MODE_AND_GO },
	{ "stream/delete",					VIMS_STREAM_DELETE },
	{ "stream/new/v4l",					VIMS_STREAM_NEW_V4L },
#ifdef SUPPORT_READ_DV2
	{ "stream/new/dv1394",					VIMS_STREAM_NEW_DV1394 },
#endif
	{ "stream/new/solid",					VIMS_STREAM_NEW_COLOR },
	{ "stream/new/y4m",					VIMS_STREAM_NEW_Y4M },
	{ "stream/new/cali",					VIMS_STREAM_NEW_CALI },
	{ "stream/startcali",					VIMS_V4L_BLACKFRAME },
	{ "stream/savecali",					VIMS_V4L_CALI },
	{ "stream/unicast",					VIMS_STREAM_NEW_UNICAST },
	{ "stream/mcast",					VIMS_STREAM_NEW_MCAST },
	{ "stream/new/picture",					VIMS_STREAM_NEW_PICTURE },
	{ "stream/offline/recstart",				VIMS_STREAM_OFFLINE_REC_START },
	{ "stream/offline/recstop",				VIMS_STREAM_OFFLINE_REC_STOP },
	{ "stream/fx/on",					VIMS_STREAM_CHAIN_ENABLE },
	{ "stream/fx/off",					VIMS_STREAM_CHAIN_DISABLE },
	{ "stream/rec/start",					VIMS_STREAM_REC_START },
	{ "stream/rec/stop",					VIMS_STREAM_REC_STOP },
	{ "stream/v4l/brightness",				VIMS_STREAM_SET_BRIGHTNESS },
	{ "stream/v4l/contrast",				VIMS_STREAM_SET_CONTRAST },
	{ "stream/v4l/hue",					VIMS_STREAM_SET_HUE },
	{ "stream/v4l/color",					VIMS_STREAM_SET_COLOR },
	{ "stream/v4l/whitebalance",				VIMS_STREAM_SET_WHITE	},
	{ "stream/v4l/saturation",				VIMS_STREAM_SET_SATURATION },
	{ "stream/length",					VIMS_STREAM_SET_LENGTH },
	{ "stream/color",					VIMS_STREAM_COLOR },

	{ "video/forward"					, VIMS_VIDEO_PLAY_FORWARD },
	{ "video/play"						, VIMS_VIDEO_PLAY_FORWARD },
//@FIXME	{ "video/reverse"					, VIMS_VIDEO_PLAY_REVERSE },
	{ "video/pause"						, VIMS_VIDEO_PLAY_STOP },
	{ "video/nextframe"					, VIMS_VIDEO_SKIP_FRAME },	
	{ "video/prevframe"					, VIMS_VIDEO_PREV_FRAME },
	{ "video/nextsecond"					, VIMS_VIDEO_SKIP_SECOND },
	{ "video/prevsecond"					, VIMS_VIDEO_PREV_SECOND },
	{ "video/gotostart"					, VIMS_VIDEO_GOTO_START },
	{ "video/gotoend"					, VIMS_VIDEO_GOTO_END },
	{ "video/frame"						, VIMS_VIDEO_SET_FRAME },
	{ "video/speed"						, VIMS_VIDEO_SET_SPEED },
	{ "video/slow"						, VIMS_VIDEO_SET_SLOW },
	{ "mcast/start",					  VIMS_VIDEO_MCAST_START },
	{ "mcast/end",						VIMS_VIDEO_MCAST_STOP },
	{ "video/speedk",					VIMS_VIDEO_SET_SPEEDK },

	{ "editlist/paste",					VIMS_EDITLIST_PASTE_AT },
	{ "editlist/copy",					VIMS_EDITLIST_COPY },
	{ "editlist/cut",					VIMS_EDITLIST_CUT },
	{ "editlist/crop",					VIMS_EDITLIST_CROP },
	{ "editlist/add",					VIMS_EDITLIST_ADD },
	{ "editlist/save",					VIMS_EDITLIST_SAVE },
	{ "editlist/load",					VIMS_EDITLIST_LOAD },

//@ NO VIMS callback!	{ "stream/activate",			VIMS_STREAM_ACTIVATE },
//@ NO VIMS callback!	{ "stream/deactivate",			VIMS_STREAM_DEACTIVATE },
	{ "sequence/status",					VIMS_SEQUENCE_STATUS },
	{ "sequence/set",					VIMS_SEQUENCE_ADD },
	{ "sequence/del",					VIMS_SEQUENCE_DEL },
	{ "projection/inc",					VIMS_PROJ_INC },
	{ "projection/dec",					VIMS_PROJ_DEC },
	{ "projection/set",					VIMS_PROJ_SET_POINT },
	{ "projection/stack",					VIMS_PROJ_STACK },
	{ "projection/toggle",					VIMS_PROJ_TOGGLE },
	{ "chain/enable",					VIMS_CHAIN_ENABLE },
	{ "chain/disable",					VIMS_CHAIN_DISABLE },
	{ "chain/fadein",					VIMS_CHAIN_FADE_IN },
	{ "chain/fadeout",					VIMS_CHAIN_FADE_OUT },
	{ "chain/clear",					VIMS_CHAIN_CLEAR },
//	{ "chain/entry/preset",					VIMS_CHAIN_ENTRY_PRESET },
	{ "chain/opacity",					VIMS_CHAIN_MANUAL_FADE },
	{ "chain/entry/setarg",					VIMS_CHAIN_ENTRY_SET_ARG_VAL },
	{ "chain/entry/defaults",				VIMS_CHAIN_ENTRY_SET_DEFAULTS },
	{ "chain/entry/channel",				VIMS_CHAIN_ENTRY_SET_CHANNEL },
	{ "chain/entry/source",					VIMS_CHAIN_ENTRY_SET_SOURCE },
	{ "chain/entry/srccha",					VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL },
	{ "chain/entry/clear",					VIMS_CHAIN_ENTRY_CLEAR },
	{ "chain/entry/up",					VIMS_CHAIN_ENTRY_UP },
	{ "chain/entry/down",					VIMS_CHAIN_ENTRY_DOWN },
	{ "chain/entry/srctoggle",				VIMS_CHAIN_ENTRY_SOURCE_TOGGLE },
	{ "chain/entry/incarg",					VIMS_CHAIN_ENTRY_INC_ARG },
	{ "chain/entry/decarg",					VIMS_CHAIN_ENTRY_DEC_ARG },
//	{ "chain/entry/toggle",					VIMS_CHAIN_ENTRY_TOGGLE },
	{ "chain/entry/state",					VIMS_CHAIN_ENTRY_SET_STATE} ,
	{ "chain/channel/inc",					VIMS_CHAIN_ENTRY_CHANNEL_INC },
	{ "chain/channel/dec",					VIMS_CHAIN_ENTRY_CHANNEL_DEC },
//	{ "chain/entry/channel/up",				VIMS_CHAIN_ENTRY_CHANNEL_UP },
//	{ "chain/entry/channel/down",				VIMS_CHAIN_ENTRY_CHANNEL_DOWN },
#ifdef HAVE_FREETYPE
	{ "display/copyright",					VIMS_COPYRIGHT },
#endif
	{ "vloopback/start",					VIMS_VLOOPBACK_START },
	{ "vloopback/stop",					VIMS_VLOOPBACK_STOP },
	{ "y4m/start",						VIMS_OUTPUT_Y4M_START },
	{ "y4m/stop",						VIMS_OUTPUT_Y4M_STOP },
#ifdef HAVE_LIBLO
	{ "osc/sender",					-2 },
#endif
	{ NULL,							-1 }
};

static	 char	**string_tokenize( const char delim, const char *name, int *ntokens ) { 
	int n = strlen(name);
	int i;
	int n_tokens = 0;
	for( i = 0; i < n; i ++ ) 
	   if( name[i] == delim )
	    n_tokens ++;

	if( n_tokens == 0 ) 
		return NULL;

	n_tokens ++;

	char **arr = (char**) malloc(sizeof(char*) * (n_tokens+1));
	int end = 0;
	int p = 0;
	char *ptr = (char*) name;
	int last = 0;
	for( i = 0; i < n ; i ++ ){ 
		if( name[i] == delim ) {
			if( *ptr == delim )
			{	*ptr ++; last ++; }

			arr[p] = strndup( ptr , end );
			ptr += end;
			last += end;
			end = 0;
			p++;
		} else {
			end ++;
		}

	}

	arr[p] = strdup( name + last + 1 );
	arr[p+1] = NULL;
	*ntokens = n_tokens+1;

	return arr;
}

static	void		free_token( char **arr ) {
	int i = 0;
	for( i = 0; arr[i] != NULL ; i ++ ) {
		free(arr[i]);
	}
	free(arr);
	arr = NULL;
}


#define MAX_ADDR 1024

int 	vj_osc_build_cont( vj_osc *o ) //FIXME never freed
{ 
	int i;

	o->index = vpn( VEVO_ANONYMOUS_PORT );
	int leave_id = 0;
	int next_id  = 0;
	int err = 0;
	int t = 0;

	int len = 1;
	while ( osc_method_layout[len].name != NULL )
		len ++;
		
	o->addr = (osc_tokens**) vj_calloc (sizeof(osc_tokens*) * MAX_ADDR );
	o->n_addr = 0;

	int next_addr = 0;

	for( i = 0; osc_method_layout[i].name != NULL && next_addr < MAX_ADDR; i ++ ) {
		int ntokens = 0;
		char **arr = string_tokenize( '/', osc_method_layout[i].name, &ntokens);
		if( arr == NULL || ntokens == 0 ) {
			continue;
		}
			
		err = vevo_property_get(o->index, arr[0] , 0, &leave_id );
		if( err == VEVO_NO_ERROR ) {
			free_token(arr);
			continue;
		}
		o->leaves[next_id] = OSCNewContainer( arr[0], o->container, &(o->cqinfo) );
	
		o->addr[next_addr] = vj_calloc(sizeof(osc_tokens));
		o->addr[next_addr]->n_addr = ntokens;
		o->addr[next_addr]->addr = arr;
		
		err = vevo_property_set(o->index, arr[0], VEVO_ATOM_TYPE_INT , 1, &next_id);
		next_id ++;
		next_addr ++;
	}

	for( i = 0; osc_method_layout[i].name != NULL && next_addr < MAX_ADDR ; i ++ ) {
		int ntokens = 0;
		int exists = 0;
		int attach_id = 0;
		char **arr = string_tokenize( '/', osc_method_layout[i].name, &ntokens);
		if( arr == NULL || ntokens == 0 ) {
			continue;
		}
		int containers = ntokens - 1;

		o->addr[next_addr] = vj_calloc(sizeof(osc_tokens));
		o->addr[next_addr]->n_addr = ntokens;
		o->addr[next_addr]->addr = arr;
		

		for( t = 1; t < containers; t ++ ) {
			int is_method = (t == (containers-1)) ? 1: 0;
			if( is_method  )
				continue;
		
			err = vevo_property_get( o->index, arr[t-1], 0, &attach_id );
			if( err != VEVO_NO_ERROR ) {
				break;
			}	
			err = vevo_property_get( o->index, arr[t], 0, &exists );
			if( err == VEVO_NO_ERROR ) {
				continue;
			}

			o->leaves[next_id] = OSCNewContainer( arr[t], o->leaves[attach_id], &(o->cqinfo ));
			
			err = vevo_property_set( o->index, arr[t], VEVO_ATOM_TYPE_INT, 1, &next_id );
			next_id ++;

			//veejay_msg(0, "Added leave '%s'%d to container '%s'%d", arr[t],next_id-1,arr[t-1],attach_id);
		}

		next_addr ++;

		}	


	for( i = 0; osc_method_layout[i].name != NULL && next_id < MAX_ADDR; i ++ ) {
		int ntokens = 0;
		/*
		 * arr is never freed; the OSCNewMethod copies the pointer to elements in arr
		 */
		char **arr = string_tokenize( '/', osc_method_layout[i].name, &ntokens);
		if( arr == NULL || ntokens == 0 )
			continue;
		int containers = ntokens - 1;
		if( containers == 0 ) {
			free_token(arr);
			continue;
		}
		
		int method = containers - 1;
		
		o->addr[next_addr] = vj_calloc(sizeof(osc_tokens));
		o->addr[next_addr]->n_addr = ntokens;
		o->addr[next_addr]->addr = arr;

		err = vevo_property_get( o->index, arr[method-1], 0, &leave_id );
#ifdef HAVE_LIBLO
		if( osc_method_layout[i].vims_id == -2 ) {
			o->ris.description = strdup( "Setup a OSC sender (Arg 0=host, 1=port)");
			OSCNewMethod( arr[method],
				      o->leaves[leave_id],
				      osc_add_client,
				      &(osc_method_layout[i].vims_id),
				      &(o->ris));
		} else {
			o->ris.description = vj_event_vevo_get_event_name( osc_method_layout[i].vims_id );
			OSCNewMethod( arr[ method ], o->leaves[  leave_id ], osc_vims_method, &(osc_method_layout[i].vims_id),&(o->ris));
	
		}	
#else
		o->ris.description = vj_event_vevo_get_event_name( osc_method_layout[i].vims_id );
		OSCNewMethod( arr[ method ], o->leaves[  leave_id ], osc_vims_method, &(osc_method_layout[i].vims_id),&(o->ris));

#endif

		o->addr[next_addr]->descr = o->ris.description;

		next_addr ++;
	}

	o->n_addr = next_addr;

	return 1;
}


/* initialization, setup a UDP socket and invoke OSC magic */
void* vj_osc_allocate(int port_id) {
	void *res;
	char tmp[200];
	
	vj_osc *o = (vj_osc*)vj_malloc(sizeof(vj_osc));
#ifdef HAVE_LIBLO
	osc_clients = (vevo_port_t*) vj_malloc(sizeof(vevo_port_t*) * 32);
	int i;
	for( i = 0; i < 32 ;i ++ )
		osc_clients[i] = NULL;
#endif
	//o->osc_args = (osc_arg*)vj_malloc(50 * sizeof(*o->osc_args));
	o->rt.InitTimeMemoryAllocator = _vj_osc_time_malloc;
	o->rt.RealTimeMemoryAllocator = _vj_osc_rt_malloc;
	o->rt.receiveBufferSize = 1024;
	o->rt.numReceiveBuffers = NUM_RECEIVE_BUFFERS;
	o->rt.numQueuedObjects = 100;
	o->rt.numCallbackListNodes = 300;
	o->leaves = (OSCcontainer*) vj_malloc(sizeof(OSCcontainer) * 300);
	o->t.initNumContainers = 300;
	o->t.initNumMethods = 300;
	o->t.InitTimeMemoryAllocator = _vj_osc_time_malloc;
	o->t.RealTimeMemoryAllocator = _vj_osc_rt_malloc;
			
	if(OSCInitReceive( &(o->rt))==FALSE) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize OSC receiver");
		return NULL;
	} 
	o->packet = OSCAllocPacketBuffer();
 	
	if(NetworkStartUDPServer( o->packet, port_id) != TRUE) {
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) Cannot start OSC/UDP server at port %d ",
				port_id);
	}

	if( IsMultiCast( tmp ) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Multicast address %s, port %d",
			tmp, port_id );
	}

	/* Top level container / */
	o->container = OSCInitAddressSpace(&(o->t));

	OSCInitContainerQueryResponseInfo( &(o->cqinfo) );
	o->cqinfo.comment = "Video commands";

	if( !vj_osc_build_cont( o ))
		return NULL;

	OSCInitMethodQueryResponseInfo( &(o->ris));
//	if( !vj_osc_attach_methods( o ))
//		return NULL;

	res =(void*) o;
     return res;
}	


void vj_osc_dump()
{

	OSCPrintWholeAddressSpace();
#ifdef HAVE_LIBLO
	veejay_msg(VEEJAY_MSG_INFO, "The OSC command /osc/sender <hostname> <port> will setup an OSC client");
	veejay_msg(VEEJAY_MSG_INFO, "which periodically sends veejay's status information. Format below:");

	veejay_msg(VEEJAY_MSG_INFO,
			"/status ( [playback mode], [real fps], [frame num], [sample_id], [fx on/off], [first frame],");
	veejay_msg(VEEJAY_MSG_INFO,
			"          [last_frame],[speed],[slow],[looptype],[in point],[out point],[num samples],");
	veejay_msg(VEEJAY_MSG_INFO,
			"	      [second per video frame],[cycle count low],[cycle count high],[macro status]) ");
	veejay_msg(VEEJAY_MSG_INFO,"\n");
#endif

}
 
/* dump the OSC address space to screen */
int vj_osc_setup_addr_space(void *d) {
	char addr[255];
	vj_osc *o = (vj_osc*) d;
	//struct OSCMethodQueryResponseInfoStruct qri;

	if(OSCGetAddressString( addr, 255, o->container ) == FALSE) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get address space of OSC");
		return -1;
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "Address of top level container [%s]",addr);
	return 0;
}


/* get a packet */
int vj_osc_get_packet(void *d) {
	vj_osc *o = (vj_osc*) d;
#ifdef HAVE_LIBLO
	osc_iterate_clients();
#endif
   /* see if there is something to read , this is effectivly NetworkPacketWaiting */
  // if(ioctl( o->sockfd, FIONREAD, &bytes,0 ) == -1) return 0;
  // if(bytes==0) return 0;
   if(NetworkPacketWaiting(o->packet)==TRUE) {
     /* yes, receive packet from UDP */
     if(NetworkReceivePacket(o->packet) == TRUE) {
	/* OSC must accept this packet (OSC will auto-invoke it, see source !) */
	OSCAcceptPacket(o->packet);
	/* Is this really productive ? */
	OSCBeProductiveWhileWaiting();
	/* tell caller we had 1 packet */	
	return 1;
      }
    }
    return 0;
}


