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
   to send OSC messages to port VJ_PORT + 4 (usually 3494)


*/

#include <config.h>
#include <veejaycore/vims.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <libOSC/libosc.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-lib.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <libveejay/vj-OSC.h>
#include <libveejay/vj-macro.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <libveejay/vj-lib.h>
#include <libveejay/vj-event.h>
#include <libveejay/vj-OSC.h>
#include <veejaycore/libvevo.h>
#include <veejaycore/vevo.h>
static veejay_t *osc_info;

#ifdef HAVE_LIBLO
#include <lo/lo.h>
static vevo_port_t **osc_clients = NULL;
#endif

#include <libOSC/libosc.h>
#include <sys/types.h>
#include <libstream/vj-tag.h>
#include <libsample/sampleadm.h>
#include <veejaycore/vevo.h>

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

typedef struct osc_container_ref_t {
  char *path;
  OSCcontainer container;
} osc_container_ref;

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
  int   *method_ids;
  int    n_methods;
  osc_container_ref *containers;
  int    n_containers;
  int    max_containers;
} vj_osc;


/* VIMS does the job */
extern void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);
extern char *vj_event_vevo_get_event_name( int id );
extern char *vj_event_vevo_get_event_format( int id );
extern int vj_event_vevo_get_num_args( int id );
extern int vj_event_exists( int id );
 

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

static int vj_osc_pad4(int n)
{
	return (n + 3) & ~3;
}

static int vj_osc_strnlen0(const char *s, int max)
{
	int i;
	for(i = 0; i < max; i++) {
		if(s[i] == 0)
			return i;
	}
	return max;
}

static float toFloat(const char *b)
{
	union { uint32_t u; float f; } v;
	v.u = (uint32_t) toInt(b);
	return v.f;
}

static int vj_osc_float_to_int(float v)
{
	return (v < 0.0f) ? (int)(v - 0.5f) : (int)(v + 0.5f);
}

static int vj_osc_parse_arguments(int arglen,
						  const void *vargs,
						  char *str,
						  int str_len,
						  int *arguments,
						  int max_arguments)
{
	const char *args = (const char*) vargs;
	int argc = 0;

	if(str && str_len > 0)
		str[0] = '\0';
	if(arguments && max_arguments > 0)
		memset(arguments, 0, sizeof(int) * max_arguments);
	if(arglen <= 0 || !args)
		return 0;

	if(args[0] == 0x2c) {
		int tag_len = vj_osc_strnlen0(args, arglen);
		int offset = vj_osc_pad4(tag_len + 1);
		int i;

		for(i = 1; i < tag_len && offset < arglen && argc < max_arguments; i++) {
			char tag = args[i];

			if(tag == 'i') {
				if(offset + 4 > arglen)
					break;
				arguments[argc] = toInt(args + offset);
				offset += 4;
				argc++;
			}
			else if(tag == 'f') {
				if(offset + 4 > arglen)
					break;
				arguments[argc] = vj_osc_float_to_int(toFloat(args + offset));
				offset += 4;
				argc++;
			}
			else if(tag == 's') {
				int avail = arglen - offset;
				int slen;
				if(avail <= 0)
					break;
				slen = vj_osc_strnlen0(args + offset, avail);
				if(str && str_len > 0 && str[0] == '\0') {
					int copy = (slen < (str_len - 1)) ? slen : (str_len - 1);
					memcpy(str, args + offset, copy);
					str[copy] = '\0';
				}
				offset += vj_osc_pad4(slen + 1);
				argc++;
			}
			else {
				veejay_msg(VEEJAY_MSG_WARNING, "OSC argument type '%c' is not mapped to VIMS", tag);
				break;
			}
		}
		return argc;
	}

	while((argc < max_arguments) && ((argc * 4 + 4) <= arglen)) {
		arguments[argc] = toInt(args + (argc * 4));
		argc++;
	}

	return argc;
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
	if(c->addr) {
		for ( i = 0; i < c->n_addr; i ++ ) {
			osc_tokens *ot = c->addr[i];
			if(ot == NULL)
				continue;
			if(ot->addr)
				free_token(ot->addr);
			if(ot->descr)
				free(ot->descr);
			free(ot);
		}
		free(c->addr);
	}

	if(c->method_ids)
		free(c->method_ids);
	if(c->containers) {
		for(i = 0; i < c->n_containers; i++)
			if(c->containers[i].path) free(c->containers[i].path);
		free(c->containers);
	}
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
	int __a = vj_osc_parse_arguments(arglen, vargs, str, sizeof(str), args, 16);

	int free_id = -1;
	int i;
	for( i = 0; i < 31; i ++ ) {
	  if(osc_clients[i] == NULL ) {
	    free_id = i;
	    break;
	  }
	}

	if( free_id == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to add more OSC senders");
		return;
	}

	if( __a != 2 || str[0] == '\0') {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid arguments, use HOSTNAME PORT");
		return;
	}
	int port_num = (args[1] > 0) ? args[1] : args[0];
	char port[6];
	snprintf( port, sizeof(port), "%d", port_num );
	char name[1024];
	snprintf(name, sizeof(name), "%s:%s", str,port );
	char *cmd = "/status";
	char *nptr = name;
	if( osc_has_connection( name ) ) {
		veejay_msg(0, "There already exists a status sender for %s",name);
		return;
	}

	osc_clients[ free_id ] = vpn(VEVO_ANONYMOUS_PORT );
	lo_address t 	       = lo_address_new( str, port );

	if(vevo_property_set( osc_clients[free_id], "lo", VEVO_ATOM_TYPE_VOIDPTR,1, &t ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to add lo_address to vevo port");
		return;
	}

	if(vevo_property_set( osc_clients[free_id], "cmd", VEVO_ATOM_TYPE_STRING, 1, &cmd ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to store command '%s'", cmd );
		return;
	}

	if( vevo_property_set( osc_clients[free_id], "connection", VEVO_ATOM_TYPE_STRING,1,&nptr ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Unable to store connection string");
		return;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Configured OSC sender to %s:%s, send /status [ArgList] every cycle",
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
				 sample_size(),
				 (int)( 100.0f/osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 vj_macro_get_status(sample_get_macro(osc_info->uc->sample_id)) );
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
				 vj_tag_size(),
				 (int) ( 100.0f/osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 vj_macro_get_status(vj_tag_get_macro(osc_info->uc->sample_id)));
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
				 (sample_size() + vj_tag_size()),
				 (int) ( 100.0f / osc_info->settings->spvf ),
				 osc_info->settings->cycle_count[0],
				 osc_info->settings->cycle_count[1],
				 0
				 );
				 
		break;
	}

	if( err == -1 ) {
		lo_address_free( t );
		return 0;
	}	
	return 1;
}

static	void	osc_iterate_clients(void)
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
	int __a = vj_osc_parse_arguments(arglen, vargs, str, sizeof(str), args, 16);
		
	vj_event_fire_net_event(osc_info, vims_id, str, args, __a, 0);
}


//@ setup osc<-> vims mapping
#define MAX_ADDR 2048
#define MAX_OSC_CONTAINERS 512
#define VJ_OSC_VIMS_MAX 1024

typedef struct osc_alias_layout_t {
	const char *name;
	int vims_id;
} osc_alias_layout;

static const osc_alias_layout osc_alias_layouts[] = {
	{ "fxlist/inc"                     , VIMS_FXLIST_INC },
	{ "fxlist/dec"                     , VIMS_FXLIST_DEC },
	{ "fxlist/enter"                   , VIMS_FXLIST_ADD },
	{ "fxlist/setbg"                   , VIMS_EFFECT_SET_BG },
	{ "ui/preview"                     , VIMS_PREVIEW_BW },
	{ "macro/macro"                    , VIMS_MACRO },
	{ "macro/select"                   , VIMS_MACRO_SELECT },
	{ "composite/select"               , VIMS_COMPOSITE },
#ifdef USE_GDK_PIXBUF
	{ "console/screenshot"             , VIMS_SCREENSHOT },
#else
#ifdef HAVE_JPEG
	{ "console/screenshot"             , VIMS_SCREENSHOT },
#endif
#endif
	{ "console/framerate"              , VIMS_FRAMERATE },
	{ "console/bezerk"                 , VIMS_BEZERK },
#ifdef HAVE_SDL
	{ "console/resize"                 , VIMS_RESIZE_SDL_SCREEN },
#endif
	{ "console/renderdepth"            , VIMS_RENDER_DEPTH },
	{ "console/volume"                 , VIMS_SET_VOLUME },
	{ "console/fullscreen"             , VIMS_FULLSCREEN },
	{ "console/suspsend"               , VIMS_SUSPEND },
	{ "console/quit"                   , VIMS_QUIT },
	{ "console/close"                  , VIMS_CLOSE },
	{ "console/dataformat"             , VIMS_RECORD_DATAFORMAT },
	{ "console/playmode"               , VIMS_SET_PLAIN_MODE },
	{ "console/load"                   , VIMS_SAMPLE_LOAD_SAMPLELIST },
	{ "console/save"                   , VIMS_SAMPLE_SAVE_SAMPLELIST },
	{ "console/switch"                 , VIMS_SWITCH_SAMPLE_STREAM },
	{ "audio/enable"                   , VIMS_AUDIO_ENABLE },
	{ "audio/disable"                  , VIMS_AUDIO_DISABLE },
	{ "bank/select"                    , VIMS_SELECT_BANK },
	{ "bank/slot"                      , VIMS_SELECT_ID },
	{ "record/autostart"               , VIMS_REC_AUTO_START },
	{ "record/stop"                    , VIMS_REC_STOP },
	{ "record/start"                   , VIMS_REC_START },
	{ "sample/mode"                    , VIMS_SAMPLE_MODE },
	{ "sample/play"                    , VIMS_SET_MODE_AND_GO },
	{ "sample/rand/start"              , VIMS_SAMPLE_RAND_START },
	{ "sample/rand/stop"               , VIMS_SAMPLE_RAND_STOP },
	{ "sample/rec/start"               , VIMS_SAMPLE_REC_START },
	{ "sample/start"                   , VIMS_SET_SAMPLE_START },
	{ "sample/end"                     , VIMS_SET_SAMPLE_END },
	{ "sample/new"                     , VIMS_SAMPLE_NEW },
	{ "sample/select"                  , VIMS_SAMPLE_SELECT },
	{ "sample/delete"                  , VIMS_SAMPLE_DEL },
	{ "sample/looptype"                , VIMS_SAMPLE_SET_LOOPTYPE },
	{ "sample/description"             , VIMS_SAMPLE_SET_DESCRIPTION },
	{ "sample/speed"                   , VIMS_SAMPLE_SET_SPEED },
	{ "sample/startposition"           , VIMS_SAMPLE_SET_START },
	{ "sample/endposition"             , VIMS_SAMPLE_SET_END },
	{ "sample/slow"                    , VIMS_SAMPLE_SET_DUP },
	{ "sample/inpoint"                 , VIMS_SAMPLE_SET_MARKER_START },
	{ "sample/outpoint"                , VIMS_SAMPLE_SET_MARKER_END },
	{ "sample/clearpoints"             , VIMS_SAMPLE_CLEAR_MARKER },
	{ "sample/edladd"                  , VIMS_EDITLIST_ADD_SAMPLE },
	{ "sample/killall"                 , VIMS_SAMPLE_DEL_ALL },
	{ "sample/copy"                    , VIMS_SAMPLE_COPY },
	{ "sample/recstart"                , VIMS_SAMPLE_REC_START },
	{ "sample/recstop"                 , VIMS_SAMPLE_REC_STOP },
	{ "sample/fx/on"                   , VIMS_SAMPLE_CHAIN_ENABLE },
	{ "sample/fx/off"                  , VIMS_SAMPLE_CHAIN_DISABLE },
	{ "sample/looptoggle"              , VIMS_SAMPLE_TOGGLE_LOOP },
	{ "stream/play"                    , VIMS_SET_MODE_AND_GO },
	{ "stream/delete"                  , VIMS_STREAM_DELETE },
	{ "stream/new/v4l"                 , VIMS_STREAM_NEW_V4L },
#ifdef SUPPORT_READ_DV2
	{ "stream/new/dv1394"              , VIMS_STREAM_NEW_DV1394 },
#endif
	{ "stream/new/solid"               , VIMS_STREAM_NEW_COLOR },
	{ "stream/new/y4m"                 , VIMS_STREAM_NEW_Y4M },
	{ "stream/new/cali"                , VIMS_STREAM_NEW_CALI },
	{ "stream/startcali"               , VIMS_V4L_BLACKFRAME },
	{ "stream/savecali"                , VIMS_V4L_CALI },
	{ "stream/unicast"                 , VIMS_STREAM_NEW_UNICAST },
	{ "stream/mcast"                   , VIMS_STREAM_NEW_MCAST },
	{ "stream/new/picture"             , VIMS_STREAM_NEW_PICTURE },
	{ "stream/offline/recstart"        , VIMS_STREAM_OFFLINE_REC_START },
	{ "stream/offline/recstop"         , VIMS_STREAM_OFFLINE_REC_STOP },
	{ "stream/fx/on"                   , VIMS_STREAM_CHAIN_ENABLE },
	{ "stream/fx/off"                  , VIMS_STREAM_CHAIN_DISABLE },
	{ "stream/rec/start"               , VIMS_STREAM_REC_START },
	{ "stream/rec/stop"                , VIMS_STREAM_REC_STOP },
	{ "stream/v4l/brightness"          , VIMS_STREAM_SET_BRIGHTNESS },
	{ "stream/v4l/contrast"            , VIMS_STREAM_SET_CONTRAST },
	{ "stream/v4l/hue"                 , VIMS_STREAM_SET_HUE },
	{ "stream/v4l/color"               , VIMS_STREAM_SET_COLOR },
	{ "stream/v4l/whitebalance"        , VIMS_STREAM_SET_WHITE },
	{ "stream/v4l/saturation"          , VIMS_STREAM_SET_SATURATION },
	{ "stream/length"                  , VIMS_STREAM_SET_LENGTH },
	{ "stream/color"                   , VIMS_STREAM_COLOR },
	{ "video/forward"                  , VIMS_VIDEO_PLAY_FORWARD },
	{ "video/play"                     , VIMS_VIDEO_PLAY_FORWARD },
	{ "video/pause"                    , VIMS_VIDEO_PLAY_STOP },
	{ "video/nextframe"                , VIMS_VIDEO_SKIP_FRAME },
	{ "video/prevframe"                , VIMS_VIDEO_PREV_FRAME },
	{ "video/nextsecond"               , VIMS_VIDEO_SKIP_SECOND },
	{ "video/prevsecond"               , VIMS_VIDEO_PREV_SECOND },
	{ "video/gotostart"                , VIMS_VIDEO_GOTO_START },
	{ "video/gotoend"                  , VIMS_VIDEO_GOTO_END },
	{ "video/frame"                    , VIMS_VIDEO_SET_FRAME },
	{ "video/speed"                    , VIMS_VIDEO_SET_SPEED },
	{ "video/slow"                     , VIMS_VIDEO_SET_SLOW },
	{ "mcast/start"                    , VIMS_VIDEO_MCAST_START },
	{ "mcast/end"                      , VIMS_VIDEO_MCAST_STOP },
	{ "video/speedk"                   , VIMS_VIDEO_SET_SPEEDK },
	{ "editlist/paste"                 , VIMS_EDITLIST_PASTE_AT },
	{ "editlist/copy"                  , VIMS_EDITLIST_COPY },
	{ "editlist/cut"                   , VIMS_EDITLIST_CUT },
	{ "editlist/crop"                  , VIMS_EDITLIST_CROP },
	{ "editlist/add"                   , VIMS_EDITLIST_ADD },
	{ "editlist/save"                  , VIMS_EDITLIST_SAVE },
	{ "editlist/load"                  , VIMS_EDITLIST_LOAD },
	{ "sequence/status"                , VIMS_SEQUENCE_STATUS },
	{ "sequence/set"                   , VIMS_SEQUENCE_ADD },
	{ "sequence/del"                   , VIMS_SEQUENCE_DEL },
	{ "projection/inc"                 , VIMS_PROJ_INC },
	{ "projection/dec"                 , VIMS_PROJ_DEC },
	{ "projection/set"                 , VIMS_PROJ_SET_POINT },
	{ "projection/stack"               , VIMS_PROJ_STACK },
	{ "projection/toggle"              , VIMS_PROJ_TOGGLE },
	{ "chain/enable"                   , VIMS_CHAIN_ENABLE },
	{ "chain/disable"                  , VIMS_CHAIN_DISABLE },
	{ "chain/fadein"                   , VIMS_CHAIN_FADE_IN },
	{ "chain/fadeout"                  , VIMS_CHAIN_FADE_OUT },
	{ "chain/clear"                    , VIMS_CHAIN_CLEAR },
	{ "chain/opacity"                  , VIMS_CHAIN_MANUAL_FADE },
	{ "chain/entry/setarg"             , VIMS_CHAIN_ENTRY_SET_ARG_VAL },
	{ "chain/entry/defaults"           , VIMS_CHAIN_ENTRY_SET_DEFAULTS },
	{ "chain/entry/channel"            , VIMS_CHAIN_ENTRY_SET_CHANNEL },
	{ "chain/entry/source"             , VIMS_CHAIN_ENTRY_SET_SOURCE },
	{ "chain/entry/srccha"             , VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL },
	{ "chain/entry/clear"              , VIMS_CHAIN_ENTRY_CLEAR },
	{ "chain/entry/up"                 , VIMS_CHAIN_ENTRY_UP },
	{ "chain/entry/down"               , VIMS_CHAIN_ENTRY_DOWN },
	{ "chain/entry/srctoggle"          , VIMS_CHAIN_ENTRY_SOURCE_TOGGLE },
	{ "chain/entry/incarg"             , VIMS_CHAIN_ENTRY_INC_ARG },
	{ "chain/entry/decarg"             , VIMS_CHAIN_ENTRY_DEC_ARG },
	{ "chain/entry/state"              , VIMS_CHAIN_ENTRY_SET_STATE },
	{ "chain/channel/inc"              , VIMS_CHAIN_ENTRY_CHANNEL_INC },
	{ "chain/channel/dec"              , VIMS_CHAIN_ENTRY_CHANNEL_DEC },
#ifdef HAVE_FREETYPE
	{ "display/copyright"              , VIMS_COPYRIGHT },
#endif
	{ "vloopback/start"                , VIMS_VLOOPBACK_START },
	{ "vloopback/stop"                 , VIMS_VLOOPBACK_STOP },
	{ "y4m/start"                      , VIMS_OUTPUT_Y4M_START },
	{ "y4m/stop"                       , VIMS_OUTPUT_Y4M_STOP },
#ifdef HAVE_LIBLO
	{ "osc/sender"                     , -2 },
#endif
	{ NULL                              , -1 }
};

static void free_token(char **arr)
{
	int i = 0;
	if(!arr)
		return;
	for(i = 0; arr[i] != NULL; i++)
		free(arr[i]);
	free(arr);
}

static char **vj_osc_tokenize_path(const char *path, int *ntokens)
{
	char **arr;
	char *copy;
	char *p;
	char *start;
	int n = 0;
	int cap = 8;

	if(ntokens)
		*ntokens = 0;
	if(!path || path[0] == '\0')
		return NULL;

	arr = (char**) vj_calloc(sizeof(char*) * cap);
	copy = strdup(path);
	if(!arr || !copy) {
		if(arr) free(arr);
		if(copy) free(copy);
		return NULL;
	}

	p = copy;
	while(*p == '/')
		p++;

	start = p;
	while(1) {
		if(*p == '/' || *p == '\0') {
			int len = (int)(p - start);
			if(len > 0) {
				if(n + 2 > cap) {
					char **tmp;
					cap *= 2;
					tmp = (char**) realloc(arr, sizeof(char*) * cap);
					if(!tmp) {
						free(copy);
						free_token(arr);
						return NULL;
					}
					memset(tmp + n, 0, sizeof(char*) * (cap - n));
					arr = tmp;
				}
				arr[n] = strndup(start, len);
				if(!arr[n]) {
					free(copy);
					free_token(arr);
					return NULL;
				}
				n++;
			}
			if(*p == '\0')
				break;
			start = p + 1;
		}
		p++;
	}

	free(copy);
	arr[n] = NULL;
	if(ntokens)
		*ntokens = n;
	return n > 0 ? arr : NULL;
}

static int vj_osc_skip_vims_id(int id)
{
	return (id > 400 && id < 500);
}

static char *vj_osc_describe_vims_method(int id, const char *alias_path)
{
	char *name = vj_event_vevo_get_event_name(id);
	char *fmt = vj_event_vevo_get_event_format(id);
	int n_args = vj_event_vevo_get_num_args(id);
	char *descr = NULL;
	int len;

	if(!name)
		name = strdup("VIMS event");
	if(!fmt)
		fmt = strdup("");
	if(!name || !fmt) {
		if(name) free(name);
		if(fmt) free(fmt);
		return NULL;
	}

	if(alias_path)
		len = snprintf(NULL, 0, "OSC alias /%s -> VIMS %03d: %s%s%s", alias_path, id, name, n_args > 0 ? " | format: " : "", n_args > 0 ? fmt : "");
	else
		len = snprintf(NULL, 0, "VIMS %03d: %s%s%s", id, name, n_args > 0 ? " | format: " : "", n_args > 0 ? fmt : "");

	if(len < 0) {
		free(name);
		free(fmt);
		return NULL;
	}

	descr = (char*) vj_calloc(sizeof(char) * (len + 1));
	if(descr) {
		if(alias_path)
			snprintf(descr, len + 1, "OSC alias /%s -> VIMS %03d: %s%s%s", alias_path, id, name, n_args > 0 ? " | format: " : "", n_args > 0 ? fmt : "");
		else
			snprintf(descr, len + 1, "VIMS %03d: %s%s%s", id, name, n_args > 0 ? " | format: " : "", n_args > 0 ? fmt : "");
	}

	free(name);
	free(fmt);
	return descr;
}

static OSCcontainer vj_osc_find_container(vj_osc *o, const char *path)
{
	int i;
	if(!o || !path)
		return NULL;
	for(i = 0; i < o->n_containers; i++) {
		if(o->containers[i].path && strcmp(o->containers[i].path, path) == 0)
			return o->containers[i].container;
	}
	return NULL;
}

static int vj_osc_register_container(vj_osc *o, const char *path, OSCcontainer container)
{
	if(!o || !path || !container)
		return 0;
	if(o->n_containers >= o->max_containers)
		return 0;
	o->containers[o->n_containers].path = strdup(path);
	if(!o->containers[o->n_containers].path)
		return 0;
	o->containers[o->n_containers].container = container;
	o->n_containers++;
	return 1;
}

static OSCcontainer vj_osc_get_or_create_container(vj_osc *o,
									 const char *path,
									 const char *name,
									 OSCcontainer parent)
{
	OSCcontainer c = vj_osc_find_container(o, path);
	if(c)
		return c;
	if(!parent || !name)
		return NULL;
	c = OSCNewContainer(name, parent, &(o->cqinfo));
	if(!c)
		return NULL;
	if(!vj_osc_register_container(o, path, c))
		return NULL;
	return c;
}

static int vj_osc_add_path_method(vj_osc *o,
							 const char *path,
							 int vims_id,
							 void (*method)(void *, int, const void *, OSCTimeTag, NetworkReturnAddressPtr),
							 char *description,
							 int *next_addr)
{
	char **arr;
	OSCcontainer parent;
	char prefix[512];
	int ntokens = 0;
	int i;
	int method_slot;

	if(!o || !path || !method || !next_addr)
		return 0;
	if(*next_addr >= MAX_ADDR || o->n_methods >= MAX_ADDR)
		return 0;

	arr = vj_osc_tokenize_path(path, &ntokens);
	if(!arr || ntokens < 2) {
		free_token(arr);
		return 0;
	}

	prefix[0] = '\0';
	parent = o->container;
	for(i = 0; i < ntokens - 1; i++) {
		OSCcontainer c;
		if(prefix[0] == '\0')
			snprintf(prefix, sizeof(prefix), "%s", arr[i]);
		else {
			int off = (int) strlen(prefix);
			snprintf(prefix + off, sizeof(prefix) - off, "/%s", arr[i]);
		}
		c = vj_osc_get_or_create_container(o, prefix, arr[i], parent);
		if(!c) {
			free_token(arr);
			return 0;
		}
		parent = c;
	}

	method_slot = o->n_methods;
	o->method_ids[method_slot] = vims_id;
	o->n_methods++;

	OSCInitMethodQueryResponseInfo(&(o->ris));
	o->ris.description = description ? description : strdup("OSC method");
	OSCNewMethod(arr[ntokens - 1], parent, method, &(o->method_ids[method_slot]), &(o->ris));

	o->addr[*next_addr] = (osc_tokens*) vj_calloc(sizeof(osc_tokens));
	if(!o->addr[*next_addr]) {
		free_token(arr);
		free(o->ris.description);
		return 0;
	}
	o->addr[*next_addr]->n_addr = ntokens;
	o->addr[*next_addr]->addr = arr;
	o->addr[*next_addr]->descr = o->ris.description;
	(*next_addr)++;
	return 1;
}

static int vj_osc_add_vims_selector(vj_osc *o, int id, int *next_addr)
{
	char path[32];
	char *descr;

	if(vj_osc_skip_vims_id(id) || !vj_event_exists(id))
		return 0;

	snprintf(path, sizeof(path), "vims/%03d", id);
	descr = vj_osc_describe_vims_method(id, NULL);
	if(!vj_osc_add_path_method(o, path, id, osc_vims_method, descr, next_addr)) {
		if(descr)
			free(descr);
		return 0;
	}
	return 1;
}

static int vj_osc_add_legacy_alias(vj_osc *o, const osc_alias_layout *alias, int *next_addr)
{
	char *descr;

	if(!alias || !alias->name)
		return 0;

#ifdef HAVE_LIBLO
	if(alias->vims_id == -2) {
		return vj_osc_add_path_method(o,
							  alias->name,
							  -2,
							  osc_add_client,
							  strdup("Setup an OSC sender (argument 0=host, 1=port)"),
							  next_addr);
	}
#endif

	if(alias->vims_id <= 0 || vj_osc_skip_vims_id(alias->vims_id) || !vj_event_exists(alias->vims_id))
		return 0;

	descr = vj_osc_describe_vims_method(alias->vims_id, alias->name);
	if(!vj_osc_add_path_method(o, alias->name, alias->vims_id, osc_vims_method, descr, next_addr)) {
		if(descr)
			free(descr);
		return 0;
	}
	return 1;
}

int 	vj_osc_build_cont(vj_osc *o)
{
	int next_addr = 0;
	int id;
	int added_vims = 0;
	int added_aliases = 0;
	int skipped_queries = 0;
	int skipped_aliases = 0;
	int i;

	o->index = NULL;
	o->addr = (osc_tokens**) vj_calloc(sizeof(osc_tokens*) * MAX_ADDR);
	o->method_ids = (int*) vj_calloc(sizeof(int) * MAX_ADDR);
	o->containers = (osc_container_ref*) vj_calloc(sizeof(osc_container_ref) * MAX_OSC_CONTAINERS);
	o->n_addr = 0;
	o->n_methods = 0;
	o->n_containers = 0;
	o->max_containers = MAX_OSC_CONTAINERS;

	if(!o->addr || !o->method_ids || !o->containers)
		return 0;

	for(id = 1; id < VJ_OSC_VIMS_MAX && next_addr < MAX_ADDR; id++) {
		if(vj_osc_skip_vims_id(id)) {
			skipped_queries++;
			continue;
		}
		if(vj_osc_add_vims_selector(o, id, &next_addr))
			added_vims++;
	}

	for(i = 0; osc_alias_layouts[i].name != NULL && next_addr < MAX_ADDR; i++) {
		int before = next_addr;
		if(vj_osc_add_legacy_alias(o, &(osc_alias_layouts[i]), &next_addr))
			added_aliases++;
		else if(before == next_addr)
			skipped_aliases++;
	}

	o->n_addr = next_addr;
	veejay_msg(VEEJAY_MSG_INFO,
			   "OSC exposed %d dynamic VIMS selectors under /vims/<selector> and %d descriptive aliases (%d query/fetch selectors skipped, %d aliases ignored)",
			   added_vims,
			   added_aliases,
			   skipped_queries,
			   skipped_aliases);
	return added_vims > 0;
}


/* initialization, setup a UDP socket and invoke OSC magic */
void* vj_osc_allocate(int port_id) {
	void *res;
	char tmp[200];
	
	vj_osc *o = (vj_osc*)vj_calloc(sizeof(vj_osc));
#ifdef HAVE_LIBLO
	osc_clients = (vevo_port_t**) vj_malloc(sizeof(vevo_port_t*) * 32);
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
	o->rt.numCallbackListNodes = MAX_ADDR + 32;
	o->leaves = (OSCcontainer*) vj_calloc(sizeof(OSCcontainer) * MAX_OSC_CONTAINERS);
	o->t.initNumContainers = MAX_OSC_CONTAINERS;
	o->t.initNumMethods = MAX_ADDR + 32;
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

//	if( !vj_osc_attach_methods( o ))
//		return NULL;

	res =(void*) o;
     return res;
}	


void vj_osc_dump(void)
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


