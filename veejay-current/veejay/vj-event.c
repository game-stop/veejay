/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
#ifdef HAVE_SDL
#include <SDL/SDL.h>
#endif
#include <stdarg.h>
#include <libhash/hash.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <libel/vj-avcodec.h>
#include <libsamplerec/samplerecord.h>
#include <utils/mpegconsts.h>
#include <utils/mpegtimecode.h>
#include <veejay/vims.h>
#include <veejay/vj-event.h>
#include <libstream/vj-tag.h>
#ifdef HAVE_V4L
#include <libstream/vj-vloopback.h>
#endif
#include <veejay/vj-plugin.h>

#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

/* Highest possible SDL Key identifier */
#define MAX_SDL_KEY	(3 * SDLK_LAST) + 1  
#define MSG_MIN_LEN	  4 /* stripped ';' */



static int _last_known_num_args = 0;
static hash_t *BundleHash;

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
} vj_keyboard_event;

static hash_t *keyboard_events;
#endif

static int _recorder_format = ENCODER_YUV420;



#define SEND_BUF 125000
static char _print_buf[SEND_BUF];
static char _s_print_buf[SEND_BUF];

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
	{ VIMS_EFFECT_SET_BG,			SDLK_b,		VIMS_MOD_ALT,	NULL	},
	{ VIMS_VIDEO_PLAY_FORWARD,		SDLK_KP6,	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PLAY_BACKWARD,		SDLK_KP4, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PLAY_STOP,			SDLK_KP5, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_SKIP_FRAME,		SDLK_KP9, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PREV_FRAME,		SDLK_KP7, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_SKIP_SECOND,		SDLK_KP8, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_PREV_SECOND,		SDLK_KP2, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_GOTO_START,		SDLK_KP1, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_GOTO_END,			SDLK_KP3, 	VIMS_MOD_NONE,	NULL	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_a,		VIMS_MOD_NONE,	"1"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_s,		VIMS_MOD_NONE,	"2"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_d,		VIMS_MOD_NONE,	"3"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_f,		VIMS_MOD_NONE,	"4"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_g,		VIMS_MOD_NONE,	"5"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_h,		VIMS_MOD_NONE,	"6"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_j,		VIMS_MOD_NONE,	"7"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_k,		VIMS_MOD_NONE,	"8"	},
	{ VIMS_VIDEO_SET_SPEED,			SDLK_l,		VIMS_MOD_NONE,	"9"	},
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
	{ 0,0,0,NULL },
};
#endif

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)			/* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)		/* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)				/* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)				/* use defaults when optional arguments are not given */			
/* the main event list */
static struct {

	const int event_id;			// VIMS id 
	const char *name;			// english text description 
	void (*function)(void *ptr, const char format[], va_list ap);
	const int num_params;			// number of arguments 
	const char *format;
	const int args[2];
	const int flags;   
} vj_event_list[] = {
	{ 0,NULL,0,0,NULL, {0,0} },
	{ VIMS_VIDEO_PLAY_FORWARD,		 "Video: play forward",
		vj_event_play_forward,		0 ,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_PLAY_BACKWARD,		 "Video: play backward",
		vj_event_play_reverse,		0 ,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_PLAY_STOP,			 "Video: play/stop",
		vj_event_play_stop,		0 ,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_SKIP_FRAME,		 "Video: skip frame forward",
		vj_event_inc_frame,		1 ,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_PREV_FRAME,		 "Video: skip frame backward",
		vj_event_dec_frame,		1 ,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_SKIP_SECOND,		 "Video: skip one second forward",
		vj_event_next_second,		1 ,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_PREV_SECOND,		 "Video: skip one second backward",
		vj_event_prev_second,		1 ,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_GOTO_START,		 "Video: go to starting position",
		vj_event_goto_start,		0 ,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_GOTO_END,			 "Video: go to ending position",
		vj_event_goto_end,		0 ,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_SET_SPEED,			 "Video: change speed to N",
		vj_event_play_speed,		1 , 	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_SET_SLOW,			 "Video: duplicate every frame N times",
		vj_event_play_slow,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_VIDEO_SET_FRAME,			 "Video: set frame",
		vj_event_set_frame,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },

	{ VIMS_CHAIN_ENTRY_UP,	 		 "ChainEntry: select next entry",
		vj_event_entry_up,    		1,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_CHAIN_ENTRY_DOWN,		 "ChainEntry: select previous entry",
		vj_event_entry_down,  		1,	"%d",		{1,0}, VIMS_ALLOW_ANY },
	{ VIMS_CHAIN_ENTRY_CHANNEL_INC,		 "ChainEntry: increment mixing channel with N",
		vj_event_chain_entry_channel_inc,1,	"%d",		{1,0}, VIMS_ALLOW_ANY }, /* uses default value if none given */
	{ VIMS_CHAIN_ENTRY_CHANNEL_DEC,		 "ChainEntry: decrement mixing channel with N",
		vj_event_chain_entry_channel_dec,1,	"%d",		{1,0}, VIMS_ALLOW_ANY }, /* uses default value if none given */
	{ VIMS_CHAIN_ENTRY_SOURCE_TOGGLE,		 "ChainEntry: set mixing source of entry N1 to source type N2",
		vj_event_chain_entry_src_toggle,2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_CHAIN_ENTRY_INC_ARG,		 "ChainEntry: increment current value N2 of parameter N1",
		vj_event_chain_arg_inc,		2,	"%d %d",	{0,1},	VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_CHAIN_ENTRY_DEC_ARG,		 "Chainentry: decrement current value N2 of parameter N1",
		vj_event_chain_arg_inc,  	2,	"%d %d",	{9,-1}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_CHAIN_ENTRY_SET_STATE,		 "ChainEntry: toggle effect on/off current entry",
		vj_event_chain_entry_video_toggle,0,	NULL,		{0,0}, VIMS_ALLOW_ANY },

	{ VIMS_CHAIN_TOGGLE,			 "Chain: Toggle effect chain on/off",
		vj_event_chain_toggle,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY },

	{ VIMS_SET_SAMPLE_START,			 "Sample: set starting frame at current position",
		vj_event_sample_start,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY   },
	{ VIMS_SET_SAMPLE_END,			 "Sample: set ending frame at current position and create new sample",
		vj_event_sample_end,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY   },
	{ VIMS_SAMPLE_SET_MARKER_START,		 "Sample: set sample N1 marker start to N2",
		vj_event_sample_set_marker_start,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_SET_MARKER_END,		 "Sample: set sample N1 marker end to N2",
		vj_event_sample_set_marker_end,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},

	{ VIMS_FXLIST_INC,		 	"EffectList: select next effect",
		 vj_event_effect_inc,		1,	"%d",		{1,0}, VIMS_ALLOW_ANY	},
	{ VIMS_FXLIST_DEC,	   	 	"EffectList: select previous effect",
		vj_event_effect_dec,		1,	"%d",		{1,0}, VIMS_ALLOW_ANY	},
	{ VIMS_FXLIST_ADD,		 	"EffectList: add selected effect on current chain enry",
		 vj_event_effect_add,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_SELECT_BANK,			 "Set sample/stream bank N",
		vj_event_select_bank,		1,	"%d",		{1,0}, VIMS_ALLOW_ANY	},
	{ VIMS_SELECT_ID,			 "Set sample/stream N of current bank",
		vj_event_select_id,		1,	"%d",		{2,0}, VIMS_ALLOW_ANY	},
	{ VIMS_SAMPLE_RAND_START,			"Start sample randomizer (0=rand duration,1=sample duration)",
		vj_event_sample_rand_start,	1,	"%d",		{0,0}, VIMS_ALLOW_ANY   },
	{ VIMS_SAMPLE_RAND_STOP,			"Stop sample randomzier",
		vj_event_sample_rand_stop,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_SAMPLE_TOGGLE_LOOP,		 "Toggle looptype to normal or pingpong",
		vj_event_sample_set_loop_type,	2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_RECORD_DATAFORMAT,		 "Set dataformat for stream/sample record",
		vj_event_tag_set_format,	1,	"%s",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	 },
	{ VIMS_REC_AUTO_START,			 "Record sample/stream and auto play after recording",
		 vj_event_misc_start_rec_auto,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_REC_START,			 "Record sample/stream start",
		vj_event_misc_start_rec,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_REC_STOP,			 "Record sample/stream stop",
		vj_event_misc_stop_rec,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_SAMPLE_NEW,			 "Sample: create new",
		vj_event_sample_new,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_PRINT_INFO,			 "Info: output sample/stream details",
		vj_event_print_info,		1,	"%d",		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_SET_PLAIN_MODE,			 "Video: set plain video mode",
		vj_event_set_play_mode,		1,	"%d",		{2,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_SET_LOOPTYPE,		 "Sample: set looptype",
		vj_event_sample_set_loop_type, 	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS  }, 
	{ VIMS_SAMPLE_SET_SPEED,			 "Sample: set speed",
		vj_event_sample_set_speed,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SET_DESCRIPTION,		"Sample: set description",
		vj_event_sample_set_descr,	2,	"%d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SET_END,			"Sample: set ending position",
		vj_event_sample_set_end, 		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SET_START,			"Sample: set starting position",
		vj_event_sample_set_start,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SET_DUP,			"Sample: set frame duplication",
		vj_event_sample_set_dup,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SET_MARKER_START,		"Sample: set marker starting position",
		vj_event_sample_set_marker_start,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_SET_MARKER_END,		"Sample: set marker ending position",
		vj_event_sample_set_marker_end,  	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_SET_MARKER,			"Sample: set marker starting and ending position",
		vj_event_sample_set_marker,	3,	"%d %d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_CLEAR_MARKER,		"Sample: clear marker",
		vj_event_sample_set_marker_clear,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
#ifdef HAVE_XML2
	{ VIMS_SAMPLE_LOAD_SAMPLELIST,		"Sample: load samples from file",
		vj_event_sample_load_list,	1,	"%s",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_SAVE_SAMPLELIST,		"Sample: save samples to file",
		vj_event_sample_save_list,	1,	"%s",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
#endif
	{ VIMS_SAMPLE_CHAIN_ENABLE,		"Sample: enable effect chain",
		vj_event_sample_chain_enable,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_CHAIN_DISABLE,		"Sample: disable effect chain",
		vj_event_sample_chain_disable,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_REC_START,			"Sample: record this sample to new",
		vj_event_sample_rec_start,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_REC_STOP,			"Sample: stop recording this sample",
		vj_event_sample_rec_stop,	  	0,	NULL,		{0,0}, VIMS_ALLOW_ANY  },
	{ VIMS_SAMPLE_DEL,			"Sample: delete",
		vj_event_sample_del,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_DEL_ALL,			"Sample: delete all",
		vj_event_sample_clear_all,	0,    	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_SAMPLE_COPY,			"Sample: copy sample <num>",
		vj_event_sample_copy,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_SAMPLE_SELECT,			"Sample: select and play a sample",
		vj_event_sample_select,		1,	"%d",		{0,0}, VIMS_ALLOW_ANY	}, // use default 
	{ VIMS_STREAM_SELECT,			"Stream: select and play a stream",
		vj_event_tag_select,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_STREAM_DELETE,			"Stream: delete",
		vj_event_tag_del,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},		
#ifdef HAVE_V4L
	{ VIMS_STREAM_NEW_V4L,			"Stream: open video4linux device (hw)",
		vj_event_tag_new_v4l,		2,	"%d %d",	{0,1}, VIMS_REQUIRE_ALL_PARAMS	},	
#endif
#ifdef SUPPORT_READ_DV2
	{ VIMS_STREAM_NEW_DV1394,		"Stream: open dv1394 device <channel>",
		vj_event_tag_new_dv1394,	 1, 	"%d",		{63,0}, VIMS_ALLOW_ANY	},
#endif
	{ VIMS_STREAM_NEW_Y4M,			"Stream: open y4m stream by name (file)",
		vj_event_tag_new_y4m,		 1,	"%s",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	}, 
	{ VIMS_STREAM_NEW_COLOR,		"Stream: new color stream by RGB color",
		vj_event_tag_new_color,		 3,     "%d %d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_RGB_PARAMETER_TYPE,		"Effect: RGB parameter type",
		vj_event_set_rgb_parameter_type, 1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	}, 
	{ VIMS_STREAM_COLOR,			"Stream: set color of a solid stream",
		vj_event_set_stream_color,	 4,	"%d %d %d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_STREAM_NEW_UNICAST,		"Stream: open network stream ",
		vj_event_tag_new_net, 		 2, 	"%d %s", 	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_NEW_MCAST,		"Stream: open multicast stream",
		vj_event_tag_new_mcast, 	 2, 	"%d %s", 	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS },	
	{ VIMS_STREAM_NEW_AVFORMAT,		"Stream: open file as stream with FFmpeg",
		vj_event_tag_new_avformat,	 1,	"%s",		{0,0}, VIMS_LONG_PARAMS	| VIMS_REQUIRE_ALL_PARAMS },
#ifdef USE_GDK_PIXBUF
	{ VIMS_STREAM_NEW_PICTURE,		"Stream: open image from file",
		vj_event_tag_new_picture,	1,	"%s",		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS },
#endif
	{ VIMS_STREAM_OFFLINE_REC_START,	"Stream: start record from an invisible stream",
		vj_event_tag_rec_offline_start,  3,	"%d %d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	}, 
	{ VIMS_STREAM_OFFLINE_REC_STOP,		"Stream: stop record from an invisible stream",
		vj_event_tag_rec_offline_stop,   0,	NULL,		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_STREAM_SET_DESCRIPTION,		"Stream: set title ",
		vj_event_tag_set_descr,		 2,	"%d %s",	{0,0}, VIMS_LONG_PARAMS  | VIMS_REQUIRE_ALL_PARAMS},
	{ VIMS_STREAM_REC_START,		"Stream: start recording from stream",
		vj_event_tag_rec_start,	 	 2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_STREAM_REC_STOP,			"Stream: stop recording from stream",
		vj_event_tag_rec_stop,		 0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_STREAM_CHAIN_ENABLE,		"Stream: enable effect chain",
		vj_event_tag_chain_enable,	 1,	"%d",		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_STREAM_CHAIN_DISABLE,		"Stream: disable effect chain",
		vj_event_tag_chain_disable,	 1,	"%d",		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_CHAIN_ENTRY_SET_EFFECT,		"ChainEntry: set effect with defaults",
		vj_event_chain_entry_set,	3,	"%d %d %d" ,	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_PRESET,		"ChainEntry: preset effect",	
		 vj_event_chain_entry_preset,   15,	"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_CHAIN_ENTRY_SET_ARG_VAL,	"ChainEntry: set parameter x value y",
		vj_event_chain_entry_set_arg_val,4,	"%d %d %d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_VIDEO_ON,	"ChainEntry: set video on entry on",
		vj_event_chain_entry_enable_video,2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_VIDEO_OFF,	"ChainEntry: set video on entry off",
		vj_event_chain_entry_disable_video,2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_DEFAULTS,	"ChainEntry: set effect on entry to defaults",
		vj_event_chain_entry_set_defaults,2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_CHANNEL,	"ChainEntry: set mixing channel on entry to",
		vj_event_chain_entry_channel,	3,	"%d %d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_SOURCE,		"ChainEntry: set mixing source on entry to",
		vj_event_chain_entry_source,	3,	"%d %d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,"ChainEntry: set mixing source and channel on",
		vj_event_chain_entry_srccha,	4,	"%d %d %d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENTRY_CLEAR,		"ChainEntry: clear entry",
		vj_event_chain_entry_del,	2,	"%d %d",	{0,-1}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_CHAIN_ENABLE,			"Chain: enable chain",
		vj_event_chain_enable,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_CHAIN_DISABLE,			"Chain: disable chain",
		vj_event_chain_disable,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_CHAIN_CLEAR,			"Chain: clear chain",
		vj_event_chain_clear,		1,	"%d",		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_CHAIN_FADE_IN,			"Chain: fade in",
		vj_event_chain_fade_in,		2,	"%d %d",	{0,0}, VIMS_ALLOW_ANY	}, 
	{ VIMS_CHAIN_FADE_OUT,			"Chain: fade out",
		vj_event_chain_fade_out,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS		},
	{ VIMS_CHAIN_MANUAL_FADE,		"Chain: set opacity",
		vj_event_manual_chain_fade,	2, 	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS 	},
	{ VIMS_CHAIN_SET_ENTRY,		"Chain: select entry ",
		vj_event_chain_entry_select,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_OUTPUT_Y4M_START,		"Output: Yuv4Mpeg start writing to file",
		vj_event_output_y4m_start,	1,	"%s",		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_OUTPUT_Y4M_STOP,		"Output: Yuv4Mpeg stop writing to file",
		vj_event_output_y4m_stop,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY },
#ifdef HAVE_SDL
	{ VIMS_RESIZE_SDL_SCREEN,		"Output: Re(initialize) SDL video screen",
		vj_event_set_screen_size,	4,	"%d %d %d %d", 	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
#endif
	{ VIMS_SET_PLAY_MODE,			"Playback: switch playmode sample/tag/plain",
		vj_event_set_play_mode,		1,	"%d",		{2,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SET_MODE_AND_GO,		"Playback: set playmode (and fire sample/tag)",
		vj_event_set_play_mode_go,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SWITCH_SAMPLE_STREAM,		"Playback: switch between samples/tags",
		vj_event_switch_sample_tag,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_AUDIO_DISABLE,			"Playback: disable audio",
		vj_event_disable_audio,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY   },
	{ VIMS_AUDIO_ENABLE,			"Playback: enable audio",
		vj_event_enable_audio,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
	{ VIMS_EDITLIST_PASTE_AT,		"EditList: Paste frames from buffer at frame",
		vj_event_el_paste_at,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_EDITLIST_CUT,			"EditList: Cut frames n1-n2 to buffer",
		vj_event_el_cut,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS  },
	{ VIMS_EDITLIST_COPY,			"EditList: Copy frames n1-n2 to buffer",
		vj_event_el_copy,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_EDITLIST_CROP,			"EditList: Crop frames n1-n2",
		vj_event_el_crop,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS   },
	{ VIMS_EDITLIST_DEL,			"EditList: Del frames n1-n2",
		vj_event_el_del,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	}, 
	{ VIMS_EDITLIST_SAVE,			"EditList: save EditList to new file",
		vj_event_el_save_editlist,	3,	"%d %d %s",	{0,0}, VIMS_LONG_PARAMS  | VIMS_ALLOW_ANY },
	{ VIMS_EDITLIST_LOAD,			"EditList: load EditList into veejay",
		vj_event_el_load_editlist,	1,	"%s",		{0,0}, VIMS_LONG_PARAMS	| VIMS_REQUIRE_ALL_PARAMS},
	{ VIMS_EDITLIST_ADD,			"EditList: add video file to editlist",
		vj_event_el_add_video,		1,	"%s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_EDITLIST_ADD_SAMPLE,		"EditList: add video file to editlist as new sample or append",
		vj_event_el_add_video_sample,	2,	"%d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_STREAM_LIST,			"Stream: send list of all streams",
		vj_event_send_tag_list,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_LIST,			"Sample: send list of Samples",
		vj_event_send_sample_list,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
	{ VIMS_SAMPLE_INFO,			"Sample: send sample information (timecode and title)",
		vj_event_send_sample_info,	2,	"%d %d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_SAMPLE_OPTIONS,			"Sample: send sample options",
		vj_event_send_sample_options,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_EDITLIST_LIST,			"EditList: send list of all files",
		vj_event_send_editlist,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY   },
	{ VIMS_BUNDLE,				"Bundle: execute collection of messages",
		vj_event_do_bundled_msg,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
#ifdef HAVE_XML2
	{ VIMS_BUNDLE_FILE,			"Veejay load configuartion file",
		vj_event_read_file,		1,	"%s",		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS	},
#endif
	{ VIMS_BUNDLE_DEL,			"Bundle: delete a bundle of messages",
		vj_event_bundled_msg_del,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS	},
#ifdef HAVE_XML2
	{ VIMS_BUNDLE_SAVE,			"Veejay save configuration file",
		vj_event_write_actionfile,	2,	"%d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_ALLOW_ANY  },
#endif
	{ VIMS_BUNDLE_LIST,			"Bundle: get all contents",
		vj_event_send_bundles,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_VIMS_LIST,			"VIMS: send whole event list",
		vj_event_send_vimslist,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_BUNDLE_ADD,			"Bundle: add a new bundle to event system",
		vj_event_bundled_msg_add,	2,	"%d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS		},
	{ VIMS_BUNDLE_CAPTURE,			"Bundle: capture effect chain",
		vj_event_quick_bundle,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_LOG,				"Send console output",
		vj_event_send_log,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_CHAIN_LIST,			"Chain: get all contents",
		vj_event_send_chain_list,	1,	"%d",		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_CHAIN_GET_ENTRY,			"Chain: get entry contents",
		vj_event_send_chain_entry,	2,	"%d %d",	{0,-1}, VIMS_ALLOW_ANY		},		
	{ VIMS_EFFECT_LIST,			"EffectList: list all effects",
		vj_event_send_effect_list,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY		},
	{ VIMS_VIDEO_INFORMATION,		"Video: send properties",
		vj_event_send_video_information,0,	NULL,		{0,0}, VIMS_ALLOW_ANY	},
#ifdef HAVE_SDL
	{ VIMS_BUNDLE_ATTACH_KEY,		"Attach/Detach a Key to VIMS event",
		vj_event_attach_detach_key,	4,	"%d %d %d %s",	{0,0}, VIMS_ALLOW_ANY 	},
#endif
#ifdef USE_GDK_PIXBUF
	{ VIMS_SCREENSHOT,			"Various: Save image to file",
		vj_event_screenshot,		3,	"%d %d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS  },
	{ VIMS_RGB24_IMAGE,			"Various: get a (scaled) image from veejay in rgb24",
		vj_event_get_scaled_image,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
#else
#ifdef HAVE_JPEG
	{ VIMS_SCREENSHOT,			"Various: Save file to jpeg",
		vj_event_screenshot,		3,	"%d %d %s",	{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS },
#endif
#endif
	{ VIMS_CHAIN_TOGGLE_ALL,		"Toggle Effect Chain on all samples or streams",
		vj_event_all_samples_chain_toggle,1,	"%d",		{0,0} , VIMS_REQUIRE_ALL_PARAMS  },
	{ VIMS_SAMPLE_UPDATE,			"Sample: Update starting and ending position by offset",
		vj_event_sample_rel_start,	3,	"%d %d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS	},	 
#ifdef HAVE_V4L
	{ VIMS_STREAM_SET_BRIGHTNESS,		"Video4Linux: set v4l brightness value",
		vj_event_v4l_set_brightness,	2,	"%d %d",	{0,0}, 	VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_SET_CONTRAST,		"Video4Linux: set v4l contrast value",
		vj_event_v4l_set_contrast,	2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_SET_HUE,			"Video4Linux: set v4l hue value",
		vj_event_v4l_set_hue,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_SET_COLOR,			"Video4Linux: set v4l color value",
		vj_event_v4l_set_color,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_SET_WHITE,			"Video4Linux: set v4l white value",
		vj_event_v4l_set_white,		2,	"%d %d",	{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_STREAM_GET_V4L,			"Video4Linux: get properties",
		vj_event_v4l_get_info,		2,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_VLOOPBACK_START,			"Vloopback: start pipe (device num)",
		vj_event_vloopback_start,	1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_VLOOPBACK_STOP,			"Vloopback: stop",
		vj_event_vloopback_stop,	0,	NULL,		{0,0}, VIMS_ALLOW_ANY }, 
#endif
	{ VIMS_EFFECT_SET_BG,			"Effect: set background (if supported)",
		vj_event_effect_set_bg,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_QUIT,				"Quit veejay",
		vj_event_quit,			0, 	NULL, 		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_SET_VOLUME,			"Veejay set audio volume",
		vj_event_set_volume,		1,	"%d",		{0,0}, VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_SUSPEND,				"Suspend veejay",
		vj_event_suspend,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_DEBUG_LEVEL,			"Toggle more/less debugging information",
		vj_event_debug_level,		0,	 NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_BEZERK,				"Toggle bezerk mode",
		vj_event_bezerk,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_SAMPLE_MODE,			"Toggle between box or triangle filter for 2x2 -> 1x1 sampling",
		vj_event_sample_mode, 		0, 	NULL, 		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_CMD_PLUGIN,			"Send a command to the plugin",
		vj_event_plugin_command,	1, 	"%s", 		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS },
	{ VIMS_LOAD_PLUGIN,			"Load a plugin from disk",
		vj_event_load_plugin, 		1, 	"%s", 		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS},
	{ VIMS_UNLOAD_PLUGIN,			"Unload a plugin from disk",
		vj_event_unload_plugin, 	1, 	"%s", 		{0,0}, VIMS_LONG_PARAMS | VIMS_REQUIRE_ALL_PARAMS},

	{ VIMS_VIDEO_MCAST_START,		"Start mcast frame sender",
		vj_event_mcast_start,		0,	NULL,		{0,0}, VIMS_ALLOW_ANY },
	{ VIMS_VIDEO_MCAST_STOP,		"Stop mcast frame sender",
		vj_event_mcast_stop,		0,	NULL,		{0,0},	VIMS_ALLOW_ANY },
	{ VIMS_GET_FRAME,			"Send a frame to the client",
		vj_event_send_frame, 		0,	NULL, 		{0,0}, VIMS_ALLOW_ANY },
#ifdef HAVE_SDL
	{ VIMS_FULLSCREEN,			"Fullscreen video toggle",
		vj_event_fullscreen,		1, 	"%d" , 		{0,0}, VIMS_ALLOW_ANY},
#endif
	{ 0,NULL,0,0,NULL,{0,0}},
};


#define FORMAT_MSG(dst,str) sprintf(dst,"%03d%s",strlen(str),str)
#define APPEND_MSG(dst,str) strncat(dst,str,strlen(str))
#define SEND_MSG_DEBUG(v,str) veejay_msg(VEEJAY_MSG_INFO, "[%d][%s]",strlen(str),str)
#define SEND_MSG(v,str)\
{\
vj_server_send(v->vjs[0], v->uc->current_link, str, strlen(str));\
}
#define RAW_SEND_MSG(v,str,len)\
{\
vj_server_send(v->vjs[0],v->uc->current_link, str, len );\
}	

#define SEND_LOG_MSG(v,str)\
{\
vj_server_send(v->vjs[3], v->uc->current_link,str,strlen(str));\
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


static inline hash_val_t int_bundle_hash(const void *key)
{
	return (hash_val_t) key;
}

static inline int int_bundle_compare(const void *key1,const void *key2)
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
vj_keyboard_event *new_keyboard_event(
	int symbol, int modifier, const char *value, int event_id );
vj_keyboard_event *get_keyboard_event( int id );
int	keyboard_event_exists(int id);
int	del_keyboard_event(int id );
char *find_keyboard_default(int id);

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
vj_event	vj_event_function_by_id(int id);
const int vj_event_get_id(int event_id);
const char  *vj_event_name_by_id(int id);
void  vj_event_parse_bundle(veejay_t *v, char *msg );
int	vj_has_video(veejay_t *v);
void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen);

void    vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod);
#ifdef HAVE_SDL
static void vj_event_get_key( int event_id, int *key_id, int *key_mod );
void vj_event_single_fire(void *ptr , SDL_Event event, int pressed);
int vj_event_register_keyb_event(int event_id, int key_id, int key_mod, const char *args);
void vj_event_unregister_keyb_event(int key_id, int key_mod);
#endif

#ifdef HAVE_XML2
void    vj_event_format_xml_event( xmlNodePtr node, int event_id );
void	vj_event_format_xml_stream( xmlNodePtr node, int stream_id );
#endif
void	vj_event_init(void);

int	vj_has_video(veejay_t *v)
{
	if(v->current_edit_list->video_frames >= 1 )
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

int			del_keyboard_event(int id )
{
	hnode_t *node;
	vj_keyboard_event *ev = get_keyboard_event( id );
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
//(int event_id, int symbol, int modifier, const char *value)

vj_keyboard_event *new_keyboard_event(
		int symbol, int modifier, const char *value, int event_id )
{
	int vims_id = vj_event_get_id( event_id );

	if(vims_id == 0)
	{
		if(!vj_event_bundle_exists( event_id ))
		{
			veejay_msg(VEEJAY_MSG_ERROR,
				"VIMS %d does not exist", event_id);
			return NULL;
		}
	}

	if( event_id <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR,
		 "VIMS event %d does not exist", event_id );
		return NULL;
	}

	vj_keyboard_event *ev = (vj_keyboard_event*)vj_malloc(sizeof(vj_keyboard_event));
	if(!ev)
		return NULL;
	memset( ev, 0, sizeof(vj_keyboard_event));

	ev->vims = (vj_events*) vj_malloc(sizeof(vj_events));
	if(!ev->vims)
		return NULL;
	memset(ev->vims, 0, sizeof(vj_events));



	if(value)
	{
		ev->arg_len = strlen(value);
		ev->arguments = strndup( value, ev->arg_len );
	}
	else
	{
		if(event_id <= VIMS_BUNDLE_START || event_id > VIMS_BUNDLE_END)
		{
			ev->arguments = find_keyboard_default( event_id );
			if(ev->arguments)
				ev->arg_len = strlen(ev->arguments);
		}	
	}

	if(vims_id != 0 )
	{
		ev->vims->act = vj_event_list[ vims_id ].function;
		ev->vims->list_id  = event_id;
	}
	else
	{
		// bundle!
		ev->vims->act = vj_event_none;
		ev->vims->list_id = event_id;
	}
	ev->key_symbol = symbol;
	ev->key_mod = modifier;

	return ev;
}


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
		veejay_msg(VEEJAY_MSG_ERROR, "Doesnt make sense to store empty bundles in memory");
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
	m->bundle = (char*) malloc(sizeof(char) * len+1);
	bzero(m->bundle, len+1);
	m->accelerator = 0;
	m->modifier = 0;
	if(!m->bundle)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating memory for bundled message context");
		return NULL;
	}
	strncpy(m->bundle, bundle_msg, len);
	
	m->event_id = event_id;

	veejay_msg(VEEJAY_MSG_DEBUG, 
		"New VIMS Bundle %d [%s]",
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

vj_event vj_event_function_by_id(int id) 
{
	vj_event function;
	int i;
	for(i=1; vj_event_list[i].name; i++)
	{
		if( id == vj_event_list[i].event_id ) 	
		{
			function = vj_event_list[i].function;
			return function;
		}
	}
	function = vj_event_none;
	return function;
}

const int vj_event_get_id(int event_id)
{
	int i;
	for(i=1; vj_event_list[i].name != NULL; i++)
	{
		if( vj_event_list[i].event_id == event_id ) return i;
	}
	return 0;
}

const char  *vj_event_name_by_id(int id)
{
	int i;
	for(i=1; vj_event_list[i].name != NULL; i++)
	{
		if(id == vj_event_list[i].event_id)
		{
			return vj_event_list[i].name;
		}
	}
	return NULL;
}

/* error statistics struct */


int vj_event_parse_msg(veejay_t *v, char *msg);

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
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) '{' expected. Skipping message [%s]",msg);
			return;
		}	

		offset+=1;	/* skip # */

		for( i = 1; i <= num_msg ; i ++ ) /* iterate through message bundle and invoke parse_msg */
		{				
			char atomic_msg[256];
			int found_end_of_msg = 0;
			int total_msg_len = strlen(msg);
			bzero(atomic_msg,256);
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
					strncpy(atomic_msg, msg+offset, (found_end_of_msg-offset));
					atomic_msg[ (found_end_of_msg-offset) ] ='\0';
					offset += j + 1;
					j = 0;
					vj_event_parse_msg( v, atomic_msg );
				}
				j++;
			}
		}
	}
}

void vj_event_dump()
{

	int i;
	for(i=0; i < VIMS_MAX; i++)
	{
	   if(net_list[i].list_id>0)
	   {	
	   int id = net_list[i].list_id;
	   //int id = i;
	   printf("\t%s\n", vj_event_list[id].name);
	   printf("\t%03d\t\t%s\n\n", i , vj_event_list[id].format);
	   }
	}
	vj_osc_dump();
}

void vj_event_print_range(int n1, int n2)
{
	int i;
	for(i=n1; i < n2; i++)
	{
		if(net_list[i].list_id > 0)
		{
			int id = net_list[i].list_id;
			if(vj_event_list[id].name != NULL )
			{
				veejay_msg(VEEJAY_MSG_INFO,"%03d\t\t%s\t\t%s",
					i, vj_event_list[id].format,vj_event_list[id].name);
			}
		}
	}
}


int vj_event_get_num_args(int net_id)
{
	int id = net_list[ net_id ].list_id;
	return (int) vj_event_list[id].num_params;	
}

typedef struct
{
	void *value;
} vims_arg_t; 

void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen)
{
	int id = net_list[ net_id ].list_id;
	
	int		argument_list[16];
	vims_arg_t	vims_arguments[16];

	if(id <= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) VIMS %d is not known", net_id );
		return;
	}
	
	memset( argument_list, 0, 16 );
	memset( vims_arguments,0, 16 );

	if( vj_event_list[id].num_params <= 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) %s single fire %d", __FUNCTION__, net_id);
		_last_known_num_args = 0;
		vj_event_trigger_function( (void*) v, net_list[net_id].act,
			vj_event_list[id].num_params, vj_event_list[id].format,
			&(vj_event_list[id].args[0]), &(vj_event_list[id].args[1]));
		return;
	}
	else
	{
		// order arguments
		char *f = (char*) vj_event_list[id].format;
		int i;
		int fmt_offset = 1;
		int flags = vj_event_list[id].flags;
		if( arglen >  vj_event_list[id].num_params)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) %d too many arguments : %d (only %d)",
				net_id, arglen, vj_event_list[id].num_params );
			return;
		}

		for( i = 0; i < arglen; i ++ )
		{
			if(f[fmt_offset] == 'd' )
				vims_arguments[i].value = &(args[i]);
			if(f[fmt_offset] == 's' )
			{
				if( str_arg == NULL && (flags & VIMS_REQUIRE_ALL_PARAMS))
				{
					veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) %d requires string",
						net_id);
					return;
				}
				int len = strlen( str_arg );
				vims_arguments[i].value = strndup( str_arg, len );
			}	
			fmt_offset += 3;
		}
		if( arglen < vj_event_list[id].num_params)
		{
			if(flags & VIMS_ALLOW_ANY)
			{
				int n;
				veejay_msg(VEEJAY_MSG_WARNING, "(VIMS) %d uses default values", net_id);
				for(n = arglen; n < vj_event_list[id].num_params; n ++ )
				{
					if(n < 2 )
						vims_arguments[i].value = (void*) &(vj_event_list[id].args[n]);
					else
						vims_arguments[i].value = &(args[n]);
				} 
			}
			if(flags & VIMS_REQUIRE_ALL_PARAMS)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) %d requires all parameters",
					net_id);
				return;
			}
		}

		vj_event_trigger_function( (void*) v, net_list[net_id].act,
			vj_event_list[id].num_params, vj_event_list[id].format,
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
			vims_arguments[15].value ); 
	
		fmt_offset =1 ;
		for( i = 0; i < vj_event_list[id].num_params; i ++ )
		{
			if( vims_arguments[i].value != NULL &&
				f[fmt_offset] == 's' )
				free(vims_arguments[i].value);
			fmt_offset += 3;
		}
	
	}	
}

int vj_event_parse_msg(veejay_t *v, char *msg)
{
	char args[150];
	int net_id=0;
	
	int msg_len = strlen(msg);
	char *tmp = NULL;
	int id = 0;
	bzero(args,150);  

	/* remove newline */
	veejay_chomp_str( msg, &msg_len );
	msg_len --;

	/* message is at least 5 bytes in length */
	if( msg == NULL || msg_len < MSG_MIN_LEN)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Message [%s] too small (only %d bytes, need at least %d)",
			(msg==NULL? "(Empty)": msg), msg_len, MSG_MIN_LEN);
		return 0;
	}

	tmp = strndup( msg, 3 );
	if( strncasecmp( tmp, "bun", 3) == 0 )
	{
		vj_event_parse_bundle(v,msg);
		return 1;
	} 
	
	if( sscanf(tmp, "%03d", &net_id ) != 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Selector '%s' is not a number!", tmp);
		return 0;
	}
	free(tmp);

	if( net_id < 0 || net_id >= VIMS_MAX)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) '%d' is not a valid VIMS selector", net_id);
		return 0;
	}

	/* 4th position is ':' */
	if( msg[3] != 0x3a || msg[msg_len] != ';' )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Syntax error, use \"<VIMS selector>:<arguments>;\" ");
		veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) [%s] : '%c' (%x) , '%c' (%x)", msg,
			msg[3],msg[3], msg[msg_len],msg[msg_len] );
		return 0;
	}

	//msg[msg_len] = '\0'; // null terminate (uses semicolon position)

	if( net_list[ net_id ].list_id == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Event %d not known", net_id );
		return 0;
	}
	
	id =  net_list[net_id].list_id;

	if( id >= VIMS_BUNDLE_START && id < VIMS_BUNDLE_END )
	{
		vj_msg_bundle *bun = vj_event_bundle_get(id );
		if(!bun)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) internal error: Bundle %d was registered but is not present in Hash");
			return 0;
		}
		vj_event_parse_bundle( v, bun->bundle );
		return 1;
	}


	if( msg_len <= MSG_MIN_LEN )
	{	
//		veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) Single fire %d", net_id);
		_last_known_num_args = vj_event_list[id].num_params; 

		vj_event_trigger_function(
			(void*)v,net_list[net_id].act,
			vj_event_list[id].num_params,
			vj_event_list[id].format,
			&(vj_event_list[id].args[0]),
			&(vj_event_list[id].args[1])
		);	
	}
	if( msg_len > MSG_MIN_LEN)
	{
		const char *fmt = vj_event_list[id].format;
		const int   np  = vj_event_list[id].num_params;
		int		fmt_offset = 1; // fmt offset
		int 		i;
		int		offset = 0;  // arguments offset
		int		num_array[16];
		char		*arguments;

	
		arguments = strndup( (msg + 4) , (msg_len -4) );

		if( arguments == NULL )
		{	
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) %d requires %d arguments but none were given",
				net_id, np );
			return 0;
		} 
		if( np <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) %d accepts no arguments", net_id); 
			if(arguments)
				free( (void*) arguments);
			return 0; // Ffree mem
		}
	
		vims_arg_t	vims_arguments[16];
		memset( vims_arguments, 0, sizeof(vims_arguments) );
		memset( num_array, 0, sizeof(num_array));
		int flags = vj_event_list[id].flags;

		for( i = 0; i < np; i ++ )
		{
			int failed_arg = 0;
			int type = 0;

			if(offset >= (msg_len - 4))
			{
				break;
			}

			if( fmt[fmt_offset] == 's' )
			{		
				type = 1;
				if( (flags & VIMS_LONG_PARAMS) ) /* copy rest of message */
				{
					int str_len = 0;
					vims_arguments[i].value = (void*) strndup( arguments+offset, msg_len - offset );
					str_len = strlen( (char*) vims_arguments[i].value );
					if(str_len < 1 )
						failed_arg ++;
					else 
						offset += str_len;
				}
				if( !(flags & VIMS_LONG_PARAMS) )
				{
					char tmp_str[256];
					bzero(tmp_str,256);
					int n = sscanf( arguments + offset, "%s", tmp_str );
					if( n <= 0 )
						failed_arg ++;
					else
					{
						int str_len = strlen( tmp_str );
						offset += str_len;
						vims_arguments[i].value = (void*) strndup( tmp_str, str_len );
					}
				}
			}
			if( fmt[fmt_offset] == 'd' )
			{
				char longest_num[16];
				bzero(longest_num,16);
				long tmp_val =0;
				type = 2;
				if(sscanf( arguments+offset, "%ld", &tmp_val) <= 0 )
					failed_arg ++;
				else
				{
					sprintf(longest_num, "%ld", tmp_val );
					offset += strlen( longest_num ); 
					if( arguments[offset] != 0x20 && arguments[offset] != 0x0)
						failed_arg++;
					else
					{
						num_array[i] = (int)tmp_val;
						offset ++;
						vims_arguments[i].value = &(num_array[i]); 
					}
				}
			}
		
			if( flags & VIMS_REQUIRE_ALL_PARAMS && failed_arg > 0)
			{
				if(type == 0 )
					veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) %d argument %d is of invalid type",
						net_id,i);
				if(type == 2)  
					veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) %d argument %d must be a number",
						net_id,i);
				if(type == 1)
					veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) %d argument %d must be a string",
						net_id,i);
				if(arguments)
					free( (void*)arguments);
				return 0;
			}

			if( failed_arg > 0 )
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "(VIMS) %d argument %d not specified, using default %d",
					net_id, i , (i < 2 ? vj_event_list[id].args[i]: num_array[i]));
				if(i<2)
				{	
					vims_arguments[i].value = (void*) &(vj_event_list[id].args[i]);
				}
				else
				{
					int zero = 0;
					if( fmt[fmt_offset] == 'd' )
						vims_arguments[i].value = (void*) &zero;
					else
						vims_arguments[i].value = NULL;
				}
			}
			fmt_offset += 3;	
		}

		while( i < np)
		{
			int zero = 0;
			if( fmt[fmt_offset] == 'd' )
				vims_arguments[i].value = (void*) &zero;
			else
				vims_arguments[i].value = NULL;
			i++;
			fmt_offset += 3;
		}


		// should be 'I' ?
		_last_known_num_args = i;

		vj_event_trigger_function(
					(void*)v,
					net_list[net_id].act,
					np,
					vj_event_list[id].format,
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
		if(arguments)
			free( (void*)arguments);
	}
	else
		_last_known_num_args = 0;
	return 1;
}	

/*
	update connections
 */
void vj_event_update_remote(void *ptr)
{
	veejay_t *v = (veejay_t*)ptr;
	int cmd_poll = 0;	// command port
	int sta_poll = 0;	// status port
	int new_link = -1;
	int sta_link = -1;
	int msg_link = -1;
	int msg_poll = 0;
	int i;
	cmd_poll = vj_server_poll(v->vjs[0]);
	sta_poll = vj_server_poll(v->vjs[1]);
	msg_poll = vj_server_poll(v->vjs[3]);
	// accept connection command socket    

	if( cmd_poll > 0)
	{
		new_link = vj_server_new_connection ( v->vjs[0] );
	}
	// accept connection on status socket
	if( sta_poll > 0) 
	{
		sta_link = vj_server_new_connection ( v->vjs[1] );
	}
	if( msg_poll > 0)
	{
		msg_link = vj_server_new_connection( v->vjs[3] );
	}
	// see if there is any link interested in status information
	for( i = 0; i < v->vjs[1]->nr_of_links; i ++ )
		veejay_pipe_write_status( v, i );

	if( v->settings->use_vims_mcast )
	{
		int res = vj_server_update(v->vjs[2],0 );
		if(res > 0)
		{
			v->uc->current_link = 0;
			char buf[MESSAGE_SIZE];
			bzero(buf, MESSAGE_SIZE);
			while( vj_server_retrieve_msg( v->vjs[2], 0, buf ) )
			{
				vj_event_parse_msg( v, buf );
				bzero( buf, MESSAGE_SIZE );
			}
		}
		
	}

	// see if there is anything to read from the command socket
	for( i = 0; i < v->vjs[0]->nr_of_links; i ++ )
	{
		int res = vj_server_update(v->vjs[0],i );
		if(res > 0)
		{
			v->uc->current_link = i;
			char buf[MESSAGE_SIZE];
			bzero(buf, MESSAGE_SIZE);
			while( vj_server_retrieve_msg( v->vjs[0], i, buf ) )
			{
				vj_event_parse_msg( v, buf );
				bzero( buf, MESSAGE_SIZE );
			}
		}
		if(res==-1)
		{
			_vj_server_del_client( v->vjs[0], i );
			_vj_server_del_client( v->vjs[1], i );
			_vj_server_del_client( v->vjs[3], i );
		}
	}

	if(!veejay_keep_messages())
		veejay_reap_messages();
	

}

void	vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod)
{
	char bundle[4096];
	bzero(bundle,4096);
	vj_event_create_effect_bundle(v, bundle, key_num, key_mod );
}

#ifdef HAVE_SDL
void vj_event_single_fire(void *ptr , SDL_Event event, int pressed)
{
	
	SDL_KeyboardEvent *key = &event.key;
	SDLMod mod = key->keysym.mod;
	
	int vims_mod = 0;

	if( (mod & KMOD_LSHIFT) || (mod & KMOD_RSHIFT ))
		vims_mod = VIMS_MOD_SHIFT;
	if( (mod & KMOD_LALT) || (mod & KMOD_ALT) )
		vims_mod = VIMS_MOD_ALT;
	if( (mod & KMOD_CTRL) || (mod & KMOD_CTRL) )
		vims_mod = VIMS_MOD_CTRL;

	int vims_key = key->keysym.sym;
	int index = vims_mod * SDLK_LAST + vims_key;

//	vj_keyboard_event *ev = &keyboard_events[index];
	vj_keyboard_event *ev = get_keyboard_event( index );
	if(!ev )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Keyboard event %d unknown", index );
		return;
	}

	// event_id is here VIMS list entry!
	int event_id = ev->vims->list_id;

	veejay_msg(VEEJAY_MSG_DEBUG, "Key fires event %d on symbol %d ?", event_id, vims_key );
	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		vj_msg_bundle *bun = vj_event_bundle_get(event_id );
		if(!bun)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Requested BUNDLE %d does not exist", event_id);
			return;
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "Keyboard fires Bundle %d", event_id);
		/*
			parse_bundle calls vj_event_parse_msg :)
			it should be possible to trigger a set of bundles with a bundle,
			but this needs testing.
		*/

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
		veejay_msg(VEEJAY_MSG_DEBUG, "Keyboard fires Event %d [%s]", event_id,
			msg );
		vj_event_parse_msg( (veejay_t*) ptr, msg );
	}

}

#endif


void vj_event_none(void *ptr, const char format[], va_list ap)
{
	veejay_msg(VEEJAY_MSG_DEBUG, "No event attached on this key");
}

#ifdef HAVE_XML2

// parsing of XML files can be handled in a general way
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
		strncpy( dst, t, strlen(t) );	
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
#define XML_CONFIG_SETTING_EDITLIST   "edit_list"

#define __xml_cint( buf, var , node, name )\
{\
sprintf(buf,"%d", var);\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );\
}

#define __xml_cfloat( buf, var , node, name )\
{\
sprintf(buf,"%f", var);\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );\
}

#define __xml_cstr( buf, var , node, name )\
{\
if(var != NULL){\
strncpy(buf,var,strlen(var));\
xmlNewChild(node, NULL, (const xmlChar*) name, (const xmlChar*) buf );}\
}


void	vj_event_format_xml_settings( veejay_t *v, xmlNodePtr node  )
{
	char buf[4069];
	bzero(buf,4069);
	int c = veejay_is_colored();
	__xml_cint( buf, v->uc->port, node, 	XML_CONFIG_SETTING_PORTNUM );
	__xml_cint( buf, v->video_out, node, 	XML_CONFIG_SETTING_PRIOUTPUT);
	__xml_cstr( buf, v->stream_outname,node,XML_CONFIG_SETTING_PRINAME );
	__xml_cint( buf, v->bes_width,node,	XML_CONFIG_SETTING_SDLSIZEX );
	__xml_cint( buf, v->bes_height,node,	XML_CONFIG_SETTING_SDLSIZEY );
	__xml_cint( buf, v->audio,node,		XML_CONFIG_SETTING_AUDIO );
	__xml_cint( buf, v->sync_correction,node,	XML_CONFIG_SETTING_SYNC );
	__xml_cint( buf, v->uc->use_timer,node,		XML_CONFIG_SETTING_TIMER );
	__xml_cint( buf, v->real_fps,node,		XML_CONFIG_SETTING_FPS );
	__xml_cint( buf, v->uc->geox,node,		XML_CONFIG_SETTING_GEOX );
	__xml_cint( buf, v->uc->geoy,node,		XML_CONFIG_SETTING_GEOY );
	__xml_cint( buf, v->no_bezerk,node,		XML_CONFIG_SETTING_BEZERK );
	__xml_cint( buf, c,node, XML_CONFIG_SETTING_COLOR );
	__xml_cint( buf, v->pixel_format,node,	XML_CONFIG_SETTING_YCBCR );
	__xml_cint( buf, v->video_output_width,node, XML_CONFIG_SETTING_WIDTH );
	__xml_cint( buf, v->video_output_height,node, XML_CONFIG_SETTING_HEIGHT );
	__xml_cfloat( buf,v->dummy->fps,node,	XML_CONFIG_SETTING_DFPS ); 
	__xml_cint( buf, v->dummy->norm,node,	XML_CONFIG_SETTING_NORM );
	__xml_cint( buf, v->dummy->active,node,	XML_CONFIG_SETTING_DUMMY );
	__xml_cint( buf, v->settings->use_mcast,node, XML_CONFIG_SETTING_MCASTOSC );
	__xml_cint( buf, v->settings->use_vims_mcast,node, XML_CONFIG_SETTING_MCASTVIMS );
	__xml_cint( buf, v->settings->zoom ,node,	XML_CONFIG_SETTING_SCALE );
	__xml_cfloat( buf, v->settings->output_fps, node, XML_CONFIG_SETTING_FPS );
	__xml_cint( buf, v->uc->playback_mode, node, XML_CONFIG_SETTING_PMODE );
	__xml_cint( buf, v->uc->sample_id, node, XML_CONFIG_SETTING_PID );
#ifdef HAVE_SDL
#endif


}

void	vj_event_xml_parse_config( veejay_t *v, xmlDocPtr doc, xmlNodePtr cur )
{
	// should be called at malloc, dont change setttings when it runs!
	if( veejay_get_state(v) != LAVPLAY_STATE_STOP)
		return;

	int c = 0;
	char sample_list[1024];
	bzero(sample_list, 1024 );
	// todo: editlist loading ; veejay restart

	while( cur != NULL )
	{
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SAMPLELIST, sample_list );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PORTNUM, &(v->uc->port) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PRIOUTPUT, &(v->video_out) );
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_PRINAME, v->stream_outname);
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SDLSIZEX, &(v->bes_width) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SDLSIZEY, &(v->bes_height) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_AUDIO, &(v->audio) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SYNC, &(v->sync_correction) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_TIMER, &(v->uc->use_timer) );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_FPS, &(v->real_fps) );
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
		cur = cur->next;
	}

	veejay_set_colors( c );
	if(sample_list)
	{
		v->settings->action_scheduler.sl = strdup( sample_list );
		veejay_msg(VEEJAY_MSG_DEBUG, "Scheduled '%s' for restart", sample_list );
	}
	v->settings->action_scheduler.state = 1;
}

void	vj_event_xml_parse_stream( veejay_t *v, xmlDocPtr doc, xmlNodePtr cur )
{
	if( veejay_get_state(v) != LAVPLAY_STATE_STOP)
		return;
	xmlNodePtr fxchain = NULL;
	int type = -1;
	int channel = 0;
	char source_name[150];
	char file_name[1024];
	char color[20];
	bzero(source_name,150);
	bzero(file_name, 1024 );
	while( cur != NULL )
	{
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_STREAM_TYPE, &type );
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_STREAM_SOURCE, source_name);
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_STREAM_FILENAME, file_name );
		get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_STREAM_COLOR, color );
		get_istr( doc, cur, (const xmlChar*) XML_CONFIG_STREAM_OPTION, &channel );
		if(!xmlStrcmp( cur->name, (const xmlChar*) XML_CONFIG_STREAM_CHAIN ))
			fxchain = cur; 	
		cur = cur->next;
	}
	if(type != -1 )
	{
		int id = veejay_create_tag( v, type, file_name, v->nstreams, 0,channel );

		if(id > 0 )
		{
			vj_tag_set_description( id, source_name );
			int rgb[3] = {0,0,0};
			sscanf( color, "%03d %03d %03d", &rgb[0],&rgb[1], &rgb[2] );
			if(type == VJ_TAG_TYPE_COLOR )
				vj_tag_set_stream_color( id, rgb[0],rgb[1],rgb[2] );
			if(fxchain != NULL )
				tagParseStreamFX( doc, fxchain->xmlChildrenNode, vj_tag_get( id ));
		}
	}

}

// not only for keyboard, also check if events in the list exist
void vj_event_xml_new_keyb_event( void *ptr, xmlDocPtr doc, xmlNodePtr cur )
{
	int key = 0;
	int key_mod = 0;
	int event_id = 0;	
	
	if( veejay_get_state( (veejay_t*) ptr ) == LAVPLAY_STATE_STOP )
	{
		return;
	}
	char msg[4096];
	bzero( msg,4096 );

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
		veejay_msg(VEEJAY_MSG_DEBUG, "Invalid event_id in configuration file used ?!");
		return;
	}

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		if( vj_event_bundle_exists(event_id))
		{
			if(!override_keyboard)
			{
				veejay_msg(VEEJAY_MSG_WARNING, "Bundle %d already exists in VIMS system! (Bundle in configfile was ignored)",event_id);
				return;
			}
			else
			{
				if(vj_event_bundle_del(event_id) != 0)
					return;
			}
		}

		vj_msg_bundle *m = vj_event_bundle_new( msg, event_id);
		if(!msg)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to create new Bundle %d - [%s]", event_id, msg );
			return;
		}
		if(!vj_event_bundle_store(m))
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "%s Error storing newly created bundle?!", __FUNCTION__);
			return;
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "Added bundle %d", event_id);
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
int  veejay_finish_action_file( void *ptr, char *file_name )
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	veejay_t *v = (veejay_t*) ptr;
	if(!file_name)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid filename");
		return 0;
	}

	doc = xmlParseFile( file_name );

	if(doc==NULL)	
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot read file %s", file_name );
		return 0;
	}

	cur = xmlDocGetRootElement( doc );
	if( cur == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "This is not a XML document");
		xmlFreeDoc(doc);
		return 0;
	}
	if( xmlStrcmp( cur->name, (const xmlChar *) XML_CONFIG_FILE))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "This is not a Veejay Configuration File");
		xmlFreeDoc(doc);
		return 0;
	}

	cur = cur->xmlChildrenNode;
	override_keyboard = 1;
	while( cur != NULL )
	{
		if( !xmlStrcmp( cur->name, (const xmlChar*) XML_CONFIG_STREAM ))
		{	
			vj_event_xml_parse_stream( v, doc, cur->xmlChildrenNode );
		}		
		cur = cur->next;
	}
	override_keyboard = 0;
	xmlFreeDoc(doc);	

	veejay_change_playback_mode( v, v->uc->playback_mode, v->uc->sample_id );

	return 1;
}

int  veejay_load_action_file( void *ptr, char *file_name )
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	veejay_t *v = (veejay_t*) ptr;
	if(!file_name)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid filename");
			return 0;
		}

	doc = xmlParseFile( file_name );

	if(doc==NULL)	
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot read file %s", file_name );
		return 0;
	}
	cur = xmlDocGetRootElement( doc );
	if( cur == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "This is not a XML document");
		xmlFreeDoc(doc);
		return 0;
	}
	if( xmlStrcmp( cur->name, (const xmlChar *) XML_CONFIG_FILE))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "This is not a Veejay Configuration File");
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

	bzero( buffer, 4096 );

	if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END)
	{ /* its a Bundle !*/
		vj_msg_bundle *m = vj_event_bundle_get( event_id );
		if(!m) 
		{	
			veejay_msg(VEEJAY_MSG_ERROR, "bundle %d does not exist", event_id);
			return;
		}

		strncpy(buffer, m->bundle, strlen(m->bundle) );
		xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_KEY_EXTRA ,
			(const xmlChar*) buffer);
			// m->event_id and event_id should be equal
	}

	/* Put VIMS keybinding and Event ID */
#ifdef HAVE_SDL
	vj_event_get_key( event_id, &key_id, &key_mod );
 #endif

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

void	vj_event_format_xml_stream( xmlNodePtr node, int stream_id )
{
	char tmp[1024];
	
	int type = vj_tag_get_type(stream_id);
	int col[3] = {0,0,0};

	vj_tag *stream = vj_tag_get( stream_id );
	if(!stream )
		return;

	vj_tag_get_source_name( stream_id, tmp );
	xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_STREAM_SOURCE , (const xmlChar*) tmp );

	vj_tag_get_method_filename( stream_id, tmp );
	xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_STREAM_FILENAME, (const xmlChar*) tmp );
	
	sprintf (tmp, "%d", type );
	xmlNewChild(node, NULL, (const xmlChar*) XML_CONFIG_STREAM_TYPE, (const xmlChar*) tmp );

	if( type == VJ_TAG_TYPE_COLOR )
	{
		vj_tag_get_stream_color( stream_id, &col[0], &col[1], &col[2] );
		sprintf(tmp, "%03d %03d %03d", col[0],col[1],col[2] );
		xmlNewChild( node, NULL, (const xmlChar*) XML_CONFIG_STREAM_COLOR,
			(const xmlChar*) tmp );
	}
	else
	{
		sprintf(tmp, "%d", stream->video_channel );
		xmlNewChild( node, NULL, (const xmlChar*) XML_CONFIG_STREAM_OPTION,
			(const xmlChar*) tmp );
	}

	xmlNodePtr cnode = xmlNewChild( node, NULL, (const xmlChar*) XML_CONFIG_STREAM_CHAIN , 
		NULL );
	tagCreateStreamFX( cnode, stream );
}

static	void	vj_event_send_new_id(veejay_t * v, int new_id)
{
	char result[6];

	if(new_id < 0 ) new_id = 0;

	bzero(result,6);
	bzero( _s_print_buf,SEND_BUF);

	sprintf( result, "%05d",new_id );
	sprintf(_s_print_buf, "%03d%s",5, result);	
	SEND_MSG( v,_s_print_buf );
}

void vj_event_write_actionfile(void *ptr, const char format[], va_list ap)
{
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
	/* Here, we can save the samplelist, editlist as it is now */
	if(args[0]==1 || args[1]==1)
	{
		/* write samplelist into XML bundle */	
		char tmp_buf[1024];
		bzero(tmp_buf,1024);
		childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) XML_CONFIG_SETTINGS, NULL );
		vj_event_format_xml_settings( (veejay_t*) ptr, childnode );

		if( sample_size() > 1 )	
		{	
			bzero( live_set, 512 );
		
			sprintf(live_set, "%s-SL", file_name );
			int res = sample_writeToFile( live_set );
			if(!res)
				veejay_msg(VEEJAY_MSG_ERROR,"Error saving sample list to %s", live_set ); 
			else
				__xml_cstr( tmp_buf, live_set, childnode, XML_CONFIG_SETTING_SAMPLELIST );
		}
	}

	for( i = 0; i < VIMS_MAX; i ++ )
	{
		if( net_list[i].list_id > 0 )
		{	
			childnode = xmlNewChild( rootnode,NULL,(const xmlChar*) XML_CONFIG_EVENT ,NULL);
			vj_event_format_xml_event( childnode, i );
		}
	}
	
	for ( i = 1 ; i < vj_tag_size(); i ++ )
	{
		if(vj_tag_exists(i))
		{
			childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) XML_CONFIG_STREAM , NULL );
			vj_event_format_xml_stream( childnode, i );
		}
	}
	xmlSaveFormatFile( file_name, doc, 1);
	xmlFreeDoc(doc);	
}

#endif  // XML2

void	vj_event_read_file( void *ptr, 	const char format[], va_list ap )
{
	char file_name[512];
	int args[1];

	P_A(args,file_name,format,ap);

#ifdef HAVE_XML2
	veejay_load_action_file( ptr, file_name );
#endif
}

#ifdef HAVE_SDL

static void vj_event_get_key( int event_id, int *key_id, int *key_mod )
{
	if ( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
	{
		if( vj_event_bundle_exists( event_id ))
		{
			vj_msg_bundle *bun = vj_event_bundle_get( event_id );
			if( bun )
			{
				*key_id = bun->accelerator;
				*key_mod = bun->modifier;
			}
		}
	}
	else
	{
		int i;
		for ( i = 0; i < MAX_SDL_KEY ; i ++ )
		{
			if( keyboard_event_exists( i ) )
			{
				vj_keyboard_event *ev = get_keyboard_event(i);
				if(ev)
				{
					if(ev->vims->list_id == event_id )
					{
						*key_id =  ev->key_symbol;
  						*key_mod=  ev->key_mod;
						return;
					}
				}
			}
		}
		// see if binding is in 
	}
	

	*key_id  = 0;
	*key_mod = 0;
}

void	vj_event_unregister_keyb_event( int sdl_key, int modifier )
{
	int index = (modifier * SDLK_LAST) + sdl_key;
	vj_keyboard_event *ev = get_keyboard_event( index );
	if(ev)
	{
		if( ev->vims )
			free(ev->vims);
		if( ev->arguments)
			free(ev->arguments );
		memset(ev, 0, sizeof( vj_keyboard_event ));
		del_keyboard_event( index );
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
		return 0;
	}

	vj_keyboard_event *ev = new_keyboard_event(	
		symbol, modifier, value, event_id );

	if(!ev)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Error in %s, %d + %d (%s, %p)  VIMS %d",
			__FUNCTION__ , modifier, symbol,  value, value, event_id );
		return 0;
	}
	
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
	for( i = 1; vj_event_list[i].event_id != 0; i ++ )
	{
		net_id = vj_event_list[i].event_id;
		net_list[ net_id ].act = vj_event_list[i].function;
		net_list[ net_id ].list_id = i;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Registered %d VIMS events", net_id );
}

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


#ifdef HAVE_SDL
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
			  "VIMS event %d not yet implemented", vj_event_default_sdl_keys[i].event_id );
		}
	}
}
#endif

void vj_event_init()
{
	
	
	int i;
#ifdef HAVE_SDL
	if( !(keyboard_events = hash_create( HASHCOUNT_T_MAX, int_bundle_compare, int_bundle_hash)))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hash for keyboard events");
		return;
	}
#endif

	/* clear Network bindings */
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
	vj_event_init_keyboard_defaults();
	
}

void vj_event_quit(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	veejay_msg(VEEJAY_MSG_INFO, "Remote requested session-end.");
	veejay_change_state(v, LAVPLAY_STATE_STOP);         
}

void  vj_event_sample_mode(void *ptr,	const char format[],	va_list ap)
{
	veejay_t *v = (veejay_t *) ptr;
	if(v->settings->sample_mode == SSM_420_JPEG_BOX)
		veejay_set_sampling( v, SSM_420_JPEG_TR );
	else
		veejay_set_sampling( v, SSM_420_JPEG_BOX );
	veejay_msg(VEEJAY_MSG_WARNING, "Sampling of 2x2 -> 1x1 is set to [%s]",
		(v->settings->sample_mode == SSM_420_JPEG_BOX ? "lame box filter" : "triangle linear filter")); 
}

void vj_event_bezerk(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->no_bezerk) v->no_bezerk = 0; else v->no_bezerk = 1;
	veejay_msg(VEEJAY_MSG_INFO, "Bezerk mode is %s", (v->no_bezerk==0? "enabled" : "disabled"));
	if(v->no_bezerk==1)
		veejay_msg(VEEJAY_MSG_DEBUG,"Bezerk On  :No sample-restart when changing input channels");
	else
		veejay_msg(VEEJAY_MSG_DEBUG,"Bezerk Off :Sample-restart when changing input channels"); 
}

void vj_event_debug_level(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	if(v->verbose) v->verbose = 0; else v->verbose = 1;
	if(v->verbose==0) veejay_msg(VEEJAY_MSG_INFO,"Not displaying debug information");  
	veejay_set_debug_level( v->verbose );

}

void vj_event_suspend(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	veejay_change_state(v, LAVPLAY_STATE_PAUSED);
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
			if( vj_has_video(v) )
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
		}
	}
}



void	vj_event_set_rgb_parameter_type(void *ptr, const char format[], va_list ap)
{	
	
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(args[0] >= 0 && args[0] < 3 )
	{
		rgb_parameter_conversion_type_ = args[0];
		if(args[0] == 0)
			veejay_msg(VEEJAY_MSG_INFO,"GIMP's RGB -> YUV");
		if(args[1] == 1)
			veejay_msg(VEEJAY_MSG_INFO,"CCIR601 RGB -> YUV");
		if(args[2] == 2)
			veejay_msg(VEEJAY_MSG_INFO,"Broken RGB -> YUV");
	}

}

void vj_event_effect_set_bg(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	v->uc->take_bg = 1;
	veejay_msg(VEEJAY_MSG_INFO, "Next frame will be taken for static background\n");
}

void	vj_event_send_bundles(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	vj_msg_bundle *m;
	int i;
	int len = 0;
	const int token_len = 14;
	for( i =0 ; i < MAX_SDL_KEY ; i ++ )
	{
		if(keyboard_event_exists(i))
		{
			vj_keyboard_event *ev = get_keyboard_event(i);
			if(ev)
			{
				len += token_len;
				if(ev->arguments != NULL)
					len += strlen(ev->arguments);
			}
		}
	}

	//len ++;

	for( i = VIMS_BUNDLE_START; i < VIMS_BUNDLE_END; i ++ )
	{
		if( vj_event_bundle_exists(i))
		{
			m = vj_event_bundle_get( i );
			if(m)
			{
				len += strlen( m->bundle );
				len += token_len;
			}
		}
	}
	
	// token len too small !!
	if(len > 0)
	{
	//	char *buf = (char*) vj_malloc(sizeof(char) * (len+5) );
	
	//	bzero(buf, len+5 );
		char *buf = _s_print_buf;
		bzero(buf, SEND_BUF);
		sprintf(buf, "%05d", len ); 
		for ( i = 0; i < MAX_SDL_KEY; i ++ )
		{
			if( keyboard_event_exists(i))
			{
				vj_keyboard_event *ev = get_keyboard_event(i);
				if( ev )
				{
					int id = ev->vims->list_id;
					int arg_len = (ev->arguments == NULL ? 0 : strlen(ev->arguments));
					char tmp[token_len];
					bzero(tmp,token_len); 
	
					sprintf(tmp, "%04d%03d%03d%04d", id, ev->key_symbol, ev->key_mod, arg_len );
					strncat(buf,tmp,token_len);
					if( arg_len > 0 )
					{
						strncat( buf, ev->arguments, arg_len );
					}
				}
			}
		}

		for( i = VIMS_BUNDLE_START; i < VIMS_BUNDLE_END; i ++ )
		{
			if( vj_event_bundle_exists(i))
			{
				m = vj_event_bundle_get( i );
				if(m)
				{
					int key_id = 0;
					int key_mod = 0;
					int bun_len = strlen(m->bundle);	
					char tmp[token_len];
					bzero(tmp,token_len);
					vj_event_get_key( i, &key_id, &key_mod );
	
					sprintf(tmp, "%04d%03d%03d%04d",
						i,key_id,key_mod, bun_len );

					strncat( buf, tmp, token_len );
					strncat( buf, m->bundle, bun_len );
				}
			}
		}

fprintf(stderr, "Buf = %d\n", len );

		SEND_MSG(v,buf);
	//	if(buf) free(buf);
	}	
	else
	{
		char *buf = "00000";
		SEND_MSG(v,buf);
	}
}

void	vj_event_send_vimslist(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int i;

	int len = 0;
	// send event list seperatly

	for( i = 1; vj_event_list[i].name != NULL ; i ++ )
	{
		// dont include bundles or query messages
		if( vj_event_list[i].event_id < 400 || vj_event_list[i].event_id > 599 )
		{
			len += strlen( vj_event_list[i].name );
			len += (vj_event_list[i].format == NULL ? 0 : strlen(vj_event_list[i].format) );
			len += 12; /* event_id: 4, num_params: 2 , format:3 (strlen), descr: 3 (strlen) */
		}
	}

	if(len > 1)
	{
	//	char *buf = (char*) vj_malloc(sizeof(char) * (len+5) );
		char *buf = _s_print_buf;
		bzero(buf, SEND_BUF );
		sprintf(buf, "%05d", len ); 

		

		for( i = 1; vj_event_list[i].name != NULL ; i ++ )
		{
			if( vj_event_list[i].event_id < 400 || vj_event_list[i].event_id > 599 )
			{
				char tmp[12];
				bzero(tmp,12);
				int event_id = vj_event_list[i].event_id;
				char *description = (char*) vj_event_list[i].name;
				char *format	  = (char*) vj_event_list[i].format;
				int   format_len  = ( vj_event_list[i].format == NULL ? 0:strlen( vj_event_list[i].format ));
				int   descr_len   = strlen(description);
				
				sprintf(tmp, "%04d%02d%03d%03d",
						event_id, vj_event_list[i].num_params , format_len, descr_len );

				strncat( buf, tmp, 12 );
				if(format != NULL) 
					strncat( buf, format,	   format_len		 );
				strncat( buf, description, descr_len );
			}
		}
	
		SEND_MSG(v,buf);
	//	if(buf) free(buf);
	}	
	else
	{
		char *buf = "0000";
		SEND_MSG(v,buf);
	}
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
			if(vj_tag_exists( vj_tag_size()-1 ))
			{
				veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_TAG, vj_tag_size()-1);
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
		else
		{
			int id = vj_tag_size() - 1;
			if(id)
			{
				veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG,id);
			}
			else
			{
				p_no_tag(id);
			}
		}
	}
	else
	if(STREAM_PLAYING(v))
	{
		if(sample_exists(v->last_sample_id) )
		{
			veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE, v->last_sample_id);
		}
		else
		{
			int id = sample_size() - 1;
			if(id)
			{
				veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE,id);
			}
			else
			{
				p_no_sample(id);
			}
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
#endif
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
			if(vj_has_video(v) )
				veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_PLAIN, 0);
			else
				veejay_msg(VEEJAY_MSG_ERROR,
				 "There are no video files in the editlist");
		}
	}
}

void vj_event_sample_new(void *ptr, const char format[], va_list ap)
{
	int new_id = 0;
	veejay_t *v = (veejay_t*) ptr;
	if(SAMPLE_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		int args[2];
		char *s = NULL;
		int num_frames = v->edit_list->video_frames-1;
		P_A(args,s,format,ap);

		if(args[0] < 0)
		{
			/* count from last frame */
			int nframe = args[0];
			args[0] = v->edit_list->video_frames - 1 + nframe;
		}
		if(args[1] == 0)
		{
			args[1] = v->edit_list->video_frames - 1;
		}

		if(args[0] >= 0 && args[1] > 0 && args[0] <= args[1] && args[0] <= num_frames &&
			args[1] <= num_frames ) 
		{
			editlist *el = veejay_edit_copy_to_new( v, v->edit_list, args[0],args[1] );
			if(!el)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cant copy EDL");
				return;
			}
			int start = 0;
			int end = el->video_frames - 1;

			sample_info *skel = sample_skeleton_new(start, end );
			if(skel)
			{
				//skel->edit_list = vj_el_clone( v->edit_list );
				skel->edit_list = el;
				if(!skel->edit_list)
					veejay_msg(VEEJAY_MSG_ERROR, "Failed to copy EDL !!");
			}

			if(sample_store(skel)==0) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Created new sample [%d] with EDL", skel->sample_id);
				sample_set_looptype(skel->sample_id,1);
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
#ifdef HAVE_SDL
void	vj_event_fullscreen(void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	const char *caption = "Veejay";
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);
	// parsed display num!! -> index of SDL array

	//int id = args[0];
	int id = 0;
	int status = args[0];

	if( status < 0 || status > 1 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid argument passed to FULLSCREEN");
		return;
	}
	
	if( status != v->settings->full_screen[id] )
	{
		v->settings->full_screen[id] = status;

		vj_sdl_free(v->sdl[id]);
		vj_sdl_init(v->sdl[id],
			v->edit_list->video_width,
			v->edit_list->video_height,
			caption,
			1,
			v->settings->full_screen[id]
		);
	}
	veejay_msg(VEEJAY_MSG_INFO,"Video screen is %s", (v->settings->full_screen[id] ? "full screen" : "windowed"));
	
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

	// multiple sdl screen needs fixing
	const char *title = "Veejay";
	
	if( w < 0 || w > 4096 || h < 0 || h > 4096 || x < 0 || y < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid parameters");
		return;
	}

	if( v->sdl[id] )
	{
		vj_sdl_free( v->sdl[id] );
		free(v->sdl[id]);
		v->sdl[id] = NULL;
	}

	if(v->sdl[id]==NULL)	
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Allocate screen %d: %d x %d", id, v->video_output_width,
				v->video_output_height );
		v->sdl[id] = vj_sdl_allocate( v->video_output_width,
					      v->video_output_height,
					      v->pixel_format );
	}

	vj_sdl_init( v->sdl[id],w, h, title, 1, v->settings->full_screen[id] );
	if(x > 0 && y > 0 )
		vj_sdl_set_geometry(v->sdl[id],x,y);

}
#endif

void vj_event_play_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	if(!STREAM_PLAYING(v))
	{
		int speed = v->settings->current_playback_speed;
		veejay_set_speed(v, (speed == 0 ? 1 : 0  ));
		veejay_msg(VEEJAY_MSG_INFO,"Video is %s", (speed==0 ? "paused" : "playing"));
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
}

void vj_event_play_slow(void *ptr, const char format[],va_list ap)
{
	int args[1];
	veejay_t *v = (veejay_t*)ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v))
	{
		if(veejay_set_framedup(v, args[0]))
		{
			veejay_msg(VEEJAY_MSG_INFO,"Video frame will be duplicated %d to output",args[0]);
		}
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
			args[0] = v->edit_list->video_frames - 1;
		veejay_set_frame(v, args[0]);
		veejay_msg(VEEJAY_MSG_INFO, "Video frame %d set",s->current_frame_num);
	}
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
	veejay_msg(VEEJAY_MSG_INFO, "Skipped frame");
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
	veejay_msg(VEEJAY_MSG_INFO, "Previous frame");
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
			    (args[0] * v->edit_list->video_fps)));
	
	}
}

void vj_event_next_second(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[1];
	char *str = NULL;
	P_A( args,str,format, ap );
	video_playback_setup *s = v->settings;
	veejay_set_frame(v, (s->current_frame_num + (int)
			     ( args[0] * v->edit_list->video_fps)));
}


void vj_event_sample_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	video_playback_setup *s = v->settings;
	if(SAMPLE_PLAYING(v) || PLAIN_PLAYING(v)) 
	{
		v->uc->sample_start = s->current_frame_num;
		veejay_msg(VEEJAY_MSG_INFO, "Sample starting position set to frame %ld", v->uc->sample_start);
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
	if(SAMPLE_PLAYING(v) || PLAIN_PLAYING(v))
	{
		v->uc->sample_end = s->current_frame_num;
		if( v->uc->sample_end > v->uc->sample_start) {
			editlist *el = veejay_edit_copy_to_new( v, (SAMPLE_PLAYING(v)  ? v->edit_list : v->current_edit_list), v->uc->sample_start,v->uc->sample_end );
			int start = 0;
			int end = el->video_frames -1;
			sample_info *skel = sample_skeleton_new(start,end);
			skel->edit_list = el;
			if(sample_store(skel)==0) {
				veejay_msg(VEEJAY_MSG_INFO,"Created new sample [%d]", skel->sample_id);
				sample_set_looptype(skel->sample_id, 1);	
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR,"%s %d: Cannot store new sample!",__FILE__,__LINE__);
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "New el = %p (from %p)", el, v->edit_list );
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
  	}
  	if(PLAIN_PLAYING(v)) 
 	{
		veejay_set_frame(v,(v->edit_list->video_frames-1));
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
  	}
  	if ( PLAIN_PLAYING(v))
	{
		veejay_set_frame(v,0);
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

	if(!vj_perform_randomize(v))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "randomizer start failed");
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Started sample randomizer");	
	}
}
void	vj_event_sample_rand_stop( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	video_playback_setup *settings = v->settings;

	if(settings->randplayer.mode != RANDMODE_INACTIVE)
		veejay_msg(VEEJAY_MSG_ERROR, "Stopped sample randomizer");
	settings->randplayer.mode = RANDMODE_INACTIVE;
}

void vj_event_sample_set_loop_type(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	if(!SAMPLE_PLAYING(v)) return;

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

	if(args[1] >= 0 && args[1] <= 2) 
	{
		if(sample_exists(args[0]))
		{
			int lp;
			sample_set_looptype( args[0] , args[1]);
			lp = sample_get_looptype(args[0]);
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d loop type is now %s",args[0],
			  ( args[1]==1 ? "Normal Looping" : lp==2 ? "Pingpong Looping" : "No Looping" ) );
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
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d speed set to %d",args[0],args[1]);
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Speed %d it too high to set on sample %d !",
				args[1],args[0]); 
		}
	}
	else
	{
		p_no_sample(args[0]);
	}
}

void vj_event_sample_set_marker_start(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	veejay_t *v = (veejay_t*)ptr;
	
	char *str = NULL;
	P_A(args,str,format,ap);
	
	if( args[0] == 0) 
	{
		if(SAMPLE_PLAYING(v))
			args[0] = v->uc->sample_id;
	}

	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0; int end = 0;
		if ( sample_get_el_position( args[0], &start, &end ) )
		{	// marker in relative positions given !
		//	args[1] += start; // add sample's start position
			if( sample_set_marker_start( args[0], args[1] ) )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker starting position at %d", args[0],args[1]);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot set marker position %d for sample %d (limits are %d - %d)",args[1],args[0],start,end);
			}
		}
	}	
}


void vj_event_sample_set_marker_end(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	
	char *str = NULL;
	P_A(args,str,format,ap);
	
	if( args[0] == 0 ) 
	{
		if(SAMPLE_PLAYING(v))
			args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0; int end = 0;
		if ( sample_get_el_position( args[0], &start, &end ) )
		{
			args[1] = end - args[1]; // add sample's ending position
			if( sample_set_marker_end( args[0], args[1] ) )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker ending position at position %d", args[0],args[1]);
			}
			else
			{
				veejay_msg(VEEJAY_MSG_INFO, "Marker position out side of sample boundaries");
			}
		}	
	}
}


void vj_event_sample_set_marker(void *ptr, const char format[], va_list ap) 
{
	int args[3];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if( args[0] == 0) 
	{
		if(SAMPLE_PLAYING(v))
			args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0]) )
	{
		int start = 0;
		int end = 0;
		if( sample_get_el_position( args[0], &start, &end ) )
		{
		//	args[1] += start;
		//	args[2] = end - args[2];
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
}


void vj_event_sample_set_marker_clear(void *ptr, const char format[],va_list ap) 
{
	int args[1];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);
	
	if( args[0] == 0) 
	{
		if(SAMPLE_PLAYING(v))
			args[0] = v->uc->sample_id;
	}
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
}

void vj_event_sample_set_dup(void *ptr, const char format[], va_list ap)
{
	int args[2];
	veejay_t *v = (veejay_t*) ptr;
	char *s = NULL;
	P_A(args,s,format,ap);

	if( args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;

	if( sample_exists(args[0])) 
	{
		if( sample_set_framedup( args[0], args[1] ) != -1) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Sample %d frame duplicator set to %d", args[0],args[1]);
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
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot set frame duplicator to %d for sample %d",args[0],args[1]);
		}
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
	if(args[0] == 0 && STREAM_PLAYING(v))
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1)
		args[0] = vj_tag_size()-1;
	if( vj_tag_set_description(args[0],str) == 1)
		veejay_msg(VEEJAY_MSG_INFO, "Streamd %d description [%s]", args[0], str );
}


void vj_event_sample_set_descr(void *ptr, const char format[], va_list ap)
{
	char str[SAMPLE_MAX_DESCR_LEN];
	int args[5];
	veejay_t *v = (veejay_t*) ptr;
	P_A(args,str,format,ap);

	if( args[0] == 0 && SAMPLE_PLAYING(v)) 
	{
		args[0] = v->uc->sample_id;
	}

	if(args[0] == -1) args[0] = sample_size()-1;

	if(sample_set_description(args[0],str) == 0)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Sample %d description [%s]",args[0],str);
	}
}

#ifdef HAVE_XML2
void vj_event_sample_save_list(void *ptr, const char format[], va_list ap)
{
	char str[255];
	int *args = NULL;
	P_A(args,str,format,ap);
	if(sample_size()-1 < 1) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No samples to save");
		return;
	}
	if(sample_writeToFile( str) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Wrote %d samples to file %s", sample_size()-1, str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error saving samples to file %s", str);
	}
}

void vj_event_sample_load_list(void *ptr, const char format[], va_list ap)
{
	char str[255];
	int *args = NULL;
	P_A( args, str, format, ap);

	if( sample_readFromFile( str ) ) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "Loaded sample list [%s]", str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error loading sample list [%s]", str);
	}
}
#endif

void vj_event_sample_rec_start( void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	int args[2];
	int changed = 0;
	int result = 0;
	char *str = NULL;
	char prefix[150];
	P_A(args,str,format,ap);

	if(!SAMPLE_PLAYING(v)) 
	{
		p_invalid_mode();
		return;
	}

	char tmp[255];
	bzero(tmp,255);
	bzero(prefix,150);
	sample_get_description(v->uc->sample_id, prefix );
	if(!veejay_create_temp_file(prefix, tmp))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot create file %s",
			tmp);
		return;
	}

	if( args[0] == 0 )
	{
		int n = sample_get_speed(v->uc->sample_id);
		if( n == 0) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Sample was paused, forcing normal speed");
			n = 1;
		}
		else
		{
			if (n < 0 ) n = n * -1;
		}
		args[0] = sample_get_longest(v->uc->sample_id);
		changed = 1;
	}

	int format_ = _recorder_format;
	if(format_==-1)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Set a destination video format first");
		return; 
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "Video frames to record: %ld", args[0]);

	if( sample_init_encoder( v->uc->sample_id, tmp, format_, v->edit_list, args[0]) == 1)
	{
		video_playback_setup *s = v->settings;
		s->sample_record_id = v->uc->sample_id;
		if(args[1])
			s->sample_record_switch = 1;
		result = 1;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to initialize sample recorder");
	}   

	if(changed)
	{
		veejay_set_sample(v,v->uc->sample_id);
	}


	if(result == 1)
	{
		veejay_msg(VEEJAY_MSG_INFO,
			"Recording editlist frames starting from %d (%d frames and %s)", 
			v->settings->current_frame_num,
			args[0], (args[1]==1? "autoplay" : "no autoplay"));
		v->settings->sample_record = 1;
		v->settings->sample_record_switch = args[1];
	}

}

void vj_event_sample_rec_stop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	
	if( SAMPLE_PLAYING(v)) 
	{
		video_playback_setup *s = v->settings;
		if( sample_stop_encoder( v->uc->sample_id ) == 1 ) 
		{
			char avi_file[255];
			v->settings->sample_record = 0;
			if( sample_get_encoded_file(v->uc->sample_id, avi_file) <= 0 )
			{
			 	veejay_msg(VEEJAY_MSG_ERROR, "adding file");
				return;
			}

			int ns = veejay_edit_addmovie_sample(v,avi_file,0 );
			if(ns)
				veejay_msg(VEEJAY_MSG_INFO, "Created new sample %d from file %s",ns,avi_file);
			else
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot add videofile %s to EditList!",avi_file);

			sample_reset_encoder( v->uc->sample_id);
			s->sample_record = 0;	
			s->sample_record_id = 0;
			if(s->sample_record_switch) 
			{
				s->sample_record_switch = 0;
				veejay_set_sample( v, ns );
				veejay_msg(VEEJAY_MSG_INFO, "Switching to sample %d (recording)", sample_size()-1);
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample recorder was never started for sample %d",v->uc->sample_id);
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
		{
			args[0] = v->uc->sample_id;
		}
	if(args[0] == -1) args[0] = sample_size()-1;	
		if(!sample_exists(args[0]))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample does not exist");
			return;
		}
		
		s_start = sample_get_startFrame(args[0]) + args[1];
		s_end = sample_get_endFrame(args[0]) + args[2];

		if(s_end > v->edit_list->video_frames-1) s_end = v->edit_list->video_frames - 1;

		if( s_start >= 1 && s_end <= (v->edit_list->video_frames-1) )
		{ 
			if	(sample_set_startframe(args[0],s_start) &&
				sample_set_endframe(args[0],s_end))
			{
				veejay_msg(VEEJAY_MSG_INFO, "Sample update start %d end %d",
					s_start,s_end);
			}
	
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Invalid playmode");
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
	if(args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}
	if(args[0] == -1) args[0] = sample_size()-1;
	
	editlist *el = sample_get_editlist( args[0] );
	if(el)
		mf = el->video_frames-1;
	else
		mf = v->edit_list->video_frames -1;

	if( (args[1] >= s->min_frame_num) && (args[1] <= mf) && sample_exists(args[0])) 
	{
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
	else
	{
		if(!sample_exists(args[0])) p_no_sample(args[0]) else veejay_msg(VEEJAY_MSG_ERROR, "Invalid position %d given",args[1]);
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
	if(args[0] == 0) 
	{
		args[0] = v->uc->sample_id;
	}
	if(args[1] == -1)
	{
		args[1] = v->edit_list->video_frames-1;
	}

	editlist *el = sample_get_editlist( args[0] );	
	if(el)
		mf = el->video_frames-1;
	else
		mf = v->edit_list->video_frames -1;

	if( (args[1] >= s->min_frame_num) && (args[1] <= mf) && (sample_exists(args[0])))
	{
		if( args[1] >= sample_get_startFrame(v->uc->sample_id)) {
	       		if(sample_set_endframe(args[0],args[1])) {
	   			veejay_msg(VEEJAY_MSG_INFO,"Sample ending frame updated to frame %d",
	        		sample_get_endFrame(args[0]));
			}
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to update sample %d 's ending position to %d",
					args[0],args[1]);
			}
	      	}
		else 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample %d 's ending position %d must be greater than or equal to starting position %d.",
				args[0],args[1], sample_get_startFrame(v->uc->sample_id));
		}
	}
	else
	{
		if(!sample_exists(args[0])) p_no_sample(args[0]) else veejay_msg(VEEJAY_MSG_ERROR, "Invalid position %d given",args[1]);
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
	if(STREAM_PLAYING(v))
	{
		vj_tag_set_effect_status(v->uc->sample_id, 1);
	}
	veejay_msg(VEEJAY_MSG_INFO, "Effect chain is enabled");
	
}


void vj_event_chain_disable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	if(SAMPLE_PLAYING(v) )
	{
		sample_set_effect_status(v->uc->sample_id, 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Sample %d is disabled",v->uc->sample_id);
	}
	if(STREAM_PLAYING(v) )
	{
		vj_tag_set_effect_status(v->uc->sample_id, 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Stream %d is enabled",v->uc->sample_id);
	}

}

void vj_event_sample_chain_enable(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*)ptr;
	int args[4];
	char *s = NULL;
	P_A(args,s,format,ap);
	if(args[0] == 0)
	{
		args[0] = v->uc->sample_id;
	}
	
	if(SAMPLE_PLAYING(v) && sample_exists(args[0]))
	{
		sample_set_effect_status(args[0], 1);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Sample %d is enabled",args[0]);
	}
	
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
		{
			sample_set_effect_status( i, args[0] );
		}
		veejay_msg(VEEJAY_MSG_INFO, "Effect Chain on all samples %s", (args[0]==0 ? "Disabled" : "Enabled"));
	}
	if(STREAM_PLAYING(v))
	{
		int i;
		for(i=0; i < vj_tag_size()-1; i++)
		{
			vj_tag_set_effect_status(i,args[0]);
		}
		veejay_msg(VEEJAY_MSG_INFO, "Effect Chain on all streams %s", (args[0]==0 ? "Disabled" : "Enabled"));
	}
}


void vj_event_tag_chain_enable(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[4];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(STREAM_PLAYING(v) && vj_tag_exists(args[0]))
	{
		vj_tag_set_effect_status(args[0], 1);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is enabled",args[0]);
	}

}
void vj_event_tag_chain_disable(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	char *s = NULL;
	P_A(args,s,format,ap);

	if(STREAM_PLAYING(v) && vj_tag_exists(args[0]))
	{
		vj_tag_set_effect_status(args[0], 0);
		veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is disabled",args[0]);
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

	if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
	{
		if( sample_set_fader_active( args[0], args[1],-1 ) )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Chain Fade In from sample to full effect chain in %d frames. Per frame %2.2f",
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
			veejay_msg(VEEJAY_MSG_INFO,"Chain Fade In from stream to full effect chain in %d frames. Per frame %2.2f",
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
	memset(args,0,16); 
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
			
			veejay_msg(VEEJAY_MSG_DEBUG, "Effect %d , params %d known as %s",
				real_id, num_p, vj_effect_get_description(real_id));

			if(sample_chain_add( args[0],args[1],args[2])!=-1)
			{
				int args_offset = 3;
				
				veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d Chain entry %d has effect %s with %d arguments",
					args[0],args[1],vj_effect_get_description(real_id),num_p);
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

				if ( vj_effect_get_extra_frame( real_id ) && args[num_p + args_offset])
				{
					int source = args[num_p+4];	
					int channel_id = args[num_p+3];
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
					else
					{
					  veejay_msg(VEEJAY_MSG_ERROR, "updating mixing channel (channel %d is an invalid %s?)",
					   channel_id, (source == 0 ? "stream" : "sample" ));
					}
				}
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

			if( args[ num_p + 3] && vj_effect_get_extra_frame(real_id) )
			{
				int channel_id = args[num_p + 3];
				int source = args[ num_p + 4];
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
				else
				{
					  veejay_msg(VEEJAY_MSG_ERROR, "updating mixing channel (channel %d is an invalid %s?)",
					   channel_id, (source == 0 ? "stream" : "sample" ));
				}
			}
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
			src = vj_tag_get_type(cha);
		}
		else
		{
			src = 0; // source is stream, toggle to sample
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
		}
		sample_set_chain_source( v->uc->sample_id, entry, src );
		sample_set_chain_channel(v->uc->sample_id,entry,cha);
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d", entry,(src==VJ_TAG_TYPE_NONE ? "Sample":"Stream"), cha);
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
	} 

	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int src = vj_tag_get_chain_source(v->uc->sample_id, entry);
		int cha = vj_tag_get_chain_channel( v->uc->sample_id, entry );
		char description[100];

		if(src == VJ_TAG_TYPE_NONE ) 
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
			src = vj_tag_get_type(cha);
		}
		else
		{
			src = 0;
			if(!sample_exists(cha))
			{
				cha = sample_size()-1;
				if(cha<=0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
					return;
				}
			}
		}
		vj_tag_set_chain_source( v->uc->sample_id, entry, src );
		vj_tag_set_chain_channel(v->uc->sample_id,entry,cha);
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

		vj_tag_get_descriptive(cha, description);
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses channel %d (%s)", entry, cha,description);
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
				if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

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
			if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

			}	
		}
	}
}

void vj_event_chain_entry_channel_dec(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	//DUMP_ARG(args);

	if(SAMPLE_PLAYING(v))
	{
		int entry = sample_get_selected_entry(v->uc->sample_id);
		int cha = sample_get_chain_channel(v->uc->sample_id,entry);
		int src = sample_get_chain_source(v->uc->sample_id,entry);

		if(src==VJ_TAG_TYPE_NONE)
		{	//decrease sample id
			if(cha <= 1)
			{
				cha = sample_size()-1;
				if(cha <= 0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
					return;
				}		
			}
			else
			{
				cha = cha - args[0];
			}
		}
		else	
		{
			if( cha <= 1)
			{
				cha = vj_tag_size()-1;
				if(cha<=0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");
					return;
				}
			}
			else
			{
				cha = cha - args[0];
			}
			src = vj_tag_get_type( cha );
			sample_set_chain_source( v->uc->sample_id,entry,src);
		}
		sample_set_chain_channel( v->uc->sample_id, entry, cha );
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
				(src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
			 
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

	}
	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
		int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
		char description[100];
		if(src==VJ_TAG_TYPE_NONE)
		{	//decrease sample id
			if(cha <= 1)
			{
				cha = sample_size()-1;
				if(cha <= 0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
					return;
				}		
			}
			else
			{
				cha = cha - args[0];
			}
		}
		else	
		{
			if( cha <= 1)
			{
				cha = vj_tag_size()-1;
				if(cha<=0)
				{
					veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");
					return;
				}
			}
			else
			{
				cha = cha - args[0];
			}
			src = vj_tag_get_type( cha );
			vj_tag_set_chain_source( v->uc->sample_id, entry, src);
		}
		vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
		vj_tag_get_descriptive( cha, description);

		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses Stream %d (%s)",entry,cha,description);
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
 
	}

}

void vj_event_chain_entry_channel_inc(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	char *str = NULL; P_A(args,str,format,ap);

	//DUMP_ARG(args);

	if(SAMPLE_PLAYING(v))
	{
		int entry = sample_get_selected_entry(v->uc->sample_id);
		int cha = sample_get_chain_channel(v->uc->sample_id,entry);
		int src = sample_get_chain_source(v->uc->sample_id,entry);
		if(src==VJ_TAG_TYPE_NONE)
		{
			int num_c = sample_size()-1;
			if(num_c <= 0)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
				return;
			}
			//decrease sample id
			if(cha >= num_c)
			{
				cha = 1;
			}
			else
			{
				cha = cha + args[0];
			}
		}
		else	
		{
			int num_c = vj_tag_size()-1;
			if(num_c <=0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");	
				return;
			}
			if( cha >= num_c)
			{
				cha = 1;
			}
			else
			{
				cha = cha + args[0];
			}
			src = vj_tag_get_type( cha );
			sample_set_chain_source( v->uc->sample_id, entry,src );
		}
		sample_set_chain_channel( v->uc->sample_id, entry, cha );
		veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
			(src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
	
			 
	}
	if(STREAM_PLAYING(v))
	{
		int entry = vj_tag_get_selected_entry(v->uc->sample_id);
		int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
		int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
		char description[100];
		if(src==VJ_TAG_TYPE_NONE)
		{
			int num_c = sample_size()-1;
			if(num_c <= 0)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
				return;
			}
			//decrease sample id
			if(cha >= num_c)
			{
				cha = 1;
			}
			else
			{
				cha = cha + args[0];
			}
		}
		else	
		{
			int num_c = vj_tag_size()-1;
			if(num_c <=0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "No streams to mix with");	
				return;
			}
			if( cha >= num_c)
			{
				cha = 1;
			}
			else
			{
				cha = cha + args[0];
			}
			src = vj_tag_get_type( cha );
			vj_tag_set_chain_source( v->uc->sample_id, entry, src);
		}

		vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
		vj_tag_get_descriptive( cha, description);
		if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

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
				if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

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
				if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

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
				if(v->no_bezerk) veejay_set_sample(v,v->uc->sample_id);
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

	
			if(	err == 0 &&
				vj_tag_set_chain_source(args[0],args[1],source)!=-1 &&
				vj_tag_set_chain_channel(args[0],args[1],channel_id) != -1)
			{
				veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d) to mix in",
					(source == VJ_TAG_TYPE_NONE ? "sample" : "stream") , channel_id);
				if(v->no_bezerk) veejay_set_sample(v,v->uc->sample_id);
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
					
		   if( vj_effect_valid_value( effect, args[0],tval ) )
		   {
			if(sample_set_effect_arg( v->uc->sample_id, c,args[0],(val+args[1]))!=-1 )
			{
				veejay_msg(VEEJAY_MSG_INFO,"Set parameter %d value %d",args[0],(val+args[1]));
			}
		   }
		   if(sample_set_effect_arg( v->uc->sample_id, c,args[0],tval) )
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
				veejay_msg(VEEJAY_MSG_ERROR,"Tried to set invalid parameter value/type: %d %d for effect %d on entry %d",
		args[2],args[3],effect,args[1]);
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
			veejay_msg(VEEJAY_MSG_ERROR, "Tried to set invalid parameter value/type : %d %d",
				args[2],args[3]);
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
		editlist *el = sample_get_editlist( v->uc->sample_id );
		int start = sample_get_startFrame( v->uc->sample_id );
		int end   = sample_get_endFrame(v->uc->sample_id);

		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}	

		if(veejay_edit_cut( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Cut frames %d-%d from sample %d into buffer",args[0],args[1],
				v->uc->sample_id);
		}
	}

	if ( STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot cut frames in this playback mode");
		return;
	}

	if( PLAIN_PLAYING(v)) 
	{
		if(veejay_edit_cut( v,v->current_edit_list, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Cut frames %d-%d into buffer",args[0],args[1]);
		}
	}
}

void vj_event_el_copy(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( SAMPLE_PLAYING(v))
	{
		editlist *el = sample_get_editlist( v->uc->sample_id );
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}	
		if(veejay_edit_copy( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Copy frames %d-%d from sample %d into buffer",v->uc->sample_id,args[0],args[1]);
		}

	}
	if ( STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot copy frames in this playback mode");
		return;
	}

	if( PLAIN_PLAYING(v)) 
	{
		if(veejay_edit_copy( v,v->current_edit_list, args[0],args[1] )) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Copy frames %d-%d into buffer",args[0],args[1]);
		}
	}

}

void vj_event_el_del(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( SAMPLE_PLAYING(v))
	{
		editlist *el = sample_get_editlist( v->uc->sample_id );
		int start = sample_get_startFrame(v->uc->sample_id );
		int end = sample_get_endFrame(v->uc->sample_id);
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL (is this possible?)");
			return;
		}	
		if(veejay_edit_delete( v,el, args[0], args[1] ))
		{
			veejay_msg(VEEJAY_MSG_INFO, "Deleted frames %d-%d from sample %d into buffer",v->uc->sample_id,args[0],args[1]);
		}
		if(end > (el->video_frames -1) )
		{
			end = el->video_frames -1;
			sample_set_endframe(v->uc->sample_id, end );
		}
	}

	if ( STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot delete frames in this playback mode");
		return;
	}

	if( PLAIN_PLAYING(v)) 
	{
		if(veejay_edit_delete(v,v->current_edit_list, args[0],args[1])) 
		{
			veejay_msg(VEEJAY_MSG_INFO, "Deleted frames %d-%d into buffer", args[0],args[1]);
		}
	}
}

void vj_event_el_crop(void *ptr, const char format[], va_list ap) 
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char *str = NULL; P_A(args,str,format,ap);

	if ( STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot delete frames in this playback mode");
		return;
	}

	if(SAMPLE_PLAYING(v))
	{
	/*	editlist *el = sample_get_editlist( v->uc->sample_id);
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL");
			return;
		}

		if( args[0] < 0 || args[0] >= el->video_frames || args[1] < 0 || args[1] >= el->video_frames)
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
			s2 = veejay_edit_delete(v, el,args[1], el->video_frames-1); 
			if(s2)
			{
				veejay_set_frame(v,0);
				veejay_msg(VEEJAY_MSG_INFO, "Delete frames 0- %d , %d - %d from sample %d", 0,args[0],args[1],
					el->video_frames - 1, v->uc->sample_id);
				res = 1;
			}
		}
		if(!res)
			veejay_msg(VEEJAY_MSG_ERROR, "Invalid range given to crop ! %d - %d", args[0],args[1] );
		*/
	}

	if( PLAIN_PLAYING(v)) 
	{
		if( args[0] < 0 || args[0] >= v->current_edit_list->video_frames || args[1] < 0 || args[1] >= v->current_edit_list->video_frames)
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
		int s1 = veejay_edit_delete(v,v->current_edit_list, 0, args[0]);	
		int res = 0;
		if(s1)
		{
			args[1] -= args[0]; // after deleting the first part, move arg[1]
			s2 = veejay_edit_delete(v,  v->current_edit_list, args[0], v->current_edit_list->video_frames-1); 
			if(s2)
			{
				veejay_set_frame(v,0);
				veejay_msg(VEEJAY_MSG_INFO, "Delete frames 0- %d , %d - %d", 0,args[0],args[1],
					v->current_edit_list->video_frames - 1);
				res = 1;
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

	if ( STREAM_PLAYING(v)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot paste frames in this playback mode");
		return;
	}

	if( SAMPLE_PLAYING(v))
	{
		editlist *el = sample_get_editlist( v->uc->sample_id );
		long length = el->video_frames-1;
		if(!el)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Sample has no EDL");
			return;
		}
		if( args[0] >= 0 && args[0] <= el->video_frames-1)
		{		
			if( veejay_edit_paste( v, el, args[0] ) ) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Pasted buffer at frame %d",args[0]);
			}
			int end = (el->video_frames - length) + sample_get_endFrame(v->uc->sample_id);
			sample_set_endframe( v->uc->sample_id, end );
		}

	}


	if( PLAIN_PLAYING(v)) 
	{
		if( args[0] >= 0 && args[0] <= v->current_edit_list->video_frames-1)
		{		
			if( veejay_edit_paste( v, v->current_edit_list, args[0] ) ) 
			{
				veejay_msg(VEEJAY_MSG_INFO, "Pasted buffer at frame %d",args[0]);
			}
		}
	}
}

void vj_event_el_save_editlist(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	//int *args = NULL;
 	int args[2] = {0,0};
	P_A(args,str,format,ap);
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
	int destination = v->edit_list->video_frames-1;
	char str[1024];
	int *args = NULL;
	P_A(args,str,format,ap);


	if ( veejay_edit_addmovie(v,v->edit_list,str,start,destination,destination))
	{
		veejay_msg(VEEJAY_MSG_INFO, "Appended video file %s to EditList",str); 
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot append file %s to EditList",str);
	} 
}

void vj_event_el_add_video_sample(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	int args[2];
	P_A(args,str,format,ap);

	int new_sample_id = args[0];

	new_sample_id = veejay_edit_addmovie_sample(v,str,new_sample_id );
	veejay_msg(VEEJAY_MSG_DEBUG , "New sample %d from file %s", new_sample_id, str );
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

void vj_event_tag_new_avformat(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char str[255];
	int *args = NULL;
	P_A(args,str,format,ap);

	int id = veejay_create_tag(v, VJ_TAG_TYPE_AVFORMAT, str, v->nstreams,0,0);

	vj_event_send_new_id( v, id );
	if( id <= 0 );
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new FFmpeg stream");
}
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

#ifdef HAVE_V4L
void vj_event_tag_new_v4l(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	char *str = NULL;
	int args[2];
	char filename[255];
	P_A(args,str,format,ap);

	sprintf(filename, "video%d", args[0]);

	int id = veejay_create_tag(v, VJ_TAG_TYPE_V4L, filename, v->nstreams,0,args[1]);
	vj_event_send_new_id( v, id );

	if( id <= 0 )
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Video4Linux stream ");
}
#endif
void vj_event_tag_new_net(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;

	char str[255];
	int args[2];

	P_A(args,str,format,ap);
	int id = veejay_create_tag(v, VJ_TAG_TYPE_NET, str, v->nstreams, 0,args[0]);
	vj_event_send_new_id( v, id);

	if(id <= 0)
		veejay_msg(VEEJAY_MSG_ERROR, "create new Network stream");
}

void vj_event_tag_new_mcast(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;

	char str[255];
	int args[2];

	P_A(args,str,format,ap);
	int id = veejay_create_tag(v, VJ_TAG_TYPE_MCAST, str, v->nstreams, 0,args[0]);
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

	
	int id =  vj_tag_new( VJ_TAG_TYPE_COLOR, NULL, -1, v->edit_list,v->pixel_format, -1 );
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
#ifdef HAVE_V4L
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
	bzero(send_msg, sizeof(send_msg));
	bzero(message,sizeof(message));

	if(vj_tag_exists(args[0]))
	{
		int values[5];
		memset(values,0,sizeof(values));
		if(vj_tag_get_v4l_properties( args[0], &values[0], &values[1], &values[2], &values[3],
				&values[4]))
		{
			sprintf(message, "%05d%05d%05d%05d%05d",
				values[0],values[1],values[2],values[3],values[4] );
		}
	}
	FORMAT_MSG(send_msg, message);
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
	char *str = NULL;
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
#endif

void vj_event_tag_set_format(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	int args[2];
	char str[255]; 
	bzero(str,255);
	P_A(args,str,format,ap);

	if(v->settings->tag_record || v->settings->offline_record)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot change data format while recording to disk");
		return;
	}

	if(strncasecmp(str, "yv16",4) == 0 || strncasecmp(str,"y422",4)==0)
	{
		_recorder_format = ENCODER_YUV422;
		veejay_msg(VEEJAY_MSG_INFO, "Recorder writes in YUV 4:2:2 Planar");
		return;
	}

	if(strncasecmp(str,"mpeg4",5)==0 || strncasecmp(str,"divx",4)==0)
	{
		_recorder_format = ENCODER_MPEG4;
		veejay_msg(VEEJAY_MSG_INFO, "Recorder writes in MPEG4 format");
		return;
	}

	if(strncasecmp(str,"msmpeg4v3",9)==0 || strncasecmp(str,"div3",4)==0)
	{
		_recorder_format = ENCODER_DIVX;
		veejay_msg(VEEJAY_MSG_INFO,"Recorder writes in MSMPEG4v3 format");
		return;
	}
	if(strncasecmp(str,"dvvideo",7)==0||strncasecmp(str,"dvsd",4)==0)
	{
		_recorder_format = ENCODER_DVVIDEO;
		veejay_msg(VEEJAY_MSG_INFO,"Recorder writes in DVVIDEO format");
		return;
	}
	if(strncasecmp(str,"mjpeg",5)== 0 || strncasecmp(str,"mjpg",4)==0 ||
		strncasecmp(str, "jpeg",4)==0)
	{
		_recorder_format = ENCODER_MJPEG;
		veejay_msg(VEEJAY_MSG_INFO, "Recorder writes in MJPEG format");
		return;
	}
	if(strncasecmp(str,"i420",4)==0 || strncasecmp(str,"yv12",4)==0 )
	{
		_recorder_format = ENCODER_YUV420;
		veejay_msg(VEEJAY_MSG_INFO, "Recorder writes in uncompressed YV12/I420 (see swapping)");
		if(v->pixel_format == FMT_422)
		{
			veejay_msg(VEEJAY_MSG_WARNING, "Using 2x2 -> 1x1 and 1x1 -> 2x2 conversion");
		}
		return;
	}
	veejay_msg(VEEJAY_MSG_INFO, "Use one of these:");
	veejay_msg(VEEJAY_MSG_INFO, "mpeg4, div3, dvvideo , mjpeg , i420 or yv16");	

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
	bzero(sourcename,255);
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

	if( vj_tag_init_encoder( v->uc->sample_id, tmp, format,		
			args[0]) != 1 ) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "Error trying to start recording from stream %d", v->uc->sample_id);
		vj_tag_stop_encoder(v->uc->sample_id);
		v->settings->tag_record = 0;
		return;
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

		int ns = veejay_edit_addmovie_sample( v,avi_file, v->uc->sample_id );
		if(ns)
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
		veejay_msg(VEEJAY_MSG_ERROR, "Already recording from a stream");
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
	}
}


void vj_event_output_y4m_start(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char str[1024];
	int *args = NULL;
	P_A( args,str,format,ap);
	if(v->stream_enabled==0)
	{
		int n=0;
		strncpy(v->stream_outname, str,strlen(str));
		n= vj_yuv_stream_start_write(v->output_stream,str,v->edit_list);
		if(n==1) 
		{
			int s = v->settings->current_playback_speed;
			veejay_msg(VEEJAY_MSG_DEBUG, "Pausing veejay");
			
			veejay_set_speed(v,0);
			if(vj_yuv_stream_open_pipe(v->output_stream,str,v->edit_list))
			{
				vj_yuv_stream_header_pipe(v->output_stream,v->edit_list);
				v->stream_enabled = 1;
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "Resuming veejay");
			veejay_set_speed(v,s);
			
		}
		if(n==0)
		if( vj_yuv_stream_start_write(v->output_stream,str,v->edit_list)==0)
		{
			v->stream_enabled = 1;
			veejay_msg(VEEJAY_MSG_INFO, "Started YUV4MPEG streaming to [%s]", str);
		}
		if(n==-1)
		{
			veejay_msg(VEEJAY_MSG_INFO, "YUV4MPEG stream not started");
		}
	}	
}

void vj_event_output_y4m_stop(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	if(v->stream_enabled==1)
	{
		vj_yuv_stream_stop_write(v->output_stream);
		v->stream_enabled = 0;
		veejay_msg(VEEJAY_MSG_INFO , "Stopped YUV4MPEG streaming to %s", v->stream_outname);
	}
}

void vj_event_enable_audio(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	if(v->edit_list->has_audio)
	{
		if(v->audio != AUDIO_PLAY)
		{
			if( vj_perform_audio_start(v) )
			{
				v->audio = AUDIO_PLAY;
			}
			else 
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot start Jack ");
			}
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Already playing audio");
		}
	}
	else 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Video has no audio");
	}
}

void vj_event_disable_audio(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t *)ptr;
	if(v->edit_list->has_audio)
	{
		if(v->audio == AUDIO_PLAY)
		{
			vj_perform_audio_stop(v);
			v->audio = NO_AUDIO;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Not playing audio");
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Video has no audio");
	}
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
	if(v->uc->key_effect >= MAX_EFFECTS) v->uc->key_effect = 1;

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
	if(v->uc->key_effect <= 0) v->uc->key_effect = MAX_EFFECTS-1;
	
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
			if(v->no_bezerk && vj_effect_get_extra_frame(real_id) ) veejay_set_sample(v,v->uc->sample_id);
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
			if(v->no_bezerk && vj_effect_get_extra_frame(real_id)) veejay_set_sample(v,v->uc->sample_id);
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



void	vj_event_plugin_command(void *ptr,	const char	format[],	va_list ap)
{
	int args[2];
	char str[1024];
	const char delimiters[] = ":";
	bzero(str,1024);
	P_A(args,str,format,ap);

	char *plugargs = strdup( strstr( str, ":" ) );
	char *plugname = strtok( str, delimiters );

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Plugin '%s' : '%s' ", plugname, plugargs );

	if( plugargs != NULL && plugname == NULL )
		plugins_event( plugname, plugargs+1 ); 
}

void	vj_event_unload_plugin(void *ptr, const char format[], va_list ap) 
{
	int args[2];
	char str[1024];
	P_A(args,str,format,ap);
	veejay_msg(VEEJAY_MSG_DEBUG, "Try to close plugin '%s'", str);
	
	plugins_free( str );
}

void	vj_event_load_plugin(void *ptr, const char format[], va_list ap)
{
	int args[2];
	char str[1024];
	P_A(args,str,format,ap);

	
	veejay_msg(VEEJAY_MSG_DEBUG, "Try to open plugin '%s' ", str);
	if(plugins_init( str ))
	{
		veejay_msg(VEEJAY_MSG_INFO, "Loaded plugin %s", str);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unloaded plugin %s", str);
	}
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
	v->real_fps = -1;

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
	bzero( prefix, 20);
	bzero( blob, (50 * SAMPLE_MAX_EFFECTS));
   
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
				bzero(bundle,200);
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
						strncat( bundle, svalue, strlen(svalue));
					}
				}
				strncpy( blob+bunlen, bundle,strlen(bundle));
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
	int start = sample_get_startFrame( id );
	int end = sample_get_endFrame( id );
	int speed = sample_get_speed(id);
	int len = end - start;

	if(start == 0) len ++;
	bzero(sampletitle,200);	
	mpeg_timecode(&tc, len,
 		mpeg_framerate_code(mpeg_conform_framerate(v->edit_list->video_fps)),v->edit_list->video_fps);
	sprintf(timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);

	mpeg_timecode(&tc,  s->current_frame_num,
		mpeg_framerate_code(mpeg_conform_framerate
		   (v->edit_list->video_fps)),
	  	v->edit_list->video_fps);

	sprintf(curtime, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
	sample_get_description( id, sampletitle );
	veejay_msg(VEEJAY_MSG_PRINT, "\n");
	veejay_msg(VEEJAY_MSG_INFO, 
		"Sample '%s'[%4d]/[%4d]\t[duration: %s | %8d ]",
		sampletitle,id,sample_size()-1,timecode,len);

	if(sample_encoder_active(v->uc->sample_id))
	{
		veejay_msg(VEEJAY_MSG_INFO, "REC %09d\t[timecode: %s | %8ld ]",
			sample_get_frames_left(v->uc->sample_id),
			curtime,(long)v->settings->current_frame_num);

	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "                \t[timecode: %s | %8ld ]",
			curtime,(long)v->settings->current_frame_num);
	}
	veejay_msg(VEEJAY_MSG_INFO, 
		"[%09d] - [%09d] @ %4.2f (speed %d)",
		start,end, (float)speed * v->edit_list->video_fps,speed);
	veejay_msg(VEEJAY_MSG_INFO,
		"[%s looping]",
		(sample_get_looptype(id) ==
		2 ? "pingpong" : (sample_get_looptype(id)==1 ? "normal" : "no")  )
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
	v->real_fps = -1;

	//vj_el_print( sample_get_editlist( id ) );

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Sample has EDL %p, Plain at %p", sample_get_editlist( id ), v->current_edit_list );

	veejay_msg(VEEJAY_MSG_PRINT, "\n");

}

void vj_event_print_plain_info(void *ptr, int x)
{
	veejay_t *v = (veejay_t*) ptr;
	v->real_fps = -1;	
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

void	vj_event_send_tag_list			(	void *ptr,	const char format[],	va_list ap	)
{
	int args[1];
	int start_from_tag = 1;
	veejay_t *v = (veejay_t*)ptr;
	char *str = NULL; 
	P_A(args,str,format,ap);
	int i,n;
	bzero( _s_print_buf,SEND_BUF);
	sprintf(_s_print_buf, "%05d",0);

	//if(args[0]>0) start_from_tag = args[0];

	n = vj_tag_size()-1;
	veejay_msg(VEEJAY_MSG_DEBUG, "I have %d streams to describe", n );
	if (n >= 1 )
	{
		char line[300];
		bzero( _print_buf, SEND_BUF);

		for(i=0; i <= n; i++)
		{
			if(vj_tag_exists(i))
			{	
				vj_tag *tag = vj_tag_get(i);
				char source_name[255];
				char cmd[300];
				bzero(source_name,200);bzero(cmd,255);
				bzero(line,300);
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
veejay_msg(VEEJAY_MSG_DEBUG, "[%s]", _s_print_buf);
	SEND_MSG(v,_s_print_buf);
}

static	void	_vj_event_gatter_sample_info( veejay_t *v, int id )
{
	char description[SAMPLE_MAX_DESCR_LEN];
	int end_frame 	= sample_get_endFrame( id );
	int start_frame = sample_get_startFrame( id );
	char timecode[15];
	MPEG_timecode_t tc;

	mpeg_timecode( &tc, (end_frame - start_frame),
		mpeg_framerate_code( mpeg_conform_framerate(v->edit_list->video_fps) ),
		v->edit_list->video_fps );

	sprintf( timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h,tc.m,tc.s,tc.f );
	sample_get_description( id, description );

	int dlen = strlen(description);
	int tlen = strlen(timecode);	

	sprintf( _s_print_buf, 
		"%05d%03d%s%03d%s%02d%02d",
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
	bzero( source, 255 );
	vj_tag_get_source_name( id, source );
	vj_tag_get_description( id, description );
	
	int dlen = strlen( description );
	int tlen = strlen( source );
	sprintf( _s_print_buf,
		"%05d%03d%s%03d%s%02d%02d",
		(     3 + dlen + 3 + tlen + 2 + 2),
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

	bzero( _s_print_buf,SEND_BUF);

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
		sprintf( _s_print_buf, "%05d", 0 );
	SEND_MSG(v , _s_print_buf );
}

#ifdef USE_GDK_PIXBUF
void	vj_event_get_scaled_image		(	void *ptr,	const char format[],	va_list	ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[2];
	uint8_t *frame[3];
	char *str = NULL;
	P_A(args,str,format,ap);

	vj_perform_get_primary_frame( v, frame, 0);
	veejay_image_t *img = vj_picture_save_to_memory(
					frame,
					v->edit_list->video_width,
					v->edit_list->video_height,
					args[0],
					args[1],
					v->edit_list->pixel_format );
	 
	if(img)
	{
		GdkPixbuf *p = NULL;
		int w,h;
		if( img->scaled_image )
		{
			p =  (GdkPixbuf*) img->scaled_image;
			w = args[0]; h = args[1];
		}	
		else
		{
			p =  (GdkPixbuf*) img->image;
			w = v->edit_list->video_width;
			h = v->edit_list->video_height;
		}


		unsigned char *msg = gdk_pixbuf_get_pixels( p );
		unsigned char *con = (unsigned char*) vj_malloc(sizeof(unsigned char)  * 5 + (w * h * 3 ));
		sprintf(con, "%05d", (w * h * 3));
		veejay_memcpy( con + 5 , msg , (w * h * 3 ));
		vj_server_send(v->vjs[0], v->uc->current_link, con, 5 + (w*h*3));
		if(con) free(con);
		if(img->image )
			gdk_pixbuf_unref( (GdkPixbuf*) img->image );
		if(img->scaled_image)
			gdk_pixbuf_unref( (GdkPixbuf*) img->scaled_image );
		if(img)
			free(img);
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to get image");
		char empty[5];
		bzero(empty, 5 );
		SEND_MSG( v, empty );
	}
}
#endif
void	vj_event_send_sample_list		(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*)ptr;
	int args[1];
	int start_from_sample = 1;
	char cmd[300];
	char *str = NULL;
	int i,n;
	P_A(args,str,format,ap);

	if(args[0]>0) start_from_sample = args[0];

	bzero( _s_print_buf,SEND_BUF);
	sprintf(_s_print_buf, "%05d", 0);

	n = sample_size()-1;
	if( n >= 1 )
	{
		char line[308];
		bzero(_print_buf, SEND_BUF);
		for(i=start_from_sample; i <= n; i++)
		{
			if(sample_exists(i))
			{	
				char description[SAMPLE_MAX_DESCR_LEN];
				int end_frame = sample_get_endFrame(i);
				int start_frame = sample_get_startFrame(i);
				bzero(cmd, 300);

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
		sprintf(_s_print_buf, "%05d%s", strlen(_print_buf),_print_buf);
	}
	SEND_MSG(v, _s_print_buf);
}

void	vj_event_send_log			(	void *ptr,	const char format[],	va_list ap 	)
{
	veejay_t *v = (veejay_t*) ptr;
	int num_lines = 0;
	int str_len = 0;
	char *messages = NULL;
	bzero( _s_print_buf,SEND_BUF);

	messages = veejay_pop_messages( &num_lines, &str_len );

	if(str_len == 0 || num_lines == 0 )
		sprintf(_s_print_buf, "%06d", 0);
	else
		sprintf(_s_print_buf, "%06d%s", str_len, messages );
	if(messages)
		free(messages);	

	SEND_LOG_MSG( v, _s_print_buf );
}

void	vj_event_send_chain_entry		( 	void *ptr,	const char format[],	va_list ap	)
{
	char fline[255];
	char line[255];
	int args[2];
	char *str = NULL;
	int error = 1;
	veejay_t *v = (veejay_t*)ptr;
	P_A(args,str,format,ap);

	bzero(fline,255);
	sprintf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);

	if(args[0] == 0) 
		args[0] = v->uc->sample_id;

	if(SAMPLE_PLAYING(v))
	{
		if(args[1]==-1) args[1] = sample_get_selected_entry(args[0]);
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
			{
				params[p] = sample_get_effect_arg(args[0],args[1],p);
			}
			for(p = num_params; p < SAMPLE_MAX_PARAMETERS; p++)
			{
				params[p] = 0;
			}

			sprintf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
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
				sample_get_chain_channel(args[0],args[1]) 
			);				
			error = 0;
		}
	}
	
	if(STREAM_PLAYING(v))
	{
		if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
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

			sprintf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
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
				vj_tag_get_chain_channel(args[0],args[1])
			);				
			error = 0;
		}
	}


	FORMAT_MSG(fline,line);
	SEND_MSG(v, fline);
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
	{
		args[0] = v->uc->sample_id;
	}

	bzero( _s_print_buf,SEND_BUF);
	bzero( _print_buf, SEND_BUF);

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
	bzero(info_msg,sizeof(info_msg));
	bzero( _s_print_buf,SEND_BUF);

	snprintf(info_msg,sizeof(info_msg), "%04d %04d %01d %c %02.3f %1d %04d %06ld %02d %03ld %08ld",
		v->edit_list->video_width,
		v->edit_list->video_height,
		v->edit_list->video_inter,
		v->edit_list->video_norm,
		v->edit_list->video_fps,  
		v->edit_list->has_audio,
		v->edit_list->audio_bits,
		v->edit_list->audio_rate,
		v->edit_list->audio_chans,
		v->edit_list->num_video_files,
		v->edit_list->video_frames
		);	
	sprintf( _s_print_buf, "%03d%s",strlen(info_msg), info_msg);

	SEND_MSG(v,_s_print_buf);
}

void 	vj_event_send_editlist			(	void *ptr,	const char format[],	va_list ap	)
{
	veejay_t *v = (veejay_t*) ptr;
	editlist *el = v->edit_list;
	
	if( el->num_video_files <= 0 )
	{
		SEND_MSG( v, "00000000");
		return;
	}

	bzero( _s_print_buf, SEND_BUF );
	int b = 0;
	int nf = 0;
	char *msg = (char*) vj_el_write_line_ascii( v->edit_list, &b,&nf );
	sprintf( _s_print_buf, "%06d%s", b, msg );
	free(msg);

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
	bzero(device_list,512);

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

	// send frame (peer to peer) on demand
	vj_perform_send_primary_frame_s( v,0 );
}


void	vj_event_mcast_start				(	void *ptr,	const char format[],	va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
	if(!v->settings->use_vims_mcast)
		veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");	
	else
	{
		v->settings->mcast_frame_sender = 1;
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
	int i;
	veejay_t *v = (veejay_t*)ptr;
	char line[300];   
	char fline[300];
	bzero( _print_buf, SEND_BUF);
	bzero( _s_print_buf,SEND_BUF);

	for(i=1; i < MAX_EFFECTS; i++)
	{
		int effect_id = vj_effect_get_real_id(i);
		bzero(line, 300);
		if(effect_id > 0 && vj_effect_get_summary(i,line)==1)
		{
			sprintf(fline, "%03d%s",strlen(line),line);
			strcat( _print_buf, fline );

		}
		else
		{
			fprintf(stderr, "no matching effect for %d\n",i);	
			fprintf(stderr, "get summary returns with %d",
				vj_effect_get_summary(i,line));
		}
	}	
	sprintf( _s_print_buf, "%05d%s",strlen(_print_buf), _print_buf);

	SEND_MSG(v,_s_print_buf);
}



int vj_event_load_bundles(char *bundle_file)
{
	FILE *fd;
	char *event_name, *event_msg;
	char buf[65535];
	int event_id=0;
	if(!bundle_file) return -1;
	fd = fopen(bundle_file, "r");
	bzero(buf,65535);
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
	bzero(value,100);
	int mode = 0;
	

	P_A( args, value, format ,ap );

	if( args[0] < 0 || args[0] > VIMS_MAX )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "invalid event identifier specified");
		return;
	}


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

	if(args[0] == 0 )
		mode = 1;
	else
		mode = 2; // assign key symbol / key modifier


	if( mode == 1 )
	{
		vj_event_unregister_keyb_event( args[1],args[2] );
	}
	if( mode == 2 )
	{
		if(vj_event_register_keyb_event( args[0], args[1], args[2], value ))
		{
		 veejay_msg(VEEJAY_MSG_DEBUG, "Trigger VIMS %d with %d,%d (%s)",
			args[0],args[1],args[2], value );
		}
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
	bzero(s, 1024);
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
		veejay_msg(VEEJAY_MSG_DEBUG,"(VIMS) Bundle exists - replace ");
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
	bzero(filename,1024);
	P_A(args, filename, format, ap );
	veejay_t *v = (veejay_t*) ptr;

	char type[5];
	bzero(type,5); 


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
	bzero(filename,1024);
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


#ifdef HAVE_V4L
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
			(v->edit_list->video_norm == 'p' ? 1 : 0),
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
#endif


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

	bzero( _s_print_buf,SEND_BUF);

	id = args[0];
	char options[100];
	char prefix[4];
	bzero(prefix, 4 );
	bzero(options, 100);

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
			int effects_on = 0;
			
			vj_tag_get_v4l_properties(id,&brightness,&hue, &contrast, &color, &white );			
			effects_on = si->effect_toggle;
			
			sprintf( options,
		        "%05d%05d%05d%05d%05d%01d",
			    brightness,
			    hue,
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

