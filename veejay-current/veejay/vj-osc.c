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
#include <veejay/vims.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <libOSC/libosc.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-OSC.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "vj-OSC.h"
#include <netinet/in.h>
#include "vj-lib.h"
#include "vj-global.h"
#include "vj-event.h"

static veejay_t *osc_info;

/* VIMS does the job */
extern void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen);


#define OSC_STRING_SIZE 255
#define OPUSH(p) { _old_p_mode = osc_info->uc->playback_mode; osc_info->uc->playback_mode = p; } 
#define OPOP()  { osc_info->uc->playback_mode = _old_p_mode; } 


// forward decl
void vj_osc_cb_play_forward(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_play_backward(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_skip_to_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_skip_to_end(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_frame(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_slow(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_next_second(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_prev_second(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_next_frame(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_prev_frame(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_new_clip(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_end(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_del(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_clip(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_end(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_looptype(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_set_marker(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_clear_marker(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_clip_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_clear(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_disable_video(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_enable_video(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_disable_audio(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_enable_audio(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_del(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_default(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_preset(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_set(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_set_arg_val(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_set_volume(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_channel(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_source(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_manual_fade(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_fade_in(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_fade_out(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_enable(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_disable(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_record_offline_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_record_offline_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_select(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_add(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_preset(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter0(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter1(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter2(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter3(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter4(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter5(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter6(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter7(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_set_parameter8(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_new_v4l(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_tag_new_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_load_cliplist(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_save_cliplist(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_output_start_vloopback(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_output_start_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_output_stop_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_save_editlist(void *context, int arglen, const void *vargs,OSCTimeTag when, NetworkReturnAddressPtr ra);   

void vj_osc_set_veejay_t(veejay_t *t);

void *_vj_osc_time_malloc(int num_bytes) ;
void *_vj_osc_rt_malloc(int num_bytes);
int	vj_osc_attach_methods( vj_osc *o );
int 	vj_osc_build_cont( vj_osc *o );

#ifdef ARCH_X86
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
				// if next is a zero, we have found all occurences.
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
	arglen -= 4;
	if(args[1] == 0x73)
	{
		strncpy(dst, args+4, (arglen>OSC_STRING_SIZE ? OSC_STRING_SIZE : arglen));
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
			// parse the arguments
			veejay_msg(VEEJAY_MSG_DEBUG, "Received typed tag with %d arguments", num_args);
			for ( i = 0; i < num_args ; i ++ )
			{
				arguments[i] = toInt( args + pad + offset );
				offset += 4;
				veejay_msg(VEEJAY_MSG_DEBUG, "Arg %d = %d", i, arguments[i] );
			}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Received raw packet with %d arguments", num_args);
		for(i = 0; i < num_args; i ++)
		{
			arguments[i] = toInt( args + offset);
			offset += 4;
			veejay_msg(VEEJAY_MSG_DEBUG, "Arg %d = %d", i, arguments[i]);
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


///////////////////////////////////// CALLBACKS FOR OSC ////////////////////////////////////////

#define PNET_F(a,b,c,d)\
{\
int arg[4];\
int arguments[16];\
arguments[1] = -1;\
arguments[2] = d;\
arguments[3] = arg[0];\
vj_event_fire_net_event( osc_info, c, NULL, arguments, 4);\
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
if(c_a >= 0 && c != NET_CHAIN_ENTRY_SET_PRESET) {\
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
}\
}\
}\
if(c_a >= 0 && c == NET_CHAIN_ENTRY_SET_PRESET) {\
 vj_osc_parse_int_arguments(a,b,arguments);\
 vj_event_fire_net_event(osc_info,c,NULL,arguments,c_a); } \
}

// DSNET_F takes default sample (last) if none is given

#define DSNET_F(a,b,c)\
{\
int c_a = vj_osc_count_int_arguments(a,b);\
int num_arg = vj_event_get_num_args(c);\
int arguments[16];\
if(c_a == 0) {\
arguments[0]=-1;\
vj_event_fire_net_event( osc_info,c, NULL,arguments,num_arg );\
}\
else {\
 if( c_a == num_arg ) { \
vj_osc_parse_int_arguments(a,b,arguments);\
vj_event_fire_net_event(osc_info,c,NULL,arguments,c_a); }\
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

void	vj_osc_cb_chain_clear( void *ctx, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	DNET_F( arglen, vargs, NET_CHAIN_CLEAR );
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

void	vj_osc_cb_chain_manual_fade(void *ctx, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_MANUAL_FADE );
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

void	vj_osc_cb_chain_add( void *ctx, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	DNET_F( arglen, vargs, NET_CHAIN_ENTRY_SET_EFFECT);
}

void	vj_osc_cb_chain_preset(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	DNET_F(arglen, vargs, NET_CHAIN_ENTRY_SET_PRESET );
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

void vj_osc_cb_tag_new_v4l(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,NET_TAG_NEW_V4L);
}

void vj_osc_cb_tag_new_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_TAG_NEW_Y4M);
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

void vj_osc_cb_output_stop_y4m(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	SNET_F(arglen,vargs,NET_OUTPUT_Y4M_STOP);
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



static struct 
{
	const char	 *descr;
	const char	 *name;
	void	 (*cb)(void *ctx, int len, const void *vargs, OSCTimeTag when,	NetworkReturnAddressPtr ra);
	int		 leave;
} osc_methods[] = 
{
	{ "/video/play",		"play",		vj_osc_cb_play_forward,			0	},	
	{ "/video/reverse",		"reverse",	vj_osc_cb_play_backward,		0	},
	{ "/video/pause",		"pause",	vj_osc_cb_stop,				0	},
	{ "/video/speed",		"speed",	vj_osc_cb_set_speed,			0	},
	{ "/video/slow",		"slow",		vj_osc_cb_set_dup,			0	},
	{ "/video/goto_start",		"goto_start",	vj_osc_cb_skip_to_start,		0	},
	{ "/video/goto_end",		"goto_end",	vj_osc_cb_skip_to_end,			0	},
	{ "/video/set_frame",		"set_frame",	vj_osc_cb_set_frame,			0	},
	{ "/video/prev_frame",		"prev_frame",	vj_osc_cb_prev_frame,			0	},
	{ "/video/next_frame",		"next_frame",	vj_osc_cb_next_frame,			0	},
	{ "/video/next_second",		"next_second",	vj_osc_cb_next_second,			0	},
	{ "/video/prev_second",		"prev_second",	vj_osc_cb_prev_second,			0	},

	{ "/clip/new",			"new",		vj_osc_cb_new_clip,			1	},
	{ "/clip/del",			"del",		vj_osc_cb_clip_del,			1	},
	{ "/clip/select",		"select",	vj_osc_cb_select_clip,			1	},
	{ "/clip/goto_start",		"goto_start",	vj_osc_cb_select_start,			1	},
	{ "/clip/goto_end",		"goto_end",	vj_osc_cb_select_end,			1	},
	{ "/clip/set/start",		"start",	vj_osc_cb_clip_set_start,		13	},
	{ "/clip/set/end",		"end",		vj_osc_cb_clip_set_end,			13	},
	{ "/clip/set/looptype",		"looptype",	vj_osc_cb_clip_set_looptype,		13	},
	{ "/clip/set/speed",		"speed",	vj_osc_cb_clip_set_speed,		13	},
	{ "/clip/set/marker",		"marker",	vj_osc_cb_clip_set_marker,		13	},
	{ "/clip/set/slow",		"slow",		vj_osc_cb_clip_set_dup,			13	},
	{ "/clip/set/nomarker",		"nomarker",	vj_osc_cb_clip_clear_marker,		13	},
	{ "/clip/rec/start",		"start",	vj_osc_cb_clip_record_start,		14	},
	{ "/clip/rec/stop",		"stop",		vj_osc_cb_clip_record_stop,		14	},

	{ "/stream/select",		"select",	vj_osc_cb_tag_select,			2	},
	{ "/stream/new/v4l",		"v4l",		vj_osc_cb_tag_new_v4l,			19	},
	{ "/stream/new/y4m",		"y4m",		vj_osc_cb_tag_new_y4m,			19	},
//	{ "/stream/new/avformat",	"avformat",	vj_osc_cb_tag_new_avformat,		19	},

	{ "/stream/rec/o_start",	"o_start",	vj_osc_cb_tag_record_offline_start,	16	},
	{ "/stream/rec/o_stop",		"o_stop",	vj_osc_cb_tag_record_offline_stop,	16	},
	{ "/stream/rec/start",		"start",	vj_osc_cb_tag_record_start,		16	},
	{ "/stream/rec/stop",		"stop",		vj_osc_cb_tag_record_stop,		16	},

	{ "/chain/reset",		"reset",	vj_osc_cb_chain_clear,			4	},
	{ "/chain/fade_in",		"fade_in",	vj_osc_cb_chain_fade_in,		4	},
	{ "/chain/fade_out",		"fade_out",	vj_osc_cb_chain_fade_out,		4	},
	{ "/chain/enable",		"enable",	vj_osc_cb_chain_enable,			4	},
	{ "/chain/disable",		"disable",	vj_osc_cb_chain_disable,		4	},
	{ "/chain/opacity",		"opacity",	vj_osc_cb_chain_manual_fade,		4	},

	{ "/entry/disable",		"disable",	vj_osc_cb_chain_entry_disable_video,	9	},
	{ "/entry/enable",		"enable",	vj_osc_cb_chain_entry_enable_video,	9	},
	{ "/entry/del",			"del",		vj_osc_cb_chain_entry_del,		9	},
	{ "/entry/select",		"select",	vj_osc_cb_chain_entry_select,		9	},
	{ "/entry/defaults",		"defaults",	vj_osc_cb_chain_entry_default,		9	},
	{ "/entry/preset",		"preset",	vj_osc_cb_chain_entry_preset,		9	},
	{ "/entry/set",			"set",		vj_osc_cb_chain_entry_set,		9	},
	{ "/entry/channel",		"channel",	vj_osc_cb_chain_entry_channel,		9	},
	{ "/entry/source",		"source",	vj_osc_cb_chain_entry_source,		9	},

	{ "/arg/set",			"set",		vj_osc_cb_chain_entry_set_arg_val,	8	},


	{ "/cl/load",			"load",		vj_osc_cb_load_cliplist,		6 	},
	{ "/cl/save",			"save",		vj_osc_cb_save_cliplist,		6	},

	{ NULL,					NULL,		NULL,							0	},
};

static struct
{
	const char *comment;
	const char *name;	
	int  leave;
	int  att; 
	int  it;
} osc_cont[] = 
{
 {	"/video/",	 	"video",	 0, -1,0   	},
 {	"/clip/", 		"clip",		 1, -1,0	},
 {	"/stream/",		"stream",	 2, -1,0	},
 {	"/record/", 		"record",	 3, -1,0	},
 {	"/chain/" , 		"chain",	 4, -1,0	},
 {	"/video/set",		"set",		 12, 0,0	},
 {	"/clip/set",   		"set",		 13, 1,0	},
 {	"/clip/rec", 		"rec",		 14, 1,0	},
// {	"/clip/chain", 		"chain",	 15, 1,0	},
// {	"/clip/entry",  	"entry",	 18, 1,0	},
 {	"/stream/rec",		"rec",		 16, 2,0	},
// {	"/stream/chain",	"chain",	 17, 2,0	},
// { 	"/stream/entry",	"entry",	 18, 2,0	},
 {	"/stream/new",  	"new",	     	 19, 2,0	},
 {	"/out/",		"out",		 5, -1,0	},
 {	"/cl",			"cl",		 6, -1,0	},
 {	"/el",			"el",		 7, -1,0	},
 { 	"/arg",			"arg",		 8, -1,0	},
 {	"/entry",		"entry",	 9, -1,0	},
// {	"<n>",			"%d",	 	20, 9 ,20	},
// {	"<n>",			"%d",		40, 8 ,10	},	
 {	NULL,			NULL,		0, -1,0		},
};


int 	vj_osc_build_cont( vj_osc *o )
{ 
	/* Create containers /video , /clip, /chain and /tag */
	int i;
	for( i = 0; osc_cont[i].name != NULL ; i ++ )
	{
		if ( osc_cont[i].it == 0 )
		{
			o->cqinfo.comment = osc_cont[i].comment;

			// add a container to a leave
			if ( ( o->leaves[ osc_cont[i].leave ] =
				OSCNewContainer( osc_cont[i].name,
						(osc_cont[i].att == -1 ? o->container : o->leaves[ osc_cont[i].att ] ),
						&(o->cqinfo) ) ) == 0 )
			{
				if(osc_cont[i].att == - 1)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Cannot create container %d (%s) ",
						i, osc_cont[i].name );
					return 0;
				}
				else
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Cannot add branch %s to  container %d)",
						osc_cont[i].name, osc_cont[i].att );  
					return 0;
				}
			}
		}
		else
		{
			int n = osc_cont[i].it;
			int j;
			int base = osc_cont[i].leave;
			char name[50];
			char comment[50];

			for ( j = 0; j < n ; j ++ )
			{
			  sprintf(name, "N%d", j);	
			  sprintf(comment, "<%d>", j);
			  veejay_msg(VEEJAY_MSG_ERROR, "Try cont.%d  '%s', %d %d ",j, name,
					base + j, base );	
			  o->cqinfo.comment = comment;
			  if ((	o->leaves[ base + j ] = OSCNewContainer( name,
									o->leaves[ osc_cont[i].att ] ,
									&(o->cqinfo) ) ) == 0 )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Cannot auto numerate container %s ",
						osc_cont[i].name );
					return 0;
	
				}
			}
		}
	}
	return 1;
}

int	vj_osc_attach_methods( vj_osc *o )
{
	int i;
	for( i = 0; osc_methods[i].name != NULL ; i ++ )
	{
		o->ris.description = osc_methods[i].descr;
		OSCNewMethod( osc_methods[i].name, 
				o->leaves[ osc_methods[i].leave ],
				osc_methods[i].cb , 
				&(o->osc_args[0]),
				&(o->ris));
	}
	return 1;
}	



/* initialization, setup a UDP socket and invoke OSC magic */
vj_osc* vj_osc_allocate(int port_id) {
	vj_osc *o = (vj_osc*)vj_malloc(sizeof(vj_osc));
	o->osc_args = (osc_arg*)vj_malloc(50 * sizeof(*o->osc_args));
	o->rt.InitTimeMemoryAllocator = _vj_osc_time_malloc;
	o->rt.RealTimeMemoryAllocator = _vj_osc_rt_malloc;
	o->rt.receiveBufferSize = 1024;
	o->rt.numReceiveBuffers = 100;
	o->rt.numQueuedObjects = 100;
	o->rt.numCallbackListNodes = 200;
	o->leaves = (OSCcontainer*) vj_malloc(sizeof(OSCcontainer) * 100);
	o->t.initNumContainers = 100;
	o->t.initNumMethods = 200;
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

	if( !vj_osc_build_cont( o ))
		return NULL;

	OSCInitMethodQueryResponseInfo( &(o->ris));
	if( !vj_osc_attach_methods( o ))
		return NULL;

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

