/*
 * Copyright (C) 2002-2004 Niels Elburg <nelburg@looze.net>
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

/* libOMC_dirty 

   initial test implementation
   use ./sendOSC from ${veejay_package_dir}/libOMC/test
   to send OSC messages to port VJ_PORT + 2 (usually 3492)


  /video/
    /video/playbackward  : Play backward
    /video/playforward   : Play forward
    /video/stop	         : Stop
    /video/gotostart     : Skip to start
    /video/gotoend       : Skip to end
    /video/speed         : Set playback speed <num>
	
   */
#include <config.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <libOMC/libomc.h>
#include "vj-common.h"
#include "vj-OSC.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "vj-OSC.h"
#include <netinet/in.h>
#include "vj-lib.h"
#include "vj-global.h"
#include "vj-event.h"

static veejay_t *osc_info;

static int _old_p_mode = 0;

/* VIMS does the job */
extern void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen);


#define OSC_STRING_SIZE 255
#define OPUSH(p) { _old_p_mode = osc_info->uc->playback_mode; osc_info->uc->playback_mode = p; } 
#define OPOP()  { osc_info->uc->playback_mode = _old_p_mode; } 

/* convert a big endian 32 bit string to an int for internal use */
static int toInt(char* b) {
   return (( (int) b[3] ) & 0xff ) + ((((int) b[2]) & 0xff) << 8) + ((((int) b[1]) & 0xff) << 16) +
	  ((((int) b[0] ) & 0xff) << 24);
}
/* parse int arguments */
static int vj_osc_count_int_arguments(int arglen, const void *vargs) {
	int num_args = 0;

	if(arglen <= 4) return 0; /* no arguments */

	/* figure out how many arguments to parse */
	switch(arglen) {
		case 8: num_args = 1; break;
		case 12: num_args = 2; break;
		case 20: num_args = 3; break; /* more than 2 */
		case 24: num_args = 4; break;
		case 28: num_args = 5; break;
		case 32: num_args = 6; break;
		case 40: num_args = 7; break; /* more than 6 */
		case 44: num_args = 8; break;
		case 48: num_args = 9; break;
		case 52: num_args = 10; break;
	}
	return num_args;
}

static int do_str_parse(int arglen,const void *vargs)
{
	char *args = (char*) vargs;
	if( args[1] == 0x73 ) return 1;
	return 0;
}

static int vj_osc_parse_char_arguments(int arglen, const void *vargs, char *dst)
{
	char *args = (char*)vargs;
	if(arglen <= 4) return 0;
	arglen -= 4;
	if(args[1] == 0x73)
	{
		strncpy(dst, args+4, (arglen>OSC_STRING_SIZE ? OSC_STRING_SIZE : arglen));
		return arglen;
	}
	return 0;
}
/* parse int arguments */
static int vj_osc_parse_int_arguments(int arglen, const void *vargs, int *arguments) {
	
	int num_args = 0;
	int i=0;
	int offset = 0;
	char *args = (char*)vargs;

	if(arglen <= 4) return 0; /* no arguments */

	/* 
           0 arguments , arglen = 4
           1 arguments , arglen = 8
           2 arguments , arglen = 12
	   3 arguments , arglen = 20 
	   4 arguments , arglen = 24
	   5 arguments , arglen = 28
	   6 arguments , arglen = 32 
	   7 arguments , arglen = 40
	   8 arguments , arglen = 44
	   9 arguments , arglen = 48
          10 arguments , arglen = 52

	*/	

	/* figure out how many arguments to parse */
	switch(arglen) {
		case 8: num_args = 1; break;
		case 12: num_args = 2; break;
		case 20: num_args = 3; break; /* more than 2 */
		case 24: num_args = 4; break;
		case 28: num_args = 5; break;
		case 32: num_args = 6; break;
		case 40: num_args = 7; break; /* more than 6 */
		case 44: num_args = 8; break;
		case 48: num_args = 9; break;
		case 52: num_args = 10; break;
	}


	/* max 2 arguments */

	if(arglen <= 12) {
	  offset = 4;
	}

	/* > 2 and < 6 */
	if(arglen >= 20 && arglen <= 32) {
	  offset = 8;
	}

	/* > 6 */
	if(arglen >= 40) {
		offset = 12;
	}

	/* actual parsing */
	for(i=0; i < num_args; i++) {
	  arguments[i] = toInt(args + offset);
	  offset += 4;
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


///////////////////////////////////// CALLBACKS FOR OSC ////////////////////////////////////////

#define PNET_F(a,b,c,d)\
{\
int arg[4];\
int arguments[16];\
int num_arg = vj_osc_parse_int_arguments(a,b,arg);\
arguments[0] = 0;\
arguments[1] = -1;\
arguments[2] = d;\
arguments[3] = arg[0];\
vj_event_fire_net_event( osc_info, c, NULL, arguments, num_arg + 3);\
}

#define SNET_F(a,b,c)\
{\
char str[OSC_STRING_SIZE];\
vj_osc_parse_char_arguments(a,b,str);\
printf("SNET: [%s]\n",str);\
vj_event_fire_net_event(osc_info, c, str,NULL, 1);\
}


#define NET_F(a,b,c)\
{\
int arguments[16];\
int num_arg = vj_osc_parse_int_arguments(a,b,arguments);\
vj_event_fire_net_event( osc_info, c, NULL,arguments, num_arg );\
}

/* DNET does some wacky things with arguments,
   if 1 argument is missing , a default clip is chosen,
   if 2 arguments are missing, a default clip and entry id is chosen.
   if there are no arguments no event is fired
*/

#define DNET_F(a,b,c)\
{\
int c_a = vj_osc_count_int_arguments(a,b);\
int num_arg = vj_event_get_num_args(c);\
int arguments[16];\
int n_a;\
if(c_a >= 0) {\
if( (num_arg-1) == c_a ) {\
arguments[0]=0;\
n_a = vj_osc_parse_int_arguments(a,b,arguments+1);\
vj_event_fire_net_event( osc_info,c, NULL,arguments,num_arg );\
}\
else {\
if ( (num_arg-2) == c_a) {\
arguments[0]=0;\
arguments[1]=-1;\
n_a = vj_osc_parse_int_arguments(a,b,arguments+2);\
vj_event_fire_net_event( osc_info,c,NULL,arguments,num_arg );\
} else {\
 if( num_arg == vj_osc_parse_int_arguments(a,b,arguments) ) { vj_event_fire_net_event(osc_info,c,NULL,arguments,c_a); }\
}\
}\
}\
}

#define DSNET_F(a,b,c)\
{\
int c_a = vj_osc_count_int_arguments(a,b);\
int num_arg = vj_event_get_num_args(c);\
int arguments[16];\
int n_a;\
if(c_a == 0) {\
arguments[0]=-1;\
n_a = vj_osc_parse_int_arguments(a,b,arguments+1);\
vj_event_fire_net_event( osc_info,c, NULL,arguments,num_arg );\
}\
else {\
 if( num_arg == vj_osc_parse_int_arguments(a,b,arguments) ) { vj_event_fire_net_event(osc_info,c,NULL,arguments,c_a); }\
}\
}


/* /video/playforward */
void vj_osc_cb_play_forward(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, NET_VIDEO_PLAY_FORWARD);

}
/* /video/playbackward */
void vj_osc_cb_play_backward(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, NET_VIDEO_PLAY_BACKWARD);
}



/* /video/stop */
void vj_osc_cb_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, 
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs, NET_VIDEO_PLAY_STOP);

}
/* /video/speed */
void vj_osc_cb_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, NET_VIDEO_SET_SPEED );
}
/* /clip/dup */
void vj_osc_cb_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {


}
/* /clip /video/gotostart */
void vj_osc_cb_skip_to_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs,NET_VIDEO_GOTO_START);
}
/* /clip /video/gotoend */
void vj_osc_cb_skip_to_end(void *context, int arglen, const void *vargs, OSCTimeTag when,	
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs,NET_VIDEO_GOTO_END);
}


void vj_osc_cb_set_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_VIDEO_SET_FRAME);
}

void vj_osc_cb_set_slow(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_VIDEO_SET_SLOW);
}

void vj_osc_cb_next_second(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_VIDEO_SKIP_SECOND);
}

void vj_osc_cb_prev_second(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_VIDEO_PREV_SECOND);
}

void vj_osc_cb_next_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_VIDEO_SKIP_FRAME);
}

void vj_osc_cb_prev_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_VIDEO_PREV_FRAME);
}

void vj_osc_cb_new_clip(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_CLIP_NEW);
}


void vj_osc_cb_select_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_SET_CLIP_START);
}


void vj_osc_cb_select_end(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_SET_CLIP_END);
}

void vj_osc_cb_clip_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, NET_CLIP_DEL);
}


void vj_osc_cb_select_clip(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DSNET_F(arglen, vargs, NET_CLIP_SELECT);
}

void vj_osc_cb_clip_set_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CLIP_SET_START);
}

void vj_osc_cb_clip_set_end(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CLIP_SET_END);
}

void vj_osc_cb_clip_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CLIP_SET_DUP);
}

void vj_osc_cb_clip_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs,NET_CLIP_SET_SPEED);
}

void vj_osc_cb_clip_set_looptype(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CLIP_SET_LOOPTYPE);
}

void vj_osc_cb_clip_set_marker(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CLIP_SET_MARKER);
}

void vj_osc_cb_clip_clear_marker(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CLIP_CLEAR_MARKER);
}

void vj_osc_cb_clip_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_CLIP_REC_START);
}

void vj_osc_cb_clip_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_CLIP_REC_STOP);
}

void vj_osc_cb_chain_entry_disable_video(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
}

void vj_osc_cb_chain_entry_enable_video(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_ON);
}

void vj_osc_cb_chain_entry_disable_audio(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
}

void vj_osc_cb_chain_entry_enable_audio(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_ON);
}

void vj_osc_cb_chain_entry_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_CLEAR);
}

void vj_osc_cb_chain_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_SET_ENTRY);
}

void vj_osc_cb_chain_entry_default(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_DEFAULTS);
}

void vj_osc_cb_chain_entry_preset(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_PRESET);
}

void vj_osc_cb_chain_entry_set(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_EFFECT);
}

void vj_osc_cb_chain_entry_set_arg_val(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL);
}

void vj_osc_cb_chain_entry_set_volume(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_VOL);
}

void vj_osc_cb_chain_entry_channel(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_CHANNEL);

}

void vj_osc_cb_chain_entry_source(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_SOURCE);

}

void vj_osc_cb_chain_fade_in(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_FADE_IN);
}

void vj_osc_cb_chain_fade_out(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_FADE_OUT);
}

void vj_osc_cb_chain_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_ENABLE);
}

void vj_osc_cb_chain_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen,vargs,NET_CHAIN_DISABLE);
}

void vj_osc_cb_tag_record_offline_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_TAG_OFFLINE_REC_START);
}


void vj_osc_cb_tag_record_offline_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_TAG_OFFLINE_REC_STOP);
}

void vj_osc_cb_tag_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_TAG_REC_START);
}

void vj_osc_cb_tag_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_TAG_REC_STOP);
}

void vj_osc_cb_tag_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DSNET_F(arglen,vargs,NET_TAG_SELECT);
}


void vj_osc_cb_clip_chain_add(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_EFFECT);
	OPOP();

}

void vj_osc_cb_clip_chain_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_CLEAR);
	OPOP();
}

void vj_osc_cb_clip_chain_preset(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_PRESET);
	OPOP();

}
void vj_osc_cb_clip_chain_entry_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_ON);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_ON);
	OPOP();

}

void vj_osc_cb_clip_chain_entry_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_OFF);
	OPOP();
}

void vj_osc_cb_clip_chain_entry_video_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_ON);
	OPOP();
}

void vj_osc_cb_clip_chain_entry_video_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
	OPOP();

}

void vj_osc_cb_clip_chain_entry_audio_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_ON);
	OPOP();

}
void vj_osc_cb_clip_chain_fade_in(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen, vargs, NET_CHAIN_FADE_IN);
	OPOP();
}

void vj_osc_cb_clip_chain_fade_out(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_FADE_OUT);
	OPOP();
}

void vj_osc_cb_clip_chain_entry_audio_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_OFF);
	OPOP();

}
void vj_osc_cb_clip_chain_set_volume(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_VOL);
	OPOP();

}

void vj_osc_cb_clip_chain_set_param(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL);
	OPOP();

}

void vj_osc_cb_set_parameter0(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,0);
}

void vj_osc_cb_set_parameter1(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,1);
}

void vj_osc_cb_set_parameter2(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,2);
}

void vj_osc_cb_set_parameter3(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,3);
}

void vj_osc_cb_set_parameter4(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,4);
}

void vj_osc_cb_set_parameter5(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,5);
}

void vj_osc_cb_set_parameter6(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,6);
}

void vj_osc_cb_set_parameter7(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,7);
}


void vj_osc_cb_set_parameter8(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	PNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL,8);
}


void vj_osc_cb_tag_chain_add(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_EFFECT);
	OPOP();

}

void vj_osc_cb_tag_chain_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_CLEAR);
	OPOP();

}

void vj_osc_cb_tag_chain_preset(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_PRESET);
	OPOP();

}
void vj_osc_cb_tag_chain_entry_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_ON);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_ON);
	OPOP();
}

void vj_osc_cb_tag_chain_entry_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_OFF);
	OPOP();
}

void vj_osc_cb_tag_chain_entry_video_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_ON);
	OPOP();

}

void vj_osc_cb_tag_chain_entry_video_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_VIDEO_OFF);
	OPOP();

}

void vj_osc_cb_tag_chain_entry_audio_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_ON);
	OPOP();

}

void vj_osc_cb_tag_chain_entry_audio_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_OFF);
	OPOP();
}
void vj_osc_cb_tag_chain_set_volume(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_AUDIO_VOL);
	OPOP();
}

void vj_osc_cb_tag_chain_set_param(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENTRY_SET_ARG_VAL);
	OPOP();
}

void vj_osc_cb_tag_chain_fade_in(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen, vargs, NET_CHAIN_FADE_IN);
	OPOP();
}

void vj_osc_cb_tag_chain_fade_out(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_FADE_OUT);
	OPOP();
}

void vj_osc_cb_clip_chain_entry_channel(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_CHANNEL);
	OPOP();
}

void vj_osc_cb_clip_chain_entry_source(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_SOURCE);
	OPOP();
}

void vj_osc_cb_clip_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen, vargs, NET_CHAIN_SET_ENTRY);
	OPOP();
}



void vj_osc_cb_tag_chain_entry_channel(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_CHANNEL);
	OPOP();
}

void vj_osc_cb_tag_chain_entry_source(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_SOURCE);
	OPOP();
}

void vj_osc_cb_tag_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen, vargs, NET_CHAIN_SET_ENTRY);
	OPOP();
}

void vj_osc_cb_tag_chain_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) 
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_ENABLE);
	OPOP();
}
void vj_osc_cb_tag_chain_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_TAG);
	DNET_F(arglen,vargs,NET_CHAIN_DISABLE);
	OPOP();
} 

void vj_osc_cb_clip_chain_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_ENABLE);
	OPOP();

}

void vj_osc_cb_clip_chain_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	OPUSH(VJ_PLAYBACK_MODE_CLIP);
	DNET_F(arglen,vargs,NET_CHAIN_DISABLE);
	OPOP();
}

void vj_osc_cb_tag_new_v4l(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_TAG_NEW_V4L);
}

void vj_osc_cb_tag_new_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_TAG_NEW_Y4M);
}

void vj_osc_cb_tag_new_rgb(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_TAG_NEW_RAW);
}

void vj_osc_cb_tag_new_vloop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	if(do_str_parse(arglen,vargs) == 1)
	{
		SNET_F(arglen,vargs,NET_TAG_NEW_VLOOP_BY_NAME);
	}
	else
	{
		NET_F(arglen,vargs,NET_TAG_NEW_VLOOP_BY_ID);
	}
}


void vj_osc_cb_load_cliplist(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_CLIP_LOAD_CLIPLIST);
}

void vj_osc_cb_save_cliplist(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_CLIP_SAVE_CLIPLIST);
}

void vj_osc_cb_save_editlist(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_EDITLIST_SAVE);
}

void vj_osc_cb_output_start_vloopback(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_VLOOPBACK_STARTN);
}
void vj_osc_cb_output_start_y4m(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_Y4M_START);
}

void vj_osc_cb_output_start_rgb24(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_RAW_START);
}
void vj_osc_cb_output_stop_vloopback(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_OUTPUT_VLOOPBACK_STOP);
}
void vj_osc_cb_output_stop_y4m(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_Y4M_STOP);
}

void vj_osc_cb_output_stop_rgb24(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_RAW_STOP);
}


/* initialize the pointer to veejay_t */

void vj_osc_set_veejay_t(veejay_t *info) {
	osc_info = info;
}

void vj_osc_free(vj_osc *c)
{
	if(c==NULL) return;
	if(c->leaves) free(c->leaves);
	if(c) free(c);
	c = NULL;
	
}
/* initialization, setup a UDP socket and invoke OSC magic */
vj_osc* vj_osc_allocate(int port_id) {
	vj_osc *o = (vj_osc*)vj_malloc(sizeof(vj_osc));
	o->osc_args = (osc_arg*)vj_malloc(20 * sizeof(*o->osc_args));
	o->rt.InitTimeMemoryAllocator = _vj_osc_time_malloc;
	o->rt.RealTimeMemoryAllocator = _vj_osc_rt_malloc;
	o->rt.receiveBufferSize = 1024;
	o->rt.numReceiveBuffers = 50;
	o->rt.numQueuedObjects = 100;
	o->rt.numCallbackListNodes = 150;
	o->leaves = (OSCcontainer*) vj_malloc(sizeof(OSCcontainer) * 50);
	o->t.initNumContainers = 50;
	o->t.initNumMethods = 150;
	o->t.InitTimeMemoryAllocator = _vj_osc_time_malloc;
	o->t.RealTimeMemoryAllocator = _vj_osc_rt_malloc;
			
	if(OSCInitReceive( &(o->rt))==FALSE) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize OSC receiver");
		return NULL;
	} 
	o->packet = OSCAllocPacketBuffer();
 	
	if(NetworkStartUDPServer( o->packet, port_id) != TRUE) {
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Cannot start OSC/UDP server at port %d ",
				port_id);
	}

	/* Top level container / */
	o->container = OSCInitAddressSpace(&(o->t));

	OSCInitContainerQueryResponseInfo( &(o->cqinfo) );
	o->cqinfo.comment = "Video commands";

	/* Create containers /video , /clip, /chain and /tag */
	if( ( o->leaves[0] = OSCNewContainer("video", o->container, &(o->cqinfo))) == 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container 'video'");
		return NULL;
	}
	if( ( o->leaves[13] = OSCNewContainer("set", o->leaves[0], &(o->cqinfo))) == 0) {
		return NULL;
	}

	/* Create container for Clip */
	o->cqinfo.comment = "Clip commands";
	if( ( o->leaves[1] = OSCNewContainer("clip",o->container, &(o->cqinfo))) == 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container clip");
		return NULL;
	}
		o->cqinfo.comment = "Set clip properties";
		if( ( o->leaves[2] = OSCNewContainer("set", o->leaves[1], &(o->cqinfo))) == 0) {
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container 'set'");
			return NULL;
		}
		o->cqinfo.comment = "Clip recorder";
		if ( (o->leaves[3] = OSCNewContainer("record", o->leaves[1], &(o->cqinfo))) == 0) {
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container 'record'");
			return NULL;
		}
		o->cqinfo.comment = "Clip Effect Chain";
		if ( (o->leaves[4] = OSCNewContainer("chain",o->leaves[1], &(o->cqinfo)))==0) {
			return NULL;
		}
		o->cqinfo.comment = "Clip Effect Chain Entry";
		if ( (o->leaves[12] = OSCNewContainer("entry", o->leaves[4], &(o->cqinfo)))==0) {
			return NULL;
		}
	/* Create container for Chain */
	o->cqinfo.comment = "Chain commands";
	if( ( o->leaves[5] = OSCNewContainer("chain",o->container,&(o->cqinfo)))==0) {
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot create container chain");
		return NULL;
	}
		o->cqinfo.comment = "Chain entry commands";
		if( (o->leaves[6] = OSCNewContainer("entry", o->leaves[5], &(o->cqinfo)))==0) {
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container 'entry'");
			return NULL;
		}
			o->cqinfo.comment = "set";
			if( ( o->leaves[22] = OSCNewContainer("set", o->leaves[6], &(o->cqinfo)))==0) {
				return NULL;
			}
	/* Create container for Tag */
	o->cqinfo.comment = "stream commands";
	if( ( o->leaves[7] = OSCNewContainer("stream", o->container, &(o->cqinfo))) == 0) {
		return NULL;
	}
		o->cqinfo.comment = "stream recording";
		if( ( o->leaves[8] = OSCNewContainer("record", o->leaves[7], &(o->cqinfo)))==0) {
			return NULL;
		}
		o->cqinfo.comment = "stream offline recording";	
		if( ( o->leaves[9] = OSCNewContainer("offline", o->leaves[8], &(o->cqinfo)))==0) {
			return NULL;
		}
		o->cqinfo.comment = "Stream Effect Chain";
		if( ( o->leaves[10] = OSCNewContainer("chain", o->leaves[7], &(o->cqinfo)))==0) {
			return NULL;
		}
		o->cqinfo.comment = "Stream Effect Chain Entry";
		if( ( o->leaves[11] = OSCNewContainer("entry",o->leaves[10], &(o->cqinfo)))==0) {
			return NULL;
		}

		o->cqinfo.comment = "New Stream";
		if( ( o->leaves[14] = OSCNewContainer("new", o->leaves[7],&(o->cqinfo)))==0) {
			return NULL;
		}

	o->cqinfo.comment = "Output Video";
	if( ( o->leaves[15] = OSCNewContainer("output", o->container, &(o->cqinfo)))==0) {
		return NULL;
	}
	o->cqinfo.comment = "vloopback";
	if( ( o->leaves[17] = OSCNewContainer("vloopback",o->leaves[15],&(o->cqinfo)))==0) {
		return NULL;
	}
	o->cqinfo.comment = "y4m";
	if( ( o->leaves[18] = OSCNewContainer("y4m",o->leaves[15], &(o->cqinfo)))==0) {
		return NULL;
	}
	o->cqinfo.comment = "rgb24";
	if( ( o->leaves[19] = OSCNewContainer("rgb24",o->leaves[15],&(o->cqinfo)))==0) {
		return NULL;
	}
		o->ris.description = "to vloopback";
		OSCNewMethod("start", o->leaves[17], vj_osc_cb_output_start_vloopback, &(o->osc_args[0]),&(o->ris));
		o->ris.description = "to yuv4mpeg";
		OSCNewMethod("start", o->leaves[18], vj_osc_cb_output_start_y4m, &(o->osc_args[0]),&(o->ris));
		o->ris.description = "to rgb24";
		OSCNewMethod("start", o->leaves[19], vj_osc_cb_output_start_rgb24,&(o->osc_args[0]),&(o->ris));

		o->ris.description = "to vloopback";
		OSCNewMethod("stop", o->leaves[17], vj_osc_cb_output_stop_vloopback, &(o->osc_args[0]),&(o->ris));
		o->ris.description = "to yuv4mpeg";
		OSCNewMethod("stop", o->leaves[18], vj_osc_cb_output_stop_y4m, &(o->osc_args[0]),&(o->ris));
		o->ris.description = "to rgb24";
		OSCNewMethod("stop", o->leaves[19], vj_osc_cb_output_stop_rgb24,&(o->osc_args[0]),&(o->ris));


	o->cqinfo.comment = "ClipList";
	if( ( o->leaves[20] = OSCNewContainer("cliplist", o->container,&(o->cqinfo)))==0){
		return NULL;
	}
		o->ris.description = "Load cliplist";
		OSCNewMethod("load", o->leaves[20], vj_osc_cb_load_cliplist, &(o->osc_args[0]),&(o->ris));
		o->ris.description = "Save cliplist";
		OSCNewMethod("save", o->leaves[20], vj_osc_cb_save_cliplist, &(o->osc_args[0]),&(o->ris));

	o->cqinfo.comment = "EditList";
	if( ( o->leaves[21] = OSCNewContainer("editlist",o->container,&(o->cqinfo)))==0)
	{
		return NULL;
	}

	o->ris.description = "Save editlist";
	OSCNewMethod("save", o->leaves[21], vj_osc_cb_save_editlist,&(o->osc_args[0]),&(o->ris));

	o->cqinfo.comment = "Gesture Control";

	OSCInitMethodQueryResponseInfo( &(o->ris));

	/* now setup the Methods that must be called when a packet is ready */
	o->ris.description = "Play backward";
	OSCNewMethod("play_reverse", o->leaves[0], vj_osc_cb_play_backward, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Play forward";
	OSCNewMethod("play_forward", o->leaves[0], vj_osc_cb_play_forward, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Stop";
	OSCNewMethod("play_pause", o->leaves[0], vj_osc_cb_stop, &(o->osc_args[0]), &(o->ris)); 
	o->ris.description = "Skip to start";
	OSCNewMethod("goto_start", o->leaves[0], vj_osc_cb_skip_to_start, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Skip to end";
	OSCNewMethod("goto_end", o->leaves[0], vj_osc_cb_skip_to_end, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Set playback speed <num>";
	OSCNewMethod("speed", o->leaves[13], vj_osc_cb_set_speed, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Set frame <num>";
	OSCNewMethod("frame", o->leaves[13], vj_osc_cb_set_frame, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Set slow <num>";
	OSCNewMethod("slow", o->leaves[13], vj_osc_cb_set_slow, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Next second";
	OSCNewMethod("next_second", o->leaves[0], vj_osc_cb_next_second, &(o->osc_args[0]),&(o->ris));
	o->ris.description = "Prev second";	
	OSCNewMethod("prev_second", o->leaves[0], vj_osc_cb_prev_second, &(o->osc_args[0]),&(o->ris));
	o->ris.description = "Next frame";
	OSCNewMethod("next_frame", o->leaves[0], vj_osc_cb_next_frame, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Previous frame";
	OSCNewMethod("prev_frame", o->leaves[0], vj_osc_cb_prev_frame, &(o->osc_args[0]), &(o->ris));

	/* Setup Clip methods */
	o->ris.description = "New clip <start> <end>";
	OSCNewMethod("new", o->leaves[1], vj_osc_cb_new_clip, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Set current frame as starting position";
	OSCNewMethod("select_start", o->leaves[1], vj_osc_cb_select_start, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Set current frame as ending position and create";
	OSCNewMethod("select_end", o->leaves[1], vj_osc_cb_select_end, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Delete clip <num>";
	OSCNewMethod("del",o->leaves[1], vj_osc_cb_clip_del, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Select and play clip <num>";
	OSCNewMethod("select", o->leaves[1], vj_osc_cb_select_clip, &(o->osc_args[0]), &(o->ris));
		/* Setup /clip/set */
		o->ris.description = "Update starting frame";
		OSCNewMethod("start", o->leaves[2], vj_osc_cb_clip_set_start, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Update ending frame";
		OSCNewMethod("end", o->leaves[2], vj_osc_cb_clip_set_end, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Change slow factor";
		OSCNewMethod("dup", o->leaves[2], vj_osc_cb_clip_set_dup, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Change speed factor";
		OSCNewMethod("speed", o->leaves[2], vj_osc_cb_clip_set_speed, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Change looptype";
		OSCNewMethod("looptype", o->leaves[2], vj_osc_cb_clip_set_looptype, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Clip marker";
		OSCNewMethod("marker", o->leaves[2], vj_osc_cb_clip_set_marker, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "clear clip marker";
		OSCNewMethod("no_marker", o->leaves[2], vj_osc_cb_clip_clear_marker, &(o->osc_args[0]), &(o->ris));
		/* Setup clip/record/ */
		o->ris.description = "Start clip recorder";
		OSCNewMethod("start", o->leaves[3], vj_osc_cb_clip_record_start, &(o->osc_args[0]), &(o->ris));
		o->ris.description = "Stop clip recorder";
		OSCNewMethod("stop", o->leaves[3], vj_osc_cb_clip_record_stop, &(o->osc_args[0]), &(o->ris));
		/* Setup clip effect chain */
		o->ris.description = "Select entry";
		OSCNewMethod("select", o->leaves[12], vj_osc_cb_clip_entry_select,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Add effect to entry";
		OSCNewMethod("add", o->leaves[12], vj_osc_cb_clip_chain_add,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Del effect from entry";
		OSCNewMethod("del", o->leaves[12], vj_osc_cb_clip_chain_del,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Preset effect on entry";
		OSCNewMethod("preset", o->leaves[12], vj_osc_cb_clip_chain_preset,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry";
		OSCNewMethod("enable", o->leaves[12],vj_osc_cb_clip_chain_entry_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry";
		OSCNewMethod("disable",o->leaves[12],vj_osc_cb_clip_chain_entry_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry video only";
		OSCNewMethod("enable_video", o->leaves[12], vj_osc_cb_clip_chain_entry_video_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry video only";
		OSCNewMethod("disable_video",o->leaves[12], vj_osc_cb_clip_chain_entry_video_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry audio only";
		OSCNewMethod("enable_audio", o->leaves[12], vj_osc_cb_clip_chain_entry_audio_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry audio only";
		OSCNewMethod("disable_audio", o->leaves[12], vj_osc_cb_clip_chain_entry_audio_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "set_volume";
		OSCNewMethod("volume", o->leaves[12],vj_osc_cb_clip_chain_set_volume,&(o->osc_args[0]),&(o->ris));	
		o->ris.description = "change_parameter";
		OSCNewMethod("parameter", o->leaves[12], vj_osc_cb_clip_chain_set_param,&(o->osc_args[0]),&(o->ris));	
		o->ris.description = "set channel";
		OSCNewMethod("channel",o->leaves[12], vj_osc_cb_clip_chain_entry_channel,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "set source";
		OSCNewMethod("source", o->leaves[12], vj_osc_cb_clip_chain_entry_source,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Select entry";
		OSCNewMethod("select", o->leaves[12], vj_osc_cb_clip_entry_select,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "fade_in";
		OSCNewMethod("fade_in",o->leaves[4],vj_osc_cb_clip_chain_fade_in,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "fade_out";
		OSCNewMethod("fade_out",o->leaves[4],vj_osc_cb_clip_chain_fade_out,&(o->osc_args[0]),&(o->ris));
      		o->ris.description = "Enable Chain";
		OSCNewMethod("enable", o->leaves[4], vj_osc_cb_clip_chain_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable Chain";
		OSCNewMethod("disable", o->leaves[4], vj_osc_cb_clip_chain_disable,&(o->osc_args[0]),&(o->ris));
		

	o->ris.description = "Chain entry disable video";
	OSCNewMethod("disable_video", o->leaves[6], vj_osc_cb_chain_entry_disable_video, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry enable video";
	OSCNewMethod("enable_video", o->leaves[6], vj_osc_cb_chain_entry_enable_video, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry disable audio";
	OSCNewMethod("disable_audio", o->leaves[6], vj_osc_cb_chain_entry_disable_audio, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry enable audio";
	OSCNewMethod("enable_audio", o->leaves[6], vj_osc_cb_chain_entry_enable_audio, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry clear";
	OSCNewMethod("del", o->leaves[6], vj_osc_cb_chain_entry_del, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry select";
	OSCNewMethod("select", o->leaves[6], vj_osc_cb_chain_entry_select, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry default";
	OSCNewMethod("default", o->leaves[6], vj_osc_cb_chain_entry_default, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry preset";
	OSCNewMethod("preset", o->leaves[6], vj_osc_cb_chain_entry_preset, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry set effect";
	OSCNewMethod("add", o->leaves[6], vj_osc_cb_chain_entry_set, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain entry set arg val";
	OSCNewMethod("parameter", o->leaves[6], vj_osc_cb_chain_entry_set_arg_val, &(o->osc_args[0]), &(o->ris));

	o->ris.description = "Chain entry set source";
	OSCNewMethod("source",o->leaves[6],vj_osc_cb_chain_entry_source,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "Chain entry set channel";
	OSCNewMethod("channel",o->leaves[6],vj_osc_cb_chain_entry_channel,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "Chain entry set volume";
	OSCNewMethod("volume", o->leaves[6], vj_osc_cb_chain_entry_set_volume, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain Fade in";
	OSCNewMethod("fade_in", o->leaves[5], vj_osc_cb_chain_fade_in, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain Fade out";
	OSCNewMethod("fade_out", o->leaves[5], vj_osc_cb_chain_fade_out, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain enable";
	OSCNewMethod("enable", o->leaves[5], vj_osc_cb_chain_enable, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Chain disable";
	OSCNewMethod("disable", o->leaves[5], vj_osc_cb_chain_disable, &(o->osc_args[0]), &(o->ris));
	

	o->ris.description = "Start offline recording from stream";
	OSCNewMethod("start", o->leaves[9], vj_osc_cb_tag_record_offline_start, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "End offline recording from stream";	
	OSCNewMethod("stop", o->leaves[9], vj_osc_cb_tag_record_offline_stop, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Start recording";
	OSCNewMethod("start", o->leaves[8], vj_osc_cb_tag_record_start, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Stop recording";
	OSCNewMethod("stop", o->leaves[8], vj_osc_cb_tag_record_stop, &(o->osc_args[0]), &(o->ris));
	o->ris.description = "Select Tag for playback";
	OSCNewMethod("select", o->leaves[7], vj_osc_cb_tag_select, &(o->osc_args[0]), &(o->ris));

	o->ris.description = "Create v4l stream";
	OSCNewMethod("v4l",o->leaves[14], vj_osc_cb_tag_new_v4l,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "Create vloopback stream";
	OSCNewMethod("vloopback",o->leaves[14], vj_osc_cb_tag_new_vloop,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "Create y4m stream";
	OSCNewMethod("y4m",o->leaves[14], vj_osc_cb_tag_new_y4m,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "Create rgb24 stream";
	OSCNewMethod("rgb",o->leaves[14], vj_osc_cb_tag_new_rgb,&(o->osc_args[0]),&(o->ris));
	
	/* Setup tag effect chain */
	o->ris.description = "fade_in";
	OSCNewMethod("fade_in",o->leaves[10],vj_osc_cb_tag_chain_fade_in,&(o->osc_args[0]),&(o->ris));
	o->ris.description = "fade_out";
	OSCNewMethod("fade_out",o->leaves[10],vj_osc_cb_tag_chain_fade_out,&(o->osc_args[0]),&(o->ris));
	


		/* tag chain entry */
		o->ris.description = "Add effect to entry";
		OSCNewMethod("add", o->leaves[11], vj_osc_cb_tag_chain_add,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Del effect from entry";
		OSCNewMethod("del", o->leaves[11], vj_osc_cb_tag_chain_del,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Preset effect on entry";
		OSCNewMethod("preset", o->leaves[11], vj_osc_cb_tag_chain_preset,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry";
		OSCNewMethod("enable", o->leaves[11],vj_osc_cb_tag_chain_entry_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry";
		OSCNewMethod("disable",o->leaves[11],vj_osc_cb_tag_chain_entry_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry video only";
		OSCNewMethod("enable_video", o->leaves[11], vj_osc_cb_tag_chain_entry_video_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry video only";
		OSCNewMethod("disable_video",o->leaves[11], vj_osc_cb_tag_chain_entry_video_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Enable entry audio only";
		OSCNewMethod("enable_audio", o->leaves[11], vj_osc_cb_tag_chain_entry_audio_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable entry audio only";
		OSCNewMethod("disable_audio", o->leaves[11], vj_osc_cb_tag_chain_entry_audio_disable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "set_volume";
		OSCNewMethod("volume", o->leaves[11],vj_osc_cb_tag_chain_set_volume,&(o->osc_args[0]),&(o->ris));	
		o->ris.description = "change_parameter";
		OSCNewMethod("parameter", o->leaves[11], vj_osc_cb_tag_chain_set_param,&(o->osc_args[0]),&(o->ris));	
		o->ris.description = "set channel";
		OSCNewMethod("channel",o->leaves[11], vj_osc_cb_tag_chain_entry_channel,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "set source";
		OSCNewMethod("source", o->leaves[11], vj_osc_cb_tag_chain_entry_source,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Select entry";
		OSCNewMethod("select", o->leaves[11], vj_osc_cb_tag_entry_select,&(o->osc_args[0]),&(o->ris));
  		o->ris.description = "Enable Chain";
		OSCNewMethod("enable", o->leaves[10], vj_osc_cb_tag_chain_enable,&(o->osc_args[0]),&(o->ris));
		o->ris.description = "Disable Chain";
		OSCNewMethod("disable", o->leaves[10], vj_osc_cb_tag_chain_disable,&(o->osc_args[0]),&(o->ris));
		

	o->ris.description = "parameter0";
	OSCNewMethod("parameter0", o->leaves[22], vj_osc_cb_set_parameter0, &(o->osc_args[0]), &(o->ris) );
	o->ris.description = "parameter1";
	OSCNewMethod("parameter1", o->leaves[22], vj_osc_cb_set_parameter1, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter2";
	OSCNewMethod("parameter2", o->leaves[22], vj_osc_cb_set_parameter2, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter3";
	OSCNewMethod("parameter3", o->leaves[22], vj_osc_cb_set_parameter3, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter4";
	OSCNewMethod("parameter4", o->leaves[22], vj_osc_cb_set_parameter4, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter5";
	OSCNewMethod("parameter5", o->leaves[22], vj_osc_cb_set_parameter5, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter6";
	OSCNewMethod("parameter6", o->leaves[22], vj_osc_cb_set_parameter6, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter7";
	OSCNewMethod("parameter7", o->leaves[22], vj_osc_cb_set_parameter7, &(o->osc_args[0]),&(o->ris) );
	o->ris.description = "parameter8";
	OSCNewMethod("parameter8", o->leaves[22], vj_osc_cb_set_parameter8, &(o->osc_args[0]),&(o->ris) );

	/* we are done initializing */

     return o;
}	


void vj_osc_dump()
{
	OSCPrintWholeAddressSpace();


}
 
/* dump the OSC address space to screen */
int vj_osc_setup_addr_space(vj_osc *o) {
	char addr[100];
	//struct OSCMethodQueryResponseInfoStruct qri;

	if(OSCGetAddressString( addr, 100, o->container ) == FALSE) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get address space of OSC");
		return -1;
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "Address of top level container [%s]",addr);
	return 0;
}


/* get a packet */
int vj_osc_get_packet(vj_osc *o) {
   //OSCTimeTag tag;
   struct timeval tv;
   tv.tv_sec=0;
   tv.tv_usec = 0;

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

