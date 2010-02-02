/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */


#include <config.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdint.h>
#ifdef HAVE_SDL
#include <SDL/SDL.h>
#endif
#include <stdarg.h>
#include <libhash/hash.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libel/vj-avcodec.h>
#include <libsamplerec/samplerecord.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <veejay/vims.h>
#include <veejay/vj-event.h>
#include <libstream/vj-tag.h>
#include <libstream/vj-vloopback.h>
#include <liblzo/lzo.h>
#include <veejay/vjkf.h>
#ifdef HAVE_GL
#include <veejay/gl.h>
#endif
#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#endif
#include <libvevo/vevo.h>
#include <libvevo/libvevo.h>
#include <veejay/vj-OSC.h>
#include <libvjnet/vj-server.h>
#include <veejay/vevo.h>
#include <veejay/vj-misc.h>
/* Highest possible SDL Key identifier */
#define MAX_SDL_KEY	(3 * SDLK_LAST) + 1  
#define MSG_MIN_LEN	  4 /* stripped ';' */
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif

static int use_bw_preview_ = 0;
static int _last_known_num_args = 0;
static hash_t *BundleHash = NULL;

static int vj_event_valid_mode(int mode) {
	switch(mode) {
	 case VJ_PLAYBACK_MODE_SAMPLE:
	 case VJ_PLAYBACK_MODE_TAG:
	 case VJ_PLAYBACK_MODE_PLAIN:
	 return 1;
	}

	return 0;
}

/* define the function pointer to any event */
typedef void (*vj_event)(void *ptr, const char format[], va_list ap);

void vj_event_create_effect_bundle(veejay_t * v,char *buf, int key_id, int key_mod );

/* struct for runtime initialization of event handlers */
typedef struct {
	int list_id;			// VIMS id
	vj_event act;			// function pointer
} vj_events;

static	vj_events	net_list[VIMS_MAX];
static	int		override_keyboard = 0;
#ifdef HAVE_SDL
typedef struct
{
	vj_events	*vims;
	int		key_symbol;
	int		key_mod;
	int		arg_len;
	char		*arguments;
	int		event_id;
} vj_keyboard_event;

static hash_t *keyboard_events = NULL;

static	vj_keyboard_event *keyboard_event_map_[2048];

typedef struct
{
	int	key_symbol;
	int	key_mod;
	char	*args;
	int	arg_len;
	void	*next;
} vims_key_list;

#endif

static int _recorder_format = ENCODER_MJPEG;

#define SEND_BUF 256000
static char _print_buf[SEND_BUF];
static char _s_print_buf[SEND_BUF];

static	void	*macro_bank_[12];
static	void	*macro_port_ = NULL;
static  int	current_macro_ = 0;
static	int	macro_status_ = 0;
static	int	macro_key_    = 1;
static	int	macro_line_[3] = {-1 ,0,0};
static  int	macro_current_age_ = 0;
static  int	macro_expected_age_ = 0;
#define MAX_MACROS 8
typedef struct {
	char *msg[MAX_MACROS];
	int   pending[MAX_MACROS];
	int   age[MAX_MACROS];
} macro_block_t;

int	vj_event_macro_status(void)
{
	return macro_status_;
}

static	char	*retrieve_macro_(veejay_t *v, long frame, int idx );
static	void	store_macro_( veejay_t *v,char *str, long frame );
static	void	reset_macro_(void);
static	void	replay_macro_(void);

extern void	veejay_pipe_write_status(veejay_t *info, int link_id );
extern int	_vj_server_del_client(vj_server * vje, int link_id);
extern int       vj_event_exists( int id );

// forward decl
int vj_event_get_video_format(void)
{
	return _recorder_format;
}

enum {
	VJ_ERROR_NONE=0,	
	VJ_ERROR_MODE=1,
	VJ_ERROR_EXISTS=2,
	VJ_ERROR_VIMS=3,
	VJ_ERROR_DIMEN=4,
	VJ_ERROR_MEM=5,
	VJ_ERROR_INVALID_MODE = 6,
};

#ifdef HAVE_SDL
#define	VIMS_MOD_SHIFT	3
#define VIMS_MOD_NONE	0
#define VIMS_MOD_CTRL	2
#define VIMS_MOD_ALT	1

static struct {					/* hardcoded keyboard layout (the default keys) */
	int event_id;			
	int key_sym;			
	int key_mod;
	const char *value;
} vj_event_default_sdl_keys[] = {

	{ 0,0,0,NULL },
	{ VIMS_PROJ_INC,			SDLK_LEFT,		VIMS_MOD_CTRL, "-1 0" 	},
	{ VIMS_PROJ_INC,			SDLK_RIGHT,		VIMS_MOD_CTRL, "1 0"	},
	{ VIMS_PROJ_INC,			SDLK_UP,		VIMS_MOD_CTRL, "0 -1" 	},
	{ VIMS_PROJ_INC,			SDLK_DOWN,		VIMS_MOD_CTRL, "0 1"	},


	{ VIMS_EFFECT_SET_BG,			SDLK_b,		VIMS_MOD_ALT,	NULL	},
	{ VIMS_VIDEO_PLAY_FORWARD,		SDLK_KP6,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PLAY_BACKWARD,		SDLK_KP4, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PLAY_STOP,			SDLK_KP5, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_SKIP_FRAME,		SDLK_KP9, 	VIMS_MOD_NONE,	"1"	},
	{ VIMS_VIDEO_PREV_FRAME,		SDLK_KP7, 	VIMS_MOD_NONE,	"1"	},
	{ VIMS_VIDEO_SKIP_SECOND,		SDLK_KP8, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PREV_SECOND,		SDLK_KP2, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_GOTO_START,		SDLK_KP1, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_GOTO_END,			SDLK_KP3, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_a,		VIMS_MOD_NONE,	"1"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_s,		VIMS_MOD_NONE,	"2"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_d,		VIMS_MOD_NONE,	"3"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_f,		VIMS_MOD_NONE,	"4"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_g,		VIMS_MOD_NONE,	"5"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_h,		VIMS_MOD_NONE,	"6"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_j,		VIMS_MOD_NONE,	"7"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_k,		VIMS_MOD_NONE,	"8"	},
	{ VIMS_VIDEO_SET_SPEEDK,			SDLK_l,		VIMS_MOD_NONE,	"9"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_a,		VIMS_MOD_ALT,	"1"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_s,		VIMS_MOD_ALT,	"2"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_d,		VIMS_MOD_ALT,	"3"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_e,		VIMS_MOD_ALT,	"4"	},	
	{ VIMS_VIDEO_SET_SLOW,			SDLK_f,		VIMS_MOD_ALT,	"5"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_g,		VIMS_MOD_ALT,	"6"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_h,		VIMS_MOD_ALT,	"7"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_j,		VIMS_MOD_ALT,	"8"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_k,		VIMS_MOD_ALT,	"9"	},
	{ VIMS_VIDEO_SET_SLOW,			SDLK_l,		VIMS_MOD_ALT,	"10"	},
#ifdef HAVE_SDL
	{ VIMS_FULLSCREEN,			SDLK_f,		VIMS_MOD_CTRL,	NULL	},
#endif
	{ VIMS_CHAIN_ENTRY_DOWN,		SDLK_KP_MINUS,	VIMS_MOD_NONE,	"1"	},
	{ VIMS_CHAIN_ENTRY_UP,			SDLK_KP_PLUS,	VIMS_MOD_NONE,	"1"	},
	{ VIMS_CHAIN_ENTRY_CHANNEL_INC,		SDLK_EQUALS,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_CHAIN_ENTRY_CHANNEL_DEC,		SDLK_MINUS,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_CHAIN_ENTRY_SOURCE_TOGGLE,	SDLK_SLASH,	VIMS_MOD_NONE,	NULL	}, // stream/sample
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_PAGEUP,	VIMS_MOD_NONE,	"0 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_KP_PERIOD,	VIMS_MOD_NONE,	"1 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_PERIOD,	VIMS_MOD_NONE,	"2 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_w,		VIMS_MOD_NONE,	"3 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_r,		VIMS_MOD_NONE,	"4 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_y,		VIMS_MOD_NONE,	"5 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_i,		VIMS_MOD_NONE,	"6 1"	},
	{ VIMS_CHAIN_ENTRY_INC_ARG,		SDLK_p,		VIMS_MOD_NONE,	"7 1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_PAGEDOWN,	VIMS_MOD_NONE,	"0 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_KP0,	VIMS_MOD_NONE,	"1 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_COMMA,	VIMS_MOD_NONE,	"2 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_q,		VIMS_MOD_NONE,	"3 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_e,		VIMS_MOD_NONE,	"4 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_t,		VIMS_MOD_NONE,	"5 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_u,		VIMS_MOD_NONE,	"6 -1"	},
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		SDLK_o,		VIMS_MOD_NONE,	"7 -1"	},
	{ VIMS_OSD,				SDLK_o,		VIMS_MOD_CTRL,  NULL	},
	{ VIMS_COPYRIGHT,			SDLK_c,		VIMS_MOD_CTRL,  NULL	},
	{ VIMS_COMPOSITE,			SDLK_i,		VIMS_MOD_CTRL,  NULL    },
	{ VIMS_OSD_EXTRA,			SDLK_h,		VIMS_MOD_CTRL,	NULL	},
	{ VIMS_PROJ_STACK,			SDLK_v,		VIMS_MOD_CTRL,	"1 0"	},
	{ VIMS_PROJ_STACK,			SDLK_p,		VIMS_MOD_CTRL,	"0 1"	},
	{ VIMS_PROJ_TOGGLE,			SDLK_a,		VIMS_MOD_CTRL,  NULL	},
	{ VIMS_FRONTBACK,			SDLK_s,		VIMS_MOD_CTRL,  NULL	},
	{ VIMS_RENDER_DEPTH,			SDLK_d,		VIMS_MOD_CTRL,  "2"	},
	{ VIMS_SELECT_BANK,			SDLK_1,		VIMS_MOD_NONE,	"1"	},
	{ VIMS_SELECT_BANK,			SDLK_2,		VIMS_MOD_NONE,	"2"	},
	{ VIMS_SELECT_BANK,			SDLK_3,		VIMS_MOD_NONE,	"3"	},
	{ VIMS_SELECT_BANK,			SDLK_4,		VIMS_MOD_NONE,	"4"	},
	{ VIMS_SELECT_BANK,			SDLK_5,		VIMS_MOD_NONE,	"5"	},
	{ VIMS_SELECT_BANK,			SDLK_6,		VIMS_MOD_NONE,	"6"	},
	{ VIMS_SELECT_BANK,			SDLK_7,		VIMS_MOD_NONE,	"7"	},
	{ VIMS_SELECT_BANK,			SDLK_8,		VIMS_MOD_NONE,	"8"	},
	{ VIMS_SELECT_BANK,			SDLK_9,		VIMS_MOD_NONE,	"9"	},
	{ VIMS_SELECT_ID,			SDLK_F1,	VIMS_MOD_NONE,	"1"	},
	{ VIMS_SELECT_ID,			SDLK_F2,	VIMS_MOD_NONE,	"2"	},
	{ VIMS_SELECT_ID,			SDLK_F3,	VIMS_MOD_NONE,	"3"	},
	{ VIMS_SELECT_ID,			SDLK_F4,	VIMS_MOD_NONE,	"4"	},
	{ VIMS_SELECT_ID,			SDLK_F5,	VIMS_MOD_NONE,	"5"	},
	{ VIMS_SELECT_ID,			SDLK_F6,	VIMS_MOD_NONE,	"6"	},
	{ VIMS_SELECT_ID,			SDLK_F7,	VIMS_MOD_NONE,	"7"	},
	{ VIMS_SELECT_ID,			SDLK_F8,	VIMS_MOD_NONE,	"8"	},
	{ VIMS_SELECT_ID,			SDLK_F9,	VIMS_MOD_NONE,	"9"	},
	{ VIMS_SELECT_ID,			SDLK_F10,	VIMS_MOD_NONE,	"10"	},
	{ VIMS_SELECT_ID,			SDLK_F11, 	VIMS_MOD_NONE,	"11"	},
	{ VIMS_SELECT_ID,			SDLK_F12,	VIMS_MOD_NONE,	"12"	},
	{ VIMS_SET_PLAIN_MODE,			SDLK_KP_DIVIDE,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_REC_AUTO_START,			SDLK_e,		VIMS_MOD_CTRL,	"100"	},
	{ VIMS_REC_STOP,			SDLK_t,		VIMS_MOD_CTRL,	NULL	},
	{ VIMS_REC_START,			SDLK_r,		VIMS_MOD_CTRL,	NULL	},
	{ VIMS_CHAIN_TOGGLE,			SDLK_END,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_CHAIN_ENTRY_SET_STATE,		SDLK_END,	VIMS_MOD_ALT,	NULL	},	
	{ VIMS_CHAIN_ENTRY_CLEAR,		SDLK_DELETE,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_FXLIST_INC,			SDLK_UP,	VIMS_MOD_NONE,	"1"	},
	{ VIMS_FXLIST_DEC,			SDLK_DOWN,	VIMS_MOD_NONE,	"1"	},
	{ VIMS_FXLIST_ADD,			SDLK_RETURN,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_SET_SAMPLE_START,			SDLK_LEFTBRACKET,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_SET_SAMPLE_END,			SDLK_RIGHTBRACKET,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_SAMPLE_SET_MARKER_START,		SDLK_LEFTBRACKET,	VIMS_MOD_ALT,	NULL	},
	{ VIMS_SAMPLE_SET_MARKER_END,		SDLK_RIGHTBRACKET,	VIMS_MOD_ALT,	NULL	},
	{ VIMS_SAMPLE_TOGGLE_LOOP,		SDLK_KP_MULTIPLY,	VIMS_MOD_NONE,NULL	},
	{ VIMS_SWITCH_SAMPLE_STREAM,		SDLK_ESCAPE,		VIMS_MOD_NONE, NULL	},
	{ VIMS_PRINT_INFO,			SDLK_HOME,		VIMS_MOD_NONE, NULL	},
	{ VIMS_SAMPLE_CLEAR_MARKER,		SDLK_BACKSPACE,		VIMS_MOD_NONE, NULL },
	{ VIMS_MACRO,				SDLK_SPACE,		VIMS_MOD_NONE, "2 1"	},
	{ VIMS_MACRO,				SDLK_SPACE,		VIMS_MOD_SHIFT,  "1 1"	},
	{ VIMS_MACRO,				SDLK_SPACE,		VIMS_MOD_CTRL, "0 0"	},
	{ VIMS_MACRO_SELECT,			SDLK_F1,		VIMS_MOD_CTRL, "0"	},
	{ VIMS_MACRO_SELECT,			SDLK_F2,		VIMS_MOD_CTRL, "1"	},
	{ VIMS_MACRO_SELECT,			SDLK_F3,		VIMS_MOD_CTRL, "2"	},
	{ VIMS_MACRO_SELECT,			SDLK_F4,		VIMS_MOD_CTRL, "3"	},
	{ VIMS_MACRO_SELECT,			SDLK_F5,		VIMS_MOD_CTRL, "4"	},
	{ VIMS_MACRO_SELECT,			SDLK_F6,		VIMS_MOD_CTRL, "5"	},
	{ VIMS_MACRO_SELECT,			SDLK_F7,		VIMS_MOD_CTRL, "6"	},
	{ VIMS_MACRO_SELECT,			SDLK_F8,		VIMS_MOD_CTRL, "7"	},
	{ VIMS_MACRO_SELECT,			SDLK_F9,		VIMS_MOD_CTRL, "8"	},
	{ VIMS_MACRO_SELECT,			SDLK_F10,		VIMS_MOD_CTRL, "9"	},
	{ VIMS_MACRO_SELECT,			SDLK_F11,		VIMS_MOD_CTRL, "10"	},
	{ VIMS_MACRO_SELECT,			SDLK_F12,		VIMS_MOD_CTRL, "11"	},
	{ 0,0,0,NULL },
};
#endif

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)			/* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)		/* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)				/* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)				/* use defaults when optional arguments are not given */			

#define FORMAT_MSG(dst,str) sprintf(dst,"%03d%s",strlen(str),str)
#define APPEND_MSG(dst,str) veejay_strncat(dst,str,strlen(str))
#define SEND_MSG_DEBUG(v,str) \
{\
char *__buf = str;\
int  __len = strlen(str);\
int  __done = 0;\
veejay_msg(VEEJAY_MSG_INFO, "--------------------------------------------------------");\
for(__done = 0; __len > (__done + 80); __done += 80)\
{\
	char *__tmp = strndup( str+__done, 80 );\
veejay_msg(VEEJAY_MSG_INFO, "[%d][%s]",strlen(str),__tmp);\
	if(__tmp) free(__tmp);\
}\
veejay_msg(VEEJAY_MSG_INFO, "[%s]", str + __done );\
vj_server_send(v->vjs[0], v->uc->current_link, __buf, strlen(__buf));\
veejay_msg(VEEJAY_MSG_INFO, "--------------------------------------------------------");\
}

#define SEND_DATA(v,buf,buflen)\
{\
  int res_ = vj_server_send(v->vjs[VEEJAY_PORT_CMD], v->uc->current_link, (uint8_t*) buf, buflen);\
  if(res_ <= 0) { \
	_vj_server_del_client( v->vjs[VEEJAY_PORT_CMD], v->uc->current_link); \
	_vj_server_del_client( v->vjs[VEEJAY_PORT_STA], v->uc->current_link); \
	return;\
	}\
}

#define SEND_MSG(v,str)\
{\
if(vj_server_send(v->vjs[VEEJAY_PORT_CMD], v->uc->current_link, (uint8_t*) str, strlen(str)) < 0) { \
	_vj_server_del_client( v->vjs[VEEJAY_PORT_CMD], v->uc->current_link); \
	_vj_server_del_client( v->vjs[VEEJAY_PORT_STA], v->uc->current_link);} \
}

/* some macros for commonly used checks */

#define SAMPLE_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) )
#define STREAM_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) )
#define PLAIN_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN) )

#define p_no_sample(a) {  veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist",a); }
#define p_no_tag(a)    {  veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist",a); }
#define p_invalid_mode() {  veejay_msg(VEEJAY_MSG_DEBUG, "Invalid playback mode for this action"); }
#define v_chi(v) ( (v < 0  || v >= SAMPLE_MAX_EFFECTS ) ) 

#define P_A(a,b,c,d)\
{\
int __z = 0;\
unsigned char *__tmpstr = NULL;\
if(a!=NULL){\
unsigned int __rp;\
unsigned int __rplen = (sizeof(a) / sizeof(int) );\
for(__rp = 0; __rp < __rplen; __rp++) a[__rp] = 0;\
}\
while(*c) { \
if(__z > _last_known_num_args )  break; \
switch(*c++) {\
 case 's':\
__tmpstr = (char*)va_arg(d,char*);\
if(__tmpstr != NULL) {\
	sprintf( b,"%s",__tmpstr);\
	}\
__z++ ;\
 break;\
 case 'd': a[__z] = *( va_arg(d, int*)); __z++ ;\
 break; }\
 }\
}

/* P_A16: Parse 16 integer arguments. This macro is used in 1 function */
#define P_A16(a,c,d)\
{\
int __z = 0;\
while(*c) { \
if(__z > 15 )  break; \
switch(*c++) { case 'd': a[__z] = va_arg(d, int); __z++ ; break; }\
}}\


#define DUMP_ARG(a)\
if(sizeof(a)>0){\
int __l = sizeof(a)/sizeof(int);\
int __i; for(__i=0; __i < __l; __i++) veejay_msg(VEEJAY_MSG_DEBUG,"[%02d]=[%06d], ",__i,a[__i]);}\
else { veejay_msg(VEEJAY_MSG_DEBUG,"arg has size of 0x0");}


#define CLAMPVAL(a) { if(a<0)a=0; else if(a >255) a =255; }
//@ FIXME: implement embedded help
//@ F1 -> sample playing FX=off -> standard help
//@ F1 -> sample playing FX=on entry >= 0 <= MAX_E : show help for FX on entry N
//
//
static	struct {
	const char *msg;
} embedded_help[] = {
	{ "'[' Set starting position of sample\n" },
	{ "']' Set ending position and create new sample\n"},
	{ "'F1-F12' Play sample (Bank * 12) + Fx\n"},
	{ "'0-9' Select Bank 0-12\n"},
	{ "'KP/' Toggle between plain and sample mode\n"},
	{ "'A...L' Speed\n"},
	{ "'A...L' + ALT Slow motion\n"},
	{ "'KP8' Forward 1 second\n"},
	{ "'KP2' Back 1 second\n"},
	{ "'KP5' Pause playback\n"},
	{ "'KP4' Play backward\n"}, 
	{ "'KP6' Play forward\n"},
	{ "'KP7' Back one frame\n"},
	{ "'KP9' Forward one frame\n"},
	{ "'KP1' Goto starting position\n"},
	{ "'KP3' Goto ending position\n"},
	{ "'KP*' Change sample looping\n"},
	{ "Cursor Up/Down Select FX from FX list\n"},
	{ "ENTER Add selected FX to current FX slot\n"},
	
	NULL
};

static struct {
	const char *msg;
} fx_embedded_help[] = {
	{ "'PgUp/PgDn' Inc/Dec FX parameter 0 "},
	{ "'KP Ins/Del' Inc/Dec FX parameter 1 "},
	{ "',/.' Inc/Dec FX parameter 2 "},
	{ "'q/w' Inc/Dec FX parameter 3 "},
	{ "'e/r' Inc/Dec FX parameter 4 "},
	{ "'t/y' Inc/Dec FX parameter 5 "},
	{ "'u/i' Inc/Dec FX parameter 6 "},
	{ "'o/p' Inc/Dec FX parameter 7 "},
	{"'\nEND' Toggle FX Chain\n"},
	{"'Delete' Clear current FX slot\n"},
	{ "'KP-' Down 1 position in FX chain\n"},
	{ "'KP+' Up 1 position in FX chain\n"},
	{ "-/+ Select mix-in source" },
	{ "/ Toggle between stream and sample source"},
	NULL
};


static char	*get_arr_embedded_help(char *ehelp[])
{
	int i;
	int len = 0;
	for( i = 0; ehelp[i] != NULL ; i ++ ) {
		len += strlen(ehelp[i]);
	}
	if( len <= 0 )
		return NULL;
	char *msg = (char*) vj_malloc(sizeof(char) * len );
	if( msg == NULL )
		return NULL;
	char *p = msg;
	int   x = 0;
	for( i = 0; ehelp[i] != NULL; i ++ ) {
		x = strlen(ehelp[i]);
		strncpy(p,ehelp[i],x);
		p += x;
	}
	return msg;
}

char	*get_embedded_help( int fx_mode, int play_mode, int fx_entry, int id )
{
	char msg[16384];
	if( play_mode == VJ_PLAYBACK_MODE_PLAIN || ( play_mode == VJ_PLAYBACK_MODE_SAMPLE && fx_mode == 0 ) )
	{
		return get_arr_embedded_help( embedded_help );
	} else {
		veejay_memset(msg,0,sizeof(msg));
		int fx_id = 0;
		if( play_mode == VJ_PLAYBACK_MODE_TAG  ) {
			fx_id = vj_tag_get_effect_any(id,fx_entry);
		} else if( play_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
			fx_id = sample_get_effect_any(id,fx_entry);
		}
		if( fx_id <= 0 ) 
			return NULL;

		int n = vj_effect_get_num_params( fx_id );
		char *fx_descr = vj_effect_get_description(fx_id);
		sprintf(msg,"FX slot %d:%s\n", fx_entry, fx_descr );
		char *p   = msg + strlen(msg);
		int i;
		for( i = 0; i < n ; i ++ ) { //@ specific FX help
			char name[128];
			char *descr = vj_effect_get_param_description(fx_id,i );
			snprintf(name,sizeof(name)-1,"%s'%s'\n",fx_embedded_help[i].msg,descr );
			int len = strlen(name);
			strncpy(p, name, len );
			p += len;
			//free(descr);
		}
		for( i = 0; fx_embedded_help[8+i].msg != NULL; i ++ ) {
			int len = strlen(fx_embedded_help[8+i].msg);
			strncpy(p, fx_embedded_help[8+i].msg, len );
			p += len;
		}
		return strdup(msg);
	}
	return NULL;
}

static  void    init_vims_for_macro();

static	void	macro_select( int slot )
{
	if( slot >= 0 && slot < 12 )
	{
		macro_bank_[ current_macro_ ] = macro_port_;
		current_macro_ = slot;
		macro_port_    = macro_bank_[ current_macro_ ];
		if( !macro_port_ )
		{
			if( macro_status_ == 1 )
			{
				veejay_msg(VEEJAY_MSG_INFO,
					"Continuing recording keystrokes in slot %d", current_macro_);
				macro_bank_[ current_macro_ ] =
					vpn(VEVO_ANONYMOUS_PORT );
				macro_port_ = macro_bank_[ current_macro_ ];
			}
			else if (macro_status_ == 2 )
			{
				veejay_msg(VEEJAY_MSG_INFO,
					"No keystrokes found in slot %d", current_macro_);
			}
		}
		macro_current_age_ = 0;
		macro_expected_age_ = 0;
	}
}

static	void	replay_macro_(void)
{
	int i,k;
	char **items;

	if(!macro_port_ )
		return;	

	items = vevo_list_properties( macro_port_ );
	if(items)
	{
		int strokes = 0;
		for(k = 0; items[k] != NULL ; k ++ )
		{
			void *mb = NULL;
			if( vevo_property_get( macro_port_, items[k],0,&mb ) == VEVO_NO_ERROR )
			{
				macro_block_t *m = (macro_block_t*) mb;
				for( i = 0; i < MAX_MACROS; i ++ )
				{
					if(m->msg[i]) { m->pending[i] = 1; strokes ++; }
				}
			}
			free(items[k]);	
		}
		veejay_msg(VEEJAY_MSG_INFO, "Replay %d keystrokes in macro slot %d!", strokes,
			current_macro_ );
		free(items);
	}
}

static	void	reset_macro_(void)
{
	int i,k;
	char **items;

	if(!macro_port_ )
		return;	

	items = vevo_list_properties( macro_port_ );
	if(items)
	{
		int strokes = 0;
		for(k = 0; items[k] != NULL ; k ++ )
		{
			void *mb = NULL;
			if( vevo_property_get( macro_port_, items[k],0,&mb ) == VEVO_NO_ERROR )
			{
				macro_block_t *m = (macro_block_t*) mb;
				for( i = 0; i < MAX_MACROS; i ++ )
					if(m->msg[i]) { free(m->msg[i]); strokes ++; }
				free(m);
			}
			free(items[k]);	
		}
		veejay_msg(VEEJAY_MSG_INFO, "Cleared %d keystrokes from macro slot %d",
				strokes, current_macro_ );
		free(items);
	}
	vevo_port_free(macro_port_);
	macro_bank_[ current_macro_ ] = NULL;
	macro_port_ = NULL;
}

static	char	*retrieve_macro_(veejay_t *v, long frame, int idx )
{
	void *mb = NULL;
	char key[16];

	int s = 0;
	if( SAMPLE_PLAYING(v))
		s = sample_get_framedups( v->uc->sample_id );
	else if ( PLAIN_PLAYING(v))
		s = v->settings->simple_frame_dup;

	snprintf(key,16,"%08ld%02d", frame,s );

	int error = vevo_property_get( macro_port_, key, 0, &mb );
	if( error == VEVO_NO_ERROR )
	{
		if( idx == MAX_MACROS )
			return NULL;

		macro_block_t *m = (macro_block_t*) mb;
		if( m->msg[idx ] && m->pending[idx] == 1 && m->age[idx] == macro_expected_age_)
		{
			m->pending[idx] = 0;
			macro_expected_age_ ++;
			return m->msg[idx];
		}
	}
	return NULL;
}

static	void	store_macro_(veejay_t *v, char *str, long frame )
{
	void *mb = NULL;
	char key[16];
	int k;
	int s = 0;
	if( SAMPLE_PLAYING(v))
		s = sample_get_framedups( v->uc->sample_id );
	else if ( PLAIN_PLAYING(v))
		s = v->settings->simple_frame_dup;


	snprintf(key,16,"%08ld%02d", frame,s );

	int error = vevo_property_get( macro_port_, key, 0, &mb );
	if( error != VEVO_NO_ERROR )
	{ // first element
		macro_block_t *m = vj_calloc( sizeof(macro_block_t));
		m->msg[0] = strdup(str); 	
		m->pending[0] = 1;
		m->age[0] = macro_current_age_;
		macro_current_age_++;
		vevo_property_set( macro_port_, key, VEVO_ATOM_TYPE_VOIDPTR,1,&m );
	}
	else
	{
	 // following elements
		macro_block_t *c = (macro_block_t*) mb;
		for( k = 1; k < MAX_MACROS; k ++ )
		{
			if(c->msg[k] == NULL )	
			{
				c->msg[k] = strdup(str);
				c->pending[k] = 1;
				c->age[k] = macro_current_age_;
				macro_current_age_ ++;
				return;
			}
		}
		veejay_msg(VEEJAY_MSG_ERROR, "Slot for frame %ld is full (keystroke recorder)",frame );
	}

	veejay_msg(0, "key = %s, '%s' %ld", key,str,frame);
	
}



static hash_val_t int_bundle_hash(const void *key)
{
	return (hash_val_t) key;
}

static int int_bundle_compare(const void *key1,const void *key2)
{
	return ((int)key1 < (int) key2 ? -1 : 
		((int) key1 > (int) key2 ? +1 : 0));
}

typedef struct {
	int event_id;
	int accelerator;
	int modifier;
	char *bundle;
} vj_msg_bundle;


/* forward declarations (former console sample/tag print info) */
#ifdef HAVE_SDL
vj_keyboard_event *new_keyboard_event( int symbol, int modifier, const char *value, int event_id );
vj_keyboard_event *get_keyboard_event( int id );
int	keyboard_event_exists(int id);
int	del_keyboard_event(int id );
char *find_keyboard_default(int id);
#endif
void vj_event_print_plain_info(void *ptr, int x);
void vj_event_print_sample_info(veejay_t *v, int id); 
void vj_event_print_tag_info(veejay_t *v, int id); 
int vj_event_bundle_update( vj_msg_bundle *bundle, int bundle_id );
vj_msg_bundle *vj_event_bundle_get(int event_id);
int vj_event_bundle_exists(int event_id);
int vj_event_suggest_bundle_id(void);
int vj_event_load_bundles(char *bundle_file);
int vj_event_bundle_store( vj_msg_bundle *m );
int vj_event_bundle_del( int event_id );
vj_msg_bundle *vj_event_bundle_new(char *bundle_msg, int event_id);
void vj_event_trigger_function(void *ptr, vj_event f, int max_args, const char format[], ...); 
void  vj_event_parse_bundle(veejay_t *v, char *msg );
int	vj_has_video(veejay_t *v, editlist *el);
void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);
void    vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod);
#ifdef HAVE_SDL
static vims_key_list * vj_event_get_keys( int event_id );
int vj_event_single_fire(void *ptr , SDL_Event event, int pressed);
int vj_event_register_keyb_event(int event_id, int key_id, int key_mod, const char *args);
void vj_event_unregister_keyb_event(int key_id, int key_mod);
#endif

#ifdef HAVE_XML2
void    vj_event_format_xml_event( xmlNodePtr node, int event_id );
//void	vj_event_format_xml_stream( xmlNodePtr node, int stream_id );
#endif
void	vj_event_init(void);

int	vj_has_video(veejay_t *v,editlist *el)
{
	if( el->is_empty )
		return 0;
	if( !el->has_video)
		return 0;
	if( el->video_frames > 0 )
		return 1;
	return 0;
}

int vj_event_bundle_update( vj_msg_bundle *bundle, int bundle_id )
{
	if(bundle) {
		hnode_t *n = hnode_create(bundle);
		if(!n) return 0;
		hnode_put( n, (void*) bundle_id);
		hnode_destroy(n);
		return 1;
	}
	return 0;
}

static	void	constrain_sample( veejay_t *v,int n )
{
#ifdef STRICT_CHECKING
	assert( v->font != NULL );
#endif
	vj_font_set_dict(v->font, sample_get_dict(n) );
	//	v->current_edit_list->video_fps,
	vj_font_prepare( v->font, sample_get_startFrame(n),
			sample_get_endFrame(n) );

}

static	void	constrain_stream( veejay_t *v, int n, long hi )
{
#ifdef STRICT_CHECKING
	assert(v->font != NULL );
#endif
	vj_font_set_dict(v->font, vj_tag_get_dict(n) );
	//	v->current_edit_list->video_fps,
	vj_font_prepare( v->font, 0, vj_tag_get_n_frames(n) );
}

vj_msg_bundle *vj_event_bundle_get(int event_id)
{
	vj_msg_bundle *m;
	hnode_t *n = hash_lookup(BundleHash, (void*) event_id);
	if(n) 
	{
		m = (vj_msg_bundle*) hnode_get(n);
		if(m)
		{
			return m;
		}
	}
	return NULL;
}
#ifdef HAVE_SDL
void			del_all_keyb_events()
{
	if(!keyboard_events)
		return;

	if(!hash_isempty( keyboard_events ))
	{
		hscan_t scan;
		hash_scan_begin( &scan, keyboard_events );
		hnode_t *node;
		while( ( node = hash_scan_next(&scan)) != NULL )
		{
			vj_keyboard_event *ev = NULL;
			ev = hnode_get( node );
			if(ev)
			{
				if(ev->arguments) free(ev->arguments);
				if(ev->vims) free(ev->vims);
			}	
		}
		hash_free_nodes( keyboard_events );
		hash_destroy( keyboard_events );
	}

	veejay_memset( keyboard_event_map_, 0, sizeof(keyboard_event_map_));
}

int			del_keyboard_event(int id )
{
	hnode_t *node;
	vj_keyboard_event *ev = get_keyboard_event( id );

	keyboard_event_map_[ id ] = NULL;

	if(ev == NULL)
		return 0;
	node = hash_lookup( keyboard_events, (void*) id );
	if(!node)
		return 0;
	if(ev->arguments)
		free(ev->arguments);
	if(ev->vims )
		free(ev->vims );
	hash_delete( keyboard_events, node );

	return 1;  
}

vj_keyboard_event	*get_keyboard_event(int id )
{
	hnode_t *node = hash_lookup( keyboard_events, (void*) id );
	if(node)
		return ((vj_keyboard_event*) hnode_get( node ));
	return NULL;
}

int		keyboard_event_exists(int id)
{
	hnode_t *node = hash_lookup( keyboard_events, (void*) id );
	if(node)
		if( hnode_get(node) != NULL )
			return 1;
	return 0;
}


vj_keyboard_event *new_keyboard_event(
		int symbol, int modifier, const char *value, int event_id )
{
//	int vims_id = event_id;
/*	if(vims_id == 0)
	{
		if(!vj_event_bundle_exists( event_id ))
		{
			veejay_msg(VEEJAY_MSG_ERROR,
				"VIMS %d does not exist", event_id);
			return NULL;
		}
	}*/
	

	if( event_id <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR,
		 "VIMS event %d does not exist", event_id );
		return NULL;
	}

	vj_keyboard_event *ev = (vj_keyboard_event*)vj_calloc(sizeof(vj_keyboard_event));
	if(!ev)
		return NULL;
	ev->vims = (vj_events*) vj_calloc(sizeof(vj_events));
	if(!ev->vims)
		return NULL;

	ev->event_id = event_id;

	keyboard_event_map_ [ (modifier * SDLK_LAST) + symbol ] = ev;

	if(value)
	{
		ev->arg_len = strlen(value);
		ev->arguments = strndup( value, ev->arg_len );
	}
	else
	{
		if(event_id < VIMS_BUNDLE_START || event_id > VIMS_BUNDLE_END)
		{
			ev->arguments = find_keyboard_default( event_id );
			if(ev->arguments)
				ev->arg_len = strlen(ev->arguments);
			else
			{
				ev->arguments = NULL;
				ev->arg_len   = 0;
			}
		}	
	}

	if( vj_event_exists( event_id ) )
	{
		ev->vims->act = (vj_event) vj_event_vevo_get_event_function( event_id );
		ev->vims->list_id  = event_id;
	}
	else if ( vj_event_bundle_exists( event_id ) )
	{
		ev->vims->act = vj_event_do_bundled_msg;
		ev->vims->list_id = event_id;
	}
	ev->key_symbol = symbol;
	ev->key_mod = modifier;

	return ev;
}
#endif

int vj_event_bundle_exists(int event_id)
{
	hnode_t *n = hash_lookup( BundleHash, (void*) event_id );
	if(!n)
		return 0;
	return ( vj_event_bundle_get(event_id) == NULL ? 0 : 1);
}

int vj_event_suggest_bundle_id(void)
{
	int i;
	for(i=VIMS_BUNDLE_START ; i < VIMS_BUNDLE_END; i++)
	{
		if ( vj_event_bundle_exists(i ) == 0 ) return i;
	}

	return -1;
}

int vj_event_bundle_store( vj_msg_bundle *m )
{
	hnode_t *n;
	if(!m) return 0;
	n = hnode_create(m);
	if(!n) return 0;
	if(!vj_event_bundle_exists(m->event_id))
	{
		hash_insert( BundleHash, n, (void*) m->event_id);
	}
	else
	{
		hnode_put( n, (void*) m->event_id);
		hnode_destroy( n );
	}

	// add bundle to VIMS list
	veejay_msg(VEEJAY_MSG_DEBUG,
		"Added Bundle VIMS %d to net_list", m->event_id );
 
	net_list[ m->event_id ].list_id = m->event_id;
	net_list[ m->event_id ].act = vj_event_none;
	return 1;
}

int vj_event_bundle_del( int event_id )
{
	hnode_t *n;
	vj_msg_bundle *m = vj_event_bundle_get( event_id );
	if(!m) return -1;

	n = hash_lookup( BundleHash, (void*) event_id );
	if(!n)
		return -1;

	net_list[ m->event_id ].list_id = 0;
	net_list[ m->event_id ].act = vj_event_none;

#ifdef HAVE_SDL
	vj_event_unregister_keyb_event( m->accelerator, m->modifier );
#endif	
	if( m->bundle )
		free(m->bundle);
	if(m)
		free(m);
	m = NULL;

	hash_delete( BundleHash, n );
	return 0;
}

vj_msg_bundle *vj_event_bundle_new(char *bundle_msg, int event_id)
{
	vj_msg_bundle *m;
	int len = 0;
	if(!bundle_msg || strlen(bundle_msg) < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Doesn't make sense to store empty bundles in memory");
		return NULL;
	}	
	len = strlen(bundle_msg);
	m = (vj_msg_bundle*) malloc(sizeof(vj_msg_bundle));
	if(!m) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating memory for bundled message");
		return NULL;
	}
	memset(m, 0, sizeof(m) );
	m->bundle = (char*) vj_calloc(sizeof(char) * len+1);
	m->accelerator = 0;
	m->modifier = 0;
	if(!m->bundle)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating memory for bundled message context");
		return NULL;
	}
	veejay_strncpy(m->bundle, bundle_msg, len);
	
	m->event_id = event_id;

	veejay_msg(VEEJAY_MSG_DEBUG, 
		"New VIMS Bundle %d [%s] created",
			event_id, m->bundle );

	return m;
}


void vj_event_trigger_function(void *ptr, vj_event f, int max_args, const char *format, ...) 
{
	va_list ap;
	va_start(ap,format);
	f(ptr, format, ap);	
	va_end(ap);
}



/* parse a keyframe packet */
void	vj_event_parse_kf( veejay_t *v, unsigned char *msg, int len )
{
	if(SAMPLE_PLAYING(v))
	{
		if(sample_chain_set_kfs( v->uc->sample_id, len, msg )==-1)
			veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid key frame blob [%s]",msg);
	}
	else if (STREAM_PLAYING(v))
	{
		if(vj_tag_chain_set_kfs(v->uc->sample_id,len,msg ) == -1)
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Invalid key frame blob [%s]",msg);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Cannot store key frame in this playback mode");
	}
}


/* parse a message received from network */
void vj_event_parse_bundle(veejay_t *v, char *msg )
{

	int num_msg = 0;
	int offset = 3;
	int i = 0;
	
	
	if ( msg[offset] == ':' )
	{
		int j = 0;
		offset += 1; /* skip ':' */
		if( sscanf(msg+offset, "%03d", &num_msg )<= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid number of messages. Skipping message [%s] ",msg);
		}
		if ( num_msg <= 0 ) 
		{
			veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid number of message given to execute. Skipping message [%s]",msg);
			return;
		}

		offset += 3;

		if ( msg[offset] != '{' )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) 'al' expected. Skipping message [%s]",msg);
			return;
		}	

		offset+=1;	/* skip # */

		for( i = 1; i <= num_msg ; i ++ ) /* iterate through message bundle and invoke parse_msg */
		{				
			char atomic_msg[256];
			int found_end_of_msg = 0;
			int total_msg_len = strlen(msg);
			veejay_memset( atomic_msg,0,256 );
			while( (offset+j) < total_msg_len)
			{
				if(msg[offset+j] == '}')
				{
					return; /* dont care about semicolon here */
				}	
				else
				if(msg[offset+j] == ';')
				{
					found_end_of_msg = offset+j+1;
					veejay_strncpy(atomic_msg, msg+offset, (found_end_of_msg-offset));
					atomic_msg[ (found_end_of_msg-offset) ] ='\0';
					offset += j + 1;
					j = 0;
					vj_event_parse_msg( v, atomic_msg, strlen(atomic_msg) );
				}
				j++;
			}
		}
	}
}

void vj_event_dump()
{
	vj_event_vevo_dump();
	
	vj_osc_dump();

}

typedef struct {
	void *value;
} vims_arg_t; 

static	void	dump_arguments_(int net_id,int arglen, int np, int prefixed, char *fmt)
{
	int i;
	char *name = vj_event_vevo_get_event_name( net_id );
	veejay_msg(VEEJAY_MSG_ERROR, "VIMS '%03d' : '%s'", net_id, name );
	if(np < arglen) {
		veejay_msg(VEEJAY_MSG_ERROR, "\tOnly %d arguments of %d seen",arglen,np);	
	} else {
		veejay_msg(VEEJAY_MSG_ERROR, "\tToo many parameters! %d of %d",np,arglen);
	}
	veejay_msg(VEEJAY_MSG_ERROR, "\tFormat is '%s'", fmt );

	for( i = prefixed; i < np; i ++ )
	{
		char *help = vj_event_vevo_help_vims( net_id, i );
		veejay_msg(VEEJAY_MSG_ERROR,"\t\tArgument %d : %s",
			i,help );
		if(help) free(help);
	}
}

static	int vvm_[600];

static	void	init_vims_for_macro()
{
	veejay_memset( vvm_,1, sizeof(vvm_));
	vvm_[VIMS_MACRO] = 0;
	vvm_[VIMS_TRACK_LIST] = 0;
	vvm_[VIMS_RGB24_IMAGE] = 0;
	vvm_[VIMS_SET_SAMPLE_START] =0;
	vvm_[VIMS_SET_SAMPLE_END] = 0;
	vvm_[VIMS_SAMPLE_NEW] = 0;
	vvm_[VIMS_SAMPLE_DEL] = 0;
	vvm_[VIMS_STREAM_DELETE] = 0;
	vvm_[VIMS_SAMPLE_LOAD_SAMPLELIST]=0;
	vvm_[VIMS_SAMPLE_SAVE_SAMPLELIST]=0;
	vvm_[VIMS_SAMPLE_DEL_ALL] = 0;
	vvm_[VIMS_SAMPLE_COPY] = 0;
	vvm_[VIMS_SAMPLE_UPDATE] = 0;
	vvm_[VIMS_SAMPLE_KF_GET]=0;
	vvm_[VIMS_SAMPLE_KF_RESET]=0;
	vvm_[VIMS_SAMPLE_KF_STATUS]=0;
	vvm_[VIMS_STREAM_NEW_V4L] = 0;
	vvm_[VIMS_STREAM_NEW_DV1394] = 0;
	vvm_[VIMS_STREAM_NEW_COLOR] = 0;
	vvm_[VIMS_STREAM_NEW_Y4M] = 0;
	vvm_[VIMS_STREAM_NEW_UNICAST]=0;
	vvm_[VIMS_STREAM_NEW_MCAST]=0;
	vvm_[VIMS_STREAM_NEW_PICTURE]=0;
	vvm_[VIMS_STREAM_SET_DESCRIPTION]=0;
	vvm_[VIMS_SAMPLE_SET_DESCRIPTION]=0;
	vvm_[VIMS_STREAM_SET_LENGTH]=0;
	vvm_[VIMS_SEQUENCE_STATUS]=0;
	vvm_[VIMS_SEQUENCE_ADD]=0;
	vvm_[VIMS_SEQUENCE_DEL]=0;
	vvm_[VIMS_CHAIN_LIST]=0;
	vvm_[VIMS_OUTPUT_Y4M_START]=0;
	vvm_[VIMS_OUTPUT_Y4M_STOP]=0;
	vvm_[VIMS_GET_FRAME]=0;
	vvm_[VIMS_VLOOPBACK_START]=0;
	vvm_[VIMS_VLOOPBACK_STOP]=0;
	vvm_[VIMS_VIDEO_MCAST_START]=0;
	vvm_[VIMS_VIDEO_MCAST_STOP]=0;
	vvm_[VIMS_SYNC_CORRECTION]=0;
	vvm_[VIMS_NO_CACHING]=0;
	vvm_[VIMS_SCREENSHOT]=0;
	vvm_[VIMS_RGB_PARAMETER_TYPE]=0;
	vvm_[VIMS_RESIZE_SDL_SCREEN] =0;
	vvm_[VIMS_DEBUG_LEVEL]=0;
	vvm_[VIMS_SAMPLE_MODE]=0;
	vvm_[VIMS_BEZERK] = 0;
	vvm_[VIMS_AUDIO_ENABLE]=0;
	vvm_[VIMS_AUDIO_DISABLE]=0;
	vvm_[VIMS_RECORD_DATAFORMAT]=0;
	vvm_[VIMS_INIT_GUI_SCREEN]=0;
	vvm_[VIMS_SUSPEND]=0;
	vvm_[VIMS_VIEWPORT]=0;
	vvm_[VIMS_PREVIEW_BW]=0;
	vvm_[VIMS_FRONTBACK]=0;
	vvm_[VIMS_RECVIEWPORT]=0;
	vvm_[VIMS_PROJECTION] = 0;
}

static	int	valid_for_macro(int net_id)
{
	int k;
	if(net_id > 400 || net_id >= 388 || (net_id >= 80 && net_id <= 86) || (net_id >= 50 && net_id <= 59))
		return 0;

	return vvm_[net_id];
}

static	void	dump_argument_( int net_id , int i )
{
	char *help = vj_event_vevo_help_vims( net_id, i );
		veejay_msg(VEEJAY_MSG_ERROR,"\t\tArgument %d : %s",
			i,help );
	if(help) free(help);
}

static	int	vj_event_verify_args( int *fx, int net_id , int arglen, int np, int prefixed, char *fmt )
{
	if(net_id != VIMS_CHAIN_ENTRY_SET_PRESET )
	{	
		if( arglen != np )	
		{
			dump_arguments_(net_id,arglen, np, prefixed, fmt);
			return 0;
		}
	}	
	else
	{
		if( arglen <= 3 )
		{
			dump_arguments_(net_id, arglen,np,prefixed, fmt );
			return 0;
		}
		int fx_id = fx[2];
		if( fx_id <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid Effect ID" );
			return 0;
		}
		else
		{
			int fx_p = vj_effect_get_num_params( fx_id );
			int fx_c = vj_effect_get_extra_frame( fx_id );
			int min = fx_p + (prefixed > 0 ? 0: 3);
			int max = min + ( fx_c ? 2 : 0 ) + prefixed;
			int a_len = arglen -( prefixed > 0 ? prefixed - 1: 0 );	
			if( a_len < min || a_len > max )
			{
				if( a_len < min )
				  veejay_msg(VEEJAY_MSG_ERROR,"Invalid number of parameters for Effect %d (Need %d, only have %d)", fx_id,
					min, a_len );
				if( a_len > max )
				  veejay_msg(VEEJAY_MSG_ERROR,"Invalid number of parameters for Effect %d (At most %d, have %d)",fx_id,
					max, a_len ); 
				return 0;
			} 
			if( a_len > min && a_len < max )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid mixing source given for Effect %d , use <Source Type> <Channel ID>",fx_id);
				return 0;
			}
		}
	}
	return 1;
}

void	vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int prefixed)
{
	int np = vj_event_vevo_get_num_args(net_id);
	char *fmt = vj_event_vevo_get_event_format( net_id );
	int flags = vj_event_vevo_get_flags( net_id );
	int fmt_offset = 1; 
	vims_arg_t	vims_arguments[16];
	memset( vims_arguments, 0, sizeof(vims_arguments) );

	if(!vj_event_verify_args(args , net_id, arglen, np, prefixed, fmt ))
	{
		if(fmt) free(fmt);
		return;
	}

	if( np == 0 )
	{
		vj_event_vevo_inline_fire_default( (void*) v, net_id, fmt );
		if(fmt) free(fmt);
		return;
	}
	
	int i=0;
	while( i < arglen )
	{
		if( fmt[fmt_offset] == 'd' )
		{
			vims_arguments[i].value = (void*) &(args[i]);
		}
		if( fmt[fmt_offset] == 's' )
		{
			if(str_arg == NULL )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Argument %d must be a string! (VIMS %03d)", i,net_id );
				if(fmt) free(fmt);
				return;
			}
			vims_arguments[i].value = (void*) strdup( str_arg );
			if(flags & VIMS_REQUIRE_ALL_PARAMS )
			{
				if( strlen((char*)vims_arguments[i].value) <= 0 )
				{
					veejay_msg(VEEJAY_MSG_ERROR, "Argument %d is not a string!",i );
					if(fmt)free(fmt);
					return;
				}
			}
		}
		fmt_offset += 3;
		i++;
	}
	_last_known_num_args = arglen;

	while( i < np )
	{
		int dv = vj_event_vevo_get_default_value( net_id, i);
		if( fmt[fmt_offset] == 'd' )
		{
			vims_arguments[i].value = (void*) &(dv);
		}
		i++;
	}

	vj_event_vevo_inline_fire( (void*) v, net_id, 	
				fmt,
				vims_arguments[0].value,
				vims_arguments[1].value,
				vims_arguments[2].value,
				vims_arguments[3].value,
				vims_arguments[4].value,
				vims_arguments[5].value,
				vims_arguments[6].value,		
				vims_arguments[7].value,
				vims_arguments[8].value,
				vims_arguments[9].value,
				vims_arguments[10].value,
				vims_arguments[11].value,
				vims_arguments[12].value,
				vims_arguments[13].value,
				vims_arguments[14].value,
				vims_arguments[15].value);
	fmt_offset = 1;
	for ( i = 0; i < np ; i ++ )
	{
		if( vims_arguments[i].value &&
			fmt[fmt_offset] == 's' )
			free( vims_arguments[i].value );
		fmt_offset += 3;
	}
	if(fmt)
		free(fmt);

}

static		int	inline_str_to_int(const char *msg, int *val)
{
	char longest_num[16];
	int str_len = 0;
	if( sscanf( msg , "%d", val ) <= 0 )
		return 0;
	veejay_memset(longest_num,0, 16 );
	sprintf(longest_num, "%d", *val );

	str_len = strlen( longest_num ); 
	return str_len;
}

static		char 	*inline_str_to_str(int flags, char *msg)
{
	char *res = NULL;	
	int len = strlen(msg);
	if( len <= 0 )
		return res;

	if( (flags & VIMS_LONG_PARAMS) ) /* copy rest of message */
	{
		res = (char*) vj_calloc(sizeof(char) * (len+1) );
		veejay_strncpy( res, msg, len );
	}
	else			
	{
		char str[255];
		veejay_memset(str,0, sizeof(str) );
		if(sscanf( msg, "%s", str ) <= 0 )
			return res;
		res = strndup( str, 255 ); 	
	}	
	return res;
}

int	vj_event_parse_msg( void *ptr, char *msg, int msg_len )
{
	veejay_t *v = (veejay_t*)ptr;
	char head[5] = { 0,0,0,0,0};
	int net_id = 0;
	int np = 0;
	if( msg == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Empty VIMS, dropped!");
		return 0;
	}

	if( msg_len < MSG_MIN_LEN )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "VIMS Message too small, dropped!");
		return 0;

	}

	veejay_memcpy(head,msg,4);

	if( strncasecmp( head, "bun", 3 ) == 0 )
	{
		veejay_chomp_str( msg, &msg_len );
		vj_event_parse_bundle( v, msg );
		return 1;
	}

	if( strncasecmp( head, "key", 3 ) == 0 )
	{
		vj_event_parse_kf( v, msg, msg_len );
		return 1;
	}

	veejay_chomp_str( msg, &msg_len );
	msg_len --;

	/* try to scan VIMS id */
	if ( sscanf( head, "%03d", &net_id ) != 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error parsing VIMS selector");
		return 0;
	}

	if( net_id != 412 && net_id != 333)
		veejay_msg(VEEJAY_MSG_DEBUG, "VIMS: Parse message '%s'", msg );
	
	if( net_id <= 0 || net_id >= VIMS_MAX )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "VIMS Selector %d invalid", net_id );
		return 0;
	}

	/* verify format */
	if( msg[3] != 0x3a || msg[msg_len] != ';' )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Syntax error in VIMS message");
		if( msg[3] != 0x3a )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "\tExpected ':' after VIMS selector");
			return 0;
		}
		if( msg[msg_len] != ';' )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "\tExpected ';' to terminate VIMS message");	
			return 0;
		}
	}

	if ( net_id >= VIMS_BUNDLE_START && net_id < VIMS_BUNDLE_END )
	{
		vj_msg_bundle *bun = vj_event_bundle_get(net_id );
		if(!bun)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) internal error: Bundle %d not registered");
			return 0;
		}
		vj_event_parse_bundle( v, bun->bundle );
		return 1;
	}

	if( net_id >= 400 && net_id < 499 )
		vj_server_client_promote( v->vjs[VEEJAY_PORT_CMD] , v->uc->current_link );

	np = vj_event_vevo_get_num_args( net_id );
		
	if ( msg_len <= MSG_MIN_LEN )
	{
		int i_args[16];
		int i = 0;
		while(  i < np  )
		{
			i_args[i] = vj_event_vevo_get_default_value( net_id, i );
			i++;
		}
		vj_event_fire_net_event( v, net_id, NULL, i_args, np, 0 );
		if( macro_status_ == 1 && macro_port_ != NULL)
		{
			if( valid_for_macro(net_id))
				store_macro_( v,msg, v->settings->current_frame_num );
		}
		
	}
	else
	{
		char *arguments = NULL;
		char *fmt = vj_event_vevo_get_event_format( net_id );
		int flags = vj_event_vevo_get_flags( net_id );
		int i = 0;
		int i_args[16];
		char *str = NULL;
		int fmt_offset = 1;
		char *arg_str = NULL;
		memset( i_args, 0, sizeof(i_args) );

		arg_str = arguments = strndup( msg + 4 , msg_len - 4 );

		if( arguments == NULL )
		{
			dump_arguments_( net_id, 0, np, 0, fmt );
			if(fmt) free(fmt );
			return 0;
		}
		if( np <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "VIMS %d accepts no arguments", net_id );
			if(fmt) free(fmt);
			return 0;
		}
		
		while( i < np )
		{
			if( fmt[fmt_offset] == 'd' )
				i_args[i] = vj_event_vevo_get_default_value(net_id, i);
			i++;
		}
			
		for( i = 0; i < np; i ++ )
		{
			int failed_arg = 1;
			
			if( fmt[fmt_offset] == 'd' )
			{
				int il = inline_str_to_int( arguments, &i_args[i] );	
				if( il > 0 )
				{
					failed_arg = 0;
					arguments += il;
				}
			}
			if( fmt[fmt_offset] == 's' && str == NULL)
			{
				str = inline_str_to_str( flags,arguments );
				if(str != NULL )
				{
					failed_arg = 0;
					arguments += strlen(str);
				}
			}
			
			if( failed_arg )
			{
				char *name = vj_event_vevo_get_event_name( net_id );
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid argument %d for VIMS '%03d' : '%s' ",
					i, net_id, name );
				if(name) free(name);
				dump_argument_( net_id, i );	
				if(fmt) free(fmt);
				return 0;
			}

			if( *arguments == ';' || *arguments == 0 )
				break;
			fmt_offset += 3;

			if( *arguments == 0x20 )	
			   *arguments ++;
		}

		i ++;

		if( flags & VIMS_ALLOW_ANY )
 			i = np;

		if( macro_status_ == 1 && macro_port_ != NULL)
		{
			if( valid_for_macro(net_id))
				store_macro_( v,msg, v->settings->current_frame_num );
		}
		vj_event_fire_net_event( v, net_id, str, i_args, i, 0 );
		

		if(fmt) free(fmt);
		if(arg_str) free(arg_str);
		if(str) free(str);

		return 1;
		
	}
	return 0;
}

void vj_event_update_remote(void *ptr)
{
	veejay_t *v = (veejay_t*)ptr;
	int i;

	if( vj_server_poll( v->vjs[VEEJAY_PORT_CMD] ) )
		vj_server_new_connection( v->vjs[VEEJAY_PORT_CMD] );
	if( vj_server_poll( v->vjs[VEEJAY_PORT_STA] ) )
		vj_server_new_connection( v->vjs[VEEJAY_PORT_STA] );

	if( vj_server_poll( v->vjs[VEEJAY_PORT_DAT] ) )
	{
		vj_server_new_connection( v->vjs[VEEJAY_PORT_DAT] );
	}

	if( v->settings->use_vims_mcast )
	{
		int res = vj_server_update(v->vjs[VEEJAY_PORT_MAT],0 );
		if(res > 0)
		{
			v->uc->current_link = 0;
			char *buf = NULL;
			int len =0;
			while( ( buf = vj_server_retrieve_msg( v->vjs[VEEJAY_PORT_MAT], 0, buf,&len )) != NULL )
			{
		
				vj_event_parse_msg( v, buf,len );
			}
		}
		
	}

	v->settings->is_dat = 0;
	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{	
		if( vj_server_link_used( v->vjs[VEEJAY_PORT_CMD], i ) )
		{
			int res = 1;
			while( res != 0 )
			{
				res = vj_server_update( v->vjs[VEEJAY_PORT_CMD], i );
				if(res>0)
				{
					v->uc->current_link = i;
					int n = 0;
					int len = 0;
					char *buf  = NULL;
					while( (buf= vj_server_retrieve_msg(v->vjs[VEEJAY_PORT_CMD],i,buf, &len))!= NULL )
					{
						vj_event_parse_msg( v, buf,len );
						n++;
					}
				}	
				if( res == -1 )
				{
					_vj_server_del_client( v->vjs[VEEJAY_PORT_CMD], i );
					_vj_server_del_client( v->vjs[VEEJAY_PORT_STA], i );
				}
			}
		}
	}

	v->settings->is_dat = 1;
	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{	
		if( vj_server_link_used( v->vjs[VEEJAY_PORT_DAT], i ) )
		{
			int res = 1;
			while( res != 0 )
			{
				res = vj_server_update( v->vjs[VEEJAY_PORT_DAT], i );
				if(res>0)
				{
					v->uc->current_link = i;
					int n = 0;
					int len = 0;
					char *buf  = NULL;
					while( (buf= vj_server_retrieve_msg(v->vjs[VEEJAY_PORT_DAT],i,buf, &len))!= NULL )
					{
						vj_event_parse_msg( v, buf,len );
						n++;
					}
				}	
				if( res == -1 )
				{
					_vj_server_del_client( v->vjs[VEEJAY_PORT_DAT], i );
				}
			}
		}
	}

	//@ repeat macros
	if(macro_status_ == 2 && macro_port_ != NULL)
	{
		int n_macro = 0;
		char *macro_msg = NULL;
		for( n_macro = 0; n_macro < MAX_MACROS ; n_macro ++ )
		{
			macro_msg = retrieve_macro_( v, v->settings->current_frame_num, n_macro );
			if(macro_msg)
				vj_event_parse_msg(v,macro_msg, strlen(macro_msg));
		}
	}


	v->settings->is_dat = 0;


	for( i = 0; i <  VJ_MAX_CONNECTIONS; i ++ )
		if( vj_server_link_used( v->vjs[VEEJAY_PORT_STA], i ))
			veejay_pipe_write_status( v, i );
	
	if(!veejay_keep_messages())
		veejay_reap_messages();
}

void	vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod)
{
	char bundle[4096];
	veejay_memset(bundle,0,4096);
	vj_event_create_effect_bundle(v, bundle, key_num, key_mod );
}

#ifdef HAVE_SDL
int vj_event_single_fire(void *ptr , SDL_Event event, int pressed)
{
	
	SDL_KeyboardEvent *key = &event.key;
	SDLMod mod = key->keysym.mod;
	veejay_t *v =  (veejay_t*) ptr;
	int vims_mod = 0;

	if( (mod & KMOD_LSHIFT) || (mod & KMOD_RSHIFT ))
		vims_mod = VIMS_MOD_SHIFT;
	if( (mod & KMOD_LALT) || (mod & KMOD_ALT) )
		vims_mod = VIMS_MOD_ALT;
	if( (mod & KMOD_CTRL) || (mod & KMOD_CTRL) )
		vims_mod = VIMS_MOD_CTRL;

	int vims_key = key->keysym.sym;
	int index = vims_mod * SDLK_LAST + vims_key;

	vj_keyboard_event *ev = get_keyboard_event( index );
	if(!ev )
	{
	//	veejay_msg(VEEJAY_MSG_ERROR,"Keyboard event %d unknown", index );
		if( event.button.button == SDL_BUTTON_WHEELUP && v->use_osd != 3 ) {
			char msg[100];
			sprintf(msg,"%03d:;", VIMS_VIDEO_SKIP_SECOND );
			vj_event_parse_msg( (veejay_t*) ptr, msg, strlen(msg) );
			return 1;
		} else if (event.button.button == SDL_BUTTON_WHEELDOWN && v->use_osd != 3) {
			char msg[100];
			sprintf(msg,"%03d:;", VIMS_VIDEO_PREV_SECOND );
			vj_event_parse_msg( (veejay_t*) ptr, msg, strlen(msg) );
			return 1;
		}	
		return 0;
	}

	int event_id = ev->vims->list_id;
	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		vj_msg_bundle *bun = vj_event_bundle_get(event_id );
		if(!bun)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Requested BUNDLE %d does not exist", event_id);
			return;
		}
		vj_event_parse_bundle( (veejay_t*) ptr, bun->bundle );
	}
	else
	{
		char msg[100];
		if( ev->arg_len > 0 )
		{
			sprintf(msg,"%03d:%s;", event_id, ev->arguments );
		}
		else
			sprintf(msg,"%03d:;", event_id );
		vj_event_parse_msg( (veejay_t*) ptr, msg, strlen(msg) );
	}
	return 1;
}

#endif
#ifdef HAVE_GL
void vj_event_single_gl_fire(void *ptr , int mod, int key)
{
	int vims_mod = 0;
#ifndef HAVE_SDL
	return;
#else
	switch( key )
	{
		case 0xff0d: key = SDLK_RETURN; break;
		case 0xff1b: key = SDLK_ESCAPE; break;
		case 0xffbe: key = SDLK_F1; break;
		case 0xffbf: key = SDLK_F2; break;
		case 0xffc0: key = SDLK_F3; break;
		case 0xffc1: key = SDLK_F4; break;
		case 0xffc2: key = SDLK_F5; break;
		case 0xffc3: key = SDLK_F6; break;
		case 0xffc4: key = SDLK_F7; break;
		case 0xffc5: key = SDLK_F8; break;
		case 0xffc6: key = SDLK_F9; break;
		case 0xffc7: key = SDLK_F10; break;
	  	case 0xffc8: key = SDLK_F11; break;
		case 0xffc9: key = SDLK_F12; break;
   		case 0xff63: key = SDLK_INSERT; break;
		case 0xff50: key = SDLK_HOME; break;
		case 0xff55: key = SDLK_PAGEUP; break;
		case 0xff56: key = SDLK_PAGEDOWN; break;
		case 0xff57: key = SDLK_END; break; 	     
		case 0xffff: key = SDLK_DELETE;break;
		case 0xff08: key = SDLK_BACKSPACE;break;
		case 0xff52: key = SDLK_UP; break;
		case 0xff53: key = SDLK_RIGHT; break;
		case 0xff54: key = SDLK_DOWN; break;
		case 0xff51: key = SDLK_LEFT; break;
		case 0xffaa: key = SDLK_KP_MULTIPLY; break;
		case 0xffb0: key = SDLK_KP0; break;
		case 0xffb1: case 0xff9c: key = SDLK_KP1; break;
		case 0xffb2: case 0xff99: key = SDLK_KP2; break;
		case 0xffb3: case 0xff9b: key = SDLK_KP3; break;
		case 0xffb4: case 0xff96: key = SDLK_KP4; break;
		case 0xffb5: case 0xff9d: key = SDLK_KP5; break;
		case 0xffb6: case 0xff98: key = SDLK_KP6; break;
		case 0xffb7: case 0xff95: key = SDLK_KP7; break;
		case 0xffb8: case 0xff97: key = SDLK_KP8; break;
		case 0xffb9: case 0xff9a: key = SDLK_KP9; break;
		case 0xffab: key = SDLK_KP_PLUS; break;
		case 0xffad: key = SDLK_KP_MINUS; break;
		case 0xff8d: key = SDLK_KP_ENTER; break;
		case 0xffaf: key = SDLK_KP_DIVIDE; break;
		case 0xff9e: case 0xff9f: key = SDLK_KP_PERIOD; break;
		case 65507: key = SDLK_s; mod = 2; break;
		default:
			if( key > (256+128))
				veejay_msg(VEEJAY_MSG_DEBUG, "\tUnknown key pressed %x, mod = %d", key, mod );
			break;
		     
	}

	switch( mod )
	{
		case 1:
		case 17:
			vims_mod = VIMS_MOD_SHIFT; break;
		case 4:
		case 20:
			vims_mod = VIMS_MOD_CTRL; break;
		case 8:
		case 24:
		case 144:
			vims_mod = VIMS_MOD_ALT; break;
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "\tUnknown modifier pressed %x, mod = %d", key , mod );
			break;

	}
	

	int vims_key = key;
	int index = vims_mod * SDLK_LAST + vims_key;

	vj_keyboard_event *ev = get_keyboard_event( index );
	if(!ev )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Keyboard event %d unknown", index );
		return;
	}

	// event_id is here VIMS list entry!
	int event_id = ev->vims->list_id;

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		vj_msg_bundle *bun = vj_event_bundle_get(event_id );
		if(!bun)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Requested BUNDLE %d does not exist", event_id);
			return;
		}
		vj_event_parse_bundle( (veejay_t*) ptr, bun->bundle );
	}
	else
	{
		char msg[100];
		if( ev->arg_len > 0 )
		{
			sprintf(msg,"%03d:%s;", event_id, ev->arguments );
		}
		else
			sprintf(msg,"%03d:;", event_id );
		vj_event_parse_msg( (veejay_t*) ptr, msg,strlen(msg) );
	}
#endif
}


#endif
void vj_event_none(void *ptr, const char format[], va_list ap)
{
	veejay_msg(VEEJAY_MSG_DEBUG, "No action implemented for requested event");
}

#ifdef HAVE_XML2
static	int	get_cstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, char *dst )
{
	xmlChar *tmp = NULL;
	char *t = NULL;
	if(! xmlStrcmp( cur->name, what ))
	{
		tmp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1 );
		t   = UTF8toLAT1(tmp);
		if(!t)
			return 0;
#ifdef STRICT_CHECKING
		veejay_msg(VEEJAY_MSG_DEBUG, "Load string property '%s' with value '%s'",
			cur->name, t);
#endif

		veejay_strncpy( dst, t, strlen(t) );	
		free(t);
		xmlFree(tmp);
		return 1;
	}
	return 0;
}
static	int	get_fstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, float *dst )
{
	xmlChar *tmp = NULL;
	char *t = NULL;
	float tmp_f = 0;
	int n = 0;
	if(! xmlStrcmp( cur->name, what ))
	{
		tmp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1 );
		t   = UTF8toLAT1(tmp);
		if(!t)
			return 0;
#ifdef STRICT_CHECKING
		veejay_msg(VEEJAY_MSG_DEBUG, "Load float property '%s' with value '%s'",
			cur->name, t);
#endif

		n = sscanf( t, "%f", &tmp_f );
		free(t);
		xmlFree(tmp);

		if( n )
			*dst = tmp_f;
		else
			return 0;

		return 1;
	}
	return 0;
}

static	int	get_istr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, int *dst )
{
	xmlChar *tmp = NULL;
	char *t = NULL;
	int tmp_i = 0;
	int n = 0;
	if(! xmlStrcmp( cur->name, what ))
	{
		tmp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1 );
		t   = UTF8toLAT1(tmp);
		if(!t)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Input not in UTF8 format!");
			return 0;
		}
#ifdef STRICT_CHECKING
		veejay_msg(VEEJAY_MSG_DEBUG, "Load int property '%s' with value '%s'",
			cur->name, t);
#endif

		n = sscanf( t, "%d", &tmp_i );
		free(t);
		xmlFree(tmp);

		if( n )
			*dst = tmp_i;
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot convert value '%s'to number", t);
			return 0;
		}
		return 1;
	}
	return 0;
}
#define XML_CONFIG_STREAM		"stream"
#define XML_CONFIG_STREAM_SOURCE	"source"
#define XML_CONFIG_STREAM_FILENAME	"filename"
#define XML_CONFIG_STREAM_TYPE		"type"
#define XML_CONFIG_STREAM_COLOR		"rgb"
#define XML_CONFIG_STREAM_OPTION	"option"
#define XML_CONFIG_STREAM_CHAIN		"fxchain"

#define XML_CONFIG_KEY_SYM "key_symbol"
#define XML_CONFIG_KEY_MOD "key_modifier"
#define XML_CONFIG_KEY_VIMS "vims_id"
#define XML_CONFIG_KEY_EXTRA "extra"
#define XML_CONFIG_EVENT "event"
#define XML_CONFIG_FILE "config"
#define XML_CONFIG_SETTINGS	   "run_settings"
#define XML_CONFIG_SETTING_PORTNUM "port_num"
#define XML_CONFIG_SETTING_HOSTNAME "hostname"
#define XML_CONFIG_SETTING_PRIOUTPUT "primary_output"
#define XML_CONFIG_SETTING_PRINAME   "primary_output_destination"
#define XML_CONFIG_SETTING_SDLSIZEX   "SDLwidth"
#define XML_CONFIG_SETTING_SDLSIZEY   "SDLheight"
#define XML_CONFIG_SETTING_AUDIO     "audio"
#define XML_CONFIG_SETTING_SYNC	     "sync"
#define XML_CONFIG_SETTING_TIMER     "timer"
#define XML_CONFIG_SETTING_FPS	     "output_fps"
#define XML_CONFIG_SETTING_GEOX	     "Xgeom_x"
#define XML_CONFIG_SETTING_GEOY	     "Xgeom_y"
#define XML_CONFIG_SETTING_BEZERK    "bezerk"
#define XML_CONFIG_SETTING_COLOR     "nocolor"
#define XML_CONFIG_SETTING_YCBCR     "chrominance_level"
#define XML_CONFIG_SETTING_WIDTH     "output_width"
#define XML_CONFIG_SETTING_HEIGHT    "output_height"
#define XML_CONFIG_SETTING_DFPS	     "dummy_fps"
#define XML_CONFIG_SETTING_DUMMY	"dummy"
#define XML_CONFIG_SETTING_NORM	     "video_norm"
#define XML_CONFIG_SETTING_MCASTOSC  "mcast_osc"
#define XML_CONFIG_SETTING_MCASTVIMS "mcast_vims"
#define XML_CONFIG_SETTING_SCALE     "output_scaler"	
#define XML_CONFIG_SETTING_PMODE	"play_mode"
#define XML_CONFIG_SETTING_PID		"play_id"
#define XML_CONFIG_SETTING_SAMPLELIST "sample_list"
#define XML_CONFIG_SETTING_FILEASSAMPLE "file_as_sample"
#define XML_CONFIG_SETTING_EDITLIST   "edit_list"
#define XML_CONFIG_BACKFX	      "backfx"
#define XML_CONFIG_COMPOSITEMODE	"composite_mode"
#define XML_CONFIG_SCALERFLAGS		"scaler_flags"
#define XML_CONFIG_SETTING_OSD		"use_osd"

#define __xml_cint( buf, var , node, name )\
{\
veejay_msg(0,"Try i '%s', '%s'",name,buf);\
sprintf(buf,"%d", var);\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );\
}

#define __xml_cfloat( buf, var , node, name )\
{\
veejay_msg(0,"Try f '%s', '%s'",name,buf);\
sprintf(buf,"%f", var);\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );\
}

#define __xml_cstr( buf, var , node, name )\
{\
if(var != NULL){\
veejay_msg(0,"Try s '%s', '%s'",name,buf);\
veejay_strncpy(buf,var,strlen(var));\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );}\
}


void	vj_event_format_xml_settings( veejay_t *v, xmlNodePtr node  )
{
	char *buf = (char*) vj_calloc(sizeof(char) * 4000 );
	int c = veejay_is_colored();

	__xml_cint( buf, v->video_out,node,		XML_CONFIG_SETTING_PRIOUTPUT );
	__xml_cint( buf, v->bes_width,node,	XML_CONFIG_SETTING_SDLSIZEX );
	__xml_cint( buf, v->bes_height,node,	XML_CONFIG_SETTING_SDLSIZEY );
	__xml_cint( buf, v->uc->geox,node,		XML_CONFIG_SETTING_GEOX );
	__xml_cint( buf, v->uc->geoy,node,		XML_CONFIG_SETTING_GEOY );
	__xml_cint( buf, v->video_output_width,node, XML_CONFIG_SETTING_WIDTH );
	__xml_cint( buf, v->video_output_height,node, XML_CONFIG_SETTING_HEIGHT );

	__xml_cint( buf, v->audio,node,		XML_CONFIG_SETTING_AUDIO );
	__xml_cint( buf, v->sync_correction,node,	XML_CONFIG_SETTING_SYNC );

	__xml_cint( buf, v->uc->use_timer,node,		XML_CONFIG_SETTING_TIMER );
	__xml_cint( buf, v->no_bezerk,node,		XML_CONFIG_SETTING_BEZERK );
	__xml_cint( buf, c,node, XML_CONFIG_SETTING_COLOR );
	__xml_cint( buf, v->pixel_format,node,	XML_CONFIG_SETTING_YCBCR );
	__xml_cfloat( buf,v->dummy->fps,node,	XML_CONFIG_SETTING_DFPS ); 
	__xml_cint( buf, v->dummy->norm,node,	XML_CONFIG_SETTING_NORM );
	__xml_cint( buf, v->dummy->active,node,	XML_CONFIG_SETTING_DUMMY );
	__xml_cint( buf, v->settings->use_mcast,node, XML_CONFIG_SETTING_MCASTOSC );
	__xml_cint( buf, v->settings->use_vims_mcast,node, XML_CONFIG_SETTING_MCASTVIMS );
	__xml_cint( buf, v->settings->zoom ,node,	XML_CONFIG_SETTING_SCALE );
	__xml_cfloat( buf, v->settings->output_fps, node, XML_CONFIG_SETTING_FPS );
	__xml_cint( buf, v->uc->playback_mode, node, XML_CONFIG_SETTING_PMODE );
	__xml_cint( buf, v->uc->sample_id, node, XML_CONFIG_SETTING_PID );
	__xml_cint( buf, v->settings->fxdepth, node, XML_CONFIG_BACKFX);
	__xml_cint( buf, v->settings->composite, node, XML_CONFIG_COMPOSITEMODE );
	__xml_cint( buf, v->settings->sws_templ.flags ,node, XML_CONFIG_SCALERFLAGS );
	__xml_cint( buf, v->uc->file_as_sample, node, XML_CONFIG_SETTING_FILEASSAMPLE );
	__xml_cint( buf, v->use_osd, node, XML_CONFIG_SETTING_OSD );

	free(buf);
}

void	vj_event_xml_parse_config( veejay_t *v, xmlDocPtr doc, xmlNodePtr cur )
{
	if( veejay_get_state(v) != LAVPLAY_STATE_STOP)
		return;

	int c = 0;
	char sample_list[1024];
	veejay_memset(sample_list,0,1024);
	// todo: editlist loading ; veejay restart

	while( cur != NULL )
	{
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SAMPLELIST, sample_list );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PORTNUM, &(v->uc->port) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PRIOUTPUT, &(v->video_out) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SDLSIZEX, &(v->bes_width) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SDLSIZEY, &(v->bes_height) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_AUDIO, &(v->audio) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SYNC, &(v->sync_correction) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_TIMER, &(v->uc->use_timer) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_GEOX, &(v->uc->geox) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_GEOY, &(v->uc->geoy) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_BEZERK, &(v->no_bezerk) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_COLOR, &c );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_YCBCR, &(v->pixel_format) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_WIDTH, &(v->video_output_width) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_HEIGHT,&(v->video_output_height) );
		get_fstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_DFPS, &(v->dummy->fps ) );
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_NORM, &(v->dummy->norm) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_DUMMY, &(v->dummy->active) ); 
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_MCASTOSC, &(v->settings->use_mcast) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_MCASTVIMS, &(v->settings->use_vims_mcast) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SCALE, &(v->settings->zoom) );
		get_fstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_FPS, &(v->settings->output_fps ) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PMODE, &(v->uc->playback_mode) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PID, &(v->uc->sample_id ) );	
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_BACKFX, &(v->settings->fxdepth) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_COMPOSITEMODE, &(v->settings->composite) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SCALERFLAGS, &(v->settings->sws_templ.flags));
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_FILEASSAMPLE, &(v->uc->file_as_sample));
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_OSD, &(v->use_osd));

		cur = cur->next;
	}

	veejay_set_colors( c );
	if(sample_list)
	{
		v->settings->action_scheduler.sl = strdup( sample_list );
		veejay_msg(VEEJAY_MSG_DEBUG, "Scheduled '%s' for restart", sample_list );
		
		v->settings->action_scheduler.state = 1;
	}
}

void vj_event_xml_new_keyb_event( void *ptr, xmlDocPtr doc, xmlNodePtr cur )
{
	int key = 0;
	int key_mod = 0;
	int event_id = 0;	
	
	char msg[4096];
	veejay_memset(msg,0,4096);

	while( cur != NULL )
	{
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_VIMS, &event_id );
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_EXTRA, msg );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_SYM, &key );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_MOD, &key_mod );		
		cur = cur->next;
	}

	if( event_id <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid key '%s' in configuration file", XML_CONFIG_KEY_VIMS);
		return;
	}

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		int b_key = 0, b_mod = 0;
		if( vj_event_bundle_exists(event_id))
		{
			vj_msg_bundle *mm = vj_event_bundle_get( event_id );
			if( mm )
			{
				b_key = mm->accelerator;
				b_mod = mm->modifier;
			}
			if(!override_keyboard)
			{
				veejay_msg(VEEJAY_MSG_WARNING,
					 "Bundle %d already exists in VIMS system! (Bundle in configfile was ignored)",event_id);
				return;
			}
			else
			{
				if(vj_event_bundle_del(event_id) != 0)
				{
					veejay_msg(0, "Unable to delete bundle %d", event_id);
					return;
				}
			}
		}

		vj_msg_bundle *m = vj_event_bundle_new( msg, event_id);
		if(!msg)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to create new Bundle %d - [%s]", event_id, msg );
			return;
		}
	
		m->accelerator = b_key;
		m->modifier    = b_mod;


		if(vj_event_bundle_store(m))
			veejay_msg(VEEJAY_MSG_DEBUG, "Added bundle %d , trigger with key %d (mod %d)", event_id, b_key, b_mod);
	}

#ifdef HAVE_SDL
	if( key > 0 && key_mod >= 0)
	{
		if( override_keyboard )
			vj_event_unregister_keyb_event( key, key_mod );
		if( !vj_event_register_keyb_event( event_id, key, key_mod, NULL ))
			veejay_msg(VEEJAY_MSG_ERROR, "Attaching key %d + %d to Bundle %d ", key,key_mod,event_id);
	}
#endif
}

int  veejay_load_action_file( void *ptr, char *file_name )
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	veejay_t *v = (veejay_t*) ptr;
	if(!file_name)
		return 0;

	doc = xmlParseFile( file_name );

	if(doc==NULL) {
		veejay_msg(0, "Cannot read file '%s'",file_name);	
		return 0;
	}
	
	cur = xmlDocGetRootElement( doc );
	if( cur == NULL)
	{
		veejay_msg(0, "Cannot get document root from '%s'",file_name);
		xmlFreeDoc(doc);
		return 0;
	}

	if( xmlStrcmp( cur->name, (const xmlChar *) XML_CONFIG_FILE))
	{
		veejay_msg(0, "This is not a veejay configuration file.");
		xmlFreeDoc(doc);
		return 0;
	}

	cur = cur->xmlChildrenNode;
	override_keyboard = 1;
	while( cur != NULL )
	{
		if( !xmlStrcmp( cur->name, (const xmlChar*) XML_CONFIG_SETTINGS ) )
		{
			vj_event_xml_parse_config( v, doc, cur->xmlChildrenNode  );
		}
		if( !xmlStrcmp( cur->name, (const xmlChar *) XML_CONFIG_EVENT ))
		{
			vj_event_xml_new_keyb_event( (void*)v, doc, cur->xmlChildrenNode );
		}
		cur = cur->next;
	}
	override_keyboard = 0;
	xmlFreeDoc(doc);	
	return 1;
}

void	vj_event_format_xml_event( xmlNodePtr node, int event_id )
{
	char buffer[4096];
	int key_id=0;
	int key_mod=-1;

	veejay_memset( buffer,0, 4096 );

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END)
	{ /* its a Bundle !*/
		vj_msg_bundle *m = vj_event_bundle_get( event_id );
		if(!m) 
		{	
			veejay_msg(VEEJAY_MSG_ERROR, "bundle %d does not exist", event_id);
			return;
		}
		veejay_strncpy(buffer, m->bundle, strlen(m->bundle) );
		xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_KEY_EXTRA ,
			(const xmlChar*) buffer);
			// m->event_id and event_id should be equal
	}
	/* Put all known VIMS so we can detect differences in runtime
           some Events will not exist if SDL, Jack, DV, Video4Linux would be missing */

	sprintf(buffer, "%d", event_id);
	xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_KEY_VIMS , 
		(const xmlChar*) buffer);
#ifdef HAVE_SDL
	if(key_id > 0 && key_mod >= 0 )
	{
		sprintf(buffer, "%d", key_id );
		xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_KEY_SYM ,
			(const xmlChar*) buffer);
		sprintf(buffer, "%d", key_mod );
		xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_KEY_MOD ,
			(const xmlChar*) buffer);
	}
#endif
}

static	void	vj_event_send_new_id(veejay_t * v, int new_id)
{

	if( vj_server_client_promoted( v->vjs[0], v->uc->current_link ))
	{
		char result[6];
		sprintf( result, "%05d",new_id );
		sprintf(_s_print_buf, "%03d%s",5, result);	
		SEND_MSG( v,_s_print_buf );
	}
}

void vj_event_write_actionfile(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char file_name[512];
	char live_set[512];
	int args[2] = {0,0};
	int i;
	//veejay_t *v = (veejay_t*) ptr;
	xmlDocPtr doc;
	xmlNodePtr rootnode,childnode;	
	P_A(args,file_name,format,ap);
	doc = xmlNewDoc( "1.0" );
	rootnode = xmlNewDocNode( doc, NULL, (const xmlChar*) XML_CONFIG_FILE,NULL);
	xmlDocSetRootElement( doc, rootnode );

	childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) XML_CONFIG_SETTINGS, NULL );
	vj_event_format_xml_settings( v, childnode  );

	for( i = 0; i < VIMS_MAX; i ++ )
	{
		if( net_list[i].list_id > 0 )
		{	
			childnode = xmlNewChild( rootnode,NULL,(const xmlChar*) XML_CONFIG_EVENT ,NULL);
			vj_event_format_xml_event( childnode, i );
		}
	}
	xmlSaveFormatFile( file_name, doc, 1);

	veejay_msg(VEEJAY_MSG_INFO, "Saved Action file as '%s'" , file_name );
	xmlFreeDoc(doc);	
}
#endif  // XML2
void	vj_event_read_file( void *ptr, 	const char format[], va_list ap )
{
	char file_name[512];
	int args[1];

	P_A(args,file_name,format,ap);

#ifdef HAVE_XML2
	if(veejay_load_action_file( ptr, file_name ))
		veejay_msg(VEEJAY_MSG_INFO, "Loaded Action file '%s'", file_name );
	else
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to load Action file '%s'", file_name );
#endif
}

#ifdef HAVE_SDL
vims_key_list	*vj_event_get_keys( int event_id )
{
	vims_key_list *list = vj_calloc( sizeof(vims_key_list));
	vims_key_list *tree = list;
	vims_key_list *next = NULL;
	if ( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
        {
                if( vj_event_bundle_exists( event_id ))
                {
                        vj_msg_bundle *bun = vj_event_bundle_get( event_id );
                        if( bun )
                        {
                                list->key_symbol = bun->accelerator;
                                list->key_mod = bun->modifier;
                        }
                }
		return list;
	}

        if(!hash_isempty( keyboard_events ))
        {
                hscan_t scan;
                hash_scan_begin( &scan, keyboard_events );
                hnode_t *node;
                while( ( node = hash_scan_next(&scan)) != NULL )
                {
                        vj_keyboard_event *ev = NULL;
                        ev = hnode_get( node );
                        if(ev && ev->event_id == event_id)
                        {
				next = vj_calloc( sizeof(vims_key_list));
				 
				tree->key_symbol = ev->key_symbol;
				tree->key_mod = ev->key_mod;
				tree->args = ev->arguments;
				tree->arg_len = ev->arg_len;
				tree->next = next;
				
				tree = next;
                        }       
                }
        }
	return list;
}

void	vj_event_unregister_keyb_event( int sdl_key, int modifier )
{
	int index = (modifier * SDLK_LAST) + sdl_key;
	vj_keyboard_event *ev = get_keyboard_event( index );
	if(ev)
	{
		vj_msg_bundle *m = vj_event_bundle_get( ev->event_id );
		if(m) 
		{
			m->accelerator = 0;
			m->modifier = 0;

			vj_event_bundle_update( m, ev->event_id );
			veejay_msg(VEEJAY_MSG_DEBUG, "Bundle %d dropped key binding",
				ev->event_id);
		}
		if( ev->vims )
			free(ev->vims);
		if( ev->arguments)
			free(ev->arguments );
		veejay_memset(ev, 0, sizeof( vj_keyboard_event ));

		del_keyboard_event( index );
	}
	else
	{
		veejay_msg(0,"No event was attached to key %d : %d", modifier, sdl_key);
	}
}

int 	vj_event_register_keyb_event(int event_id, int symbol, int modifier, const char *value)
{
	int offset = SDLK_LAST * modifier;
	int index = offset + symbol;
	if( keyboard_event_exists( index ))
	{
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Keboard binding %d + %d already exists", modifier, symbol);
		vj_keyboard_event *ff = get_keyboard_event(index);
		if(ff && value)
		{
			if(ff->arguments) free(ff->arguments);
			ff->arguments = strdup(value);
			ff->arg_len   = strlen(value);
			veejay_msg( VEEJAY_MSG_DEBUG,
			  "Updated arguments of keybinding %d+%d, (VIMS %03d:%s;) ",modifier,symbol, ff->event_id,
				value);
			return 1;
		}
		return 0;
	}

	if( vj_event_bundle_exists(event_id))
	{
		vj_keyboard_event *ev = get_keyboard_event( index );
		if( ev )
		{
			ev->key_symbol = symbol;
			ev->key_mod = modifier;
			veejay_msg(VEEJAY_MSG_INFO,
				"Updated Bundle ID %d with keybinding %d+%d",
					 ev->event_id, modifier, symbol );
			return 1;
		}
	}

	vj_keyboard_event *ev = NULL;

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		char val[10];
		vj_msg_bundle *m = vj_event_bundle_get( event_id );
		sprintf(val, "%d", event_id );
		if( m )
		{
			m->accelerator = symbol;
			m->modifier    = modifier;
			
			vj_event_bundle_update( m, event_id );
		
			ev = new_keyboard_event( symbol, modifier, val, event_id );	
			veejay_msg(VEEJAY_MSG_DEBUG, "Bundle %d triggered by key %d (mod %d)", event_id,symbol, modifier);
		}
	}
	else
	{
		ev = new_keyboard_event( symbol, modifier, value, event_id );
	}


	if(!ev)
		return 0;
	
	hnode_t *node = hnode_create( ev );
	if(!node)
	{
		return 0;
	}
	
	hash_insert( keyboard_events, node, (void*) index );
	
	return 1;
}
#endif
void	vj_event_init_network_events()
{
	int i;
	int net_id = 0;
	for( i = 0; i <= 600; i ++ )
	{
		net_list[ net_id ].act =
			(vj_event) vj_event_vevo_get_event_function( i );

		if( net_list[ net_id ].act )
		{
			net_list[net_id].list_id = i;
			net_id ++;
		}
	}	
	veejay_msg(VEEJAY_MSG_DEBUG, "Registered %d VIMS events", net_id );
}
#ifdef HAVE_SDL
char *find_keyboard_default(int id)
{
	char *result = NULL;
	int i;
	for( i = 1; vj_event_default_sdl_keys[i].event_id != 0; i ++ )
	{
		if( vj_event_default_sdl_keys[i].event_id == id )
		{
			if( vj_event_default_sdl_keys[i].value != NULL )
				result = strdup( vj_event_default_sdl_keys[i].value );
			break;
		}
	}
	return result;
}

void	vj_event_init_keyboard_defaults()
{
	int i;
	int keyb_events = 0;
	for( i = 1; vj_event_default_sdl_keys[i].event_id != 0; i ++ )
	{
		if( vj_event_register_keyb_event(
				vj_event_default_sdl_keys[i].event_id,
				vj_event_default_sdl_keys[i].key_sym,
				vj_event_default_sdl_keys[i].key_mod,
				vj_event_default_sdl_keys[i].value ))
		{
			keyb_events++;
		}
		else
		{

			veejay_msg(VEEJAY_MSG_ERROR,
			  "VIMS event %03d does not exist ", vj_event_default_sdl_keys[i].event_id );
		}
	}
}
#endif

void vj_event_init()
{
	int i;
	
	veejay_memset( keyboard_event_map_, 0, sizeof(keyboard_event_map_));

	vj_init_vevo_events();
#ifdef HAVE_SDL
	if( !(keyboard_events = hash_create( HASHCOUNT_T_MAX, int_bundle_compare, int_bundle_hash)))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hash for keyboard events");
		return;
	}
#endif
	for(i=0; i < VIMS_MAX; i++)
	{
		net_list[i].act = vj_event_none;
		net_list[i].list_id = 0;
	}

	if( !(BundleHash = hash_create(HASHCOUNT_T_MAX, int_bundle_compare, int_bundle_hash)))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hashtable for message bundles");
		return;
	}

	vj_event_init_network_events();
#ifdef HAVE_SDL
	vj_event_init_keyboard_defaults();
#endif
	init_vims_for_macro();

}

void vj_event_linkclose(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	veejay_msg(VEEJAY_MSG_INFO, "Remote requested session-end, quitting Client");
	int i = v->uc->current_link;
        _vj_server_del_client( v->vjs[0], i );
        _vj_server_del_client( v->vjs[1], i );
        _vj_server_del_client( v->vjs[3], i );
}

void vj_event_quit(void *ptr, const char format[], va_list ap)
{
	int i;
	veejay_t *v = (veejay_t*)ptr;
	veejay_msg(VEEJAY_MSG_INFO, "Remote requested session-end, quitting Veejay");
//@ hang up clients
	for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
	{	
		if( vj_server_link_used( v->vjs[VEEJAY_PORT_CMD], i ) ) 
		{
			_vj_server_del_client(v->vjs[VEEJAY_PORT_CMD],i);
			_vj_server_del_client(v->vjs[VEEJAY_PORT_STA],i);
		}
	}


	veejay_change_state(v, LAVPLAY_STATE_STOP);         
}

void  vj_event_sample_mode(void *ptr,	const char format[],	va_list ap)
{
}

void	vj_event_set_framerate( void *ptr, const char format[] , va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
        int args[2];
        char *s = NULL;

        P_A(args,s,format,ap);

	float new_fps = (float) args[0] * 0.01;

	if(new_fps == 0.0 )
		new_fps = v->current_edit_list->video_fps;

	veejay_set_framerate( v, new_fps );

	veejay_msg(VEEJAY_MSG_INFO, "Playback engine is now playing at %2.2f FPS", new_fps );
}

void	vj_event_sync_correction( void *ptr,const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *s = NULL;

	P_A(args,s,format,ap);

	if(args[0] == 0 )
	{
		v->sync_correction = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Sync correction disabled");
	}
	else if( args[0] == 1 )
	{
		v->sync_correction = 1;
		veejay_msg(VEEJAY_MSG_INFO, "Sync correction enabled");
	}

}

void vj_event_bezerk(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->no_bezerk) v->no_bezerk = 0; else v->no_bezerk = 1;
	if(v->no_bezerk==1)
		veejay_msg(VEEJAY_MSG_INFO,"Bezerk On  :No sample-restart when changing input channels");
	else
		veejay_msg(VEEJAY_MSG_INFO,"Bezerk Off :Sample-restart when changing input channels"); 
}
void vj_event_no_caching(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->no_caching)
		v->no_caching = 0;
	else
		v->no_caching = 1;

	if(v->no_caching==1)
	{
		int i = 0;
		int k = 0;
		vj_el_break_cache( v->edit_list );
		for( i = 1; i < sample_size() - 1; i ++ ) {
			editlist *e = sample_get_editlist(i);
			if(e) {
				vj_el_break_cache(e); k++;
			}
		}
		veejay_msg(VEEJAY_MSG_INFO,"Cleared %d samples from cache.", k );
	}
	else
	{
		vj_el_setup_cache( v->current_edit_list );
		veejay_msg(VEEJAY_MSG_INFO,"Sample FX Cache enabled : Recycling identicial samples in FX chain (default)"); 
	}

	vj_el_set_caching(v->no_caching);
}

void vj_event_debug_level(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->verbose) v->verbose = 0; else v->verbose = 1;
	veejay_set_debug_level( v->verbose );
	if(v->verbose)
		veejay_msg(VEEJAY_MSG_INFO, "Displaying debug information" );
	else
		veejay_msg(VEEJAY_MSG_INFO, "Not displaying debug information");
}

void vj_event_suspend(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	veejay_change_state(v, LAVPLAY_STATE_PAUSED);
	veejay_msg(VEEJAY_MSG_WARNING, "Suspending veejay");
}

void	vj_event_play_norestart( void *ptr, const char format[], va_list ap )
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	//@ change mode so veejay does not restart samples at all

	if( args[0] == 0 ) { 	
		//@ off
		v->settings->sample_restart = 0;
	} else if ( args[0] == 1 ) {
		//@ on
		v->settings->sample_restart = 1;
	}

}

void vj_event_set_play_mode_go(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;

	P_A(args,s,format,ap);
	if(vj_event_valid_mode(args[0]))
	{
		if(args[0] == VJ_PLAYBACK_MODE_PLAIN) 
		{
			if( vj_has_video(v,v->edit_list) )
				veejay_change_playback_mode(v, args[0], 0);
			else
				veejay_msg(VEEJAY_MSG_ERROR,
				"There are no video files in the editlist");
			return;
		}
	
		if(args[0] == VJ_PLAYBACK_MODE_SAMPLE) 
		{
			if(args[1]==0) args[1] = v->uc->sample_id;
			if(args[1]==-1) args[1] = sample_size()-1;
			if(sample_exists(args[1]))
			{
				veejay_change_playback_mode(v,args[0] ,args[1]);
			}
			else
			{	
				p_no_sample(args[1]);
			}
			return;
		}
		if(args[0] == VJ_PLAYBACK_MODE_TAG)
		{
			if(args[1]==0) args[1] = v->uc->sample_id;
			if(args[1]==-1) args[1] = vj_tag_size()-1;
			if(vj_tag_exists(args[1]))
			{
				veejay_change_playback_mode(v,args[0],args[1]);
			}
			else
			{
				p_no_tag(args[1]);
			}
			return;
		}
	}
	else
	{
		p_invalid_mode();
	}
}



void	vj_event_set_rgb_parameter_type(void *ptr, const char format[], va_list ap)
{	
	
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(args[0] >= 0 && args[0] <= 3 )
	{
		rgb_parameter_conversion_type_ = args[0];
		if(args[0] == 0)
			veejay_msg(VEEJAY_MSG_INFO,"GIMP's RGB -> YUV");
		if(args[1] == 1)
			veejay_msg(VEEJAY_MSG_INFO,"CCIR601 RGB -> YUV");
		if(args[2] == 2)
			veejay_msg(VEEJAY_MSG_INFO,"Broken RGB -> YUV");
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Use: 0=GIMP , 1=CCIR601, 2=Broken");
	}
}

void vj_event_effect_set_bg(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	v->uc->take_bg = 1;
	veejay_msg(VEEJAY_MSG_INFO, "Next frame will be taken for static background\n");
}

void	vj_event_send_keylist( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	unsigned int i,len=0;
	char message[256];
	char *blob = vj_calloc( 1024 * 32 );
	char line[512];
	char header[7];
	int skip = 0;
	if(!hash_isempty( keyboard_events ))
	{
		hscan_t scan;
		hash_scan_begin( &scan, keyboard_events );
		hnode_t *node;
		while( ( node = hash_scan_next(&scan)) != NULL )
		{
			vj_keyboard_event *ev = NULL;
			ev = hnode_get( node );
			if(ev)
			{
				if( ev->event_id >= VIMS_BUNDLE_START && ev->event_id < VIMS_BUNDLE_END )
				{
					skip = 1;
					if( vj_event_bundle_exists(ev->event_id))
					{
						vj_msg_bundle *mm = vj_event_bundle_get( ev->event_id);
						if( mm->bundle ) { skip = 0; snprintf(message, 256, "%s", mm->bundle ); }
					}
				}
				else
				{
					if(ev->arguments)
						snprintf(message,256, "%03d:%s;", ev->event_id,ev->arguments);
					else
						snprintf(message,256, "%03d:;", ev->event_id );
				}

				if(!skip)
				{
					snprintf( line, 512, "%04d%03d%03d%03d%s",
						ev->event_id, ev->key_mod, ev->key_symbol, strlen(message), message );
					int line_len = strlen(line);
					len += line_len;
					veejay_strncat( blob, line, line_len);
				}
				skip = 0;
			}	
		}
	}

	sprintf( header, "%06d", len );

	SEND_MSG( v, header );
	SEND_MSG( v, blob );

	free( blob );

}

static	int	min_bundles_len(veejay_t *v )
{
	vj_msg_bundle *m;
	int i;
	int len = 0;
	const int token_len = 20;
	char tmp[1024];
	char *buf = NULL;

	for( i = 0; i <= 600 ; i ++ )
	{
		if( i >= VIMS_BUNDLE_START && i < VIMS_BUNDLE_END )
		{
			if(!vj_event_bundle_exists(i))
				continue;

			len += token_len;
			m = vj_event_bundle_get(i);
			len += strlen( m->bundle );

		}
		else
		{
			if( !vj_event_exists(i) || (i >= 400 && i < VIMS_BUNDLE_START))
				continue;
		
			char *name = vj_event_vevo_get_event_name(i);
			char *form = vj_event_vevo_get_event_format(i);
		
			len += token_len;
			len += strlen(name);
	
			int form_len = (form ? strlen( form ): 0);
			int name_len = (name ? strlen(name) : 0);
#ifdef HAVE_SDL
			vims_key_list *tree = vj_event_get_keys( i );
			while( tree != NULL )
			{
				vims_key_list *this = tree;
				len += tree->arg_len;
				len += form_len;
				len += token_len;
				len += name_len;
				tree = tree->next;
				free(this);
			}

#endif
			free(name);	
			if(form) free(form);
		}
	}
	return len;
}

void	vj_event_send_bundles(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	vj_msg_bundle *m;
	int i;
	const int token_len = 20;
	char tmp[1024];

	int len = min_bundles_len(v);
#ifdef STRICT_CHECKING
	int consumed_len = len;
#endif

	if( len <= 0 )
	{
		SEND_MSG(v, "000000");
		return;
	}

	char *buf = vj_calloc( len+6+64 );

	int rc  = 0;

	for( i = 0; i <= 600 ; i ++ )
	{
		if( i >= VIMS_BUNDLE_START && i < VIMS_BUNDLE_END )
		{
			if(!vj_event_bundle_exists(i))
				continue;

			m = vj_event_bundle_get(i);
#ifdef STRICT_CHECKING
			assert( m!= NULL);
#endif
			int bun_len = strlen(m->bundle);

			sprintf(tmp, "%04d%03d%03d%04d%s%03d%03d",
				i, m->accelerator, m->modifier, bun_len, m->bundle, 0,0 );

			veejay_strncat( buf, tmp, strlen(tmp) );
#ifdef STRICT_CHECKING
			consumed_len -= strlen(tmp);
			assert( consumed_len > 0 );
#endif
		}
		else
		{
			if( !vj_event_exists(i) || (i >= 400 && i < VIMS_BUNDLE_START) )
				continue;
		
			char *name = vj_event_vevo_get_event_name(i);
			char *form  = vj_event_vevo_get_event_format(i);
#ifdef STRICT_CHECKING
			assert( name != NULL );
#endif
			int name_len = strlen(name);
			int form_len = (form ? strlen(form)  : 0);
#ifdef HAVE_SDL
			vims_key_list *tree = vj_event_get_keys( i );
			while( tree != NULL )
			{
				vims_key_list *this = tree;
				sprintf(tmp, "%04d%03d%03d%04d%s%03d%03d",
					i, tree->key_symbol, tree->key_mod, name_len, name, form_len, tree->arg_len );
				veejay_strncat( buf,tmp,strlen(tmp));
#ifdef STRICT_CHECKING
				if( tree->arg_len )
					assert( tree->args != NULL );
#endif	
				if(form)
					veejay_strncat( buf, form, form_len);	
				if(tree->arg_len)
					veejay_strncat( buf, tree->args, tree->arg_len );
#ifdef STRICT_CHECKING
				consumed_len -= strlen(tmp);
				consumed_len -= form_len;
				consumed_len -= tree->arg_len;
				assert( consumed_len > 0 );
#endif
				tree = tree->next;
				free(this);
			}

#endif
			free(name);
			if(form)
				free(form);

		}
	}

#ifdef STRICT_CHECKING
	assert( consumed_len >= 0 );
#endif
	int  pack_len = strlen( buf );
	char header[7];
	sprintf(header, "%06d", pack_len );
	SEND_MSG(v, header);
	SEND_MSG(v,buf);

	if(buf) free(buf);

}

void	vj_event_send_vimslist(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char *buf = vj_event_vevo_list_serialize();
	SEND_MSG(v,buf);
	if(buf) free(buf);
}

void	vj_event_send_devicelist( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;

	char *buf = vj_tag_scan_devices();
	SEND_MSG( v, buf );
	free(buf);
}


void vj_event_sample_select(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;	
	P_A( args, s , format, ap);

	if(args[0] == 0 )
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1)
	{
		args[0] = sample_size()-1;
	}
	if(sample_exists(args[0]))
	{
		veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE,args[0] );
	}
	else
	{
		p_no_sample(args[0]);
	}
}

void vj_event_tag_select(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;	
	P_A( args, s , format, ap);

	if(args[0] == 0 )
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0]==-1)
	{
		args[0] = vj_tag_size()-1;
	}

	if(vj_tag_exists(args[0]))
	{
		veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG,args[0]);
	}
	else
	{
		p_no_tag(args[0]);
	}
}


void vj_event_switch_sample_tag(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;

	int last_tag = vj_tag_size()-1;
	int last_sample=sample_size()-1;

	if(last_tag < 1 ) 
		last_tag = 1;
	if(last_sample < 1 )
		last_sample = 1;

	if(!STREAM_PLAYING(v) && !SAMPLE_PLAYING(v))
	{
		if(sample_exists(v->last_sample_id)) 
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE, v->last_sample_id);
			return;
		}
		if(vj_tag_exists(v->last_tag_id))
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG, v->last_tag_id);
			return;
		}
		if(sample_size()-1 <= 0)
		{
			if(vj_tag_exists( last_tag ))
			{
				veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_TAG, last_tag);
				return;
			}
		}	
	}

	if(SAMPLE_PLAYING(v))
	{
		if(vj_tag_exists(v->last_tag_id))
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG, v->last_tag_id);
		}
		else if ( vj_tag_exists(last_tag)) 
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG, last_tag);
	}
	else if(STREAM_PLAYING(v))
	{
		if(sample_exists(v->last_sample_id) )
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE, v->last_sample_id);
		}
		else if( sample_exists( last_sample ))
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE,last_sample);
		}
	}
}

void	vj_event_set_volume(void *ptr, const char format[], va_list ap)
{
	int args[1];	
	char *s = NULL;
	P_A(args,s,format,ap)
	if(args[0] >= 0 && args[0] <= 100)
	{
#ifdef HAVE_JACK
		if(vj_jack_set_volume(args[0]))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Volume set to %d", args[0]);
		}
#else		
		veejay_msg(VEEJAY_MSG_ERROR, "Audio support not compiled in");
#endif
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Use a value between 0-100 for audio volume");
	}
}
void vj_event_set_play_mode(void *ptr, const char format[], va_list ap)
{
	int args[1];
	char *s = NULL;
	veejay_t *v = (veejay_t*) ptr;
	P_A(args,s,format,ap);

	if(vj_event_valid_mode(args[0]))
	{
		int mode = args[0];
		/* check if current playing ID is valid for this mode */
		if(mode == VJ_PLAYBACK_MODE_SAMPLE)
		{
			int last_id = sample_size()-1;
			if(last_id == 0)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "There are no samples. Cannot switch to sample mode");
				return;
			}
			if(!sample_exists(v->last_sample_id))
			{
				v->uc->sample_id = last_id;
			}
			if(sample_exists(v->uc->sample_id))
			{
				veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_SAMPLE, v->uc->sample_id );
			}
		}
		if(mode == VJ_PLAYBACK_MODE_TAG)
		{
			int last_id = vj_tag_size()-1;
			if(last_id == 0)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "There are no streams. Cannot switch to stream mode");
				return;
			}
			
			if(!vj_tag_exists(v->last_tag_id)) /* jump to last used Tag if ok */
			{
				v->uc->sample_id = last_id;
			}
			if(vj_tag_exists(v->uc->sample_id))
			{
				veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG, v->uc->sample_id);
			}
		}
		if(mode == VJ_PLAYBACK_MODE_PLAIN)
		{
			if(vj_has_video(v,v->edit_list) )
				veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_PLAIN, 0);
			else
				veejay_msg(VEEJAY_MSG_ERROR,
				 "There are no video files in the editlist");
		}
	}
	else
	{
		p_invalid_mode();
	}
	
}

void vj_event_sample_new(void *ptr, const char format[], va_list ap)
{
	int new_id = 0;
	veejay_t *v = (veejay_t*) ptr;
	if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v)) 
	{
		int args[2];
		char *s = NULL;
		P_A(args,s,format,ap);

		editlist *E = v->edit_list;
		if( SAMPLE_PLAYING(v))
			E = v->current_edit_list;

		if(args[0] < 0)
		{
			args[0] = v->uc->sample_start;
		}
		if(args[1] == 0)
		{
			args[1] = E->total_frames;
		}

		int num_frames = E->total_frames;
		

		if(args[0] >= 0 && args[1] > 0 && args[0] <= args[1] && args[0] <= num_frames &&
			args[1] <= num_frames ) 
		{
			editlist *el = veejay_edit_copy_to_new( v, E, args[0],args[1] );
			if(!el)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cant copy EDL");
				return;
			}
			int start = 0;
			int end   = el->total_frames;

			sample_info *skel = sample_skeleton_new(start, end );
			if(skel)
			{
				skel->edit_list = el;
				if(!skel->edit_list)
					veejay_msg(VEEJAY_MSG_ERROR, "Failed to copy EDL !!");
			}

			if(sample_store(skel)==0) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Created new sample [%d] with EDL", skel->sample_id);
				new_id = skel->sample_id;
			}
		
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid frame range given : %d - %d , range is %d - %d",
				args[0],args[1], 1,num_frames);
		}
	}
	else 
	{
		p_invalid_mode();
	}

	vj_event_send_new_id( v, new_id);

}

void	vj_event_fullscreen(void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	// parsed display num!! -> index of SDL array

	//int id = args[0];
	int id = 0;
	int status = args[0];

	switch(v->video_out)
	{
		/*
		case 4:
#ifdef HAVE_GL
		{
			int go_fs = x_display_get_fs( v->gl ) == 1 ? 0:1;
			x_display_set_fullscreen( v->gl, go_fs );
			v->settings->full_screen = go_fs;
		}
#endif
			break;
		*/
		case 0:
		case 2:
#ifdef HAVE_SDL
		{
			int go_fs = v->sdl[id]->fs == 1 ? 0:1 ;
			char *caption = veejay_title(v);

			vj_sdl *tmpsdl = vj_sdl_allocate( v->video_output_width,v->video_output_height,v->pixel_format);
			
			if(vj_sdl_init(
				v->settings->ncpu,
				tmpsdl,
				v->bes_width,
				v->bes_height,
				caption,
				1,
				go_fs
			) ) {
				if( v->sdl[id] ) {
					vj_sdl_free(v->sdl[id]);
				}
				v->sdl[id] = tmpsdl;		
				if( go_fs)
					vj_sdl_grab( v->sdl[id], 0 );
				v->settings->full_screen = go_fs;
			}
			else {
				vj_sdl_free(tmpsdl);
			}
			free(caption);
		}
#endif
		break;
		default:
		break;
	}
	veejay_msg(VEEJAY_MSG_INFO,"Video screen is %s",
		(v->settings->full_screen ? "full screen" : "windowed"));

}


void vj_event_set_screen_size(void *ptr, const char format[], va_list ap) 
{
	int args[5];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;

	P_A(args,s,format,ap);

	int id = 0;
	int w  = args[0];
	int h  = args[1];
        int x  = args[2];
        int y  = args[3];

	if( w < 0 || w > 4096 || h < 0 || h > 4096 || x < 0 || y < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid arguments '%d %d %d %d'", w,h,x,y );
		return;
	}

	if( w == 0 && h == 0 )
	{
		switch( v->video_out )
		{
			case 0:
			case 2:
#ifdef HAVE_SDL
				if( v->sdl[id] )
				{
					vj_sdl_free( v->sdl[id] );
					free(v->sdl[id]);
					v->sdl[id] = NULL;
					v->video_out = 5;
					vj_sdl_quit();
					veejay_msg(VEEJAY_MSG_INFO, "Closed SDL window");
					return;
				}
#endif
				break;
			default:
				break;
		}
	}
	else
	{
		char *title = veejay_title(v);
		
		switch( v->video_out )
		{
			case 5:
#ifdef HAVE_SDL
				if(!v->sdl[id] )
				{
					v->sdl[id] = vj_sdl_allocate( 
						v->video_output_width,
					 	v->video_output_height,
					 	v->pixel_format );
					veejay_msg(VEEJAY_MSG_INFO, "Allocated SDL window");
				
					if(vj_sdl_init( v->settings->ncpu,
						v->sdl[id],
						v->bes_width,
						v->bes_height,
						title,
						1,
						v->settings->full_screen )
					) {
					veejay_msg(VEEJAY_MSG_INFO, "Opened SDL Video Window of size %d x %d", w, h );
					v->video_out = 0;
					}
				}
#endif
			case 0:
#ifdef HAVE_SDL				
				if( x > 0 && y > 0 )
					vj_sdl_set_geometry(v->sdl[id],x,y);
		
				if( w > 0 && h > 0 )
					vj_sdl_resize( v->sdl[id], w, h, v->settings->full_screen );
#endif				
				break;
		/*
			case 4:
#ifdef HAVE_GL
				if( w > 0 && h > 0 )
					x_display_resize(w,h,w,h);	
#endif
				break;
				*/
			default:
				break;
		}
		free(title);
	}
}

void vj_event_play_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	if(!STREAM_PLAYING(v))
	{
		int speed = v->settings->current_playback_speed;
		veejay_set_speed(v, (speed == 0 ? 1 : 0  ));
		veejay_msg(VEEJAY_MSG_INFO,"Video is %s", (speed==0 ? "paused" : "playing"));
	}
	else
	{
		p_invalid_mode();
	}
}

void	vj_event_render_depth( void *ptr, const char format[] , va_list ap )
{
	int args[1];
	veejay_t *v = (veejay_t*)ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	int status = 0;
	int toggle = 0;
	if( args[0] == 2 ) 
		toggle = 1;

	if( args[0] ) {	
		status = 1;
	}

	if( toggle ) {
		if( v->settings->fxdepth == 1 ) 	
			v->settings->fxdepth = 0;
		else	
			v->settings->fxdepth = 1;	
	} else {
		v->settings->fxdepth = status;
	}
	if( v->settings->fxdepth == 1 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Rendering chain entries 1 - 3 of all underlying samples and streams.");
	} else {
		veejay_msg(VEEJAY_MSG_INFO, "Skipping all FX on all underlying samples and streams.");
	}
}

void	vj_event_viewport_composition( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->composite) {
	if(STREAM_PLAYING(v))
	{
		if( vj_tag_get_composite_view(v->uc->sample_id) == NULL ) {
			veejay_msg(VEEJAY_MSG_WARNING, "No perspective transform setup for Stream %d, play it and press CTRL-s",
				v->uc->sample_id );
			return;
		}

		int status = vj_tag_get_composite( v->uc->sample_id );
		if( status == 1 || status == 2 ) {
			status = 0;
		} else {
			status = 2;
		}
		vj_tag_set_composite( v->composite, v->uc->sample_id, status );

		veejay_msg(VEEJAY_MSG_INFO, "Stream #%d will %s be transformed when used as secundary input",
			v->uc->sample_id, (status==2? "now" : "not"));
		veejay_msg(VEEJAY_MSG_INFO, "Press CTRL+i again to toggle.");

	} else if (SAMPLE_PLAYING(v)) {
		if( sample_get_composite_view(v->uc->sample_id ) == NULL ) {
			veejay_msg(VEEJAY_MSG_WARNING, "No perspective transform setup for Sample %d, play it and press CTRL-s",
				v->uc->sample_id );
			return;
		}
		int status = sample_get_composite( v->uc->sample_id );
		if( status == 1 || status == 2 ) 
			status = 0;
		else 
			status = 2;

		sample_set_composite( v->composite, v->uc->sample_id, status );
		veejay_msg(VEEJAY_MSG_INFO, "Sample #%d will %s be transformed when used as secundary input",
			v->uc->sample_id, (status==2? "now" : "not"));
		veejay_msg(VEEJAY_MSG_INFO, "Press CTRL+i again to toggle.");

		}
	}
}

void vj_event_play_reverse(void *ptr,const char format[],va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(!STREAM_PLAYING(v))
	{
		int speed = v->settings->current_playback_speed;
		if( speed == 0 ) speed = -1;
		else 
			if(speed > 0) speed = -(speed);
		veejay_set_speed(v,
				speed );

		veejay_msg(VEEJAY_MSG_INFO, "Video is playing in reverse at speed %d.", speed);
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_play_forward(void *ptr, const char format[],va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(!STREAM_PLAYING(v))
	{
		int speed = v->settings->current_playback_speed;
		if(speed == 0) speed = 1;
		else if(speed < 0 ) speed = -1 * speed;

	 	veejay_set_speed(v,
			speed );  

		veejay_msg(VEEJAY_MSG_INFO, "Video is playing forward at speed %d" ,speed);
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_play_speed(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	if(!STREAM_PLAYING(v))
	{
		char *s = NULL;
		int speed = 0;
		P_A(args,s,format,ap);
		veejay_set_speed(v, args[0] );
		speed = v->settings->current_playback_speed;
		veejay_msg(VEEJAY_MSG_INFO, "Video is playing at speed %d now (%s)",
			speed, speed == 0 ? "paused" : speed < 0 ? "reverse" : "forward" );
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_play_speed_kb(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	if(!STREAM_PLAYING(v))
	{
		char *s = NULL;
		P_A(args,s,format,ap);
	
		int speed = abs(args[0]);
		if( v->settings->current_playback_speed <  0 )
			veejay_set_speed( v, -1 * speed );
		else
			veejay_set_speed(v, speed );
		speed = v->settings->current_playback_speed;
		veejay_msg(VEEJAY_MSG_INFO, "Video is playing at speed %d now (%s)",
			speed, speed == 0 ? "paused" : speed < 0 ? "reverse" : "forward" );
	}
	else
	{
		p_invalid_mode();
	}
}



void vj_event_play_slow(void *ptr, const char format[],va_list ap)
{
	int args[1];
	veejay_t *v = (veejay_t*)ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v))
	{
		if(args[0] <= 0 )
			args[0] = 1;

		if(veejay_set_framedup(v, args[0]))
		{
			if( SAMPLE_PLAYING(v))
				sample_reset_loopcount( v->uc->sample_id );
			veejay_msg(VEEJAY_MSG_INFO,"A/V frames will be repeated %d times ",args[0]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to set frame repeat of %d", args[0]);
		}
	}
	else
	{
		p_invalid_mode();
	}
	
}


void vj_event_set_frame(void *ptr, const char format[], va_list ap)
{
	int args[1];
	veejay_t *v = (veejay_t*) ptr;
	if(!STREAM_PLAYING(v))
	{
		video_playback_setup *s = v->settings;
		char *str = NULL;
		P_A(args,str,format,ap);
		if(args[0] == -1 )
			args[0] = v->current_edit_list->total_frames;
		veejay_set_frame(v, args[0]);
	}
	else
	{
		p_invalid_mode();
	}
}


void	vj_event_projection_dec( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	
	float inc_x = (float) args[0];
	float inc_y = (float) args[1];

	if(!v->composite)
	{
		veejay_msg(0,"No viewport active");
		return;
	}
	viewport_finetune_coord( composite_get_vp(v->composite),vj_perform_get_width(v), vj_perform_get_height(v),
		inc_x,
		inc_y);
		
}
void	vj_event_projection_inc( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if(!v->composite)
	{
		veejay_msg(0,"No viewport active");
		return;
	}
	viewport_finetune_coord( composite_get_vp(v->composite),vj_perform_get_width(v), vj_perform_get_height(v),
		args[0],
		args[1]);
}

void vj_event_inc_frame(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	P_A( args,s,format, ap );
	if(!STREAM_PLAYING(v))
	{
		video_playback_setup *s = v->settings;
		veejay_set_frame(v, (s->current_frame_num + args[0]));
		veejay_msg(VEEJAY_MSG_INFO, "Skip to frame %d", s->current_frame_num);
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_dec_frame(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *) ptr;
	int args[1];
	char *s = NULL;
	P_A( args,s,format, ap );
	if(!STREAM_PLAYING(v))
	{
		video_playback_setup *s = v->settings;
		veejay_set_frame(v, (s->current_frame_num - args[0]));
		veejay_msg(VEEJAY_MSG_INFO, "Skip to frame %d", s->current_frame_num);
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_prev_second(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;	
	int args[1];
	char *s = NULL;
	P_A( args,s,format, ap );
	if(!STREAM_PLAYING(v))
	{
		video_playback_setup *s = v->settings;
		veejay_set_frame(v, (s->current_frame_num - (int) 
			    (args[0] * v->current_edit_list->video_fps)));
		veejay_msg(VEEJAY_MSG_INFO, "Skip to frame %d", s->current_frame_num );
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_next_second(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[1];
	char *str = NULL;
	P_A( args,str,format, ap );
	if(!STREAM_PLAYING(v))
	{
		video_playback_setup *s = v->settings;
		veejay_set_frame(v, (s->current_frame_num + (int)
				     ( args[0] * v->current_edit_list->video_fps)));
		veejay_msg(VEEJAY_MSG_INFO, "Skip to frame %d", s->current_frame_num );
	}
	else
	{
		p_invalid_mode();
	}
}


void vj_event_sample_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	video_playback_setup *s = v->settings;
	if(SAMPLE_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		v->uc->sample_start = s->current_frame_num;
		veejay_msg(VEEJAY_MSG_INFO, "Change sample starting position to %ld", v->uc->sample_start);
	}	
	else
	{
		p_invalid_mode();
	}
}



void vj_event_sample_end(void *ptr, const char format[] , va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	video_playback_setup *s = v->settings;

	if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v))
	{
		v->uc->sample_end = s->current_frame_num;
		if( v->uc->sample_end > v->uc->sample_start) {
			long vstart = v->uc->sample_start;
			long vend   = v->uc->sample_end;

			if(v->settings->current_playback_speed < 0) {
				long tmp = vend;
				vend = vstart;
				vstart = tmp;
			}

			if(vstart < 0 ) {
				vstart=0; 
			}
			if(vend > v->current_edit_list->total_frames) {
 				vend = v->current_edit_list->total_frames;
			}

			editlist *E = v->edit_list;
			if( SAMPLE_PLAYING(v))
				E = v->current_edit_list;
			editlist *el = veejay_edit_copy_to_new( v, E, vstart, vend );
			if(!el)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to clone current editlist!");
				return;
			}

			long start = 0;
			long end   = el->total_frames;
	
			sample_info *skel = sample_skeleton_new(start,end);
			if(!skel)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new sample!");
				return;
			}	

			v->uc->sample_start = v->uc->sample_end; // set new starting position (repeat ']')

			skel->edit_list = el;

			if(sample_store(skel)==0) {
				veejay_msg(VEEJAY_MSG_INFO,"Created new Sample %d\t [%ld] | %ld-%ld | [%ld]",
					skel->sample_id,
					0,
					start,
					end,
					el->total_frames);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Unable to create new sample");
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample ending position before starting position. Cannot create new sample");
		}
	}
	else
	{
		p_invalid_mode();
	}

}
 
void vj_event_goto_end(void *ptr, const char format[], va_list ap)
{
  	veejay_t *v = (veejay_t*) ptr;
	if(STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	} 
 	if(SAMPLE_PLAYING(v))
  	{	
		veejay_set_frame(v, sample_get_endFrame(v->uc->sample_id));
		veejay_msg(VEEJAY_MSG_INFO, "Goto sample's endings position");
  	}
  	if(PLAIN_PLAYING(v)) 
 	{
		veejay_set_frame(v,v->current_edit_list->total_frames);
		veejay_msg(VEEJAY_MSG_INFO, "Goto frame %ld of edit decision list",
				v->edit_list->total_frames);
  	}
}

void vj_event_goto_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}
  	if( SAMPLE_PLAYING(v))
	{
		veejay_set_frame(v, sample_get_startFrame(v->uc->sample_id));
		veejay_msg(VEEJAY_MSG_INFO, "Goto sample's starting position"); 
  	}
  	if ( PLAIN_PLAYING(v))
	{
		veejay_set_frame(v,0);
		veejay_msg(VEEJAY_MSG_INFO, "Goto first frame of edit decision list");
  	}
}

void	vj_event_sample_rand_start( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	video_playback_setup *settings = v->settings;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(args[0] == RANDTIMER_FRAME)
		settings->randplayer.timer = RANDTIMER_FRAME;
	else
		settings->randplayer.timer = RANDTIMER_LENGTH;


	settings->randplayer.mode = RANDMODE_SAMPLE;

	vj_perform_randomize(v);
	veejay_msg(VEEJAY_MSG_INFO, "Started sample randomizer, %s",
			(settings->randplayer.timer == RANDTIMER_FRAME ? "freestyling" : "playing full length of gambled samples"));	
}

void	vj_event_sample_rand_stop( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	video_playback_setup *settings = v->settings;

	if(settings->randplayer.mode != RANDMODE_INACTIVE)
		veejay_msg(VEEJAY_MSG_INFO, "Stopped sample randomizer");
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Sample randomizer not started");
	settings->randplayer.mode = RANDMODE_INACTIVE;
}

void vj_event_sample_set_loop_type(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!SAMPLE_PLAYING(v)) 
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;

	if(args[1] == -1)
	{
		if(sample_exists(args[0]))
		{
			if(sample_get_looptype(args[0])==2)
			{
				int lp;
				sample_set_looptype(args[0],1);
				lp = sample_get_looptype(args[0]);
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d loop type is now %s",args[0],
		  			( lp==1 ? "Normal Looping" : (lp==2 ? "Pingpong Looping" : "No Looping" ) ) );
				return;
			}
			else
			{
				int lp;
				sample_set_looptype(args[0],2);
				lp = sample_get_looptype(args[0]);
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d loop type is now %s",args[0],
		  			( lp==1 ? "Normal Looping" : lp==2 ? "Pingpong Looping" : "No Looping" ) );
				return;
			}
		}
		else
		{
			p_no_sample(args[0]);
			return;
		}
	}

	if(args[1] >= 0 && args[1] <= 3) 
	{
		if(sample_exists(args[0]))
		{
			int lp;
			sample_set_looptype( args[0] , args[1]);
			lp = sample_get_looptype(args[0]);
			switch(lp)
			{
				case 0: veejay_msg(VEEJAY_MSG_INFO, "Play once");break;
				case 1: veejay_msg(VEEJAY_MSG_INFO, "Normal looping");break;
				case 2: veejay_msg(VEEJAY_MSG_INFO, "Pingpong looping");break;
				case 3: veejay_msg(VEEJAY_MSG_INFO, "Random frame");break;
			}
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist or invalid looptype %d",args[1],args[0]);
	}
}

void vj_event_sample_set_speed(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args, s, format, ap);

	if(SAMPLE_PLAYING(v))
	{
		if(args[0] == -1)
			args[0] = sample_size() - 1;

		if( args[0] == 0) 
			args[0] = v->uc->sample_id;

		if( sample_set_speed(args[0], args[1]) != -1)
		{
			veejay_msg(VEEJAY_MSG_INFO, "Changed speed of sample %d to %d",args[0],args[1]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Speed %d it too high to set on sample %d !",
				args[1],args[0]); 
		}
	}
	else
	{
		p_invalid_mode();
	}
}

void vj_event_sample_set_marker_start(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	veejay_t *v = (veejay_t*)ptr;
	
	char *str = NULL;
	P_A(args,str,format,ap);
	
	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}

	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0; int end = 0;
		if ( sample_get_el_position( args[0], &start, &end ) )
		{
			if( sample_set_marker_start( args[0], args[1] ) )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker starting position set at %d", args[0],args[1]);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot set marker position %d for sample %d (limits are %d - %d)",args[1],args[0],start,end);
			}
		}
	}
	else
	{
		p_no_sample( args[0] );
	}	
}


void vj_event_sample_set_marker_end(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	
	char *str = NULL;
	P_A(args,str,format,ap);
	
	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();	
		return;
	}

	if( args[0] == 0 ) 
		args[0] = v->uc->sample_id;

	if(args[0] == -1)
		args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0; int end = 0;
		if ( sample_get_el_position( args[0], &start, &end ) )
		{
			args[1] = end - args[1]; // add sample's ending position
			if( sample_set_marker_end( args[0], args[1] ) )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker ending position set at position %d", args[0],args[1]);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_INFO, "Marker position out side of sample boundaries");
			}
		}	
	}
	else
	{
		p_no_sample(args[0]);
	}
}


void vj_event_sample_set_marker(void *ptr, const char format[], va_list ap) 
{
	int args[3];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0;
		int end = 0;
		if( sample_get_el_position( args[0], &start, &end ) )
		{
			if( sample_set_marker( args[0], args[1],args[2] ) )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker starting position at %d, ending position at %d", args[0],args[1],args[2]);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot set marker %d-%d for sample %d",args[1],args[2],args[0]);
			}
		}
	}
	else
	{
		p_no_sample( args[0] );
	}	
}


void vj_event_sample_set_marker_clear(void *ptr, const char format[],va_list ap) 
{
	int args[1];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0) 
		args[0] = v->uc->sample_id;

	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		if( sample_marker_clear( args[0] ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker cleared", args[0]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot set marker %d-%d for sample %d",args[1],args[2],args[0]);
		}
	}
	else
	{
		p_no_sample(args[0]);
	}	
}

void vj_event_sample_set_dup(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0) 
		args[0] = v->uc->sample_id;
	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0])) 
	{
		if( args[1] <= 0 )
			args[1] = 1;
		if( sample_set_framedup( args[0], args[1] ) != -1) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d frame repeat set to %d", args[0],args[1]);
			if( args[0] == v->uc->sample_id)
			{
			    if(veejay_set_framedup(v, args[1]))
               		    {
                       		 veejay_msg(VEEJAY_MSG_INFO,
					"Video frame will be duplicated %d to output",args[1]);
                	    }
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot set frame repeat to %d for sample %d",args[0],args[1]);
		}
		sample_reset_loopcount( args[0] );
	}
	else
	{
		p_no_sample(args[0]);
	}
}

void	vj_event_tag_set_descr( void *ptr, const char format[], va_list ap)
{
	char str[TAG_MAX_DESCR_LEN];
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	P_A(args,str,format,ap);

	if(!STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0 )
		args[0] = v->uc->sample_id;

	if(args[0] == -1)
		args[0] = vj_tag_size()-1;

	if( vj_tag_set_description(args[0],str) == 1)
		veejay_msg(VEEJAY_MSG_INFO, "Changed stream title to '%s'", str );
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot change title of stream %d to '%s'", args[0], str );
}

void vj_event_sample_set_descr(void *ptr, const char format[], va_list ap)
{
	char str[SAMPLE_MAX_DESCR_LEN];
	int args[5];
	veejay_t *v = (veejay_t*) ptr;
	P_A(args,str,format,ap);

	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if( args[0] == 0 ) 
		args[0] = v->uc->sample_id;

	if(args[0] == -1) args[0] = sample_size()-1;

	if(sample_set_description(args[0],str) == 0)
		veejay_msg(VEEJAY_MSG_INFO, "Changed sample title to %s",str);
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot change title of sample %d to '%s'", args[0],str );
}

#ifdef HAVE_XML2
void vj_event_sample_save_list(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	int *args = NULL;
	P_A(args,str,format,ap);
	if(sample_writeToFile( str, v->composite,v->seq,v->font, v->uc->sample_id, v->uc->playback_mode) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Saved %d samples to file '%s'", sample_size()-1, str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error saving samples to file %s", str);
	}
}

void vj_event_sample_load_list(void *ptr, const char format[], va_list ap)
{
	char str[1024];
	int *args = NULL;
	veejay_t *v = (veejay_t*) ptr;
	P_A( args, str, format, ap);

	int id = 0;
	int mode = 0;
	
	if( sample_readFromFile( str, v->composite,v->seq, v->font, v->edit_list, &id, &mode ) ) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "Loaded sample list from file '%s'", str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to load samples from file '%s", str);
	}
}
#endif

void vj_event_sample_rec_start( void *ptr, const char format[], va_list ap)
{
	char tmp[255];
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	int result = 0;
	char *str = NULL;
	char prefix[150];
	P_A(args,str,format,ap);

	if(!SAMPLE_PLAYING(v)) 
	{
		p_invalid_mode();
		return;
	}

	veejay_memset(tmp,0,255);
	veejay_memset(prefix,0,150);

	if( !v->seq->active )
	{
		sample_get_description(v->uc->sample_id, prefix );
	}
	else
	{
		if( v->seq->rec_id )
		{
			veejay_msg(0, "Already recording the sequence!");
			return;
		}
		else
		{
			v->seq->rec_id = v->uc->sample_id;
			sprintf( prefix, "sequence_");
		}
	}

	if(!veejay_create_temp_file(prefix, tmp))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create temporary file, Record aborted." );
		if(v->seq->rec_id && v->seq->active)
			v->seq->rec_id = 0;
		return;
	}

	if( args[0] == 0 )
	{
		if(!v->seq->active )
		{
			args[0] = sample_get_longest(v->uc->sample_id);
		}
		else
		{
			int i;
			for( i = 0; i < MAX_SEQUENCES; i ++ )
			{
				args[0] += sample_get_longest( v->seq->samples[i] );
			}
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "\tRecording %d frames", args[0]);
	}

	int format_ = _recorder_format;
	if(format_==-1)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Select a video codec first");
		if(v->seq->active && v->seq->rec_id )
			v->seq->rec_id = 0;
		return; 
	}

	if(args[0] <= 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to record less then 2 frames");
		if(v->seq->active && v->seq->rec_id )
			v->seq->rec_id = 0;

		return;
	}

	if( sample_init_encoder( v->uc->sample_id, tmp, format_, v->current_edit_list, args[0]) == 1)
	{
		video_playback_setup *s = v->settings;
		s->sample_record_id = v->uc->sample_id;
		s->sample_record_switch = args[1];
		result = 1;
		if(v->use_osd)
		{
			veejay_msg(VEEJAY_MSG_INFO,"Turned off OSD, recording now");
			v->use_osd = 0;
		}
		veejay_msg(VEEJAY_MSG_INFO, "Sample recording started , record %d frames from sample %d and %s",
				args[0],s->sample_record_id, (args[1] == 1 ? "play new sample" : "dont play new sample" ));
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to start sample recorder");
		sample_stop_encoder( v->uc->sample_id );
		result = 0;
		v->settings->sample_record = 0;
		return;
	}   

	if(result == 1)
	{
		v->settings->sample_record = 1;
		v->settings->sample_record_switch = args[1];
	}

	if( v->seq->active )
	{
		int i;
		int start_at = 0;
		for( i = 0; i < MAX_SEQUENCES; i ++ )
		{
			if ( sample_exists( v->seq->samples[i] ))
			{
				start_at = v->seq->samples[i];
				break;	
			}
		}
		if( start_at == v->uc->sample_id )
			veejay_set_frame(v,sample_get_startFrame(v->uc->sample_id));
		else
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE, start_at );	
	}
	else
	{
		veejay_set_frame(v, sample_get_startFrame(v->uc->sample_id));
	}
}

void vj_event_sample_rec_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	
	if( SAMPLE_PLAYING(v)) 
	{
		video_playback_setup *s = v->settings;
		int stop_sample = v->uc->sample_id;

		if(v->seq->active && v->seq->rec_id )
			stop_sample = v->seq->rec_id;

		if( sample_stop_encoder( stop_sample ) == 1 ) 
		{
			char avi_file[255];
			v->settings->sample_record = 0;
			if( sample_get_encoded_file( stop_sample, avi_file) <= 0 )
			{
			 	veejay_msg(VEEJAY_MSG_ERROR, "Unable to append file '%s' to sample %d", avi_file, stop_sample);
			}
			else
			{
				// add to new sample
				int ns = veejay_edit_addmovie_sample(v,avi_file,0 );
				if(ns > 0)
					veejay_msg(VEEJAY_MSG_INFO, "Loaded file '%s' to new sample %d",avi_file, ns);
				if(ns <= 0 )
					veejay_msg(VEEJAY_MSG_ERROR, "Unable to append file %s to EditList!",avi_file);
			
		
				sample_reset_encoder( stop_sample );
				s->sample_record = 0;	
				s->sample_record_id = 0;
				if(v->seq->active && v->seq->rec_id )
					v->seq->rec_id = 0;
				if(s->sample_record_switch) 
				{
					s->sample_record_switch = 0;
					if( ns > 0 )
						veejay_change_playback_mode( v,VJ_PLAYBACK_MODE_SAMPLE, ns );
				}
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample recorder was never started for sample %d",stop_sample);
		}
	}
	else 
	{
		p_invalid_mode();
	}
}


void vj_event_sample_set_num_loops(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(args[0] == 0) args[0] = v->uc->sample_id;
	if(args[0] == -1) args[0] = sample_size()-1;

	if(sample_exists(args[0]))
	{

		if(	sample_set_loops(v->uc->sample_id, args[1]))
		{	veejay_msg(VEEJAY_MSG_INFO, "Setted %d no. of loops for sample %d",
				sample_get_loops(v->uc->sample_id),args[0]);
		}
		else	
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot set %d loops for sample %d",args[1],args[0]);
		}

	}
	else
	{
		p_no_sample(args[0]);
	}
}


void vj_event_sample_rel_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[4];
	//video_playback_setup *s = v->settings;
	char *str = NULL;
	int s_start;
	int s_end;

	P_A(args,str,format,ap);
	if(SAMPLE_PLAYING(v))
	{

		if(args[0] == 0)
			args[0] = v->uc->sample_id;

		if(args[0] == -1) args[0] = sample_size()-1;	

		if(!sample_exists(args[0]))
		{
			p_no_sample(args[0]);
			return;
		}
		
		s_start = sample_get_startFrame(args[0]) + args[1];
		s_end = sample_get_endFrame(args[0]) + args[2];

		if	(sample_set_startframe(args[0],s_start) &&
			sample_set_endframe(args[0],s_end))
		{
			constrain_sample( v, args[0] );
			veejay_msg(VEEJAY_MSG_INFO, "Sample update start %d end %d",
				s_start,s_end);
		}
	}
	else
	{
		p_invalid_mode();
	}

}

void vj_event_sample_set_start(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	int mf;
	video_playback_setup *s = v->settings;
	char *str = NULL;
	P_A(args,str,format,ap);

	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if(args[0] == 0) 
		args[0] = v->uc->sample_id;
	if(args[0] == -1)
		args[0] = sample_size()-1;
	
	if( args[0] <= 0 )
		return;

	if( args[1] < sample_get_endFrame(args[0])) {
		if( sample_set_startframe(args[0],args[1] ) ) {
			veejay_msg(VEEJAY_MSG_INFO, "Sample starting frame updated to frame %d",
			  sample_get_startFrame(args[0]));
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to update sample %d 's starting position to %d",args[0],args[1]);
		}
	}
	else 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d 's starting position %d must be greater than ending position %d.",
			args[0],args[1], sample_get_endFrame(args[0]));
	}
}

void vj_event_sample_set_end(void *ptr, const char format[] , va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	int mf;
	video_playback_setup *s = v->settings;
	char *str = NULL;
	P_A(args,str,format,ap);
	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if(args[0] == 0) 
		args[0] = v->uc->sample_id;
	if(args[1] == -1)
		args[1] = sample_video_length( args[0] );
	if(args[1] <= 0 )
	{
		veejay_msg(0, "Impossible to set ending position %d for sample %d", args[1],args[0] );
		return;
	}
	if( args[1] >= sample_get_startFrame(v->uc->sample_id))
	{
		if(sample_set_endframe(args[0],args[1]))
		{
			constrain_sample( v, args[0] );
	   		veejay_msg(VEEJAY_MSG_INFO,"Sample ending frame updated to frame %d",
		        	sample_get_endFrame(args[0]));
		}
		else
		{
			veejay_msg(0, "Impossible to set ending position %d for sample %d", args[1],args[0] );
		}
	}
	else
	{
		veejay_msg(0, "Ending position must be greater then start position");
	}
}

void vj_event_sample_del(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);
	int deleted_sample = 0;

	if(SAMPLE_PLAYING(v) && v->uc->sample_id == args[0])
	{
		veejay_msg(VEEJAY_MSG_INFO,"Cannot delete sample while playing");
		return;
	}

	if(sample_del(args[0]))
	{
		veejay_msg(VEEJAY_MSG_INFO, "Deleted sample %d", args[0]);
		deleted_sample = args[0];
		int i;
		for( i = 0; i < MAX_SEQUENCES ; i ++ )
			if( v->seq->samples[i] == deleted_sample )
				v->seq->samples[i] = 0;

		sample_verify_delete( args[0] , 0 );
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to delete sample %d",args[0]);
	}
	vj_event_send_new_id(  v, deleted_sample );
}

void vj_event_sample_copy(void *ptr, const char format[] , va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	int new_sample =0;
	P_A(args,s,format,ap);

	if( sample_exists(args[0] ))
	{
		new_sample = sample_copy(args[0]);
		if(!new_sample)
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to copy sample %d.",args[0]);
	}
	vj_event_send_new_id( v, new_sample );
}

void vj_event_sample_clear_all(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if( !SAMPLE_PLAYING(v)) 
	{
		sample_del_all();
		veejay_msg(VEEJAY_MSG_INFO,"Deleted all samples");
	}
	else
	{
		p_invalid_mode();
	}
} 



void vj_event_chain_enable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v))
	{
		sample_set_effect_status(v->uc->sample_id, 1);
	}
	else
	{
		if(STREAM_PLAYING(v))
		{
			vj_tag_set_effect_status(v->uc->sample_id, 1);
		}	
		else
			p_invalid_mode();
	}
	veejay_msg(VEEJAY_MSG_INFO, "Enabled effect chain");
	
}

void	vj_event_stream_set_length( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(STREAM_PLAYING(v))
	{
		if(args[0] > 0 && args[0] < 999999 )
		{
			vj_tag_set_n_frames(v->uc->sample_id, args[0]);
			v->settings->max_frame_num = args[0];
			constrain_stream( v, v->uc->sample_id, (long) args[0]);
		}
		else
		  veejay_msg(VEEJAY_MSG_ERROR, "Ficticious length must be 0 - 999999");
	}
	else
		p_invalid_mode();
}

void vj_event_chain_disable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v) )
	{
		sample_set_effect_status(v->uc->sample_id, 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Sample %d is disabled",v->uc->sample_id);
	}
	else
	{
		if(STREAM_PLAYING(v) )
		{
			vj_tag_set_effect_status(v->uc->sample_id, 0);
			veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Stream %d is enabled",v->uc->sample_id);
		}
		else
			p_invalid_mode();
	}
}

void vj_event_sample_chain_enable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	int args[4];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(!SAMPLE_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	if(args[0] == 0)
	{
		args[0] = v->uc->sample_id;
	}
	
	if(sample_exists(args[0]))
	{
		sample_set_effect_status(args[0], 1);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Sample %d is enabled",args[0]);
	}
	else
		p_no_sample(args[0]);
	
}

void	vj_event_all_samples_chain_toggle(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(SAMPLE_PLAYING(v))
	{
		int i;
		for(i=0; i < sample_size()-1; i++)
			sample_set_effect_status( i, args[0] );
		veejay_msg(VEEJAY_MSG_INFO, "Effect Chain on all samples %s", (args[0]==0 ? "Disabled" : "Enabled"));
	}
	else
	{
		if(STREAM_PLAYING(v))
		{
			int i;
			for(i=0; i < vj_tag_size()-1; i++)
				vj_tag_set_effect_status(i,args[0]);
			veejay_msg(VEEJAY_MSG_INFO, "Effect Chain on all streams %s", (args[0]==0 ? "Disabled" : "Enabled"));
		}
		else
			p_invalid_mode();
	}
}


void vj_event_tag_chain_enable(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[4];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!STREAM_PLAYING(v))
	{
		p_invalid_mode();	
		return;
	}

	if( args[0] == 0 )
		args[0] = v->uc->sample_id;

	if(vj_tag_exists(args[0]))
	{
		vj_tag_set_effect_status(args[0], 1);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is enabled",args[0]);
	}
	else
		p_no_tag(args[0]);

}
void vj_event_tag_chain_disable(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(!STREAM_PLAYING(v))
	{
		p_invalid_mode();	
		return;
	}

	if( args[0] == 0 )
		args[0] = v->uc->sample_id;
	if(vj_tag_exists(args[0]))
	{
		vj_tag_set_effect_status(args[0], 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is disabled",args[0]);
	}
	else
	{
		p_no_tag(args[0]);
	}

}

void vj_event_sample_chain_disable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(args[0] == 0)
	{
		args[0] = v->uc->sample_id;
	}
	
	if(SAMPLE_PLAYING(v) && sample_exists(args[0]))
	{
		sample_set_effect_status(args[0], 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is disabled",args[0]);
	}
	if(STREAM_PLAYING(v) && vj_tag_exists(args[0]))
	{
		vj_tag_set_effect_status(args[0], 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is disabled",args[0]);
	}
	
}


void vj_event_chain_toggle(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v))
	{
		int flag = sample_get_effect_status(v->uc->sample_id);
		if(flag == 0) 
		{
			sample_set_effect_status(v->uc->sample_id,1); 
		}
		else
		{
			sample_set_effect_status(v->uc->sample_id,0);
		}
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain is %s.", (sample_get_effect_status(v->uc->sample_id) ? "enabled" : "disabled"));
	}
	if(STREAM_PLAYING(v))
	{
		int flag = vj_tag_get_effect_status(v->uc->sample_id);
		if(flag == 0) 	
		{
			vj_tag_set_effect_status(v->uc->sample_id,1); 
		}
		else
		{
			vj_tag_set_effect_status(v->uc->sample_id,0);
		}
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain is %s.", (vj_tag_get_effect_status(v->uc->sample_id) ? "enabled" : "disabled"));
	}
}	

void vj_event_chain_entry_video_toggle(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v))
	{
		int c = sample_get_selected_entry(v->uc->sample_id);
		int flag = sample_get_chain_status(v->uc->sample_id,c);
		if(flag == 0)
		{
			sample_set_chain_status(v->uc->sample_id, c,1);	
		}
		else
		{	
			sample_set_chain_status(v->uc->sample_id, c,0);
		}
		veejay_msg(VEEJAY_MSG_INFO, "Video on chain entry %d is %s", c,
			(flag==0 ? "Disabled" : "Enabled"));
	}
	if(STREAM_PLAYING(v))
	{
		int c = vj_tag_get_selected_entry(v->uc->sample_id);
		int flag = vj_tag_get_chain_status( v->uc->sample_id,c);
		if(flag == 0)
		{
			vj_tag_set_chain_status(v->uc->sample_id, c,1);	
		}
		else
		{	
			vj_tag_set_chain_status(v->uc->sample_id, c,0);
		}
		veejay_msg(VEEJAY_MSG_INFO, "Video on chain entry %d is %s", c,
			(flag==0 ? "Disabled" : "Enabled"));

	}
}

void vj_event_chain_entry_enable_video(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];	
	char *s = NULL;
	P_A(args,s,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(sample_exists(args[0]))
		{
			if(sample_set_chain_status(args[0],args[1],1) != -1)	
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d: Video on chain entry %d is %s",args[0],args[1],
					( sample_get_chain_status(args[0],args[1]) == 1 ? "Enabled" : "Disabled"));
			}
		}
		else
			p_no_sample(args[0]);
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0)args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(vj_tag_exists(args[0])) 
		{
			if(vj_tag_set_chain_status(args[0],args[1],1)!=-1)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Stream %d: Video on chain entry %d is %s",args[0],args[1],
					vj_tag_get_chain_status(args[0],args[1]) == 1 ? "Enabled" : "Disabled" );
			}
		}
		else
			p_no_tag(args[0]);
	}
}
void vj_event_chain_entry_disable_video(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);

		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			if(sample_set_chain_status(args[0],args[1],0)!=-1)
			{	
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d: Video on chain entry %d is %s",args[0],args[1],
				( sample_get_chain_status(args[0],args[1])==1 ? "Enabled" : "Disabled"));
			}
		}
		else
			p_no_sample(args[0]);
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;	
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);

		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0]))
		{
			if(vj_tag_set_chain_status(args[0],args[1],0)!=-1)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Stream %d: Video on chain entry %d is %s",args[0],args[1],
					vj_tag_get_chain_status(args[0],args[1]) == 1 ? "Enabled" : "Disabled" );
			}
		}
		else
			p_no_tag(args[0]);
	}

}

void	vj_event_chain_fade_follow(void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if( args[0] == 0 || args[0] == 1 ) {
		vj_perform_follow_fade( args[0] );
	}
}

void	vj_event_manual_chain_fade(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
	{
		args[0] = v->uc->sample_id;
	}

	if( args[1] < 0 || args[1] > 255)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Invalid opacity range %d use [0-255] ", args[1]);
		return;
	}
	args[1] = 255 - args[1];

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
	{
		if( sample_set_manual_fader( args[0], args[1] ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Set chain opacity to %f",
				sample_get_fader_val( args[0] ));
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error setting chain opacity of sample %d to %d", args[0],args[1]);
		}
	}
	if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
	{
		if( vj_tag_set_manual_fader( args[0], args[1] ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Set chain opacity to %f",
				vj_tag_get_fader_val(args[0]));
		}
	}

}

void vj_event_chain_fade_in(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
	{
		args[0] = v->uc->sample_id;
	}

	if( args[1] == 0 ) 
		args[1] = 1; //@forward

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
	{
		if( sample_set_fader_active( args[0], args[1],-1 ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Chain Fade In from sample to full effect chain in %d frames. Per frame %2.4f",
				args[1], sample_get_fader_inc(args[0]));
			if(sample_get_effect_status(args[0]==0))
			{
				sample_set_effect_status(args[0], -1);
			}
		}
	}
	if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
	{
		if( vj_tag_set_fader_active( args[0], args[1],-1 ) )
		{
			veejay_msg(VEEJAY_MSG_INFO,"Chain Fade In from stream to full effect chain in %d frames. Per frame %2.4f",
				args[1], sample_get_fader_inc(args[0]));
			if(vj_tag_get_effect_status(args[0]==0))
			{
				vj_tag_set_effect_status(args[0],-1);
			}
		}
	}

}

void vj_event_chain_fade_out(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
	{
		args[0] = v->uc->sample_id;
	}

	if( args[1] == 0 )
		args[1] = -1;

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
	{
		if( sample_set_fader_active( args[0], args[1],1 ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Chain Fade Out from sample to full effect chain in %d frames. Per frame %2.2f",
				args[1], sample_get_fader_inc(args[0]));
		}
	}
	if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
	{
		if( vj_tag_set_fader_active( args[0], args[1],1 ) )
		{
			veejay_msg(VEEJAY_MSG_INFO,"Chain Fade Out from stream to full effect chain in %d frames. Per frame %2.2f",
				args[1], sample_get_fader_inc(args[0]));
		}
	}
}



void vj_event_chain_clear(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; 
	P_A(args,str,format,ap);

	if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
	{
		args[0] = v->uc->sample_id;
	}

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
	{
		int i;
		for(i=0; i < SAMPLE_MAX_EFFECTS;i++)
		{
			int effect = sample_get_effect_any(args[0],i);
			if(vj_effect_is_valid(effect))
			{
				sample_chain_remove(args[0],i);
				veejay_msg(VEEJAY_MSG_INFO,"Sample %d: Deleted effect %s from entry %d",
					args[0],vj_effect_get_description(effect), i);
			}
		}
		v->uc->chain_changed = 1;
	}
	if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
	{
		int i;
		for(i=0; i < SAMPLE_MAX_EFFECTS;i++)
		{
			int effect = vj_tag_get_effect_any(args[0],i);
			if(vj_effect_is_valid(effect))
			{
				vj_tag_chain_remove(args[0],i);
				veejay_msg(VEEJAY_MSG_INFO,"Stream %d: Deleted effect %s from entry %d",	
					args[0],vj_effect_get_description(effect), i);
			}
		}
		v->uc->chain_changed = 1;
	}


}

void vj_event_chain_entry_del(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int effect = sample_get_effect_any(args[0],args[1]);
			if( vj_effect_is_valid(effect)) 
			{
				sample_chain_remove(args[0],args[1]);
				v->uc->chain_changed = 1;
				veejay_msg(VEEJAY_MSG_INFO,"Sample %d: Deleted effect %s from entry %d",
					args[0],vj_effect_get_description(effect), args[1]);
			}
		}
	}

	if (STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0])) 		
		{
			int effect = vj_tag_get_effect_any(args[0],args[1]);
			if(vj_effect_is_valid(effect))
			{
				vj_tag_chain_remove(args[0],args[1]);
				v->uc->chain_changed = 1;
				veejay_msg(VEEJAY_MSG_INFO,"Stream %d: Deleted effect %s from entry %d",	
					args[0],vj_effect_get_description(effect), args[1]);
			}
		}
	}
}

void vj_event_chain_entry_set_defaults(void *ptr, const char format[], va_list ap) 
{

}

void vj_event_chain_entry_set(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[3];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = sample_size()-1;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);

		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			//int real_id = vj_effect_real_to_sequence(args[2]);
			if(sample_chain_add(args[0],args[1],args[2]) != -1) 
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d chain entry %d has effect %s",
					args[0],args[1],vj_effect_get_description(args[2]));
				v->uc->chain_changed = 1;
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot set effect %d on sample %d chain %d",args[2],args[0],args[1]);
			}
		}
	}
	if( STREAM_PLAYING(v)) 
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = vj_tag_size()-1;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);	

		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0]))
		{
			if(vj_tag_set_effect(args[0],args[1], args[2]) != -1)
			{
			//	veejay_msg(VEEJAY_MSG_INFO, "Stream %d chain entry %d has effect %s",
			//		args[0],args[1],vj_effect_get_description(real_id));
				v->uc->chain_changed = 1;
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot set effect %d on stream %d chain %d",args[2],args[0],args[1]);
			}
		}
	}
}

void vj_event_chain_entry_select(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	if( SAMPLE_PLAYING(v)  )
	{
		if(args[0] >= 0 && args[0] < SAMPLE_MAX_EFFECTS)
		{
			if( sample_set_selected_entry( v->uc->sample_id, args[0])) 
			{
			veejay_msg(VEEJAY_MSG_INFO,"Selected entry %d [%s]",
			  sample_get_selected_entry(v->uc->sample_id), 
			  vj_effect_get_description( 
				sample_get_effect_any(v->uc->sample_id,sample_get_selected_entry(v->uc->sample_id))));
			}
		}
	}
	if ( STREAM_PLAYING(v))
	{
		if(args[0] >= 0 && args[0] < SAMPLE_MAX_EFFECTS)
		{
			if( vj_tag_set_selected_entry(v->uc->sample_id,args[0]))
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected entry %d [%s]",
			 	vj_tag_get_selected_entry(v->uc->sample_id),
				vj_effect_get_description( 
					vj_tag_get_effect_any(v->uc->sample_id,vj_tag_get_selected_entry(v->uc->sample_id))));
			}
		}	
	}
}

void vj_event_entry_up(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(SAMPLE_PLAYING(v) || STREAM_PLAYING(v))
	{
		int effect_id=-1;
		int c=-1;
		if(SAMPLE_PLAYING(v))
		{
			c = sample_get_selected_entry(v->uc->sample_id) + args[0];
			if(c >= SAMPLE_MAX_EFFECTS) c = 0;
			sample_set_selected_entry( v->uc->sample_id, c);
			effect_id = sample_get_effect_any(v->uc->sample_id, c );
		}
		if(STREAM_PLAYING(v))
		{
			c = vj_tag_get_selected_entry(v->uc->sample_id)+args[0];
			if( c>= SAMPLE_MAX_EFFECTS) c = 0;
			vj_tag_set_selected_entry(v->uc->sample_id,c);
			effect_id = vj_tag_get_effect_any(v->uc->sample_id,c);
		}

		veejay_msg(VEEJAY_MSG_INFO, "Entry %d has effect %s",
			c, vj_effect_get_description(effect_id));

	}
}
void vj_event_entry_down(void *ptr, const char format[] ,va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) 
	{
		int effect_id=-1;
		int c = -1;
		
		if(SAMPLE_PLAYING(v))
		{
			c = sample_get_selected_entry( v->uc->sample_id ) - args[0];
			if(c < 0) c = SAMPLE_MAX_EFFECTS-1;
			sample_set_selected_entry( v->uc->sample_id, c);
			effect_id = sample_get_effect_any(v->uc->sample_id, c );
		}
		if(STREAM_PLAYING(v))
		{
			c = vj_tag_get_selected_entry(v->uc->sample_id) - args[0];
			if(c<0) c= SAMPLE_MAX_EFFECTS-1;
			vj_tag_set_selected_entry(v->uc->sample_id,c);
			effect_id = vj_tag_get_effect_any(v->uc->sample_id,c);
		}
		veejay_msg(VEEJAY_MSG_INFO , "Entry %d has effect %s",
			c, vj_effect_get_description(effect_id));
	}
}

void vj_event_chain_entry_preset(void *ptr,const char format[], va_list ap)
{
	int args[16];
	veejay_t *v = (veejay_t*)ptr;
	veejay_memset(args,0,sizeof(int) * 16); 
	//P_A16(args,format,ap);
	char *str = NULL;
	P_A(args,str,format,ap);
	if(SAMPLE_PLAYING(v)) 
	{
	    int num_p = 0;
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int real_id = args[2];
			int i;
			num_p   = vj_effect_get_num_params(real_id);
			
			if(sample_chain_add( args[0],args[1],args[2])!=-1)
			{
				int args_offset = 3;
				
				for(i=0; i < num_p; i++)
				{
					if(vj_effect_valid_value(real_id,i,args[(i+args_offset)]) )
					{

				 		if(sample_set_effect_arg(args[0],args[1],i,args[(i+args_offset)] )==-1)	
						{
							veejay_msg(VEEJAY_MSG_ERROR, "Error setting argument %d value %d for %s",
							i,
							args[(i+args_offset)],
							vj_effect_get_description(real_id));
						}
					}
				}

			/*	if ( vj_effect_get_extra_frame( real_id ))
				{
					int source = args[num_p+3];	
					int channel_id = args[num_p+4];
					int err = 1;
					if( (source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))|| (source == VJ_TAG_TYPE_NONE && sample_exists(channel_id)) )
					{
						err = 0;
					}
					if(	err == 0 && sample_set_chain_source( args[0],args[1], source ) &&
				       		sample_set_chain_channel( args[0],args[1], channel_id  ))
					{
					  veejay_msg(VEEJAY_MSG_INFO, "Updated mixing channel to %s %d",
						(source == VJ_TAG_TYPE_NONE ? "sample" : "stream" ),
						channel_id);
					}
				}*/
			}
		}
	}
	if( STREAM_PLAYING(v)) 
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(v->uc->sample_id)) 
		{
			int real_id = args[2];
			int num_p   = vj_effect_get_num_params(real_id);
			int i;
		
			if(vj_tag_set_effect(args[0],args[1], args[2]) != -1)
			{
				for(i=0; i < num_p; i++) 
				{
					if(vj_effect_valid_value(real_id, i, args[i+3]) )
					{
						if(vj_tag_set_effect_arg(args[0],args[1],i,args[i+3]) == -1)
						{
							veejay_msg(VEEJAY_MSG_ERROR, "setting argument %d value %d for  %s",
								i,
								args[i+3],
								vj_effect_get_description(real_id));
						}
					}
					else
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Parameter %d value %d is invalid for effect %d (%d-%d)",
							i,args[(i+3)], real_id,
							vj_effect_get_min_limit(real_id,i),
							vj_effect_get_max_limit(real_id,i));
					}
				}
				v->uc->chain_changed = 1;
			}
/*
			if( vj_effect_get_extra_frame(real_id) )
			{
				int channel_id = args[num_p + 4];
				int source = args[ num_p + 3];
				int err = 1;

				if( (source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))|| (source == VJ_TAG_TYPE_NONE && sample_exists(channel_id)) )
				{
					err = 0;
				}

				if( err == 0 && vj_tag_set_chain_source( args[0],args[1], source ) &&
				    vj_tag_set_chain_channel( args[0],args[1], channel_id ))
				{
					veejay_msg(VEEJAY_MSG_INFO,"Updated mixing channel to %s %d",
						(source == VJ_TAG_TYPE_NONE ? "sample" : "stream"), channel_id  );
				}
			}*/
		}
	}

}

void vj_event_chain_entry_src_toggle(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v))
	{
		int entry = sample_get_selected_entry(v->uc->sample_id);
		int src = sample_get_chain_source(v->uc->sample_id, entry);
		int cha = sample_get_chain_channel( v->uc->sample_id, entry );
		if(src == 0 ) // source is sample, toggle to stream
		{
			if(!vj_tag_exists(cha))
			{
				cha =vj_tag_size()-1;
				if(cha <= 0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");
					return;
				}
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "Switched from source Sample to Stream");
			//src = vj_tag_get_type(cha);
			src = 1;
		}
		else
		{
			if(!sample_exists(cha))
			{
				cha = sample_size()-1;
				if(cha<=0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
					return;
				}
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "Switched from source Stream to Sample");
			src = 0;
		}
		sample_set_chain_source( v->uc->sample_id, entry, src );
		sample_set_chain_channel(v->uc->sample_id,entry,cha);
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d", entry,(src==VJ_TAG_TYPE_NONE ? "Sample":"Stream"), cha);
		if(v->no_bezerk)
		{
			veejay_set_frame(v, sample_get_startFrame(v->uc->sample_id));
		}

	} 

	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int src = vj_tag_get_chain_source(v->uc->sample_id, entry);
		int cha = vj_tag_get_chain_channel( v->uc->sample_id, entry );
		char description[100];

		if(src == VJ_TAG_TYPE_NONE ) // mix sample, change to stream
		{
			if(!vj_tag_exists(cha))
			{
				cha = vj_tag_size()-1;
				if(cha <= 0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");
					return;
				}
			}
			src = 1;
		}
		else
		{
			if(!sample_exists(cha))
			{
				cha = sample_size()-1;
				if(cha<=0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
					return;
				}
			}
			src = 0;
		}
		vj_tag_set_chain_source( v->uc->sample_id, entry, src );
		vj_tag_set_chain_channel(v->uc->sample_id,entry,cha);

		vj_tag_get_descriptive(cha, description);
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d (%s)", entry,( src == 0 ? "Sample" : "Stream" ), cha,description);
	} 
}

void vj_event_chain_entry_source(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[3];
	char *str = NULL;
	P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int src = args[2];
			int c = sample_get_chain_channel(args[0],args[1]);
			if(src == VJ_TAG_TYPE_NONE)
			{
				if(!sample_exists(c))
				{
					c = sample_size()-1;
					if(c<=0)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "You should create a sample first\n");
						return;
					}
				}
			}
			else
			{
				if(!vj_tag_exists(c) )
				{
					c = vj_tag_size() - 1;
					if(c<=0)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "You should create a stream first (there are none)");
						return;
					}
					src = vj_tag_get_type(c);
				}
			}

			if(c > 0)
			{
			   sample_set_chain_channel(args[0],args[1], c);
			   sample_set_chain_source (args[0],args[1],src);

				veejay_msg(VEEJAY_MSG_INFO, "Mixing with source (%s %d)", 
					src == VJ_TAG_TYPE_NONE ? "sample" : "stream",c);
	//			if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
				if(v->no_bezerk)
				{
					veejay_set_frame(v,
						sample_get_startFrame(v->uc->sample_id));
				}
			}
				
		}
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0]))
		{
			int src = args[2];
			int c = vj_tag_get_chain_channel(args[0],args[1]);

			if(src == VJ_TAG_TYPE_NONE)
			{
				if(!sample_exists(c))
				{
					c = sample_size()-1;
					if(c<=0)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "You should create a sample first\n");
						return;
					}
				}
			}
			else
			{
				if(!vj_tag_exists(c) )
				{
					c = vj_tag_size() - 1;
					if(c<=0)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "You should create a stream first (there are none)");
						return;
					}
					src = vj_tag_get_type(c);
				}
			}

			if(c > 0)
			{
			   vj_tag_set_chain_channel(args[0],args[1], c);
			   vj_tag_set_chain_source (args[0],args[1],src);
				veejay_msg(VEEJAY_MSG_INFO, "Mixing with source (%s %d)", 
					src==VJ_TAG_TYPE_NONE ? "sample" : "stream",c);
	//		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

			}	
		}
	}
}

#define clamp_channel( a, b, c ) ( ( a < b ? c : (a >= c ? b : a )))

void vj_event_chain_entry_channel_dec(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v))
	{ 
		int entry = sample_get_selected_entry(v->uc->sample_id);
		int cha = sample_get_chain_channel(v->uc->sample_id,entry);
		int src = sample_get_chain_source(v->uc->sample_id,entry);
		int old = cha;
		if(src==VJ_TAG_TYPE_NONE)
		{	//decrease sample id
			cha = cha - args[0];
			if( sample_size()-1 <= 0 )
			{
				veejay_msg(0, "No samples to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				sample_size()-1 );

			if( !sample_exists( cha ) )
				cha = old;
		}
		else	
		{
			cha = cha - args[0];
			if( vj_tag_size()-1 <= 0 )
			{
				veejay_msg(0, "No streams to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				vj_tag_size()-1 );

			if( !vj_tag_exists( cha ))
				cha = old;
		}
		sample_set_chain_channel( v->uc->sample_id, entry, cha );
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
				(src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
			 
		if(v->no_bezerk) 
			veejay_set_frame(v , sample_get_startFrame(v->uc->sample_id));
	}
	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
		int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
		int old = cha;	
		char description[100];

		if(src==VJ_TAG_TYPE_NONE)
		{	//decrease sample id
			cha = cha - args[0];
			if( sample_size()-1 <= 0 )
			{
				veejay_msg(0, "No samples to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				sample_size()-1 );
			if( !sample_exists(cha ) )
				cha = old;
		}
		else	
		{
			cha = cha - args[0];
			if( vj_tag_size()-1 <= 0 )
			{
				veejay_msg(0, "No streams to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				vj_tag_size()-1 );
			if(! vj_tag_exists( cha ))
				cha = old;
		}

		vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
		vj_tag_get_descriptive( cha, description);

		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses Stream %d (%s)",entry,cha,description);
//		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
 
	}

}

void vj_event_chain_entry_channel_inc(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v))
	{
		int entry = sample_get_selected_entry(v->uc->sample_id);
		int cha = sample_get_chain_channel(v->uc->sample_id,entry);
		int src = sample_get_chain_source(v->uc->sample_id,entry);
		int old = cha;
		if(src==VJ_TAG_TYPE_NONE)
		{	//decrease sample id
			cha = cha + args[0];
			if( sample_size()-1 <= 0 )
			{
				veejay_msg(0, "No samples to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				sample_size()-1 );
			if( !sample_exists( cha ) )
				cha = old;
		}
		else	
		{
			cha = cha + args[0];
			if( vj_tag_size()-1 <= 0 )
			{
				veejay_msg(0, "No streams to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				vj_tag_size()-1 );
			if( !vj_tag_exists(cha) )
				cha = old;
		}
	
		sample_set_chain_channel( v->uc->sample_id, entry, cha );
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
			(src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
//		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
		if(v->no_bezerk) veejay_set_frame(v,sample_get_startFrame(v->uc->sample_id));	
			 
	}
	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
		int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
		int old   = cha;
		char description[100];

		if(src==0)
		{	//decrease sample id
			cha = cha + args[0];
			if( sample_size()-1 <= 0 )
			{
				veejay_msg(0, "No samples to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				sample_size()-1 );
			if( !sample_exists( cha ) )
				cha = old;
		}
		else	
		{
			cha = cha + args[0];
			if( vj_tag_size()-1 <= 0 )
			{
				veejay_msg(0, "No streams to mix with");
				return;
			}
			clamp_channel(
				cha,
				1,
				vj_tag_size()-1 );
			if( !vj_tag_exists( cha ))
				cha = old;
		}

		vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
		vj_tag_get_descriptive( cha, description);
//		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses Stream %d (%s)",entry,
			vj_tag_get_chain_channel(v->uc->sample_id,entry),description);
	}
}

void vj_event_chain_entry_channel(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[3];
	char *str = NULL; P_A(args,str,format,ap);
	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of bounds: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int src = sample_get_chain_source( args[0],args[1]);
			int err = 1;
			if(src == VJ_TAG_TYPE_NONE && sample_exists(args[2]))
			{
				err = 0;
			}
			if(src != VJ_TAG_TYPE_NONE && vj_tag_exists(args[2]))
			{
				err = 0;
			}	
			if(err == 0 && sample_set_chain_channel(args[0],args[1], args[2])>= 0)	
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d)",
				  (src == VJ_TAG_TYPE_NONE ? "sample" : "stream"),args[2]);
	//			if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
				if(v->no_bezerk) veejay_set_frame(v,sample_get_startFrame(v->uc->sample_id));
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid channel (%s %d) given",
					(src ==VJ_TAG_TYPE_NONE ? "sample" : "stream") , args[2]);
			}
		}
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0]))
		{
			int src = vj_tag_get_chain_source(args[0],args[1]);
			int err = 1;
			if( src == VJ_TAG_TYPE_NONE && sample_exists( args[2]))
				err = 0;
			if( src != VJ_TAG_TYPE_NONE && vj_tag_exists( args[2] ))
				err = 0;

			if( err == 0 && vj_tag_set_chain_channel(args[0],args[1],args[2])>=0)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d)",
				(src==VJ_TAG_TYPE_NONE ? "sample" : "stream"), args[2]);
//				if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid channel (%s %d) given",
					(src ==VJ_TAG_TYPE_NONE ? "sample" : "stream") , args[2]);
			}
		}
	}
}

void vj_event_chain_entry_srccha(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[4];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = sample_size()-1;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int source = args[2];
			int channel_id = args[3];
			int err = 1;
			if( source == VJ_TAG_TYPE_NONE && sample_exists(channel_id))
				err = 0;
			if( source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))
				err = 0;

	
			if(	err == 0 &&
				sample_set_chain_source(args[0],args[1],source)!=-1 &&
				sample_set_chain_channel(args[0],args[1],channel_id) != -1)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d) to mix in",
					(source == VJ_TAG_TYPE_NONE ? "sample" : "stream") , channel_id);
				if( source != VJ_TAG_TYPE_NONE ) {
					int slot = sample_has_cali_fx( args[0]);//@sample
				        if( slot >= 0 )	{
						sample_cali_prepare( args[0],slot,channel_id);
						veejay_msg(VEEJAY_MSG_DEBUG, "Using calibration data of stream %d",channel_id);
					}
				}
				if(v->no_bezerk) veejay_set_frame(v,sample_get_startFrame(v->uc->sample_id));
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid channel (%s %d) given",
					(source ==VJ_TAG_TYPE_NONE ? "sample" : "stream") , args[2]);
			}
		}
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = sample_size()-1;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

	 	if(vj_tag_exists(args[0])) 
		{
			int source = args[2];
			int channel_id = args[3];
			int err = 1;
			if( source == VJ_TAG_TYPE_NONE && sample_exists(channel_id))
				err = 0;
			if( source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))
				err = 0;

			//@ if there is CALI in FX chain,
			//@ call cali_prepare and pass channel id

				
			if(	err == 0 &&
				vj_tag_set_chain_source(args[0],args[1],source)!=-1 &&
				vj_tag_set_chain_channel(args[0],args[1],channel_id) != -1)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d) to mix in",
					(source == VJ_TAG_TYPE_NONE ? "sample" : "stream") , channel_id);
				
				if( source != VJ_TAG_TYPE_NONE ) {
					int slot = vj_tag_has_cali_fx( args[0]);
				        if( slot >= 0 )	{
						vj_tag_cali_prepare( args[0],slot, channel_id);
					}
					
				}
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Invalid channel (%s %d) given",
					(source ==VJ_TAG_TYPE_NONE ? "sample" : "stream") , args[2]);
			}
		}	
	}

}


void vj_event_chain_arg_inc(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v)) 
	{
		int c = sample_get_selected_entry(v->uc->sample_id);
		int effect = sample_get_effect_any(v->uc->sample_id, c);
		int val = sample_get_effect_arg(v->uc->sample_id,c,args[0]);
		if ( vj_effect_is_valid( effect  ) )
		{

			int tval = val + args[1];
			if( tval > vj_effect_get_max_limit( effect,args[0] ) )
				tval = vj_effect_get_min_limit( effect,args[0]);
			else
				if( tval < vj_effect_get_min_limit( effect,args[0] ) )
					tval = vj_effect_get_max_limit( effect,args[0] );
			if(sample_set_effect_arg( v->uc->sample_id, c,args[0],tval)!=-1 )
			{
				veejay_msg(VEEJAY_MSG_INFO,"Set parameter %d value %d",args[0],tval);
			}
		}
	}

	if(STREAM_PLAYING(v)) 
	{
		int c = vj_tag_get_selected_entry(v->uc->sample_id);
		int effect = vj_tag_get_effect_any(v->uc->sample_id, c);
		int val = vj_tag_get_effect_arg(v->uc->sample_id, c, args[0]);

		int tval = val + args[1];

		if( tval > vj_effect_get_max_limit( effect,args[0] ))
			tval = vj_effect_get_min_limit( effect,args[0] );
		else
			if( tval < vj_effect_get_min_limit( effect,args[0] ))
				tval = vj_effect_get_max_limit( effect,args[0] );

	
		if(vj_tag_set_effect_arg(v->uc->sample_id, c, args[0], tval) )
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set parameter %d value %d",args[0], tval );
		}
	}
}

void vj_event_chain_entry_set_arg_val(void *ptr, const char format[], va_list ap)
{
	int args[4];
	veejay_t *v = (veejay_t*)ptr;
	char *str = NULL; P_A(args,str,format,ap);
	
	if(SAMPLE_PLAYING(v)) 
	{
	 	if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = sample_size()-1;
		if(args[1] == -1) args[1] = sample_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(sample_exists(args[0]))
		{
			int effect = sample_get_effect_any( args[0], args[1] );
			if( vj_effect_valid_value(effect,args[2],args[3]) )
			{
				if(sample_set_effect_arg( args[0], args[1], args[2], args[3])) {
				  veejay_msg(VEEJAY_MSG_INFO, "Set parameter %d to %d on Entry %d of Sample %d", args[2], args[3],args[1],args[0]);
				}
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Parameter %d with value %d invalid for Chain Entry %d of Sample %d",
					args[2], args[3], args[1], args[0] );
			}
		} else { veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist", args[0]); }	
	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) args[0] = v->uc->sample_id;
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(v->uc->sample_id);
		if(v_chi(args[1]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
			return;
		}

		if(vj_tag_exists(args[0]))
		{
			int effect = vj_tag_get_effect_any(args[0],args[1] );
			if ( vj_effect_valid_value( effect,args[2],args[3] ) )
			{
				if(vj_tag_set_effect_arg(args[0],args[1],args[2],args[3])) {
					veejay_msg(VEEJAY_MSG_INFO,"Set parameter %d to %d on Entry %d of Stream %d", args[2],args[3],args[2],args[1]);
				}
			}
			else {
				veejay_msg(VEEJAY_MSG_ERROR, "Parameter %d with value %d for Chain Entry %d invalid for Stream %d",
					args[2],args[3], args[1],args[0]);
			}
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR,"Stream %d does not exist", args[0]);
		}
	}
}

void vj_event_el_cut(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if( SAMPLE_PLAYING(v))
	{
		if( !sample_usable_edl( v->uc->sample_id ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "This sample type has no EDL (all frames are identical)");
			return;
		}

		editlist *el = sample_get_editlist( v->uc->sample_id );
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}	
		if( args[0] < 0 || args[0] > el->total_frames || args[1] < 0 || args[1] > el->total_frames)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Frame number out of bounds");
			return;
		}

		if(veejay_edit_cut( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Cut frames %d-%d from sample %d into buffer",args[0],args[1],
				v->uc->sample_id);
		}

		sample_set_startframe( v->uc->sample_id, 0 );
		sample_set_endframe(   v->uc->sample_id, sample_video_length(v->uc->sample_id) );

		constrain_sample( v, v->uc->sample_id );
	}

	if ( STREAM_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot cut frames in this playback mode");
		return;
	}

}

void vj_event_el_copy(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( SAMPLE_PLAYING(v))
	{
                if( !sample_usable_edl( v->uc->sample_id ))
                {
                        veejay_msg(VEEJAY_MSG_ERROR, "This sample type has no EDL (all frames are identical)");
                        return;
                }

		editlist *el = sample_get_editlist( v->uc->sample_id );
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}
		if( args[0] < 0 || args[0] > el->total_frames || args[1] < 0 || args[1] > el->total_frames)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Frame number out of bounds");
			return;
		}
	
		if(veejay_edit_copy( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Copy frames %d-%d from sample %d into buffer",args[0],args[1],
				v->uc->sample_id);
		}

		sample_set_startframe( v->uc->sample_id, 0 );
		sample_set_endframe(   v->uc->sample_id,sample_video_length(v->uc->sample_id));

		constrain_sample( v, v->uc->sample_id );
	}
	if ( STREAM_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot copy frames in this playback mode");
		return;
	}

}

void vj_event_el_del(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( SAMPLE_PLAYING(v))
	{
                if( !sample_usable_edl( v->uc->sample_id ))
                {
                        veejay_msg(VEEJAY_MSG_ERROR, "This sample type has no EDL (all frames are identical)");
                        return;
                }

		editlist *el = sample_get_editlist( v->uc->sample_id );

		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}	
		if( args[0] < 0 || args[0] > el->total_frames || args[1] < 0 || args[1] > el->total_frames)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Frame number out of bounds");
			return;
		}

		if(veejay_edit_delete( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Deleted frames %d-%d from EDL of sample %d",
				v->uc->sample_id,args[0],args[1]);
		}
		sample_set_startframe( v->uc->sample_id, 0 );
		sample_set_endframe(   v->uc->sample_id, sample_video_length(v->uc->sample_id));

		constrain_sample( v, v->uc->sample_id );

	}

	if ( STREAM_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot delete frames in this playback mode");
		return;
	}

}

void vj_event_el_crop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( STREAM_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot delete frames in this playback mode");
		return;
	}

	if(SAMPLE_PLAYING(v))
	{
                if( !sample_usable_edl( v->uc->sample_id ))
                {
                        veejay_msg(VEEJAY_MSG_ERROR, "This sample type has no EDL (all frames are identical)");
                        return;
                }

		editlist *el = sample_get_editlist( v->uc->sample_id);
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL");
			return;
		}

		if( args[0] < 0 || args[0] > el->total_frames || args[1] < 0 || args[1] > el->total_frames)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Frame number out of bounds");
			return;
		}

		if( args[1] <= args[0] )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Crop: start - end (start must be smaller then end)");
			return;
		}
		int s2 =0;
		int s1 = veejay_edit_delete(v,el, 0, args[0]);	
		int res = 0;
		if(s1)
		{
			args[1] -= args[0]; // after deleting the first part, move arg[1]
			s2 = veejay_edit_delete(v, el,args[1], el->total_frames); 
			if(s2)
			{
				veejay_set_frame(v,0);
				veejay_msg(VEEJAY_MSG_INFO, "Delete frames 0- %d , %d - %d from sample %d", 0,args[0],args[1],
					el->total_frames, v->uc->sample_id);
				res = 1;
				sample_set_startframe( v->uc->sample_id, 0 );
				sample_set_endframe(   v->uc->sample_id, sample_video_length(v->uc->sample_id) );
				constrain_sample( v, v->uc->sample_id );
			}

		}
		if(!res)
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid range given to crop ! %d - %d", args[0],args[1] );
		
	}
}

void vj_event_el_paste_at(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	if ( STREAM_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot paste frames in this playback mode");
		return;
	}

	if( SAMPLE_PLAYING(v))
	{
                if( !sample_usable_edl( v->uc->sample_id ))
                {
                        veejay_msg(VEEJAY_MSG_ERROR, "This sample type has no EDL (all frames are identical)");
                        return;
                }

		editlist *el = sample_get_editlist( v->uc->sample_id );
		long length = el->total_frames;
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL");
			return;
		}
		if( args[0] >= 0 && args[0] <= el->total_frames)
		{		
			if( veejay_edit_paste( v, el, args[0] ) ) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Pasted buffer at frame %d",args[0]);
			}
			sample_set_startframe( v->uc->sample_id, 0 );
			sample_set_endframe(   v->uc->sample_id, sample_video_length(v->uc->sample_id));
			constrain_sample( v, v->uc->sample_id );
		}

	}
}

void vj_event_el_save_editlist(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	veejay_memset(str,0,1024);
 	int args[2] = {0,0};
	P_A(args,str,format,ap);
	if( STREAM_PLAYING(v) || PLAIN_PLAYING(v) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Wrong playback mode for saving EDL of sample");
		return;
	}

	if( veejay_save_all(v, str,args[0],args[1]) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Saved EditList as %s",str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to save EditList as %s",str);
	}
}

void vj_event_el_load_editlist(void *ptr, const char format[], va_list ap)
{
	veejay_msg(VEEJAY_MSG_ERROR, "EditList: Load not implemented");
}


void vj_event_el_add_video(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int start = -1;
	int destination = v->current_edit_list->total_frames;
	char str[1024];
	int *args = NULL;
	P_A(args,str,format,ap);

	if(SAMPLE_PLAYING(v))
	{
		if( !sample_usable_edl( v->uc->sample_id ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot append video to a picture sample");
			return;
		}
	}

	if ( veejay_edit_addmovie(v,v->current_edit_list,str,start,destination))
		veejay_msg(VEEJAY_MSG_INFO, "Added video file %s to EditList",str); 
	else
		veejay_msg(VEEJAY_MSG_INFO, "Unable to add file %s to EditList",str); 
}

void vj_event_el_add_video_sample(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	int args[2];
	P_A(args,str,format,ap);

	int new_sample_id = args[0];
	if(new_sample_id == 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Trying to create new sample from %s",
			str );
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Trying to append %s to current sample",
			str );
	}
	new_sample_id = veejay_edit_addmovie_sample(v,str,new_sample_id );
	if(new_sample_id <= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to open %s", str );
		new_sample_id = 0;
	}
	vj_event_send_new_id( v,new_sample_id );
}

void vj_event_tag_del(void *ptr, const char format[] , va_list ap ) 
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	
	if(STREAM_PLAYING(v) && v->uc->sample_id == args[0])
	{
		veejay_msg(VEEJAY_MSG_INFO,"Cannot delete stream while playing");
	}
	else 
	{
		if(vj_tag_exists(args[0]))	
		{
			if(vj_tag_del(args[0]))
			{
				veejay_msg(VEEJAY_MSG_INFO, "Deleted stream %d", args[0]);
				vj_tag_verify_delete( args[0], 1 );
			}
		}	
	}
	vj_event_send_new_id(  v, args[0] );
}

void vj_event_tag_toggle(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);
	if(STREAM_PLAYING(v))
	{
		int active = vj_tag_get_active(v->uc->sample_id);
		vj_tag_set_active( v->uc->sample_id, !active);
		veejay_msg(VEEJAY_MSG_INFO, "Stream is %s", (vj_tag_get_active(v->uc->sample_id) ? "active" : "disabled"));
	}
}

#ifdef USE_GDK_PIXBUF
void vj_event_tag_new_picture(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char str[255];
	int *args = NULL;
	P_A(args,str,format,ap);

	int id = veejay_create_tag(v, VJ_TAG_TYPE_PICTURE, str, v->nstreams,0,0);

	vj_event_send_new_id( v, id );
	if(id <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Picture stream");
}
#endif

#ifdef SUPPORT_READ_DV2
void	vj_event_tag_new_dv1394(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if(args[0] == -1) args[0] = 63;
	veejay_msg(VEEJAY_MSG_DEBUG, "Try channel %d", args[0]);
	int id = veejay_create_tag(v, VJ_TAG_TYPE_DV1394, "/dev/dv1394", v->nstreams,0, args[0]);
	vj_event_send_new_id( v, id );
	if( id <= 0)
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new DV1394 stream");
}
#endif

void	vj_event_v4l_blackframe( void *ptr, const char format[] , va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;

	char *str = NULL;
	int args[3];
	
	P_A(args,str,format,ap);
	
	int id = args[0];
	if( id == 0 ) {
		if( STREAM_PLAYING(v) )
			id = v->uc->sample_id;
	}

	if( id == 0 ) {
		return;
	}

	if( args[1] == 0 ) {
		vj_tag_drop_blackframe(id);
	} else {
		vj_tag_grab_blackframe(id, args[1], args[2],args[3]);
	}
}

void	vj_event_cali_write_file( void *ptr, const char format[], va_list ap)
{
	char str[1024];
	int args[2];
	veejay_t *v = (veejay_t*) ptr;

	P_A(args,str,format,ap);

	int id = args[0];
	if( id == 0 ) {
		if(STREAM_PLAYING(v))
			id = v->uc->sample_id;
	}
	if( vj_tag_exists( id ) ){
		if(vj_tag_cali_write_file( id, str, v->current_edit_list )) {
			veejay_msg(VEEJAY_MSG_INFO, "Saved calibration file to %s", str );
		} 
	}
	else {
	  p_no_tag(id);
	}
}

void	vj_event_stream_new_cali( void *ptr, const char format[], va_list ap)
{
	char str[1024];
	int args[2];
	veejay_t *v = (veejay_t*) ptr;

	P_A(args,str,format,ap);


	int id = veejay_create_tag(
			v, VJ_TAG_TYPE_CALI, 
			str, 
			v->nstreams, 
			0,0);
	if(id > 0 )
		v->nstreams++;

	vj_event_send_new_id( v, id );

	if( id <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create load calibration file '%s'",str);
	else	
		veejay_msg(VEEJAY_MSG_INFO, "Loaded calibration file to Stream %d",id );


}

void vj_event_tag_new_v4l(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char *str = NULL;
	int args[2];
	char filename[255];
	P_A(args,str,format,ap);

	sprintf(filename, "video%d", args[0]);

	int id = vj_tag_new(VJ_TAG_TYPE_V4L,
			    filename,
			    v->nstreams,
			    v->edit_list,
			    v->pixel_format,
			    args[1],
			    args[0],
			    v->settings->composite );

	if(id > 0 )
		v->nstreams++;

	vj_event_send_new_id( v, id );
	if( id <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Video4Linux stream ");
}

void vj_event_tag_new_net(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;

	char str[255];
	int args[2];

	P_A(args,str,format,ap);

	if( strncasecmp( str, "localhost",9 ) == 0 )
	{
		if( args[0] == v->uc->port )
		{	
			veejay_msg(0, "Try another port number, I am listening on this one.");
			return;
		}
	}
	
	int id = veejay_create_tag(v, VJ_TAG_TYPE_NET, str, v->nstreams, args[0],0);
	vj_event_send_new_id( v, id);

	if(id <= 0)
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to create unicast stream");
}

void vj_event_tag_new_mcast(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;

	char str[255];
	int args[3];

	P_A(args,str,format,ap);

	int id = veejay_create_tag(v, VJ_TAG_TYPE_MCAST, str, v->nstreams, args[0],0);

	vj_event_send_new_id( v, id  );

	if( id <= 0)
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new multicast stream");

}



void vj_event_tag_new_color(void *ptr, const char format[], va_list ap)
{
	veejay_t *v= (veejay_t*) ptr;
	char *str=NULL;
	int args[4];
	P_A(args,str,format,ap);

	int i;
	for(i = 0 ; i < 3; i ++ )
		CLAMPVAL( args[i] );

	
	int id =  vj_tag_new( VJ_TAG_TYPE_COLOR, NULL, -1, v->edit_list,v->pixel_format, -1,0 , v->settings->composite);
	if(id > 0)
	{
		vj_tag_set_stream_color( id, args[0],args[1],args[2] );
	}	

	vj_event_send_new_id( v , id );
	if( id <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new solid color stream");

}

void vj_event_tag_new_y4m(void *ptr, const char format[], va_list ap)
{
	veejay_t *v= (veejay_t*) ptr;
	char str[255];
	int *args = NULL;
	P_A(args,str,format,ap);
	int id  = veejay_create_tag(v, VJ_TAG_TYPE_YUV4MPEG, str, v->nstreams,0,0);

	vj_event_send_new_id( v, id );
	if( id <= 0 )
		veejay_msg(VEEJAY_MSG_INFO, "Unable to create new Yuv4mpeg stream");
}
void vj_event_v4l_set_brightness(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0]==0) args[0] = v->uc->sample_id;
	if(args[0]==-1) args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
		if(vj_tag_set_brightness(args[0],args[1]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set brightness to %d",args[1]);
		}
	}
	
}

void	vj_event_vp_proj_toggle(void *ptr, const char format[],va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;

	if(!v->composite ) {
		veejay_msg(0, "No viewport active.");
		return;
	}

	int mode = !composite_get_status(v->composite);
	composite_set_status( v->composite, mode );

	veejay_msg(VEEJAY_MSG_INFO, "Projection transform is now %s",
			(mode==0? "inactive" : "active"));
}

void	vj_event_vp_stack( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	if(!v->composite ) {
		veejay_msg(0, "No viewport active.");
		return;
	}

	if( args[0] == 1 )
	{
	/*	int cs = composite_get_colormode(v->composite);
		if(cs == 0 )
			cs = 1;
		else 
			cs = 0;	
		composite_set_colormode( v->composite, cs );
		veejay_msg(VEEJAY_MSG_INFO ,"Secundary Input renders in %s", (cs == 1 ?"Grayscale" : "Color" ) );
		return;*/
	}

	if ( args[1] == 1 ) {
		if(v->settings->composite == 1 ) 
			v->settings->composite = 2;
		else if (v->settings->composite == 2 )
			v->settings->composite = 1;
		veejay_msg(VEEJAY_MSG_INFO, "Focus on %s, press CTRL-h for more help.", (v->settings->composite == 1 ? "Projection" : "Secundary Input"));
		if( SAMPLE_PLAYING(v) ) {

		/*	sample_reload_config( v->composite, v->uc->sample_id,v->settings->composite );

			if(v->settings->composite == 2 && sample_get_composite(v->uc->sample_id ) == 0 )
			{
				sample_set_composite( v->composite, v->uc->sample_id, 2  );
				void *cur = sample_get_composite_view(v->uc->sample_id);
				if(cur==NULL) {
					cur = composite_clone( v->composite );
				}
				composite_set_backing(v->composite,cur );
				veejay_msg(0, "Saved calibration to current sample");
			}*/
			sample_set_composite( v->composite, v->uc->sample_id, v->settings->composite  );

			veejay_msg(VEEJAY_MSG_INFO,
				"Secundary input sample %d will %s.", v->uc->sample_id,
				(v->settings->composite == 2 ? "be transformed" : "not be transformed" ) );
		} else if (STREAM_PLAYING(v)) {
			
		/*	vj_tag_reload_config( v->composite, v->uc->sample_id,v->settings->composite );

			if(v->settings->composite == 2 && vj_tag_get_composite(v->uc->sample_id) == 0 )
			{
				vj_tag_set_composite( v->composite, v->uc->sample_id, 2  );
				void *cur = vj_tag_get_composite_view(v->uc->sample_id);
				if(cur==NULL) {
					cur = composite_clone( v->composite );
				}
				composite_set_backing(v->composite,cur );
				
				veejay_msg(0, "Saved calibration to current sample");
			}*/

			vj_tag_set_composite( v->composite, v->uc->sample_id, v->settings->composite  );

			veejay_msg(VEEJAY_MSG_INFO,
				"Secundary input stream %d will %s.", v->uc->sample_id,
				(v->settings->composite == 2 ? "be transformed" : "not be transformed" ) );

		}
	} 

}
void	vj_event_vp_get_points( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	char msg[280];
	char message[256];
	
	if( args[0] == 0 || !v->composite) {
		snprintf(message,256,"%d %d %d %d %d %d %d %d",
			0,0,0,0,0,0,0,0);
		FORMAT_MSG(msg,message);
		SEND_MSG(v,msg);
		return;
	}	

//	int *r = viewport_event_get_projection(  composite_get_vp( v->composite ),args[0] );
	int r[8];
	memset(r,0,sizeof(r));	
	snprintf(message,256, "%d %d %d %d %d %d %d %d",
		r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7] );

	FORMAT_MSG(msg,message);
	SEND_MSG(v,msg);
}

void	vj_event_vp_set_points( void *ptr, const char format[], va_list ap )
{
	int args[4];
	veejay_t *v = (veejay_t*)ptr;
	veejay_memset(args,0,sizeof(args)); 
	char *str = NULL;
	P_A(args,str,format,ap);

	if(!v->composite ) {
		veejay_msg(0, "No viewport active.");
		return;
	}

	if( args[0] <= 0 || args[0] > 4 ) {
		veejay_msg(0, "Invalid point number. Use 1 - 4");
		return;
	}
	if( args[1] < 0 ) {
		veejay_msg(0, "Scale must be a positive number.");
		return;
	}
	float point_x =  ( (float) args[2] / (float) args[1] );
	float point_y =  ( (float) args[3] / (float) args[1] );

	video_playback_setup *settings = v->settings;
	v->settings->cx = point_x;
	v->settings->cy = point_y;
	v->settings->cn = args[0];
	v->settings->ca = 1;

}

// 159, 164 for white
void	vj_event_v4l_get_info(void *ptr, const char format[] , va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0]==0) args[0] = v->uc->sample_id;
	if(args[0]==-1) args[0] = vj_tag_size()-1;

	char send_msg[33];
	char message[30];
	veejay_memset(send_msg, 0,sizeof(send_msg));
	veejay_memset(message, 0,sizeof(message));

	sprintf( send_msg, "000" );

	if(vj_tag_exists(args[0]))
	{
		int values[6];
		memset(values,0,6*sizeof(int));
		if(vj_tag_get_v4l_properties( args[0], &values[0], &values[1], &values[2], &values[3],	&values[4]))
		{
			sprintf(message, "%05d%05d%05d%05d%05d%05d",
				values[0],values[1],values[2],values[3],values[4],values[5] );
			FORMAT_MSG(send_msg, message);
		}
	}

	SEND_MSG( v,send_msg );
}

void vj_event_v4l_set_contrast(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0]==0) args[0] = v->uc->sample_id;
	if(args[0]==-1)args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
		if(vj_tag_set_contrast(args[0],args[1]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set contrast to %d",args[1]);
		}
	}
}

void vj_event_v4l_set_white(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0]==0) args[0] = v->uc->sample_id;
	if(args[0]==-1)args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
		if(vj_tag_set_white(args[0],args[1]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set whiteness to %d",args[1]);
		}
	}

}
void vj_event_v4l_set_saturation(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0]==0) args[0] = v->uc->sample_id;
	if(args[0]==-1)args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
veejay_msg(0, "broken");
	}

}
void vj_event_v4l_set_color(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0] == 0) args[0] = v->uc->sample_id;
	if(args[0] == -1) args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
		if(vj_tag_set_color(args[0],args[1]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set color to %d",args[1]);
		}
	}

}
void vj_event_v4l_set_hue(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	unsigned char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0] == 0) args[0] = v->uc->sample_id;
	if(args[0] == -1) args[0] = vj_tag_size()-1;
	if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
	{
		if(vj_tag_set_hue(args[0],args[1]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Set hue to %d",args[1]);
		}
	}

}
void	vj_event_viewport_frontback(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(!v->composite) {
		veejay_msg(VEEJAY_MSG_ERROR, "No viewport active.");
		return;
	}

	if( v->settings->composite && composite_get_ui( v->composite ) ) {
		if(v->use_osd==3) 
			v->use_osd = 0;
		v->settings->composite = 2;
		if(STREAM_PLAYING(v)) {
			void *cur = vj_tag_get_composite_view(v->uc->sample_id);
			if(cur == NULL ) {
				cur = (void*)composite_clone(v->composite);
				vj_tag_set_composite_view(v->uc->sample_id,cur);
			}
			vj_tag_reload_config(v->composite,v->uc->sample_id,v->settings->composite );
			veejay_msg(VEEJAY_MSG_INFO, "Saved calibration to stream %d",v->uc->sample_id);
		} else if (SAMPLE_PLAYING(v)) {
			void *cur = sample_get_composite_view( v->uc->sample_id );
			if( cur == NULL ) {
				cur = composite_clone(v->composite);
				sample_set_composite_view(v->uc->sample_id, cur );
			}
                       	sample_reload_config( v->composite,v->uc->sample_id, v->settings->composite);
			veejay_msg(VEEJAY_MSG_INFO, "Saved calibration to sample %d",v->uc->sample_id );
               }
	       composite_set_ui(v->composite, 0 );
	       if(v->video_out==0 || v->video_out == 2)
	 	      vj_sdl_grab( v->sdl[0], 0 );
	}
	else {
		composite_set_ui( v->composite, 1 );
		v->settings->composite = 1;
		v->use_osd=3;
		if(v->video_out==0 || v->video_out == 2)
			vj_sdl_grab( v->sdl[0], 1 );

		veejay_msg(VEEJAY_MSG_INFO, "You can now calibrate your projection/camera, press CTRL-s again to exit.");
	}
}

void	vj_event_toggle_osd( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->use_osd == 0 )
	{
		v->use_osd = 1;
		veejay_msg(VEEJAY_MSG_INFO, "OSD on");
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "OSD off");
		v->use_osd = 0;
	}
}
void	vj_event_toggle_copyright( void *ptr, const char format[], va_list ap )
{
	static int old_osd = -1;
	veejay_t *v = (veejay_t*) ptr;
	if( old_osd == -1 )
		old_osd = v->use_osd;
	if(v->use_osd == 0 || v->use_osd == 1)
		v->use_osd = 2;
	else
		v->use_osd = (old_osd==-1?0: old_osd);
}
void	vj_event_toggle_osd_extra( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->use_osd == 3 )
	  v->use_osd = 0;
	else
	{
	  v->use_osd = 3;
	  veejay_msg(VEEJAY_MSG_INFO, "Not displaying viewport help");
	}
}

static struct {
	char *name;
	int   id;
} recorder_formats[] = {
	{ "mlzo", ENCODER_LZO },
	{ "y4m422", ENCODER_YUV4MPEG },
	{ "y4m420", ENCODER_YUV4MPEG420 },
	{ "yv16", ENCODER_YUV422 },
	{ "y422", ENCODER_YUV422 },
	{ "i420", ENCODER_YUV420 },
	{ "y420", ENCODER_YUV420 },
	{ "div3", ENCODER_DIVX   },
	{ "mpeg4", ENCODER_MPEG4 },
#ifdef SUPPORT_READ_DV2
	{ "dvvideo", ENCODER_DVVIDEO },
#endif
	{ "dvsd", ENCODER_DVVIDEO },
	{ "mjpeg", ENCODER_MJPEG },
	{ "mjpeg-b", ENCODER_MJPEGB },
	{ "mjpegb", ENCODER_MJPEGB },
	{ "ljpeg", ENCODER_LJPEG },
#ifdef HAVE_LIBQUICKTIME
	{ "quicktime-mjpeg", ENCODER_QUICKTIME_MJPEG },
#ifdef SUPPORT_READ_DV2
	{ "quicktime-dv", ENCODER_QUICKTIME_DV },
#endif
#endif
	{ "vj20", ENCODER_YUV420F },
	{ "vj22", ENCODER_YUV422F },
	{ NULL , -1 }
};

void vj_event_tag_set_format(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char str[255]; 
	veejay_memset(str,0,255);
	P_A(args,str,format,ap);

	if(v->settings->tag_record || v->settings->offline_record)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot change data format while recording to disk");
		return;
	}

	int i;
	if( strncasecmp(str, "list", 4 ) == 0 || strncasecmp( str, "help",4) == 0 ) {
		for(i = 0; recorder_formats[i].name != NULL ; i ++ ) {
			veejay_msg(VEEJAY_MSG_INFO,"%s", recorder_formats[i].name );
		}
		return;
	}


	for( i = 0; recorder_formats[i].name != NULL ; i ++ ) {
		if(strncasecmp( str, recorder_formats[i].name, strlen(recorder_formats[i].name) ) == 0 ) {
			_recorder_format = recorder_formats[i].id;
		}
	}


	if( strncasecmp(str, "yuv", 3 ) == 0 || strncasecmp(str, "intern", 6 ) == 0) {
		switch(v->pixel_format) {
			case FMT_422F: _recorder_format = ENCODER_YUV422F; break;
			case FMT_422 : _recorder_format = ENCODER_YUV422; break;
		}
	}

	if(strncasecmp(str,"dvvideo",7)==0||strncasecmp(str,"dvsd",4)==0)
	{
		if(vj_el_is_dv(v->current_edit_list)) {
			_recorder_format = ENCODER_DVVIDEO;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Not working in a valid DV resolution");
		}
		return;
	}

#ifdef HAVE_LIBQUICKTIME
	if(strncasecmp(str,"quicktime-dv", 12 ) == 0 )
	{
		if( vj_el_is_dv( v->current_edit_list ))
		{
			_recorder_format = ENCODER_QUICKTIME_DV;
			veejay_msg(VEEJAY_MSG_INFO, "Recorder writes in QT DV format");
		}
		else
			veejay_msg(VEEJAY_MSG_ERROR, "Not working in valid DV resolution");
	}
#endif

	veejay_msg(VEEJAY_MSG_INFO,
			"Recording in %s" , vj_avcodec_get_encoder_name( _recorder_format ) );
}

static void _vj_event_tag_record( veejay_t *v , int *args, char *str )
{
	if(!STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	char tmp[255];
	char prefix[255];
	if(args[0] <= 0) 
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Number of frames to record must be > 0");
		return;
	}

	if(args[1] < 0 || args[1] > 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Auto play is either on or off");
		return;
	}	

	char sourcename[255];	
	veejay_memset(sourcename,0,255);
	vj_tag_get_description( v->uc->sample_id, sourcename );
	sprintf(prefix,"%s-%02d-", sourcename, v->uc->sample_id);
	if(! veejay_create_temp_file(prefix, tmp )) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot create temporary file %s", tmp);
		return;
	}

	int format = _recorder_format;
	if(_recorder_format == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Set a destination format first");
		return;
	}

	if(args[0] <= 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to record less then 2 frames");
		return;
	}

	if( vj_tag_init_encoder( v->uc->sample_id, tmp, format,		
			args[0]) <= 0 ) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "Error trying to start recording from stream %d", v->uc->sample_id);
		vj_tag_stop_encoder(v->uc->sample_id);
		v->settings->tag_record = 0;
		return;
	} 

	if(v->use_osd)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Turned off OSD, recording now");
		v->use_osd = 0;
	}
	
	if(args[1]==0) 
		v->settings->tag_record_switch = 0;
	else
		v->settings->tag_record_switch = 1;

	v->settings->tag_record = 1;
}

void vj_event_tag_rec_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; 
	P_A(args,str,format,ap);

	_vj_event_tag_record( v, args, str );
}

void vj_event_tag_rec_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t *)ptr;
	video_playback_setup *s = v->settings;

	if( STREAM_PLAYING(v)  && v->settings->tag_record) 
	{
		int play_now = s->tag_record_switch;
		if(!vj_tag_stop_encoder( v->uc->sample_id))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Wasnt recording anyway");
			return;
		}
		
		char avi_file[255];
		if( !vj_tag_get_encoded_file(v->uc->sample_id, avi_file)) 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Dont know where I put the file?!");
			return;
		}	

		// create new sample 
		int ns = veejay_edit_addmovie_sample( v,avi_file, 0 );
		if(ns > 0)
		{
			int len = vj_tag_get_encoded_frames(v->uc->sample_id) - 1;
			veejay_msg(VEEJAY_MSG_INFO,"Added file %s (%d frames) to EditList as sample %d",
				avi_file, len ,ns);	
		}		
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot add videofile %s to EditList!",avi_file);
		}

		veejay_msg(VEEJAY_MSG_ERROR, "Stopped recording from stream %d", v->uc->sample_id);
		vj_tag_reset_encoder( v->uc->sample_id);
		s->tag_record = 0;
		s->tag_record_switch = 0;

		if(play_now) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d now", sample_size()-1);
			veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_SAMPLE, sample_size()-1 );
		}
	}
	else
	{
		if(v->settings->offline_record)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Perhaps you want to stop recording from a non visible stream ? See VIMS id %d",
				VIMS_STREAM_OFFLINE_REC_STOP);
		}
		veejay_msg(VEEJAY_MSG_ERROR, "Not recording from visible stream");
	}
}

void vj_event_tag_rec_offline_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[3];
	char *str = NULL; P_A(args,str,format,ap);

	if( v->settings->offline_record )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Already recording from stream %d", v->settings->offline_tag_id);
		return;
	}
 	if( v->settings->tag_record)
        {
		veejay_msg(VEEJAY_MSG_ERROR ,"Please stop the stream recorder first");
		return;
	}

	if( STREAM_PLAYING(v) && (args[0] == v->uc->sample_id) )
	{
		veejay_msg(VEEJAY_MSG_INFO,"Using stream recorder for stream  %d (is playing)",args[0]);
		_vj_event_tag_record(v, args+1, str);
		return;
	}


	if( vj_tag_exists(args[0]))
	{
		char tmp[255];
		
		int format = _recorder_format;
		char prefix[40];
		sprintf(prefix, "stream-%02d", args[0]);

		if(!veejay_create_temp_file(prefix, tmp ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error creating temporary file %s", tmp);
			return;
		}

		if(format==-1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Set a destination format first");
			return;
		}
	
		if( vj_tag_init_encoder( args[0], tmp, format,		
			args[1]) ) 
		{
			video_playback_setup *s = v->settings;
			veejay_msg(VEEJAY_MSG_INFO, "(Offline) recording from stream %d", args[0]);
			s->offline_record = 1;
			s->offline_tag_id = args[0];
			s->offline_created_sample = args[2];
		} 
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(Offline) error starting recording stream %d",args[0]);
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist",args[0]);
	}
}

void vj_event_tag_rec_offline_stop(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	video_playback_setup *s = v->settings;
	if(s->offline_record) 
	{
		if( vj_tag_stop_encoder( s->offline_tag_id ) == 0 )
		{
			char avi_file[255];

			if( vj_tag_get_encoded_file(v->uc->sample_id, avi_file)!=0) return;
		
			// create new sample	
			int ns = veejay_edit_addmovie_sample(v,avi_file,0);

			if(ns)
			{
				if( vj_tag_get_encoded_frames(v->uc->sample_id) > 0)
					veejay_msg(VEEJAY_MSG_INFO, "Created new sample %d from file %s",
							ns,avi_file);
			}		
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot add videofile %s to EditList!",avi_file);
			}

			vj_tag_reset_encoder( v->uc->sample_id);

			if(s->offline_created_sample) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Playing new sample %d now ", sample_size()-1);
				veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE , sample_size()-1);
			}
		}
		s->offline_record = 0;
		s->offline_tag_id = 0;
		s->offline_created_sample = 0;
	}
}


void vj_event_output_y4m_start(void *ptr, const char format[], va_list ap)
{
	veejay_msg(0, "Y4M out stream: obsolete - use recorder.");
}

void vj_event_output_y4m_stop(void *ptr, const char format[], va_list ap)
{
	veejay_msg(0, "Y4M out stream: obsolete - use recorder.");
}

void vj_event_enable_audio(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
#ifdef HAVE_JACK
	if (!v->audio_running )
	{
		veejay_msg(0,"Veejay was started without audio.");
		return;
	}

	if( v->audio == NO_AUDIO  )
	{
		vj_jack_enable();
		v->audio = AUDIO_PLAY;
	}
#endif	
}

void vj_event_disable_audio(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
#ifdef HAVE_JACK
	if (!v->audio_running )
	{
		veejay_msg(0,"Veejay was started without audio.");
		return;
	}

	if( v->audio != NO_AUDIO )
	{
		vj_jack_disable();
		v->audio = NO_AUDIO;
		vj_jack_reset();
	}
#endif
}


void vj_event_effect_inc(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int real_id;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);	
	if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}
	v->uc->key_effect += args[0];
	if(v->uc->key_effect >= vj_effect_max_effects()) v->uc->key_effect = 1;

	real_id = vj_effect_get_real_id(v->uc->key_effect);

	veejay_msg(VEEJAY_MSG_INFO, "Selected %s Effect %s (%d)", 
		(vj_effect_get_extra_frame(real_id)==1 ? "Video" : "Image"),
		vj_effect_get_description(real_id),
		real_id);
}

void vj_event_effect_dec(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int real_id;
	int args[1];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v))
	{
		p_invalid_mode();
		return;
	}

	v->uc->key_effect -= args[0];
	if(v->uc->key_effect <= 0) v->uc->key_effect = vj_effect_max_effects()-1;
	
	real_id = vj_effect_get_real_id(v->uc->key_effect);
	veejay_msg(VEEJAY_MSG_INFO, "Selected %s Effect %s (%d)",
		(vj_effect_get_extra_frame(real_id) == 1 ? "Video" : "Image"), 
		vj_effect_get_description(real_id),
		real_id);	
}
void vj_event_effect_add(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(SAMPLE_PLAYING(v)) 
	{	
		int c = sample_get_selected_entry(v->uc->sample_id);
		if ( sample_chain_add( v->uc->sample_id, c, 
				       vj_effect_get_real_id(v->uc->key_effect)) != 1)
		{
			int real_id = vj_effect_get_real_id(v->uc->key_effect);
			veejay_msg(VEEJAY_MSG_INFO,"Added Effect %s on chain entry %d",
				vj_effect_get_description(real_id),
				c
			);
			if(v->no_bezerk && vj_effect_get_extra_frame(real_id) ) 
			{
				//veejay_set_sample(v,v->uc->sample_id);
				//
				int nf = sample_get_startFrame( v->uc->sample_id );
				veejay_set_frame(v,nf );
			}
			v->uc->chain_changed = 1;
		}
	}
	if(STREAM_PLAYING(v))
	{
		int c = vj_tag_get_selected_entry(v->uc->sample_id);
		if ( vj_tag_set_effect( v->uc->sample_id, c,
				vj_effect_get_real_id( v->uc->key_effect) ) != -1) 
		{
			int real_id = vj_effect_get_real_id(v->uc->key_effect);
			veejay_msg(VEEJAY_MSG_INFO,"Added Effect %s on chain entry %d",
				vj_effect_get_description(real_id),
				c
			);
//			if(v->no_bezerk && vj_effect_get_extra_frame(real_id)) veejay_set_sample(v,v->uc->sample_id);
			v->uc->chain_changed = 1;
		}
	}

}

void vj_event_misc_start_rec_auto(void *ptr, const char format[], va_list ap)
{
 
}
void vj_event_misc_start_rec(void *ptr, const char format[], va_list ap)
{

}
void vj_event_misc_stop_rec(void *ptr, const char format[], va_list ap)
{

}

void vj_event_select_id(void *ptr, const char format[], va_list ap)
{
	veejay_t *v=  (veejay_t*)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str, format, ap);
	if(!STREAM_PLAYING(v))
	{ 
		int sample_id = (v->uc->sample_key*12)-12 + args[0];
		if(sample_exists(sample_id))
		{
			veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_SAMPLE, sample_id);
			vj_event_print_sample_info(v,sample_id);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Selected sample %d does not exist",sample_id);
		}
	}	
	else
	{
		int sample_id = (v->uc->sample_key*12)-12 + args[0];
		if(vj_tag_exists(sample_id ))
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG ,sample_id);

		}
		else
		{
			veejay_msg(VEEJAY_MSG_INFO,"Selected stream %d does not exist",sample_id);
		}
	}

}

void	vj_event_select_macro( void *ptr, const char format[], va_list ap )
{
	int args[2];
	char *str = NULL;
	P_A( args, str, format, ap );
	macro_select( args[0] );
	veejay_msg(VEEJAY_MSG_INFO, "Changed macro slot to %d", current_macro_ );
}

void vj_event_select_bank(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v =(veejay_t*) ptr;
	int args[1];

	char *str = NULL; P_A(args,str,format,ap);
	if(args[0] >= 1 && args[0] <= 9)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Selected bank %d (active sample range is now %d-%d)",args[0],
			(12 * args[0]) - 12 , (12 * args[0]));
		v->uc->sample_key = args[0];
	}
}

void vj_event_print_tag_info(veejay_t *v, int id) 
{
	int i, y, j, value;
	char description[100];
	char source[150];
	char title[150];
	vj_tag_get_descriptive(id,description);
	vj_tag_get_description(id, title);
	vj_tag_get_source_name(id, source);

	if(v->settings->tag_record)
		veejay_msg(VEEJAY_MSG_INFO, "Stream '%s' [%d]/[%d] [%s] %s recorded: %06ld frames ",
			title,id,vj_tag_size()-1,description,
		(vj_tag_get_active(id) ? "is active" : "is not active"),
		vj_tag_get_encoded_frames(id));
	else
	veejay_msg(VEEJAY_MSG_INFO,
		"Stream [%d]/[%d] [%s] %s ",
		id, vj_tag_size()-1, description,
		(vj_tag_get_active(id) == 1 ? "is active" : "is not active"));

	if( vj_tag_get_composite( id ) ) 
		veejay_msg(VEEJAY_MSG_INFO, "This tag is transformed when used as secundary input.");

	veejay_msg(VEEJAY_MSG_INFO,  "|-----------------------------------|");	
	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		y = vj_tag_get_effect_any(id, i);
		if (y != -1) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "%02d  [%d] [%s] %s (%s)",
				i,
				y,
				vj_tag_get_chain_status(id,i) ? "on" : "off", vj_effect_get_description(y),
				(vj_effect_get_subformat(y) == 1 ? "2x2" : "1x1")
			);

			for (j = 0; j < vj_effect_get_num_params(y); j++)
			{
				value = vj_tag_get_effect_arg(id, i, j);
				if (j == 0)
				{
		    			veejay_msg(VEEJAY_MSG_PRINT, "          [%04d]", value);
				}
				else
				{
		    			veejay_msg(VEEJAY_MSG_PRINT, " [%04d]",value);
				}
				
	    		}
	    		veejay_msg(VEEJAY_MSG_PRINT, "\n");

			if (vj_effect_get_extra_frame(y) == 1)
			{
				int source = vj_tag_get_chain_source(id, i);
				veejay_msg(VEEJAY_MSG_INFO, "     V %s [%d]",(source == VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),
			    		vj_tag_get_chain_channel(id,i)
					);
				//veejay_msg(VEEJAY_MSG_INFO, "     A: %s",   vj_tag_get_chain_audio(id, i) ? "yes" : "no");
	    		}

	    		veejay_msg(VEEJAY_MSG_PRINT, "\n");
		}
    	}
}

void vj_event_create_effect_bundle(veejay_t * v, char *buf, int key_id, int key_mod )
{
	char blob[50 * SAMPLE_MAX_EFFECTS];
	char prefix[20];
	int i ,y,j;
	int num_cmd = 0;
	int id = v->uc->sample_id;
	int event_id = 0;
	int bunlen=0;
	veejay_memset(prefix,0,20);
	veejay_memset(blob,0,50*SAMPLE_MAX_EFFECTS );
   
	if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot take snapshot of Effect Chain");
		return;
	}

 	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		y = (SAMPLE_PLAYING(v) ? sample_get_effect_any(id, i) : vj_tag_get_effect_any(id,i) );
		if (y != -1)
		{
			num_cmd++;
		}
	}
	if(num_cmd < 0) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Effect Chain is empty." );           
		return;
	}

	for (i=0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		y = (SAMPLE_PLAYING(v) ? sample_get_effect_any(id, i) : vj_tag_get_effect_any(id,i) );
		if( y != -1)
		{
			//int entry = i;
			int effect_id = y;
			if(effect_id != -1)
			{
				char bundle[200];
				int np = vj_effect_get_num_params(y);
				veejay_memset(bundle,0,200);
				sprintf(bundle, "%03d:0 %d %d", VIMS_CHAIN_ENTRY_SET_PRESET,i, effect_id );
		    		for (j = 0; j < np; j++)
				{
					char svalue[10];
					int value = (SAMPLE_PLAYING(v) ? sample_get_effect_arg(id, i, j) : vj_tag_get_effect_arg(id,i,j));
					if(value != -1)
					{
						if(j == (np-1))
							sprintf(svalue, " %d;", value);
						else 
							sprintf(svalue, " %d", value);
						veejay_strncat( bundle, svalue, strlen(svalue));
					}
				}
				veejay_strncpy( blob+bunlen, bundle,strlen(bundle));
				bunlen += strlen(bundle);
			}
		}
 	}
	sprintf(prefix, "BUN:%03d{", num_cmd);
	sprintf(buf, "%s%s}",prefix,blob);
	event_id = vj_event_suggest_bundle_id();

	if(event_id <= 0 )	
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot add more bundles");
		return;
	}

	vj_msg_bundle *m = vj_event_bundle_new( buf, event_id);
	if(!m)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Bundle");
		return;
	}
	if(!vj_event_bundle_store(m))
		veejay_msg(VEEJAY_MSG_ERROR, "Error storing Bundle %d", event_id);
}


void vj_event_print_sample_info(veejay_t *v, int id) 
{
	video_playback_setup *s = v->settings;
	int y, i, j;
	long value;
	char timecode[15];
	char curtime[15];
	char sampletitle[200];
	MPEG_timecode_t tc;
	y4m_ratio_t ratio = mpeg_conform_framerate( (double)v->current_edit_list->video_fps );
	long start = sample_get_startFrame( id );
	long end = sample_get_endFrame( id );
	long speed = sample_get_speed(id);
	long len = end - start;

//	if(start == 0) len ++;
	veejay_memset( &tc,0,sizeof(MPEG_timecode_t));
	mpeg_timecode(&tc, len,	mpeg_framerate_code( ratio ),v->current_edit_list->video_fps);
	sprintf(timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);

	mpeg_timecode(&tc,  s->current_frame_num, mpeg_framerate_code(ratio),v->current_edit_list->video_fps);
	sprintf(curtime, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
	sample_get_description( id, sampletitle );

	veejay_msg(VEEJAY_MSG_PRINT, "\n");
	veejay_msg(VEEJAY_MSG_INFO, 
		"Sample '%s'[%4d]/[%4d]\t[duration: %s | %8ld]",
		sampletitle,id,sample_size()-1,timecode,len);
	
	if( sample_get_composite( id ) )
		veejay_msg(VEEJAY_MSG_INFO, "This sample will be transformed when used as secundary input.");

	if(sample_encoder_active(v->uc->sample_id))
	{
		veejay_msg(VEEJAY_MSG_INFO, "REC %09d\t[timecode: %s | %8ld ]",
			sample_get_frames_left(v->uc->sample_id),
			curtime,(long)v->settings->current_frame_num);

	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "               \t[timecode: %s | %8ld ]",
			curtime,(long)v->settings->current_frame_num);
	}
	veejay_msg(VEEJAY_MSG_INFO, 
		"[%09ld] - [%09ld] @ %4.2f (speed %d)",
		start,end, (float)speed * v->current_edit_list->video_fps,speed);
	veejay_msg(VEEJAY_MSG_INFO,
		"[%s looping]",
		(sample_get_looptype(id) ==
		2 ? "pingpong" : (sample_get_looptype(id)==1 ? "normal" : (sample_get_looptype(id)==3 ? "random" : "none"))  )
		);

	int first = 0;
    	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		y = sample_get_effect_any(id, i);
		if (y != -1)
		{
			if(!first)
			{
			 veejay_msg(VEEJAY_MSG_INFO, "\nI: E F F E C T  C H A I N\nI:");
			 veejay_msg(VEEJAY_MSG_INFO,"Entry|Effect ID|SW | Name");
				first = 1;
			}
			veejay_msg(VEEJAY_MSG_INFO, "%02d   |%03d      |%s| %s %s",
				i,
				y,
				sample_get_chain_status(id,i) ? "on " : "off", vj_effect_get_description(y),
				(vj_effect_get_subformat(y) == 1 ? "2x2" : "1x1")
			);

	    		for (j = 0; j < vj_effect_get_num_params(y); j++)
			{
				value = sample_get_effect_arg(id, i, j);
				if (j == 0)
				{
		    			veejay_msg(VEEJAY_MSG_PRINT, "I:\t\t\tP%d=[%d]",j, value);
				}
				else
				{
		    			veejay_msg(VEEJAY_MSG_PRINT, " P%d=[%d] ",j,value);
				}
			}
			veejay_msg(VEEJAY_MSG_PRINT, "\n");
	    		if (vj_effect_get_extra_frame(y) == 1)
			{
				int source = sample_get_chain_source(id, i);
						 
				veejay_msg(VEEJAY_MSG_PRINT, "I:\t\t\t Mixing with %s %d\n",(source == VJ_TAG_TYPE_NONE ? "sample" : "stream"),
			    		sample_get_chain_channel(id,i)
					);
	    		}
		}
    	}

	//vj_el_print( sample_get_editlist( id ) );

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Sample has EDL %p, Plain at %p", sample_get_editlist( id ), v->current_edit_list );

	veejay_msg(VEEJAY_MSG_PRINT, "\n");

}

void vj_event_print_plain_info(void *ptr, int x)
{
	veejay_t *v = (veejay_t*) ptr;
	if( PLAIN_PLAYING(v)) vj_el_print( v->edit_list );
}

void vj_event_print_info(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);
	if(args[0]==0)
	{
		args[0] = v->uc->sample_id;
	}

	veejay_msg(VEEJAY_MSG_INFO, "%d / %d Mb used in cache",
		get_total_mem(),
		vj_el_cache_size() );

	vj_event_print_plain_info(v,args[0]);

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])  )
	{	
		vj_event_print_sample_info( v, args[0] );
	}
	if( STREAM_PLAYING(v) && vj_tag_exists(args[0]) )
	{
		vj_event_print_tag_info(v, args[0]) ;
	}
}

void	vj_event_send_track_list		(	void *ptr,	const char format[], 	va_list ap 	)
{
	veejay_t *v = (veejay_t*)ptr;
	veejay_memset( _s_print_buf,0,SEND_BUF);
	sprintf(_s_print_buf, "%05d",0);
	int n = vj_tag_size()-1;
	if (n >= 1 )
	{
		char line[300];
		veejay_memset( _print_buf, 0,SEND_BUF);
		int i;
		for(i=0; i <= n; i++)
		{
			if(vj_tag_exists(i) && !vj_tag_is_deleted(i))
			{	
				vj_tag *tag = vj_tag_get(i);
				if(tag->source_type == VJ_TAG_TYPE_NET )
				{
					char cmd[275];
					char space[275];
					sprintf(space, "%s %d", tag->descr, tag->id );
					sprintf(cmd, "%03d%s",strlen(space),space);
					APPEND_MSG(_print_buf,cmd); 
				}
			}
		}
		sprintf(_s_print_buf, "%05d%s",strlen(_print_buf),_print_buf);
	}

	SEND_MSG(v,_s_print_buf);
}

void	vj_event_send_tag_list			(	void *ptr,	const char format[],	va_list ap	)
{
	int args[1];
	
	veejay_t *v = (veejay_t*)ptr;
	char *str = NULL; 
	P_A(args,str,format,ap);
	int i,n;
	veejay_memset( _s_print_buf,0,SEND_BUF);
	sprintf(_s_print_buf, "%05d",0);

	//if(args[0]>0) start_from_tag = args[0];

	n = vj_tag_size()-1;
	if (n >= 1 )
	{
		char line[300];
		veejay_memset( _print_buf,0, SEND_BUF);

		for(i=0; i <= n; i++)
		{
			if(vj_tag_exists(i) &&!vj_tag_is_deleted(i))
			{	
				vj_tag *tag = vj_tag_get(i);
				char source_name[255];
				char cmd[300];
				veejay_memset(source_name,0,200);veejay_memset(cmd,0,255);
				veejay_memset(line,0,300);
				//vj_tag_get_description( i, source_name );
				vj_tag_get_source_name( i, source_name );
				sprintf(line,"%05d%02d%03d%03d%03d%03d%03d%s",
					i,
					vj_tag_get_type(i),
					tag->color_r,
					tag->color_g,
					tag->color_b,
					tag->opacity, 
					strlen(source_name),
					source_name
				);
				sprintf(cmd, "%03d%s",strlen(line),line);
				APPEND_MSG(_print_buf,cmd); 
			}
		}
		sprintf(_s_print_buf, "%05d%s",strlen(_print_buf),_print_buf);
	}

	SEND_MSG(v,_s_print_buf);
}

static	void	_vj_event_gatter_sample_info( veejay_t *v, int id )
{
	char description[SAMPLE_MAX_DESCR_LEN];
	int end_frame 	= sample_get_endFrame( id );
	int start_frame = sample_get_startFrame( id );
	char timecode[20];
	MPEG_timecode_t tc;
	y4m_ratio_t ratio = mpeg_conform_framerate( (double) v->current_edit_list->video_fps );
	mpeg_timecode( &tc, (end_frame - start_frame),mpeg_framerate_code(ratio),v->current_edit_list->video_fps );

	sprintf( timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h,tc.m,tc.s,tc.f );
	sample_get_description( id, description );

	int dlen = strlen(description);
	int tlen = strlen(timecode);	

	sprintf( _s_print_buf, 
		"%08d%03d%s%03d%s%02d%02d",
		( 3 + dlen + 3+ tlen + 2 +2),
		dlen,
		description,
		tlen,
		timecode,
		0,
		id
	);	

}
static	void	_vj_event_gatter_stream_info( veejay_t *v, int id )
{
	char description[SAMPLE_MAX_DESCR_LEN];
	char source[255];
	int  stream_type = vj_tag_get_type( id );
	veejay_memset( source,0, 255 );
	vj_tag_get_source_name( id, source );
	vj_tag_get_description( id, description );
	
	int dlen = strlen( description );
	int tlen = strlen( source );
	sprintf( _s_print_buf,
		"%08d%03d%s%03d%s%02d%02d",
		(  3 + dlen + 3 + tlen + 2 + 2),
		dlen,
		description,
		tlen,
		source,
		stream_type,
		id 
	);
}

void	vj_event_send_sample_info		(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	int failed = 1;
	char *str = NULL;
	P_A(args,str,format,ap);
	if(args[0] == 0 )
		args[0] = v->uc->sample_id;

	veejay_memset( _s_print_buf,0,SEND_BUF);

	switch( args[1] )
	{
		case 0:
			if(args[0] == -1)
				args[0] = sample_size() - 1;

			if(sample_exists(args[0]))
			{
				_vj_event_gatter_sample_info(v,args[0]);
				failed = 0;
			}
			break;
		case  1:
			if(args[0] == -1)
				args[0] = vj_tag_size() - 1;

			if(vj_tag_exists(args[0]))
			{
				_vj_event_gatter_stream_info(v,args[0]);	
				failed = 0;
			}
			break;
		default:
			break;
	}
	
	if(failed)
		sprintf( _s_print_buf, "%08d", 0 );
	SEND_MSG(v , _s_print_buf );
}

void	vj_event_get_scaled_image		(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	int w=0,h=0;
	w = args[0]; 
	h = args[1];

	if( w <= 0 || h <= 0 || w >= 2000 || h >= 2000 )
	{
		veejay_msg(0, "Invalid image dimension %dx%d requested",w,h );
		SEND_MSG(v, "0000000" );
		return;
	}

	veejay_image_t *img = NULL;
	int pixel_format = get_ffmpeg_pixfmt(v->pixel_format);
	VJFrame frame;
	veejay_memcpy(&frame, v->effect_frame1, sizeof(VJFrame));
	vj_perform_get_primary_frame( v, frame.data );
	if( v->settings->composite ) {
		pixel_format = composite_get_top( v->composite, frame.data,
						  frame.data,
						  v->settings->composite );
		frame.width = v->video_output_width;
		frame.height = v->video_output_height;
		switch(pixel_format) {
			case PIX_FMT_YUV444P:
			case PIX_FMT_YUVJ444P:
				frame.uv_width = frame.width;
				frame.uv_height= frame.height;
				frame.ssm = 1;
				frame.shift_v = 0;
				frame.shift_h = 0;
				frame.len = frame.width * frame.height;
				frame.uv_len = frame.len;
				break;
			case PIX_FMT_YUVJ422P:
			case PIX_FMT_YUV422P:
				frame.uv_width = frame.width;
				frame.uv_height= frame.height / 2;
				frame.ssm = 0;
				frame.shift_v = 1;
				frame.shift_h = 0;
				frame.len = frame.width * frame.height;
				frame.uv_len = frame.uv_width * frame.uv_height;
				break;
			}

	}
	//@ fast*_picture delivers always 4:2:0 data to reduce bandwidth
	if( use_bw_preview_ )
		vj_fastbw_picture_save_to_mem(
				&frame,
				w,
				h,
				pixel_format );
	else
		vj_fast_picture_save_to_mem(
				&frame,
				w,
				h,
				pixel_format );

	int dstlen = (use_bw_preview_ ? ( w * h ) : (( w * h ) + ((w * h)/2)) );

	char header[8];
	sprintf( header, "%06d%1d", dstlen, use_bw_preview_ );
	SEND_DATA(v, header, 7 );
	SEND_DATA(v, vj_perform_get_preview_buffer(), dstlen );
}

void	vj_event_get_cali_image		(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *str = NULL;
	P_A(args,str,format,ap);

	int id   = args[0];
	int type = args[1];
	
	if( !vj_tag_exists(id) || vj_tag_get_type(id) != VJ_TAG_TYPE_V4L || type < 0 || type > 2)
	{
		SEND_MSG(v, "000000000" );
		return;
	}

	vj_tag *tag = vj_tag_get(id);

	int total_len = 0;
	int uv_len    = 0;
	int len       = 0;

	uint8_t *buf = vj_tag_get_cali_buffer( id , type,  &total_len, &len, &uv_len );

	if( buf == NULL ) {
		SEND_MSG(v, "00000000"  );
	}
	else {
		char header[128];//FIXME
		memset(header,0,sizeof(header));
		sprintf( header, "%03d%08d%06d%06d%06d%06d",8+6+6+6+6,len, len, 0, v->current_edit_list->video_width, v->current_edit_list->video_height );
		SEND_MSG( v, header );

		int res = vj_server_send(v->vjs[VEEJAY_PORT_CMD], v->uc->current_link, buf,len);
	}
}



void	vj_event_toggle_bw( void *ptr, const char format[], va_list ap )
{
	if( use_bw_preview_ )
		use_bw_preview_ = 0;
	else
		use_bw_preview_ = 1;
}

void	vj_event_send_working_dir(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char str[2048];
	P_A(args,str,format,ap);

	
	filelist_t *list = (filelist_t*)find_media_files(v);
	if(!list) {
		veejay_msg(VEEJAY_MSG_ERROR, "No usable files found.");
		sprintf(_s_print_buf, "00000000");
	}else {

		int len = 1;
		int i;
		for( i = 0; i < list->num_files; i ++ ) {
			len += ( list->files[i] == NULL ? 0 : strlen( list->files[i] ) );
		}

		int msg_len = (list->num_files*4) + len - 1;
		sprintf(_s_print_buf, "%08d", msg_len );

		int tlen=0;
		for( i = 0; i <list->num_files; i ++ ) {
			char tmp[1024];
			if(list->files[i]==NULL)
				continue;
			tlen = strlen(list->files[i]);
#ifdef STRICT_CHECKING
			assert( tlen <= sizeof(tmp));
#endif
			snprintf(tmp,sizeof(tmp), "%04d%s",tlen,list->files[i]);

			strcat( _s_print_buf,tmp);
		}
		SEND_MSG(v,_s_print_buf);
		free_media_files(v,list);
	}
}

void	vj_event_send_sample_list		(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	int start_from_sample = 1;
	char cmd[2048];
	char *str = NULL;
	int i,n;
	P_A(args,str,format,ap);
	if(args[0] > 0 )
		start_from_sample = args[0];
	veejay_memset( _s_print_buf,0,SEND_BUF);
	sprintf(_s_print_buf, "00000000");

	n = sample_size();
	if( n > 1 )
	{
		char line[400];
		veejay_memset(_print_buf, 0,SEND_BUF);
		for(i=start_from_sample; i <= n; i++)
		{
			if(sample_exists(i))
			{	
				char description[SAMPLE_MAX_DESCR_LEN];
				int end_frame = sample_get_endFrame(i);
				int start_frame = sample_get_startFrame(i);
				veejay_memset(cmd,0, sizeof(cmd));

				/* format of sample:
				 	00000 : id
				    000000000 : start    
                                    000000000 : end
                                    xxx: str  : description
				*/
				sample_get_description( i, description );
				
				sprintf(cmd,"%05d%09d%09d%03d%s",
					i,
					start_frame,	
					end_frame,
					strlen(description),
					description
				);
				FORMAT_MSG(line,cmd);
				APPEND_MSG(_print_buf,line); 
			}

		}
		sprintf(_s_print_buf, "%08d%s", strlen(_print_buf),_print_buf);

	}
	SEND_MSG(v, _s_print_buf);
}

void	vj_event_send_log			(	void *ptr,	const char format[],	va_list ap 	)
{
	veejay_t *v = (veejay_t*) ptr;
	int num_lines = 0;
	int str_len = 0;
	char *messages = NULL;
	veejay_memset( _s_print_buf,0,SEND_BUF);

	messages = veejay_pop_messages( &num_lines, &str_len );

	if(str_len == 0 || num_lines == 0 )
		sprintf(_s_print_buf, "%06d", 0);
	else
		sprintf(_s_print_buf, "%06d%s", str_len, messages );
	if(messages)
		free(messages);	

	veejay_msg(VEEJAY_MSG_DEBUG, "\tDebug: send log %s", _s_print_buf);

	SEND_MSG( v, _s_print_buf );
}

void	vj_event_send_sample_stack		(	void *ptr,	const char format[],	va_list ap )
{
	char line[32];
	int args[4];
	char *str = NULL;
	int error = 1;

	char	buffer[1024];
	char    message[1024];  
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,str,format,ap);

	buffer[0] = '\0';
	message[0] = '\0';

	int channel, source,fx_id,i, offset,sample_len;

	if( SAMPLE_PLAYING(v)  ) {
		if(args[0] == 0) 
			args[0] = v->uc->sample_id;

		for( i = 0; i < SAMPLE_MAX_EFFECTS ;i ++ ) {
			fx_id = sample_get_effect_any( args[0], i );
			if( fx_id <= 0 )
				continue;
			channel = sample_get_chain_channel( args[0], i );
			source  = sample_get_chain_source( args[0], i );
			offset  = sample_get_offset( args[0], i );
			if( source == 0 )
				sample_len= sample_video_length( channel );
			else 
				sample_len = 50; //@TODO, implement stream handling
			snprintf( line, sizeof(line), "%02d%04d%02d%08d%08d", i, channel, source, offset, sample_len );
			strncat( buffer, line, strlen(line));
		}
	} else if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) 
			args[0] = v->uc->sample_id;

		for(i = 0; i < SAMPLE_MAX_EFFECTS ; i ++ ) {
			fx_id = vj_tag_get_effect_any( args[0], i );
			if( fx_id <= 0 )
				continue;
			channel = vj_tag_get_chain_channel( args[0], i );
			source  = vj_tag_get_chain_source( args[0], i );
			offset	= vj_tag_get_offset( args[0], i );
			if( source == 0 )
				sample_len= sample_video_length( channel );
			else 
				sample_len = 50; //@TODO, implement stream handling

			snprintf( line, sizeof(line), "%02d%04d%02d%08d%08d",i,channel,source, offset, sample_len );
			strncat( buffer, line, strlen(line));
		}
	}	



	FORMAT_MSG( message, buffer );
	SEND_MSG(   v, message );

}

void	vj_event_send_chain_entry		( 	void *ptr,	const char format[],	va_list ap	)
{
	char fline[255];
	char line[255];
	int args[4];
	char *str = NULL;
	int error = 1;
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,str,format,ap);
	veejay_memset(line,0,255);
	veejay_memset(fline,0,255);
	sprintf(line, "%03d", 0);

	if( SAMPLE_PLAYING(v)  )
	{
		if(args[0] == 0) 
			args[0] = v->uc->sample_id;

		if(args[1]==-1)
			args[1] = sample_get_selected_entry(args[0]);

		int effect_id = sample_get_effect_any(args[0], args[1]);
		
		if(effect_id > 0)
		{
			int is_video = vj_effect_get_extra_frame(effect_id);
			int params[SAMPLE_MAX_PARAMETERS];
			int p;
			int video_on = sample_get_chain_status(args[0],args[1]);
			int audio_on = 0;
			//int audio_on = sample_get_chain_audio(args[0],args[1]);
			int num_params = vj_effect_get_num_params(effect_id);
			for(p = 0 ; p < num_params; p++)
				params[p] = sample_get_effect_arg(args[0],args[1],p);
#ifdef STRICT_CHECKING
			assert( args[2] >= 0 && args[2] <= num_params );
#endif
			for(p = num_params; p < SAMPLE_MAX_PARAMETERS; p++)
				params[p] = 0;

			int kf_start = 0, kf_end = 0, kf_type = 0;
			int kf_status = sample_get_kf_status( args[0],args[1] );
			sample_get_kf_tokens( args[0],args[1],args[2],&kf_start,&kf_end,&kf_type );
			sprintf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
				effect_id,
				is_video,
				num_params,
				params[0],
				params[1],	
				params[2],	
				params[3],
				params[4],		
				params[5],
				params[6],
				params[7],
				params[8],
				video_on,
				audio_on,
				sample_get_chain_source(args[0],args[1]),
				sample_get_chain_channel(args[0],args[1]),
				kf_status, kf_start,kf_end,kf_type
			);				
			error = 0;
		}
	}
	
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0) 
			args[0] = v->uc->sample_id;

		if(args[1] == -1)
			args[1] = vj_tag_get_selected_entry(args[0]);

 		int effect_id = vj_tag_get_effect_any(args[0], args[1]);

		if(effect_id > 0)
		{
			int is_video = vj_effect_get_extra_frame(effect_id);
			int params[SAMPLE_MAX_PARAMETERS];
			int p;
			int num_params = vj_effect_get_num_params(effect_id);
			int video_on = vj_tag_get_chain_status(args[0], args[1]);
			for(p = 0 ; p < num_params; p++)
			{
				params[p] = vj_tag_get_effect_arg(args[0],args[1],p);
			}
			for(p = num_params; p < SAMPLE_MAX_PARAMETERS;p++)
			{
				params[p] = 0;
			}
			int kf_start = 0, kf_end = 0, kf_type = 0;
			int kf_status = vj_tag_get_kf_status(args[0],args[1]);
			vj_tag_get_kf_tokens( args[0],args[1],args[2],&kf_start,&kf_end,&kf_type );
			sprintf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
				effect_id,
				is_video,
				num_params,
				params[0],
				params[1],	
				params[2],	
				params[3],
				params[4],		
				params[5],
				params[6],
				params[7],
				params[8],
				video_on,	
				0,
				vj_tag_get_chain_source(args[0],args[1]),
				vj_tag_get_chain_channel(args[0],args[1]),
				kf_status,kf_start,kf_end, kf_type
			);				
			error = 0;
		}
	}

	if(!error)
	{
		FORMAT_MSG(fline,line);
		SEND_MSG(v, fline);
	}
	else
		SEND_MSG(v,line);
}

void	vj_event_send_chain_list		( 	void *ptr,	const char format[],	va_list ap	)
{
	int i;
	char line[18];
	int args[1];
	char *str = NULL;
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,str,format,ap);

	if(args[0] == 0) 
		args[0] = v->uc->sample_id;

	veejay_memset( _s_print_buf,0,SEND_BUF);
	veejay_memset(  _print_buf,0, SEND_BUF);

	sprintf( _s_print_buf, "%03d",0 );

	if(SAMPLE_PLAYING(v))
	{
		if(args[0] == -1) args[0] = sample_size()-1;

		for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
		{
			int effect_id = sample_get_effect_any(args[0], i);
			if(effect_id > 0)
			{
				int is_video = vj_effect_get_extra_frame(effect_id);
				int using_effect = sample_get_chain_status(args[0], i);
				int using_audio = 0;
				//int using_audio = sample_get_chain_audio(args[0],i);
				sprintf(line,"%02d%03d%1d%1d%1d",
					i,
					effect_id,
					is_video,
					(using_effect <= 0  ? 0 : 1 ),
					(using_audio  <= 0  ? 0 : 1 )
				);
						
				APPEND_MSG(_print_buf,line);
			}
		}
		sprintf(_s_print_buf, "%03d%s",strlen(_print_buf), _print_buf);

	}
	if(STREAM_PLAYING(v))
	{
		if(args[0] == -1) args[0] = vj_tag_size()-1;

		for(i=0; i < SAMPLE_MAX_EFFECTS; i++) 
		{
			int effect_id = vj_tag_get_effect_any(args[0], i);
			if(effect_id > 0)
			{
				int is_video = vj_effect_get_extra_frame(effect_id);
				int using_effect = vj_tag_get_chain_status(args[0],i);
				sprintf(line, "%02d%03d%1d%1d%1d",
					i,
					effect_id,
					is_video,
					(using_effect <= 0  ? 0 : 1 ),
					0
				);
				APPEND_MSG(_print_buf, line);
			}
		}
		sprintf(_s_print_buf, "%03d%s",strlen( _print_buf ), _print_buf);

	}
	SEND_MSG(v, _s_print_buf);
}

void 	vj_event_send_video_information		( 	void *ptr,	const char format[],	va_list ap	)
{
	/* send video properties */
	char info_msg[255];
	veejay_t *v = (veejay_t*)ptr;
	veejay_memset(info_msg,0,sizeof(info_msg));
	veejay_memset( _s_print_buf,0,SEND_BUF);
	veejay_memset( info_msg,0, 255 );

	editlist *el = v->current_edit_list;
/*
	editlist *el = ( SAMPLE_PLAYING(v) ? sample_get_editlist( v->uc->sample_id ) : 
				v->current_edit_list );
*/
	long n_frames = el->total_frames;
	if( SAMPLE_PLAYING(v))
		n_frames = sample_max_video_length( v->uc->sample_id );

	snprintf(info_msg,sizeof(info_msg)-1, "%04d %04d %01d %c %02.3f %1d %04d %06ld %02d %03ld %08ld %1d",
		el->video_width,
		el->video_height,
		el->video_inter,
		el->video_norm,
		el->video_fps,  
		el->has_audio,
		el->audio_bits,
		el->audio_rate,
		el->audio_chans,
		el->num_video_files,
		n_frames,
		v->audio
		);	
	sprintf( _s_print_buf, "%03d%s",strlen(info_msg), info_msg);


	SEND_MSG(v,_s_print_buf);
}

void 	vj_event_send_editlist			(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*) ptr;
	veejay_memset( _s_print_buf,0, SEND_BUF );
	int b = 0;
	editlist *el = v->current_edit_list;
/* ( SAMPLE_PLAYING(v) ? sample_get_editlist( v->uc->sample_id ) : 
				v->current_edit_list );*/

	if( el->num_video_files <= 0 )
	{
		SEND_MSG( v, "000000");
		return;
	}


	char *msg = (char*) vj_el_write_line_ascii( el, &b );
	sprintf( _s_print_buf, "%06d%s", b, msg );
	if(msg)free(msg);

	SEND_MSG( v, _s_print_buf );
}

void	vj_event_send_devices			(	void *ptr,	const char format[],	va_list ap	)
{
	char str[255];
	struct dirent **namelist;
	int n_dev = 0;
	int n;
	char device_list[512];
	char useable_devices[2];
	int *args = NULL;
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,str,format,ap);
	veejay_memset(device_list,0,512);

	n = scandir(str,&namelist,0,alphasort);
	if( n<= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No device information in [%s]",str);
		SEND_MSG(v,"0000");
		return;
	}

		
	while(n--)
	{
		if( strncmp(namelist[n]->d_name, "video", 4)==0)
		{
			FILE *fd;
			char filename[300];
			sprintf(filename,"%s%s",str,namelist[n]->d_name);
			fd = fopen( filename, "r");
			if(fd)
			{
				fclose(fd);
			}
		}
	}
	sprintf(useable_devices,"%02d", n_dev);

	APPEND_MSG( device_list, useable_devices );
	SEND_MSG(v,device_list);

}

void	vj_event_send_frame				( 	void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	
	int i = 0;
	for( i = 0; i < 8 ; v->rlinks ++ )
		if( v->rlinks[i] < 0 || v->rlinks[i] == v->uc->current_link )
			break;

	if( i == 8 ) {
		veejay_msg(0, "No more video stream connections allowed, limited to 8");	
		SEND_MSG(v,"00000000000000000000"); 
		return;
	}

	if (!v->settings->is_dat )
	{
		veejay_msg(1, "Wrong control port for retrieving frames!");
		SEND_MSG(v, "00000000000000000000"); //@ send empty header only (20 bytes)
		return;
	}


	v->rlinks[i] = v->uc->current_link;
	v->settings->unicast_frame_sender = 1;

//	v->settings->unicast_frame_sender = 1;
//	v->settings->unicast_link_id      = v->uc->current_link;
}


void	vj_event_mcast_start				(	void *ptr,	const char format[],	va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char s[255];	
	P_A( args, s , format, ap);

	if(!v->settings->use_vims_mcast)
		veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -T commandline option)");	
	else
	{
		v->settings->mcast_frame_sender = 1;
		v->settings->mcast_mode = args[0];
		vj_server_set_mcast_mode( v->vjs[2],args[0] );
		veejay_msg(VEEJAY_MSG_INFO, "Veejay started mcast frame sender");
	}	
}


void	vj_event_mcast_stop				(	void *ptr,	const char format[],	va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	if(!v->settings->use_vims_mcast)
		veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");	
	else
	{
		v->settings->mcast_frame_sender = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Veejay stopped mcast frame sender");
	}	
}

void	vj_event_send_effect_list		(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int i;
	char *priv_msg = NULL;
	int   len = 0;

	for( i = 1; i < vj_effect_max_effects(); i ++ )
		len += vj_effect_get_summary_len( i );

	priv_msg = (char*) malloc(sizeof(char) * (5 + len + 1000));
	memset(priv_msg, 0, (5+len+100));
	sprintf(priv_msg, "%05d", len );
	char line[1025];
	char fline[1025];
	for(i=1; i < vj_effect_max_effects(); i++)
	{
		int effect_id = vj_effect_get_real_id(i);
		veejay_memset(line,0, sizeof(line));
		veejay_memset(fline,0,sizeof(fline));
		if(vj_effect_get_summary(i,line))
		{
			sprintf(fline, "%03d%s", strlen(line), line );
			veejay_strncat( priv_msg, fline, strlen(fline) );
		}
	}
	SEND_MSG(v,priv_msg);
	free(priv_msg);
}



int vj_event_load_bundles(char *bundle_file)
{
	FILE *fd;
	char *event_name, *event_msg;
	char buf[65535];
	int event_id=0;
	if(!bundle_file) return -1;
	fd = fopen(bundle_file, "r");
	veejay_memset(buf,0,65535);
	if(!fd) return -1;
	while(fgets(buf,4096,fd))
	{
		buf[strlen(buf)-1] = 0;
		event_name = strtok(buf, "|");
		event_msg = strtok(NULL, "|");
		if(event_msg!=NULL && event_name!=NULL) {
			//veejay_msg(VEEJAY_MSG_INFO, "Event: %s , Msg [%s]",event_name,event_msg);
			event_id = atoi( event_name );
			if(event_id && event_msg)
			{
				vj_msg_bundle *m = vj_event_bundle_new( event_msg, event_id );
				if(m != NULL) 
				{
					if( vj_event_bundle_store(m) ) 
					{
						veejay_msg(VEEJAY_MSG_INFO, "(VIMS) Registered a bundle as event %03d",event_id);
					}
				}
			}
		}
	}
	fclose(fd);
	return 1;
}

void vj_event_do_bundled_msg(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[1];
	char s[1024];	
	vj_msg_bundle *m;
	P_A( args, s , format, ap);
	//veejay_msg(VEEJAY_MSG_INFO, "Parsing message bundle as event");
	m = vj_event_bundle_get(args[0]);
	if(m)
	{
		vj_event_parse_bundle( v, m->bundle );
	}	
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Requested event %d does not exist. ",args[0]);
	}
}

#ifdef HAVE_SDL
void vj_event_attach_detach_key(void *ptr, const char format[], va_list ap)
{
	int args[4] = { 0,0,0,0 };
	char value[100];
	veejay_memset(value,0,sizeof(value));
	int mode = 0;
	

	P_A( args, value, format ,ap );

	if( args[1] <= 0 || args[1] >= SDLK_LAST)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid key identifier %d (range is 1 - %d)", args[1], SDLK_LAST);
		return;
	}
	if( args[2] < 0 || args[2] > VIMS_MOD_SHIFT )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid key modifier (3=shift,2=ctrl,1=alt, 0=none)");
		return;
	}

	char *clone = NULL;
	mode = args[0];

	switch(mode)
	{
		case 1:
			vj_event_unregister_keyb_event( args[1],args[2] );
			break;
		default:

			if( strncmp(value, "dummy",5 ) != 0 )
				clone = value;
			vj_event_register_keyb_event( args[0], args[1], args[2], clone );
		break;
	}
}	
#endif

void vj_event_bundled_msg_del(void *ptr, const char format[], va_list ap)
{
	
	int args[1];	
	char *s = NULL;
	P_A(args,s,format,ap);
	if ( vj_event_bundle_del( args[0] ) == 0)
	{
		veejay_msg(VEEJAY_MSG_INFO,"Bundle %d deleted from event system",args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Bundle is %d is not known",args[0]);
	}
}




void vj_event_bundled_msg_add(void *ptr, const char format[], va_list ap)
{
	
	int args[2] = {0,0};
	char s[1024];
	veejay_memset(s,0, 1024);
	P_A(args,s,format,ap);

	if(args[0] == 0)
	{
		args[0] = vj_event_suggest_bundle_id();
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) suggested new Event id %d", args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) requested to add/replace %d", args[0]);
	}

	if(args[0] < VIMS_BUNDLE_START|| args[0] > VIMS_BUNDLE_END )
	{
		// invalid bundle
		veejay_msg(VEEJAY_MSG_ERROR, "Customized events range from %d-%d", VIMS_BUNDLE_START, VIMS_BUNDLE_END);
		return;
	}
	// allocate new
	veejay_strrep( s, '_', ' ');
	vj_msg_bundle *m = vj_event_bundle_new(s, args[0]);
	if(!m)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error adding bundle ?!");
		return;
	}

	// bye existing bundle
	if( vj_event_bundle_exists(args[0]))
	{
		veejay_msg(VEEJAY_MSG_DEBUG,"(VIMS) Bundle exists - replacing contents ");
		vj_msg_bundle *mm = vj_event_bundle_get( args[0] );
		if(mm)
		{
			m->modifier = mm->modifier;
			m->accelerator = mm->accelerator;
		}

		vj_event_bundle_del( args[0] );
	}

	if( vj_event_bundle_store(m)) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "(VIMS) Registered Bundle %d in VIMS",args[0]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Error in Bundle %d '%s'",args[0],s );
	}
}

void	vj_event_set_stream_color(void *ptr, const char format[], va_list ap)
{
	int args[4];
	char *s = NULL;
	P_A(args,s,format,ap);
	veejay_t *v = (veejay_t*) ptr;
	
	if(STREAM_PLAYING(v))
	{
		if(args[0] == 0 ) args[0] = v->uc->sample_id;
		if(args[0] == -1) args[0] = vj_tag_size()-1;
	}
	// allow changing of color while playing plain/sample
	if(vj_tag_exists(args[0]) &&
		vj_tag_get_type(args[0]) == VJ_TAG_TYPE_COLOR )
	{
		CLAMPVAL( args[1] );
		CLAMPVAL( args[2] );
		CLAMPVAL( args[3] );	
		vj_tag_set_stream_color(args[0],args[1],args[2],args[3]);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist",
			args[0]);
	}
}

#ifdef USE_GDK_PIXBUF
void vj_event_screenshot(void *ptr, const char format[], va_list ap)
{
	int args[4];
	char filename[1024];
	veejay_memset(filename,0,1024);
	P_A(args, filename, format, ap );
	veejay_t *v = (veejay_t*) ptr;

	char type[5];
	veejay_memset(type,0,5); 


	veejay_get_file_ext( filename, type, sizeof(type));

	if(args[0] == 0 )
		args[0] = v->video_output_width;
	if(args[1] == 0 )
		args[1] = v->video_output_height;
	
	v->settings->export_image = 
		vj_picture_prepare_save( filename , type, args[0], args[1] );
	if(v->settings->export_image)
	  v->uc->hackme = 1;
}
#else
#ifdef HAVE_JPEG
void vj_event_screenshot(void *ptr, const char format[], va_list ap)
{
	int args[4];
	char filename[1024];
	veejay_memset(filename,0,1024);
	P_A(args, filename, format, ap );
	veejay_t *v = (veejay_t*) ptr;

	v->uc->hackme = 1;
	v->uc->filename = strdup( filename );
}
#endif
#endif

void		vj_event_quick_bundle( void *ptr, const char format[], va_list ap)
{
	vj_event_commit_bundle( (veejay_t*) ptr,0,0);
}


void	vj_event_vloopback_start(void *ptr, const char format[], va_list ap)
{
	int args[2];
	char *s = NULL;
	char device_name[100];

	P_A(args,s,format,ap);
	
	veejay_t *v = (veejay_t*)ptr;

	sprintf(device_name, "/dev/video%d", args[0] );

	veejay_msg(VEEJAY_MSG_INFO, "Open vloopback %s", device_name );

	v->vloopback = vj_vloopback_open( device_name, 	
			(v->current_edit_list->video_norm == 'p' ? 1 : 0),
			 1, // pipe, 0 = mmap 
			 v->video_output_width,
			 v->video_output_height,
			 v->pixel_format );
	if(v->vloopback == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,
			"Cannot open vloopback %s", device_name );

		return;
	}

	int ret = 0;

	veejay_msg(VEEJAY_MSG_DEBUG, "Vloopback pipe");
	ret = vj_vloopback_start_pipe( v->vloopback );
	/*
		veejay_msg(VEEJAY_MSG_DEBUG, "Vloopback mmap");
		ret = vj_vloopback_start_mmap( v->vloopback );
	*/

	if(ret)
	{
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Setup vloopback!");
	}

	if(!ret)
	{
		veejay_msg(VEEJAY_MSG_ERROR,
			"closing vloopback");
		if(v->vloopback)
			vj_vloopback_close( v->vloopback );
		v->vloopback = NULL;
	}	

	if( v->vloopback == NULL )
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup vloopback pusher"); 

}

void	vj_event_vloopback_stop( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	vj_vloopback_close( v->vloopback );
}

/* 
 * Function that returns the options for a special sample (markers, looptype, speed ...) or
 * for a special stream ... 
 *
 * Needs two Parameters, first on: -1 last created sample, 0 == current playing sample, >=1 id of sample
 * second parameter is the playmode of this sample to decide if its a video sample or any kind of stream
 * (for this see comment on void vj_event_send_sample_info(..) 
 */ 
void vj_event_send_sample_options	(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	int id=0;
	char *str = NULL;
	int failed = 1;	

	P_A(args,str,format,ap);
	if(args[0] == 0 )
		args[0] = v->uc->sample_id;
	if(args[0] == -1)
		args[0] = sample_size() - 1;

	veejay_memset( _s_print_buf,0,SEND_BUF);

	id = args[0];
	char options[100];
	char prefix[4];
	veejay_memset(prefix,0, 4 );
	veejay_memset(options, 0,100);

	switch(args[1])
	    {
	    case VJ_PLAYBACK_MODE_SAMPLE: 
		if(sample_exists(id))
		    {
		    /* For gathering sample-infos use the sample_info_t-structure that is defined in /libsample/sampleadm.h */
		    sample_info *si = sample_get(id);
	        	if (si)
		        {
		        int start = si->first_frame;
		        int end   = si->last_frame;
		        int speed = si->speed;
    		        int loop = si->looptype;
		        int marker_start = si->marker_start;
		        int marker_end = si->marker_end;
		        int effects_on = si->effect_toggle;

			sprintf( options,
		        "%06d%06d%03d%02d%06d%06d%01d",
	    	    	     start,
		             end,
			     speed,
			     loop,
			     marker_start,
			     marker_end,
			     effects_on);
			failed = 0;

			sprintf(prefix, "%02d", 0 );

			}	
		    }
		break;
	    case VJ_PLAYBACK_MODE_TAG:		
		if(vj_tag_exists(id)) 
		    {
		    /* For gathering further informations of the stream first decide which type of stream it is 
		       the types are definded in libstream/vj-tag.h and uses then the structure that is definded in 
		       libstream/vj-tag.h as well as some functions that are defined there */
		    vj_tag *si = vj_tag_get(id);
		    int stream_type = si->source_type;
		    
			sprintf(prefix, "%02d", stream_type );

		    if (stream_type == VJ_TAG_TYPE_COLOR)
			{
			int col[3] = {0,0,0};
			col[0] = si->color_r;
			col[1] = si->color_g;
			col[2] = si->color_b;
			
			sprintf( options,
		        "%03d%03d%03d",
			    col[0],
			    col[1],
			    col[2]
			    );
			failed = 0;
			}
		    /* this part of returning v4l-properties is here implemented again ('cause there is
		     * actually a VIMS-command to get these values) to get all necessary stream-infos at 
		     * once so only ONE VIMS-command is needed */
		    else if (stream_type == VJ_TAG_TYPE_V4L)
			{
			int brightness=0;
			int hue = 0;
			int contrast = 0;
			int color = 0;
			int white = 0;
			int sat = 0;
			int effects_on = 0;
			
			vj_tag_get_v4l_properties(id,&brightness,&hue,&contrast, &color, &white );			
			effects_on = si->effect_toggle;
			
			sprintf( options,
		        "%05d%05d%05d%05d%05d%05d%01d",
			    brightness,
			    hue,
			    sat,
			    contrast,
			    color,
			    white,
			    effects_on);
			failed = 0;
			}
		    else	
			{
			int effects_on = si->effect_toggle;
			sprintf( options,
		        "%01d",
			    effects_on);
			failed = 0;
			}
		    }
		break;
	    default:
		break;		
	    }	

	if(failed)
		sprintf( _s_print_buf, "%05d", 0 );
	else
		sprintf( _s_print_buf, "%05d%s%s",strlen(prefix) + strlen(options), prefix,options );

	SEND_MSG(v , _s_print_buf );
}
#ifdef HAVE_FREETYPE
void	vj_event_get_srt_list(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	char *str = NULL;
	int len = 0;

	if(!v->font)
	{
		SEND_MSG(v, "000000" );
		return;
	}

	char **list = vj_font_get_sequences( v->font );
	int i;

	if(!list)
	{
		SEND_MSG(v, "000000" );
		return;
	}
	
	for(i = 0; list[i] != NULL ; i ++ )
	{
		int k = strlen(list[i]);
		if(k>0)
			len += (k+1);
	}	
	if(len <= 0)
	{
		SEND_MSG(v, "000000" );
		return;
	}

	str = vj_calloc( len + 20 );
	char *p = str;
	sprintf(p, "%06d", len );
	p += 6;
	for(i = 0; list[i] != NULL ; i ++ )
	{
		sprintf(p, "%s ", list[i]);
		p += strlen(list[i]) + 1;
		free(list[i]);
	}
	free(list);
	
	SEND_MSG(v , str );
	free(str);
}

void	vj_event_get_font_list(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	char *str = NULL;
	int len = 0;

	if(!v->font)
	{
		SEND_MSG(v, "000000" );
		return;
	}

	char **list = vj_font_get_all_fonts( v->font );
	int i;

	if(!list)
	{
		SEND_MSG(v, "000000" );
		return;
	}
	
	for(i = 0; list[i] != NULL ; i ++ )
	{
		int k = strlen(list[i]);
		if(k>0)
			len += (k+3);
	}	
	if(len <= 0)
	{
		SEND_MSG(v, "000000" );
		return;
	}

	str = vj_calloc( len + 20 );
	char *p = str;
	sprintf(p, "%06d", len );
	p += 6;
	for(i = 0; list[i] != NULL ; i ++ )
	{
		int k = strlen(list[i]);
		sprintf(p, "%03d%s", k,list[i]);
		p += (k + 3);
		free(list[i]);
	}
	free(list);
		
	SEND_MSG(v , str );
	free(str);

}
void	vj_event_get_srt_info(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2] = {0,0};
	char *str = NULL;
	P_A(args,str,format,ap);

	if(!v->font)
	{
		SEND_MSG(v, "000000");
		return;
	}

	char *sequence = vj_font_get_sequence( v->font,args[0] );

	if(!sequence)
	{
		SEND_MSG(v, "000000");
		return;

	}
	
	int len = strlen( sequence );
	str = vj_calloc( len+20 );
	sprintf(str,"%06d%s",len,sequence);
	free(sequence);	
	
	SEND_MSG(v , str );
}

void	vj_event_save_srt(	void *ptr,	const char format[],	va_list	ap	)
{
	char file_name[512];
	int args[1];
	veejay_t *v = (veejay_t*)ptr;

	P_A(args,file_name,format,ap);

	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}

	if( vj_font_save_srt( v->font, file_name ) )
		veejay_msg(VEEJAY_MSG_INFO, "Saved SRT file '%s'", file_name );
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to save SRT file '%s'", file_name );	
}
void	vj_event_load_srt(	void *ptr,	const char format[],	va_list	ap	)
{
	char file_name[512];
	int args[1];
	veejay_t *v = (veejay_t*)ptr;

	P_A(args,file_name,format,ap);

	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}

	if( vj_font_load_srt( v->font, file_name ) )
		veejay_msg(VEEJAY_MSG_INFO, "Loaded SRT file '%s'", file_name );
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to open SRT file '%s'", file_name );	
}

void	vj_event_select_subtitle(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[6];
	veejay_t *v = (veejay_t*)ptr;

	if(!v->font)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No font renderer active");
		return;
	}
	
	P_A(args,NULL,format,ap);

	vj_font_set_current( v->font, args[0] );
}


void	vj_event_get_keyframes( void *ptr, 	const char format[],	va_list ap	)
{
	int args[3];
	veejay_t *v = (veejay_t*)ptr;

	P_A(args,NULL,format,ap);

	if(SAMPLE_PLAYING(v))
	{
		int data_len = 0;
		unsigned char *data = sample_chain_get_kfs( v->uc->sample_id, args[0],args[1], &data_len );
		if( data_len > 0 && data )
		{	
			char header[32];
			sprintf(header, "%08d",data_len );
			SEND_DATA( v, header,8);
			SEND_DATA( v, data, data_len );
			free(data);
			return;
		}
	} else if (STREAM_PLAYING(v))
	{
		int data_len = 0;
		unsigned char *data = vj_tag_chain_get_kfs( v->uc->sample_id, args[0],args[1], &data_len );
		if( data_len > 0 && data )
		{	
			char header[32];
			sprintf(header, "%08d",data_len );
			SEND_DATA( v, header,8);
			SEND_DATA( v, data, data_len );
			free(data);
			return;
		}

	}	
	SEND_MSG( v, "00000000" );
}

void	vj_event_set_kf_status( void *ptr,	const char format[], 	va_list ap	)
{
	int args[3];
	veejay_t *v = (veejay_t*)ptr;

	P_A(args,NULL,format,ap);

	if(SAMPLE_PLAYING(v))
	{
		sample_chain_set_kf_status( v->uc->sample_id, args[0],args[1] );
		veejay_msg(VEEJAY_MSG_INFO, "Sample %d is using animated parameter values", v->uc->sample_id);
	} else if (STREAM_PLAYING(v))
	{
		vj_tag_chain_set_kf_status(v->uc->sample_id,args[0],args[1] );
		veejay_msg(VEEJAY_MSG_INFO, "Stream %d is using animated parameter values", v->uc->sample_id);

	}
}
void	vj_event_reset_kf( void *ptr,	const char format[], 	va_list ap	)
{
	int args[3];
	veejay_t *v = (veejay_t*)ptr;

	P_A(args,NULL,format,ap);

	if(SAMPLE_PLAYING(v))
	{
		sample_chain_reset_kf( v->uc->sample_id, args[0] );
	} else if (STREAM_PLAYING(v))
	{
		vj_tag_chain_reset_kf( v->uc->sample_id, args[0] );
	}

}

static	void	*select_dict( veejay_t *v , int n )
{
	void *dict = NULL;
	if( SAMPLE_PLAYING(v) )
		return sample_get_dict( n );
	else if(STREAM_PLAYING(v))
		return vj_tag_get_dict( n );
	return NULL;
}

void	vj_event_add_subtitle(	void *ptr,	const char format[],	va_list	ap	)
{
	unsigned char text[2048];
	int args[6];
	int k;
	veejay_t *v = (veejay_t*)ptr;

	if(!v->font)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No font renderer active");
		return;
	}

	veejay_memset(text,0,2048);
	P_A(args,text,format,ap);

	void *dict = select_dict( v, v->uc->sample_id );
	if(!dict)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
		return;
	}

	int len = strlen( text );
	if ( len <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No text given");
		return;
	}
	for( k = 0; k < len ; k ++ ) {
		if( !isprint( text[k] ) )
			text[k] == 0x20;
	}
		


	if( args[3] < 0 || args[4] < 0 ||
			args[3] >= v->current_edit_list->video_width ||
			args[4] >= v->current_edit_list->video_height )
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				"Invalid XY position");
		return;
	}

	vj_font_set_dict( v->font, dict );

	int id = vj_font_new_text( v->font, text, (long) args[1], (long)args[2], args[0] );
	
	vj_font_set_position( v->font, args[3] ,args[4] );

	char newslot[50];
	sprintf(newslot, "%05d%05d",5, id );
	SEND_MSG(v,newslot);	
}
void	vj_event_upd_subtitle(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[5]; 
	char text[2048];

	veejay_t *v = (veejay_t*)ptr;
	P_A(args,text,format,ap);

	if(!v->font )
	{
		veejay_msg(0, "No font renderer active");
		return;
	}

        void *dict = select_dict( v, v->uc->sample_id );
        if(!dict)
        {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
                return;
        }

	vj_font_set_dict( v->font, dict );
	vj_font_update_text( v->font, (long) args[1], (long) args[2], args[0], text );
}

void	vj_event_del_subtitle(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);
	
	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}


        void *dict = select_dict( v, v->uc->sample_id );
        if(!dict)
        {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
                return;
        }

	vj_font_set_dict( v->font, dict );

	vj_font_del_text( v->font, args[0] );

}

void	vj_event_font_set_position(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);

	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}

        void *dict = select_dict( v, v->uc->sample_id );
        if(!dict)
        {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
                return;
        }
	vj_font_set_dict( v->font, dict );

	vj_font_set_position( v->font, args[0] ,args[1] );
}
void	vj_event_font_set_color(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[6];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);

	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}

	void *dict = select_dict( v, v->uc->sample_id );
        if(!dict)
        {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
                return;
        }
        vj_font_set_dict( v->font, dict );


	switch( args[4] )
	{
		case 0:
			vj_font_set_outline_and_border(
				v->font, args[0],args[1]  );
			                //outline, //use_bg
			break;
		case 1:
			vj_font_set_fgcolor( v->font,
					args[0],args[1],args[2],args[3] );
			break;
		case 2:
			vj_font_set_bgcolor( v->font,
					args[0],args[1],args[2],args[3] );
			break;
		case 3:
			vj_font_set_lncolor( v->font,
					args[0],args[1],args[2],args[3] );
			break;
		default:
			veejay_msg(0, "Invalid mode. Use 0=outline/border 1=FG,2=BG,3=LN" );
			break;
	}
}
void	vj_event_font_set_size_and_font(	void *ptr,	const char format[],	va_list	ap	)
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);
 
	if(!v->font)
	{
		veejay_msg(0, "No font renderer active");
		return;
	}
 
	void *dict = select_dict( v, v->uc->sample_id );
        if(!dict)
        {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback mode for subtitles");
                return;
        }
        vj_font_set_dict( v->font, dict );

	vj_font_set_size_and_font(v->font, args[0],args[1]);
}
#endif

void	vj_event_sequencer_add_sample(		void *ptr,	const char format[],	va_list ap )
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);

	int seq = args[0];
	int id = args[1];

	if( seq < 0 || seq >= MAX_SEQUENCES )
	{
		veejay_msg( VEEJAY_MSG_ERROR,"Slot not within bounds");
		return;
	}
	
	if( sample_exists(id ))
	{
		v->seq->samples[ seq ] = id;
		if( v->seq->size < MAX_SEQUENCES )
		{ 
			v->seq->size ++;
		}
		veejay_msg(VEEJAY_MSG_INFO, "Added sample %d to slot %d/%d",
				id, seq,MAX_SEQUENCES );
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist. It cannot be added to the sequencer",id);
	}	

}

void	vj_event_sequencer_del_sample(		void *ptr, 	const char format[], 	va_list ap )
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);

	int seq_it = args[0];
	
	if( seq_it < 0 || seq_it >= MAX_SEQUENCES )
	{
		veejay_msg( VEEJAY_MSG_ERROR, "Sequence slot %d is not used, nothing deleted",seq_it );
		return;
	}	

	if( v->seq->samples[ seq_it ] )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Deleted sequence %d (Sample %d)", seq_it,
				v->seq->samples[ seq_it ] );
		v->seq->samples[ seq_it ] = 0;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sequence slot %d already empty", seq_it );
	}

}

void	vj_event_get_sample_sequences( 		void *ptr, 	const char format[],	va_list ap )
{
	veejay_t *v = (veejay_t*)ptr;
	int i;

	if( v->seq->size <= 0 )
	{
		SEND_MSG(v,"000000");
		return;
	}
	
	veejay_memset( _s_print_buf, 0, SEND_BUF );

	sprintf(_s_print_buf, "%06d%04d%04d%04d",
			( 12 + (4*MAX_SEQUENCES)),
			v->seq->current,MAX_SEQUENCES, v->seq->active );
	
	for( i =0; i < MAX_SEQUENCES ;i ++ )
	{
		char tmp[32];
		sprintf(tmp, "%04d", v->seq->samples[i]);
		veejay_strncat(_s_print_buf, tmp, 4 );
	}

	SEND_MSG(v, _s_print_buf );
	
}

void	vj_event_sample_sequencer_active(	void *ptr, 	const char format[],	va_list ap )
{
	int args[5];
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,NULL,format,ap);

	if( v->seq->size == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sequencer list is empty. Please add samples first");
		return;
	}

	if( args[0] == 0 )
	{
		v->seq->active = 0;
		v->seq->current = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Sample sequencer disabled");
	}
	else 
	{
		v->seq->active = 1;
		veejay_msg(VEEJAY_MSG_INFO, "Sample sequencer enabled");
	}
}

void	vj_event_set_macro_status( void *ptr,	const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2] = {0,0};
	int k,i;
	char *str = NULL;
	P_A(args,str,format,ap);

	if( args[1] == 0 )
	{
		reset_macro_();
		macro_status_ = 0;
		macro_current_age_ = 0;
		macro_expected_age_ = 0;
		args[0] = 0;
		macro_line_[0] = -1;
		macro_line_[1] = 0;
		macro_line_[2] = 0;
		veejay_msg(VEEJAY_MSG_INFO, "Cleared all recorded keystrokes");
	}
	if( args[0] == 0 )
	{
		if( macro_port_ )
		{
			macro_status_ = 0; //@ stop
			veejay_msg(VEEJAY_MSG_INFO, "Stopped macro recorder");
		}
	} else if (args[0] == 1 )
	{
		reset_macro_();
		macro_port_ = vpn(VEVO_ANONYMOUS_PORT);
		macro_bank_[ current_macro_ ] = macro_port_;
		veejay_msg(VEEJAY_MSG_INFO , "Recording keystrokes!");
		macro_status_ = 1; 
		macro_line_[0] = v->settings->current_frame_num;
		macro_line_[1] = v->uc->playback_mode;
		macro_line_[2] = v->uc->sample_id;
		macro_current_age_ =0;
	} 
	else if (args[0] == 2)
	{
		if( macro_status_ == 0 && macro_port_ )
		{
			macro_status_ = 2;
			veejay_msg(VEEJAY_MSG_INFO, "Resume playing keystrokes");
		} else if( macro_line_[0] >= 0 && macro_port_ != NULL)
		{
		/*	if( macro_status_ == 1 )
			{ //@ store current speed and direction 
				char last[100];
				snprintf(last,100, "%03d:%d;",
					VIMS_VIDEO_SET_SPEED, v->settings->current_playback_speed );
				store_macro_( v, last, v->settings->current_frame_num );
			}*/
			macro_status_ = 2;
			veejay_msg(VEEJAY_MSG_INFO, "Replay all keystrokes!");
			veejay_change_playback_mode( v, macro_line_[1],macro_line_[2] );
			veejay_set_frame( v, macro_line_[0] );
			macro_expected_age_ = 0;
			replay_macro_();
		}
		else
		{
			veejay_msg(VEEJAY_MSG_INFO, "No keystrokes to playback!");
		}
	}
}

void	vj_event_stop()
{
	// destroy bundlehash, destroy keyboard_events
#ifdef HAVE_SDL
	del_all_keyb_events();
#endif

	vj_picture_free();	

	vj_event_vevo_free();

	int i;
	for( i = 0; i < 12; i ++ )
	{
		macro_port_ = macro_bank_[i];
		if(macro_port_)
		reset_macro_();
	}
}

