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
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <veejay/vj-OSC.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <veejay/vims.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-event.h>
#include <veejay/vj-OSC.h>

static veejay_t *osc_info;

#include <libOSC/libosc.h>
#include <sys/types.h>

typedef struct osc_arg_t {
    int a;
    int b;
    int c;
} osc_arg;

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
} vj_osc;


/* VIMS does the job */
extern void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);


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
void vj_osc_cb_plain_mode( void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra );
void vj_osc_cb_next_second(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_prev_second(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_next_frame(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_prev_frame(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_new_sample(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_copy_sample(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_end(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_del(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_select_sample(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_jitter(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_end(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_looptype(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_set_marker(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_clear_marker(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_record_format(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_his_render(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sample_his_play(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_clear(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_disable_video(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_enable_video(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_del(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_default(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_preset(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_set(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_chain_entry_set_arg_val(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
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
void vj_osc_cb_chain_toggle_all(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
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
void vj_osc_cb_tag_new_solid(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);

void vj_osc_cb_tag_new_net(void *context, int arglen, const void *vargs, OSCTimeTag when,NetworkReturnAddressPtr ra );

void vj_osc_cb_tag_new_mcast(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra );

void vj_osc_cb_load_samplelist(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_save_samplelist(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_output_start_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_output_stop_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
#ifdef HAVE_SDL
void vj_osc_cb_resize( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra );
void vj_osc_cb_fullscreen(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
#endif
void vj_osc_cb_screenshot(void *context, int arglen, const void *vargs,OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_sampling(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_bezerk(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void vj_osc_cb_verbose(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_paste_at(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_copy(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_del(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_crop(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_cut(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_add(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_save(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_el_load(void *context,int arglen,const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_mcast_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);
void	vj_osc_cb_mcast_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra);

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


///////////////////////////////////// CALLBACKS FOR OSC ////////////////////////////////////////

#define NET_F_prefixed(a,b,c,d)\
{\
int arguments[16];\
int num_arg = vj_osc_parse_int_arguments(a,b,arguments+3);\
if( num_arg != 1 ) {\
 veejay_msg(VEEJAY_MSG_ERROR, "parameter %d accepts only 1 value\n", d );\
 } else {\
arguments[0] = 0;\
arguments[1] = -1;\
arguments[2] = d;\
vj_event_fire_net_event( osc_info, c, NULL, arguments, 4,3);\
}\
}\

#define NET_F_prefixed_full(a,b,c)\
{\
int arguments[16];\
int num_arg = vj_osc_parse_int_arguments(a,b,arguments+2);\
arguments[0] = 0;\
arguments[1] = -1;\
vj_event_fire_net_event( osc_info, c, NULL, arguments, num_arg + 2,2);\
}
#define NET_F_prefixed_single(a,b,c)\
{\
int arguments[16];\
int num_arg = vj_osc_parse_int_arguments(a,b,arguments+1);\
arguments[0] = 0;\
vj_event_fire_net_event( osc_info, c, NULL, arguments, num_arg + 1,1);\
}

#define NET_F_full_str(a,b,c)\
{\
char str[OSC_STRING_SIZE];\
int __n = vj_osc_parse_char_arguments(a,b,str);\
str[__n] = '\0';\
int args[2] = { 0,0};\
vj_event_fire_net_event(osc_info, c, str,args, __n + 2,2);\
}

#define NET_F_str(a,b,c)\
{\
char str[OSC_STRING_SIZE];\
int __n = vj_osc_parse_char_arguments(a,b,str);\
str[__n] = '\0';\
vj_event_fire_net_event(osc_info, c, str,NULL, 1,0);\
}

#define NET_F_mixed(a,b,c)\
{\
char str[OSC_STRING_SIZE];\
int  args[16];\
int __a = vj_osc_count_int_arguments(a,b);\
int __n = vj_osc_parse_char_arguments(a,b,str);\
memset( args,0,16 );\
str[__n] = '\0';\
vj_osc_parse_int_arguments( a , b , args );\
vj_event_fire_net_event(osc_info, c, str,args, __a + 1,0);\
}
#define NET_F_mixed_str(a,b,c)\
{\
char str[OSC_STRING_SIZE];\
int  args[16];\
int __n = vj_osc_parse_char_arguments(a,b,str);\
memset( args,0,16 );\
args[0] = 0;\
args[1] = 0;\
str[__n] = '\0';\
vj_event_fire_net_event(osc_info, c, str,args, 3,2);\
}




#define NET_F(a,b,c)\
{\
int arguments[16];\
memset( arguments,0,16 ); \
int num_arg = vj_osc_parse_int_arguments(a,b,arguments);\
vj_event_fire_net_event( osc_info, c, NULL,arguments, num_arg,0 );\
}

/* DVIMS does some wacky things with arguments,
   if 1 argument is missing , a default sample is chosen,
   if 2 arguments are missing, a default sample and entry id is chosen.
   if there are no arguments no event is fired
*/


void vj_osc_cb_el_add_sample(void *context, int arglen, const void *vargs, OSCTimeTag when,
		NetworkReturnAddressPtr ra)
{
	NET_F_mixed(arglen, vargs, VIMS_EDITLIST_ADD_SAMPLE);
}
void vj_osc_cb_el_load(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen, vargs, VIMS_EDITLIST_LOAD);
}
void vj_osc_cb_el_save(void *context, int arglen , const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_mixed_str(arglen, vargs, VIMS_EDITLIST_SAVE);
}
void vj_osc_cb_el_add(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F_str(arglen, vargs, VIMS_EDITLIST_ADD );
}
void vj_osc_cb_el_cut(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F(arglen, vargs, VIMS_EDITLIST_CUT );
}
void	vj_osc_cb_mcast_start(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs, VIMS_VIDEO_MCAST_START );
}
void	vj_osc_cb_mcast_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs, VIMS_VIDEO_MCAST_STOP );
}

void vj_osc_cb_el_crop(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F(arglen, vargs, VIMS_EDITLIST_CROP );
}
void vj_osc_cb_el_del(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F(arglen, vargs, VIMS_EDITLIST_DEL );
}
void vj_osc_cb_el_copy(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_EDITLIST_COPY );
}
void vj_osc_cb_el_paste_at( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_EDITLIST_PASTE_AT);
}

/* /video/playforward */
void vj_osc_cb_play_forward(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, VIMS_VIDEO_PLAY_FORWARD);

}
/* /video/playbackward */
void vj_osc_cb_play_backward(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, VIMS_VIDEO_PLAY_BACKWARD);
}



/* /video/stop */
void vj_osc_cb_stop(void *context, int arglen, const void *vargs, OSCTimeTag when, 
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs, VIMS_VIDEO_PLAY_STOP);

}
/* /video/speed */
void vj_osc_cb_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen, vargs, VIMS_VIDEO_SET_SPEED );
}
/* /sample/dup */
void vj_osc_cb_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {


}
/* /sample /video/gotostart */
void vj_osc_cb_skip_to_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs,VIMS_VIDEO_GOTO_START);
}
/* /sample /video/gotoend */
void vj_osc_cb_skip_to_end(void *context, int arglen, const void *vargs, OSCTimeTag when,	
	NetworkReturnAddressPtr ra) {

	NET_F(arglen,vargs,VIMS_VIDEO_GOTO_END);
}


void vj_osc_cb_set_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_VIDEO_SET_FRAME);
}

void vj_osc_cb_set_slow(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_VIDEO_SET_SLOW);
}

void vj_osc_cb_plain_mode(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F( arglen,vargs,VIMS_SET_PLAIN_MODE );
}

void vj_osc_cb_next_second(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_VIDEO_SKIP_SECOND);
}

void vj_osc_cb_prev_second(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_VIDEO_PREV_SECOND);
}

void vj_osc_cb_next_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_VIDEO_SKIP_FRAME);
}

void vj_osc_cb_prev_frame(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_VIDEO_PREV_FRAME);
}

void vj_osc_cb_new_sample(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SAMPLE_NEW);
}

void vj_osc_cb_copy_sample(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SAMPLE_COPY );
}

void vj_osc_cb_select_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SET_SAMPLE_START);
}


void vj_osc_cb_select_end(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SET_SAMPLE_END);
}

void vj_osc_cb_sample_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SAMPLE_DEL);
}


void vj_osc_cb_select_sample(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SAMPLE_SELECT);
}
void vj_osc_cb_sample_set_jitter(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_SAMPLE_UPDATE );
}	
void vj_osc_cb_sample_set_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_SAMPLE_SET_START);
}

void vj_osc_cb_sample_set_end(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_SAMPLE_SET_END);
}

void vj_osc_cb_sample_set_dup(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_SAMPLE_SET_DUP);
}

void vj_osc_cb_sample_set_speed(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs,VIMS_SAMPLE_SET_SPEED);
}

void vj_osc_cb_sample_set_looptype(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_SAMPLE_SET_LOOPTYPE);
}

void vj_osc_cb_sample_set_marker(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_SAMPLE_SET_MARKER);
}

void vj_osc_cb_sample_clear_marker(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_SAMPLE_CLEAR_MARKER);
}

void vj_osc_cb_sample_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_SAMPLE_REC_START);
}
void vj_osc_cb_record_format(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen, vargs, VIMS_RECORD_DATAFORMAT );
}
void vj_osc_cb_sample_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_SAMPLE_REC_STOP);
}

void	vj_osc_cb_chain_clear( void *ctx, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F_prefixed_single( arglen, vargs, VIMS_CHAIN_CLEAR );
}

void vj_osc_cb_chain_entry_disable_video(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_SET_VIDEO_OFF);
}

void vj_osc_cb_chain_entry_enable_video(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_SET_VIDEO_ON);
}

void vj_osc_cb_chain_toggle_all(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_CHAIN_TOGGLE_ALL);
}

void vj_osc_cb_chain_entry_del(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_CLEAR);
}

void vj_osc_cb_chain_entry_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_CHAIN_SET_ENTRY);
}

void vj_osc_cb_chain_entry_default(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_SET_DEFAULTS);
}

void vj_osc_cb_chain_entry_preset(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_SET_PRESET);
}

void vj_osc_cb_chain_entry_set(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen, vargs, VIMS_CHAIN_ENTRY_SET_EFFECT);
}

void vj_osc_cb_chain_entry_set_arg_val(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL);
}


void vj_osc_cb_chain_entry_channel(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen, vargs, VIMS_CHAIN_ENTRY_SET_CHANNEL);

}

void vj_osc_cb_chain_entry_source(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full(arglen, vargs, VIMS_CHAIN_ENTRY_SET_SOURCE);
}

void	vj_osc_cb_chain_manual_fade(void *ctx, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_CHAIN_MANUAL_FADE );
}

void vj_osc_cb_chain_fade_in(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen, vargs, VIMS_CHAIN_FADE_IN);
}

void vj_osc_cb_chain_fade_out(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_CHAIN_FADE_OUT);
}

void vj_osc_cb_chain_enable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_CHAIN_ENABLE);
}

void vj_osc_cb_chain_disable(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_single(arglen,vargs,VIMS_CHAIN_DISABLE);
}

void vj_osc_cb_tag_record_offline_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_OFFLINE_REC_START);
}


void vj_osc_cb_tag_record_offline_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_OFFLINE_REC_STOP);
}

void vj_osc_cb_tag_record_start(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_REC_START);
}

void vj_osc_cb_tag_record_stop(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_REC_STOP);
}

void vj_osc_cb_tag_select(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_SELECT);
}

void	vj_osc_cb_chain_add( void *ctx, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_prefixed_full( arglen, vargs, VIMS_CHAIN_ENTRY_SET_EFFECT);
}

void vj_osc_cb_set_parameter0(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,0);
}

void vj_osc_cb_set_parameter1(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,1);
}

void vj_osc_cb_set_parameter2(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,2);
}

void vj_osc_cb_set_parameter3(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,3);
}

void vj_osc_cb_set_parameter4(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,4);
}

void vj_osc_cb_set_parameter5(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,5);
}

void vj_osc_cb_set_parameter6(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,6);
}

void vj_osc_cb_set_parameter7(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,7);
}


void vj_osc_cb_set_parameter8(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_prefixed(arglen,vargs,VIMS_CHAIN_ENTRY_SET_ARG_VAL,8);
}
void vj_osc_cb_tag_new_v4l(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_NEW_V4L);
}
void vj_osc_cb_tag_new_solid(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_STREAM_NEW_COLOR);
}

void vj_osc_cb_tag_new_net(void *context, int arglen, const void *vargs, OSCTimeTag when,
    NetworkReturnAddressPtr ra)
{
	NET_F_mixed(arglen,vargs,VIMS_STREAM_NEW_UNICAST);
}
void vj_osc_cb_tag_new_mcast(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra )
{
	NET_F_mixed(arglen,vargs,VIMS_STREAM_NEW_MCAST);
}

void vj_osc_cb_tag_new_y4m(void *context, int arglen, const void *vargs, OSCTimeTag when,
	NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen,vargs,VIMS_STREAM_NEW_Y4M);
}

void vj_osc_cb_load_samplelist(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen,vargs,VIMS_SAMPLE_LOAD_SAMPLELIST);
}

void vj_osc_cb_save_samplelist(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen,vargs,VIMS_SAMPLE_SAVE_SAMPLELIST);
}

void vj_osc_cb_output_start_y4m(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_str(arglen,vargs,VIMS_OUTPUT_Y4M_START);
}

void vj_osc_cb_output_stop_y4m(void *context, int arglen, const void *vargs,
	OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen,vargs,VIMS_OUTPUT_Y4M_STOP);
}

#ifdef HAVE_SDL
void vj_osc_cb_resize(void *context, int arglen, const void *vargs, OSCTimeTag when,
NetworkReturnAddressPtr ra )
{
	NET_F( arglen, vargs, VIMS_RESIZE_SDL_SCREEN );
}
void vj_osc_cb_fullscreen(void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F( arglen, vargs, VIMS_FULLSCREEN );
}
#endif
void vj_osc_cb_screenshot( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F_mixed_str(arglen, vargs, VIMS_SCREENSHOT);
}
void vj_osc_cb_sampling( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F(arglen, vargs, VIMS_SAMPLE_MODE );
}
void vj_osc_cb_verbose( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra)
{
	NET_F( arglen, vargs, VIMS_DEBUG_LEVEL )
}
void vj_osc_cb_bezerk( void *context, int arglen, const void *vargs, OSCTimeTag when, NetworkReturnAddressPtr ra )
{
	NET_F( arglen, vargs, VIMS_BEZERK );
}
/* initialize the pointer to veejay_t */

void vj_osc_set_veejay_t(veejay_t *info) {
	osc_info = info;
}

void vj_osc_free(void *d)
{
	if(!d) return;
	vj_osc *c = (vj_osc*) d;
	void *addr = OSCPacketBufferGetClientAddr(c->packet );
	if(addr)
		free(addr);
	if(c->osc_args) free(c->osc_args);
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
	{ "play foward",			"play",			vj_osc_cb_play_forward,				0	},	
	{ "play reverse",			"reverse",		vj_osc_cb_play_backward,			0	},
	{ "pause",					"pause",		vj_osc_cb_stop,						0	},
	{ "speed ( < 0 = reverse, > 0 = forward)",
								"speed",		vj_osc_cb_set_speed,				0	},
	{ "slow",					"slow",			vj_osc_cb_set_dup,					0	},
	{ "goto start",				"goto_start",	vj_osc_cb_skip_to_start,			0	},
	{ "goto end",				"goto_end",		vj_osc_cb_skip_to_end,				0	},
	{ "set frame <pos>",		"set_frame",	vj_osc_cb_set_frame,				0	},
	{ "set previous frame",		"prev_frame",	vj_osc_cb_prev_frame,				0	},
	{ "set next frame",			"next_frame",	vj_osc_cb_next_frame,				0	},
	{ "set next second",		"next_second",	vj_osc_cb_next_second,				0	},
	{ "set previous second",	"prev_second",	vj_osc_cb_prev_second,				0	},
	{ "play plain video",		"mode",		vj_osc_cb_plain_mode,				0	},
	{ "create new sample <pos start> <pos end>",	
								"new",			vj_osc_cb_new_sample,					1	},
	{ "copy sample <num> as new sample",
								"copy",			vj_osc_cb_copy_sample,				1	},
	{ "delete sample <num>",		"del",			vj_osc_cb_sample_del,					1	},
	{ "select and play sample <num>",
								"select",		vj_osc_cb_select_sample,				1	},
	{ "relative start/end position update <pos1> <pos2>",
								"jitter",		vj_osc_cb_sample_set_jitter,			13	},
	{ "set sample new starting position <pos>",
								"start",		vj_osc_cb_sample_set_start,			13	},
	{ "set sample new ending position <pos>",
								"end",			vj_osc_cb_sample_set_end,				13	},
	{ "sample set looptype <0 = none, 1 = normal, 2 = bounce>",
								"looptype",		vj_osc_cb_sample_set_looptype,		13	},
	{ "sample set playback speed <num>",
								"speed",		vj_osc_cb_sample_set_speed,			13	},
	{ "sample set marker <pos1> <pos2>",
								"marker",		vj_osc_cb_sample_set_marker,			13	},
	{ "sample set frame duplicate <num>",
								"slow",			vj_osc_cb_sample_set_dup,				13	},
	{ "sample delete marker",
								"nomarker",		vj_osc_cb_sample_clear_marker,		13	},
	{ "sample start recording <0=entire sample, N=num frames> <0=dont play 1=play>",
								"start",		vj_osc_cb_sample_record_start,		14	},
	{ "sample stop recording",	"stop",			vj_osc_cb_sample_record_stop,			14	},
	{ "sample set recorder format (mjpeg,mpeg4,dv,divx,yv12,yv16)",
								"format",		vj_osc_cb_record_format,			14	},
	{ "stream select and play <num>",
								"select",		vj_osc_cb_tag_select,				2	},
	{ "new video4linux input stream <device num> <channel num>",
								"v4l",			vj_osc_cb_tag_new_v4l,				19	},
	{ "new solid color stream <R> <G> <B>",		
								"solid",		vj_osc_cb_tag_new_solid,			19	},
	{ "new yuv4mpeg input stream <filename>",
								"y4m",			vj_osc_cb_tag_new_y4m,				19	},
	{ "new multicast input stream <address> <port>",
								"mcast",		vj_osc_cb_tag_new_mcast,			19 },
	{ "new peer-to-peer input stream <hostname> <port>",
								"net",			vj_osc_cb_tag_new_net,				19 },

	{ "hidden record from stream <num frames> <autoplay bool>",
								"o_start",		vj_osc_cb_tag_record_offline_start,	16	},
	{ "stop hidden recording ",	"o_stop",		vj_osc_cb_tag_record_offline_stop,	16	},
	{ "start stream recorder <num frames> <autoplay bool>",
								"start",		vj_osc_cb_tag_record_start,			16	},
	{ "stop stream recorder ",	"stop",			vj_osc_cb_tag_record_stop,			16	},
	{ "set stream recorder format (mjpeg,divx,dv,yv12,yv16,mpeg4)",
								"format",		vj_osc_cb_record_format,			16	},
	{ "Effect chain clear",		"reset",		vj_osc_cb_chain_clear,				4	},
	{ "Fade in effect chain <num frames>",
								"fade_in"	,	vj_osc_cb_chain_fade_in,			4	},
	{ "Fade out effect chain <num frames>",	
								"fade_out",		vj_osc_cb_chain_fade_out,			4	},
	{ "Effect chain enabled",	"enable",		vj_osc_cb_chain_enable,				4	},
	{ "Effect chain disabled",	"disable",		vj_osc_cb_chain_disable,			4	},
	{ "Manual Fader (0=A 255=B",
								"opacity",		vj_osc_cb_chain_manual_fade,		4	},
	{ "All Effect chains on/off (1/0)",
								"global_fx",	 vj_osc_cb_chain_toggle_all,		4	},
	{ "Disable effect chain entry <num>",
								"disable",		vj_osc_cb_chain_entry_disable_video,9	},
	{ "Enable effect chain entry <num>",
								"enable",		vj_osc_cb_chain_entry_enable_video,	9	},
	{ "Clear effect chain entry <num>",
								"clear",		vj_osc_cb_chain_entry_del,			9	},
	{ "Select effect chain entry <num>",
								"select",		vj_osc_cb_chain_entry_select,		9	},
	{ "Set effect default values on chain entry <num>",
								"defaults",		vj_osc_cb_chain_entry_default,		9	},
	{ "Preset an effect on chain entry",
								"preset",	vj_osc_cb_chain_entry_preset,		9	},
//	{ "/entry/set",			"set",		vj_osc_cb_chain_entry_set,		9	},
	{ "Select channel <num> for mixing effect on entry <num>",
								"channel",	vj_osc_cb_chain_entry_channel,		9	},
	{ "Select source (0=sample,1=stream) for mixing effect on entry <num>",
							"source",	vj_osc_cb_chain_entry_source,		9	},

	{ "Set argument <num> to value <num> for effect on entry <num>",
							"set",		vj_osc_cb_chain_entry_set_arg_val,	8	},


	{ "Samplelist load <filename>",			"load",		vj_osc_cb_load_samplelist,		6 	},
	{ "Samplelist save <filename>",			"save",		vj_osc_cb_save_samplelist,		6	},
	{ "Editlist add filename (as new sample)","add_sample", vj_osc_cb_el_add_sample,			7	},
	{ "EditList paste frames from buf at frame <num>",
							"paste_at",	vj_osc_cb_el_paste_at,			7	},
	{ "EditList copy frames <n1> to <n2> to buffer",
							"copy",		vj_osc_cb_el_copy,				7	},
	{ "EditList delete frames <n1> to <n2>",
							"del",		vj_osc_cb_el_del,				7	},
	{ "EditList crop frames 0 - <n1>  <n2> - end",
							"crop",		vj_osc_cb_el_crop,				7	},
	{ "EditList cut frames <n1> to <n2> into buffer",
							"cut",		vj_osc_cb_el_cut,				7	},	
	{ "EditList add file <filename>",
							"add",		vj_osc_cb_el_add,				7	},
	{ "EditList save <filename>",
							"save",		vj_osc_cb_el_save,				7	},
	{ "EditList load <filename>",
							"load",		vj_osc_cb_el_load,				7	},
#ifdef HAVE_SDL
	{ "Resize SDL video window <width> <height>",
							"resize",	vj_osc_cb_resize,				10  },
	{ "Toggle SDL video window fullscreen/windowed",
							"fullscreen", vj_osc_cb_fullscreen,			10	},
#endif
	{ "Configure sampling mode (linear=0 or triangle=1)",
							"sampling",	vj_osc_cb_sampling,				11	},
	{ "Toggle verbose output mode",
							"verbose",	vj_osc_cb_verbose,				11	},
	{ "Toggle bezerk mode",		"bezerk",	vj_osc_cb_bezerk,				11	},

	{ "Stop mcast frame sender",	"mcaststop",	vj_osc_cb_mcast_stop,				5	},
	{ "Start mcast frame sender",	"mcaststart",	vj_osc_cb_mcast_start,				5	},

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
 {	"/sample/", 		"sample",		 1, -1,0	},
 {	"/stream/",		"stream",	 2, -1,0	},
 {	"/record/", 		"record",	 3, -1,0	},
 {	"/chain/" , 		"chain",	 4, -1,0	},
 {	"/video/set",		"set",		 12, 0,0	},
 {	"/sample/set",   		"set",		 13, 1,0	},
 {	"/sample/rec", 		"rec",		 14, 1,0	},
// {	"/sample/chain", 		"chain",	 15, 1,0	},
// {	"/sample/entry",  	"entry",	 18, 1,0	},
 {	"/stream/rec",		"rec",		 16, 2,0	},
// {	"/stream/chain",	"chain",	 17, 2,0	},
// { 	"/stream/entry",	"entry",	 18, 2,0	},
 {	"/stream/new",  	"new",	     	 19, 2,0	},
 {	"/out/",		"out",		 5, -1,0	},
 {	"/cl",			"cl",		 6, -1,0	},
 {	"/el",			"el",		 7, -1,0	},
 { 	"/arg",			"arg",		 8, -1,0	},
 {	"/entry",		"entry",	 9, -1,0	},
#ifdef HAVE_SDL
 {  "/output",		"output",	10, -1,0	},
#endif
 {  "/config",		"config",	11,	-1,0	},
// {	"<n>",			"%d",	 	20, 9 ,20	},
// {	"<n>",			"%d",		40, 8 ,10	},	
 {	NULL,			NULL,		0, -1,0		},

};


int 	vj_osc_build_cont( vj_osc *o )
{ 
	/* Create containers /video , /sample, /chain and /tag */
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
		if( o->leaves[ osc_methods[i].leave ] )
		{
		OSCNewMethod( osc_methods[i].name, 
				o->leaves[ osc_methods[i].leave ],
				osc_methods[i].cb , 
				&(o->osc_args[0]),
				&(o->ris));
		}
	}
	return 1;
}	



/* initialization, setup a UDP socket and invoke OSC magic */
void* vj_osc_allocate(int port_id) {
	void *res;
	char tmp[200];
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
	if( !vj_osc_attach_methods( o ))
		return NULL;

	res =(void*) o;
     return res;
}	


void vj_osc_dump()
{

	veejay_msg(VEEJAY_MSG_INFO,"Veejay OSC");
	veejay_msg(VEEJAY_MSG_INFO,"When using strings, set it *always* as the first argument");
	OSCPrintWholeAddressSpace();


}
 
/* dump the OSC address space to screen */
int vj_osc_setup_addr_space(void *d) {
	char addr[100];
	vj_osc *o = (vj_osc*) d;
	//struct OSCMethodQueryResponseInfoStruct qri;

	if(OSCGetAddressString( addr, 100, o->container ) == FALSE) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get address space of OSC");
		return -1;
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "Address of top level container [%s]",addr);
	return 0;
}


/* get a packet */
int vj_osc_get_packet(void *d) {
   //OSCTimeTag tag;
   struct timeval tv;
   tv.tv_sec=0;
   tv.tv_usec = 0;
	vj_osc *o = (vj_osc*) d;
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

